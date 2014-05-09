// Copyright (C) 2012  JINMEI Tatuya
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
// REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
// AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
// INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
// LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
// OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
// PERFORMANCE OF THIS SOFTWARE.

#include <query_context.h>
#include <query_repository.h>
#include <dispatcher.h>
#include <message_manager.h>
#include <asio_message_manager.h>

#include <util/buffer.h>

#include <exceptions/exceptions.h>
#include <dns/message.h>
#include <dns/rrclass.h>

#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/foreach.hpp>

#include <algorithm>
#include <istream>
#include <cassert>
#include <list>

#include <netinet/in.h>

using namespace std;
using namespace isc::util;
using namespace isc::dns;
using namespace Queryperf;
using boost::scoped_ptr;
using namespace boost::posix_time;
using boost::posix_time::seconds;

namespace {
class QueryEvent {
    typedef boost::function<void(qid_t, const Message*)> RestartCallback;
public:
    QueryEvent(MessageManager& mgr, qid_t qid, QueryContext* ctx,
               RestartCallback restart_callback) :
        ctx_(ctx), qid_(qid), restart_callback_(restart_callback),
        timer_(mgr.createMessageTimer(
                   boost::bind(&QueryEvent::queryTimerCallback, this))),
        tcp_sock_(NULL), tcp_rcvbuf_(NULL)
    {}

    ~QueryEvent() {
        delete tcp_sock_;
        delete ctx_;
        delete[] tcp_rcvbuf_;
    }

    QueryContext::QuerySpec start(qid_t qid, const time_duration& timeout) {
        assert(ctx_ != NULL);
        qid_ = qid;
        timer_->start(timeout);
        return (ctx_->start(qid_));
    }

    void* getTCPBuf() {
        if (tcp_rcvbuf_ == NULL) {
            tcp_rcvbuf_ = new uint8_t[TCP_RCVBUF_LEN];
        }
        return (tcp_rcvbuf_);
    }
    size_t getTCPBufLen() {
        return (TCP_RCVBUF_LEN);
    }

    qid_t getQid() const { return (qid_); }

    bool matchResponse(qid_t qid) const { return (qid_ == qid); }

    void setTCPSocket(MessageSocket* tcp_sock) {
        assert(tcp_sock_ == NULL);
        tcp_sock_ = tcp_sock;
    }

    void clearTCPSocket() {
        assert(tcp_sock_ != NULL);
        delete tcp_sock_;
        tcp_sock_ = NULL;
    }

private:
    void queryTimerCallback() {
        cout << "[Timeout] Query timed out: msg id: " << qid_ << endl;
        if (tcp_sock_ != NULL) {
            clearTCPSocket();
        }
        restart_callback_(qid_, NULL);
    }

    QueryContext* ctx_;
    qid_t qid_;
    RestartCallback restart_callback_;
    boost::shared_ptr<MessageTimer> timer_;
    MessageSocket* tcp_sock_;
    static const size_t TCP_RCVBUF_LEN = 65535;
    uint8_t* tcp_rcvbuf_;      // lazily allocated
};

typedef boost::shared_ptr<QueryEvent> QueryEventPtr;
} // unnamed namespace

namespace Queryperf {
struct Dispatcher::DispatcherImpl {
    DispatcherImpl(MessageManager& msg_mgr,
                   QueryContextCreator& ctx_creator) :
        msg_mgr_(&msg_mgr), qryctx_creator_(&ctx_creator),
        response_(Message::PARSE)
    {
        initParams();
    }

    DispatcherImpl(const string& data_file) :
        qry_repo_local_(new QueryRepository(data_file)),
        msg_mgr_local_(new ASIOMessageManager),
        qryctx_creator_local_(new QueryContextCreator(*qry_repo_local_)),
        msg_mgr_(msg_mgr_local_.get()),
        qryctx_creator_(qryctx_creator_local_.get()),
        response_(Message::PARSE)
    {
        initParams();
    }

    DispatcherImpl(istream& input_stream) :
        qry_repo_local_(new QueryRepository(input_stream)),
        msg_mgr_local_(new ASIOMessageManager),
        qryctx_creator_local_(new QueryContextCreator(*qry_repo_local_)),
        msg_mgr_(msg_mgr_local_.get()),
        qryctx_creator_(qryctx_creator_local_.get()),
        response_(Message::PARSE)
    {
        initParams();
    }

    void initParams() {
        keep_sending_ = true;
        window_ = DEFAULT_WINDOW;
        qid_ = 0;
        queries_sent_ = 0;
        queries_completed_ = 0;
        server_address_ = DEFAULT_SERVER;
        server_port_ = DEFAULT_PORT;
        test_duration_ = DEFAULT_DURATION;
        query_timeout_ = seconds(DEFAULT_QUERY_TIMEOUT);
    }

    void run();

    // Callback from the message manager called when a response to a query is
    // delivered.
    void responseCallback(const MessageSocket::Event& sockev);

    void responseTCPCallback(const MessageSocket::Event& sockev,
                             QueryEvent* qev);

    // Generate next query either due to completion or timeout.
    void restartQuery(qid_t qid, const Message* response);

    // A subroutine commonly used to send a single query.
    void sendQuery(QueryEvent& qev, const QueryContext::QuerySpec& qry_spec) {
        if (qry_spec.proto == IPPROTO_UDP) {
            udp_socket_->send(qry_spec.data, qry_spec.len);
        } else {
            MessageSocket* tcp_sock =
                msg_mgr_->createMessageSocket(
                    IPPROTO_TCP, server_address_, server_port_,
                    qev.getTCPBuf(), qev.getTCPBufLen(),
                    boost::bind(&DispatcherImpl::responseTCPCallback, this,
                                _1, &qev));
                qev.setTCPSocket(tcp_sock);
                tcp_sock->send(qry_spec.data, qry_spec.len);
        }

        ++queries_sent_;
        ++qid_;
    }

    // Callback from the message manager on expiration of the session timer.
    // Stop sending more queries; only wait for outstanding ones.
    void sessionTimerCallback() {
        keep_sending_ = false;
    }

    // These are placeholders for the support class objects when they are
    // built within the context.
    scoped_ptr<QueryRepository> qry_repo_local_;
    scoped_ptr<ASIOMessageManager> msg_mgr_local_;
    scoped_ptr<QueryContextCreator> qryctx_creator_local_;

    // These are pointers to the objects actually used in the object
    MessageManager* msg_mgr_;
    QueryContextCreator* qryctx_creator_;

    // Note that these should be placed after msg_mgr_local_; in the destructor
    // these should be released first.
    scoped_ptr<MessageSocket> udp_socket_;
    scoped_ptr<MessageTimer> session_timer_;
    uint8_t udp_recvbuf_[4096];

    // Configurable parameters
    string server_address_;
    uint16_t server_port_;
    size_t test_duration_;
    time_duration query_timeout_;

    bool keep_sending_; // whether to send next query on getting a response
    size_t window_;
    qid_t qid_;
    Message response_;          // placeholder for response messages
    list<boost::shared_ptr<QueryEvent> > outstanding_;

    // statistics
    size_t queries_sent_;
    size_t queries_completed_;
    ptime start_time_;
    ptime end_time_;
};

void
Dispatcher::DispatcherImpl::run() {
    // Allocate resources used throughout the test session:
    // common UDP socket and the whole session timer.
    udp_socket_.reset(msg_mgr_->createMessageSocket(
                          IPPROTO_UDP, server_address_, server_port_,
                          udp_recvbuf_, sizeof(udp_recvbuf_),
                          boost::bind(&DispatcherImpl::responseCallback,
                                      this, _1)));
    session_timer_.reset(msg_mgr_->createMessageTimer(
                             boost::bind(&DispatcherImpl::sessionTimerCallback,
                                         this)));

    // Start the session timer.
    session_timer_->start(seconds(test_duration_));

    // Create a pool of query contexts.  Setting QID to 0 for now.
    for (size_t i = 0; i < window_; ++i) {
        QueryEventPtr qev(new QueryEvent(
                              *msg_mgr_, 0, qryctx_creator_->create(),
                              boost::bind(&DispatcherImpl::restartQuery,
                                          this, _1, _2)));
        outstanding_.push_back(qev);
    }

    // Record the start time and dispatch initial queries at once.
    start_time_ = microsec_clock::local_time();
    BOOST_FOREACH(boost::shared_ptr<QueryEvent>& qev, outstanding_) {
        sendQuery(*qev, qev->start(qid_, query_timeout_));
    }

    // Enter the event loop.
    msg_mgr_->run();
}

void
Dispatcher::DispatcherImpl::responseCallback(
    const MessageSocket::Event& sockev)
{
    // Parse the header of the response
    InputBuffer buffer(sockev.data, sockev.datalen);
    response_.clear(Message::PARSE);
    response_.parseHeader(buffer);
    // TODO: catch exception due to bogus response

    restartQuery(response_.getQid(), &response_);
}

void
Dispatcher::DispatcherImpl::responseTCPCallback(
    const MessageSocket::Event& sockev, QueryEvent* qev)
{
    qev->clearTCPSocket();

    if (sockev.datalen > 0) {
        // Parse the header of the response
        InputBuffer buffer(sockev.data, sockev.datalen);
        response_.clear(Message::PARSE);
        response_.parseHeader(buffer);
    } else {
        cout << "[Fail] TCP connection terminated unexpectedly" << endl;
    }

    restartQuery(qev->getQid(), sockev.datalen > 0 ? &response_ : NULL);
}

void
Dispatcher::DispatcherImpl::restartQuery(qid_t qid, const Message* response) {
    // Identify the matching query from the outstanding queue.
    const list<boost::shared_ptr<QueryEvent> >::iterator qev_it =
        find_if(outstanding_.begin(), outstanding_.end(),
                boost::bind(&QueryEvent::matchResponse, _1, qid));
    if (qev_it != outstanding_.end()) {
        if (response != NULL) {
            // TODO: let the context check the response further
            ++queries_completed_;
        }

        // If necessary, create a new query and dispatch it.
        if (keep_sending_) {
            sendQuery(**qev_it, (*qev_it)->start(qid_, query_timeout_));

            // Move this context to the end of the queue.
            outstanding_.splice(qev_it, outstanding_, outstanding_.end());
        } else {
            outstanding_.erase(qev_it);
            if (outstanding_.empty()) {
                msg_mgr_->stop();
            }
        }
    } else {
        // TODO: record the mismatched response
    }
}

Dispatcher::Dispatcher(MessageManager& msg_mgr,
                       QueryContextCreator& ctx_creator) :
    impl_(new DispatcherImpl(msg_mgr, ctx_creator))
{
}

const char* const Dispatcher::DEFAULT_SERVER = "::1";

Dispatcher::Dispatcher(const string& data_file) {
    if (data_file == "-") {
        impl_ = new DispatcherImpl(cin);
    } else {
        impl_ = new DispatcherImpl(data_file);
    }
}

Dispatcher::Dispatcher(istream& input_stream) :
    impl_(new DispatcherImpl(input_stream))
{
}

Dispatcher::~Dispatcher() {
    delete impl_;
}

void
Dispatcher::loadQueries() {
    // Query preload must be done before running tests.
    if (!impl_->start_time_.is_special()) {
        throw DispatcherError("query load attempt after run");
    }
    // Preload can be used (via the dispatcher) only for the internal
    // repository.
    if (!impl_->qry_repo_local_) {
        throw DispatcherError("query load attempt for external repository");
    }

    impl_->qry_repo_local_->load();
}

void
Dispatcher::setDefaultQueryClass(const std::string& qclass_txt) {
    // default qclass must be set before running tests.
    if (!impl_->start_time_.is_special()) {
        throw DispatcherError("default query class is being set after run");
    }
    // qclass can be used (via the dispatcher) only for the internal
    // repository.
    if (!impl_->qry_repo_local_) {
        throw DispatcherError("default query class is being set "
                              "for external repository");
    }

    try {
        impl_->qry_repo_local_->setQueryClass(RRClass(qclass_txt));
    } catch (const isc::Exception&) {
        throw DispatcherError("invalid query class: " + qclass_txt);
    }
}

void
Dispatcher::setDNSSEC(bool on) {
    // This must be set before running tests.
    if (!impl_->start_time_.is_special()) {
        throw DispatcherError("DNSSEC DO bit is being set/reset after run");
    }
    // DNSSEC bit can be set (via the dispatcher) only for the internal
    // repository.
    if (!impl_->qry_repo_local_) {
        throw DispatcherError("DNSSEC DO bit is being set/reset "
                              "for external repository");
    }
    impl_->qry_repo_local_->setDNSSEC(on);
}

void
Dispatcher::setEDNS(bool on) {
    // This must be set before running tests.
    if (!impl_->start_time_.is_special()) {
        throw DispatcherError("EDNS flag is being set/reset after run");
    }
    // EDNS flag can be set (via the dispatcher) only for the internal
    // repository.
    if (!impl_->qry_repo_local_) {
        throw DispatcherError("EDNS flag bit is being set/reset "
                              "for external repository");
    }
    impl_->qry_repo_local_->setEDNS(on);
}

void
Dispatcher::run() {
    assert(impl_->udp_socket_ == NULL);
    impl_->run();
    impl_->end_time_ = microsec_clock::local_time();
}

string
Dispatcher::getServerAddress() const {
    return (impl_->server_address_);
}

void
Dispatcher::setServerAddress(const string& address) {
    if (!impl_->start_time_.is_special()) {
        throw DispatcherError("server address cannot be reset after run()");
    }
    impl_->server_address_ = address;
}

uint16_t
Dispatcher::getServerPort() const {
    return (impl_->server_port_);
}

void
Dispatcher::setServerPort(uint16_t port) {
    if (!impl_->start_time_.is_special()) {
        throw DispatcherError("server port cannot be reset after run()");
    }
    impl_->server_port_ = port;
}

void
Dispatcher::setProtocol(int proto) {
    // This must be set before running tests.
    if (!impl_->start_time_.is_special()) {
        throw DispatcherError("Default transport protocol cannot be set "
                              "after run()");
    }
    // Default transport protocol can be set (via the dispatcher) only for
    // the internal repository.
    if (!impl_->qry_repo_local_) {
        throw DispatcherError("Default transport protocol cannot be set "
                              "for external repository");
    }
    impl_->qry_repo_local_->setProtocol(proto);
}

size_t
Dispatcher::getTestDuration() const {
    return (impl_->test_duration_);
}

void
Dispatcher::setTestDuration(size_t duration) {
    if (!impl_->start_time_.is_special()) {
        throw DispatcherError("test duration cannot be reset after run()");
    }
    impl_->test_duration_ = duration;
}

size_t
Dispatcher::getQueriesSent() const {
    return (impl_->queries_sent_);
}

size_t
Dispatcher::getQueriesCompleted() const {
    return (impl_->queries_completed_);
}

const ptime&
Dispatcher::getStartTime() const {
    return (impl_->start_time_);
}

const ptime&
Dispatcher::getEndTime() const {
    return (impl_->end_time_);
}

} // end of QueryPerf

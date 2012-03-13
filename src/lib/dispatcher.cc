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

#include <dns/message.h>

#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/foreach.hpp>

#include <algorithm>
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
struct QueryEvent {
    QueryEvent(qid_t qid, QueryContext* ctx) :
        qid_(qid), ctx_(ctx)
    {}

    ~QueryEvent() {
        delete ctx_;
    }

    void reset() {
        ctx_ = NULL;
    }

    qid_t qid_;
    QueryContext* ctx_;
};
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

    void initParams() {
        keep_sending_ = true;
        window_ = DEFAULT_WINDOW;
        qid_ = 0;
        queries_sent_ = 0;
        queries_completed_ = 0;
        server_address_ = DEFAULT_SERVER;
        server_port_ = DEFAULT_PORT;
        test_duration_ = DEFAULT_DURATION;
    }

    void run();

    // Callback from the message manager called when a response to a query is
    // delivered.
    void responseCallback(const MessageSocket::Event& sockev);

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

    // Configurable parameters
    string server_address_;
    uint16_t server_port_;
    size_t test_duration_;

    bool keep_sending_; // whether to send next query on getting a response
    size_t window_;
    qid_t qid_;
    Message response_;          // placeholder for response messages
    list<QueryEvent> outstanding_;
    //list<> available_;

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
                          boost::bind(&DispatcherImpl::responseCallback,
                                      this, _1)));
    session_timer_.reset(msg_mgr_->createMessageTimer(
                             boost::bind(&DispatcherImpl::sessionTimerCallback,
                                         this)));

    // Start the session timer.
    session_timer_->start(seconds(test_duration_));

    // Create a pool of query contexts.  Setting QID to 0 for now.
    for (size_t i = 0; i < window_; ++i) {
        QueryEvent qev(0, qryctx_creator_->create());
        outstanding_.push_back(qev);
        qev.reset();
    }

    // Record the start time and dispatch initial queries at once.
    start_time_ = microsec_clock::local_time();
    BOOST_FOREACH(QueryEvent& qev, outstanding_) {
        QueryContext::WireData qry_data = qev.ctx_->start(qid_);
        qev.qid_ = qid_;
        udp_socket_->send(qry_data.data, qry_data.len);
        ++queries_sent_;
        ++qid_;
    }

    // Enter the event loop.
    msg_mgr_->run();
}

namespace {
bool
matchResponse(const QueryEvent& qev, qid_t qid) {
    return (qev.qid_ == qid);
}
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

    // Identify the matching query from the outstanding queue.
    const list<QueryEvent>::iterator qev_it =
        find_if(outstanding_.begin(), outstanding_.end(),
                boost::bind(matchResponse, _1, response_.getQid()));
    if (qev_it != outstanding_.end()) {
        // TODO: let the context check the response further

        ++queries_completed_;

        // If necessary, create a new query and dispatch it.
        if (keep_sending_) {
            QueryContext::WireData qry_data = qev_it->ctx_->start(qid_);
            qev_it->qid_ = qid_;
            udp_socket_->send(qry_data.data, qry_data.len);
            ++queries_sent_;
            ++qid_;

            // Move this context to the end of the queue.
            outstanding_.splice(qev_it, outstanding_, outstanding_.end());
        } else {
            outstanding_.erase(qev_it);
            if (outstanding_.empty()) {
                msg_mgr_->stop();
            }
        }
    } else {
        // TODO: record the mismatched resonse
    }
}

Dispatcher::Dispatcher(MessageManager& msg_mgr,
                       QueryContextCreator& ctx_creator) :
    impl_(new DispatcherImpl(msg_mgr, ctx_creator))
{
}

const char* const Dispatcher::DEFAULT_SERVER = "::1";

Dispatcher::Dispatcher(const string& data_file) :
    impl_(new DispatcherImpl(data_file))
{
}

Dispatcher::~Dispatcher() {
    delete impl_;
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

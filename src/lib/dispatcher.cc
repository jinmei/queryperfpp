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
#include <dispatcher.h>
#include <message_manager.h>

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
        keep_sending_(true), window_(DEFAULT_WINDOW), qid_(0),
        response_(Message::PARSE), udp_socket_(NULL), msg_mgr_(msg_mgr),
        ctx_creator_(ctx_creator)
    {}

    void run();

    // Callback from the message manager called when a response to a query is
    // delivered.
    void responseCallback(const MessageSocket::Event& sockev);

    // Callback from the message manager on expiration of the session timer.
    // Stop sending more queries; only wait for outstanding ones.
    void sessionTimerCallback() {
        keep_sending_ = false;
    }

    bool keep_sending_; // whether to send next query on getting a response
    size_t window_;
    qid_t qid_;
    Message response_;          // placeholder for response messages
    scoped_ptr<MessageSocket> udp_socket_;
    scoped_ptr<MessageTimer> session_timer_;
    list<QueryEvent> outstanding_;
    //list<> available_;
    MessageManager& msg_mgr_;
    QueryContextCreator& ctx_creator_;
};

void
Dispatcher::DispatcherImpl::run() {
    // Allocate resources used throughout the test session:
    // common UDP socket and the whole session timer.
    udp_socket_.reset(msg_mgr_.createMessageSocket(
                          IPPROTO_UDP, "::1", 5300,
                          boost::bind(&DispatcherImpl::responseCallback,
                                      this, _1)));
    session_timer_.reset(msg_mgr_.createMessageTimer(
                             boost::bind(&DispatcherImpl::sessionTimerCallback,
                                         this)));

    // Start the session timer.
    session_timer_->start(seconds(DEFAULT_DURATION));

    // Create a pool of query contexts.  Setting QID to 0 for now.
    for (size_t i = 0; i < window_; ++i) {
        QueryEvent qev(0, ctx_creator_.create());
        outstanding_.push_back(qev);
        qev.reset();
    }
    // Dispatch initial queries at once.
    BOOST_FOREACH(QueryEvent& qev, outstanding_) {
        QueryContext::WireData qry_data = qev.ctx_->start(qid_);
        qev.qid_ = qid_;
        udp_socket_->send(qry_data.data, qry_data.len);
        ++qid_;
    }

    // Enter the event loop.
    msg_mgr_.run();
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

        // If necessary, create a new query and dispatch it.
        if (keep_sending_) {
            QueryContext::WireData qry_data = qev_it->ctx_->start(qid_);
            qev_it->qid_ = qid_;
            udp_socket_->send(qry_data.data, qry_data.len);
            ++qid_;

            // Move this context to the end of the queue.
            outstanding_.splice(qev_it, outstanding_, outstanding_.end());
        } else {
            outstanding_.erase(qev_it);
            if (outstanding_.empty()) {
                msg_mgr_.stop();
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

Dispatcher::~Dispatcher() {
    delete impl_;
}

void
Dispatcher::run() {
    assert(impl_->udp_socket_ == NULL);
    impl_->run();
}

} // end of QueryPerf

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

#include <algorithm>
#include <cassert>
#include <list>

#include <netinet/in.h>

using namespace std;
using namespace isc::util;
using namespace isc::dns;
using namespace Queryperf;

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
        window_(DEFAULT_WINDOW), qid_(0), response_(Message::PARSE),
        udp_socket_(NULL), msg_mgr_(msg_mgr), ctx_creator_(ctx_creator)
    {}

    void run();

    // Callback from the message manager called when a response to a query is
    // delivered.
    void responseCallback(const MessageSocket::Event& sockev);

    size_t window_;
    qid_t qid_;
    Message response_;          // placeholder for response messages
    MessageSocket* udp_socket_;
    list<QueryEvent> outstanding_;
    //list<> available_;
    MessageManager& msg_mgr_;
    QueryContextCreator& ctx_creator_;
};

void
Dispatcher::DispatcherImpl::run() {
    udp_socket_ =
        msg_mgr_.createMessageSocket(IPPROTO_UDP, "::1", 5300,
                                     boost::bind(
                                         &DispatcherImpl::responseCallback,
                                         this, _1));

    // Create pooled query contexts and dispatch initial queries.
    for (size_t i = 0; i < window_; ++i) {
        // Note: this leaks right now
        QueryEvent qev(qid_, ctx_creator_.create());
        QueryContext::WireData qry_data = qev.ctx_->start(qid_++);
        udp_socket_->send(qry_data.data, qry_data.len);
        outstanding_.push_back(qev);
        qev.reset();
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
    InputBuffer buffer(sockev.data, sockev.datalen);
    response_.clear(Message::PARSE);
    response_.parseHeader(buffer);
    // TODO: catch exception due to bogus response

    const list<QueryEvent>::iterator qev_it =
        find_if(outstanding_.begin(), outstanding_.end(),
                boost::bind(matchResponse, _1, response_.getQid()));
    if (qev_it != outstanding_.end()) {
        // TODO: let the context check the response further

        // Create a new query and dispatch it.
        QueryContext::WireData qry_data = qev_it->ctx_->start(qid_);
        qev_it->qid_ = qid_;
        udp_socket_->send(qry_data.data, qry_data.len);
        qid_++;

        // Move this context to the end of the queue.
        outstanding_.splice(qev_it, outstanding_, outstanding_.end());
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

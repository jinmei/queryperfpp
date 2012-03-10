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

#include <dns/message.h>

#include <boost/bind.hpp>

#include <cassert>
#include <list>

#include <netinet/in.h>

using namespace std;
using namespace isc::dns;

namespace {
struct QueryEvent {
};
} // unnamed namespace

namespace Queryperf {

struct Dispatcher::DispatcherImpl {
    DispatcherImpl(MessageManager& msg_mgr,
                   QueryContextCreator& ctx_creator) :
        window_(DEFAULT_WINDOW), qid_(0), udp_socket_(NULL),
        msg_mgr_(msg_mgr), ctx_creator_(ctx_creator)
    {}

    void run();

    // Callback from the message manager called when a response to a query is
    // delivered.
    void responseCallback(const MessageSocket::Event&) {
        // empty for now
    }

    size_t window_;
    qid_t qid_;
    MessageSocket* udp_socket_;
    //list<> outstanding_;
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
        QueryContext* query_ctx = ctx_creator_.create();
        QueryContext::WireData qry_data = query_ctx->start(qid_++);
        udp_socket_->send(qry_data.data, qry_data.len);
    }

    // Enter the event loop.
    msg_mgr_.run();
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

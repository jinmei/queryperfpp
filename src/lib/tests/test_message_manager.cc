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

#include <test_message_manager.h>

#include <util/buffer.h>
#include <dns/message.h>

#include <gtest/gtest.h>

#include <boost/shared_ptr.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>

#include <cassert>
#include <memory>
#include <stdexcept>

#include <netinet/in.h>

using namespace isc::util;
using namespace isc::dns;

using boost::shared_ptr;

namespace {
// safeguard limit to prevent infinite loop in run() due to a buggy test
const size_t MAX_RUN_LOOP = 1000;
}

namespace Queryperf {
namespace unittest {

TestMessageSocket::~TestMessageSocket() {
    ++manager_->n_deleted_sockets_;
}

void
TestMessageSocket::send(const void* data, size_t datalen) {
    InputBuffer buffer(data, datalen);
    shared_ptr<Message> query_msg(new Message(Message::PARSE));
    query_msg->fromWire(buffer);
    queries_.push_back(query_msg);
}

void
TestMessageTimer::start(const boost::posix_time::time_duration& duration) {
    ++n_started_;
    duration_seconds_ = duration.seconds();
}

void
TestMessageTimer::cancel() {
    // ignore this, do nothing
}

MessageSocket*
TestMessageManager::createMessageSocket(int proto,
                                        const std::string&, uint16_t,
                                        void*, size_t,
                                        MessageSocket::Callback callback)
{
    TestMessageSocket* ret;
    if (proto == IPPROTO_UDP) {
        if (socket_ != NULL) {
            throw std::runtime_error("duplicate socket creation");
        }
        socket_ = new TestMessageSocket(callback);
        ret = socket_;
    } else {
        assert(proto == IPPROTO_TCP);
        std::auto_ptr<TestMessageSocket> p(new TestMessageSocket(callback));
        tcp_sockets_.push_back(p.get());
        ret = p.release();   // give the ownership
    }

    ret->manager_ = this;
    return (ret);
}

MessageTimer*
TestMessageManager::createMessageTimer(MessageTimer::Callback callback) {
    std::auto_ptr<TestMessageTimer> p(new TestMessageTimer(callback));
    timers_.push_back(p.get());
    return (p.release()); // give the ownership
}

void
TestMessageManager::run() {
    if (running_) {
        throw std::runtime_error("Test message manager: duplicate run");
    }
    running_ = true;

    size_t count = 0;
    while (running_) {
        ASSERT_GT(MAX_RUN_LOOP, count++);
        if (run_handler_) {
            run_handler_();
        } else {
            break;
        }
    }
}

void
TestMessageManager::stop() {
    if (!running_) {
        throw std::runtime_error("Test message manager: stopping before start");
    }
    running_ = false;
}

} // end of unittest
} // end of QueryPerf

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

#include <stdexcept>

using namespace std;
using namespace isc::util;
using namespace isc::dns;

using boost::shared_ptr;

namespace {
// safeguard limit to prevent infinite loop in run() due to a buggy test
const size_t MAX_RUN_LOOP = 1000;
}

namespace Queryperf {
namespace unittest {

void
TestMessageSocket::send(const void* data, size_t datalen) {
    InputBuffer buffer(data, datalen);
    shared_ptr<Message> query_msg(new Message(Message::PARSE));
    query_msg->fromWire(buffer);
    queries_.push_back(query_msg);

    // Immediatelly respond with the query itself.
    callback_(Event(data, datalen));
}

MessageSocket*
TestMessageManager::createMessageSocket(int, const std::string&, uint16_t,
                                        MessageSocket::Callback callback)
{
    if (socket_) {
        throw runtime_error("duplicate socket creation");
    }
    socket_.reset(new TestMessageSocket(callback));
    return (socket_.get());
}

void
TestMessageManager::run() {
    size_t count = 0;
    while (true) {
        ASSERT_GT(MAX_RUN_LOOP, count++);
        if (run_handler_) {
            run_handler_();
        } else {
            break;
        }
    }
}

} // end of unittest
} // end of QueryPerf

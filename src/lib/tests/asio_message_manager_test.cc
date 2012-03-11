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

#include <asio_message_manager.h>

#include <gtest/gtest.h>

#include <boost/bind.hpp>
#include <boost/scoped_ptr.hpp>

#include <cstring>

#include <netinet/in.h>

using namespace std;
using namespace Queryperf;
using boost::scoped_ptr;

namespace {
class ASIOMessageManagerTest : public ::testing::Test {
protected:
    ASIOMessageManagerTest() {}

    ASIOMessageManager asio_manager;

    void sendCallback(const MessageSocket::Event&) {}
};

TEST_F(ASIOMessageManagerTest, createMessageSocketIPv6) {
    scoped_ptr<ASIOMessageSocket> sock(
        dynamic_cast<ASIOMessageSocket*>(
            asio_manager.createMessageSocket(
                IPPROTO_UDP, "::1", 5300,
                boost::bind(&ASIOMessageManagerTest::sendCallback,
                            this, _1))));
    ASSERT_TRUE(sock);
    const int s =  sock->native();
    EXPECT_NE(-1, s);

    // The socket should be already bound (and connected)
    struct sockaddr_in6 sin6;
    memset(&sin6, 0, sizeof(sin6));
    socklen_t salen = sizeof(sin6);
    EXPECT_NE(-1, getsockname(s, static_cast<struct sockaddr*>(
                                  static_cast<void*>(&sin6)),
                              &salen));
    EXPECT_NE(0, sin6.sin6_port);
}

TEST_F(ASIOMessageManagerTest, createMessageSocketIPv4) {
    scoped_ptr<ASIOMessageSocket> sock(
        dynamic_cast<ASIOMessageSocket*>(
            asio_manager.createMessageSocket(
                IPPROTO_UDP, "127.0.0.1", 5304,
                boost::bind(&ASIOMessageManagerTest::sendCallback,
                            this, _1))));
    ASSERT_TRUE(sock);
    const int s =  sock->native();
    EXPECT_NE(-1, s);

    // The socket should be already bound (and connected)
    struct sockaddr_in sin4;
    memset(&sin4, 0, sizeof(sin4));
    socklen_t salen = sizeof(sin4);
    EXPECT_NE(-1, getsockname(s, static_cast<struct sockaddr*>(
                                  static_cast<void*>(&sin4)),
                              &salen));
    EXPECT_NE(0, sin4.sin_port);
}

TEST_F(ASIOMessageManagerTest, createMessageSocketBadParam) {
    // TCP is not (yet) supported
    EXPECT_THROW(asio_manager.createMessageSocket(
                     IPPROTO_TCP, "::1", 5300,
                     boost::bind(&ASIOMessageManagerTest::sendCallback, this,
                                 _1)),
                 MessageSocketError);

    // Bad address
    EXPECT_THROW(asio_manager.createMessageSocket(
                     IPPROTO_UDP, "127.0.0..1", 5300,
                     boost::bind(&ASIOMessageManagerTest::sendCallback, this,
                                 _1)),
                 MessageSocketError);

    // Null callback
    EXPECT_THROW(asio_manager.createMessageSocket(IPPROTO_UDP, "127.0.0.1",
                                                  5300, NULL),
                 MessageSocketError);
}
} // unnamed namespace

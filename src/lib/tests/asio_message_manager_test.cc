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

// Many helper methods and classes for tests are derived from BIND 10 tests,
// whose copyright notice follow:
// Copyright (C) 2011  Internet Systems Consortium, Inc. ("ISC")
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
#include <boost/noncopyable.hpp>

#include <cstring>
#include <string>
#include <utility>

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <netdb.h>

using namespace std;
using namespace Queryperf;
using boost::scoped_ptr;

namespace {
const char TEST_DATA[] = "queryperf test";

// A simple helper structure to automatically close test sockets on return
// or exception in a RAII manner.  non copyable to prevent duplicate close.
struct ScopedSocket : boost::noncopyable {
    ScopedSocket() : fd(-1) {}
    ScopedSocket(int sock) : fd(sock) {}
    ~ScopedSocket() {
        closeSocket();
    }
    void reset(int sock) {
        closeSocket();
        fd = sock;
    }
    int fd;
private:
    void closeSocket() {
        if (fd >= 0) {
            close(fd);
        }
    }
};

// Lower level C-APIs require conversion between various variants of
// sockaddr's, which is not friendly with C++.  The following templates
// are a shortcut of common workaround conversion in such cases.

template <typename SAType>
const struct sockaddr*
convertSockAddr(const SAType* sa) {
    const void* p = sa;
    return (static_cast<const struct sockaddr*>(p));
}

template <typename SAType>
const SAType*
convertSockAddr(const struct sockaddr* sa) {
    const void* p = sa;
    return (static_cast<const SAType*>(p));
}

template <typename SAType>
struct sockaddr*
convertSockAddr(SAType* sa) {
    void* p = sa;
    return (static_cast<struct sockaddr*>(p));
}

template <typename SAType>
SAType*
convertSockAddr(struct sockaddr* sa) {
    void* p = sa;
    return (static_cast<SAType*>(p));
}

// A helper to impose some reasonable amount of wait on recv(from)
// if possible.  It returns an option flag to be set for the system call
// (when necessary).
int
setRecvDelay(int s) {
    const struct timeval timeo = { 10, 0 };
    if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeo, sizeof(timeo)) == -1) {
        if (errno == ENOPROTOOPT) {
            // Workaround for Solaris: see recursive_query_unittest
            return (MSG_DONTWAIT);
        } else {
            throw runtime_error(string("set RCVTIMEO failed: ") +
                                strerror(errno));
        }
    }
    return (0);
}

// A shortcut type that is convenient to be used for socket related
// system calls, which generally require this pair
typedef pair<const struct sockaddr*, socklen_t> SockAddrInfo;

// A helper class to convert textual representation of IP address and port
// to a pair of sockaddr and its length (in the form of a SockAddrInfo
// pair).  Its get method uses getaddrinfo(3) for the conversion and stores
// the result in the addrinfo_list_ vector until the object is destructed.
// The allocated resources will be automatically freed in an RAII manner.
class SockAddrCreator {
public:
    ~SockAddrCreator() {
        vector<struct addrinfo*>::const_iterator it;
        for (it = addrinfo_list_.begin(); it != addrinfo_list_.end(); ++it) {
            freeaddrinfo(*it);
        }
    }
    SockAddrInfo get(const string& addr_str, const string& port_str) {
        struct addrinfo hints, *res;
        memset(&hints, 0, sizeof(hints));
        hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM; // could be either DGRAM or STREAM here
        const int error = getaddrinfo(addr_str.c_str(), port_str.c_str(),
                                      &hints, &res);
        if (error != 0) {
            throw runtime_error("getaddrinfo failed for " + addr_str + ", " +
                                port_str + ": " + gai_strerror(error));
        }

        // Technically, this is not entirely exception safe; if push_back
        // throws, the resources allocated for 'res' will leak.  We prefer
        // brevity here and ignore the minor failure mode.
        addrinfo_list_.push_back(res);

        return (SockAddrInfo(res->ai_addr, res->ai_addrlen));
    }
private:
    vector<struct addrinfo*> addrinfo_list_;
};

// An empty call back for MessageSocket::send.  Used when we don't have
// to test the callback behavior.
void
noopCallback(const MessageSocket::Event&) {
}

class ASIOMessageManagerTest : public ::testing::Test {
protected:
    ASIOMessageManagerTest() : sendcallback_called_(0), send_done_(0) {}

    // A convenient shortcut for the namespace-scope version of getSockAddr
    SockAddrInfo getSockAddr(const string& addr_str, const string& port_str) {
        return (addr_creator_.get(addr_str, port_str));
    }

    // Common callback for the message socket.
    void sendCallback(const MessageSocket::Event& ev) {
        ++sendcallback_called_;
        EXPECT_EQ(sizeof(TEST_DATA), ev.datalen);
        EXPECT_STREQ(TEST_DATA, static_cast<const char*>(ev.data));
        if (send_done_ == sendcallback_called_) {
            asio_manager_.stop();
        }
    }

    // A helper method that creates a specified type of socket that is
    // supposed to be passed via a SocketSessionForwarder.  It will bound
    // to the specified address and port in sainfo.  If do_listen is true
    // and it's a TCP socket, it will also start listening to new connection
    // requests.
    int createSocket(int family, int type, int protocol,
                     const SockAddrInfo& sainfo, bool do_listen = false)
    {
        int s = socket(family, type, protocol);
        if (s < 0) {
            throw runtime_error(string("socket(2) failed: ") +
                                strerror(errno));
        }
        const int on = 1;
        if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) {
            throw runtime_error(string("setsockopt(SO_REUSEADDR) failed: ") +
                                strerror(errno));
        }
        if (bind(s, sainfo.first, sainfo.second) < 0) {
            close(s);
            throw runtime_error(string("bind(2) failed: ") + strerror(errno));
        }
        if (do_listen && protocol == IPPROTO_TCP) {
            if (listen(s, 1) == -1) {
                throw runtime_error(string("listen(2) failed: ") +
                                    strerror(errno));
            }
        }
        return (s);
    }

    void sendCheck(int recv_fd, const string& addr, uint16_t port,
                   MessageSocket::Callback callback);

    size_t sendcallback_called_;
    size_t send_done_;
    ASIOMessageManager asio_manager_;
    scoped_ptr<MessageSocket> test_sock_;

private:
    SockAddrCreator addr_creator_;
};

TEST_F(ASIOMessageManagerTest, createMessageSocketIPv6) {
    scoped_ptr<ASIOMessageSocket> sock(
        dynamic_cast<ASIOMessageSocket*>(
            asio_manager_.createMessageSocket(
                IPPROTO_UDP, "::1", 5300, noopCallback)));
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
            asio_manager_.createMessageSocket(
                IPPROTO_UDP, "127.0.0.1", 5304, noopCallback)));
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
    EXPECT_THROW(asio_manager_.createMessageSocket(
                     IPPROTO_TCP, "::1", 5300, noopCallback),
                 MessageSocketError);

    // Bad address
    EXPECT_THROW(asio_manager_.createMessageSocket(
                     IPPROTO_UDP, "127.0.0..1", 5300, noopCallback),
                 MessageSocketError);

    // Null callback
    EXPECT_THROW(asio_manager_.createMessageSocket(IPPROTO_UDP, "127.0.0.1",
                                                  5300, NULL),
                 MessageSocketError);
}

void
ASIOMessageManagerTest::sendCheck(int recv_fd, const string& addr,
                                  uint16_t port,
                                  MessageSocket::Callback callback =
                                  noopCallback)
{
    // Create a socket on the manager if not created
    if (!test_sock_) {
        test_sock_.reset(asio_manager_.createMessageSocket(
                             IPPROTO_UDP, addr, port, callback));
    }

    // Send data from the first socket, and receive on the second one.
    test_sock_->send(TEST_DATA, sizeof(TEST_DATA));
    ++send_done_;

    char recvbuf[sizeof(TEST_DATA)];
    sockaddr_storage ss;
    socklen_t sa_len = sizeof(ss);
    EXPECT_EQ(sizeof(TEST_DATA), recvfrom(recv_fd, recvbuf, sizeof(recvbuf),
                                          setRecvDelay(recv_fd),
                                          convertSockAddr(&ss), &sa_len));
    EXPECT_STREQ(TEST_DATA, recvbuf);

    // Then echo back the received data to the sender (which may or may not be
    // used in the test)
    EXPECT_EQ(sizeof(TEST_DATA),
              sendto(recv_fd, recvbuf, sizeof(recvbuf), 0,
                     convertSockAddr(&ss), sa_len));
}

TEST_F(ASIOMessageManagerTest, send) {
    ScopedSocket recv_s(createSocket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP,
                                     getSockAddr("::1", "5306")));
    sendCheck(recv_s.fd, "::1", 5306);

    test_sock_.reset(NULL);
    recv_s.reset(createSocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP,
                              getSockAddr("127.0.0.1", "5304")));
    sendCheck(recv_s.fd, "127.0.0.1", 5304);
}

TEST_F(ASIOMessageManagerTest, sendCallbackIPv6) {
    ScopedSocket recv_s(createSocket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP,
                                     getSockAddr("::1", "5306")));
    EXPECT_EQ(0, sendcallback_called_);
    sendCheck(recv_s.fd, "::1", 5306,
              boost::bind(&ASIOMessageManagerTest::sendCallback, this,
                          _1));
    EXPECT_EQ(0, sendcallback_called_); // callback still shouldn't be called
    asio_manager_.run();
    EXPECT_EQ(1, sendcallback_called_);
}

TEST_F(ASIOMessageManagerTest, sendCallbackIPv4) {
    // same test as the previous one, for IPv4.
    ScopedSocket recv_s(createSocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP,
                                     getSockAddr("127.0.0.1", "5304")));
    EXPECT_EQ(0, sendcallback_called_);
    sendCheck(recv_s.fd, "127.0.0.1", 5304,
              boost::bind(&ASIOMessageManagerTest::sendCallback, this,
                          _1));
    EXPECT_EQ(0, sendcallback_called_);
    asio_manager_.run();
    EXPECT_EQ(1, sendcallback_called_);
}

TEST_F(ASIOMessageManagerTest, multipleSends) {
    ScopedSocket recv_s(createSocket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP,
                                     getSockAddr("::1", "5306")));
    EXPECT_EQ(0, sendcallback_called_);
    sendCheck(recv_s.fd, "::1", 5306,
              boost::bind(&ASIOMessageManagerTest::sendCallback, this,
                          _1));
    sendCheck(recv_s.fd, "::1", 5306,
              boost::bind(&ASIOMessageManagerTest::sendCallback, this,
                          _1));
    asio_manager_.run();
    EXPECT_EQ(2, sendcallback_called_);
}
} // unnamed namespace

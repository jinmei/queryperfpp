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
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/noncopyable.hpp>
#include <boost/lexical_cast.hpp>

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
using boost::lexical_cast;
using namespace boost::posix_time;
using boost::posix_time::seconds;
using boost::posix_time::ptime;

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
noopSocketCallback(const MessageSocket::Event&) {
}

// Same for the timer
void
noopTimerCallback() {
}

class ASIOMessageManagerTest : public ::testing::Test {
public:
    ASIOMessageManagerTest() : sendcallback_called_(0),
                               timercallback_called_(0),
                               helpercallback_called_(0),
                               send_done_(0)
    {}

    // A convenient shortcut for the namespace-scope version of getSockAddr
    SockAddrInfo getSockAddr(const string& addr_str, const string& port_str) {
        return (addr_creator_.get(addr_str, port_str));
    }

    // Common callback for the message socket.
    void sendCallback(const MessageSocket::Event& ev) {
        ++sendcallback_called_;
        // In the TCP test, a complete response message hasn't be sent
        // until callbackForTCPTest is called at least 4 times.  See that
        // function.
        // (helpercallback_called_ should be 0 for UDP)
        if (helpercallback_called_ > 0 && helpercallback_called_ <= 3) {
            EXPECT_EQ(0, ev.datalen);
        } else {
            EXPECT_EQ(sizeof(TEST_DATA), ev.datalen);
            EXPECT_STREQ(TEST_DATA, static_cast<const char*>(ev.data));
        }
        if (send_done_ == sendcallback_called_) {
            asio_manager_.stop();
        }
    }

    void timerCallback() {
        ++timercallback_called_;
    }

    // A helper method that creates a specified type of socket that is
    // supposed to be passed via a SocketSessionForwarder.  It will bound
    // to the specified address and port in sainfo.  If do_listen is true
    // and it's a TCP socket, it will also start listening to new connection
    // requests.
    int createSocket(int family, int type, int protocol,
                     const SockAddrInfo& sainfo, bool do_listen = true)
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

    void sendUDPCheck(int recv_fd, const string& addr, uint16_t port,
                      MessageSocket::Callback callback);
    void sendTCPCheck(int recv_fd, int udp_af, const string& addr,
                      const string& port, size_t callback_limit);
    void callbackForTCPTest(const MessageSocket::Event& ev,
                            int tcp_fd, int udp_fd, size_t callback_limit);

    size_t sendcallback_called_;
    size_t timercallback_called_;
    size_t helpercallback_called_; // # of times callbackForTCPTest is called
    size_t send_done_;
    ASIOMessageManager asio_manager_;
    scoped_ptr<MessageSocket> test_sock_;
    scoped_ptr<MessageSocket> udp_sock_; // auxiliary socket used in TCP test
    scoped_ptr<MessageTimer> test_timer_;
    ScopedSocket accept_s_;
    uint8_t recvbuf_[65535];

private:
    SockAddrCreator addr_creator_;
};

TEST_F(ASIOMessageManagerTest, createMessageSocketIPv6) {
    scoped_ptr<ASIOMessageSocket> sock(
        dynamic_cast<ASIOMessageSocket*>(
            asio_manager_.createMessageSocket(
                IPPROTO_UDP, "::1", 5300, recvbuf_, sizeof(recvbuf_),
                noopSocketCallback)));
    ASSERT_TRUE(sock);
    const int s =  sock->native(); // note: this doesn't have to be closed
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

TEST_F(ASIOMessageManagerTest, createMessageSocketTCPIPv6) {
    scoped_ptr<ASIOMessageSocket> sock(
        dynamic_cast<ASIOMessageSocket*>(
            asio_manager_.createMessageSocket(
                IPPROTO_TCP, "::1", 5300, recvbuf_, sizeof(recvbuf_),
                noopSocketCallback)));
    ASSERT_TRUE(sock);
    // In the case of TCP, the underlying socket is not open until send()
    EXPECT_EQ(-1, sock->native());
}

TEST_F(ASIOMessageManagerTest, createMessageSocketIPv4) {
    scoped_ptr<ASIOMessageSocket> sock(
        dynamic_cast<ASIOMessageSocket*>(
            asio_manager_.createMessageSocket(
                IPPROTO_UDP, "127.0.0.1", 5304, recvbuf_, sizeof(recvbuf_),
                noopSocketCallback)));
    ASSERT_TRUE(sock);
    const int s =  sock->native();
    EXPECT_NE(-1, s);

    // Receive buffer size should be at least 32KB.
    int bufsize;
    socklen_t optlen = sizeof(bufsize);
    EXPECT_EQ(0, getsockopt(s, SOL_SOCKET, SO_RCVBUF, &bufsize, &optlen));
    EXPECT_LE(32768, bufsize);

    // The socket should be already bound (and connected)
    struct sockaddr_in sin4;
    memset(&sin4, 0, sizeof(sin4));
    socklen_t salen = sizeof(sin4);
    EXPECT_NE(-1, getsockname(s, static_cast<struct sockaddr*>(
                                  static_cast<void*>(&sin4)),
                              &salen));
    EXPECT_NE(0, sin4.sin_port);
}

TEST_F(ASIOMessageManagerTest, createMessageSocketTCPIPv4) {
    scoped_ptr<ASIOMessageSocket> sock(
        dynamic_cast<ASIOMessageSocket*>(
            asio_manager_.createMessageSocket(
                IPPROTO_TCP, "127.0.0.1", 5304, recvbuf_, sizeof(recvbuf_),
                noopSocketCallback)));
    ASSERT_TRUE(sock);
    // In the case of TCP, the underlying socket is not open until send()
    EXPECT_EQ(-1, sock->native());
}

TEST_F(ASIOMessageManagerTest, createMessageSocketBadParam) {
    // Unspecified protocol (assuming it's neither UDP or TCP)
    EXPECT_THROW(asio_manager_.createMessageSocket(
                     0, "::1", 5300, recvbuf_, sizeof(recvbuf_),
                     noopSocketCallback),
                 MessageSocketError);

    // Bad address
    EXPECT_THROW(asio_manager_.createMessageSocket(
                     IPPROTO_UDP, "127.0.0..1", 5300, recvbuf_,
                     sizeof(recvbuf_), noopSocketCallback),
                 MessageSocketError);

    // Null callback
    EXPECT_THROW(asio_manager_.createMessageSocket(IPPROTO_UDP, "127.0.0.1",
                                                   5300, recvbuf_,
                                                   sizeof(recvbuf_), NULL),
                 MessageSocketError);
}

void
ASIOMessageManagerTest::sendUDPCheck(int recv_fd, const string& addr,
                                     uint16_t port,
                                     MessageSocket::Callback callback =
                                     noopSocketCallback)
{
    // Create a socket on the manager if not created
    if (!test_sock_) {
        test_sock_.reset(asio_manager_.createMessageSocket(
                             IPPROTO_UDP, addr, port, recvbuf_,
                             sizeof(recvbuf_), callback));
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
    EXPECT_EQ(sizeof(TEST_DATA), sendto(recv_fd, recvbuf, sizeof(recvbuf), 0,
                     convertSockAddr(&ss), sa_len));
}

TEST_F(ASIOMessageManagerTest, sendUDP) {
    ScopedSocket recv_s(createSocket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP,
                                     getSockAddr("::1", "5306")));
    sendUDPCheck(recv_s.fd, "::1", 5306);

    test_sock_.reset(NULL);
    recv_s.reset(createSocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP,
                              getSockAddr("127.0.0.1", "5304")));
    sendUDPCheck(recv_s.fd, "127.0.0.1", 5304);
}

// This is a helper function used below.  It allows the test code to get
// the control back from the underlying ASIO service.  The sendto() call
// at the end of this function will cause callbackForTCPTest to be called
// from ASIO.
void
triggerCallback(MessageSocket& sender_sock, int recv_fd) {
    sender_sock.send(TEST_DATA, sizeof(TEST_DATA));

    char recvbuf[sizeof(TEST_DATA)];
    sockaddr_storage ss;
    socklen_t sa_len = sizeof(ss);
    EXPECT_EQ(sizeof(TEST_DATA), recvfrom(recv_fd, recvbuf, sizeof(recvbuf),
                                          setRecvDelay(recv_fd),
                                          convertSockAddr(&ss), &sa_len));
    EXPECT_STREQ(TEST_DATA, recvbuf);
    EXPECT_EQ(sizeof(TEST_DATA), sendto(recv_fd, recvbuf, sizeof(recvbuf), 0,
                     convertSockAddr(&ss), sa_len));
}

void
ASIOMessageManagerTest::callbackForTCPTest(const MessageSocket::Event&,
                                           int tcp_fd, int udp_fd,
                                           size_t callback_limit)
{
    ++helpercallback_called_;

    char lenbuf[2];
    ASSERT_GE(sizeof(lenbuf), 2);
    char databuf[sizeof(TEST_DATA)];
    const char* const DUMMY_DATA = "dummy data";

    switch (helpercallback_called_) {
    case 1:
        // First two octets store the message length
        EXPECT_EQ(2, recv(tcp_fd, lenbuf, 2, MSG_WAITALL));
        EXPECT_EQ(sizeof(TEST_DATA), lenbuf[0] * 256 + lenbuf[1]);

        // Then the following real data and check it.
        EXPECT_EQ(sizeof(TEST_DATA), recv(tcp_fd, databuf, sizeof(databuf),
                                          MSG_WAITALL));
        EXPECT_STREQ(TEST_DATA, databuf);
        break;
    case 2:
        // Confirm the client has made sure there would be no more query.
        EXPECT_EQ(0, recv(accept_s_.fd, lenbuf, 1, setRecvDelay(accept_s_.fd)));
        break;
    case 3:
        // Send response, 1st part (message length)
        lenbuf[0] = 0;
        lenbuf[1] = sizeof(TEST_DATA);
        EXPECT_EQ(sizeof(lenbuf), send(tcp_fd, lenbuf, sizeof(lenbuf), 0));
        break;
    case 4:
        // send response, 2nd part (the message)
        EXPECT_EQ(sizeof(TEST_DATA), send(tcp_fd, TEST_DATA, sizeof(TEST_DATA),
                                          0));
        break;
    default:
        // additional message.  Using different content so we can confirm
        // whether the first one will appear in the callback.
        lenbuf[0] = 0;
        lenbuf[1] = sizeof(DUMMY_DATA);
        EXPECT_EQ(sizeof(lenbuf), send(tcp_fd, lenbuf, sizeof(lenbuf), 0));
        EXPECT_EQ(sizeof(DUMMY_DATA), send(tcp_fd, DUMMY_DATA,
                                           sizeof(DUMMY_DATA), 0));
        break;
    }

    if (helpercallback_called_ == callback_limit) {
        // If we reached the upper limit of callback, we'll close the server
        // side connection.  This will trigger the callback at the client.
        accept_s_.reset(-1);
    } else {
        // Otherwise, we'll move to the next state in this callback.
        triggerCallback(*udp_sock_, udp_fd);
    }
}

void
ASIOMessageManagerTest::sendTCPCheck(int listen_fd, int udp_af,
                                     const string& addr,
                                     const string& port,
                                     size_t callback_limit)
{
    // Create a socket on the manager if not created
    if (!test_sock_) {
        test_sock_.reset(asio_manager_.createMessageSocket(
                             IPPROTO_TCP, addr, lexical_cast<uint16_t>(port),
                             recvbuf_, sizeof(recvbuf_),
                             boost::bind(
                                 &ASIOMessageManagerTest::sendCallback, this,
                                 _1)));
    }

    // Send data from the first socket, and receive on the second one.
    test_sock_->send(TEST_DATA, sizeof(TEST_DATA));
    ++send_done_;

    // Accept the connection
    sockaddr_storage ss;
    socklen_t sa_len = sizeof(ss);
    accept_s_.reset(accept(listen_fd, convertSockAddr(&ss), &sa_len));
    ASSERT_NE(-1, accept_s_.fd);

    // We use a helper UDP socket to internally handle our own events;
    // otherwise the test could get stuck within the ASIO main loop.
    ScopedSocket udp_s(createSocket(udp_af, SOCK_DGRAM, IPPROTO_UDP,
                                    getSockAddr(addr, port)));

    // Send data from the UDP test socket, receive on the UDP socket,
    // and then send the data back.  This way we can be called back from
    // the manager, and handle the TCP events.
    uint8_t udp_recvbuf[128];
    udp_sock_.reset(
        asio_manager_.createMessageSocket(
            IPPROTO_UDP, addr, lexical_cast<uint16_t>(port), udp_recvbuf,
            sizeof(udp_recvbuf),
            boost::bind(&ASIOMessageManagerTest::callbackForTCPTest, this,
                        _1, accept_s_.fd, udp_s.fd, callback_limit)));
    triggerCallback(*udp_sock_, udp_s.fd);

    // Then start the manager loop.  TCP message will be sent, and the socket
    // will wait for the response.
    asio_manager_.run();
}

TEST_F(ASIOMessageManagerTest, sendCallbackUDPIPv6) {
    ScopedSocket recv_s(createSocket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP,
                                     getSockAddr("::1", "5306")));
    EXPECT_EQ(0, sendcallback_called_);
    sendUDPCheck(recv_s.fd, "::1", 5306,
              boost::bind(&ASIOMessageManagerTest::sendCallback, this, _1));
    EXPECT_EQ(0, sendcallback_called_); // callback still shouldn't be called
    asio_manager_.run();
    EXPECT_EQ(1, sendcallback_called_);
}

TEST_F(ASIOMessageManagerTest, sendCallbackUDPIPv4) {
    // same test as the previous one, for IPv4.
    ScopedSocket recv_s(createSocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP,
                                     getSockAddr("127.0.0.1", "5304")));
    EXPECT_EQ(0, sendcallback_called_);
    sendUDPCheck(recv_s.fd, "127.0.0.1", 5304,
              boost::bind(&ASIOMessageManagerTest::sendCallback, this, _1));
    EXPECT_EQ(0, sendcallback_called_);
    asio_manager_.run();
    EXPECT_EQ(1, sendcallback_called_);
}

// Note: this test could block if the tested code has a bug.
TEST_F(ASIOMessageManagerTest, sendTCPIPv6) {
    ScopedSocket listen_s(createSocket(AF_INET6, SOCK_STREAM, IPPROTO_TCP,
                                       getSockAddr("::1", "5306")));
    sendTCPCheck(listen_s.fd, AF_INET6, "::1", "5306", 1);
    EXPECT_EQ(1, sendcallback_called_);
}

TEST_F(ASIOMessageManagerTest, sendTCPIPv6Partial) {
    // We'll stop sending after sending length
    ScopedSocket listen_s(createSocket(AF_INET6, SOCK_STREAM, IPPROTO_TCP,
                                       getSockAddr("::1", "5306")));
    sendTCPCheck(listen_s.fd, AF_INET6, "::1", "5306", 3);
    EXPECT_EQ(1, sendcallback_called_);
}

TEST_F(ASIOMessageManagerTest, sendTCPIPv6Complete) {
    ScopedSocket listen_s(createSocket(AF_INET6, SOCK_STREAM, IPPROTO_TCP,
                                       getSockAddr("::1", "5306")));
    // We'll call callbackForTCPTest 4 times, at which point the process will
    // be completed.
    sendTCPCheck(listen_s.fd, AF_INET6, "::1", "5306", 4);
    EXPECT_EQ(1, sendcallback_called_);
}

TEST_F(ASIOMessageManagerTest, sendTCPIPv6Multi) {
    ScopedSocket listen_s(createSocket(AF_INET6, SOCK_STREAM, IPPROTO_TCP,
                                       getSockAddr("::1", "5306")));
    // We'll call callbackForTCPTest one more than the previous case, which
    // will result in two response messages.
    sendTCPCheck(listen_s.fd, AF_INET6, "::1", "5306", 5);
    EXPECT_EQ(1, sendcallback_called_);
}

TEST_F(ASIOMessageManagerTest, sendTCPIPv4) {
    ScopedSocket listen_s(createSocket(AF_INET, SOCK_STREAM, IPPROTO_TCP,
                                       getSockAddr("127.0.0.1", "5304")));
    sendTCPCheck(listen_s.fd, AF_INET, "127.0.0.1", "5304", 1);
    EXPECT_EQ(1, sendcallback_called_);
}

TEST_F(ASIOMessageManagerTest, sendTCPIPv4Partial) {
    ScopedSocket listen_s(createSocket(AF_INET, SOCK_STREAM, IPPROTO_TCP,
                                       getSockAddr("127.0.0.1", "5304")));
    sendTCPCheck(listen_s.fd, AF_INET, "127.0.0.1", "5304", 3);
    EXPECT_EQ(1, sendcallback_called_);
}

TEST_F(ASIOMessageManagerTest, sendCallbackTCPIPv4) {
    ScopedSocket listen_s(createSocket(AF_INET, SOCK_STREAM, IPPROTO_TCP,
                                       getSockAddr("127.0.0.1", "5304")));
    sendTCPCheck(listen_s.fd, AF_INET, "127.0.0.1", "5304", 4);
    EXPECT_EQ(1, sendcallback_called_);
}

TEST_F(ASIOMessageManagerTest, sendTCPIPv4Multi) {
    ScopedSocket listen_s(createSocket(AF_INET, SOCK_STREAM, IPPROTO_TCP,
                                       getSockAddr("127.0.0.1", "5304")));
    sendTCPCheck(listen_s.fd, AF_INET, "127.0.0.1", "5304", 5);
    EXPECT_EQ(1, sendcallback_called_);
}

TEST_F(ASIOMessageManagerTest, multipleUDPSends) {
    ScopedSocket recv_s(createSocket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP,
                                     getSockAddr("::1", "5306")));
    EXPECT_EQ(0, sendcallback_called_);
    sendUDPCheck(recv_s.fd, "::1", 5306,
                 boost::bind(&ASIOMessageManagerTest::sendCallback, this,
                             _1));
    sendUDPCheck(recv_s.fd, "::1", 5306,
                 boost::bind(&ASIOMessageManagerTest::sendCallback, this,
                             _1));
    asio_manager_.run();
    EXPECT_EQ(2, sendcallback_called_);
}

TEST_F(ASIOMessageManagerTest, createMessageTimer) {
    test_timer_.reset(asio_manager_.createMessageTimer(noopTimerCallback));
    EXPECT_TRUE(test_timer_);
}

// Note: this test takes time, so disabled by default.
TEST_F(ASIOMessageManagerTest, DISABLED_startMessageTimer) {
    test_timer_.reset(asio_manager_.createMessageTimer(
                          boost::bind(&ASIOMessageManagerTest::timerCallback,
                                      this)));
    ASSERT_TRUE(test_timer_);
    const ptime start_tm = microsec_clock::local_time();
    test_timer_->start(seconds(1));
    EXPECT_EQ(0, timercallback_called_);
    asio_manager_.run();
    const ptime end_tm = microsec_clock::local_time();
    EXPECT_EQ(1, timercallback_called_);

    // Check the duration is in some conservative range for the specified timer
    const long duration = (end_tm - start_tm).total_microseconds();
    EXPECT_GE(1500000, duration);
    EXPECT_LE(500000, duration);
}

TEST_F(ASIOMessageManagerTest, cancelMessageTimer) {
    test_timer_.reset(asio_manager_.createMessageTimer(
                          boost::bind(&ASIOMessageManagerTest::timerCallback,
                                      this)));
    ASSERT_TRUE(test_timer_);
    test_timer_->start(seconds(1));
    test_timer_->cancel();
    asio_manager_.run();
    EXPECT_EQ(0, timercallback_called_);
}
} // unnamed namespace

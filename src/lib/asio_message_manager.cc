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

#include <message_manager.h>
#include <asio_message_manager.h>

#include <asio.hpp>

#include <boost/shared_array.hpp>
#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/lexical_cast.hpp>

#include <string>

#include <stdint.h>

using namespace std;
using asio::io_service;
using asio::ip::udp;
using asio::ip::tcp;
using boost::lexical_cast;

namespace Queryperf {

class UDPMessageSocket : public ASIOMessageSocket {
public:
    UDPMessageSocket(io_service& io_service, const string& address,
                     uint16_t port, void* recvbuf, size_t recvbuf_len,
                     Callback callback);
    virtual void send(const void* data, size_t datalen);
    virtual void cancel();

    virtual int native() { return (asio_sock_.native()); }

private:
    // The handler for ASIO receive operations on this socket.
    void handleRead(const asio::error_code& ec, size_t length);

private:
    udp::socket asio_sock_;
    Callback callback_;
    bool receiving_;
    void* recvbuf_;
    size_t recvbuf_len_;
};

UDPMessageSocket::UDPMessageSocket(io_service& io_service,
                                   const string& address, uint16_t port,
                                   void* recvbuf, size_t recvbuf_len,
                                   Callback callback) :
    asio_sock_(io_service), callback_(callback), receiving_(false),
    recvbuf_(recvbuf), recvbuf_len_(recvbuf_len)
{
    try {
        // connect the socket, which implicitly opens a new one.
        const udp::endpoint dest(asio::ip::address::from_string(address),
                                 port);
        asio_sock_.connect(dest);

        // make sure the receive buffer is large enough (32KB, derived from
        // the original queryperf)
        asio_sock_.set_option(asio::socket_base::receive_buffer_size(32768));
    } catch (const asio::system_error& e) {
        throw MessageSocketError(string("Failed to create a socket: ") +
                                 e.what());
    }
}

void
UDPMessageSocket::send(const void* data, size_t datalen) {
    asio::error_code ec;
    asio_sock_.send(asio::buffer(data, datalen), 0, ec);
    if (ec) {
        throw MessageSocketError(string("Unexpected failure on socket send: ")
                                 + ec.message());
    }
    if (!receiving_) {
        asio_sock_.async_receive(asio::buffer(recvbuf_, recvbuf_len_),
                                 boost::bind(&UDPMessageSocket::handleRead,
                                             this, _1, _2));
        receiving_ = true;
    }
}

void
UDPMessageSocket::cancel() {
    // In our usage we don't need this.  We can postpone implementing it.
    throw MessageSocketError("cancel on UDP socket is not supported yet");
}

void
UDPMessageSocket::handleRead(const asio::error_code& ec, size_t length) {
    if (ec) {
        throw MessageSocketError("unexpected failure on socket read: " +
                                 ec.message());
    }
    callback_(Event(recvbuf_, length));
    asio_sock_.async_receive(asio::buffer(recvbuf_, recvbuf_len_),
                             boost::bind(&UDPMessageSocket::handleRead,
                                         this, _1, _2));
}

class TCPMessageSocket : public ASIOMessageSocket {
public:
    TCPMessageSocket(ASIOMessageManager* manager,
                     io_service& io_service, const string& address,
                     uint16_t port, void* recvbuf, size_t recvbuf_len,
                     Callback callback);
    virtual void send(const void* data, size_t datalen);
    virtual void cancel();
    virtual int native() { return (asio_sock_.native()); }
    bool cancelCheck(const asio::error_code& ec) {
        if (!cancelled_) {
            return (false);
        }
        if (ec == asio::error::operation_aborted) {
            delete this;
        }
        return (true);
    }

private:
    void handleConnect(const asio::error_code& ec);
    void handleWrite(const asio::error_code& ec, size_t length);
    void handleReadLength(const asio::error_code& ec, size_t length);
    void handleReadData(const asio::error_code& ec, size_t length);

private:
    ASIOMessageManager* manager_;
    tcp::socket asio_sock_;
    asio::error_code asio_error_; // placeholder for getting ASIO error
    tcp::endpoint dest_;
    Callback callback_;
    void* recvbuf_;       // for the first message
    size_t recvbuf_len_;  // available size of recvbuf_
    size_t recvdata_len_; // actual message length of the first message
    uint8_t aux_recvbuf_[65535]; // placeholder for subsequent messages
    uint8_t msglen_placeholder_[2];
    boost::array<asio::const_buffer, 2> sendbufs_;
    bool cancelled_;
};

TCPMessageSocket::TCPMessageSocket(ASIOMessageManager* manager,
                                   io_service& io_service,
                                   const string& address, uint16_t port,
                                   void* recvbuf, size_t recvbuf_len,
                                   Callback callback) :
    manager_(manager), asio_sock_(io_service),
    dest_(asio::ip::address::from_string(address), port),
    callback_(callback), recvbuf_(recvbuf), recvbuf_len_(recvbuf_len),
    recvdata_len_(0), cancelled_(false)
{
    // Note: we don't even open the socket yet.
}

void
TCPMessageSocket::send(const void* data, size_t datalen) {
    msglen_placeholder_[0] = datalen >> 8;
    msglen_placeholder_[1] = (datalen & 0x00ff);
    sendbufs_[0] = asio::buffer(msglen_placeholder_,
                                sizeof(msglen_placeholder_));
    sendbufs_[1] = asio::buffer(data, datalen);

    // TBD: error check
#if 1
    asio_sock_.open(dest_.protocol());
    asio_sock_.set_option(tcp::acceptor::reuse_address(true));
    for (size_t i = 0; i < 10; ++i) {
        asio_sock_.bind(tcp::endpoint(dest_.address(),
                                      manager_->getNextTCPPort()),
                        asio_error_);
        if (!asio_error_) {
            break;
        }
    }
    if (asio_error_) {
        cerr << "[Warn] Failed to open TCP connection: "
             << asio_error_.message() << endl;
        // Note: we still cannot do callback; it could cause another call
        // to this method (recursively), and result in call stack overflow.
    }
#endif

    asio_sock_.async_connect(dest_,
                             boost::bind(&TCPMessageSocket::handleConnect,
                                         this, _1));
}

void
TCPMessageSocket::cancel() {
    assert(!cancelled_);
    asio_sock_.cancel();
    cancelled_ = true;
}

void
TCPMessageSocket::handleConnect(const asio::error_code& ec) {
    if (cancelCheck(ec)) {
        return;
    }
    if (ec) {
        cerr << "[Warn] TCP connect failed: " << ec.message() << endl;
        callback_(Event(NULL, 0));
        return;
    }
    asio::async_write(asio_sock_, sendbufs_,
                      boost::bind(&TCPMessageSocket::handleWrite, this,
                                  _1, _2));
}

void
TCPMessageSocket::handleWrite(const asio::error_code& ec, size_t) {
    if (cancelCheck(ec)) {
        return;
    }
    if (ec) {
        cerr << "[Warn] TCP send failed: " << ec.message() << endl;
        callback_(Event(NULL, 0));
        return;
    }
    // Immediately after sending the query, shutdown the outbound direction
    // of the socket, so the server won't wait for subsequent queries.
    asio_sock_.shutdown(tcp::socket::shutdown_send, asio_error_);
    if (asio_error_) {
        cerr << "[Warn] failed to shut down TCP socket: "
             << asio_error_.message() << endl;
        callback_(Event(NULL, 0));
        return;
    }

    // Then wait for the response.
    asio_sock_.async_receive(asio::buffer(msglen_placeholder_,
                                          sizeof(msglen_placeholder_)),
                             boost::bind(&TCPMessageSocket::handleReadLength,
                                         this, _1, _2));
}

void
TCPMessageSocket::handleReadLength(const asio::error_code& ec, size_t length) {
    if (cancelCheck(ec)) {
        return;
    }
    if (ec == asio::error::eof) {
        // We've received all messages.  Note that this includes the case
        // where the server closes the connection without sending any message
        // or with partial message.
        callback_(Event(recvbuf_, recvdata_len_));
        return;
    }
    if (ec) {
        cerr << "[Warn] failed to read TCP message length: "
             << ec.message() << endl;
        callback_(Event(NULL, 0));
        return;
    }
    if (length != sizeof(msglen_placeholder_)) {
        throw MessageSocketError("received unexpected size of data for "
                                 "msglen: " + lexical_cast<string>(length));
    }
    const uint16_t msglen = msglen_placeholder_[0] * 256 +
        msglen_placeholder_[1];
    assert(sizeof(aux_recvbuf_) >= msglen);
    // Now we are going to receive the main message.  We keep the first
    // message in recvbuf_ for callback, and hold others in the aux
    // buffer only temporarily.
    asio_sock_.async_receive(asio::buffer(
                                 recvdata_len_ == 0 ? recvbuf_ : aux_recvbuf_,
                                 msglen),
                             boost::bind(&TCPMessageSocket::handleReadData,
                                         this, _1, _2));
}

void
TCPMessageSocket::handleReadData(const asio::error_code& ec, size_t length) {
    if (cancelCheck(ec)) {
        return;
    }
    if (ec == asio::error::eof) {
        // We've received all messages.  This is an unexpected connection
        // termination by the server.  Do the callback with what we've had
        // so far anyway.
        callback_(Event(recvbuf_, recvdata_len_));
        return;
    }
    if (ec) {
        cerr << "[Warn] failed to read TCP message: " << ec.message() << endl;
        callback_(Event(NULL, recvdata_len_));
        return;
    }
    // If this is the first message, remember its length.
    if (recvdata_len_ == 0) {
        recvdata_len_ = length;
    }

    // There may be more messages, like in the case for AXFR or large IX FR
    // For now, we'll simply read and discard any subsequent message until
    // the server closes the connection, at which point we return the control
    // to the original caller with a callback.
    asio_sock_.async_receive(asio::buffer(msglen_placeholder_,
                                          sizeof(msglen_placeholder_)),
                             boost::bind(&TCPMessageSocket::handleReadLength,
                                         this, _1, _2));
}

struct ASIOMessageManager::ASIOMessageManagerImpl {
    ASIOMessageManagerImpl() :
        tcp_port_(LOWEST_TCP_PORT)
    {}

    // This is the lowest non privileged port
    static const uint16_t LOWEST_TCP_PORT = 1024;

    uint16_t getNextTCPPort() {
        const uint16_t port = tcp_port_;
        if ((port % 10000) == 0) {
            cout << "10K TCP ports examined: " << port << endl;
        }
        tcp_port_ = (port == 65535) ? LOWEST_TCP_PORT : (port + 1);
        return (port);
    }

    io_service io_service_;
    uint16_t tcp_port_; // A TCP port which is likely to be available
};

ASIOMessageManager::ASIOMessageManager() :
    impl_(new ASIOMessageManagerImpl)
{}

ASIOMessageManager::~ASIOMessageManager() {
    delete impl_;
}

MessageSocket*
ASIOMessageManager::createMessageSocket(int proto, const string& address,
                                        uint16_t port,
                                        void* recvbuf, size_t recvbuf_len,
                                        MessageSocket::Callback callback)
{
    if (!callback) {
        throw MessageSocketError("null socket callback specified");
    }
    if (proto == IPPROTO_UDP) {
        return (new UDPMessageSocket(impl_->io_service_, address, port,
                                     recvbuf, recvbuf_len, callback));
    } else if (proto == IPPROTO_TCP) {
        if (recvbuf_len < 65535) { // must be able to hold a full TCP msg
            throw MessageSocketError("Insufficient TCP receive buffer");
        }
        return (new TCPMessageSocket(this, impl_->io_service_, address, port,
                                     recvbuf, recvbuf_len, callback));
    }
    throw MessageSocketError("unsupported or invalid protocol: " +
                             lexical_cast<string>(proto));
}

class ASIOMessageTimer : public MessageTimer, private boost::noncopyable {
public:
    ASIOMessageTimer(asio::io_service& io_service,
                     Callback callback) :
        asio_timer_(io_service), callback_(callback)
    {}
    virtual void start(const boost::posix_time::time_duration& duration);

    virtual void cancel() { asio_timer_.cancel(); }

private:
    // The handler for ASIO timer expiration
    void handleExpire(const asio::error_code& ec) {
        if (ec == asio::error::operation_aborted) {
            return;             // we ignore cancel event
        } else if (ec) {
            throw MessageTimerError(string("Unexpected error on timer: ") +
                                    ec.message());
        }
        callback_();
    }

private:
    asio::deadline_timer asio_timer_;
    Callback callback_;
};

void
ASIOMessageTimer::start(const boost::posix_time::time_duration& duration) {
    asio::error_code ec;
    asio_timer_.expires_from_now(duration, ec);
    if (ec) {
        throw MessageTimerError(string("Unexpected failure on setting timer: ")
                                + ec.message());
    }
    asio_timer_.async_wait(boost::bind(&ASIOMessageTimer::handleExpire, this,
                                       _1));
}

MessageTimer*
ASIOMessageManager::createMessageTimer(MessageTimer::Callback callback) {
    return (new ASIOMessageTimer(impl_->io_service_, callback));
}

void
ASIOMessageManager::run() {
    impl_->io_service_.run();
}

void
ASIOMessageManager::stop() {
    impl_->io_service_.stop();
}

uint16_t
ASIOMessageManager::getNextTCPPort() {
    return (impl_->getNextTCPPort());
}

} // end of QueryPerf

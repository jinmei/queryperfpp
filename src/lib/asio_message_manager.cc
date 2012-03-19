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
                     uint16_t port, Callback callback);
    virtual void send(const void* data, size_t datalen);

    virtual int native() { return (asio_sock_.native()); }

private:
    // The handler for ASIO receive operations on this socket.
    void handleRead(const asio::error_code& ec, size_t length);

private:
    udp::socket asio_sock_;
    Callback callback_;
    bool receiving_;
    uint8_t recvbuf_[4096];
};

UDPMessageSocket::UDPMessageSocket(io_service& io_service,
                                   const string& address,
                                   uint16_t port, Callback callback) :
    asio_sock_(io_service), callback_(callback), receiving_(false)
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
        asio_sock_.async_receive(asio::buffer(recvbuf_, sizeof(recvbuf_)),
                                 boost::bind(&UDPMessageSocket::handleRead,
                                             this, _1, _2));
        receiving_ = true;
    }
}

void
UDPMessageSocket::handleRead(const asio::error_code& ec, size_t length) {
    if (ec) {
        throw MessageSocketError("unexpected failure on socket read: " +
                                 ec.message());
    }
    callback_(Event(recvbuf_, length));
    asio_sock_.async_receive(asio::buffer(recvbuf_, sizeof(recvbuf_)),
                             boost::bind(&UDPMessageSocket::handleRead,
                                         this, _1, _2));
}

class TCPMessageSocket : public ASIOMessageSocket {
public:
    TCPMessageSocket(io_service& io_service, const string& address,
                     uint16_t port, Callback callback);
    virtual void send(const void* data, size_t datalen);

    virtual int native() { return (asio_sock_.native()); }

private:
    tcp::socket asio_sock_;
    tcp::endpoint dest_;
    Callback callback_;
    uint8_t recvbuf_[4096];
};

TCPMessageSocket::TCPMessageSocket(io_service& io_service,
                                   const string& address, uint16_t port,
                                   Callback callback) :
    asio_sock_(io_service),
    dest_(asio::ip::address::from_string(address), port), callback_(callback)
{
    // Note: we don't even open the socket yet.
}

void
TCPMessageSocket::send(const void* /*data*/, size_t /*datalen*/) {
}

struct ASIOMessageManager::ASIOMessageManagerImpl {
    io_service io_service_;
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
                                        MessageSocket::Callback callback)
{
    if (!callback) {
        throw MessageSocketError("null socket callback specified");
    }
    if (proto == IPPROTO_UDP) {
        return (new UDPMessageSocket(impl_->io_service_, address, port,
                                     callback));
    } else if (proto == IPPROTO_TCP) {
        return (new TCPMessageSocket(impl_->io_service_, address, port,
                                     callback));
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

} // end of QueryPerf

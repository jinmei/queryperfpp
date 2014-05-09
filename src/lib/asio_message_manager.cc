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

#include <config.h>

#include <message_manager.h>
#include <asio_message_manager.h>

#ifdef HAVE_NONBOOST_ASIO
#include <asio.hpp>
#else
#include <boost/asio.hpp>
#endif

#include <boost/shared_array.hpp>
#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/lexical_cast.hpp>

#include <memory>
#include <limits>
#include <string>
#include <iostream>

#include <stdint.h>

#ifdef HAVE_NONBOOST_ASIO
using namespace asio;
#else
using namespace boost::asio;
using boost::system::error_code;
using boost::system::system_error;
#endif
using boost::lexical_cast;

namespace Queryperf {

class ASIOMessageSocket::ASIOMessageSocketImpl {
protected:
    ASIOMessageSocketImpl() {}
public:
    virtual ~ASIOMessageSocketImpl() {}
    virtual void send(const void* data, size_t datalen) = 0;
    virtual void cancel() = 0;
    virtual int native() = 0;
};

namespace {
class UDPMessageSocket : public ASIOMessageSocket::ASIOMessageSocketImpl {
public:
    UDPMessageSocket(io_service& io_service, const std::string& address,
                     uint16_t port, void* recvbuf, size_t recvbuf_len,
                     MessageSocket::Callback callback);
    virtual void send(const void* data, size_t datalen);
    virtual void cancel() {     // in our simplified usage, this is enough
        delete this;
    }

    virtual int native() { return (asio_sock_.native()); }

private:
    // The handler for ASIO receive operations on this socket.
    void handleRead(const error_code& ec, size_t length);

private:
    ip::udp::socket asio_sock_;
    MessageSocket::Callback callback_;
    bool receiving_;
    void* recvbuf_;
    size_t recvbuf_len_;
};

UDPMessageSocket::UDPMessageSocket(io_service& io_service,
                                   const std::string& address, uint16_t port,
                                   void* recvbuf, size_t recvbuf_len,
                                   MessageSocket::Callback callback) :
    asio_sock_(io_service), callback_(callback), receiving_(false),
    recvbuf_(recvbuf), recvbuf_len_(recvbuf_len)
{
    try {
        // connect the socket, which implicitly opens a new one.
        const ip::udp::endpoint dest(ip::address::from_string(address), port);
        asio_sock_.connect(dest);

        // make sure the receive buffer is large enough (32KB, derived from
        // the original queryperf)
        asio_sock_.set_option(socket_base::receive_buffer_size(32768));
    } catch (const system_error& e) {
        throw MessageSocketError(std::string("Failed to create a socket: ") +
                                 e.what());
    }
}

void
UDPMessageSocket::send(const void* data, size_t datalen) {
    error_code ec;
    asio_sock_.send(buffer(data, datalen), 0, ec);
    if (ec) {
        throw MessageSocketError(
            std::string("Unexpected failure on socket send: ") + ec.message());
    }
    if (!receiving_) {
        asio_sock_.async_receive(buffer(recvbuf_, recvbuf_len_),
                                 boost::bind(&UDPMessageSocket::handleRead,
                                             this, _1, _2));
        receiving_ = true;
    }
}

void
UDPMessageSocket::handleRead(const error_code& ec, size_t length) {
    if (ec) {
        throw MessageSocketError("unexpected failure on socket read: " +
                                 ec.message());
    }
    callback_(MessageSocket::Event(recvbuf_, length));
    asio_sock_.async_receive(buffer(recvbuf_, recvbuf_len_),
                             boost::bind(&UDPMessageSocket::handleRead,
                                         this, _1, _2));
}

class TCPMessageSocket : public ASIOMessageSocket::ASIOMessageSocketImpl {
public:
    TCPMessageSocket(io_service& io_service, const std::string& address,
                     uint16_t port, void* recvbuf,
                     MessageSocket::Callback callback);
    ~TCPMessageSocket() { delete aux_recvbuf_; }
    virtual void send(const void* data, size_t datalen);
    virtual void cancel();
    virtual int native() { return (asio_sock_.native()); }

private:
    void handleConnect(const error_code& ec);
    void handleWrite(const error_code& ec, size_t length);
    void handleReadLength(const error_code& ec, size_t length);
    void handleReadData(const error_code& ec, size_t length);

    bool cancelCheck(const error_code& ec) {
        if (!cancelled_) {
            return (false);
        }
        if (ec == error::operation_aborted) {
            delete this;
        }
        return (true);
    }

    void sendCallback(const void* callback_data, size_t data_len) {
        completed_ = true;
        callback_(MessageSocket::Event(callback_data, data_len));
    }

private:
    //ASIOMessageManager* manager_;
    ip::tcp::socket asio_sock_;
    error_code asio_error_; // placeholder for getting ASIO error
    ip::tcp::endpoint dest_;
    MessageSocket::Callback callback_;
    void* recvbuf_;       // for the first message (must be of > 64KB)
    size_t recvdata_len_; // actual message length of the first message
    uint8_t* aux_recvbuf_; // placeholder for subsequent messages
    uint8_t msglen_placeholder_[2];
    boost::array<const_buffer, 2> sendbufs_;
    bool cancelled_;
    bool completed_;
};

TCPMessageSocket::TCPMessageSocket(io_service& io_service,
                                   const std::string& address, uint16_t port,
                                   void* recvbuf,
                                   MessageSocket::Callback callback) :
    asio_sock_(io_service),
    dest_(ip::address::from_string(address), port),
    callback_(callback), recvbuf_(recvbuf), recvdata_len_(0),
    aux_recvbuf_(NULL), cancelled_(false), completed_(false)
{
    // Note: we don't even open the socket yet.
}

void
TCPMessageSocket::send(const void* data, size_t datalen) {
    msglen_placeholder_[0] = datalen >> 8;
    msglen_placeholder_[1] = (datalen & 0x00ff);
    sendbufs_[0] = buffer(msglen_placeholder_,
                                sizeof(msglen_placeholder_));
    sendbufs_[1] = buffer(data, datalen);
    asio_sock_.async_connect(dest_,
                             boost::bind(&TCPMessageSocket::handleConnect,
                                         this, _1));
}

void
TCPMessageSocket::cancel() {
    if (asio_sock_.is_open() && !completed_) {
        // Initiate delayed abort.
        assert(!cancelled_);
        asio_sock_.cancel();
        cancelled_ = true;
    } else {
        // If it's not even opened yet, there's nothing to do.  Just kill
        // itself.
        delete this;
    }
}

void
TCPMessageSocket::handleConnect(const error_code& ec) {
    if (cancelCheck(ec)) {
        return;
    }
    if (ec) {
        std::cerr << "[Warn] TCP connect failed: " << ec.message() << std::endl;
        sendCallback(NULL, 0);
        return;
    }
    async_write(asio_sock_, sendbufs_,
                boost::bind(&TCPMessageSocket::handleWrite, this, _1, _2));
}

void
TCPMessageSocket::handleWrite(const error_code& ec, size_t) {
    if (cancelCheck(ec)) {
        return;
    }
    if (ec) {
        std::cerr << "[Warn] TCP send failed: " << ec.message() << std::endl;
        sendCallback(NULL, 0);
        return;
    }
    // Immediately after sending the query, shutdown the outbound direction
    // of the socket, so the server won't wait for subsequent queries.
    asio_sock_.shutdown(ip::tcp::socket::shutdown_send, asio_error_);
    if (asio_error_) {
        std::cerr << "[Warn] failed to shut down TCP socket: "
                  << asio_error_.message() << std::endl;
        sendCallback(NULL, 0);
        return;
    }

    // Then wait for the response.
    asio_sock_.async_receive(buffer(msglen_placeholder_,
                                    sizeof(msglen_placeholder_)),
                             boost::bind(&TCPMessageSocket::handleReadLength,
                                         this, _1, _2));
}

void
TCPMessageSocket::handleReadLength(const error_code& ec, size_t length) {
    if (cancelCheck(ec)) {
        return;
    }
    if (ec == error::eof) {
        // We've received all messages.  Note that this includes the case
        // where the server closes the connection without sending any message
        // or with partial message.
        sendCallback(recvbuf_, recvdata_len_);
        return;
    }
    if (ec) {
        std::cerr << "[Warn] failed to read TCP message length: "
                  << ec.message() << std::endl;
        sendCallback(NULL, 0);
        return;
    }
    if (length != sizeof(msglen_placeholder_)) {
        throw MessageSocketError("received unexpected size of data for "
                                 "msglen: " +
                                 lexical_cast<std::string>(length));
    }
    const uint16_t msglen = msglen_placeholder_[0] * 256 +
        msglen_placeholder_[1];
    // Now we are going to receive the main message.  We keep the first
    // message in recvbuf_ for callback, and hold others in the aux
    // buffer only temporarily.
    if (recvdata_len_ > 0) {
        aux_recvbuf_ = new uint8_t[std::numeric_limits<uint16_t>::max()];
    }
    asio_sock_.async_receive(buffer(
                                 recvdata_len_ == 0 ? recvbuf_ : aux_recvbuf_,
                                 msglen),
                             boost::bind(&TCPMessageSocket::handleReadData,
                                         this, _1, _2));
}

void
TCPMessageSocket::handleReadData(const error_code& ec, size_t length) {
    if (cancelCheck(ec)) {
        return;
    }
    if (ec == error::eof) {
        // We've received all messages.  This is an unexpected connection
        // termination by the server.  Do the callback with what we've had
        // so far anyway.
        sendCallback(recvbuf_, recvdata_len_);
        return;
    }
    if (ec) {
        std::cerr << "[Warn] failed to read TCP message: " << ec.message()
                  << std::endl;
        sendCallback(NULL, recvdata_len_);
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
    asio_sock_.async_receive(buffer(msglen_placeholder_,
                                    sizeof(msglen_placeholder_)),
                             boost::bind(&TCPMessageSocket::handleReadLength,
                                         this, _1, _2));
}
} // end of unnamed namespace

ASIOMessageSocket::~ASIOMessageSocket() {
    // The ownership is being released from the caller.  The underlying
    // impl object will be responsible for destructing itself.
    impl_->cancel();
    impl_ = NULL;               // not necessary, but just in case
}

void
ASIOMessageSocket::send(const void* data, size_t datalen) {
    impl_->send(data, datalen);
}

int
ASIOMessageSocket::native() {
    return (impl_->native());
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
ASIOMessageManager::createMessageSocket(int proto, const std::string& address,
                                        uint16_t port,
                                        void* recvbuf, size_t recvbuf_len,
                                        MessageSocket::Callback callback)
{
    MessageSocket* ret;

    if (!callback) {
        throw MessageSocketError("null socket callback specified");
    }
    if (proto == IPPROTO_UDP) {
        std::auto_ptr<UDPMessageSocket> impl_p(
            new UDPMessageSocket(impl_->io_service_, address, port,
                                 recvbuf, recvbuf_len, callback));
        ret = new ASIOMessageSocket(impl_p.get());
        impl_p.release();
        return (ret);
    } else if (proto == IPPROTO_TCP) {
        if (recvbuf_len < 65535) { // must be able to hold a full TCP msg
            throw MessageSocketError("Insufficient TCP receive buffer");
        }
        std::auto_ptr<TCPMessageSocket> impl_p(
            new TCPMessageSocket(impl_->io_service_, address, port, recvbuf,
                                 callback));
        ret = new ASIOMessageSocket(impl_p.get());
        impl_p.release();
        return (ret);
    }
    throw MessageSocketError("unsupported or invalid protocol: " +
                             lexical_cast<std::string>(proto));
}

class ASIOMessageTimer : public MessageTimer {
public:
    ASIOMessageTimer(io_service& io_service, Callback callback) :
        asio_timer_(io_service), callback_(callback)
    {}
    virtual void start(const boost::posix_time::time_duration& duration);

    virtual void cancel() { asio_timer_.cancel(); }

private:
    // The handler for ASIO timer expiration
    void handleExpire(const error_code& ec) {
        if (ec == error::operation_aborted) {
            return;             // we ignore cancel event
        } else if (ec) {
            throw MessageTimerError(std::string("Unexpected error on timer: ") +
                                    ec.message());
        }
        callback_();
    }

private:
    deadline_timer asio_timer_;
    Callback callback_;
};

void
ASIOMessageTimer::start(const boost::posix_time::time_duration& duration) {
    error_code ec;
    asio_timer_.expires_from_now(duration, ec);
    if (ec) {
        throw MessageTimerError(
            std::string("Unexpected failure on setting timer: ") +
            ec.message());
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

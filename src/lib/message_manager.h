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

#ifndef __QUERYPERF_MESSAGE_MANAGER_H
#define __QUERYPERF_MESSAGE_MANAGER_H 1

#include <boost/noncopyable.hpp>
#include <boost/function.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>

#include <string>
#include <stdexcept>

#include <stdint.h>

namespace Queryperf {

/// \brief Exception class thrown on socket related errors.
class MessageSocketError : public std::runtime_error {
public:
    explicit MessageSocketError(const std::string& what_arg) :
        std::runtime_error(what_arg)
    {}
};

/// \brief Exception class thrown on timer related errors.
class MessageTimerError : public std::runtime_error {
public:
    explicit MessageTimerError(const std::string& what_arg) :
        std::runtime_error(what_arg)
    {}
};

class MessageSocket : private boost::noncopyable {
public:
    struct Event {
        Event(const void* data_param, size_t datalen_param) :
            data(data_param), datalen(datalen_param)
        {}
        const void* const data;
        const size_t datalen;
    };
    typedef boost::function<void(Event)> Callback;

protected:
    MessageSocket() {}

public:
    virtual ~MessageSocket() {}

    virtual void send(const void* data, size_t datalen) = 0;
};

/// \brief Timers that work with a \c MessageManager.
class MessageTimer : private boost::noncopyable {
public:
    typedef boost::function<void()> Callback;

protected:
    MessageTimer() {}

public:
    virtual ~MessageTimer() {}
    virtual void start(const boost::posix_time::time_duration& duration) = 0;
    virtual void cancel() = 0;
};

class MessageManager : private boost::noncopyable {
protected:
    MessageManager() {}

public:
    virtual ~MessageManager() {}

    /// \param proto Transport protocol of the socket.  Either IPPROTO_UDP
    ///        or IPPROTO_TCP.
    /// \param address Textual representation of the destination (IPv6 or
    ///        IPv4) address.
    /// \param port The destination UDP or TCP port.
    /// \param callback The callback function or functor that is to be called
    ///        when a complete response is received on the socket.
    virtual MessageSocket* createMessageSocket(
        int proto, const std::string& address, uint16_t port,
        void* recvbuf, size_t recvbuf_len,
        MessageSocket::Callback callback) = 0;

    /// \brief Create a timer object.
    virtual MessageTimer* createMessageTimer(
        MessageTimer::Callback callback) = 0;

    /// \brief Start the main event loop.
    virtual void run() = 0;

    /// \brief Stop the event loop.
    virtual void stop() = 0;
};

} // end of QueryPerf

#endif // __QUERYPERF_MESSAGE_MANAGER_H 

// Local Variables:
// mode: c++
// End:

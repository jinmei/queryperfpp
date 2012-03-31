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

#ifndef __QUERYPERF_TEST_MESSAGE_MANAGER_H
#define __QUERYPERF_TEST_MESSAGE_MANAGER_H 1

#include <message_manager.h>

#include <dns/message.h>

#include <boost/noncopyable.hpp>
#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>

#include <string>
#include <vector>

#include <stdint.h>

namespace Queryperf {
namespace unittest {

// In this module we expose most of the member variables to the tests for
// convenience.  Data encapsulation isn't much important here.

class TestMessageManager;

class TestMessageSocket : public MessageSocket {
public:
    friend class TestMessageManager;
    TestMessageSocket(Callback callback) : callback_(callback),
                                           manager_(NULL)
    {}
    ~TestMessageSocket();
    virtual void send(const void* data, size_t datalen);

    std::vector<boost::shared_ptr<isc::dns::Message> > queries_;
    Callback callback_;

private:
    TestMessageManager* manager_;
};

class TestMessageTimer : public MessageTimer {
public:
    TestMessageTimer(Callback callback) :
        callback_(callback), n_started_(0), duration_seconds_(0)
    {}

    virtual void start(const boost::posix_time::time_duration& duration);
    virtual void cancel();

    Callback callback_;
    unsigned int n_started_;    // number of times started
    long duration_seconds_;
};

class TestMessageManager : public MessageManager {
public:
    typedef boost::function<void()> Handler;

    TestMessageManager() : socket_(NULL),
                           n_deleted_sockets_(0), running_(false) {}

    virtual MessageSocket* createMessageSocket(
        int proto, const std::string& address, uint16_t port,
        void* recvbuf, size_t recvbuf_len,
        MessageSocket::Callback callback);

    virtual MessageTimer* createMessageTimer(MessageTimer::Callback callback);

    virtual void run();

    virtual void stop();

    void setRunHandler(Handler handler) { run_handler_ = handler; }

    // Use a fixed internal UDP socket object.
    TestMessageSocket* socket_;

    // TCP sockets
    std::vector<TestMessageSocket*> tcp_sockets_;
    size_t n_deleted_sockets_;

    // Timers created in this manager.
    std::vector<TestMessageTimer*> timers_;

private:
    // run() Callback.  It delegates the control to the corresponding test.
    Handler run_handler_;

    bool running_;
};

} // end of unittest
} // end of QueryPerf

#endif // __QUERYPERF_TEST_MESSAGE_MANAGER_H 

// Local Variables:
// mode: c++
// End:

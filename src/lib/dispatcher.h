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

#ifndef __QUERYPERF_DISPATCHER_H
#define __QUERYPERF_DISPATCHER_H 1

#include <query_context.h>

#include <boost/noncopyable.hpp>

namespace Queryperf {

class Dispatcher : private boost::noncopyable {
public:
    // parameters eventually taken:
    //   window size, test duration, server adress, server port
    //   socket buffer size
    Dispatcher();
    ~Dispatcher();

    void run();

private:
    struct DispatcherImpl;
    DispatcherImpl* impl_;
};

} // end of QueryPerf

#endif // __QUERYPERF_DISPATCHER_H 

// Local Variables:
// mode: c++
// End:

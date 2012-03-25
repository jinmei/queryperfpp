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

#ifndef __QUERYPERF_QUERY_CONTEXT_H
#define __QUERYPERF_QUERY_CONTEXT_H 1

#include <libqueryperfpp_fwd.h>

#include <dns/message.h>

#include <boost/noncopyable.hpp>

#include <sys/types.h>

namespace Queryperf {

class QueryContext : private boost::noncopyable {
public:
    /// \brief A supplemental structure to represent parameters of a query
    ///
    /// This structure intends to store DNS queries in wire format and some
    /// network layer information.
    struct QuerySpec {
        QuerySpec(int proto_param, const void* data_param, size_t len_param) :
            proto(proto_param), data(data_param), len(len_param)
        {}
        const int proto;
        const void* const data;
        const size_t len;
    };

    QueryContext(QueryRepository& repository);
    ~QueryContext();

    QuerySpec start(isc::dns::qid_t qid);

private:
    struct QueryContextImpl;
    QueryContextImpl* impl_;
};

class QueryContextCreator {
public:
    QueryContextCreator(QueryRepository& repository) : repository_(repository)
    {}

    QueryContext* create();

private:
    QueryRepository& repository_;
};

} // end of QueryPerf

#endif // __QUERYPERF_QUERY_CONTEXT_H 

// Local Variables:
// mode: c++
// End:

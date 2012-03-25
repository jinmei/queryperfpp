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

#ifndef __QUERYPERF_QUERY_REPOSITORY_H
#define __QUERYPERF_QUERY_REPOSITORY_H 1

#include <dns/message.h>
#include <dns/rrclass.h>

#include <boost/noncopyable.hpp>

#include <istream>
#include <string>
#include <stdexcept>

namespace Queryperf {

class QueryRepositoryError : public std::runtime_error {
public:
    QueryRepositoryError(const std::string& what_arg) :
        std::runtime_error(what_arg)
    {}
};

class QueryRepository : private boost::noncopyable {
public:
    explicit QueryRepository(std::istream& input);
    explicit QueryRepository(const std::string& input_file);
    ~QueryRepository();

    /// \brief Preload all data and hold it internally.
    void load();

    /// \brief Return preloaded query count if preload took place.
    ///
    /// It returns 0 if preload hasn't been initiated.
    size_t getQueryCount() const;

    void getNextQuery(isc::dns::Message& message, int& protocol);

    /// \brief Set the default RR class of the queries.
    ///
    /// When preload is used, this must be called before load().
    ///
    /// \param qclass The default RR class of the queries.
    void setQueryClass(isc::dns::RRClass qclass);

    /// \brief Toggle the DNSSEC DO bit (in EDNS0 OPT RR) of the queries.
    ///
    /// When preload is used, this must be called before load().
    ///
    /// \param on A boolean flag indicating whether to set the DO bit.
    void setDNSSEC(bool on);

    /// \brief Set the default transport protocol used to send queries.
    ///
    /// When preload is used, this must be called before load().
    ///
    /// \param proto Either IPPROTO_UDP (for UDP) or IPPROTO_TCP (for TCP).
    void setProtocol(int proto);

    /// \brief Toggle whether to include EDNS0 in queries.
    ///
    /// Note that in order to suppress EDNS0 completely, the DNSSEC DO bit
    /// must not be expected to be set.  So, in practice, if this method
    /// is called with \c false, \c setDNSSEC() would also have to be called
    /// with \c false.
    ///
    /// \param on A boolean flag indicating whether to include EDNS0.
    void setEDNS(bool on);

private:
    struct QueryRepositoryImpl;
    QueryRepositoryImpl* impl_;
};

} // end of QueryPerf

#endif // __QUERYPERF_QUERY_REPOSITORY_H

// Local Variables:
// mode: c++
// End:

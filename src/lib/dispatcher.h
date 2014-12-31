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

#include <libqueryperfpp_fwd.h>

#include <boost/noncopyable.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>

#include <stdexcept>
#include <istream>

#include <sys/types.h>
#include <stdint.h>

namespace Queryperf {

/// \brief Exception class thrown on an error within the dispatcher.
class DispatcherError : public std::runtime_error {
public:
    explicit DispatcherError(const std::string& what_arg) :
        std::runtime_error(what_arg)
    {}
};

class Dispatcher : private boost::noncopyable {
public:
    //
    // Default parameters: derived from the original queryperf.
    // parameters eventually taken: socket buffer size

    /// \brief Default window size: maximum number of queries outstanding.
    static const size_t DEFAULT_WINDOW = 20;

    /// \brief Default test duration in seconds.
    static const long DEFAULT_DURATION = 30;

    /// \brief Default server address to be tested (::1)
    static const char* const DEFAULT_SERVER;

    /// \brief Default server port
    static const uint16_t DEFAULT_PORT = 53;

    /// \brief Default timeout for query completion in seconds.
    static const unsigned int DEFAULT_QUERY_TIMEOUT = 5;

    /// \brief Generic constructor.
    ///
    /// \param msg_mgr A message manager object that handles I/O and timeout
    /// events.
    Dispatcher(MessageManager& msg_mgr, QueryContextCreator& ctx_creator);

    /// \brief Constructor when using "builtin" classes with input file name.
    Dispatcher(const std::string& data_file);

    /// \brief Constructor when using "builtin" classes with input stream.
    Dispatcher(std::istream& input_stream);

    /// \brief Destructor.
    ~Dispatcher();

    /// \brief Preload queries.
    ///
    /// This can be called at most once, and must be called before run().
    void loadQueries();

    /// \brief Start the dispatcher.
    void run();

    void setServerAddress(const std::string& address);
    std::string getServerAddress() const;

    void setServerPort(uint16_t port);
    uint16_t getServerPort() const;

    void setTestDuration(size_t duration);
    size_t getTestDuration() const;

    /// \brief Set the default transport protocol used to send queries.
    ///
    /// This method must be called before run().
    void setProtocol(int proto);

    /// \brief Set the default RR class of queries.
    ///
    /// This must be called before run().
    void setDefaultQueryClass(const std::string& qclass_txt);

    /// \brief Toggle whether to set the EDNS0 DO bit.
    ///
    /// Note that if the bit is to be set (when \c on is true), it implies
    /// EDNS0 OPT RR will be included in queries by default.
    ///
    /// This method must be called before run().
    void setDNSSEC(bool on);

    /// \brief Toggle whether to include EDNS0 in queries.
    ///
    /// Note that even if this is set to false EDNS0 will be used unless
    /// DNSSEC is also disabled by \c setDNSSEC().
    ///
    /// This method must be called before run().
    void setEDNS(bool on);

    /// \brief Return the number of queries sent from the dispatcher.
    size_t getQueriesSent() const;

    /// \brief Return the number of queries correctly responded.
    size_t getQueriesCompleted() const;

    /// \brief Return the absolute time when the first query was sent.
    const boost::posix_time::ptime& getStartTime() const;

    /// \brief Return the absolute time when the dispatcher stops.
    const boost::posix_time::ptime& getEndTime() const;

private:
    struct DispatcherImpl;
    DispatcherImpl* impl_;
};

} // end of QueryPerf

#endif // __QUERYPERF_DISPATCHER_H 

// Local Variables:
// mode: c++
// End:

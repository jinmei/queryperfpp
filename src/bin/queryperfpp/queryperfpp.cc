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

#include <dispatcher.h>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/shared_ptr.hpp>

#include <cassert>
#include <cstring>
#include <sstream>
#include <iostream>
#include <vector>
#include <stdexcept>

#include <netinet/in.h>

#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>

using namespace Queryperf;
using namespace boost::posix_time;
using boost::lexical_cast;
using boost::shared_ptr;

namespace {
struct QueryStatistics {
    QueryStatistics() : queries_sent(0), queries_completed(0)
    {}

    size_t queries_sent;
    size_t queries_completed;
    std::vector<double> qps_results; // a list of QPS per worker thread
};

double
accumulateResult(const Dispatcher& disp, QueryStatistics& result) {
    result.queries_sent += disp.getQueriesSent();
    result.queries_completed += disp.getQueriesCompleted();

    const time_duration duration = disp.getEndTime() - disp.getStartTime();
    return (disp.getQueriesCompleted() / (
                static_cast<double>(duration.total_microseconds()) / 1000000));
}

// Default Parameters
uint16_t getDefaultPort() { return (Dispatcher::DEFAULT_PORT); }
long getDefaultDuration() { return (Dispatcher::DEFAULT_DURATION); }
const size_t DEFAULT_THREAD_COUNT = 1;
const char* const DEFAULT_CLASS = "IN";
const bool DEFAULT_DNSSEC = true; // set EDNS DO bit by default
const bool DEFAULT_EDNS = true; // set EDNS0 OPT RR by default
const char* const DEFAULT_DATA_FILE = "-"; // stdin
const char* const DEFAULT_PROTOCOL = "udp";

void
usage() {
    const std::string usage_head = "Usage: queryperf++ ";
    const std::string indent(usage_head.size(), ' ');
    std::cerr << usage_head
         << "[-C qclass] [-d datafile] [-D on|off] [-e on|off] [-l limit]\n";
    std::cerr << indent
         << "[-L] [-n #threads] [-p port] [-P udp|tcp] [-Q query_sequence]\n";
    std::cerr << indent
         << "[-s server_addr]\n";
    std::cerr << "  -C sets default query class (default: "
         << DEFAULT_CLASS << ")\n";
    std::cerr << "  -d sets the input data file (default: stdin)\n";
    std::cerr << "  -D sets whether to set EDNS DO bit (default: "
         << (DEFAULT_EDNS ? "on" : "off") << ")\n";
    std::cerr << "  -e sets whether to include EDNS (default: "
         << (DEFAULT_DNSSEC ? "on" : "off") << ")\n";
    std::cerr << "  -l sets how long to run tests in seconds (default: "
         << getDefaultDuration() << ")\n";
    std::cerr << "  -L enables query preloading (default: disabled)\n";
    std::cerr << "  -n sets the number of querying threads (default: "
         << DEFAULT_THREAD_COUNT << ")\n";
    std::cerr << "  -p sets the port on which to query the server (default: "
         << getDefaultPort() << ")\n";
    std::cerr << "  -P sets transport protocol for queries (default: "
         << DEFAULT_PROTOCOL << ")\n";
    std::cerr
        << "  -Q sets newline-separated query data (default: unspecified)\n";
    std::cerr << "  -s sets the server to query (default: "
              << Dispatcher::DEFAULT_SERVER << ")";
    std::cerr << std::endl;
    exit(1);
}

void*
runQueryperf(void* arg) {
    Dispatcher* disp = static_cast<Dispatcher*>(arg);
    try {
        disp->run();
    } catch (const std::exception& ex) {
        std::cerr << "Worker thread died unexpectedly: " << ex.what()
                  << std::endl;
    }
    return (NULL);
}

typedef shared_ptr<Dispatcher> DispatcherPtr;
typedef shared_ptr<std::stringstream> SStreamPtr;

bool
parseOnOffFlag(const char* optname, const char* const optarg,
               bool default_val)
{
    if (optarg != NULL) {
        if (std::string(optarg) == "on") {
            return (true);
        } else if (std::string(optarg) == "off") {
            return (false);
        } else {
            std::cerr << "Option argument of "<< optname
                      << " must be 'on' or 'off'" << std::endl;
            exit(1);
        }
    }
    return (default_val);
}
}

int
main(int argc, char* argv[]) {
    const char* qclass_txt = DEFAULT_CLASS;
    const char* data_file = NULL;
    const char* dnssec_flag_txt = NULL;
    const char* edns_flag_txt = NULL;
    const char* server_address = Dispatcher::DEFAULT_SERVER;
    const char* proto_txt = DEFAULT_PROTOCOL;
    std::string server_port_str = lexical_cast<std::string>(getDefaultPort());
    std::string time_limit_str =
        lexical_cast<std::string>(getDefaultDuration());
    const char* num_threads_txt = NULL;
    const char* query_txt = NULL;
    size_t num_threads = DEFAULT_THREAD_COUNT;
    bool preload = false;

    int ch;
    while ((ch = getopt(argc, argv, "C:d:D:e:hl:Ln:p:P:Q:s:")) != -1) {
        switch (ch) {
        case 'C':
            qclass_txt = optarg;
            break;
        case 'd':
            data_file = optarg;
            break;
        case 'D':
            dnssec_flag_txt = optarg;
            break;
        case 'e':
            edns_flag_txt = optarg;
            break;
        case 'n':
            num_threads_txt = optarg;
            break;
        case 's':
            server_address = optarg;
            break;
        case 'p':
            server_port_str = std::string(optarg);
            break;
        case 'P':
            proto_txt = optarg;
            break;
        case 'Q':
            query_txt = optarg;
            break;
        case 'l':
            time_limit_str = std::string(optarg);
            break;
        case 'L':
            preload = true;
            break;
        case 'h':
        case '?':
        default :
            usage();
        }
    }

    // Validation on options
    if (data_file == NULL && query_txt == NULL) {
        data_file = DEFAULT_DATA_FILE;
    }
    if (data_file != NULL && query_txt != NULL) {
        std::cerr << "-d and -Q cannot be specified at the same time"
                  << std::endl;
        return (1);
    }
    const bool dnssec_flag = parseOnOffFlag("-D", dnssec_flag_txt,
                                            DEFAULT_DNSSEC);
    const bool edns_flag = parseOnOffFlag("-e", edns_flag_txt, DEFAULT_EDNS);
    if (!edns_flag && dnssec_flag) {
        std::cerr << "[WARN] EDNS is disabled but DNSSEC is enabled; "
                  << "EDNS will still be included." << std::endl;
    }
    const std::string proto_str(proto_txt);
    if (proto_str != "udp" && proto_str != "tcp") {
        std::cerr << "Invalid protocol: " << proto_str << std::endl;
        return (1);
    }
    const int proto = proto_str == "udp" ? IPPROTO_UDP : IPPROTO_TCP;

    try {
        std::vector<DispatcherPtr> dispatchers;
        std::vector<SStreamPtr> input_streams;
        if (num_threads_txt != NULL) {
            num_threads = lexical_cast<size_t>(num_threads_txt);
        }
        if (num_threads > 1 && data_file != NULL &&
            std::string(data_file) == "-") {
            std::cerr << "stdin can be used as input only with 1 thread"
                      << std::endl;
            return (1);
        }

        // Prepare
        std::cout << "[Status] Processing input data" << std::endl;
        for (size_t i = 0; i < num_threads; ++i) {
            DispatcherPtr disp;
            if (data_file != NULL) {
                disp.reset(new Dispatcher(data_file));
            } else {
                assert(query_txt != NULL);
                SStreamPtr ss(new std::stringstream(query_txt));
                disp.reset(new Dispatcher(*ss));
                input_streams.push_back(ss);
            }
            disp->setServerAddress(server_address);
            disp->setServerPort(lexical_cast<uint16_t>(server_port_str));
            disp->setTestDuration(lexical_cast<size_t>(time_limit_str));
            disp->setDefaultQueryClass(qclass_txt);
            disp->setDNSSEC(dnssec_flag);
            disp->setEDNS(edns_flag);
            disp->setProtocol(proto);
            // Preload must be the final step of configuration before running.
            if (preload) {
                disp->loadQueries();
            }
            dispatchers.push_back(disp);
        }

        // Run
        std::cout << "[Status] Sending queries to " << server_address
             << " over " << proto_str << ", port " << server_port_str << std::endl;
        std::vector<pthread_t> threads;
        const ptime start_time = microsec_clock::local_time();
        for (size_t i = 0; i < num_threads; ++i) {
            pthread_t th;
            const int error = pthread_create(&th, NULL, runQueryperf,
                                             dispatchers[i].get());
            if (error != 0) {
                throw std::runtime_error(
                    std::string("Failed to create a worker thread: ") +
                    strerror(error));
            }
            threads.push_back(th);
        }

        for (size_t i = 0; i < num_threads; ++i) {
            const int error = pthread_join(threads[i], NULL);
            if (error != 0) {
                // if join failed, we warn about it and just continue anyway
                std::cerr
                    << "pthread_join failed: " << strerror(error) << std::endl;
            }
        }
        const ptime end_time = microsec_clock::local_time();
        std::cout << "[Status] Testing complete" << std::endl;

        // Accumulate per-thread statistics.  Print the summary QPS for each,
        // and if more than one thread was used, print the sum of them.
        std::cout << "\nStatistics:\n\n";

        QueryStatistics result;
        double total_qps = 0;
        std::cout.precision(6);
        for (size_t i = 0; i < num_threads; ++i) {
            const double qps = accumulateResult(*dispatchers[i], result);
            total_qps += qps;
            std::cout << "  Queries per second #" << i <<
                ":  " << std::fixed << qps << " qps\n";
        }
        if (num_threads > 1) {
            std::cout << "         Summarized QPS:  " << std::fixed << total_qps
                 << " qps\n";
        }
        std::cout << std::endl;

        // Print the total result.
        std::cout << "  Queries sent:         " << result.queries_sent
             << " queries\n";
        std::cout << "  Queries completed:    " << result.queries_completed
             << " queries\n";
        std::cout << "\n";

        std::cout << "  Percentage completed: " << std::setprecision(2);
        if (result.queries_sent > 0) {
            std::cout << std::setw(6)
                      << (static_cast<double>(result.queries_completed) /
                          result.queries_sent) * 100 << "%\n";
        } else {
            std::cout << "N/A\n";
        }
        std::cout << "  Percentage lost:      ";
        if (result.queries_sent > 0) {
            const size_t lost_count = result.queries_sent -
                result.queries_completed;
            std::cout << std::setw(6) << (static_cast<double>(lost_count) /
                                          result.queries_sent) * 100 << "%\n";
        } else {
            std::cout << "N/A\n";
        }
        std::cout << "\n";

        std::cout << "  Started at:           " << start_time << std::endl;
        std::cout << "  Finished at:          " << end_time << std::endl;
        const time_duration duration = end_time - start_time;
        std::cout
            << "  Run for:              " << std::setprecision(6)
            << (static_cast<double>(duration.total_microseconds()) / 1000000)
            << " seconds\n";
        std::cout << "\n";

        const double qps = result.queries_completed / (
            static_cast<double>(duration.total_microseconds()) / 1000000);
        std::cout.precision(6);
        std::cout << "  Queries per second:   " << std::fixed << qps
                  << " qps\n";
        std::cout << std::endl;
    } catch (const std::exception& ex) {
        std::cerr << "Unexpected failure: " << ex.what() << std::endl;
        return (1);
    }

    return (0);
}

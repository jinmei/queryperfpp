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

#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>

using namespace std;
using namespace Queryperf;
using namespace boost::posix_time;
using boost::lexical_cast;
using boost::shared_ptr;

namespace {
void
usage() {
    const string usage_head = "Usage: queryperf++ ";
    const string indent(usage_head.size(), ' ');
    cerr << usage_head
         << "[-C qclass] [-d datafile] [-D on|off] [-e on|off] [-l limit]\n";
    cerr << indent
         << "[-L] [-n #threads] [-s server_addr] [-p port]\n";
    cerr << indent
         << "[-Q query_sequence]\n";
    cerr << "  -C specifies default query class (default: \"IN\")\n";
    cerr << "  -D specifies whether to set EDNS DO bit (default: \"on\")\n";
    cerr << "  -e specifies whether to include EDNS (default: \"on\")\n";
    cerr << "  -Q specifies NL-separated query data in command line (default: unuspecified)";
    cerr << endl;
    exit(1);
}

void*
runQueryperf(void* arg) {
    Dispatcher* disp = static_cast<Dispatcher*>(arg);
    try {
        disp->run();
    } catch (const exception& ex) {
        cerr << "Worker thread died unexpectedly: " << ex.what() << endl;
    }
    return (NULL);
}

const bool DEFAULT_DNSSEC = true; // set EDNS DO bit by default
const bool DEFAULT_EDNS = true; // set EDNS0 OPT RR by default
const char* const DEFAULT_DATA_FILE = "-"; // stdin

typedef shared_ptr<Dispatcher> DispatcherPtr;
typedef shared_ptr<stringstream> SStreamPtr;

bool
parseOnOffFlag(const char* optname, const char* const optarg,
               bool default_val)
{
    if (optarg != NULL) {
        if (string(optarg) == "on") {
            return (true);
        } else if (string(optarg) == "off") {
            return (false);
        } else {
            cerr << "Option argument of "<< optname
                 << " must be 'on' or 'off'" << endl;
            exit(1);
        }
    }
    return (default_val);
}
}

int
main(int argc, char* argv[]) {
    const char* qclass_txt = NULL;
    const char* data_file = NULL;
    const char* dnssec_flag_txt = NULL;
    const char* edns_flag_txt = NULL;
    const char* server_address = NULL;
    const char* server_port_txt = NULL;
    const char* time_limit_txt = NULL;
    const char* num_threads_txt = NULL;
    const char* query_txt = NULL;
    size_t num_threads = 1;
    bool preload = false;

    int ch;
    while ((ch = getopt(argc, argv, "C:d:D:e:hl:Ln:p:Q:s:")) != -1) {
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
            server_port_txt = optarg;
            break;
        case 'Q':
            query_txt = optarg;
            break;
        case 'l':
            time_limit_txt = optarg;
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
    if (data_file == NULL && query_txt == NULL) {
        data_file = DEFAULT_DATA_FILE;
    }
    if (data_file != NULL && query_txt != NULL) {
        cerr << "-d and -Q cannot be specified at the same time" << endl;
        return (1);
    }
    const bool dnssec_flag = parseOnOffFlag("-D", dnssec_flag_txt,
                                            DEFAULT_DNSSEC);
    const bool edns_flag = parseOnOffFlag("-e", edns_flag_txt, DEFAULT_EDNS);
    if (!edns_flag && dnssec_flag) {
        cerr << "[WARN] EDNS is disabled but DNSSEC is enabled; "
             << "EDNS will still be included." << endl;
    }

    try {
        vector<DispatcherPtr> dispatchers;
        vector<SStreamPtr> input_streams;
        if (num_threads_txt != NULL) {
            num_threads = lexical_cast<size_t>(num_threads_txt);
        }
        if (num_threads > 1 && data_file != NULL && string(data_file) == "-") {
            cerr << "stdin can be used as input only with 1 thread" << endl;
            return (1);
        }

        // Prepare
        for (size_t i = 0; i < num_threads; ++i) {
            DispatcherPtr disp;
            if (data_file != NULL) {
                disp.reset(new Dispatcher(data_file));
            } else {
                assert(query_txt != NULL);
                cout << query_txt << endl;
                SStreamPtr ss(new stringstream(query_txt));
                disp.reset(new Dispatcher(*ss));
                input_streams.push_back(ss);
            }
            if (server_address != NULL) {
                disp->setServerAddress(server_address);
            }
            if (server_port_txt != NULL) {
                disp->setServerPort(lexical_cast<uint16_t>(server_port_txt));
            }
            if (time_limit_txt != NULL) {
                disp->setTestDuration(lexical_cast<size_t>(time_limit_txt));
            }
            if (qclass_txt != NULL) {
                disp->setDefaultQueryClass(qclass_txt);
            }
            disp->setDNSSEC(dnssec_flag);
            disp->setEDNS(edns_flag);
            if (preload) {
                disp->loadQueries();
            }
            dispatchers.push_back(disp);
        }

        // Run
        vector<pthread_t> threads;
        for (size_t i = 0; i < num_threads; ++i) {
            pthread_t th;
            const int error = pthread_create(&th, NULL, runQueryperf,
                                             dispatchers[i].get());
            if (error != 0) {
                throw runtime_error(
                    string("Failed to create a worker thread: ") +
                    strerror(error));
            }
            threads.push_back(th);
        }

        for (size_t i = 0; i < num_threads; ++i) {
            const int error = pthread_join(threads[i], NULL);
            if (error != 0) {
                // if join failed, we warn about it and just continue anyway
                cerr << "pthread_join failed: " << strerror(error) << endl;
            }
        }

        for (size_t i = 0; i < num_threads; ++i) {
            const Dispatcher& disp = *dispatchers[i];
            cout << "  Queries sent:         " << disp.getQueriesSent()
                 << " queries\n";
            cout << "  Queries completed:    " << disp.getQueriesCompleted()
                 << " queries\n";
            cout << "\n";

            cout << "  Started at:           " << disp.getStartTime() << endl;
            cout << "  Finished at:          " << disp.getEndTime() << endl;
            cout << "\n";

            const time_duration duration = disp.getEndTime() -
                disp.getStartTime();
            const double qps = disp.getQueriesCompleted() / (
                static_cast<double>(duration.total_microseconds()) / 1000000);
            cout.precision(6);
            cout << "  Queries per second:   " << fixed << qps << " qps\n";
            cout << endl;
        }
    } catch (const exception& ex) {
        cerr << "Unexpected failure: " << ex.what() << endl;
        return (1);
    }

    return (0);
}

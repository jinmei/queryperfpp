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

#include <query_repository.h>
#include <query_context.h>
#include <asio_message_manager.h>
#include <dispatcher.h>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/shared_ptr.hpp>

#include <cstring>
#include <iostream>
#include <vector>
#include <stdexcept>

#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

using namespace std;
using namespace Queryperf;
using namespace boost::posix_time;
using boost::lexical_cast;
using boost::shared_ptr;

namespace {
void
usage() {
    cerr << "Usage: queryperf++ [-d datafile]" << endl;
    exit(1);
}

void*
runQueryperf(void* arg) {
    Dispatcher* disp = static_cast<Dispatcher*>(arg);
    disp->run();
    return (NULL);
}

typedef shared_ptr<QueryRepository> QueryRepositoryPtr;
typedef shared_ptr<ASIOMessageManager> ASIOMessageManagerPtr;
typedef shared_ptr<QueryContextCreator> QueryContextCreatorPtr;
typedef shared_ptr<Dispatcher> DispatcherPtr;
}

int
main(int argc, char* argv[]) {
    const char* data_file = NULL;
    size_t num_threads = 1;

    int ch;
    while ((ch = getopt(argc, argv, "d:n:")) != -1) {
        switch (ch) {
        case 'd':
            data_file = optarg;
            break;
        case 'n':
            num_threads = lexical_cast<size_t>(optarg);
            break;
        case '?':
        default :
            usage();
        }
    }
    if (data_file == NULL) {
        cerr << "data file must be explicitly specified by -d for now" << endl;
        exit(1);
    }

    try {
        vector<QueryRepositoryPtr> repositories;
        vector<ASIOMessageManagerPtr> msg_mgrs;
        vector<QueryContextCreatorPtr> ctx_creators;
        vector<DispatcherPtr> dispatchers;
        vector<pthread_t> threads;

        // Prepare
        for (size_t i = 0; i < num_threads; ++i) {
            repositories.push_back(QueryRepositoryPtr(new QueryRepository(
                                                          data_file)));
            msg_mgrs.push_back(ASIOMessageManagerPtr(new ASIOMessageManager));
            ctx_creators.push_back(QueryContextCreatorPtr(
                                       new QueryContextCreator(
                                           *repositories.back())));
            dispatchers.push_back(DispatcherPtr(new Dispatcher(
                                                    *msg_mgrs.back(),
                                                    *ctx_creators.back())));
        }

        // Run
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

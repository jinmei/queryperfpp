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

#include <iostream>

#include <stdlib.h>
#include <unistd.h>

using namespace std;
using namespace Queryperf;
using namespace boost::posix_time;

namespace {
void
usage() {
    cerr << "Usage: queryperf++ [-d datafile]" << endl;
    exit(1);
}
}

int
main(int argc, char* argv[]) {
    const char* data_file = NULL;

    int ch;
    while ((ch = getopt(argc, argv, "d:")) != -1) {
        switch (ch) {
        case 'd':
            data_file = optarg;
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
        QueryRepository query_repo(data_file);
        ASIOMessageManager msg_mgr;
        QueryContextCreator ctx_creator(query_repo);
        Dispatcher disp(msg_mgr, ctx_creator);
        disp.run();

        cout << "  Queries sent:         " << disp.getQueriesSent()
             << " queries\n";
        cout << "  Queries completed:    " << disp.getQueriesCompleted()
             << " queries\n";
        cout << "\n";

        cout << "  Started at:           " << disp.getStartTime() << endl;
        cout << "  Finished at:          " << disp.getEndTime() << endl;
        cout << "\n";

        const time_duration duration = disp.getEndTime() - disp.getStartTime();
        const double qps = disp.getQueriesCompleted() / (
            static_cast<double>(duration.total_microseconds()) / 1000000);
        cout.precision(6);
        cout << "  Queries per second:   " << fixed << qps << " qps\n";
        cout << endl;
    } catch (const exception& ex) {
        cerr << "Unexpected failure: " << ex.what() << endl;
        return (1);
    }

    return (0);
}

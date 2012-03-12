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

#include <dns/name.h>
#include <dns/message.h>
#include <dns/opcode.h>
#include <dns/rcode.h>
#include <dns/rrclass.h>
#include <dns/rrtype.h>
#include <dns/question.h>

#include <boost/scoped_ptr.hpp>

#include <istream>
#include <fstream>
#include <string>
#include <map>

using namespace std;
using boost::scoped_ptr;
using namespace isc::dns;

namespace {
// an ad hoc threadshold to prevent a busy loop due to an empty input file.
const size_t MAX_EMPTY_LOOP = 1000;
}

namespace Queryperf {

struct QueryRepository::QueryRepositoryImpl {
    QueryRepositoryImpl(istream& input) : input_(input) {
        initialize();
    }

    QueryRepositoryImpl(const string& input_file) :
        input_ifs_(new ifstream(input_file.c_str())),
        input_(*input_ifs_)
    {
        initialize();
    }

    void initialize() {
        // BIND 10 libdns++ doesn't yet recognize all standardized RR type
        // menmonics.  To suppress noisy log and avoid ignoring query data
        // containing such RR types, we use a homebrew mapping table.
        // We keep the same map for each repository object to avoid inter
        // thread contention.
        aux_typemap_["A6"] = "TYPE38";
        aux_typemap_["ANY"] = "TYPE255";
    }

    scoped_ptr<ifstream> input_ifs_;
    istream& input_;
    map<string, string> aux_typemap_;
};

QueryRepository::QueryRepository(istream& input) :
    impl_(new QueryRepositoryImpl(input))
{
}

QueryRepository::QueryRepository(const string& input_file) :
    impl_(new QueryRepositoryImpl(input_file))
{
    if (impl_->input_.fail()) {
        delete impl_;
        throw QueryRepositoryError("failed to input data file: " + input_file);
    }
}

QueryRepository::~QueryRepository() {
    delete impl_;
}

void
QueryRepository::getNextQuery(Message& query_msg) {
    QuestionPtr question;

    while (!question) {
        string line;
        size_t loop_count = 0;
        while (line.empty()) {
            if (loop_count++ == MAX_EMPTY_LOOP) {
                throw QueryRepositoryError("failed to get input line too long,"
                                           " possibly an empty input?");
            }
            getline(impl_->input_, line);
            if (impl_->input_.eof()) {
                impl_->input_.clear();
                impl_->input_.seekg(0);
            } else if (impl_->input_.bad() || impl_->input_.fail()) {
                throw QueryRepositoryError("unexpected failure in reading "
                                           "input data");
            }
            if (line[0] == ';') { // comment check (note it's safe to see [0])
                line.clear();     // force ingoring this line.
            }
        }

        stringstream ss(line);
        string qname_text, qtype_text;
        ss >> qname_text >> qtype_text;
        if (ss.bad() || ss.fail() || !ss.eof()) {
            // Ignore the line is organized in an unexpected way.
            continue;
        }
        // Workaround for some RR types that are not recognized by BIND 10
        map<string, string>::const_iterator it =
            impl_->aux_typemap_.find(qtype_text);
        if (it != impl_->aux_typemap_.end()) {
            qtype_text = it->second;
        }
        try {
            question.reset(new Question(Name(qname_text), RRClass::IN(),
                                        RRType(qtype_text)));
        } catch (const isc::Exception& ex) {
            // The input data may contain bad string, which would trigger an
            // exception.  We ignore them and continue reading until we find
            // a valid one.
            cerr << "Error parsing query (" << ex.what() << "): "
                 << line << endl;
        }
    }

    query_msg.clear(Message::RENDER);
    query_msg.setOpcode(Opcode::QUERY());
    query_msg.setRcode(Rcode::NOERROR());
    query_msg.setHeaderFlag(Message::HEADERFLAG_RD);
    query_msg.addQuestion(question);
}

} // end of QueryPerf

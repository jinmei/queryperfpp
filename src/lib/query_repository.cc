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

using namespace std;
using boost::scoped_ptr;
using namespace isc::dns;

namespace Queryperf {

struct QueryRepository::QueryRepositoryImpl {
    QueryRepositoryImpl(istream& input) : input_(input) {}

    QueryRepositoryImpl(const string& input_file) :
        input_ifs_(new ifstream(input_file.c_str())),
        input_(*input_ifs_)
    {}

    scoped_ptr<ifstream> input_ifs_;
    istream& input_;
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

string
QueryRepository::getNextQuery(Message& query_msg) {
    string line;

    while (line.empty()) {
        getline(impl_->input_, line);
        if (impl_->input_.eof()) {
            impl_->input_.clear();
            impl_->input_.seekg(0);
        } else if (impl_->input_.bad() || impl_->input_.fail()) {
            throw QueryRepositoryError("unexpected failure in reading input "
                                       "data");
        }
    }

    stringstream ss(line);
    string qname_text, qtype_text;
    ss >> qname_text >> qtype_text;
    query_msg.clear(Message::RENDER);
    query_msg.setOpcode(Opcode::QUERY());
    query_msg.setRcode(Rcode::NOERROR());
    query_msg.setHeaderFlag(Message::HEADERFLAG_RD);
    query_msg.addQuestion(QuestionPtr(new Question(Name(qname_text),
                                                   RRClass::IN(),
                                                   RRType(qtype_text))));

    return (line);
}

} // end of QueryPerf

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
#include <dns/edns.h>
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
#include <vector>

using namespace std;
using boost::scoped_ptr;
using namespace isc::dns;

namespace {
// an ad hoc threadshold to prevent a busy loop due to an empty input file.
const size_t MAX_EMPTY_LOOP = 1000;
}

namespace Queryperf {

struct QueryRepository::QueryRepositoryImpl {
    QueryRepositoryImpl(istream& input) :
        qclass_(RRClass::IN()), input_(input)
    {
        initialize();
    }

    QueryRepositoryImpl(const string& input_file) :
        qclass_(RRClass::IN()),
        input_ifs_(new ifstream(input_file.c_str())),
        input_(*input_ifs_)
    {
        initialize();
    }

    void initialize() {
        use_dnssec_ = true;
        use_edns_ = true;

        edns_.reset(new EDNS);
        edns_->setUDPSize(4096);
        edns_->setDNSSECAwareness(true);

        // BIND 10 libdns++ doesn't yet recognize all standardized RR type
        // menmonics.  To suppress noisy log and avoid ignoring query data
        // containing such RR types, we use a homebrew mapping table.
        // We keep the same map for each repository object to avoid inter
        // thread contention.
        aux_typemap_["A6"] = "TYPE38";
        aux_typemap_["ANY"] = "TYPE255";
    }

    // Extract the next question from the input stream
    QuestionPtr readNextQuestion(bool rewind);

    // Return the next question, either from the preloaded vector (if done)
    // or from the input stream.
    QuestionPtr getNextQuestion();

    RRClass qclass_;            // Query class
    scoped_ptr<ifstream> input_ifs_;
    istream& input_;
    map<string, string> aux_typemap_;
    vector<QuestionPtr> questions_; // used in the "preload" mode
    bool use_edns_;                 // whether to include ENDS by default.
    bool use_dnssec_;               // whether to set EDNS DO bit by default.
                                    // EDNS will be included regardless of
                                    // use_edns_.
    EDNSPtr edns_;                  // template of common EDNS OPT RR
    vector<QuestionPtr>::const_iterator current_question_;
    vector<QuestionPtr>::const_iterator end_question_;
};

QuestionPtr
QueryRepository::QueryRepositoryImpl::readNextQuestion(bool rewind) {
    QuestionPtr question;

    if (!rewind && input_.eof()) {
        return (QuestionPtr());
    }

    while (!question) {
        string line;
        size_t loop_count = 0;
        while (line.empty()) {
            if (loop_count++ == MAX_EMPTY_LOOP) {
                throw QueryRepositoryError("failed to get input line too long,"
                                           " possibly an empty input?");
            }
            getline(input_, line);
            if (input_.eof()) {
                if (rewind) {
                    input_.clear();
                    input_.seekg(0);
                } else if (line.empty()) {
                    return (QuestionPtr());
                }
            } else if (input_.bad() || input_.fail()) {
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
            aux_typemap_.find(qtype_text);
        if (it != aux_typemap_.end()) {
            qtype_text = it->second;
        }
        try {
            question.reset(new Question(Name(qname_text), qclass_,
                                        RRType(qtype_text)));
        } catch (const isc::Exception& ex) {
            // The input data may contain bad string, which would trigger an
            // exception.  We ignore them and continue reading until we find
            // a valid one.
            cerr << "Error parsing query (" << ex.what() << "): "
                 << line << endl;
        }
    }

    return (question);
}

QuestionPtr
QueryRepository::QueryRepositoryImpl::getNextQuestion() {
    if (!questions_.empty()) {
        // queries have been preloaded.  get the next one from the vector.
        QuestionPtr question = *current_question_;
        if (++current_question_ == end_question_) {
            current_question_ = questions_.begin();
        }
        return (question);
    }
    return (readNextQuestion(true));
}

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
QueryRepository::load() {
    // duplicate load check
    if (!impl_->questions_.empty()) {
        throw QueryRepositoryError("duplicate preload attempt");
    }

    QuestionPtr question;
    while ((question = impl_->readNextQuestion(false)) != NULL) {
        impl_->questions_.push_back(question);
    }
    if (impl_->questions_.empty()) {
        throw QueryRepositoryError("failed to preload queries: empty input");
    }
    impl_->current_question_ = impl_->questions_.begin();
    impl_->end_question_ = impl_->questions_.end();
}

size_t
QueryRepository::getQueryCount() const {
    return (impl_->questions_.size());
}

void
QueryRepository::getNextQuery(Message& query_msg) {
    QuestionPtr question = impl_->getNextQuestion();

    query_msg.clear(Message::RENDER);
    query_msg.setOpcode(Opcode::QUERY());
    query_msg.setRcode(Rcode::NOERROR());
    query_msg.setHeaderFlag(Message::HEADERFLAG_RD);
    query_msg.addQuestion(question);
    if (impl_->use_edns_ || impl_->use_dnssec_) {
        query_msg.setEDNS(impl_->edns_);
    }
}

void
QueryRepository::setQueryClass(RRClass qclass) {
    if (!impl_->questions_.empty()) {
        throw QueryRepositoryError("query class is being set after preload");
    }

    impl_->qclass_ = qclass;
}

void
QueryRepository::setDNSSEC(bool on) {
    if (!impl_->questions_.empty()) {
        throw QueryRepositoryError(
            "DNSSEC DO bit is being changed after preload");
    }

    impl_->use_dnssec_ = on;
    impl_->edns_->setDNSSECAwareness(on);
}

void
QueryRepository::setEDNS(bool on) {
    if (!impl_->questions_.empty()) {
        throw QueryRepositoryError("EDNS flag is being changed after preload");
    }

    impl_->use_edns_ = on;
}

} // end of QueryPerf

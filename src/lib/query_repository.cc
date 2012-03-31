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
#include <dns/rdata.h>
#include <dns/rrclass.h>
#include <dns/rrtype.h>
#include <dns/rrttl.h>
#include <dns/question.h>

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/scoped_ptr.hpp>

#include <istream>
#include <fstream>
#include <string>
#include <map>
#include <vector>

#include <netinet/in.h>

using namespace std;
using boost::lexical_cast;
using boost::scoped_ptr;
using namespace isc::dns;

namespace {
// an ad hoc threadshold to prevent a busy loop due to an empty input file.
const size_t MAX_EMPTY_LOOP = 1000;

// Set of parameters of a request (mostly query, but may be of a different
// opcode)
struct RequestParam {
    RequestParam(QuestionPtr question_param, int proto_param) :
        question(question_param), proto(proto_param)
    {}

    // Default constructor.  Using some invalid initial values.
    RequestParam() : proto(IPPROTO_NONE) {}

    void setEDNSPolicy(bool use_dnssec_param, bool use_edns_param) {
        // For special types of queries, we don't use EDNS by default
        if (question->getType() == RRType::AXFR() ||
            question->getType() == RRType::IXFR()) {
            use_dnssec = false;
            use_edns = false;
        } else {
            use_dnssec = use_dnssec_param;
            use_edns = use_edns_param;
        }
    }

    QuestionPtr question;
    int proto;                  // Transport protocol
    vector<RRsetPtr> authorities;
    bool use_dnssec;
    bool use_edns;
};

struct QueryOptions {
    QueryOptions() {
        clear();
    }
    void clear() {
        serial = 0;
    }
    uint32_t serial;         // querier's serial, only useful for IXFR
};
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
        proto_ = IPPROTO_UDP;

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
        aux_typemap_["AXFR"] = "TYPE252";
        aux_typemap_["IXFR"] = "TYPE251";
    }

    // Extract the next question from the input stream
    QuestionPtr readNextRequest(vector<RRsetPtr>& authorities,
                                bool rewind);

    // Extract optional attributes of the query.  Used by readNextRequest.
    void parseQueryOptions(stringstream& ss);

    // Get the parameters of the next request, either from the preloaded
    // vector (if done) or from the input stream.
    const RequestParam& getNextParam();

    RRClass qclass_;            // Query class
    scoped_ptr<ifstream> input_ifs_;
    istream& input_;
    map<string, string> aux_typemap_;
    vector<RequestParam> params_;   // used in the "preload" mode
    bool use_edns_;                 // whether to include ENDS by default.
    bool use_dnssec_;               // whether to set EDNS DO bit by default.
                                    // EDNS will be included regardless of
                                    // use_edns_.
    EDNSPtr edns_;                  // template of common EDNS OPT RR
    int proto_;                     // Default transport protocol
    vector<RequestParam>::const_iterator current_param_;
    vector<RequestParam>::const_iterator end_param_;

    QueryOptions options_;

private:
    RequestParam param_placeholder_;
};

void
QueryRepository::QueryRepositoryImpl::parseQueryOptions(stringstream& ss) {
    while (!ss.eof()) {
        string option;
        ss >> option;

        const size_t pos_delim = option.find('=');
        if (pos_delim == string::npos) {
            throw QueryRepositoryError("Invalid query option: no '='");
        }
        const string optname = option.substr(0, pos_delim);
        const string optarg = option.substr(pos_delim + 1);

        // Set option: for now just hardcode known options.
        if (optname == "serial") {
            options_.serial = lexical_cast<uint32_t>(optarg);
        }
    }
}

QuestionPtr
QueryRepository::QueryRepositoryImpl::readNextRequest(
    vector<RRsetPtr>& authorities, bool rewind)
{
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
                line.clear();     // force ignoring this line.
            }
        }

        options_.clear();
        authorities.clear();
        stringstream ss(line);
        string qname_text, qtype_text;
        ss >> qname_text >> qtype_text;
        if (ss.bad() || ss.fail()) {
            // Ignore the line is organized in an unexpected way.
            continue;
        }
        if (!ss.eof()) {
            try {
                parseQueryOptions(ss);
            } catch (const std::exception& ex) {
                cerr << "Error parsing query option (" << ex.what() << "): "
                     << line << endl;
                continue;
            }
        }
        // Workaround for some RR types that are not recognized by BIND 10
        map<string, string>::const_iterator it =
            aux_typemap_.find(qtype_text);
        if (it != aux_typemap_.end()) {
            qtype_text = it->second;
        }
        try {
            const RRType qtype(qtype_text);
            const Name qname(qname_text);
            question.reset(new Question(qname, qclass_, qtype));

            // For IXFR, we need to add an SOA to the authority section.
            if (qtype == RRType::IXFR()) {
                RRsetPtr rrset(new RRset(qname, qclass_, qtype, RRTTL(0)));
                rrset->addRdata(rdata::createRdata(
                                    RRType::SOA(), qclass_,
                                    ". . " +
                                    lexical_cast<string>(options_.serial) +
                                    " 0 0 0 0"));
                authorities.push_back(rrset);
            }
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

const RequestParam&
QueryRepository::QueryRepositoryImpl::getNextParam() {
    if (!params_.empty()) {
        // queries have been preloaded.  get the next one from the vector.
        const RequestParam& param = *current_param_;
        if (++current_param_ == end_param_) {
            current_param_ = params_.begin();
        }
        return (param);
    }

    param_placeholder_.question =
        readNextRequest(param_placeholder_.authorities, true);
    param_placeholder_.proto = proto_;
    param_placeholder_.setEDNSPolicy(use_dnssec_, use_edns_);
    return (param_placeholder_);
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
        throw QueryRepositoryError("failed to open input data file: " +
                                   input_file);
    }
}

QueryRepository::~QueryRepository() {
    delete impl_;
}

void
QueryRepository::load() {
    // duplicate load check
    if (!impl_->params_.empty()) {
        throw QueryRepositoryError("duplicate preload attempt");
    }

    QuestionPtr question;
    vector<RRsetPtr> authorities;
    while ((question = impl_->readNextRequest(authorities, false))
           != NULL) {
        impl_->params_.push_back(RequestParam(question, impl_->proto_));
        impl_->params_.back().authorities = authorities;
        impl_->params_.back().setEDNSPolicy(impl_->use_dnssec_,
                                            impl_->use_edns_);
    }
    if (impl_->params_.empty()) {
        throw QueryRepositoryError("failed to preload queries: empty input");
    }
    impl_->current_param_ = impl_->params_.begin();
    impl_->end_param_ = impl_->params_.end();
}

size_t
QueryRepository::getQueryCount() const {
    return (impl_->params_.size());
}

void
QueryRepository::getNextQuery(Message& query_msg, int& protocol) {
    const RequestParam& param = impl_->getNextParam();

    query_msg.clear(Message::RENDER);
    query_msg.setOpcode(Opcode::QUERY());
    query_msg.setRcode(Rcode::NOERROR());
    query_msg.setHeaderFlag(Message::HEADERFLAG_RD);
    query_msg.addQuestion(param.question);
    BOOST_FOREACH(const RRsetPtr rrset, param.authorities) {
        query_msg.addRRset(Message::SECTION_AUTHORITY, rrset);
    }
    protocol = param.proto;
    if (param.use_edns || param.use_dnssec) {
        query_msg.setEDNS(impl_->edns_);
    }
}

void
QueryRepository::setQueryClass(RRClass qclass) {
    if (!impl_->params_.empty()) {
        throw QueryRepositoryError("query class is being set after preload");
    }

    impl_->qclass_ = qclass;
}

void
QueryRepository::setDNSSEC(bool on) {
    if (!impl_->params_.empty()) {
        throw QueryRepositoryError(
            "DNSSEC DO bit is being changed after preload");
    }

    impl_->use_dnssec_ = on;
    impl_->edns_->setDNSSECAwareness(on);
}

void
QueryRepository::setEDNS(bool on) {
    if (!impl_->params_.empty()) {
        throw QueryRepositoryError("EDNS flag is being changed after preload");
    }

    impl_->use_edns_ = on;
}

void
QueryRepository::setProtocol(int proto) {
    if (!impl_->params_.empty()) {
        throw QueryRepositoryError("Protocol is being changed after preload");
    }
    if (proto != IPPROTO_UDP && proto != IPPROTO_TCP) {
        throw QueryRepositoryError("Invalid or unsupported transport protocol "
                                   ": " + lexical_cast<string>(proto));
    }

    impl_->proto_ = proto;
}

} // end of QueryPerf

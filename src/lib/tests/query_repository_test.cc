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
#include <common_test.h>

#include <dns/name.h>
#include <dns/message.h>
#include <dns/opcode.h>
#include <dns/rcode.h>
#include <dns/rdata.h>
#include <dns/rrclass.h>
#include <dns/rrtype.h>

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <iostream>

#include <netinet/in.h>

using namespace std;
using namespace isc::dns;
using namespace Queryperf;
using namespace Queryperf::unittest;

namespace {
class QueryRepositoryTest : public ::testing::Test {
protected:
    QueryRepositoryTest() : msg(Message::RENDER) {}

    Message msg;
    int protocol;               // placeholder
};

void
initialCheck(QueryRepository& repo, Message& msg,
             int expected_proto = IPPROTO_UDP,
             bool expected_edns = true, bool expected_dnssec = true,
             RRClass expected_qclass = RRClass::IN())
{
    int protocol;

    repo.getNextQuery(msg, protocol);
    EXPECT_EQ(expected_proto, protocol);
    queryMessageCheck(msg, 0, Name("example.com"), RRType::SOA(),
                       default_expected_rr_counts,
                      expected_edns, expected_dnssec, expected_qclass);

    repo.getNextQuery(msg, protocol);
    EXPECT_EQ(expected_proto, protocol);
    queryMessageCheck(msg, 0, Name("www.example.com"), RRType::A(),
                       default_expected_rr_counts,
                      expected_edns, expected_dnssec, expected_qclass);

    // Should go to the first line
    repo.getNextQuery(msg, protocol);
    EXPECT_EQ(expected_proto, protocol);
    queryMessageCheck(msg, 0, Name("example.com"), RRType::SOA(),
                       default_expected_rr_counts,
                      expected_edns, expected_dnssec, expected_qclass);
}

TEST_F(QueryRepositoryTest, createFromString) {
    stringstream ss("example.com. SOA\nwww.example.com. A");
    QueryRepository repo(ss);
    // We didn't preload queries, so the query count should be 0.
    EXPECT_EQ(0, repo.getQueryCount());
    initialCheck(repo, msg);
}

TEST_F(QueryRepositoryTest, createFromStringWithPreload) {
    stringstream ss("example.com. SOA\nwww.example.com. A");
    QueryRepository repo(ss);

    // preload queries.  We should be able to know the # of queries.
    repo.load();
    EXPECT_EQ(2, repo.getQueryCount());

    initialCheck(repo, msg);
}

TEST_F(QueryRepositoryTest, duplicatePreload) {
    stringstream ss("example.com. SOA\nwww.example.com. A");
    QueryRepository repo(ss);

    // duplicate preload should be rejected.
    repo.load();
    EXPECT_THROW(repo.load(), QueryRepositoryError);
}

TEST_F(QueryRepositoryTest, createFromFile) {
    QueryRepository repo("test-input.txt");
    initialCheck(repo, msg);
}

TEST_F(QueryRepositoryTest, createFromNotExistentFile) {
    EXPECT_THROW(QueryRepository repo("nosuchfile.txt"), QueryRepositoryError);
}

TEST_F(QueryRepositoryTest, setQueryClass) {
    QueryRepository repo("test-input.txt");
    repo.setQueryClass(RRClass::CH());
    initialCheck(repo, msg, IPPROTO_UDP, true, true, RRClass::CH());

    // Query class cannot be set after preload.
    repo.load();
    EXPECT_THROW(repo.setQueryClass(RRClass::IN()), QueryRepositoryError);
}

TEST_F(QueryRepositoryTest, setDNSSECOff) {
    QueryRepository repo("test-input.txt");
    repo.setDNSSEC(false);
    initialCheck(repo, msg, IPPROTO_UDP, true, false);

    // DNSSEC setting cannot be changed after preload.
    repo.load();
    EXPECT_THROW(repo.setDNSSEC(true), QueryRepositoryError);
}

TEST_F(QueryRepositoryTest, setEDNSOff) {
    QueryRepository repo("test-input.txt");
    // Set it to false, but unless DNSSEC is also disabled, EDNS is still used.
    repo.setEDNS(false);
    initialCheck(repo, msg, IPPROTO_UDP, true, true);
}

TEST_F(QueryRepositoryTest, setEDNSAndDNSSECOff) {
    QueryRepository repo("test-input.txt");
    // Set both EDNS and DNSSEC to false.  Now EDNS is suppressed.
    repo.setEDNS(false);
    repo.setDNSSEC(false);
    initialCheck(repo, msg, IPPROTO_UDP, false, false);

    repo.load();
    EXPECT_THROW(repo.setEDNS(true), QueryRepositoryError);
}

TEST_F(QueryRepositoryTest, setProtocol) {
    QueryRepository repo("test-input.txt");
    repo.setProtocol(IPPROTO_TCP);
    initialCheck(repo, msg, IPPROTO_TCP);
    repo.setProtocol(IPPROTO_UDP); // setting UDP is also okay
    // Others will be rejected.
    EXPECT_THROW(repo.setProtocol(IPPROTO_NONE), QueryRepositoryError);

    // DNSSEC setting cannot be changed after preload.
    repo.load();
    EXPECT_THROW(repo.setProtocol(IPPROTO_UDP), QueryRepositoryError);
}

TEST_F(QueryRepositoryTest, ignoredLines) {
    // Some lines should be just ignored:
    // - empty line
    // - comments (starting with ';')
    // - bogus input: bad name/type, incomplete line
    stringstream ss("example..com. SOA\n" // name is bad
                    "www.example.com. BADTYPE\n" // RR type is bad
                    "\n" // empty line
                    "; A\n" // comment (shouldn't be confused with an input)
                    "example NS garbage\n" // trailing garbage (with warning)
                    "nameonly\n" // incomplete line
                    "mail.example.org. AAAA\n");
    QueryRepository repo(ss);
    repo.getNextQuery(msg, protocol);
    queryMessageCheck(msg, 0, Name("mail.example.org"), RRType::AAAA());
}

TEST_F(QueryRepositoryTest, emptyInput) {
    // a special case: the input data contains anything
    stringstream empty_stream;
    QueryRepository repo(empty_stream);
    EXPECT_THROW(repo.getNextQuery(msg, protocol), QueryRepositoryError);
}

TEST_F(QueryRepositoryTest, emptyInputWithPreload) {
    // same test for preloading
    stringstream empty_stream;
    QueryRepository repo(empty_stream);
    EXPECT_THROW(repo.load(), QueryRepositoryError);
}

TEST_F(QueryRepositoryTest, uncommonTypes) {
    // Some types are not yet fully recognized by the BIND 10 libdns++.
    // As a workaround these should be converted internally.
    stringstream ss("example.com. A6\n"
                    "www.example.com. ANY");
    QueryRepository repo(ss);
    repo.getNextQuery(msg, protocol);
    queryMessageCheck(msg, 0, Name("example.com"), RRType(38));
    repo.getNextQuery(msg, protocol);
    queryMessageCheck(msg, 0, Name("www.example.com"), RRType::ANY());
}

void
checkAXFR(QueryRepository& repo, Message& msg) {
    int protocol;

    repo.getNextQuery(msg, protocol);
    queryMessageCheck(msg, 0, Name("example.com"), RRType::AXFR(),
                      default_expected_rr_counts, false);

    // Unless specified by a per query option (not supported yet), AXFR queries
    // don't include EDNS.
    EXPECT_FALSE(msg.getEDNS());
}

TEST_F(QueryRepositoryTest, AXFR) {
    stringstream ss("example.com. AXFR\n");
    QueryRepository repo(ss);
    checkAXFR(repo, msg);
}

TEST_F(QueryRepositoryTest, AXFRPreload) {
    stringstream ss("example.com. AXFR\n");
    QueryRepository repo(ss);
    repo.load();
    checkAXFR(repo, msg);
}

void
checkIXFR(QueryRepository& repo, Message& msg) {
    int protocol;

    repo.getNextQuery(msg, protocol);

    // IXFR queries should have SOA in the authority section.
    const size_t expected_rr_counts[4] = {1, 0, 1, 0};
    queryMessageCheck(msg, 0, Name("example.com"), RRType::IXFR(),
                      expected_rr_counts, false);
    ConstRRsetPtr auth = *msg.beginSection(Message::SECTION_AUTHORITY);
    EXPECT_EQ(Name("example.com"), auth->getName());
    EXPECT_EQ(RRClass::IN(), auth->getClass());
    EXPECT_EQ(RRType::IXFR(), auth->getType());
    EXPECT_EQ(0, auth->getRdataIterator()->getCurrent().compare(
                  *rdata::createRdata(RRType::SOA(), RRClass::IN(),
                                      ". . 42 0 0 0 0")));

    // Unless specified by a per query option (not supported yet), IXFR queries
    // don't include EDNS.
    EXPECT_FALSE(msg.getEDNS());
}

TEST_F(QueryRepositoryTest, IXFR) {
    stringstream ss("example.com. IXFR serial=42\n");
    QueryRepository repo(ss);
    checkIXFR(repo, msg);
}

TEST_F(QueryRepositoryTest, IXFRPreload) {
    stringstream ss("example.com. IXFR serial=42\n");
    QueryRepository repo(ss);
    repo.load();
    checkIXFR(repo, msg);
}
}

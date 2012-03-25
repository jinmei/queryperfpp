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

#include <common_test.h>

#include <util/buffer.h>

#include <dns/name.h>
#include <dns/message.h>
#include <dns/opcode.h>
#include <dns/rcode.h>
#include <dns/rrtype.h>
#include <dns/rrclass.h>

#include <query_repository.h>
#include <query_context.h>

#include <gtest/gtest.h>

#include <sstream>

#include <netinet/in.h>

using namespace std;
using namespace isc::dns;
using namespace isc::util;
using namespace Queryperf;

namespace {
class QueryContextTest : public ::testing::Test {
protected:
    QueryContextTest() : input_ss("example.com. SOA\n"
                                  "www.example.com. A"),
                         repo(input_ss)
    {}

private:
    stringstream input_ss;
protected:
    QueryRepository repo;
};

void
messageCheck(const QueryContext::QuerySpec& msg_spec, qid_t expected_qid,
             const Name& expected_qname, RRType expected_qtype,
             int expected_proto = IPPROTO_UDP)
{
    EXPECT_EQ(expected_proto, msg_spec.proto);
    unittest::queryMessageCheck(msg_spec.data, msg_spec.len, expected_qid,
                                expected_qname, expected_qtype);
}

TEST_F(QueryContextTest, start) {
    const qid_t TEST_QID = 42;

    QueryContext ctx(repo);

    // Construct two messages reusing the same context, and check the
    // content.
    messageCheck(ctx.start(TEST_QID), TEST_QID, Name("example.com"),
                 RRType::SOA());
    messageCheck(ctx.start(TEST_QID + 1), TEST_QID + 1,
                 Name("www.example.com"), RRType::A());
}

TEST_F(QueryContextTest, setProtocol) {
    // Check the behavior when specifying a non default transport protocol.
    const qid_t TEST_QID = 4200;

    QueryContext ctx(repo);
    repo.setProtocol(IPPROTO_TCP);
    messageCheck(ctx.start(TEST_QID), TEST_QID, Name("example.com"),
                 RRType::SOA(), IPPROTO_TCP);
}

}

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

#include <gtest/gtest.h>

using namespace isc::dns;
using namespace isc::util;

namespace Queryperf {
namespace unittest {
size_t default_expected_rr_counts[4] = {1, 0, 0, 0};

void
queryMessageCheck(const void* data, size_t data_len, qid_t expected_qid,
                  const Name& expected_qname, RRType expected_qtype,
                  bool expected_edns, bool expected_dnssec,
                  RRClass expected_qclass)
{
    EXPECT_NE(0, data_len);
    ASSERT_NE(static_cast<const void*>(NULL), data);

    Message msg(Message::PARSE);
    InputBuffer buffer(data, data_len);
    msg.fromWire(buffer);
    queryMessageCheck(msg, expected_qid, expected_qname, expected_qtype,
                      default_expected_rr_counts, expected_edns,
                      expected_dnssec, expected_qclass);
}

void
queryMessageCheck(const Message& msg, qid_t expected_qid,
                  const Name& expected_qname, RRType expected_qtype,
                  const size_t expected_rr_counts[4],
                  bool expected_edns, bool expected_dnssec,
                  RRClass expected_qclass)
{
    EXPECT_EQ(Opcode::QUERY(), msg.getOpcode());
    EXPECT_EQ(Rcode::NOERROR(), msg.getRcode());
    EXPECT_EQ(expected_qid, msg.getQid());
    EXPECT_FALSE(msg.getHeaderFlag(Message::HEADERFLAG_QR));
    EXPECT_FALSE(msg.getHeaderFlag(Message::HEADERFLAG_AA));
    EXPECT_FALSE(msg.getHeaderFlag(Message::HEADERFLAG_TC));
    EXPECT_TRUE(msg.getHeaderFlag(Message::HEADERFLAG_RD));
    EXPECT_FALSE(msg.getHeaderFlag(Message::HEADERFLAG_RA));
    EXPECT_FALSE(msg.getHeaderFlag(Message::HEADERFLAG_AD));
    EXPECT_FALSE(msg.getHeaderFlag(Message::HEADERFLAG_CD));
    EXPECT_EQ(expected_rr_counts[0], msg.getRRCount(Message::SECTION_QUESTION));
    EXPECT_EQ(expected_rr_counts[1], msg.getRRCount(Message::SECTION_ANSWER));
    EXPECT_EQ(expected_rr_counts[2],
              msg.getRRCount(Message::SECTION_AUTHORITY));
    // Note: getRRCount doesn't take into account EDNS in this context
    EXPECT_EQ(expected_rr_counts[3],
              msg.getRRCount(Message::SECTION_ADDITIONAL));
    QuestionIterator qit = msg.beginQuestion();
    ASSERT_FALSE(qit == msg.endQuestion());
    EXPECT_EQ(expected_qname, (*qit)->getName());
    EXPECT_EQ(expected_qtype, (*qit)->getType());
    EXPECT_EQ(expected_qclass, (*qit)->getClass());

    EXPECT_EQ(expected_edns, msg.getEDNS() != NULL);
    if (msg.getEDNS()) {
        if (expected_dnssec) {
            EXPECT_TRUE(msg.getEDNS()->getDNSSECAwareness());
        } else {
            EXPECT_FALSE(msg.getEDNS()->getDNSSECAwareness());
        }
    }
}
} // end of unittest
} // end of Queryperf

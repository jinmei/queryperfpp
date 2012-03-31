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

#ifndef __QUERYPERF_COMMON_TEST_H
#define __QUERYPERF_COMMON_TEST_H 1

#include <dns/message.h>
#include <dns/name.h>
#include <dns/rrtype.h>

#include <sys/types.h>

namespace Queryperf {
namespace unittest {
extern size_t default_expected_rr_counts[4];

void
queryMessageCheck(const void* data, size_t data_len,
                  isc::dns::qid_t expected_qid,
                  const isc::dns::Name& expected_qname,
                  isc::dns::RRType expected_qtype,
                  bool expected_edns = true,
                  bool expected_dnssec = true,
                  isc::dns::RRClass expected_qclass = isc::dns::RRClass::IN());

void
queryMessageCheck(const isc::dns::Message& msg, isc::dns::qid_t expected_qid,
                  const isc::dns::Name& expected_qname,
                  isc::dns::RRType expected_qtype,
                  const size_t expected_rr_counts[4] =
                  default_expected_rr_counts,
                  bool expected_edns = true,
                  bool expected_dnssec = true,
                  isc::dns::RRClass expected_qclass = isc::dns::RRClass::IN());
} // end of unittest
} // end of QueryPerf

#endif // __QUERYPERF_COMMON_TEST_H 

// Local Variables:
// mode: c++
// End:

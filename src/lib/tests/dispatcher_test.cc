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

#include <test_message_manager.h>
#include <common_test.h>

#include <query_repository.h>
#include <query_context.h>
#include <dispatcher.h>
#include <common_test.h>

#include <dns/message.h>
#include <dns/name.h>
#include <dns/rrtype.h>

#include <gtest/gtest.h>

#include <boost/bind.hpp>

#include <sstream>

using namespace std;
using namespace isc::dns;
using namespace Queryperf;
using namespace Queryperf::unittest;

namespace {
class DispatcherTest : public ::testing::Test {
protected:
    DispatcherTest() : ss("example.com. SOA\n"
                          "www.example.com. A"),
                       repo(ss), ctx_creator(repo),
                       disp(msg_mgr, ctx_creator)
    {}

private:
    stringstream ss;
    QueryRepository repo;
    QueryContextCreator ctx_creator;

protected:
    Dispatcher disp;

public:
    // refererenced from callbacks
    TestMessageManager msg_mgr;
};

void
initialQueryCheck(DispatcherTest* test) {
    // Examine the queries recorded in the manager and check if these are
    // the expected ones.

    ASSERT_TRUE(test->msg_mgr.socket_);
    EXPECT_EQ(20, test->msg_mgr.socket_->queries_.size());
    for (size_t i = 0; i < test->msg_mgr.socket_->queries_.size(); ++i) {
        queryMessageCheck(*test->msg_mgr.socket_->queries_[i], i,
                          (i % 2) == 0 ? Name("example.com") :
                          Name("www.example.com"),
                          (i % 2) == 0 ? RRType::SOA() :
                          RRType::A());
    }

    // Reset the handler
    test->msg_mgr.setRunHandler(NULL);
}

TEST_F(DispatcherTest, initialQueries) {
    msg_mgr.setRunHandler(boost::bind(initialQueryCheck, this));
    disp.run();
}
} // unnamed namespace

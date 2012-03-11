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
#include <dns/messagerenderer.h>
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

    // Stop the manager
    test->msg_mgr.stop();
}

TEST_F(DispatcherTest, initialQueries) {
    msg_mgr.setRunHandler(boost::bind(initialQueryCheck, this));
    disp.run();
}

void
respondToQuery(TestMessageManager* mgr, size_t qid) {
    // Respond to the specified position of query
    Message& query = *mgr->socket_->queries_.at(qid);
    query.makeResponse();
    MessageRenderer renderer;
    query.toWire(renderer);
    mgr->socket_->callback_(MessageSocket::Event(renderer.getData(),
                                                 renderer.getLength()));

    // Another query should have been sent immediatelly, and should be
    // recorded in the manager.
    EXPECT_EQ(21 + qid, mgr->socket_->queries_.size());

    // Continue this until we respond to all initial queries and the first
    // query in the second round.
    if (qid < 20) {
        mgr->setRunHandler(boost::bind(respondToQuery, mgr, qid + 1));
    } else {
        mgr->stop();
    }
}

TEST_F(DispatcherTest, nextQuery) {
    msg_mgr.setRunHandler(boost::bind(respondToQuery, &msg_mgr, 0));
    disp.run();

    // On completion, the first 20 and an additional one query has been
    // responded.  We'll check that the rest of the queries are expected ones.
    EXPECT_EQ(41, msg_mgr.socket_->queries_.size());
    for (size_t i = 21; i < msg_mgr.socket_->queries_.size(); ++i) {
        queryMessageCheck(*msg_mgr.socket_->queries_[i], i,
                          (i % 2) == 0 ? Name("example.com") :
                          Name("www.example.com"),
                          (i % 2) == 0 ? RRType::SOA() :
                          RRType::A());
    }
}

void
sendBadResponse(TestMessageManager* mgr) {
    // Respond to the specified position of query
    Message& query = *mgr->socket_->queries_.at(0);
    query.makeResponse();
    query.setQid(65535);        // set to incorrect QID
    MessageRenderer renderer;
    query.toWire(renderer);
    mgr->socket_->callback_(MessageSocket::Event(renderer.getData(),
                                                 renderer.getLength()));
    mgr->stop();
}

TEST_F(DispatcherTest, queryMismatch) {
    // Will respond to the first query with a mismatched QID
    msg_mgr.setRunHandler(boost::bind(sendBadResponse, &msg_mgr));
    disp.run();

    // The bad response should be ignored, and the queue size should be the
    // same.
    EXPECT_EQ(20, msg_mgr.socket_->queries_.size());
}

void
respondToQueryForDuration(TestMessageManager* mgr, size_t qid) {
    // If we reach the "duration" after the initial queries, we have responded
    // to all queries; the dispatcher should stop the manager.
    if (qid == mgr->timers_[0]->duration_seconds_ + 20) {
        return;
    }

    // Simplest of query responder: always return a simple response
    Message& query = *mgr->socket_->queries_.at(qid);
    query.makeResponse();
    MessageRenderer renderer;
    query.toWire(renderer);
    mgr->socket_->callback_(MessageSocket::Event(renderer.getData(),
                                                 renderer.getLength()));
    ASSERT_FALSE(mgr->timers_.empty());
    if (++qid == mgr->timers_[0]->duration_seconds_) {
        // Assume the session duration has expired.  Do callback, then no more
        // query should come.
        mgr->timers_[0]->callback_();
    }
    mgr->setRunHandler(boost::bind(respondToQueryForDuration, mgr, qid));
}

TEST_F(DispatcherTest, sessionTimer) {
    msg_mgr.setRunHandler(boost::bind(respondToQueryForDuration, &msg_mgr,
                                      0));
    disp.run();

    // Check if the session timer has started and its duration is correct.
    // The first timer in the timers_ queue should be the session timer.
    ASSERT_FALSE(msg_mgr.timers_.empty());
    EXPECT_TRUE(msg_mgr.timers_[0]->started_);
    EXPECT_EQ(30, msg_mgr.timers_[0]->duration_seconds_);

    // There should have been int(duration) + #initial queries = 50, and the
    // last one should have been responded.
    EXPECT_EQ(50, msg_mgr.socket_->queries_.size());
    EXPECT_TRUE(msg_mgr.socket_->queries_.back()->getHeaderFlag(
                    Message::HEADERFLAG_QR));
}

} // unnamed namespace

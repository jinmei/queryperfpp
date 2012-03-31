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
#include <boost/date_time/posix_time/posix_time.hpp>

#include <sstream>
#include <vector>

#include <netinet/in.h>

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
protected:
    QueryRepository repo; // protected so we can change protocol from test
private:
    QueryContextCreator ctx_creator;

protected:
    Dispatcher disp;

public:
    // refererenced from callbacks
    TestMessageManager msg_mgr;
};

void
initialTimerCheck(DispatcherTest* test) {
    // Each of the queries have associated timers, following the session timer.
    // All timers should have just started.
    EXPECT_EQ(21, test->msg_mgr.timers_.size());

    for (vector<TestMessageTimer*>::const_iterator it =
             test->msg_mgr.timers_.begin();
         it != test->msg_mgr.timers_.end();
         ++it) {
        EXPECT_EQ(1, (*it)->n_started_);
        // Check the timer duration: 30 for session timer, 5 for query timers.
        if (it == test->msg_mgr.timers_.begin()) {
            EXPECT_EQ(30, (*it)->duration_seconds_);
        } else {
            EXPECT_EQ(5, (*it)->duration_seconds_);
        }
    }
}

void
initialQueryCheck(DispatcherTest* test) {
    // Examine the queries recorded in the manager and check if these are
    // the expected ones.

    ASSERT_TRUE(test->msg_mgr.socket_);
    // There should be 20 initial queries queued.
    EXPECT_EQ(20, test->msg_mgr.socket_->queries_.size());

    for (size_t i = 0; i < test->msg_mgr.socket_->queries_.size(); ++i) {
        queryMessageCheck(*test->msg_mgr.socket_->queries_[i], i,
                          (i % 2) == 0 ? Name("example.com") :
                          Name("www.example.com"),
                          (i % 2) == 0 ? RRType::SOA() :
                          RRType::A());
    }

    initialTimerCheck(test);

    // Stop the manager
    test->msg_mgr.stop();
}

void
initialTCPQueryCheck(DispatcherTest* test) {
    // UDP socket is always created, but in this scenario it's not used
    ASSERT_TRUE(test->msg_mgr.socket_);
    EXPECT_EQ(0, test->msg_mgr.socket_->queries_.size());

    // There should be 20 TCP sockets created, each has exactly one query.
    EXPECT_EQ(20, test->msg_mgr.tcp_sockets_.size());
    size_t i = 0;
    for (vector<TestMessageSocket*>::const_iterator s =
             test->msg_mgr.tcp_sockets_.begin();
         s != test->msg_mgr.tcp_sockets_.end();
         ++s, ++i) {
        EXPECT_EQ(1, (*s)->queries_.size());
        queryMessageCheck(*(*s)->queries_[0], i,
                          (i % 2) == 0 ? Name("example.com") :
                          Name("www.example.com"),
                          (i % 2) == 0 ? RRType::SOA() :
                          RRType::A());
    }

    initialTimerCheck(test);

    // Stop the manager
    test->msg_mgr.stop();
}


TEST_F(DispatcherTest, initialQueries) {
    msg_mgr.setRunHandler(boost::bind(initialQueryCheck, this));
    EXPECT_EQ(0, disp.getQueriesSent());
    EXPECT_EQ(0, disp.getQueriesCompleted());
    disp.run();
    EXPECT_EQ(20, disp.getQueriesSent());
    EXPECT_EQ(0, disp.getQueriesCompleted());
}

TEST_F(DispatcherTest, initialTCPQueries) {
    msg_mgr.setRunHandler(boost::bind(initialTCPQueryCheck, this));
    repo.setProtocol(IPPROTO_TCP);

    EXPECT_EQ(0, disp.getQueriesSent());
    EXPECT_EQ(0, disp.getQueriesCompleted());
    disp.run();
    EXPECT_EQ(20, disp.getQueriesSent());
    EXPECT_EQ(0, disp.getQueriesCompleted());
    // No timeout or response yet, so all TCP sockets are still active.
    EXPECT_EQ(0, msg_mgr.n_deleted_sockets_);
}

void
respondToQuery(TestMessageManager* mgr, size_t qid, int proto) {
    // Respond to the specified position of query
    Message& query = (proto == IPPROTO_UDP) ? *mgr->socket_->queries_.at(qid) :
        *mgr->tcp_sockets_.at(qid)->queries_.at(0);
    query.makeResponse();
    MessageRenderer renderer;
    query.toWire(renderer);
    if (proto == IPPROTO_UDP) {
        mgr->socket_->callback_(MessageSocket::Event(renderer.getData(),
                                                     renderer.getLength()));
    } else {
        mgr->tcp_sockets_.at(qid)->callback_(
            MessageSocket::Event(renderer.getData(), renderer.getLength()));
    }

    // Another query should have been sent immediately, and should be
    // recorded in the manager.
    if (proto == IPPROTO_UDP) {
        EXPECT_EQ(21 + qid, mgr->socket_->queries_.size());
    } else {
        EXPECT_EQ(21 + qid, mgr->tcp_sockets_.size());

        // In the case of TCP, the completed TCP sockets should have been
        // deleted.
        EXPECT_EQ(1 + qid, mgr->n_deleted_sockets_);
    }

    // The corresponding timer should have been restarted.
    EXPECT_EQ((qid / 20) + 2, mgr->timers_.at((qid % 20) + 1)->n_started_);

    // Continue this until we respond to all initial queries and the first
    // query in the second round.
    if (qid < 20) {
        mgr->setRunHandler(boost::bind(respondToQuery, mgr, qid + 1, proto));
    } else {
        mgr->stop();
    }
}

TEST_F(DispatcherTest, nextQuery) {
    const int proto = IPPROTO_UDP;
    msg_mgr.setRunHandler(boost::bind(&respondToQuery, &msg_mgr, 0, proto));
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

TEST_F(DispatcherTest, nextQueryTCP) {
    // Same test as the previous one, but for TCP
    const int proto = IPPROTO_TCP;
    msg_mgr.setRunHandler(boost::bind(respondToQuery, &msg_mgr, 0, proto));
    repo.setProtocol(IPPROTO_TCP);
    disp.run();

    EXPECT_EQ(41, msg_mgr.tcp_sockets_.size());
    for (size_t i = 21; i < msg_mgr.tcp_sockets_.size(); ++i) {
        queryMessageCheck(*msg_mgr.tcp_sockets_.at(i)->queries_[0], i,
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
queryTimeoutCallback(TestMessageManager* mgr, int proto) {
    // Do timeout callcack for the first query.
    mgr->timers_.at(1)->callback_();

    // Then another query should have been sent.
    if (proto == IPPROTO_UDP) {
        EXPECT_EQ(21, mgr->socket_->queries_.size());
    } else {
        EXPECT_EQ(21, mgr->tcp_sockets_.size());

        // The socket for the timed out query should have been deleted.
        EXPECT_EQ(1, mgr->n_deleted_sockets_);
    }

    mgr->stop();
}

TEST_F(DispatcherTest, queryTimeout) {
    const int proto = IPPROTO_UDP;
    msg_mgr.setRunHandler(boost::bind(queryTimeoutCallback, &msg_mgr, proto));
    disp.run();

    // No queries should have been considered completed.
    EXPECT_EQ(0, disp.getQueriesCompleted());
}

TEST_F(DispatcherTest, queryTimeoutTCP) {
    // Same test as the previous one, but using TCP.
    const int proto = IPPROTO_TCP;
    msg_mgr.setRunHandler(boost::bind(queryTimeoutCallback, &msg_mgr, proto));
    repo.setProtocol(IPPROTO_TCP);
    disp.run();

    // No queries should have been considered completed.
    EXPECT_EQ(0, disp.getQueriesCompleted());
}

void
respondToQueryForDuration(TestMessageManager* mgr, size_t qid) {
    // If we reach the "duration" after the initial queries, we have responded
    // to all queries; the dispatcher should stop the manager.
    if (qid == mgr->timers_[0]->duration_seconds_ + 20) {
        return;
    }

    // Simplest form of query responder: always return a simple response
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
    msg_mgr.setRunHandler(boost::bind(respondToQueryForDuration, &msg_mgr, 0));
    disp.run();

    // Check if the session timer has started and its duration is correct.
    // The first timer in the timers_ queue should be the session timer.
    ASSERT_FALSE(msg_mgr.timers_.empty());
    EXPECT_EQ(1, msg_mgr.timers_[0]->n_started_);
    EXPECT_EQ(30, msg_mgr.timers_[0]->duration_seconds_);

    // There should have been int(duration) + #initial queries = 50, and the
    // last one should have been responded.
    EXPECT_EQ(50, msg_mgr.socket_->queries_.size());
    EXPECT_TRUE(msg_mgr.socket_->queries_.back()->getHeaderFlag(
                    Message::HEADERFLAG_QR));

    EXPECT_EQ(50, disp.getQueriesSent());
    EXPECT_EQ(50, disp.getQueriesCompleted());
    EXPECT_FALSE(disp.getStartTime().is_special());
    EXPECT_FALSE(disp.getEndTime().is_special());
    EXPECT_TRUE(disp.getStartTime() < disp.getEndTime());
}

TEST_F(DispatcherTest, builtins) {
    // creating dispatcher with "builtin" support classes.  No disruption
    // should happen.
    Dispatcher disp("test-input.txt");

    // They try preload queries.  Again, no disruption should happen.
    disp.loadQueries();

    // Duplicate preload should be rejected (by the internal repository).
    EXPECT_THROW(disp.loadQueries(), QueryRepositoryError);
}

TEST_F(DispatcherTest, preloadAfterRun) {
    Dispatcher disp("test-input.txt");
    // There's no server to be tested, so the send attempt should fail
    EXPECT_THROW(disp.run(), MessageSocketError);

    // preload can be done only before running the test.
    EXPECT_THROW(disp.loadQueries(), DispatcherError);
}

TEST_F(DispatcherTest, preloadForExternalRepository) {
    // preload for external query repository is prohibited
    EXPECT_THROW(disp.loadQueries(), DispatcherError);
}

TEST_F(DispatcherTest, setQclass) {
    Dispatcher disp("test-input.txt");
    // Invalid RR class text should be rejected
    EXPECT_THROW(disp.setDefaultQueryClass("no_such_class"), DispatcherError);
    // This is okay
    disp.setDefaultQueryClass("CH");
}

TEST_F(DispatcherTest, setQclassForExternalRepository) {
    // qclass cannot be specified for external query repository
    EXPECT_THROW(disp.setDefaultQueryClass("CH"), DispatcherError);
}

TEST_F(DispatcherTest, setQclassAfterRun) {
    Dispatcher disp("test-input.txt");
    // There's no server to be tested, so the send attempt should fail
    EXPECT_THROW(disp.run(), MessageSocketError);

    // query class can be set only before running the test.
    EXPECT_THROW(disp.setDefaultQueryClass("CH"), DispatcherError);
}

TEST_F(DispatcherTest, setDNSSEC) {
    Dispatcher disp("test-input.txt");
    disp.setDNSSEC(false);      // this shouldn't cause disruption
    EXPECT_THROW(disp.run(), MessageSocketError);
    // this can be set only before running the test.
    EXPECT_THROW(disp.setDNSSEC(true), DispatcherError);
}

TEST_F(DispatcherTest, setDNSSECForExternalRepository) {
    EXPECT_THROW(disp.setDNSSEC(false), DispatcherError);
}

TEST_F(DispatcherTest, setEDNS) {
    Dispatcher disp("test-input.txt");
    disp.setEDNS(false);      // this shouldn't cause disruption
    EXPECT_THROW(disp.run(), MessageSocketError);
    // this can be set only before running the test.
    EXPECT_THROW(disp.setEDNS(true), DispatcherError);
}

TEST_F(DispatcherTest, setEDNSForExternalRepository) {
    EXPECT_THROW(disp.setEDNS(false), DispatcherError);
}

TEST_F(DispatcherTest, setProtocol) {
    Dispatcher disp("test-input.txt");
    disp.setProtocol(IPPROTO_UDP);
    EXPECT_THROW(disp.run(), MessageSocketError);
    EXPECT_THROW(disp.setProtocol(IPPROTO_TCP), DispatcherError);
}

TEST_F(DispatcherTest, setProtocolRepository) {
    EXPECT_THROW(disp.setProtocol(IPPROTO_TCP), DispatcherError);
}

TEST_F(DispatcherTest, serverAddress) {
    // Default server address
    EXPECT_EQ("::1", disp.getServerAddress());

    // Reset it.
    disp.setServerAddress("127.0.0.1");
    EXPECT_EQ("127.0.0.1", disp.getServerAddress());

    // Once started it cannot be changed.
    disp.run();
    EXPECT_THROW(disp.setServerAddress("::1"), DispatcherError);
}

TEST_F(DispatcherTest, serverPort) {
    // Default server port
    EXPECT_EQ(53, disp.getServerPort());

    // Reset it.
    disp.setServerPort(5300);
    EXPECT_EQ(5300, disp.getServerPort());

    // Once started it cannot be changed.
    disp.run();
    EXPECT_THROW(disp.setServerAddress("::1"), DispatcherError);
}

TEST_F(DispatcherTest, testDuration) {
    // Default test duration
    EXPECT_EQ(30, disp.getTestDuration());

    // Reset it.
    disp.setTestDuration(60);
    EXPECT_EQ(60, disp.getTestDuration());

    // Once started it cannot be changed.
    disp.run();
    EXPECT_THROW(disp.setTestDuration(120), DispatcherError);
}

} // unnamed namespace

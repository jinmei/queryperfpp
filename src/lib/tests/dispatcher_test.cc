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
#include <query_context.h>
#include <dispatcher.h>

#include <test_message_manager.h>

#include <gtest/gtest.h>

#include <sstream>

using namespace std;
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
    TestMessageManager msg_mgr;
    Dispatcher disp;
};

TEST_F(DispatcherTest, initialRun) {
    disp.run();
}
} // unnamed namespace

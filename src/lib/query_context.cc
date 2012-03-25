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

#include <dns/message.h>
#include <dns/messagerenderer.h>

using namespace isc::dns;

namespace Queryperf {

struct QueryContext::QueryContextImpl {
    QueryContextImpl(QueryRepository& repository) :
        repository_(&repository), query_msg_(Message::RENDER)
    {}

    QueryRepository* repository_;
    Message query_msg_;
    MessageRenderer query_renderer_;
};

QueryContext::QueryContext(QueryRepository& repository) :
    impl_(new QueryContextImpl(repository))
{}

QueryContext::~QueryContext() {
    delete impl_;
}

QueryContext::QuerySpec
QueryContext::start(qid_t qid) {
    int protocol;
    impl_->repository_->getNextQuery(impl_->query_msg_, protocol);
    impl_->query_msg_.setQid(qid);
    impl_->query_renderer_.clear();
    impl_->query_msg_.toWire(impl_->query_renderer_);
    return (QuerySpec(protocol, impl_->query_renderer_.getData(),
                      impl_->query_renderer_.getLength()));
}

QueryContext*
QueryContextCreator::create() {
    return (new QueryContext(repository_));
}

} // end of QueryPerf

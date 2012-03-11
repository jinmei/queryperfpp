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

#include <message_manager.h>
#include <asio_message_manager.h>

#include <asio.hpp>

#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/lexical_cast.hpp>

#include <string>

#include <stdint.h>

using namespace std;
using asio::io_service;
using asio::ip::udp;
using boost::lexical_cast;

namespace Queryperf {

class UDPMessageSocket : public ASIOMessageSocket {
public:
    UDPMessageSocket(io_service& io_service, const std::string& address,
                     uint16_t port, Callback callback);
    virtual void send(const void* data, size_t datalen);

    virtual int native() { return (asio_sock_.native()); }

private:
    udp::socket asio_sock_;
};

UDPMessageSocket::UDPMessageSocket(io_service& io_service,
                                   const string& address,
                                   uint16_t port, Callback /*callback*/) :
    asio_sock_(io_service)
{
    try {
        const udp::endpoint dest(asio::ip::address::from_string(address),
                                 port);
        asio_sock_.connect(dest);
    } catch (const asio::system_error& e) {
        throw MessageSocketError(string("Failed to create a socket: ") +
                                 e.what());
    }
}

void
UDPMessageSocket::send(const void* data, size_t datalen) {
    asio_sock_.send(asio::buffer(data, datalen));
}

struct ASIOMessageManager::ASIOMessageManagerImpl {
    io_service io_service_;
};

ASIOMessageManager::ASIOMessageManager() :
    impl_(new ASIOMessageManagerImpl)
{}

ASIOMessageManager::~ASIOMessageManager() {
    delete impl_;
}

MessageSocket*
ASIOMessageManager::createMessageSocket(int proto, const string& address,
                                        uint16_t port,
                                        MessageSocket::Callback callback)
{
    if (!callback) {
        throw MessageSocketError("null socket callback specified");
    }
    if (proto == IPPROTO_UDP) {
        return (new UDPMessageSocket(impl_->io_service_, address, port,
                                     callback));
    }
    throw MessageSocketError("unsupported or invalid protocol: " +
                             lexical_cast<string>(proto));
}

MessageTimer*
ASIOMessageManager::createMessageTimer(MessageTimer::Callback /*callback*/) {
    return (NULL);
}

void
ASIOMessageManager::run() {
}

void
ASIOMessageManager::stop() {
}

} // end of QueryPerf

// SPDX-License-Identifier: MS-PL
#pragma once
#include "RelayMessageAssembler.hpp"
#include "CNA/GamerServices/Configuration.hpp"
#include "CNA/Internal/GamerServices/ServiceSessionDirectory.hpp"
#include <memory>
#include <stop_token>

namespace CNA::Internal::Net {
/** @brief Derives the relay URL from the validated deployment authority, never a response URL.
 * @param configuration Credential-free deployment settings. @return Validated ws/wss URL. */
std::string relayEndpoint(const CNA::GamerServices::Configuration& configuration);
/** @brief One worker-owned verified WebSocket; never calls XNA objects or callbacks. */
class RelayWebSocket {
public:
    /** @brief Establishes bounded TLS/upgrade/hello and validates authority before returning.
     * @param configuration Deployment trust settings. @param ticket One-use authority, consumed.
     * @param stop Cancellation for connect/hello. */
    RelayWebSocket(const CNA::GamerServices::Configuration& configuration,
        GamerServices::ServiceRelayTicket ticket,std::stop_token stop);
    /** @brief Closes the owned transport without detached work. */
    ~RelayWebSocket();
    RelayWebSocket(const RelayWebSocket&)=delete;
    RelayWebSocket& operator=(const RelayWebSocket&)=delete;
    /** @brief Attempts bounded nonblocking receipt; controls are validated and skipped.
     * @param progressed Whether a transport chunk was consumed.
     * @return Owned binary message, if complete. */
    std::optional<std::vector<unsigned char>> receive(bool& progressed);
    /** @brief Resumes one owned binary frame; caller keeps its bytes stable until completion.
     * @param frame Complete validated envelope. @param offset Updated consumed count.
     * @return Whether the entire frame was consumed. */
    bool send(std::span<const unsigned char> frame,std::size_t& offset);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}

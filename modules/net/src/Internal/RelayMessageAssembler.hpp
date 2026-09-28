// SPDX-License-Identifier: MS-PL
#pragma once
#include "CnaService/RelayProtocol.hpp"
#include <optional>

namespace CNA::Internal::Net {
/** @brief Private WebSocket message kinds, independent of libcurl numeric flags. */
enum class RelayMessageKind { Text,Binary,Continuation,Ping,Pong,Close };
/** @brief One validated transport chunk; bytes borrow the adapter's scratch buffer. */
struct RelayChunk {
    RelayMessageKind kind=RelayMessageKind::Binary;
    bool moreFragments=false;
    std::size_t offset=0,bytesLeft=0;
    std::span<const unsigned char> bytes;
};
/** @brief Completed owned message; control messages do not consume partial data messages. */
struct RelayMessage {
    RelayMessageKind kind;
    std::vector<unsigned char> bytes;
};
/** @brief Bounded private frame/chunk reassembly with control-message interleaving. */
class RelayMessageAssembler {
public:
    /** @brief Sets trusted client limits before receiving.
     * @param maxBytes Message ceiling, at most the canonical relay ceiling.
     * @param maxFragments Client fragment ceiling, between one and 64. */
    explicit RelayMessageAssembler(std::size_t maxBytes=CnaService::MaxRelayFrameBytes,std::size_t maxFragments=64);
    /** @brief Validates metadata and lengths before copying any bytes.
     * @param chunk Borrowed transport chunk. @return Completed owned message, if any. */
    std::optional<RelayMessage> push(const RelayChunk& chunk);
    /** @brief Discards partial state after failure or connection teardown. */
    void reset() noexcept;
private:
    [[noreturn]] void fail(const char* code);
    struct Frame {RelayMessageKind kind;bool more;std::size_t next=0,remaining=0;};
    const std::size_t maxBytes_,maxFragments_;
    std::optional<Frame> frame_;
    std::optional<RelayMessageKind> messageKind_;
    std::size_t fragments_=0;
    std::vector<unsigned char> data_,control_;
};
/** @brief Validates the bounded server welcome before a worker publishes transport readiness.
 * @param bytes Complete text response. @param requestId Exact connection request correlation.
 * @param session Expected ticket-bound session. @param machine Expected ticket-bound machine. */
void validateRelayWelcome(std::string_view bytes,std::string_view requestId,std::string_view session,std::string_view machine);
}

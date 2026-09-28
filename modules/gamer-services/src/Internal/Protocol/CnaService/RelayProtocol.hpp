// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace CnaService {
/** @brief Version of the independent realtime relay envelope. */
inline constexpr unsigned char RelayVersion=1;
/** @brief Fixed magic/version/type/reserved/machine envelope size. */
inline constexpr std::size_t RelayHeaderBytes=24;
/** @brief Maximum encapsulated ENet UDP datagram; covers ENet's maximum MTU. */
inline constexpr std::size_t MaxRelayDatagramBytes=4096;
/** @brief Maximum complete binary relay message. */
inline constexpr std::size_t MaxRelayFrameBytes=RelayHeaderBytes+MaxRelayDatagramBytes;
/** @brief Maximum queued frames per connection. */
inline constexpr std::size_t MaxRelayQueuedFrames=64;
/** @brief Maximum queued bytes per connection. */
inline constexpr std::size_t MaxRelayQueuedBytes=MaxRelayQueuedFrames*MaxRelayFrameBytes;
/** @brief One-use connection ticket lifetime in seconds. */
inline constexpr int RelayTicketLifetimeSeconds=60;
/** @brief Maximum established grant lifetime; reconnect requires fresh authority. */
inline constexpr int RelayGrantLifetimeSeconds=3600;
/** @brief Maximum outstanding ticket/grant records per machine. */
inline constexpr int MaxMachineRelayTickets=8;
/** @brief Maximum ticket/grant records per title. */
inline constexpr int MaxTitleRelayTickets=8192;
/** @brief Separate relay upgrade target; credentials never appear in its URL. */
inline constexpr std::string_view RelayPath="/cna/relay/v1";
/** @brief Maximum first text authentication message. */
inline constexpr std::size_t MaxRelayHelloBytes=1024;
/** @brief Maximum datagrams or peer control frames per one-second window. */
inline constexpr std::size_t MaxRelayMessagesPerSecond=512;
/** @brief Maximum received binary bytes per one-second window. */
inline constexpr std::size_t MaxRelayBytesPerSecond=1048576;
/** @brief Maximum relay connections; reserves capacity for control requests. */
inline constexpr std::size_t MaxRelayConnections=96;
/** @brief Authentication deadline after WebSocket upgrade. */
inline constexpr int RelayAuthenticationSeconds=5;
/** @brief Maximum periodic grant revocation detection delay. */
inline constexpr int RelayValidationSeconds=5;
/** @brief Machine identity as sixteen opaque service-assigned bytes. */
using RelayMachineId=std::array<unsigned char,16>;
/** @brief Deterministic parse refusal; never includes payload or credentials. */
class RelayError : public std::runtime_error {
public:
    /** @brief Constructs a refusal. @param code Constant safe code. */
    explicit RelayError(std::string code);
    /** @brief Gets the safe failure code. @return Constant code. */
    const std::string& code() const noexcept;
private:
    std::string code_;
};
/** @brief Validated frame view borrowing the caller-owned message buffer. */
struct RelayFrameView {
    /** @brief Destination on client sends; authenticated source on server sends. */
    RelayMachineId machine{};
    /** @brief Encapsulated datagram; remains valid only while the input buffer lives. */
    std::span<const unsigned char> datagram;
};
/** @brief Parses one bounded complete binary relay message before any payload allocation.
 * @param bytes Complete message. @return Validated borrowed view. */
RelayFrameView parseRelayFrame(std::span<const unsigned char> bytes);
/** @brief Encodes one bounded message. @param machine Authorized destination/source.
 * @param datagram Encapsulated bytes. @return Complete binary message. */
std::vector<unsigned char> encodeRelayFrame(const RelayMachineId& machine,std::span<const unsigned char> datagram);
/** @brief Converts an opaque lowercase service machine ID. @param hex Thirty-two hex digits.
 * @return Sixteen bytes; all-zero IDs are forbidden. */
RelayMachineId relayMachineId(std::string_view hex);
/** @brief Formats a valid nonzero machine ID. @param machine Opaque ID. @return Lowercase hex. */
std::string relayMachineName(const RelayMachineId& machine);
}

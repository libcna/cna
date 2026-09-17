// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Inspector/Protocol.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <sys/types.h>
#endif

namespace CNA::Inspector::Detail
{
#if defined(_WIN32)
    using SocketHandle = SOCKET;
    inline constexpr SocketHandle InvalidSocket = INVALID_SOCKET;
#else
    using SocketHandle = int;
    inline constexpr SocketHandle InvalidSocket = -1;
#endif

    [[nodiscard]] bool InitializeSockets(std::string& error);
    [[nodiscard]] bool GenerateSecureRandom(std::span<std::uint8_t> bytes, std::string& error);
    [[nodiscard]] bool IsLoopbackAddress(std::string_view address);
    [[nodiscard]] SocketHandle CreateTcpListener(std::string_view address,
                                                 std::uint16_t requestedPort,
                                                 std::uint16_t& boundPort,
                                                 std::string& error);
    [[nodiscard]] SocketHandle AcceptTcp(SocketHandle listener,
                                         std::uint32_t timeoutMilliseconds,
                                         std::string& error);
    [[nodiscard]] SocketHandle ConnectTcp(std::string_view host,
                                          std::uint16_t port,
                                          std::uint32_t timeoutMilliseconds,
                                          std::string& error);
    void ShutdownSocket(SocketHandle socket) noexcept;
    void CloseSocket(SocketHandle socket) noexcept;
    [[nodiscard]] bool ReceiveExact(SocketHandle socket,
                                    std::span<std::uint8_t> bytes,
                                    std::uint32_t timeoutMilliseconds,
                                    std::string& error);
    [[nodiscard]] bool SendAll(SocketHandle socket,
                               std::span<const std::uint8_t> bytes,
                               std::uint32_t timeoutMilliseconds,
                               std::string& error);
    [[nodiscard]] bool ReceiveUntil(SocketHandle socket,
                                    std::vector<std::uint8_t>& bytes,
                                    std::string_view delimiter,
                                    std::size_t maximumBytes,
                                    std::uint32_t timeoutMilliseconds,
                                    std::string& error);
    [[nodiscard]] bool ReceivePacket(SocketHandle socket,
                                     Packet& packet,
                                     std::uint32_t timeoutMilliseconds,
                                     std::string& error);
    [[nodiscard]] bool SendPacket(SocketHandle socket,
                                  const Packet& packet,
                                  std::uint32_t timeoutMilliseconds,
                                  std::string& error);
}

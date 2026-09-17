// SPDX-License-Identifier: MS-PL

#include "InternalSocket.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <limits>
#include <mutex>

#if defined(_WIN32)
#include <ws2tcpip.h>
#include <bcrypt.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace CNA::Inspector::Detail
{
    namespace
    {
        constexpr std::size_t HeaderBytes = 24;

        std::string LastSocketError(std::string_view operation)
        {
#if defined(_WIN32)
            return std::string(operation) + " failed with Winsock error "
                + std::to_string(WSAGetLastError());
#else
            return std::string(operation) + " failed: " + std::strerror(errno);
#endif
        }

        bool WouldBlockOrInterrupted()
        {
#if defined(_WIN32)
            const auto error = WSAGetLastError();
            return error == WSAEWOULDBLOCK || error == WSAEINTR
                || error == WSAEINPROGRESS || error == WSAEALREADY;
#else
            return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR
                || errno == EINPROGRESS || errno == EALREADY;
#endif
        }

        bool Wait(SocketHandle socket,
                  bool writable,
                  std::uint32_t timeoutMilliseconds,
                  std::string& error)
        {
            fd_set descriptors;
            FD_ZERO(&descriptors);
            FD_SET(socket, &descriptors);
            timeval timeout{};
            timeout.tv_sec = static_cast<long>(timeoutMilliseconds / 1000U);
            timeout.tv_usec = static_cast<long>((timeoutMilliseconds % 1000U) * 1000U);
            const auto result = select(static_cast<int>(socket + 1),
                                       writable ? nullptr : &descriptors,
                                       writable ? &descriptors : nullptr,
                                       nullptr,
                                       &timeout);
            if (result > 0)
            {
                return true;
            }
            if (result == 0)
            {
                error = "socket operation timed out";
                return false;
            }
            if (WouldBlockOrInterrupted())
            {
                error = "socket operation interrupted";
            }
            else
            {
                error = LastSocketError("select");
            }
            return false;
        }

        bool SetNonBlocking(SocketHandle socket, bool enabled, std::string& error)
        {
#if defined(_WIN32)
            u_long mode = enabled ? 1UL : 0UL;
            if (ioctlsocket(socket, FIONBIO, &mode) != 0)
            {
                error = LastSocketError("ioctlsocket");
                return false;
            }
#else
            const auto flags = fcntl(socket, F_GETFL, 0);
            if (flags < 0 || fcntl(socket, F_SETFL,
                                   enabled ? flags | O_NONBLOCK : flags & ~O_NONBLOCK) < 0)
            {
                error = LastSocketError("fcntl");
                return false;
            }
#endif
            return true;
        }

        void ConfigureInteractiveTcp(SocketHandle socket)
        {
            int enabled = 1;
            (void) setsockopt(socket, IPPROTO_TCP, TCP_NODELAY,
                              reinterpret_cast<const char*>(&enabled), sizeof(enabled));
#if defined(SO_NOSIGPIPE)
            (void) setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
        }
    }

    bool InitializeSockets(std::string& error)
    {
#if defined(_WIN32)
        static std::once_flag once;
        static int startupResult = WSASYSNOTREADY;
        std::call_once(once, [] {
            WSADATA data{};
            startupResult = WSAStartup(MAKEWORD(2, 2), &data);
        });
        if (startupResult != 0)
        {
            error = "WSAStartup failed with error " + std::to_string(startupResult);
            return false;
        }
#else
        (void) error;
#endif
        return true;
    }

    bool GenerateSecureRandom(std::span<std::uint8_t> bytes, std::string& error)
    {
#if defined(_WIN32)
        if (bytes.size() > std::numeric_limits<ULONG>::max()
            || BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                               BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        {
            error = "Windows system random-number generation failed";
            return false;
        }
        return true;
#else
        const auto descriptor = open("/dev/urandom", O_RDONLY);
        if (descriptor < 0)
        {
            error = LastSocketError("open /dev/urandom");
            return false;
        }
        std::size_t offset = 0;
        while (offset < bytes.size())
        {
            const auto received = read(descriptor, bytes.data() + offset, bytes.size() - offset);
            if (received < 0 && errno == EINTR) continue;
            if (received <= 0)
            {
                error = received == 0 ? "system random source closed unexpectedly"
                                      : LastSocketError("read /dev/urandom");
                CloseSocket(descriptor);
                return false;
            }
            offset += static_cast<std::size_t>(received);
        }
        CloseSocket(descriptor);
        return true;
#endif
    }

    bool IsLoopbackAddress(std::string_view address)
    {
        return address == "127.0.0.1" || address == "::1" || address == "localhost";
    }

    SocketHandle CreateTcpListener(std::string_view address,
                                   std::uint16_t requestedPort,
                                   std::uint16_t& boundPort,
                                   std::string& error)
    {
        if (!InitializeSockets(error))
        {
            return InvalidSocket;
        }
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = AI_NUMERICSERV;
        addrinfo* addresses = nullptr;
        const auto portText = std::to_string(requestedPort);
        const std::string addressText(address);
        if (getaddrinfo(addressText.c_str(), portText.c_str(), &hints, &addresses) != 0)
        {
            error = "could not resolve Inspector bind address";
            return InvalidSocket;
        }

        SocketHandle listener = InvalidSocket;
        for (auto* candidate = addresses; candidate != nullptr; candidate = candidate->ai_next)
        {
            listener = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
            if (listener == InvalidSocket)
            {
                continue;
            }
            int enabled = 1;
            (void) setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
                              reinterpret_cast<const char*>(&enabled), sizeof(enabled));
#if defined(SO_NOSIGPIPE)
            (void) setsockopt(listener, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
            if (bind(listener, candidate->ai_addr, static_cast<int>(candidate->ai_addrlen)) == 0
                && listen(listener, 4) == 0)
            {
                break;
            }
            CloseSocket(listener);
            listener = InvalidSocket;
        }
        freeaddrinfo(addresses);
        if (listener == InvalidSocket)
        {
            error = LastSocketError("bind/listen");
            return InvalidSocket;
        }

        sockaddr_storage local{};
#if defined(_WIN32)
        int localLength = sizeof(local);
#else
        socklen_t localLength = sizeof(local);
#endif
        if (getsockname(listener, reinterpret_cast<sockaddr*>(&local), &localLength) != 0)
        {
            error = LastSocketError("getsockname");
            CloseSocket(listener);
            return InvalidSocket;
        }
        if (local.ss_family == AF_INET)
        {
            boundPort = ntohs(reinterpret_cast<const sockaddr_in*>(&local)->sin_port);
        }
        else
        {
            boundPort = ntohs(reinterpret_cast<const sockaddr_in6*>(&local)->sin6_port);
        }
        return listener;
    }

    SocketHandle AcceptTcp(SocketHandle listener,
                           std::uint32_t timeoutMilliseconds,
                           std::string& error)
    {
        if (!Wait(listener, false, timeoutMilliseconds, error))
        {
            return InvalidSocket;
        }
        const auto accepted = accept(listener, nullptr, nullptr);
        if (accepted == InvalidSocket)
        {
            error = LastSocketError("accept");
            return InvalidSocket;
        }
        ConfigureInteractiveTcp(accepted);
        return accepted;
    }

    SocketHandle ConnectTcp(std::string_view host,
                            std::uint16_t port,
                            std::uint32_t timeoutMilliseconds,
                            std::string& error)
    {
        if (!InitializeSockets(error))
        {
            return InvalidSocket;
        }
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = AI_NUMERICSERV;
        addrinfo* addresses = nullptr;
        const auto portText = std::to_string(port);
        const std::string hostText(host);
        if (getaddrinfo(hostText.c_str(), portText.c_str(), &hints, &addresses) != 0)
        {
            error = "could not resolve Inspector agent address";
            return InvalidSocket;
        }

        SocketHandle connected = InvalidSocket;
        for (auto* candidate = addresses; candidate != nullptr; candidate = candidate->ai_next)
        {
            connected = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
            if (connected == InvalidSocket)
            {
                continue;
            }
            if (!SetNonBlocking(connected, true, error))
            {
                CloseSocket(connected);
                connected = InvalidSocket;
                continue;
            }
            const auto result = connect(connected, candidate->ai_addr,
                                        static_cast<int>(candidate->ai_addrlen));
            if (result != 0 && !WouldBlockOrInterrupted())
            {
                CloseSocket(connected);
                connected = InvalidSocket;
                continue;
            }
            if (result != 0 && !Wait(connected, true, timeoutMilliseconds, error))
            {
                CloseSocket(connected);
                connected = InvalidSocket;
                continue;
            }
            int socketError = 0;
#if defined(_WIN32)
            int socketErrorSize = sizeof(socketError);
#else
            socklen_t socketErrorSize = sizeof(socketError);
#endif
            if (getsockopt(connected, SOL_SOCKET, SO_ERROR,
                           reinterpret_cast<char*>(&socketError), &socketErrorSize) != 0
                || socketError != 0)
            {
                CloseSocket(connected);
                connected = InvalidSocket;
                continue;
            }
            if (!SetNonBlocking(connected, false, error))
            {
                CloseSocket(connected);
                connected = InvalidSocket;
                continue;
            }
            ConfigureInteractiveTcp(connected);
            break;
        }
        freeaddrinfo(addresses);
        if (connected == InvalidSocket && error.empty())
        {
            error = "could not connect to the Inspector agent";
        }
        return connected;
    }

    void ShutdownSocket(SocketHandle socket) noexcept
    {
        if (socket == InvalidSocket)
        {
            return;
        }
#if defined(_WIN32)
        (void) shutdown(socket, SD_BOTH);
#else
        (void) shutdown(socket, SHUT_RDWR);
#endif
    }

    void CloseSocket(SocketHandle socket) noexcept
    {
        if (socket == InvalidSocket)
        {
            return;
        }
#if defined(_WIN32)
        (void) closesocket(socket);
#else
        (void) close(socket);
#endif
    }

    bool ReceiveExact(SocketHandle socket,
                      std::span<std::uint8_t> bytes,
                      std::uint32_t timeoutMilliseconds,
                      std::string& error)
    {
        std::size_t offset = 0;
        const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(timeoutMilliseconds);
        while (offset < bytes.size())
        {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline)
            {
                error = "socket receive timed out";
                return false;
            }
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
            if (!Wait(socket, false, static_cast<std::uint32_t>(remaining.count() + 1), error))
            {
                return false;
            }
#if defined(_WIN32)
            const auto received = recv(socket, reinterpret_cast<char*>(bytes.data() + offset),
                                       static_cast<int>(bytes.size() - offset), 0);
#else
            const auto received = recv(socket, bytes.data() + offset, bytes.size() - offset, 0);
#endif
            if (received == 0)
            {
                error = "Inspector peer closed the connection";
                return false;
            }
            if (received < 0)
            {
                if (WouldBlockOrInterrupted()) continue;
                error = LastSocketError("recv");
                return false;
            }
            offset += static_cast<std::size_t>(received);
        }
        return true;
    }

    bool SendAll(SocketHandle socket,
                 std::span<const std::uint8_t> bytes,
                 std::uint32_t timeoutMilliseconds,
                 std::string& error)
    {
        std::size_t offset = 0;
        const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(timeoutMilliseconds);
        while (offset < bytes.size())
        {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline)
            {
                error = "socket send timed out";
                return false;
            }
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
            if (!Wait(socket, true, static_cast<std::uint32_t>(remaining.count() + 1), error))
            {
                return false;
            }
#if defined(_WIN32)
            const auto sent = send(socket, reinterpret_cast<const char*>(bytes.data() + offset),
                                   static_cast<int>(bytes.size() - offset), 0);
#else
#if defined(MSG_NOSIGNAL)
            const auto sent = send(socket, bytes.data() + offset, bytes.size() - offset, MSG_NOSIGNAL);
#else
            const auto sent = send(socket, bytes.data() + offset, bytes.size() - offset, 0);
#endif
#endif
            if (sent <= 0)
            {
                if (sent < 0 && WouldBlockOrInterrupted()) continue;
                error = LastSocketError("send");
                return false;
            }
            offset += static_cast<std::size_t>(sent);
        }
        return true;
    }

    bool ReceiveUntil(SocketHandle socket,
                      std::vector<std::uint8_t>& bytes,
                      std::string_view delimiter,
                      std::size_t maximumBytes,
                      std::uint32_t timeoutMilliseconds,
                      std::string& error)
    {
        bytes.clear();
        if (delimiter.empty() || maximumBytes == 0)
        {
            error = "invalid bounded receive configuration";
            return false;
        }
        const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(timeoutMilliseconds);
        std::array<std::uint8_t, 1024> buffer{};
        while (bytes.size() < maximumBytes)
        {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline)
            {
                error = "socket receive timed out";
                return false;
            }
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
            if (!Wait(socket, false, static_cast<std::uint32_t>(remaining.count() + 1), error))
            {
                return false;
            }
            const auto available = std::min(buffer.size(), maximumBytes - bytes.size());
#if defined(_WIN32)
            const auto received = recv(socket, reinterpret_cast<char*>(buffer.data()),
                                       static_cast<int>(available), 0);
#else
            const auto received = recv(socket, buffer.data(), available, 0);
#endif
            if (received <= 0)
            {
                if (received < 0 && WouldBlockOrInterrupted()) continue;
                error = received == 0 ? "Inspector peer closed the connection"
                                      : LastSocketError("recv");
                return false;
            }
            bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + received);
            if (bytes.size() >= delimiter.size())
            {
                const auto overlap = std::min(
                    bytes.size(), static_cast<std::size_t>(received) + delimiter.size());
                const auto start = bytes.end() - static_cast<std::ptrdiff_t>(overlap);
                if (std::search(start, bytes.end(), delimiter.begin(), delimiter.end()) != bytes.end())
                {
                    return true;
                }
            }
        }
        error = "bounded HTTP request header limit exceeded";
        return false;
    }

    bool ReceivePacket(SocketHandle socket,
                       Packet& packet,
                       std::uint32_t timeoutMilliseconds,
                       std::string& error)
    {
        std::array<std::uint8_t, HeaderBytes> headerBytes{};
        if (!ReceiveExact(socket, headerBytes, timeoutMilliseconds, error)
            || !ProtocolCodec::DecodeHeader(headerBytes, packet.header, error))
        {
            return false;
        }
        packet.payload.resize(packet.header.payloadBytes);
        return packet.payload.empty()
            || ReceiveExact(socket, packet.payload, timeoutMilliseconds, error);
    }

    bool SendPacket(SocketHandle socket,
                    const Packet& packet,
                    std::uint32_t timeoutMilliseconds,
                    std::string& error)
    {
        if (packet.payload.size() != packet.header.payloadBytes
            || packet.payload.size() > MaximumPayloadBytes)
        {
            error = "attempted to send an invalid Inspector packet";
            return false;
        }
        const auto header = ProtocolCodec::EncodeHeader(packet.header);
        return SendAll(socket, header, timeoutMilliseconds, error)
            && (packet.payload.empty() || SendAll(socket, packet.payload, timeoutMilliseconds, error));
    }
}

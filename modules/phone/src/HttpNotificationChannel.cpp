// SPDX-License-Identifier: MS-PL
#include "Microsoft/Phone/Notification/HttpNotificationChannel.hpp"

#include <algorithm>
#include <bit>
#include <cerrno>
#include <cstring>
#include <map>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#    include <winsock2.h>
#    include <ws2tcpip.h>
using socket_t = SOCKET;
#    define CNA_CLOSE_SOCKET closesocket
#else
#    include <arpa/inet.h>
#    include <netinet/in.h>
#    include <sys/socket.h>
#    include <unistd.h>
using socket_t = int;
#    define CNA_CLOSE_SOCKET ::close
#endif

// The same pitfall CNA/Internal/Net/ENetHostHandle.hpp records, reached by a different route:
// <winsock2.h> pulls in <windows.h>, which defines the macro ERROR (wingdi.h), which collides with
// CNA::LogLevel::ERROR and CNA::LogCategory::ERROR in the CNA/Logger.hpp below -- so this file did
// not compile on any Windows toolchain. Undefined locally, the standard idiom; non-Windows builds
// are unaffected because the macro is never defined there.
// Win32 native validation WINNATIVE-0009.
#ifdef ERROR
#undef ERROR
#endif

#include "CNA/Logger.hpp"

namespace Microsoft::Phone::Notification {

namespace {

constexpr socket_t InvalidNativeSocket = static_cast<socket_t>(-1);

SharpRuntime::intcs SocketError()
{
#if defined(_WIN32)
    return WSAGetLastError();
#else
    return errno;
#endif
}

void ShutdownSocket(socket_t socket)
{
#if defined(_WIN32)
    ::shutdown(socket, SD_BOTH);
#else
    ::shutdown(socket, SHUT_RDWR);
#endif
}

// Every open channel, so Find() can answer. A channel removes itself when it closes.
std::mutex& RegistryMutex()
{
    static std::mutex value;
    return value;
}

std::map<std::string, HttpNotificationChannel*>& Registry()
{
    static std::map<std::string, HttpNotificationChannel*> value;
    return value;
}

// Reads one HTTP request off a connected socket and returns its body.
//
// Deliberately small: the only client is a service posting a notification, so this understands
// a request with a Content-Length and nothing else. Chunked encoding, continuation lines and
// pipelining are not accepted rather than half-supported -- a push that this cannot read is
// better refused visibly than delivered truncated.
bool ReadRequestBody(socket_t connection, std::vector<std::uint8_t>& body,
                     [[maybe_unused]] std::uintptr_t stopEvent)
{
    std::string buffer;
    char chunk[4096];
#if defined(_WIN32)
    // A blocking Winsock receive is not reliably cancelled by shutdown either.
    // Keep the socket owned and wait for data or the explicit cancellation event.
    const auto releaseEvent = [connection](void* event) {
        WSAEventSelect(connection, nullptr, 0);
        WSACloseEvent(event);
    };
    std::unique_ptr<void, decltype(releaseEvent)> readable(WSACreateEvent(), releaseEvent);
    if (!readable || WSAEventSelect(connection, readable.get(), FD_READ | FD_CLOSE) == SOCKET_ERROR)
        return false;
#endif
    const auto receive = [&]() {
#if defined(_WIN32)
        const WSAEVENT events[]{reinterpret_cast<WSAEVENT>(stopEvent), readable.get()};
        while (true) {
            if (WSAWaitForMultipleEvents(2, events, FALSE, WSA_INFINITE, FALSE) != WSA_WAIT_EVENT_0 + 1)
                return -1;
            WSANETWORKEVENTS occurred{};
            if (WSAEnumNetworkEvents(connection, events[1], &occurred) == SOCKET_ERROR)
                return -1;
            const int count = ::recv(connection, chunk, sizeof(chunk), 0);
            if (count != SOCKET_ERROR || WSAGetLastError() != WSAEWOULDBLOCK)
                return count;
            if (occurred.lNetworkEvents & FD_CLOSE) return 0;
        }
#else
        return ::recv(connection, chunk, sizeof(chunk), 0);
#endif
    };

    // Headers first.
    std::size_t headerEnd = std::string::npos;
    while (headerEnd == std::string::npos) {
        const auto read = receive();
        if (read <= 0) {
            return false;
        }
        buffer.append(chunk, static_cast<std::size_t>(read));
        headerEnd = buffer.find("\r\n\r\n");
        if (buffer.size() > (1u << 20)) {
            return false;
        }
    }

    std::size_t contentLength = 0;
    {
        const std::string headers = buffer.substr(0, headerEnd);
        std::string lowered = headers;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const std::size_t at = lowered.find("content-length:");
        if (at != std::string::npos) {
            contentLength = static_cast<std::size_t>(
                std::strtoul(headers.c_str() + at + std::strlen("content-length:"), nullptr, 10));
        }
    }

    std::string content = buffer.substr(headerEnd + 4);
    while (content.size() < contentLength) {
        const auto read = receive();
        if (read <= 0) {
            return false;
        }
        content.append(chunk, static_cast<std::size_t>(read));
    }

    body.assign(content.begin(), content.begin() + static_cast<std::ptrdiff_t>(contentLength));
    return true;
}

} // namespace

HttpNotificationChannel* HttpNotificationChannel::Find(const std::string& channelName)
{
    const std::lock_guard<std::mutex> lock(RegistryMutex());
    const auto found = Registry().find(channelName);
    return found == Registry().end() ? nullptr : found->second;
}

HttpNotificationChannel::HttpNotificationChannel(std::string channelName, std::string serviceName)
    : channelName_(std::move(channelName)), serviceName_(std::move(serviceName))
{
}

HttpNotificationChannel::~HttpNotificationChannel()
{
    Close();
}

void HttpNotificationChannel::QueueOpenError(SharpRuntime::intcs code, const char* operation)
{
#if defined(_WIN32)
    const auto hresult = std::bit_cast<std::int32_t>(0x80070000u | (static_cast<std::uint32_t>(code) & 0xffffu));
#else
    // The public property is an HRESULT, not errno. POSIX has no Windows error
    // mapping here: report E_FAIL and retain the native code in additional data.
    constexpr std::int32_t hresult = -2147467259;
#endif
    NotificationChannelErrorEventArgs error(ChannelErrorType::ChannelOpenFailed, hresult, code,
        std::string("HttpNotificationChannel: ") + operation + ": " + std::system_category().message(code));
    const std::lock_guard<std::mutex> lock(mutex_);
    pendingErrors_.push_back(std::move(error));
}

void HttpNotificationChannel::Open()
{
    if (listening_) {
        return;
    }

    const socket_t listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener == InvalidNativeSocket) {
        QueueOpenError(SocketError(), "socket creation failed");
        CNA::Logger::Warn("HttpNotificationChannel: could not create a listening socket.");
        return;
    }

    int reuse = 1;
    (void)::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse),
                       sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    // Port zero: the operating system picks a free one, which is the point -- the channel's
    // address is discovered and handed out, never configured, exactly as a push URI was.
    address.sin_port = 0;

    if (::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(listener, 4) != 0) {
        const auto error = SocketError();
        CNA_CLOSE_SOCKET(listener);
        QueueOpenError(error, "listener setup failed");
        CNA::Logger::Warn("HttpNotificationChannel: could not listen for notifications.");
        return;
    }

    sockaddr_in bound{};
    socklen_t boundSize = sizeof(bound);
    if (::getsockname(listener, reinterpret_cast<sockaddr*>(&bound), &boundSize) != 0) {
        const auto error = SocketError();
        CNA_CLOSE_SOCKET(listener);
        QueueOpenError(error, "listener address lookup failed");
        return;
    }

#if defined(_WIN32)
    const WSAEVENT acceptEvent = WSACreateEvent();
    // Preserve the first failing operation's error instead of making another
    // native call that may overwrite it.
    const WSAEVENT stopEvent = acceptEvent == WSA_INVALID_EVENT ? WSA_INVALID_EVENT : WSACreateEvent();
    if (acceptEvent == WSA_INVALID_EVENT || stopEvent == WSA_INVALID_EVENT ||
        WSAEventSelect(listener, acceptEvent, FD_ACCEPT) == SOCKET_ERROR) {
        const auto error = SocketError();
        if (acceptEvent != WSA_INVALID_EVENT) WSACloseEvent(acceptEvent);
        if (stopEvent != WSA_INVALID_EVENT) WSACloseEvent(stopEvent);
        CNA_CLOSE_SOCKET(listener);
        QueueOpenError(error, "listener event setup failed");
        CNA::Logger::Warn("HttpNotificationChannel: could not create listener events.");
        return;
    }
    acceptEvent_ = reinterpret_cast<std::uintptr_t>(acceptEvent);
    stopEvent_ = reinterpret_cast<std::uintptr_t>(stopEvent);
#endif
    listenSocket_ = static_cast<std::uintptr_t>(listener);
    channelUri_.emplace("http://127.0.0.1:" + std::to_string(ntohs(bound.sin_port)) + "/" +
                        channelName_ + "/");
    listening_ = true;

    {
        const std::lock_guard<std::mutex> lock(RegistryMutex());
        Registry()[channelName_] = this;
    }

    try {
        listener_ = std::thread([this, listener] { Listen(static_cast<std::uintptr_t>(listener)); });
    } catch (...) {
        Close();
        throw;
    }

    NotificationChannelUriEventArgs args(*channelUri_);
    ChannelUriUpdated.Raise(nullptr, args);
}

void HttpNotificationChannel::Listen(std::uintptr_t listener)
{
    while (listening_) {
#if defined(_WIN32)
        const WSAEVENT events[]{reinterpret_cast<WSAEVENT>(stopEvent_),
                                reinterpret_cast<WSAEVENT>(acceptEvent_)};
        if (WSAWaitForMultipleEvents(2, events, FALSE, WSA_INFINITE, FALSE) != WSA_WAIT_EVENT_0 + 1 ||
            !listening_) break;
        WSANETWORKEVENTS occurred{};
        if (WSAEnumNetworkEvents(static_cast<socket_t>(listener), events[1], &occurred) == SOCKET_ERROR)
            break;
        if (!(occurred.lNetworkEvents & FD_ACCEPT)) continue;
#endif
        const socket_t connection = ::accept(static_cast<socket_t>(listener), nullptr, nullptr);
        if (connection == InvalidNativeSocket) {
            if (!listening_) {
                break;
            }
            continue;
        }

        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (!listening_) {
                CNA_CLOSE_SOCKET(connection);
                break;
            }
            activeConnection_ = static_cast<std::uintptr_t>(connection);
        }
#if defined(SO_NOSIGPIPE)
        int noSigPipe = 1;
        (void)::setsockopt(connection, SOL_SOCKET, SO_NOSIGPIPE,
                          reinterpret_cast<const char*>(&noSigPipe), sizeof(noSigPipe));
#endif

        std::vector<std::uint8_t> body;
#if defined(_WIN32)
        const bool read = ReadRequestBody(connection, body, stopEvent_);
#else
        const bool read = ReadRequestBody(connection, body, 0);
#endif

        // The sender is told the notification was received before it is handled, because
        // handling happens on another thread entirely -- holding the connection open until the
        // game's next frame would make every push wait on the frame rate.
        const std::string response = read ? "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n"
                                          : "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
        if (listening_) {
#if defined(MSG_NOSIGNAL)
            constexpr int sendFlags = MSG_NOSIGNAL;
#else
            constexpr int sendFlags = 0;
#endif
            (void)::send(connection, response.c_str(), response.size(), sendFlags);
        }
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            CNA_CLOSE_SOCKET(connection);
            activeConnection_ = InvalidSocket;
            if (read && !body.empty()) pending_.push_back(std::move(body));
        }
    }
}

void HttpNotificationChannel::Close()
{
    if (!listening_.exchange(false)) {
        return;
    }

    // Winsock rejects shutdown on a listening socket with WSAENOTCONN. Wake its
    // event wait explicitly; POSIX shutdown wakes the blocking accept instead.
#if defined(_WIN32)
    WSASetEvent(reinterpret_cast<WSAEVENT>(stopEvent_));
#else
    if (listenSocket_ != InvalidSocket) {
        ShutdownSocket(static_cast<socket_t>(listenSocket_));
    }
#endif
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (activeConnection_ != InvalidSocket)
            ShutdownSocket(static_cast<socket_t>(activeConnection_));
    }

    if (listener_.joinable()) {
        listener_.join();
    }

    // Keep the listener descriptor owned until its thread exits, so it cannot be reused
    // for an unrelated socket between the worker's captured handle and accept().
    if (listenSocket_ != InvalidSocket) {
        CNA_CLOSE_SOCKET(static_cast<socket_t>(listenSocket_));
        listenSocket_ = InvalidSocket;
    }
#if defined(_WIN32)
    WSACloseEvent(reinterpret_cast<WSAEVENT>(acceptEvent_));
    WSACloseEvent(reinterpret_cast<WSAEVENT>(stopEvent_));
    acceptEvent_ = stopEvent_ = 0;
#endif

    {
        const std::lock_guard<std::mutex> lock(RegistryMutex());
        const auto found = Registry().find(channelName_);
        if (found != Registry().end() && found->second == this) {
            Registry().erase(found);
        }
    }

    channelUri_.reset();
}

void HttpNotificationChannel::BindToShellToast()
{
    shellToastBound_ = true;
}

void HttpNotificationChannel::DispatchPendingNotificationsEXT()
{
    std::vector<std::vector<std::uint8_t>> ready;
    std::vector<NotificationChannelErrorEventArgs> errors;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        ready.swap(pending_);
        errors.swap(pendingErrors_);
    }

    for (const auto& error : errors) {
        ErrorOccurred.Raise(nullptr, error);
    }

    for (auto& body : ready) {
        HttpNotificationEventArgs args(std::move(body));
        HttpNotificationReceived.Raise(nullptr, args);
    }
}

} // namespace Microsoft::Phone::Notification

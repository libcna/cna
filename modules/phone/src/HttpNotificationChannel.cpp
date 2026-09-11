// SPDX-License-Identifier: MS-PL
#include "Microsoft/Phone/Notification/HttpNotificationChannel.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <map>
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

#include "CNA/Logger.hpp"

namespace Microsoft::Phone::Notification {

namespace {

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
bool ReadRequestBody(socket_t connection, std::vector<std::uint8_t>& body)
{
    std::string buffer;
    char chunk[4096];

    // Headers first.
    std::size_t headerEnd = std::string::npos;
    while (headerEnd == std::string::npos) {
        const auto read = ::recv(connection, chunk, sizeof(chunk), 0);
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
        const auto read = ::recv(connection, chunk, sizeof(chunk), 0);
        if (read <= 0) {
            break;
        }
        content.append(chunk, static_cast<std::size_t>(read));
    }

    body.assign(content.begin(), content.begin() + static_cast<std::ptrdiff_t>(
                                                       std::min(content.size(), contentLength)));
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

void HttpNotificationChannel::Open()
{
    if (listening_) {
        return;
    }

    const socket_t listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
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
        CNA_CLOSE_SOCKET(listener);
        CNA::Logger::Warn("HttpNotificationChannel: could not listen for notifications.");
        return;
    }

    sockaddr_in bound{};
    socklen_t boundSize = sizeof(bound);
    if (::getsockname(listener, reinterpret_cast<sockaddr*>(&bound), &boundSize) != 0) {
        CNA_CLOSE_SOCKET(listener);
        return;
    }

    listenSocket_ = static_cast<int>(listener);
    channelUri_.emplace("http://127.0.0.1:" + std::to_string(ntohs(bound.sin_port)) + "/" +
                        channelName_ + "/");
    listening_ = true;

    {
        const std::lock_guard<std::mutex> lock(RegistryMutex());
        Registry()[channelName_] = this;
    }

    listener_ = std::thread([this] { Listen(); });

    NotificationChannelUriEventArgs args(*channelUri_);
    ChannelUriUpdated.Raise(nullptr, args);
}

void HttpNotificationChannel::Listen()
{
    while (listening_) {
        const socket_t connection = ::accept(static_cast<socket_t>(listenSocket_), nullptr, nullptr);
        if (connection < 0) {
            if (!listening_) {
                break;
            }
            continue;
        }

        std::vector<std::uint8_t> body;
        const bool read = ReadRequestBody(connection, body);

        // The sender is told the notification was received before it is handled, because
        // handling happens on another thread entirely -- holding the connection open until the
        // game's next frame would make every push wait on the frame rate.
        const std::string response = read ? "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n"
                                          : "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
        (void)::send(connection, response.c_str(), response.size(), 0);
        CNA_CLOSE_SOCKET(connection);

        if (read && !body.empty()) {
            const std::lock_guard<std::mutex> lock(mutex_);
            pending_.push_back(std::move(body));
        }
    }
}

void HttpNotificationChannel::Close()
{
    if (!listening_) {
        return;
    }

    listening_ = false;

    // Shutting the socket down is what wakes the blocked accept(); closing it alone can leave
    // the thread parked until a connection happens to arrive.
    if (listenSocket_ >= 0) {
#if defined(_WIN32)
        ::shutdown(static_cast<socket_t>(listenSocket_), SD_BOTH);
#else
        ::shutdown(static_cast<socket_t>(listenSocket_), SHUT_RDWR);
#endif
        CNA_CLOSE_SOCKET(static_cast<socket_t>(listenSocket_));
        listenSocket_ = -1;
    }

    if (listener_.joinable()) {
        listener_.join();
    }

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
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        ready.swap(pending_);
    }

    for (auto& body : ready) {
        HttpNotificationEventArgs args(std::move(body));
        HttpNotificationReceived.Raise(nullptr, args);
    }
}

} // namespace Microsoft::Phone::Notification

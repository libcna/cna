// SPDX-License-Identifier: MS-PL
#include "Microsoft/Phone/Notification/PushNotificationSender.hpp"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstring>
#include <string>

#if defined(_WIN32)
#    include <winsock2.h>
#    include <ws2tcpip.h>
using socket_t = SOCKET;
#    define CNA_CLOSE_SOCKET closesocket
#else
#    include <arpa/inet.h>
#    include <netdb.h>
#    include <netinet/in.h>
#    include <sys/socket.h>
#    include <unistd.h>
using socket_t = int;
#    define CNA_CLOSE_SOCKET ::close
#endif

namespace Microsoft::Phone::Notification {

namespace {

bool Interrupted()
{
#if defined(_WIN32)
    return WSAGetLastError() == WSAEINTR;
#else
    return errno == EINTR;
#endif
}

bool ReceiverAccepted(socket_t connection)
{
    // Queueing bytes locally is not an acknowledgement, particularly for browser
    // socket shims. Require complete HTTP headers and a final successful status.
    std::string response;
    std::size_t received = 0;
    constexpr std::size_t MaximumResponseHeaders = 64 * 1024;
    while (true) {
        const auto headerEnd = response.find("\r\n\r\n");
        if (headerEnd != std::string::npos) {
            const auto lineEnd = response.find("\r\n");
            if (lineEnd < 12 ||
                (!response.starts_with("HTTP/1.0 ") && !response.starts_with("HTTP/1.1 ")) ||
                response[9] < '1' || response[9] > '5' ||
                response[10] < '0' || response[10] > '9' ||
                response[11] < '0' || response[11] > '9' ||
                (response[12] != ' ' && response[12] != '\r')) {
                return false;
            }
            const int status = (response[9] - '0') * 100 +
                               (response[10] - '0') * 10 + response[11] - '0';
            if (status >= 200) return status < 300;
            // A protocol upgrade is not a notification acknowledgement. Other
            // informational responses precede the actual final response.
            if (status == 101) return false;
            response.erase(0, headerEnd + 4);
            continue;
        }
        if (received == MaximumResponseHeaders) return false;
        char buffer[512];
        const auto size = static_cast<int>(std::min(sizeof(buffer), MaximumResponseHeaders - received));
        const auto count = ::recv(connection, buffer, size, 0);
        if (count < 0 && Interrupted()) continue;
        if (count <= 0) return false;
        response.append(buffer, static_cast<std::size_t>(count));
        received += static_cast<std::size_t>(count);
    }
}

const char* PriorityHeader(MessageSendPriority priority, bool toast)
{
    // The class values Windows Phone defined: raw notifications are 3/13/23 by priority and
    // toasts 2/12/22, immediate first.
    switch (priority) {
    case MessageSendPriority::High: return toast ? "2" : "3";
    case MessageSendPriority::Normal: return toast ? "12" : "13";
    case MessageSendPriority::Low: return toast ? "22" : "23";
    }
    return toast ? "12" : "13";
}

// Posts a body to a channel URI. Small on purpose: the receiver is a channel this runtime
// opened, on a host this process can reach, and a push that cannot be delivered is not an
// error the sender can do anything about.
bool Post(const System::Uri& clientUri, const std::string& body, const std::string& contentType,
          const std::string& notificationClass, const std::string& target)
{
    const std::string uri = clientUri.ToString();
    const std::size_t hostAt = uri.find("//");
    if (hostAt == std::string::npos) {
        return false;
    }
    const std::size_t pathAt = uri.find('/', hostAt + 2);
    std::string hostPort = uri.substr(hostAt + 2, pathAt == std::string::npos
                                                      ? std::string::npos
                                                      : pathAt - hostAt - 2);
    const std::string path = pathAt == std::string::npos ? "/" : uri.substr(pathAt);

    std::string host = hostPort;
    int port = 80;
    if (const std::size_t portAt = hostPort.find(':'); portAt != std::string::npos) {
        host = hostPort.substr(0, portAt);
        port = std::atoi(hostPort.c_str() + portAt + 1);
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* resolved = nullptr;
    if (::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &resolved) != 0 ||
        resolved == nullptr) {
        return false;
    }

    const socket_t connection =
        ::socket(resolved->ai_family, resolved->ai_socktype, resolved->ai_protocol);
    if (connection == static_cast<socket_t>(-1)) {
        ::freeaddrinfo(resolved);
        return false;
    }

    const bool connected = ::connect(connection, resolved->ai_addr, resolved->ai_addrlen) == 0;
    ::freeaddrinfo(resolved);
    if (!connected) {
        CNA_CLOSE_SOCKET(connection);
        return false;
    }
    struct CloseConnection {
        socket_t socket;
        ~CloseConnection() { CNA_CLOSE_SOCKET(socket); }
    } closeConnection{connection};

#if defined(SO_NOSIGPIPE)
    const int noSigPipe = 1;
    (void)::setsockopt(connection, SOL_SOCKET, SO_NOSIGPIPE,
                      reinterpret_cast<const char*>(&noSigPipe), sizeof(noSigPipe));
#endif

    std::string request = "POST " + path + " HTTP/1.1\r\nHost: " + hostPort +
                          "\r\nContent-Type: " + contentType +
                          "\r\nX-NotificationClass: " + notificationClass;
    if (!target.empty()) {
        request += "\r\nX-WindowsPhone-Target: " + target;
    }
    request += "\r\nContent-Length: " + std::to_string(body.size()) +
               "\r\nConnection: close\r\n\r\n" + body;

    std::size_t offset = 0;
    while (offset < request.size()) {
        // Winsock takes an int length; POSIX also permits a short successful send.
        const auto count = static_cast<int>(std::min(request.size() - offset,
                                                     static_cast<std::size_t>(INT_MAX)));
#if defined(MSG_NOSIGNAL)
        constexpr int flags = MSG_NOSIGNAL;
#else
        constexpr int flags = 0;
#endif
        const auto sent = ::send(connection, request.data() + offset, count, flags);
        if (sent < 0 && Interrupted()) continue;
        if (sent <= 0) return false;
        offset += static_cast<std::size_t>(sent);
    }
    return ReceiverAccepted(connection);
}

} // namespace

bool RawPushNotificationMessage::SendAsync(const System::Uri& clientUri) const
{
    const std::string body(RawData.begin(), RawData.end());
    return Post(clientUri, body, "text/xml", PriorityHeader(priority_, false), "");
}

bool ToastPushNotificationMessage::SendAsync(const System::Uri& clientUri) const
{
    const std::string body =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<wp:Notification xmlns:wp=\"WPNotification\"><wp:Toast>"
        "<wp:Text1>" + Title + "</wp:Text1><wp:Text2>" + SubTitle + "</wp:Text2>"
        "</wp:Toast></wp:Notification>";
    return Post(clientUri, body, "text/xml", PriorityHeader(priority_, true), "toast");
}

} // namespace Microsoft::Phone::Notification

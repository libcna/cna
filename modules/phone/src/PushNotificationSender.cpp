// SPDX-License-Identifier: MS-PL
#include "Microsoft/Phone/Notification/PushNotificationSender.hpp"

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
    if (connection < 0) {
        ::freeaddrinfo(resolved);
        return false;
    }

    const bool connected = ::connect(connection, resolved->ai_addr, resolved->ai_addrlen) == 0;
    ::freeaddrinfo(resolved);
    if (!connected) {
        CNA_CLOSE_SOCKET(connection);
        return false;
    }

    std::string request = "POST " + path + " HTTP/1.1\r\nHost: " + hostPort +
                          "\r\nContent-Type: " + contentType +
                          "\r\nX-NotificationClass: " + notificationClass;
    if (!target.empty()) {
        request += "\r\nX-WindowsPhone-Target: " + target;
    }
    request += "\r\nContent-Length: " + std::to_string(body.size()) +
               "\r\nConnection: close\r\n\r\n" + body;

    const bool sent =
        ::send(connection, request.c_str(), request.size(), 0) ==
        static_cast<ssize_t>(request.size());

    char discard[512];
    (void)::recv(connection, discard, sizeof(discard), 0);
    CNA_CLOSE_SOCKET(connection);
    return sent;
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

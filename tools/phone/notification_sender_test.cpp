// SPDX-License-Identifier: MS-PL
#include "Microsoft/Phone/Notification/PushNotificationSender.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace
{
std::string response;
std::size_t responseOffset = 0;
std::size_t receiveChunk = 4096;
std::size_t sendChunk = 4096;
bool pending = false;
bool interruptReceive = false;
bool interruptSend = false;
bool failSend = false;
unsigned sends = 0;
}

extern "C" ssize_t __real_send(int, const void*, size_t, int);
extern "C" ssize_t __wrap_send(int socket, const void* data, size_t size, int flags)
{
    ++sends;
    if (failSend) return 0;
    if (interruptSend) {
        interruptSend = false;
        errno = EINTR;
        return -1;
    }
    return __real_send(socket, data, std::min(size, sendChunk), flags);
}

extern "C" ssize_t __wrap_recv(int, void* buffer, size_t size, int)
{
    if (interruptReceive) {
        interruptReceive = false;
        errno = EINTR;
        return -1;
    }
    if (pending) {
        errno = EAGAIN;
        return -1;
    }
    const auto count = std::min(std::min(size, receiveChunk), response.size() - responseOffset);
    std::memcpy(buffer, response.data() + responseOffset, count);
    responseOffset += count;
    return static_cast<ssize_t>(count);
}

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    const std::string mode = argv[1];
    bool expected = false;
    if (mode == "accepted") {
        response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
        expected = true;
    } else if (mode == "fragmented") {
        response = "HTTP/1.0 202 Accepted\r\nContent-Length: 0\r\n\r\n";
        receiveChunk = 1;
        expected = true;
    } else if (mode == "interim") {
        response = "HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 204 No Content\r\n\r\n";
        expected = true;
    } else if (mode == "refused") {
        response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
    } else if (mode == "redirect") {
        response = "HTTP/1.1 302 Found\r\nLocation: /other\r\n\r\n";
    } else if (mode == "closed") {
        response.clear();
    } else if (mode == "pending") {
        pending = true;
    } else if (mode == "truncated") {
        response = "HTTP/1.1 200 OK\r\nContent-Len";
    } else if (mode == "malformed") {
        response = "HTTP/1.1 2000 Not a status\r\n\r\n";
    } else if (mode == "short-status") {
        response = "oops\r\n\r\n";
    } else if (mode == "upgrade") {
        response = "HTTP/1.1 101 Switching Protocols\r\n\r\n";
    } else if (mode == "overlong") {
        response = "HTTP/1.1 200 OK\r\nX-Header: " + std::string(64 * 1024, 'x') + "\r\n\r\n";
    } else if (mode == "failed-send") {
        failSend = true;
        response = "HTTP/1.1 200 OK\r\n\r\n";
    } else if (mode == "short-send") {
        sendChunk = 7;
        response = "HTTP/1.1 200 OK\r\n\r\n";
        expected = true;
    } else if (mode == "interrupted") {
        interruptSend = interruptReceive = true;
        response = "HTTP/1.1 200 OK\r\n\r\n";
        expected = true;
    } else {
        return 3;
    }

    // A real local transport accepts the bytes into its backlog, but recv is
    // scripted: local enqueue must never be mistaken for receiver acceptance.
    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) return 4;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t length = sizeof(address);
    if (::bind(listener, reinterpret_cast<sockaddr*>(&address), length) ||
        ::listen(listener, 1) ||
        ::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length)) {
        ::close(listener);
        return 5;
    }
    Microsoft::Phone::Notification::RawPushNotificationMessage message;
    message.RawData = {'x'};
    const bool result = message.SendAsync(System::Uri(
        "http://127.0.0.1:" + std::to_string(ntohs(address.sin_port)) + "/notify"));
    ::close(listener);
    std::printf("%s: accepted=%d expected=%d sends=%u received=%zu\n",
                mode.c_str(), result, expected, sends, responseOffset);
    if (!sends || result != expected) return 1;
    if (expected && responseOffset != response.size()) return 1;
    if (mode == "short-send" && sends <= 1) return 1;
    if (mode == "failed-send" && responseOffset != 0) return 1;
    return 0;
}

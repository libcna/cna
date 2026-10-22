// SPDX-License-Identifier: MS-PL
#include "Microsoft/Phone/Notification/HttpNotificationChannel.hpp"

#include <cerrno>
#include <cstdio>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <type_traits>
#include <unistd.h>

using Microsoft::Phone::Notification::ChannelErrorType;
using Microsoft::Phone::Notification::NotificationChannelErrorEventArgs;
static_assert(static_cast<int>(ChannelErrorType::ChannelOpenFailed) == 0);
static_assert(static_cast<int>(ChannelErrorType::PayloadFormatError) == 1);
static_assert(static_cast<int>(ChannelErrorType::MessageBadContent) == 2);
static_assert(static_cast<int>(ChannelErrorType::NotificationRateTooHigh) == 3);
static_assert(static_cast<int>(ChannelErrorType::PowerLevelChanged) == 4);
static_assert(static_cast<int>(ChannelErrorType::Unknown) == 5);
static_assert(std::is_base_of_v<System::EventArgs, NotificationChannelErrorEventArgs>);
static_assert(!std::is_default_constructible_v<NotificationChannelErrorEventArgs>);

namespace {
std::string failure;
int owned = -1;
unsigned closed = 0;
}

extern "C" int __real_socket(int, int, int);
extern "C" int __real_bind(int, const sockaddr*, socklen_t);
extern "C" int __real_listen(int, int);
extern "C" int __real_getsockname(int, sockaddr*, socklen_t*);
extern "C" int __real_close(int);

extern "C" int __wrap_socket(int domain, int type, int protocol)
{
    if (failure == "socket") { errno = EACCES; return -1; }
    return owned = __real_socket(domain, type, protocol);
}
extern "C" int __wrap_bind(int socket, const sockaddr* address, socklen_t length)
{
    if (failure == "bind") { errno = EACCES; return -1; }
    return __real_bind(socket, address, length);
}
extern "C" int __wrap_listen(int socket, int backlog)
{
    if (failure == "listen") { errno = EACCES; return -1; }
    return __real_listen(socket, backlog);
}
extern "C" int __wrap_getsockname(int socket, sockaddr* address, socklen_t* length)
{
    if (failure == "name") { errno = EACCES; return -1; }
    return __real_getsockname(socket, address, length);
}
extern "C" int __wrap_close(int socket)
{
    if (socket == owned) { ++closed; owned = -1; }
    const int result = __real_close(socket);
    errno = EBADF; // Cleanup must not overwrite the reported operation's error.
    return result;
}

template<class Channel>
int Verify(Channel& channel)
{
    if constexpr (!requires { channel.ErrorOccurred; }) {
        channel.Open();
        std::puts("FAIL: failed Open has no ErrorOccurred event");
        return 1;
    } else {
        unsigned errors = 0, uris = 0;
        bool correct = true;
        std::thread::id dispatchThread;
        channel.ChannelUriUpdated += [&](System::Object*, const auto&) { ++uris; };
        channel.ErrorOccurred += [&](System::Object*, const auto& args) {
            ++errors;
            using ErrorType = decltype(args.getErrorTypeProperty());
            correct &= args.getErrorTypeProperty() == ErrorType::ChannelOpenFailed &&
                args.getErrorCodeProperty() == -2147467259 &&
                args.getErrorAdditionalDataProperty() == EACCES &&
                !args.getMessageProperty().empty() &&
                std::this_thread::get_id() == dispatchThread;
            channel.Close(); // No mutex may remain held while invoking the handler.
        };
        channel.Open();
        channel.Open();
        if (errors || uris || channel.getChannelUriProperty() || Channel::Find("open-failure")) return 2;
        if (owned != -1 || closed != (failure == "socket" ? 0u : 2u)) return 3;
        channel.Close(); // Queued errors survive Close just like queued notifications.
        std::thread dispatcher([&] {
            dispatchThread = std::this_thread::get_id();
            channel.DispatchPendingNotificationsEXT();
            channel.DispatchPendingNotificationsEXT();
        });
        dispatcher.join();
        if (errors != 2 || !correct) return 4;
        failure.clear();
        channel.Open();
        channel.Open();
        if (uris != 1 || !channel.getChannelUriProperty() || Channel::Find("open-failure") != &channel) return 5;
        channel.Close();
        channel.DispatchPendingNotificationsEXT();
        if (errors != 2 || owned != -1 || Channel::Find("open-failure")) return 6;
        std::puts("PASS: exact queued errors, cleanup, caller-thread dispatch and recovery");
        return 0;
    }
}

int main(int argc, char** argv)
{
    if (argc != 2) return 10;
    failure = argv[1];
    if (failure != "socket" && failure != "bind" && failure != "listen" && failure != "name") return 11;
    Microsoft::Phone::Notification::HttpNotificationChannel channel("open-failure", "regression");
    return Verify(channel);
}

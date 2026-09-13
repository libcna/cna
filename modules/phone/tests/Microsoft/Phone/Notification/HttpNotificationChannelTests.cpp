// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "Microsoft/Phone/Notification/HttpNotificationChannel.hpp"
#include "Microsoft/Phone/Notification/PushNotificationSender.hpp"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using Microsoft::Phone::Notification::HttpNotificationChannel;
using Microsoft::Phone::Notification::HttpNotificationEventArgs;
using Microsoft::Phone::Notification::NotificationChannelUriEventArgs;

namespace {

// Posts to the channel the way a service does: a plain HTTP POST to the URI it handed out.
// Written on a bare socket so the test needs nothing but the channel itself.
bool Push(const std::string& uri, const std::string& body)
{
    const std::size_t hostAt = uri.find("//");
    const std::size_t portAt = uri.find(':', hostAt + 2);
    const std::size_t pathAt = uri.find('/', portAt);
    if (hostAt == std::string::npos || portAt == std::string::npos || pathAt == std::string::npos) {
        return false;
    }
    const int port = std::stoi(uri.substr(portAt + 1, pathAt - portAt - 1));
    const std::string path = uri.substr(pathAt);

    const int connection = ::socket(AF_INET, SOCK_STREAM, 0);
    if (connection < 0) {
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(connection, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        ::close(connection);
        return false;
    }

    const std::string request = "POST " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\n" +
                                "Content-Length: " + std::to_string(body.size()) +
                                "\r\nContent-Type: text/xml\r\n\r\n" + body;
    const bool sent = ::send(connection, request.c_str(), request.size(), 0) ==
                      static_cast<ssize_t>(request.size());

    char discard[512];
    (void)::recv(connection, discard, sizeof(discard), 0);
    ::close(connection);
    return sent;
}

} // namespace

TEST(HttpNotificationChannelTest, AClosedChannelHasNoUri)
{
    HttpNotificationChannel channel("TestChannel", "TestService");
    EXPECT_EQ(channel.getChannelUriProperty(), nullptr);
}

TEST(HttpNotificationChannelTest, OpeningReportsAnAddressAServiceCanPushTo)
{
    HttpNotificationChannel channel("UriChannel", "TestService");

    std::string reported;
    channel.ChannelUriUpdated += [&reported](System::Object*,
                                             const NotificationChannelUriEventArgs& e) {
        reported = e.getChannelUriProperty().ToString();
    };

    channel.Open();

    ASSERT_NE(channel.getChannelUriProperty(), nullptr);
    EXPECT_EQ(reported, channel.getChannelUriProperty()->ToString());
    EXPECT_NE(reported.find("http://127.0.0.1:"), std::string::npos);
    EXPECT_NE(reported.find("/UriChannel/"), std::string::npos);

    channel.Close();
    EXPECT_EQ(channel.getChannelUriProperty(), nullptr);
}

TEST(HttpNotificationChannelTest, WhatAServicePostsArrivesAsANotification)
{
    HttpNotificationChannel channel("PushChannel", "TestService");
    channel.Open();
    ASSERT_NE(channel.getChannelUriProperty(), nullptr);

    std::vector<std::string> received;
    channel.HttpNotificationReceived += [&received](System::Object*,
                                                    const HttpNotificationEventArgs& e) {
        received.push_back(e.getBodyAsStringProperty());
    };

    const std::string body = "<Message ContentType=\"SimpleType\" />";
    ASSERT_TRUE(Push(channel.getChannelUriProperty()->ToString(), body));

    // The listener runs on its own thread, so the notification may not have landed yet; the
    // point of the queue is that it is not raised until the game asks for it.
    for (int attempt = 0; attempt < 50 && received.empty(); attempt++) {
        channel.DispatchPendingNotificationsEXT();
        if (received.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0], body);

    channel.Close();
}

TEST(HttpNotificationChannelTest, NothingIsRaisedUntilTheGameAsksForIt)
{
    HttpNotificationChannel channel("QueueChannel", "TestService");
    channel.Open();
    ASSERT_NE(channel.getChannelUriProperty(), nullptr);

    int raised = 0;
    channel.HttpNotificationReceived += [&raised](System::Object*,
                                                  const HttpNotificationEventArgs&) { raised++; };

    ASSERT_TRUE(Push(channel.getChannelUriProperty()->ToString(), "one"));
    ASSERT_TRUE(Push(channel.getChannelUriProperty()->ToString(), "two"));

    // The sender has been answered, so the bytes are here -- and still nothing has run.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(raised, 0);

    for (int attempt = 0; attempt < 50 && raised < 2; attempt++) {
        channel.DispatchPendingNotificationsEXT();
        if (raised < 2) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    EXPECT_EQ(raised, 2);

    channel.Close();
}

TEST(HttpNotificationChannelTest, AnOpenChannelCanBeFoundByName)
{
    EXPECT_EQ(HttpNotificationChannel::Find("FindableChannel"), nullptr);

    HttpNotificationChannel channel("FindableChannel", "TestService");
    channel.Open();

    EXPECT_EQ(HttpNotificationChannel::Find("FindableChannel"), &channel);

    channel.Close();
    EXPECT_EQ(HttpNotificationChannel::Find("FindableChannel"), nullptr);
}

TEST(HttpNotificationChannelTest, ShellToastBindingIsRecorded)
{
    HttpNotificationChannel channel("ToastChannel", "TestService");
    EXPECT_FALSE(channel.getIsShellToastBoundProperty());

    channel.BindToShellToast();

    EXPECT_TRUE(channel.getIsShellToastBoundProperty());
}

TEST(PushNotificationSenderTest, WhatTheSenderPostsIsWhatTheChannelReceives)
{
    // Both halves of the push path against each other: the sender a service uses, and the
    // channel an application opens.
    HttpNotificationChannel channel("SenderChannel", "TestService");
    channel.Open();
    ASSERT_NE(channel.getChannelUriProperty(), nullptr);

    std::vector<std::string> received;
    channel.HttpNotificationReceived += [&received](System::Object*,
                                                    const HttpNotificationEventArgs& e) {
        received.push_back(e.getBodyAsStringProperty());
    };

    Microsoft::Phone::Notification::RawPushNotificationMessage message(
        Microsoft::Phone::Notification::MessageSendPriority::High);
    const std::string payload = "<Message ContentType=\"GameState\" />";
    message.RawData.assign(payload.begin(), payload.end());

    ASSERT_TRUE(message.SendAsync(*channel.getChannelUriProperty()));

    for (int attempt = 0; attempt < 50 && received.empty(); attempt++) {
        channel.DispatchPendingNotificationsEXT();
        if (received.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0], payload);

    channel.Close();
}

TEST(PushNotificationSenderTest, AToastCarriesItsTitleToTheReceiver)
{
    HttpNotificationChannel channel("ToastSenderChannel", "TestService");
    channel.Open();
    ASSERT_NE(channel.getChannelUriProperty(), nullptr);

    std::vector<std::string> received;
    channel.HttpNotificationReceived += [&received](System::Object*,
                                                    const HttpNotificationEventArgs& e) {
        received.push_back(e.getBodyAsStringProperty());
    };

    Microsoft::Phone::Notification::ToastPushNotificationMessage toast(
        Microsoft::Phone::Notification::MessageSendPriority::High);
    toast.Title = "Yacht - Network player made his step";

    ASSERT_TRUE(toast.SendAsync(*channel.getChannelUriProperty()));

    for (int attempt = 0; attempt < 50 && received.empty(); attempt++) {
        channel.DispatchPendingNotificationsEXT();
        if (received.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    ASSERT_EQ(received.size(), 1u);
    EXPECT_NE(received[0].find("Yacht - Network player made his step"), std::string::npos);

    channel.Close();
}

TEST(PushNotificationSenderTest, SendingToAnAddressNobodyIsListeningOnFails)
{
    Microsoft::Phone::Notification::RawPushNotificationMessage message;
    message.RawData = {1, 2, 3};

    // Port 1 on loopback: nothing binds it, so the connection is refused rather than hanging.
    EXPECT_FALSE(message.SendAsync(System::Uri("http://127.0.0.1:1/nothing/")));
}

// SPDX-License-Identifier: MS-PL
// The console's social notifications: a new message, a friend request and a friend coming online,
// raised from the signed-in account's inbox and friends; the first read after sign-in only learns.
#include <gtest/gtest.h>
#include "../../../../../src/Internal/GuideOverlay.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/Internal/GamerServices/ServiceInvitations.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "System/IServiceProvider.hpp"
#include <algorithm>
#include <chrono>
#include <thread>

namespace Service = CNA::Internal::GamerServices;
using namespace Microsoft::Xna::Framework::GamerServices;

namespace {
struct Provider final : System::IServiceProvider {
    void* GetService(const std::type_info&) const override { return nullptr; }
};

class SocialNotificationTest : public ::testing::Test {
protected:
    void SetUp() override {
        previous_ = Service::backend();
        std::vector<Service::ServiceIdentity> people;
        for (auto [id, tag] : {std::pair{"a", "Alice"}, {"b", "Bob"}, {"c", "Carol"}}) {
            Service::ServiceIdentity person;
            person.userId = id;
            person.gamertag = tag;
            people.push_back(person);
        }
        service_ = Service::makeFakeBackend(std::move(people));
        Service::setBackendForTesting(service_);
        Service::resetInvitationsForTesting();
        if (!GamerServicesDispatcher::getIsInitializedProperty()) GamerServicesDispatcher::Initialize(provider_);
        // Bob asked to be friends and wrote before Alice signed in, then went offline; she accepts
        // once she is in. (The fake service lets only a present account act.)
        Service::setFakeRemotePresence(*service_, "b", true);
        service_->changeFriend("b", "Alice", "add");
        service_->sendMessage("b", {"Alice"}, "Sent while you were away");
        Service::setFakeRemotePresence(*service_, "b", false);
        service_->signIn(0, "Alice", "fixture");
        Settle([] { return Gamer::getSignedInGamersProperty()->getCountProperty() == 1; });
        service_->changeFriend("a", "Bob", "accept");
    }
    void TearDown() override {
        service_->signOut(0);
        Settle([] { return Gamer::getSignedInGamersProperty()->getCountProperty() == 0; });
        Service::resetInvitationsForTesting();
        Service::setBackendForTesting(previous_);
    }
    template <typename Condition>
    static bool Settle(Condition condition) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!condition()) {
            if (std::chrono::steady_clock::now() > deadline) return false;
            GamerServicesDispatcher::Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }
    static bool Showing(const std::string& text) {
        const auto toasts = Service::guideNotifications();
        return std::find(toasts.begin(), toasts.end(), text) != toasts.end();
    }
    // One complete read: the watcher polls, and its completion runs at a later Update.
    static void Read() {
        Service::pollSocialNowForTesting();
        for (int frame = 0; frame < 50; ++frame) {
            GamerServicesDispatcher::Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    Provider provider_;
    std::shared_ptr<Service::IGamerServicesBackend> previous_, service_;
};
}

TEST_F(SocialNotificationTest, WhatChangedSinceTheLastReadIsAnnounced) {
    Read();
    // What was already there when Alice signed in is not news.
    EXPECT_FALSE(Showing("Message from Bob: Sent while you were away"));
    Service::setFakeRemotePresence(*service_, "b", true);
    service_->sendMessage("b", {"Alice"}, "Rematch tonight?");
    Service::setFakeRemotePresence(*service_, "c", true);
    service_->changeFriend("c", "Alice", "add");
    Read();
    EXPECT_TRUE(Showing("Message from Bob: Rematch tonight?"));
    EXPECT_TRUE(Showing("Friend request from Carol: Open their gamer card to answer"));
    EXPECT_TRUE(Showing("Bob is now online"));
    // Nothing new: nothing more.
    const auto count = Service::guideNotifications().size();
    Read();
    EXPECT_EQ(count, Service::guideNotifications().size());
}

TEST_F(SocialNotificationTest, APushHintReadsAtOnceInsteadOfAtTheInterval) {
    Service::setFakeRemotePresence(*service_, "c", true);
    // No forced read: an account that just signed in is read at once by itself.
    for (int frame = 0; frame < 50; ++frame) {
        GamerServicesDispatcher::Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    service_->sendMessage("c", {"Alice"}, "Check out the new track.");
    // Without a hint the next read is a quarter of a minute away.
    for (int frame = 0; frame < 50; ++frame) GamerServicesDispatcher::Update();
    EXPECT_FALSE(Showing("Message from Carol: Check out the new track."));
    Service::serviceHint("messages");
    EXPECT_TRUE(Settle([] { return Showing("Message from Carol: Check out the new track."); }));
}

TEST_F(SocialNotificationTest, ALongMessageIsShortenedOnAWholeCharacter) {
    Service::setFakeRemotePresence(*service_, "b", true);
    Read();
    service_->sendMessage("b", {"Alice"}, "Caf\xc3\xa9 caf\xc3\xa9 caf\xc3\xa9 caf\xc3\xa9 caf\xc3\xa9 caf\xc3\xa9 caf\xc3\xa9 caf\xc3\xa9 caf\xc3\xa9");
    Read();
    const auto toasts = Service::guideNotifications();
    const auto found = std::find_if(toasts.begin(), toasts.end(), [](const std::string& text) { return text.starts_with("Message from Bob: "); });
    ASSERT_NE(found, toasts.end());
    EXPECT_TRUE(found->ends_with("..."));
    EXPECT_LE(found->size(), std::string("Message from Bob: ").size() + 48);
    // No split UTF-8 sequence before the ellipsis.
    const auto body = found->substr(0, found->size() - 3);
    EXPECT_NE(static_cast<unsigned char>(body.back()) & 0xC0, 0xC0);
}

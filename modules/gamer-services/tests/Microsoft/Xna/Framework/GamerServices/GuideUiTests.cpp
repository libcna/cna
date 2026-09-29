// SPDX-License-Identifier: MS-PL
// The CNA system Guide screens, driven as a player would (commands, choices) without a device.
// Named Guide* so it sorts after GamerServicesDispatcherTest.IsInitializedDefaultsFalse.
#include <gtest/gtest.h>
#include "../../../../../src/Internal/GuideOverlay.hpp"
#include "../../../../../src/Internal/Guide/GuideSystem.hpp"
#include "../../../../../src/Internal/Guide/GuideScreen.hpp"
#include "../../../../../src/Internal/Guide/GuideUi.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/Internal/GamerServices/ServiceInvitations.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "System/IServiceProvider.hpp"
#include <algorithm>
#include <chrono>
#include <thread>

namespace Service = CNA::Internal::GamerServices;
namespace Ui = CNA::Internal::GamerServices::GuideUi;
using namespace Microsoft::Xna::Framework::GamerServices;
using Microsoft::Xna::Framework::PlayerIndex;

namespace {
struct Provider final : System::IServiceProvider {
    void* GetService(const std::type_info&) const override { return nullptr; }
};

class GuideUiTest : public ::testing::Test {
protected:
    void SetUp() override {
        previous_ = Service::backend();
        std::vector<Service::ServiceIdentity> people;
        for (auto [id, tag] : {std::pair{"a", "Alice"}, {"b", "Bob"}, {"c", "Carol"}, {"d", "Dave"}, {"e", "Erin"}}) {
            Service::ServiceIdentity person;
            person.userId = id;
            person.gamertag = tag;
            person.allowOnlineSessions = true;
            people.push_back(person);
        }
        service_ = Service::makeFakeBackend(std::move(people));
        Service::setBackendForTesting(service_);
        Service::resetInvitationsForTesting();
        if (!GamerServicesDispatcher::getIsInitializedProperty()) GamerServicesDispatcher::Initialize(provider_);
        service_->signIn(0, "Alice", "fixture");
        Settle([] { return Gamer::getSignedInGamersProperty()->getCountProperty() == 1; });
        // Alice: Bob and Carol are friends (Bob playing, Carol away), Dave asked her, she asked Erin.
        for (const char* id : {"b", "c", "d"}) Service::setFakeRemotePresence(*service_, id, true);
        for (const char* tag : {"Bob", "Carol", "Erin"}) service_->changeFriend("a", tag, "add");
        service_->changeFriend("b", "Alice", "accept");
        service_->changeFriend("c", "Alice", "accept");
        service_->changeFriend("d", "Alice", "add");
        Service::setFakeRemotePresence(*service_, "b", true, "Racing", "online");
        Service::setFakeRemotePresence(*service_, "c", true, "", "away");
        Service::setFakeRemotePresence(*service_, "d", false);
    }
    void TearDown() override {
        Ui::closeAll();
        Guide::ResetPendingKeyboardInputForTestingEXT();
        Service::setFakeAvatarsUnreachable(*service_, false);
        service_->signOut(0);
        Settle([] { return Gamer::getSignedInGamersProperty()->getCountProperty() == 0; });
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
    static bool Labels(std::size_t count) { return Settle([&] { return Ui::labelsForTesting().size() >= count; }); }
    Provider provider_;
    std::shared_ptr<Service::IGamerServicesBackend> previous_, service_;
};
}

TEST_F(GuideUiTest, FriendsListRequestsFirstThenWhoIsOnlineWithTheirPresence) {
    Guide::ShowFriends(PlayerIndex::One);
    EXPECT_TRUE(Guide::getIsVisibleProperty());
    ASSERT_EQ("friends", Ui::currentScreenForTesting());
    ASSERT_TRUE(Labels(4));
    EXPECT_EQ((std::vector<std::string>{"Dave - Wants to be your friend", "Bob - Online \xe2\x80\xa2 Racing", "Carol - Away",
                                        "Erin - Friend request sent"}),
              Ui::labelsForTesting());
}

TEST_F(GuideUiTest, TheFocusMovesAndStopsAtTheEndsAndAcceptOpensTheGamerCard) {
    Guide::ShowFriends(PlayerIndex::One);
    ASSERT_TRUE(Labels(4));
    EXPECT_EQ(0, Ui::focusForTesting());
    Ui::sendForTesting(Ui::Command::Up);
    EXPECT_EQ(0, Ui::focusForTesting());
    for (int step = 0; step < 6; ++step) Ui::sendForTesting(Ui::Command::Down);
    EXPECT_EQ(3, Ui::focusForTesting());
    Ui::sendForTesting(Ui::Command::Up);
    Ui::sendForTesting(Ui::Command::Up);
    Ui::sendForTesting(Ui::Command::Accept);
    ASSERT_EQ("gamerCard", Ui::currentScreenForTesting());
    ASSERT_TRUE(Labels(2));
    EXPECT_EQ("Bob", Ui::labelsForTesting()[0]);
    // Back returns to the list where it was; Back again closes the Guide.
    Ui::sendForTesting(Ui::Command::Back);
    EXPECT_EQ("friends", Ui::currentScreenForTesting());
    EXPECT_EQ(1, Ui::focusForTesting());
    Ui::sendForTesting(Ui::Command::Back);
    EXPECT_FALSE(Guide::getIsVisibleProperty());
}

TEST_F(GuideUiTest, TheRailSwitchesCategoriesAndWrapsAround) {
    Service::openSystemGuide(PlayerIndex::One);
    const std::vector<std::string> order{"home", "friends", "party", "messages", "achievements", "leaderboards", "players", "content", "settings"};
    for (std::size_t step = 1; step <= order.size(); ++step) {
        Ui::sendForTesting(Ui::Command::Next);
        EXPECT_EQ(order[step % order.size()], Ui::currentScreenForTesting());
    }
    Ui::sendForTesting(Ui::Command::Previous);
    EXPECT_EQ("settings", Ui::currentScreenForTesting());
    // A dialog has no rail: switching does nothing there.
    Guide::ResetPendingKeyboardInputForTestingEXT();
    Ui::closeAll();
    std::unique_ptr<Gamer> bob(Gamer::GetFromGamertag("Bob"));
    Guide::ShowPlayerReview(PlayerIndex::One, bob.get());
    Ui::sendForTesting(Ui::Command::Next);
    EXPECT_EQ("review", Ui::currentScreenForTesting());
}

TEST_F(GuideUiTest, TheGamerCardAcceptsAFriendRequestAndReflectsIt) {
    std::unique_ptr<Gamer> dave(Gamer::GetFromGamertag("Dave"));
    Guide::ShowGamerCard(PlayerIndex::One, dave.get());
    ASSERT_TRUE(Labels(2));
    EXPECT_EQ((std::vector<std::string>{"Dave", "Accept friend request", "Send message", "Review player"}), Ui::labelsForTesting());
    Ui::sendForTesting(Ui::Command::Accept);
    ASSERT_TRUE(Settle([] { return Ui::labelsForTesting().size() > 1 && Ui::labelsForTesting()[1] == "Remove friend"; }));
    EXPECT_TRUE((*Gamer::getSignedInGamersProperty())[0]->IsFriend(dave.get()));
    // The card is still up, and the action was announced.
    EXPECT_EQ("gamerCard", Ui::currentScreenForTesting());
    const auto toasts = Service::guideNotifications();
    EXPECT_NE(std::find(toasts.begin(), toasts.end(), "Friends updated: Dave"), toasts.end());
}

TEST_F(GuideUiTest, YourOwnCardHasNoActions) {
    std::unique_ptr<Gamer> alice(Gamer::GetFromGamertag("Alice"));
    Guide::ShowGamerCard(PlayerIndex::One, alice.get());
    ASSERT_TRUE(Labels(1));
    EXPECT_EQ(std::vector<std::string>{"Alice"}, Ui::labelsForTesting());
}

TEST_F(GuideUiTest, AnUnreachableServiceShowsAnErrorInsteadOfAnEmptyList) {
    Service::setFakeAvatarsUnreachable(*service_, true);
    Guide::ShowFriends(PlayerIndex::One);
    for (int frame = 0; frame < 50; ++frame) GamerServicesDispatcher::Update();
    // The screen stays up and answers input; nothing was loaded.
    EXPECT_EQ("friends", Ui::currentScreenForTesting());
    EXPECT_TRUE(Ui::labelsForTesting().empty());
    Ui::sendForTesting(Ui::Command::Down);
    Ui::sendForTesting(Ui::Command::Back);
    EXPECT_FALSE(Guide::getIsVisibleProperty());
}

TEST_F(GuideUiTest, FindGamerOpensTheirCardFromTheirGamertag) {
    Guide::ShowFriends(PlayerIndex::One);
    Ui::sendForTesting(Ui::Command::Y);
    ASSERT_TRUE(Guide::getHasPendingKeyboardInputEXTProperty());
    for (unsigned char c : std::string("Erin")) Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextInput(c);
    Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextInput(u'\r');
    EXPECT_EQ("gamerCard", Ui::currentScreenForTesting());
    ASSERT_TRUE(Labels(2));
    EXPECT_EQ("Erin", Ui::labelsForTesting()[0]);
    EXPECT_EQ("Cancel friend request", Ui::labelsForTesting()[1]);
}

TEST_F(GuideUiTest, TheRailShowsWhoIsSignedInAndTheirUnreadMessages) {
    service_->sendMessage("b", {"Alice"}, "hello");
    Service::openSystemGuide(PlayerIndex::One);
    ASSERT_TRUE(Settle([] { return Ui::identity(PlayerIndex::One).unread == 1; }));
    const auto& who = Ui::identity(PlayerIndex::One);
    EXPECT_EQ("Alice", who.gamertag);
    EXPECT_TRUE(who.online);
    // Reading it lowers the count on the rail at once.
    Ui::closeAll();
    Guide::ShowMessages(PlayerIndex::One);
    ASSERT_TRUE(Labels(1));
    Ui::sendForTesting(Ui::Command::Accept);
    EXPECT_EQ("message", Ui::currentScreenForTesting());
    EXPECT_EQ(0, Ui::identity(PlayerIndex::One).unread);
}

TEST_F(GuideUiTest, AnInvitationClosedWithoutAnAnswerStaysPendingAndIsAnsweredOnce) {
    int answers = 0;
    std::optional<bool> last = true;
    Ui::open(Ui::invitationScreen(PlayerIndex::One, "Bob", "b", "A Player Match game.", [&](std::optional<bool> yes) {
        ++answers;
        last = yes;
    }), PlayerIndex::One);
    EXPECT_EQ("invitation", Ui::currentScreenForTesting());
    // View gamer card, then back to the invitation, then later.
    Ui::clickForTesting(2);
    EXPECT_EQ("gamerCard", Ui::currentScreenForTesting());
    Ui::sendForTesting(Ui::Command::Back);
    EXPECT_EQ("invitation", Ui::currentScreenForTesting());
    Ui::sendForTesting(Ui::Command::Back);
    EXPECT_FALSE(Guide::getIsVisibleProperty());
    EXPECT_EQ(1, answers);
    EXPECT_FALSE(last.has_value());
}

TEST_F(GuideUiTest, SystemSoundsPlayOrStaySilentWithoutFailing) {
    // Synthesized in-process; without an audio device the Guide simply stays silent.
    for (auto sound : {Ui::Sound::Move, Ui::Sound::Accept, Ui::Sound::Back, Ui::Sound::Open, Ui::Sound::Notify, Ui::Sound::Error})
        EXPECT_NO_THROW(Ui::play(sound));
    EXPECT_NO_THROW(Ui::releaseSounds());
}

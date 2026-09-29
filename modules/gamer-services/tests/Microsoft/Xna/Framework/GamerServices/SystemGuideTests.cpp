// SPDX-License-Identifier: MS-PL
// The system Guide menu the Guide button (Home) opens. Sorted after GamerServicesServiceTests.cpp,
// so IsInitializedDefaultsFalse still runs first.
#include <gtest/gtest.h>
#include "../../../../../src/Internal/GuideOverlay.hpp"
#include "CNA/GamerServices/Configuration.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/Internal/GamerServices/LocalProfiles.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GameDefaults.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPrivilegeException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPrivileges.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInEventArgs.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedOutEventArgs.hpp"
#include "System/ObjectDisposedException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/FriendCollection.hpp"
#include "System/IServiceProvider.hpp"
#include "System/InvalidOperationException.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace Service = CNA::Internal::GamerServices;
using namespace Microsoft::Xna::Framework::GamerServices;
using Microsoft::Xna::Framework::PlayerIndex;

namespace {
struct Provider final : System::IServiceProvider {
    void* GetService(const std::type_info&) const override { return nullptr; }
};
void Type(const std::string& value) {
    for (unsigned char character : value) Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextInput(character);
    Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextInput(u'\r');
}
template <typename Condition>
bool Settle(Condition condition) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!condition()) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        GamerServicesDispatcher::Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}
int Count() { return Gamer::getSignedInGamersProperty()->getCountProperty(); }

class SystemGuideTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = std::filesystem::temp_directory_path() / ("cna-system-guide-" + std::to_string(getpid()));
        std::filesystem::remove_all(root_);
        const auto* previous = std::getenv("CNA_GAMER_SERVICES_PROFILES_DIR");
        previousProfiles_ = previous ? std::optional<std::string>(previous) : std::nullopt;
        setenv("CNA_GAMER_SERVICES_PROFILES_DIR", root_.c_str(), 1);
        previous_ = Service::backend();
        if (!GamerServicesDispatcher::getIsInitializedProperty()) GamerServicesDispatcher::Initialize(provider_);
    }
    void TearDown() override {
        Guide::ResetPendingMessageBoxForTestingEXT();
        Guide::ResetPendingKeyboardInputForTestingEXT();
        for (auto* gamer : *Gamer::getSignedInGamersProperty())
            Service::backend()->signOut(static_cast<int>(gamer->getPlayerIndexProperty()));
        Settle([] { return Count() == 0; });
        Service::setBackendForTesting(previous_);
        CNA::GamerServices::setConfigurationOverride(std::nullopt);
        if (previousProfiles_) setenv("CNA_GAMER_SERVICES_PROFILES_DIR", previousProfiles_->c_str(), 1);
        else unsetenv("CNA_GAMER_SERVICES_PROFILES_DIR");
        std::filesystem::remove_all(root_);
    }
    void Offline() {
        CNA::GamerServices::setConfigurationOverride(CNA::GamerServices::Configuration{});
        Service::setBackendForTesting({});
    }
    std::shared_ptr<Service::IGamerServicesBackend> Fake() {
        Service::ServiceIdentity alice;
        alice.userId = "a";
        alice.gamertag = "Alice";
        alice.allowOnlineSessions = true;
        auto fake = Service::makeFakeBackend({alice});
        Service::setBackendForTesting(fake);
        return fake;
    }
    std::filesystem::path root_;
    std::optional<std::string> previousProfiles_;
    Provider provider_;
    std::shared_ptr<Service::IGamerServicesBackend> previous_;
};
}

TEST_F(SystemGuideTest, WithNobodySignedInItOffersSignIn) {
    Offline();
    Service::openSystemGuide(PlayerIndex::One);
    ASSERT_TRUE(Guide::getHasPendingMessageBoxEXTProperty());
    EXPECT_EQ(0, Guide::GetPendingMessageBoxFocusButtonForTestingEXT());
    Guide::SimulateMessageBoxClickEXT(0);
    ASSERT_TRUE(Guide::getHasPendingKeyboardInputEXTProperty());
    EXPECT_EQ("Sign in", Guide::GetPendingKeyboardInputTitleForTestingEXT());
    Type("Robin");
    ASSERT_TRUE(Settle([] { return Count() == 1; }));
    EXPECT_EQ("Robin", (*Gamer::getSignedInGamersProperty())[0]->getGamertagProperty());
}

TEST_F(SystemGuideTest, APlayersButtonSignsInThatPlayersSlot) {
    Offline();
    Service::openSystemGuide(PlayerIndex::Two);
    Guide::SimulateMessageBoxClickEXT(0);
    // Two panes: the first empty slot is player one's, then player two's.
    Type("Robin");
    ASSERT_TRUE(Settle([] { return Count() == 1 && Guide::getHasPendingKeyboardInputEXTProperty(); }));
    Type("Sam");
    ASSERT_TRUE(Settle([] { return Count() == 2; }));
    EXPECT_EQ(PlayerIndex::Two, (*Gamer::getSignedInGamersProperty())[1]->getPlayerIndexProperty());
}

TEST_F(SystemGuideTest, ALocalProfileCanSignOutFromTheGuide) {
    Offline();
    Service::backend()->signInLocal(0, "Robin");
    ASSERT_TRUE(Settle([] { return Count() == 1; }));
    int signedOut = 0;
    const auto token = SignedInGamer::SignedOut.Add([&](System::Object*, const SignedOutEventArgs&) { ++signedOut; });
    Service::openSystemGuide(PlayerIndex::One);
    ASSERT_TRUE(Guide::getHasPendingMessageBoxEXTProperty());
    EXPECT_EQ(1, Guide::GetPendingMessageBoxFocusButtonForTestingEXT());
    Guide::SimulateMessageBoxClickEXT(0);
    EXPECT_TRUE(Settle([] { return Count() == 0; }));
    SignedInGamer::SignedOut.Remove(token);
    EXPECT_EQ(1, signedOut);
}

TEST_F(SystemGuideTest, AServicePlayerReachesFriendsAndInvitations) {
    auto fake = Fake();
    fake->signIn(0, "Alice", "fixture");
    ASSERT_TRUE(Settle([] { return Count() == 1; }));
    Service::openSystemGuide(PlayerIndex::One);
    ASSERT_TRUE(Guide::getHasPendingMessageBoxEXTProperty());
    Guide::SimulateMessageBoxClickEXT(0);
    // The friends pane is the Guide's own next screen.
    EXPECT_TRUE(Guide::getIsVisibleProperty());
    Guide::ResetPendingMessageBoxForTestingEXT();
    Guide::ResetPendingKeyboardInputForTestingEXT();
    Settle([] { return !Guide::getIsVisibleProperty(); });

    // Without an online session there is nothing to invite to; the Guide says so.
    Service::openSystemGuide(PlayerIndex::One);
    Guide::SimulateMessageBoxClickEXT(1);
    EXPECT_TRUE(Guide::getHasPendingMessageBoxEXTProperty());
    Guide::SimulateMessageBoxClickEXT(0);
    EXPECT_FALSE(Guide::getIsVisibleProperty());
}

TEST_F(SystemGuideTest, OnlineStatusIsWhatFriendsSeeAsAwayOrBusy) {
    Service::ServiceIdentity alice, bob;
    alice.userId = "a";
    alice.gamertag = "Alice";
    bob.userId = "b";
    bob.gamertag = "Bob";
    auto fake = Service::makeFakeBackend({alice, bob});
    Service::setBackendForTesting(fake);
    fake->signIn(0, "Alice", "fixture");
    fake->signIn(1, "Bob", "fixture");
    ASSERT_TRUE(Settle([] { return Count() == 2; }));
    fake->changeFriend("a", "Bob", "add");
    fake->changeFriend("b", "Alice", "accept");
    auto bobSees = [&] {
        auto friends = (*Gamer::getSignedInGamersProperty())[PlayerIndex::Two]->GetFriends();
        return std::pair{friends[0]->getIsAwayProperty(), friends[0]->getIsBusyProperty()};
    };
    EXPECT_EQ(bobSees(), std::pair(false, false));

    for (const auto& [button, expected] : {std::pair{1, std::pair{true, false}}, std::pair{2, std::pair{false, true}},
                                           std::pair{0, std::pair{false, false}}}) {
        Service::openSystemGuide(PlayerIndex::One);
        ASSERT_TRUE(Guide::getHasPendingMessageBoxEXTProperty());
        Guide::SimulateMessageBoxClickEXT(3);
        ASSERT_TRUE(Guide::getHasPendingMessageBoxEXTProperty());
        Guide::SimulateMessageBoxClickEXT(button);
        EXPECT_TRUE(Settle([&] { return bobSees() == expected; })) << button;
        Settle([] { return !Guide::getIsVisibleProperty(); });
    }
    // A friend who signs out is simply offline, whatever the status.
    Service::openSystemGuide(PlayerIndex::One);
    Guide::SimulateMessageBoxClickEXT(3);
    Guide::SimulateMessageBoxClickEXT(1);
    ASSERT_TRUE(Settle([&] { return bobSees().first; }));
    fake->signOut(0);
    ASSERT_TRUE(Settle([] { return Count() == 1; }));
    EXPECT_EQ(bobSees(), std::pair(false, false));
}

// An account's GameDefaults are the ones its service keeps, in a local profile's form.
TEST_F(SystemGuideTest, AnAccountCarriesItsServiceGameDefaults) {
    Service::ServiceIdentity alice;
    alice.userId = "a";
    alice.gamertag = "Alice";
    alice.gameDefaults = R"({"gameDifficulty":"Easy","invertYAxis":true,"secondaryColor":"#0080ff","racingCameraAngle":"Front"})";
    auto fake = Service::makeFakeBackend({alice});
    Service::setBackendForTesting(fake);
    fake->signIn(0, "Alice", "fixture");
    ASSERT_TRUE(Settle([] { return Count() == 1; }));
    const auto& defaults = (*Gamer::getSignedInGamersProperty())[0]->getGameDefaultsProperty();
    EXPECT_EQ(GameDifficulty::Easy, defaults.getGameDifficultyProperty());
    EXPECT_TRUE(defaults.getInvertYAxisProperty());
    ASSERT_TRUE(defaults.getSecondaryColorProperty().has_value());
    EXPECT_EQ(Microsoft::Xna::Framework::Color(0, 128, 255), *defaults.getSecondaryColorProperty());
    EXPECT_EQ(RacingCameraAngle::Front, defaults.getRacingCameraAngleProperty());
    EXPECT_EQ(ControllerSensitivity::Low, defaults.getControllerSensitivityProperty());
}

// XNA ShowSignIn(onlineOnly: true): "local gamers can sign in as guests of a profile currently signed in".
TEST_F(SystemGuideTest, AGuestSignsInWithItsAccountAndLeavesWithIt) {
    auto fake = Fake();
    fake->signIn(0, "Alice", "fixture");
    ASSERT_TRUE(Settle([] { return Count() == 1; }));
    Guide::ShowSignIn(2, true);
    ASSERT_TRUE(Guide::getHasPendingKeyboardInputEXTProperty());
    Type("guest");
    ASSERT_TRUE(Settle([] { return Count() == 2; }));
    auto* guest = (*Gamer::getSignedInGamersProperty())[PlayerIndex::Two];
    ASSERT_NE(nullptr, guest);
    EXPECT_EQ("Alice (1)", guest->getGamertagProperty());
    EXPECT_TRUE(guest->getIsGuestProperty());
    EXPECT_TRUE(guest->getIsSignedInToLiveProperty());
    EXPECT_FALSE(guest->getPrivilegesProperty().getAllowOnlineSessionsProperty());
    EXPECT_THROW(guest->AwardAchievement("first"), GamerPrivilegeException);
    EXPECT_FALSE((*Gamer::getSignedInGamersProperty())[PlayerIndex::One]->getIsGuestProperty());
    // Signing the account out takes its guest along.
    fake->signOut(0);
    ASSERT_TRUE(Settle([] { return Count() == 0; }));
}

TEST_F(SystemGuideTest, WithoutOnlineOnlyGuestIsAnOrdinaryUsername) {
    auto fake = Fake();
    fake->signIn(0, "Alice", "fixture");
    ASSERT_TRUE(Settle([] { return Count() == 1; }));
    Guide::ShowSignIn(2, false);
    Type("guest");
    // The account sign-in carries on: the password pane.
    ASSERT_TRUE(Guide::getHasPendingKeyboardInputEXTProperty());
    EXPECT_EQ(1, Count());
    Guide::ResetPendingKeyboardInputForTestingEXT();
}

TEST_F(SystemGuideTest, AGuestSignsItselfOutFromTheGuide) {
    auto fake = Fake();
    fake->signIn(0, "Alice", "fixture");
    ASSERT_TRUE(Settle([] { return Count() == 1; }));
    fake->signInGuest(1, "Alice (1)", 0);
    ASSERT_TRUE(Settle([] { return Count() == 2; }));
    Service::openSystemGuide(PlayerIndex::Two);
    ASSERT_TRUE(Guide::getHasPendingMessageBoxEXTProperty());
    Guide::SimulateMessageBoxClickEXT(0);
    ASSERT_TRUE(Settle([] { return Count() == 1; }));
    EXPECT_EQ("Alice", (*Gamer::getSignedInGamersProperty())[0]->getGamertagProperty());
}

TEST_F(SystemGuideTest, NothingOpensWhileTheGuideIsVisible) {
    Offline();
    auto* result = Guide::BeginShowMessageBox("Game", "Busy", {"OK"}, 0, MessageBoxIcon::None, {}, {});
    Service::openSystemGuide(PlayerIndex::One);
    Guide::SimulateMessageBoxClickEXT(0);
    EXPECT_FALSE(Guide::getIsVisibleProperty());
    delete result;
    Service::openSystemGuide(static_cast<PlayerIndex>(7));
    EXPECT_FALSE(Guide::getIsVisibleProperty());
}

// Reference SignedInGamer: SignedIn's add accessor tells a new handler about every gamer already
// signed in (sender null); signing out disposes the old gamer before SignedOut, leaving
// IsSignedInToLive as it was.
TEST_F(SystemGuideTest, SignedInReplaysOnSubscribeAndSigningOutDisposesTheGamer) {
    auto fake = Fake();
    fake->signIn(0, "Alice", "fixture");
    ASSERT_TRUE(Settle([] { return Count() == 1; }));
    std::vector<std::string> replayed;
    const auto signedIn = SignedInGamer::SignedIn.Add([&](System::Object* sender, const SignedInEventArgs& e) {
        EXPECT_EQ(nullptr, sender);
        replayed.push_back(e.getGamerProperty()->getGamertagProperty());
    });
    EXPECT_EQ(std::vector<std::string>{"Alice"}, replayed);
    SignedInGamer* alice = (*Gamer::getSignedInGamersProperty())[0];
    bool disposedWhenSignedOut = false;
    const auto signedOut = SignedInGamer::SignedOut.Add([&](System::Object*, const SignedOutEventArgs& e) {
        disposedWhenSignedOut = e.getGamerProperty() == alice && e.getGamerProperty()->getIsDisposedProperty();
    });
    fake->signOut(0);
    EXPECT_TRUE(Settle([] { return Count() == 0; }));
    SignedInGamer::SignedIn.Remove(signedIn);
    SignedInGamer::SignedOut.Remove(signedOut);
    EXPECT_TRUE(disposedWhenSignedOut);
    EXPECT_TRUE(alice->getIsDisposedProperty());
    EXPECT_TRUE(alice->getIsSignedInToLiveProperty());
    EXPECT_THROW((void)alice->BeginGetProfile({}, {}), System::ObjectDisposedException);
    EXPECT_THROW((void)alice->GetFriends(), System::ObjectDisposedException);
}

TEST_F(SystemGuideTest, IsVisibleIsThePublicViewOfTheGuidePanes) {
    Offline();
    EXPECT_FALSE(Guide::getIsVisibleProperty());
    auto* result = Guide::BeginShowMessageBox("Game", "Busy", {"OK"}, 0, MessageBoxIcon::None, {}, {});
    EXPECT_TRUE(Guide::getIsVisibleProperty());
    Guide::SimulateMessageBoxClickEXT(0);
    EXPECT_FALSE(Guide::getIsVisibleProperty());
    delete result;
}

// Reference Guide.IsVisible throws InvalidOperationException before gamer services are
// initialized. Initialization cannot be undone in a process, so a fresh one observes it.
#ifdef __linux__
TEST(GuideVisibilityTest, IsVisibleRequiresInitializedGamerServices) {
    if (std::getenv("CNA_TEST_GUIDE_VISIBLE_CHILD") != nullptr) {
        ASSERT_FALSE(GamerServicesDispatcher::getIsInitializedProperty());
        EXPECT_THROW((void)Guide::getIsVisibleProperty(), System::InvalidOperationException);
        return;
    }
    const pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        setenv("CNA_TEST_GUIDE_VISIBLE_CHILD", "1", 1);
        execl("/proc/self/exe", "cna-guide-visible",
              "--gtest_filter=GuideVisibilityTest.IsVisibleRequiresInitializedGamerServices", static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    ASSERT_EQ(pid, waitpid(pid, &status, 0));
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(0, WEXITSTATUS(status));
}
#endif

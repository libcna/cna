// SPDX-License-Identifier: MS-PL
// The system Guide menu the Guide button (Home) opens. Sorted after GamerServicesServiceTests.cpp,
// so IsInitializedDefaultsFalse still runs first.
#include <gtest/gtest.h>
#include "../../../../../src/Internal/GuideOverlay.hpp"
#include "../../../../../src/Internal/Guide/GuideUi.hpp"
#include "CNA/GamerServices/Configuration.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/Internal/GamerServices/LocalProfiles.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GameDefaults.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPrivilegeException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPrivileges.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerProfile.hpp"
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
namespace Ui = CNA::Internal::GamerServices::GuideUi;
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
// Chooses the item of the top Guide screen whose label starts with a text.
bool Choose(const std::string& label) {
    const auto labels = Ui::labelsForTesting();
    for (std::size_t index = 0; index < labels.size(); ++index) {
        if (labels[index].starts_with(label)) {
            Ui::clickForTesting(static_cast<int>(index));
            return true;
        }
    }
    return false;
}

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
        Ui::closeAll();
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
    ASSERT_EQ("home", Ui::currentScreenForTesting());
    EXPECT_EQ(std::vector<std::string>{"Sign in"}, Ui::labelsForTesting());
    EXPECT_EQ(0, Ui::focusForTesting());
    Ui::sendForTesting(Ui::Command::Accept);
    ASSERT_EQ("signIn", Ui::currentScreenForTesting());
    Type("Robin");
    ASSERT_TRUE(Settle([] { return Count() == 1; }));
    EXPECT_EQ("Robin", (*Gamer::getSignedInGamersProperty())[0]->getGamertagProperty());
}

// The Guide button toggles the Guide, as on the console, but never closes a game's own dialog.
TEST_F(SystemGuideTest, TheGuideButtonClosesTheGuideItOpened) {
    Fake()->signIn(0, "Alice", "fixture");
    ASSERT_TRUE(Settle([] { return Count() == 1; }));
    Service::systemGuideButton(PlayerIndex::One);
    ASSERT_EQ("home", Ui::currentScreenForTesting());
    Ui::sendForTesting(Ui::Command::Next);
    Service::systemGuideButton(PlayerIndex::One);
    EXPECT_FALSE(Ui::visible());
    (void)Guide::BeginShowMessageBox(PlayerIndex::One, "Save", "Overwrite?", {"Yes", "No"}, 0, MessageBoxIcon::None, {}, {});
    Service::systemGuideButton(PlayerIndex::One);
    EXPECT_TRUE(Guide::getIsVisibleProperty());
}

TEST_F(SystemGuideTest, APlayersButtonSignsInThatPlayersSlot) {
    Offline();
    Service::openSystemGuide(PlayerIndex::Two);
    ASSERT_TRUE(Choose("Sign in"));
    // Two panes: the first empty slot is player one's, then player two's.
    Type("Robin");
    ASSERT_TRUE(Settle([] { return Count() == 1 && Ui::currentScreenForTesting() == "signIn"; }));
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
    // An offline profile: what works offline, and signing out.
    EXPECT_EQ((std::vector<std::string>{"Achievements", "Edit avatar", "Sign out"}), Ui::labelsForTesting());
    ASSERT_TRUE(Choose("Sign out"));
    EXPECT_FALSE(Ui::visible());
    EXPECT_TRUE(Settle([] { return Count() == 0; }));
    SignedInGamer::SignedOut.Remove(token);
    EXPECT_EQ(1, signedOut);
}

TEST_F(SystemGuideTest, AServicePlayerReachesFriendsAndInvitations) {
    auto fake = Fake();
    fake->signIn(0, "Alice", "fixture");
    ASSERT_TRUE(Settle([] { return Count() == 1; }));
    Service::openSystemGuide(PlayerIndex::One);
    ASSERT_EQ("home", Ui::currentScreenForTesting());
    ASSERT_TRUE(Choose("Friends"));
    EXPECT_EQ("friends", Ui::currentScreenForTesting());
    EXPECT_TRUE(Guide::getIsVisibleProperty());
    // The rail's next category replaces the screen; Back from the root closes the Guide.
    Ui::sendForTesting(Ui::Command::Next);
    EXPECT_EQ("party", Ui::currentScreenForTesting());
    Ui::sendForTesting(Ui::Command::Previous);
    Ui::sendForTesting(Ui::Command::Previous);
    EXPECT_EQ("home", Ui::currentScreenForTesting());
    Ui::sendForTesting(Ui::Command::Back);
    EXPECT_FALSE(Guide::getIsVisibleProperty());
    // Without an online session there is nothing to invite to (XNA's InvalidOperationException).
    EXPECT_THROW(Guide::ShowGameInvite(PlayerIndex::One, std::vector<Gamer*>{}), System::InvalidOperationException);
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

    Service::openSystemGuide(PlayerIndex::One);
    ASSERT_TRUE(Choose("Online status"));
    ASSERT_EQ("settings", Ui::currentScreenForTesting());
    // Online status steps Online, Away, Busy and round again.
    for (const auto& expected : {std::pair{true, false}, std::pair{false, true}, std::pair{false, false}}) {
        Ui::sendForTesting(Ui::Command::Right);
        EXPECT_TRUE(Settle([&] { return bobSees() == expected; }));
    }
    // A friend who signs out is simply offline, whatever the status.
    Ui::sendForTesting(Ui::Command::Right);
    ASSERT_TRUE(Settle([&] { return bobSees().first; }));
    Ui::closeAll();
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
    ASSERT_EQ("signIn", Ui::currentScreenForTesting());
    EXPECT_EQ((std::vector<std::string>{"Sign in with a CNA account", "Play as a guest of Alice"}), Ui::labelsForTesting());
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
    EXPECT_EQ(std::vector<std::string>{"Sign out"}, Ui::labelsForTesting());
    ASSERT_TRUE(Choose("Sign out"));
    ASSERT_TRUE(Settle([] { return Count() == 1; }));
    EXPECT_EQ("Alice", (*Gamer::getSignedInGamersProperty())[0]->getGamertagProperty());
}

// XNA GamerProfile: GamerZone is the member's own choice (the Guide's Gamer zone), Reputation the
// stars other players' reviews give; nobody reviewed means XNA's unset 0.
TEST_F(SystemGuideTest, GamerZoneIsChosenInTheGuideAndReputationComesFromReviews) {
    Service::ServiceIdentity alice, bob;
    alice.userId = "a";
    alice.gamertag = "Alice";
    bob.userId = "b";
    bob.gamertag = "Bob";
    bob.reputation = 3.75f;
    auto fake = Service::makeFakeBackend({alice, bob});
    Service::setBackendForTesting(fake);
    fake->signIn(0, "Alice", "fixture");
    ASSERT_TRUE(Settle([] { return Count() == 1; }));
    auto profileOf = [&](Gamer* gamer) {
        std::unique_ptr<GamerProfile> profile;
        std::unique_ptr<System::IAsyncResult> result(gamer->BeginGetProfile({}, {}));
        Settle([&] { return result->getIsCompletedProperty(); });
        profile.reset(gamer->EndGetProfile(result.get()));
        return profile;
    };
    auto* self = (*Gamer::getSignedInGamersProperty())[0];
    auto mine = profileOf(self);
    EXPECT_EQ(GamerZone::Unknown, mine->getGamerZoneProperty());
    EXPECT_FLOAT_EQ(0.0f, mine->getReputationProperty());
    Service::openSystemGuide(PlayerIndex::One);
    ASSERT_TRUE(Choose("Online status"));
    // The gamer zone row steps Recreation, Pro, Family.
    Ui::sendForTesting(Ui::Command::Down);
    EXPECT_EQ(1, Ui::focusForTesting());
    for (int step = 0; step < 3; ++step) Ui::sendForTesting(Ui::Command::Right);
    Ui::closeAll();
    EXPECT_TRUE(Settle([&] { return profileOf(self)->getGamerZoneProperty() == GamerZone::Family; }));
    std::unique_ptr<Gamer> other(Gamer::GetFromGamertag("Bob"));
    EXPECT_FLOAT_EQ(3.75f, profileOf(other.get())->getReputationProperty());
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

TEST_F(SystemGuideTest, TrialModeIsLatchedAtEachUpdateAndClosesOnlineSessions) {
    auto fake = Fake();
    fake->signIn(0, "Alice", "fixture");
    ASSERT_TRUE(Settle([] { return Count() == 1; }));
    auto* alice = (*Gamer::getSignedInGamersProperty())[0];
    ASSERT_FALSE(Guide::getIsTrialModeProperty());
    ASSERT_TRUE(alice->getPrivilegesProperty().getAllowOnlineSessionsProperty());
    // The request takes effect at the next update, as XNA reads the Guide state there.
    Guide::setSimulateTrialModeProperty(true);
    EXPECT_FALSE(Guide::getIsTrialModeProperty());
    GamerServicesDispatcher::Update();
    EXPECT_TRUE(Guide::getIsTrialModeProperty());
    EXPECT_FALSE(alice->getPrivilegesProperty().getAllowOnlineSessionsProperty());
    Guide::setSimulateTrialModeProperty(false);
    EXPECT_TRUE(Guide::getIsTrialModeProperty());
    GamerServicesDispatcher::Update();
    EXPECT_FALSE(Guide::getIsTrialModeProperty());
    EXPECT_TRUE(alice->getPrivilegesProperty().getAllowOnlineSessionsProperty());
}

TEST_F(SystemGuideTest, TheMarketplaceOffersATestPurchaseToASimulatedTrial) {
    auto fake = Fake();
    fake->signIn(0, "Alice", "fixture");
    ASSERT_TRUE(Settle([] { return Count() == 1; }));
    Guide::ShowMarketplace(PlayerIndex::One);
    ASSERT_EQ("content", Ui::currentScreenForTesting());
    EXPECT_EQ("Full version", Ui::labelsForTesting().front());
    EXPECT_EQ("CNA avatars", Ui::labelsForTesting().back());
    Ui::closeAll();

    Guide::setSimulateTrialModeProperty(true);
    GamerServicesDispatcher::Update();
    Guide::ShowMarketplace(PlayerIndex::One);
    ASSERT_EQ("testPurchase", Ui::currentScreenForTesting());
    EXPECT_EQ((std::vector<std::string>{"Yes", "No"}), Ui::labelsForTesting());
    // No leaves the trial as it is.
    Ui::clickForTesting(1);
    ASSERT_EQ("content", Ui::currentScreenForTesting());
    EXPECT_EQ("Trial version", Ui::labelsForTesting().front());
    EXPECT_TRUE(Guide::getSimulateTrialModeProperty());
    // Buying (the trial row) and Yes: the purchase shows at the next update.
    Ui::clickForTesting(0);
    ASSERT_EQ("testPurchase", Ui::currentScreenForTesting());
    Ui::clickForTesting(0);
    EXPECT_FALSE(Guide::getSimulateTrialModeProperty());
    EXPECT_TRUE(Guide::getIsTrialModeProperty());
    GamerServicesDispatcher::Update();
    EXPECT_FALSE(Guide::getIsTrialModeProperty());
    EXPECT_EQ("Full version", Ui::labelsForTesting().front());
}

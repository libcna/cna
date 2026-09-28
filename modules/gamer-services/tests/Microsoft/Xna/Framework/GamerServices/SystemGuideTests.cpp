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
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
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

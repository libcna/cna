// SPDX-License-Identifier: MS-PL
// Offline local profiles: the store, Guide sign-in without a service, and automatic sign-in.
// Sorted after GamerServicesServiceTests.cpp, so IsInitializedDefaultsFalse still runs first.
#include <gtest/gtest.h>
#include "CNA/GamerServices/Configuration.hpp"
#include "CNA/Internal/GamerServices/AvatarDescriptionCodec.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/Internal/GamerServices/LocalProfiles.hpp"
#include "../../../../../src/Internal/Protocol/CnaService/Protocol.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarDescription.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPrivileges.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "System/ArgumentException.hpp"
#include "System/IServiceProvider.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <thread>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace Service = CNA::Internal::GamerServices;
namespace Avatars = CNA::Internal::GamerServices::Avatars;
using namespace Microsoft::Xna::Framework::GamerServices;
using Microsoft::Xna::Framework::PlayerIndex;

namespace {
struct Provider final : System::IServiceProvider {
    void* GetService(const std::type_info&) const override { return nullptr; }
};

// Points the profile store and automatic sign-in at a private directory for one test.
class ProfileEnvironment {
public:
    ProfileEnvironment() {
        root_ = std::filesystem::temp_directory_path() / ("cna-local-profiles-" + std::to_string(getpid()) + "-" + std::to_string(++counter_));
        std::filesystem::remove_all(root_);
        Save("CNA_GAMER_SERVICES_PROFILES_DIR", root_.string());
        Save("CNA_GAMER_SERVICES_AUTO_SIGN_IN", std::nullopt);
    }
    ~ProfileEnvironment() {
        for (const auto& [name, value] : saved_) {
            if (value) setenv(name.c_str(), value->c_str(), 1);
            else unsetenv(name.c_str());
        }
        std::filesystem::remove_all(root_);
    }
    void Set(const char* name, const std::string& value) { setenv(name, value.c_str(), 1); }
    std::filesystem::path Store() const { return root_ / "profiles.json"; }
    void Write(const std::string& text) const {
        std::filesystem::create_directories(root_);
        std::ofstream(Store(), std::ios::binary) << text;
    }
    std::string Read() const {
        std::ifstream input(Store(), std::ios::binary);
        return {std::istreambuf_iterator<char>(input), {}};
    }
private:
    void Save(const char* name, std::optional<std::string> value) {
        const auto* current = std::getenv(name);
        saved_.emplace_back(name, current ? std::optional<std::string>(current) : std::nullopt);
        if (value) setenv(name, value->c_str(), 1);
        else unsetenv(name);
    }
    static inline int counter_ = 0;
    std::filesystem::path root_;
    std::vector<std::pair<std::string, std::optional<std::string>>> saved_;
};

std::string AvatarHex() {
    std::mt19937 random(7);
    const auto bytes = Avatars::encode(Avatars::randomDescriptor(std::nullopt, random));
    std::string text;
    constexpr char digits[] = "0123456789abcdef";
    for (auto byte : bytes) { text += digits[byte >> 4]; text += digits[byte & 15]; }
    return text;
}

void Type(const std::string& value) {
    for (unsigned char character : value) Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextInput(character);
}
void Enter() { Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextInput(u'\r'); }
void Erase(std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextInput(u'\b');
}
}

TEST(LocalProfileStoreTest, NamesFollowGamertagRules) {
    for (const char* valid : {"a", "Player One", "A1b2", "Abcdefghijklmno"})
        EXPECT_TRUE(Service::isValidLocalGamertag(valid)) << valid;
    for (const char* invalid : {"", " Lead", "Trail ", "1Digit", "Two  Spaces", "Abcdefghijklmnop", "Tab\tX",
                                "under_score", "dash-ed", "\xc3\x9cml"})
        EXPECT_FALSE(Service::isValidLocalGamertag(invalid)) << invalid;
}

TEST(LocalProfileStoreTest, OpeningCreatesAStoredProfileAndReusesItIgnoringCase) {
    ProfileEnvironment environment;
    EXPECT_TRUE(Service::loadLocalProfiles().empty());
    const auto created = Service::openLocalProfile("Robin");
    EXPECT_EQ("Robin", created.gamertag);
    EXPECT_FALSE(created.autoSignIn);
    ASSERT_EQ(static_cast<std::size_t>(Avatars::DescriptionSize), created.avatar.size());
    EXPECT_TRUE(Avatars::decode(created.avatar).has_value());
    ASSERT_TRUE(std::filesystem::exists(environment.Store()));

    const auto again = Service::openLocalProfile("rOBIN");
    EXPECT_EQ("Robin", again.gamertag);
    EXPECT_EQ(created.avatar, again.avatar);
    const auto stored = Service::loadLocalProfiles();
    ASSERT_EQ(1u, stored.size());
    EXPECT_EQ(created.avatar, stored[0].avatar);
    EXPECT_EQ(created.avatar, Service::localProfileAvatar("robin"));
    EXPECT_TRUE(Service::localProfileAvatar("Nobody").empty());
    EXPECT_THROW((void)Service::openLocalProfile("9lives"), System::ArgumentException);
}

TEST(LocalProfileStoreTest, AStoreThatCannotBeReadIsNeverOverwritten) {
    ProfileEnvironment environment;
    environment.Write("not a profile store");
    const auto profile = Service::openLocalProfile("Casey");
    EXPECT_EQ("not a profile store", environment.Read());
    EXPECT_TRUE(Service::loadLocalProfiles().empty());
    // The profile still lasts this run, avatar included.
    EXPECT_EQ(profile.avatar, Service::localProfileAvatar("Casey"));
}

TEST(LocalProfileStoreTest, MalformedEntriesAreSkippedAndALostAvatarIsReplaced) {
    ProfileEnvironment environment;
    const auto avatar = AvatarHex();
    environment.Write(R"({"version":1,"profiles":[)"
                      R"({"gamertag":"Dana","autoSignIn":true,"avatar":")" + avatar + R"("},)"
                      R"({"gamertag":"dana","avatar":")" + avatar + R"("},)"
                      R"({"gamertag":"2bad"},{"name":"Eve"},7,)"
                      R"({"gamertag":"Eli","avatar":"00ff"}]})");
    const auto stored = Service::loadLocalProfiles();
    ASSERT_EQ(2u, stored.size());
    EXPECT_EQ("Dana", stored[0].gamertag);
    EXPECT_TRUE(stored[0].autoSignIn);
    EXPECT_EQ(static_cast<std::size_t>(Avatars::DescriptionSize), stored[0].avatar.size());
    EXPECT_EQ("Eli", stored[1].gamertag);
    EXPECT_TRUE(stored[1].avatar.empty());

    const auto repaired = Service::openLocalProfile("Eli");
    EXPECT_EQ(static_cast<std::size_t>(Avatars::DescriptionSize), repaired.avatar.size());
    EXPECT_EQ(repaired.avatar, Service::loadLocalProfiles()[1].avatar);
}

TEST(LocalProfileStoreTest, AutomaticSignInNamesComeFromTheEnvironment) {
    ProfileEnvironment environment;
    environment.Set("CNA_GAMER_SERVICES_AUTO_SIGN_IN", " Ann , Ben Two");
    const auto profiles = Service::autoSignInLocalProfiles();
    ASSERT_EQ(2u, profiles.size());
    EXPECT_EQ("Ann", profiles[0].gamertag);
    EXPECT_EQ("Ben Two", profiles[1].gamertag);
    EXPECT_EQ(2u, Service::loadLocalProfiles().size());
    for (const char* invalid : {"Ann,ann", "A,B,C,D,E", "Ann,,Ben", "Ann,1st"}) {
        environment.Set("CNA_GAMER_SERVICES_AUTO_SIGN_IN", invalid);
        EXPECT_THROW((void)Service::autoSignInLocalProfiles(), CnaService::Error) << invalid;
    }
}

TEST(LocalProfileStoreTest, AutomaticSignInOtherwiseUsesTheStoredFlags) {
    ProfileEnvironment environment;
    std::string entries;
    for (const char* name : {"Aa", "Bb", "Cc", "Dd", "Ee", "Ff"})
        entries += std::string(entries.empty() ? "" : ",") + R"({"gamertag":")" + name + R"(","autoSignIn":)" +
                   (std::string(name) == "Bb" ? "false" : "true") + "}";
    environment.Write(R"({"version":1,"profiles":[)" + entries + "]}");
    const auto profiles = Service::autoSignInLocalProfiles();
    ASSERT_EQ(4u, profiles.size());
    EXPECT_EQ("Aa", profiles[0].gamertag);
    EXPECT_EQ("Cc", profiles[1].gamertag);
    EXPECT_EQ("Ee", profiles[3].gamertag);
}

#if defined(__unix__) || defined(__APPLE__)
TEST(LocalProfileStoreTest, ProcessesCreatingProfilesAtOnceKeepEveryProfile) {
    ProfileEnvironment environment;
    std::vector<pid_t> children;
    for (int child = 0; child < 6; ++child) {
        const pid_t pid = fork();
        ASSERT_GE(pid, 0);
        if (pid == 0) {
            try { (void)Service::openLocalProfile("Child " + std::string(1, static_cast<char>('A' + child))); }
            catch (...) { _exit(1); }
            _exit(0);
        }
        children.push_back(pid);
    }
    for (const pid_t pid : children) {
        int status = 0;
        ASSERT_EQ(pid, waitpid(pid, &status, 0));
        EXPECT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    EXPECT_EQ(6u, Service::loadLocalProfiles().size());
}
#endif

namespace {
class LocalSignInTest : public ::testing::Test {
protected:
    void SetUp() override {
        previous_ = Service::backend();
        CNA::GamerServices::setConfigurationOverride(CNA::GamerServices::Configuration{});
        Service::setBackendForTesting({});
        ASSERT_FALSE(Service::backend()->serviceEnabled());
        if (!GamerServicesDispatcher::getIsInitializedProperty()) GamerServicesDispatcher::Initialize(provider_);
        ASSERT_EQ(0, Gamer::getSignedInGamersProperty()->getCountProperty());
        signedIn_ = 0;
        subscription_ = SignedInGamer::SignedIn.Add([this](System::Object*, const SignedInEventArgs&) { ++signedIn_; });
    }
    void TearDown() override {
        SignedInGamer::SignedIn.Remove(subscription_);
        Guide::ResetPendingMessageBoxForTestingEXT();
        Guide::ResetPendingKeyboardInputForTestingEXT();
        for (auto* gamer : *Gamer::getSignedInGamersProperty())
            Service::backend()->signOut(static_cast<int>(gamer->getPlayerIndexProperty()));
        Settle([] { return Gamer::getSignedInGamersProperty()->getCountProperty() == 0; });
        Service::setBackendForTesting(previous_);
        CNA::GamerServices::setConfigurationOverride(std::nullopt);
    }
    // Identity events from the backend's worker arrive at a later Update.
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
    static SignedInGamer* Player(int index) { return (*Gamer::getSignedInGamersProperty())[index]; }
    static int Count() { return Gamer::getSignedInGamersProperty()->getCountProperty(); }
    ProfileEnvironment environment_;
    Provider provider_;
    std::shared_ptr<Service::IGamerServicesBackend> previous_;
    System::EventHandler<SignedInEventArgs>::Token subscription_ = 0;
    int signedIn_ = 0;
};
}

TEST_F(LocalSignInTest, WithoutAServiceTheGuideCreatesAndSignsInALocalProfile) {
    Guide::ShowSignIn(1, false);
    ASSERT_TRUE(Guide::getHasPendingKeyboardInputEXTProperty());
    EXPECT_TRUE(Guide::getIsVisibleProperty());
    EXPECT_EQ("Sign in", Guide::GetPendingKeyboardInputTitleForTestingEXT());
    EXPECT_EQ("Profile for player 1. Enter a name to create a profile.", Guide::GetPendingKeyboardInputDescriptionForTestingEXT());
    Type(" Robin ");
    Enter();
    ASSERT_TRUE(Settle([] { return Count() == 1; }));
    auto* gamer = Player(0);
    EXPECT_EQ("Robin", gamer->getGamertagProperty());
    EXPECT_EQ(PlayerIndex::One, gamer->getPlayerIndexProperty());
    EXPECT_FALSE(gamer->getIsSignedInToLiveProperty());
    EXPECT_FALSE(gamer->getIsGuestProperty());
    EXPECT_FALSE(gamer->getPrivilegesProperty().getAllowOnlineSessionsProperty());
    EXPECT_FALSE(gamer->getPrivilegesProperty().getAllowPurchaseContentProperty());
    EXPECT_TRUE(gamer->getPrivilegesProperty().getAllowPremiumContentProperty());
    EXPECT_EQ(1, signedIn_);
    EXPECT_FALSE(Guide::getIsVisibleProperty());
    ASSERT_EQ(1u, Service::loadLocalProfiles().size());
    EXPECT_EQ("Robin", Service::loadLocalProfiles()[0].gamertag);
}

TEST_F(LocalSignInTest, StoredProfilesAreOfferedAndTheFirstIsSuggested) {
    (void)Service::openLocalProfile("Robin");
    (void)Service::openLocalProfile("Sam");
    Guide::ShowSignIn(2, false);
    EXPECT_EQ("Profile for player 1: Robin, Sam, or a new name.", Guide::GetPendingKeyboardInputDescriptionForTestingEXT());
    EXPECT_EQ("Robin", Guide::GetPendingKeyboardInputDisplayTextForTestingEXT());
    Enter();
    ASSERT_TRUE(Settle([] { return Count() == 1 && Guide::getHasPendingKeyboardInputEXTProperty(); }));
    // The second pane leaves out the profile already signed in.
    EXPECT_EQ("Profile for player 2: Sam, or a new name.", Guide::GetPendingKeyboardInputDescriptionForTestingEXT());
    Erase(3);
    Type("Taylor");
    Enter();
    ASSERT_TRUE(Settle([] { return Count() == 2; }));
    EXPECT_EQ("Robin", Player(0)->getGamertagProperty());
    EXPECT_EQ("Taylor", Player(1)->getGamertagProperty());
    EXPECT_EQ(PlayerIndex::Two, Player(1)->getPlayerIndexProperty());
    EXPECT_FALSE(Guide::getIsVisibleProperty());
    EXPECT_EQ(3u, Service::loadLocalProfiles().size());
}

TEST_F(LocalSignInTest, AnInvalidOrDuplicateNameEndsSignInWithAMessage) {
    Guide::ShowSignIn(1, false);
    Type("1st");
    Enter();
    EXPECT_TRUE(Guide::getHasPendingMessageBoxEXTProperty());
    Guide::SimulateMessageBoxClickEXT(0);
    EXPECT_FALSE(Guide::getIsVisibleProperty());
    EXPECT_EQ(0, Count());
    EXPECT_TRUE(Service::loadLocalProfiles().empty());

    Guide::ShowSignIn(1, false);
    Type("Robin");
    Enter();
    ASSERT_TRUE(Settle([] { return Count() == 1; }));
    Guide::ShowSignIn(2, false);
    Erase(15);
    Type("rOBIN");
    Enter();
    EXPECT_TRUE(Guide::getHasPendingMessageBoxEXTProperty());
    Guide::SimulateMessageBoxClickEXT(0);
    EXPECT_EQ(1, Count());
}

TEST_F(LocalSignInTest, CancelingStopsWithoutSigningIn) {
    Guide::ShowSignIn(4, false);
    Guide::SimulateKeyboardInputCancelEXT();
    EXPECT_FALSE(Guide::getIsVisibleProperty());
    for (int i = 0; i < 3; ++i) GamerServicesDispatcher::Update();
    EXPECT_EQ(0, Count());
}

TEST_F(LocalSignInTest, AnOnlineOnlySignInNeedsAService) {
    EXPECT_THROW(Guide::ShowSignIn(1, true), GamerServicesNotAvailableException);
    EXPECT_FALSE(Guide::getIsVisibleProperty());
}

TEST_F(LocalSignInTest, ALocalProfilesAvatarIsItsStoredDescription) {
    const auto profile = Service::openLocalProfile("Robin");
    Guide::ShowSignIn(1, false);
    Enter();
    ASSERT_TRUE(Settle([] { return Count() == 1; }));
    int calls = 0;
    auto* result = AvatarDescription::BeginGetFromGamer(Player(0), [&](System::IAsyncResult&) { ++calls; }, std::any{});
    EXPECT_TRUE(result->getCompletedSynchronouslyProperty());
    EXPECT_EQ(1, calls);
    const auto description = AvatarDescription::EndGetFromGamer(result);
    delete result;
    ASSERT_TRUE(description.getIsValidProperty());
    const auto& bytes = description.getDescriptionProperty();
    EXPECT_TRUE(std::equal(bytes.begin(), bytes.end(), profile.avatar.begin(), profile.avatar.end()));
}

// Dispatcher initialization happens once per process, so the test runs itself again, alone, in a
// fresh process to observe startup.
#ifdef __linux__
TEST(LocalAutoSignInTest, ConfiguredProfilesAppearAtTheFirstUpdate) {
    if (std::getenv("CNA_TEST_LOCAL_AUTO_SIGN_IN_CHILD") != nullptr) {
        ASSERT_FALSE(GamerServicesDispatcher::getIsInitializedProperty());
        CNA::GamerServices::setConfigurationOverride(CNA::GamerServices::Configuration{});
        Service::setBackendForTesting({});
        int events = 0;
        (void)SignedInGamer::SignedIn.Add([&](System::Object*, const SignedInEventArgs&) { ++events; });
        Provider provider;
        GamerServicesDispatcher::Initialize(provider);
        // Like XNA, profiles already signed in at startup appear, and raise SignedIn, at Update.
        EXPECT_EQ(0, Gamer::getSignedInGamersProperty()->getCountProperty());
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (Gamer::getSignedInGamersProperty()->getCountProperty() < 2 && std::chrono::steady_clock::now() < deadline)
            GamerServicesDispatcher::Update();
        auto* gamers = Gamer::getSignedInGamersProperty();
        ASSERT_EQ(2, gamers->getCountProperty());
        EXPECT_EQ("Ann", (*gamers)[0]->getGamertagProperty());
        EXPECT_EQ(PlayerIndex::One, (*gamers)[0]->getPlayerIndexProperty());
        EXPECT_EQ("Ben", (*gamers)[1]->getGamertagProperty());
        EXPECT_EQ(PlayerIndex::Two, (*gamers)[1]->getPlayerIndexProperty());
        EXPECT_FALSE((*gamers)[0]->getIsSignedInToLiveProperty());
        EXPECT_EQ(2, events);
        return;
    }
    ProfileEnvironment environment;
    environment.Set("CNA_GAMER_SERVICES_AUTO_SIGN_IN", "Ann,Ben");
    const pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        setenv("CNA_TEST_LOCAL_AUTO_SIGN_IN_CHILD", "1", 1);
        execl("/proc/self/exe", "cna-local-auto-sign-in",
              "--gtest_filter=LocalAutoSignInTest.ConfiguredProfilesAppearAtTheFirstUpdate", static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    ASSERT_EQ(pid, waitpid(pid, &status, 0));
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(0, WEXITSTATUS(status));
    EXPECT_EQ(2u, Service::loadLocalProfiles().size());
}
#endif

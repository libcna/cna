// SPDX-License-Identifier: MS-PL
// The Guide's leaderboard and achievement pages, driven as a player would without a device.
#include <gtest/gtest.h>
#include "../../../../../src/Internal/Guide/GuideUi.hpp"
#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "System/IServiceProvider.hpp"
#include <chrono>
#include <thread>

namespace Service = CNA::Internal::GamerServices;
namespace Avatars = CNA::Internal::GamerServices::Avatars;
namespace Ui = CNA::Internal::GamerServices::GuideUi;
using namespace Microsoft::Xna::Framework::GamerServices;
using Microsoft::Xna::Framework::PlayerIndex;

namespace {
struct Provider final : System::IServiceProvider {
    void* GetService(const std::type_info&) const override { return nullptr; }
};

std::vector<unsigned char> Picture() {
    for (const auto& file : Avatars::embeddedCatalogFiles())
        if (std::string_view(file.name) == "v1/face_features.png") return {file.data, file.data + file.size};
    return {};
}

class GuideLeaderboardTest : public ::testing::Test {
protected:
    void SetUp() override {
        previous_ = Service::backend();
        std::vector<Service::ServiceIdentity> people;
        for (auto [id, tag] : {std::pair{"a", "Alice"}, {"b", "Bob"}, {"c", "Carol"}, {"d", "Dave"}}) {
            Service::ServiceIdentity person;
            person.userId = id;
            person.gamertag = tag;
            person.allowOnlineSessions = true;
            people.push_back(person);
        }
        Service::ServiceLeaderboardFixture best;
        best.key = "BestScore";
        best.entries = {{"a", "Alice", 1200, 0, {}}, {"b", "Bob", 4800, 0, {}}, {"c", "Carol", 2500, 0, {}}, {"d", "Dave", 900, 0, {}}};
        Service::ServiceLeaderboardFixture laps;
        laps.key = "FastestLap";
        laps.mode = 1;
        laps.ascending = true;
        laps.entries = {{"b", "Bob", 61000, 0, {}}, {"a", "Alice", 59000, 0, {}}};
        const auto picture = Picture();
        pictureHash_ = Avatars::sha256Hex(std::span<const std::uint8_t>(picture.data(), picture.size()));
        Service::ServiceAchievement first;
        first.key = "first";
        first.name = "First Steps";
        first.description = "Finish the first level.";
        first.score = 10;
        first.picture = pictureHash_;
        Service::ServiceAchievement secret;
        secret.key = "secret";
        secret.name = "Hidden Path";
        secret.description = "Find the hidden path.";
        secret.score = 50;
        secret.displayBeforeEarned = false;
        service_ = Service::makeFakeBackend(people, {first, secret}, {best, laps});
        Service::setFakeAvatarCatalog(*service_, "", {{pictureHash_, picture}});
        Service::setBackendForTesting(service_);
        if (!GamerServicesDispatcher::getIsInitializedProperty()) GamerServicesDispatcher::Initialize(provider_);
        service_->signIn(0, "Alice", "fixture");
        Settle([] { return Gamer::getSignedInGamersProperty()->getCountProperty() == 1; });
        Service::setFakeRemotePresence(*service_, "c", true);
        service_->changeFriend("a", "Carol", "add");
        service_->changeFriend("c", "Alice", "accept");
    }
    void TearDown() override {
        Ui::closeAll();
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
    static bool Labels(std::vector<std::string> expected) {
        return Settle([&] { return Ui::labelsForTesting() == expected; });
    }
    Provider provider_;
    std::shared_ptr<Service::IGamerServicesBackend> previous_, service_;
    std::string pictureHash_;
};
}

TEST_F(GuideLeaderboardTest, TheRailReachesTheTitlesBoards) {
    Ui::open(Ui::achievementsScreen(PlayerIndex::One), PlayerIndex::One);
    Ui::sendForTesting(Ui::Command::Next);
    ASSERT_EQ("leaderboards", Ui::currentScreenForTesting());
    EXPECT_TRUE(Labels({"Best Score - 4", "Fastest Lap \xe2\x80\xa2 Mode 1 - 2"}));
}

TEST_F(GuideLeaderboardTest, ABoardShowsTheTopPlayersThoseAroundYouAndYourFriends) {
    Ui::open(Ui::leaderboardsScreen(PlayerIndex::One), PlayerIndex::One);
    ASSERT_TRUE(Settle([] { return Ui::labelsForTesting().size() == 2; }));
    Ui::clickForTesting(0);
    ASSERT_EQ("leaderboard", Ui::currentScreenForTesting());
    EXPECT_TRUE(Labels({"#1 Bob 4800", "#2 Carol 2500", "#3 Alice 1200", "#4 Dave 900"}));
    Ui::sendForTesting(Ui::Command::X);
    EXPECT_TRUE(Labels({"#1 Bob 4800", "#2 Carol 2500", "#3 Alice 1200", "#4 Dave 900"}));
    // Around you starts on your own row.
    EXPECT_EQ(2, Ui::focusForTesting());
    Ui::sendForTesting(Ui::Command::X);
    EXPECT_TRUE(Labels({"#2 Carol 2500", "#3 Alice 1200"}));
    // A row opens that player's gamer card.
    Ui::clickForTesting(0);
    EXPECT_EQ("gamerCard", Ui::currentScreenForTesting());
    Ui::sendForTesting(Ui::Command::Back);
    Ui::sendForTesting(Ui::Command::Back);
    EXPECT_EQ("leaderboards", Ui::currentScreenForTesting());
    // Lowest first on an ascending board.
    Ui::clickForTesting(1);
    EXPECT_TRUE(Labels({"#1 Alice 59000", "#2 Bob 61000"}));
}

TEST_F(GuideLeaderboardTest, AnAchievementOpensWithItsPictureAndWhenItWasEarned) {
    (void)service_->award("a", "first");
    Ui::open(Ui::achievementsScreen(PlayerIndex::One), PlayerIndex::One);
    ASSERT_TRUE(Labels({"[x] First Steps", "[ ] Hidden Path"}));
    Ui::clickForTesting(0);
    ASSERT_EQ("achievement", Ui::currentScreenForTesting());
    const auto labels = Ui::labelsForTesting();
    ASSERT_EQ(2u, labels.size());
    EXPECT_EQ("First Steps", labels[0]);
    EXPECT_TRUE(labels[1].starts_with("Unlocked ")) << labels[1];
    // The picture is read from the service once.
    EXPECT_TRUE(Settle([&] { return Service::fakeAvatarTraffic(*service_).fileDownloads == 1; }));
    Ui::sendForTesting(Ui::Command::Accept);
    EXPECT_EQ("achievements", Ui::currentScreenForTesting());
    // A secret one stays secret until it is earned.
    Ui::clickForTesting(1);
    EXPECT_EQ((std::vector<std::string>{"Secret achievement", "Locked"}), Ui::labelsForTesting());
}

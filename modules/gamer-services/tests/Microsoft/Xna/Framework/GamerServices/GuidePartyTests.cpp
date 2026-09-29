// SPDX-License-Identifier: MS-PL
// Parties: PartySize from the party service, the Guide's party pages, and joining a friend's or
// party member's game; driven as a player would, without a device.
#include <gtest/gtest.h>
#include "../../../../../src/Internal/GuideOverlay.hpp"
#include "../../../../../src/Internal/Guide/GuideUi.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/Internal/GamerServices/ServiceInvitations.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
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

class GuidePartyTest : public ::testing::Test {
protected:
    void SetUp() override {
        previous_ = Service::backend();
        std::vector<Service::ServiceIdentity> people;
        for (auto [id, tag] : {std::pair{"a", "Alice"}, {"b", "Bob"}, {"c", "Carol"}}) {
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
        for (const char* id : {"b", "c"}) Service::setFakeRemotePresence(*service_, id, true);
        for (const char* tag : {"Bob", "Carol"}) service_->changeFriend("a", tag, "add");
        service_->changeFriend("b", "Alice", "accept");
        service_->changeFriend("c", "Alice", "accept");
    }
    void TearDown() override {
        Ui::closeAll();
        Service::resetInvitationsForTesting();
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
    static SignedInGamer* Alice() { return (*Gamer::getSignedInGamersProperty())[PlayerIndex::One]; }
    static bool Labels(const std::vector<std::string>& expected) {
        return Settle([&] { return Ui::labelsForTesting() == expected; });
    }
    Provider provider_;
    std::shared_ptr<Service::IGamerServicesBackend> previous_, service_;
};
}

TEST_F(GuidePartyTest, PartySizeFollowsThePartyService) {
    EXPECT_EQ(0, Alice()->getPartySizeProperty());
    const auto party = service_->changeParty("a", "invite", "Bob");
    (void)service_->changeParty("b", "accept", party.id);
    Service::pollPartiesNowForTesting();
    EXPECT_TRUE(Settle([] { return Alice()->getPartySizeProperty() == 2; }));
    (void)service_->changeParty("b", "leave", "");
    Service::pollPartiesNowForTesting();
    EXPECT_TRUE(Settle([] { return Alice()->getPartySizeProperty() == 1; }));
}

TEST_F(GuidePartyTest, ThePartyPageInvitesFriendsAndLeaves) {
    Guide::ShowParty(PlayerIndex::One);
    ASSERT_EQ("party", Ui::currentScreenForTesting());
    // No party yet: inviting starts one.
    ASSERT_TRUE(Labels({"Invite friends"}));
    Ui::clickForTesting(0);
    ASSERT_EQ("partyInvite", Ui::currentScreenForTesting());
    ASSERT_TRUE(Labels({"Bob", "Carol"}));
    Ui::clickForTesting(0);
    Ui::sendForTesting(Ui::Command::Back);
    ASSERT_EQ("party", Ui::currentScreenForTesting());
    ASSERT_TRUE(Labels({"Alice (leader)", "Invite friends", "Leave party"}));
    EXPECT_EQ(1, Alice()->getPartySizeProperty());
    // Bob joins from his side; the page and PartySize catch up.
    (void)service_->changeParty("b", "accept", service_->party("a").id);
    Service::pollPartiesNowForTesting();
    ASSERT_TRUE(Settle([] { return Alice()->getPartySizeProperty() == 2; }));
    Ui::closeAll();
    Guide::ShowParty(PlayerIndex::One);
    ASSERT_TRUE(Labels({"Alice (leader)", "Bob", "Invite friends", "Leave party"}));
    Ui::clickForTesting(3);
    ASSERT_TRUE(Labels({"Invite friends"}));
    EXPECT_EQ(0, Alice()->getPartySizeProperty());
    EXPECT_EQ(1u, service_->party("b").members.size());
}

TEST_F(GuidePartyTest, APartyInvitationIsAnsweredOnThePartyPage) {
    (void)service_->changeParty("c", "invite", "Alice");
    Service::pollPartiesNowForTesting();
    ASSERT_TRUE(Settle([] { return Service::knownParty("a") && !Service::knownParty("a")->invitations.empty(); }));
    Guide::ShowParty(PlayerIndex::One);
    ASSERT_TRUE(Labels({"Invitation from Carol", "Invite friends"}));
    Ui::clickForTesting(0);
    ASSERT_TRUE(Labels({"Carol (leader)", "Alice", "Invite friends", "Leave party"}));
    EXPECT_EQ(2, Alice()->getPartySizeProperty());
    // Declining another one (X) drops it.
    (void)service_->changeParty("b", "invite", "Alice");
    Ui::closeAll();
    Guide::ShowParty(PlayerIndex::One);
    ASSERT_TRUE(Labels({"Invitation from Bob", "Carol (leader)", "Alice", "Invite friends", "Leave party"}));
    Ui::sendForTesting(Ui::Command::X);
    EXPECT_TRUE(Labels({"Carol (leader)", "Alice", "Invite friends", "Leave party"}));
}

TEST_F(GuidePartyTest, PartySessionsListJoinableMembersAndWithoutAPartyShowFriends) {
    Service::pollPartiesNowForTesting();
    ASSERT_TRUE(Settle([] { return Service::knownParty("a").has_value(); }));
    Guide::ShowPartySessions(PlayerIndex::One);
    EXPECT_EQ("friends", Ui::currentScreenForTesting());
    Ui::closeAll();

    const auto party = service_->changeParty("a", "invite", "Bob");
    (void)service_->changeParty("b", "accept", party.id);
    Service::pollPartiesNowForTesting();
    ASSERT_TRUE(Settle([] { return Alice()->getPartySizeProperty() == 2; }));
    Service::ServiceSessionSettings settings;
    settings.maxGamers = 4;
    (void)service_->sessionDirectory().create("b", {"b"}, Service::ServiceSessionKind::PlayerMatch, settings);
    Service::setFakeJoinable(*service_, "b", true);
    Guide::ShowPartySessions(PlayerIndex::One);
    ASSERT_EQ("partySessions", Ui::currentScreenForTesting());
    ASSERT_TRUE(Labels({"Bob"}));
    // Joining: Bob's game grants the request, which is accepted as an invitation for JoinInvited.
    Service::acceptedInvitation().reset();
    Ui::clickForTesting(0);
    EXPECT_FALSE(Ui::visible());
    ASSERT_TRUE(Settle([] { return Service::acceptedInvitation().has_value(); }));
    EXPECT_EQ("Bob", Service::acceptedInvitation()->invitation.senderGamertag);
    // A join request never lands in the inbox.
    EXPECT_TRUE(service_->sessionDirectory().listInvites("a", 0, 8).invites.empty());
}

TEST_F(GuidePartyTest, AJoinableFriendsCardOffersJoinGameAndAPartyInvitation) {
    Service::setFakeJoinable(*service_, "b", true);
    Guide::ShowFriends(PlayerIndex::One);
    ASSERT_TRUE(Settle([] { return Ui::labelsForTesting().size() == 2; }));
    std::unique_ptr<Gamer> bob(Gamer::GetFromGamertag("Bob"));
    Ui::closeAll();
    Guide::ShowGamerCard(PlayerIndex::One, bob.get());
    // The card's labels are the gamertag, then its actions: joining comes first.
    ASSERT_TRUE(Settle([] {
        const auto labels = Ui::labelsForTesting();
        return labels.size() > 1 && labels[1] == "Join game";
    }));
    const auto labels = Ui::labelsForTesting();
    const auto invite = std::find(labels.begin(), labels.end(), "Invite to party");
    ASSERT_NE(invite, labels.end());
    Ui::clickForTesting(static_cast<int>(invite - labels.begin()) - 1);
    EXPECT_TRUE(Settle([&] { return service_->party("b").invitations.size() == 1; }));
}

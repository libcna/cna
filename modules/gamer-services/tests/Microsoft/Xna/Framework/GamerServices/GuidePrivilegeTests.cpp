// SPDX-License-Identifier: MS-PL
// GSH-03: an account's service policy becomes its XNA GamerPrivileges, and a Blocked privilege
// refuses the Guide calls XNA refuses with GamerPrivilegeException. Named Guide* so it sorts after
// GamerServicesDispatcherTest.IsInitializedDefaultsFalse.
#include <gtest/gtest.h>
#include "../../../../../src/Internal/Guide/GuideUi.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/Internal/GamerServices/VoiceMutes.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPrivilegeException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "System/IServiceProvider.hpp"
#include <chrono>
#include <memory>
#include "Microsoft/Xna/Framework/GamerServices/GuideAlreadyVisibleException.hpp"
#include <thread>

namespace Service = CNA::Internal::GamerServices;
namespace Ui = CNA::Internal::GamerServices::GuideUi;
using namespace Microsoft::Xna::Framework::GamerServices;
using Microsoft::Xna::Framework::PlayerIndex;

namespace {
struct Provider final : System::IServiceProvider {
    void* GetService(const std::type_info&) const override { return nullptr; }
};
class GuidePrivilegeTest : public ::testing::Test {
protected:
    void SetUp() override {
        previous_ = Service::backend();
        Service::resetVoiceMutesForTesting();
        std::vector<Service::ServiceIdentity> people;
        for (auto [id, tag] : {std::pair{"k", "Kid"}, {"b", "Bob"}, {"p", "Pal"}}) {
            Service::ServiceIdentity person;
            person.userId = id;
            person.gamertag = tag;
            person.allowOnlineSessions = true;
            people.push_back(person);
        }
        // The operator's policy for Kid: no communication, no profiles, friends' content only, no
        // trading, purchases or premium content.
        auto& kid = people[0];
        kid.communication = "blocked";
        kid.profileViewing = "blocked";
        kid.userContent = "friends";
        kid.tradeContent = kid.purchaseContent = kid.premiumContent = false;
        service_ = Service::makeFakeBackend(std::move(people));
        Service::setBackendForTesting(service_);
        if (!GamerServicesDispatcher::getIsInitializedProperty()) GamerServicesDispatcher::Initialize(provider_);
    }
    void TearDown() override {
        Ui::closeAll();
        for (int slot = 0; slot < 2; ++slot) service_->signOut(slot);
        Settle([] { return Gamer::getSignedInGamersProperty()->getCountProperty() == 0; });
        Service::setBackendForTesting(previous_);
        Service::resetVoiceMutesForTesting();
    }
    template <class Condition>
    static bool Settle(Condition condition) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!condition()) {
            if (std::chrono::steady_clock::now() > deadline) return false;
            GamerServicesDispatcher::Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }
    SignedInGamer* SignIn(int slot, const char* tag) {
        service_->signIn(slot, tag, "fixture");
        EXPECT_TRUE(Settle([&] { return Gamer::getSignedInGamersProperty()->getCountProperty() == slot + 1; }));
        return (*Gamer::getSignedInGamersProperty())[slot];
    }
    Provider provider_;
    std::shared_ptr<Service::IGamerServicesBackend> previous_, service_;
};
}

TEST_F(GuidePrivilegeTest, TheAccountsPolicyBecomesItsGamerPrivileges) {
    const auto privileges = SignIn(0, "Kid")->getPrivilegesProperty();
    EXPECT_EQ(GamerPrivilegeSetting::Blocked, privileges.getAllowCommunicationProperty());
    EXPECT_EQ(GamerPrivilegeSetting::Blocked, privileges.getAllowProfileViewingProperty());
    EXPECT_EQ(GamerPrivilegeSetting::FriendsOnly, privileges.getAllowUserCreatedContentProperty());
    EXPECT_FALSE(privileges.getAllowTradeContentProperty());
    EXPECT_FALSE(privileges.getAllowPurchaseContentProperty());
    EXPECT_FALSE(privileges.getAllowPremiumContentProperty());
    EXPECT_TRUE(privileges.getAllowOnlineSessionsProperty());
    const auto pal = SignIn(1, "Pal")->getPrivilegesProperty();
    EXPECT_EQ(GamerPrivilegeSetting::Everyone, pal.getAllowCommunicationProperty());
    EXPECT_TRUE(pal.getAllowPurchaseContentProperty());
}

TEST_F(GuidePrivilegeTest, BlockedPrivilegesRefuseTheGuideCallsXnaRefuses) {
    SignedInGamer* kid = SignIn(0, "Kid");
    std::unique_ptr<Gamer> bob(Gamer::GetFromGamertag("Bob"));
    EXPECT_THROW(Guide::ShowComposeMessage(PlayerIndex::One, "hi", {bob.get()}), GamerPrivilegeException);
    EXPECT_THROW(Guide::ShowGameInvite(PlayerIndex::One, {bob.get()}), GamerPrivilegeException);
    EXPECT_THROW(Guide::ShowGamerCard(PlayerIndex::One, bob.get()), GamerPrivilegeException);
    EXPECT_THROW(Guide::ShowMarketplace(PlayerIndex::One), GamerPrivilegeException);
    EXPECT_FALSE(Guide::getIsVisibleProperty());
    // The profile's own card stays open to it.
    Guide::ShowGamerCard(PlayerIndex::One, kid);
    EXPECT_TRUE(Guide::getIsVisibleProperty());
    Ui::closeAll();
    // Another profile with every privilege is not affected.
    SignIn(1, "Pal");
    Guide::ShowGamerCard(PlayerIndex::Two, bob.get());
    EXPECT_TRUE(Guide::getIsVisibleProperty());
}

TEST_F(GuidePrivilegeTest, TheBlockListReadAtSignInMutesVoice) {
    // Blocked earlier, from another console.
    Service::setFakeRemotePresence(*service_, "p", true);
    service_->setBlocked("p", "Bob", true);
    Service::setFakeRemotePresence(*service_, "p", false);
    SignIn(0, "Pal");
    EXPECT_TRUE(Service::playerBlocked("Pal", "Bob"));
    EXPECT_TRUE(Service::voiceMuted("Pal", "Bob"));
    EXPECT_FALSE(Service::voiceMuted("Pal", "Kid"));
}

// GSH-04: FriendGamer.HasVoice is what the friend's client reports, and only while it is online.
TEST_F(GuidePrivilegeTest, AFriendHasVoiceWhileOnlineWithAClientThatCanTalk) {
    SignedInGamer* pal = SignIn(0, "Pal");
    Service::setFakeRemotePresence(*service_, "b", true);
    service_->changeFriend("p", "Bob", "add");
    service_->changeFriend("b", "Pal", "accept");
    auto bobHasVoice = [&] {
        auto friends = pal->GetFriends();
        for (int i = 0; i < friends.getCountProperty(); ++i)
            if (friends[i]->getGamertagProperty() == "Bob") return friends[i]->getHasVoiceProperty();
        ADD_FAILURE() << "Bob is not a friend";
        return false;
    };
    EXPECT_FALSE(bobHasVoice());
    Service::setFakeVoice(*service_, "b", true);
    EXPECT_TRUE(bobHasVoice());
    Service::setFakeRemotePresence(*service_, "b", false);
    EXPECT_FALSE(bobHasVoice());
}


TEST_F(GuidePrivilegeTest, MarketplaceDiagnosticsDistinguishMissingProfileAndPurchasePrivilege) {
    // Guide.ShowMarketplace IL_0000..0039 and SignedInGamerCollection.get_Item(PlayerIndex)
    // IL_0000..0027: an undefined/unoccupied player is a missing profile, not a range error.
    for (PlayerIndex player : {PlayerIndex::One, static_cast<PlayerIndex>(99)}) {
        try { Guide::ShowMarketplace(player); FAIL() << "Expected missing-profile refusal"; }
        catch (const GamerPrivilegeException& error) {
            EXPECT_EQ("A signed in gamer profile is required to perform this operation. A LIVE profile may also be required. There are no profiles currently signed in, or the profile is not signed in to LIVE.", error.getMessageProperty());
        }
    }
    (void)SignIn(0, "Kid");
    try { Guide::ShowMarketplace(PlayerIndex::One); FAIL() << "Expected purchase refusal"; }
    catch (const GamerPrivilegeException& error) {
        EXPECT_EQ("The current gamer profile does not have suitable privileges to perform this operation. You may require a LIVE Gold account, or need to change your parental control settings.", error.getMessageProperty());
    }
    EXPECT_FALSE(Guide::getIsVisibleProperty());
    (void)SignIn(1, "Pal");
    std::unique_ptr<System::IAsyncResult> pending(Guide::BeginShowMessageBox(
        "T", "B", {"OK"}, 0, MessageBoxIcon::None, {}, {}));
    struct ResetPending { ~ResetPending() { Guide::ResetPendingMessageBoxForTestingEXT(); } } reset;
    try { Guide::ShowMarketplace(PlayerIndex::One); FAIL() << "Privilege must precede visibility"; }
    catch (const GamerPrivilegeException& error) {
        EXPECT_EQ("The current gamer profile does not have suitable privileges to perform this operation. You may require a LIVE Gold account, or need to change your parental control settings.", error.getMessageProperty());
    }
    try { Guide::ShowMarketplace(PlayerIndex::Two); FAIL() << "Expected visible Guide refusal"; }
    catch (const GuideAlreadyVisibleException& error) {
        // ErrorHandler.GetExceptionFromResult IL_0105..010f.
        EXPECT_EQ("The function cannot be completed at this time: the Guide UI is already active. Wait until Guide.IsVisible is false before issuing this call.", error.getMessageProperty());
    }
    EXPECT_FALSE(pending->getIsCompletedProperty());
    Guide::SimulateMessageBoxClickEXT(0);
    EXPECT_EQ(std::optional<int>(0), Guide::EndShowMessageBox(pending.get()));
}

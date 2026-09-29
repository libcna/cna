// SPDX-License-Identifier: MS-PL
#ifndef __EMSCRIPTEN__
#include "OnlineSessionTestFixture.hpp"
#include "../../../../../gamer-services/src/Internal/Guide/GuideUi.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GuideAlreadyVisibleException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/InviteAcceptedEventArgs.hpp"
#include "System/ArgumentException.hpp"
#include "System/InvalidOperationException.hpp"

namespace {
using namespace OnlineSessionTesting;
using Microsoft::Xna::Framework::PlayerIndex;
using Microsoft::Xna::Framework::Net::NetworkSessionType;
namespace Ui=CNA::Internal::GamerServices::GuideUi;
class OnlineInvitationTest : public OnlineSessionTest {
protected:
    void TearDown() override {
        Ui::closeAll();
        for(auto token:tokens)NetworkSession::InviteAccepted.Remove(token);
        joining.reset();OnlineSessionTest::TearDown();
    }
    // The host lives in a private engine so this process can be the invited public client.
    Service::ServiceInvitation invite(const std::string& recipient="Bob") {
        privatePeer(false);return service->sessionDirectory().sendInvite("a",peer->snapshot().session,recipient);
    }
    // The invitation card: Accept, Decline, View gamer card.
    void prompted() {
        Service::pollInvitationsNowForTesting();
        until([&]{return Ui::currentScreenForTesting()=="invitation";});
    }
    void subscribe(std::function<void(const InviteAcceptedEventArgs&)> handler) {
        tokens.push_back(NetworkSession::InviteAccepted.Add([handler=std::move(handler)](auto*,const InviteAcceptedEventArgs& args){handler(args);}));
    }
    Service::ServiceInvitationState stateOf(const std::string& user,const std::string& id) {
        return service->sessionDirectory().getInvite(user,id).state;
    }
    std::vector<System::EventHandler<InviteAcceptedEventArgs>::Token> tokens;
    std::unique_ptr<System::IAsyncResult> joining;
};
}

TEST_F(OnlineInvitationTest, GuideAcceptanceRaisesInviteAcceptedAndTheInvitedJoinCompletes) {
    const auto sent=invite();int raised=0;SignedInGamer* who=nullptr;bool current=true;
    // SAMPLE-096 shape: the handler joins immediately, without another confirmation.
    subscribe([&](const InviteAcceptedEventArgs& args) {
        ++raised;who=args.getGamerProperty();current=args.getIsCurrentSessionProperty();
        joining.reset(NetworkSession::BeginJoinInvited(std::vector<SignedInGamer*>{gamer(3),gamer(1)},{}, {}));
    });
    prompted();EXPECT_EQ(Service::ServiceInvitationState::Pending,stateOf("b",sent.invite));
    Ui::clickForTesting(0);
    until([&]{return raised==1&&joining&&joining->getIsCompletedProperty();});
    EXPECT_EQ(gamer(1),who);EXPECT_FALSE(current);EXPECT_FALSE(joining->getCompletedSynchronouslyProperty());
    EXPECT_THROW((void)NetworkSession::EndJoin(joining.get()),System::ArgumentException);
    session=NetworkSession::EndJoinInvited(joining.get());
    ASSERT_NE(nullptr,session);EXPECT_FALSE(session->getIsHostProperty());
    EXPECT_EQ(NetworkSessionType::PlayerMatch,session->getSessionTypeProperty());
    ASSERT_EQ(2,session->getLocalGamersProperty().getCountProperty());
    // The invitee owns the join, so it comes first whatever order the caller used.
    EXPECT_EQ("Bob",session->getLocalGamersProperty()[0]->getGamertagProperty());
    EXPECT_EQ(4,session->getAllGamersProperty().getCountProperty());
    EXPECT_EQ("Alice",session->getHostProperty()->getGamertagProperty());
    EXPECT_EQ(Service::ServiceInvitationState::Used,stateOf("b",sent.invite));
    EXPECT_FALSE(Service::acceptedInvitation().has_value());
    for(int index=0;index<5;++index)tick();EXPECT_EQ(1,raised);
}

TEST_F(OnlineInvitationTest, AnAcceptanceWithoutSubscribersIsDeliveredOnceToTheFirstSubscriber) {
    invite();prompted();Ui::clickForTesting(0);
    until([&]{return Service::acceptedInvitation().has_value();});
    for(int index=0;index<3;++index)tick();
    int first=0,second=0;SignedInGamer* who=nullptr;
    subscribe([&](const InviteAcceptedEventArgs& args){++first;who=args.getGamerProperty();});
    EXPECT_EQ(1,first);EXPECT_EQ(gamer(1),who);
    subscribe([&](const InviteAcceptedEventArgs&){++second;});
    for(int index=0;index<3;++index)tick();
    EXPECT_EQ(1,first);EXPECT_EQ(0,second);
    // The accepted invitation remains joinable until JoinInvited consumes it.
    joining.reset(NetworkSession::BeginJoinInvited(1,{}, {}));
    until([&]{return joining->getIsCompletedProperty();});
    session=NetworkSession::EndJoinInvited(joining.get());
    ASSERT_EQ(1,session->getLocalGamersProperty().getCountProperty());
    EXPECT_EQ("Bob",session->getLocalGamersProperty()[0]->getGamertagProperty());
}

TEST_F(OnlineInvitationTest, DecliningDismissesTheInvitationWithoutInviteAccepted) {
    const auto sent=invite();int raised=0;subscribe([&](const InviteAcceptedEventArgs&){++raised;});
    prompted();Ui::clickForTesting(1);
    until([&]{return stateOf("b",sent.invite)==Service::ServiceInvitationState::Dismissed;});
    Service::pollInvitationsNowForTesting();for(int index=0;index<10;++index)tick();
    EXPECT_EQ(0,raised);EXPECT_FALSE(Guide::getIsVisibleProperty());EXPECT_FALSE(Service::acceptedInvitation().has_value());
    EXPECT_THROW((void)NetworkSession::BeginJoinInvited(1,{}, {}),System::InvalidOperationException);
}

TEST_F(OnlineInvitationTest, JoinInvitedValidatesTheAcceptedInviteeBeforeAnyServiceWork) {
    EXPECT_THROW((void)NetworkSession::BeginJoinInvited(1,{}, {}),System::InvalidOperationException);
    EXPECT_THROW((void)NetworkSession::BeginJoinInvited(std::vector<SignedInGamer*>{},{}, {}),System::ArgumentException);
    EXPECT_THROW((void)NetworkSession::BeginJoinInvited(std::vector<SignedInGamer*>{nullptr},{}, {}),System::ArgumentException);
    invite();subscribe([](const InviteAcceptedEventArgs&){});
    prompted();Ui::clickForTesting(0);
    until([&]{return Service::acceptedInvitation().has_value();});
    EXPECT_THROW((void)NetworkSession::BeginJoinInvited(std::vector<SignedInGamer*>{gamer(3)},{}, {}),System::InvalidOperationException);
    EXPECT_EQ(0,NetworkSession::GetActiveActionInstanceCountForTesting());
    EXPECT_TRUE(Service::acceptedInvitation().has_value());
}

TEST_F(OnlineInvitationTest, ShowGameInviteAndTheGamerCardInviteToTheActiveOnlineSession) {
    EXPECT_THROW(Guide::ShowGameInvite(PlayerIndex::One,std::vector<Gamer*>{gamer(1)}),System::InvalidOperationException);
    EXPECT_THROW(Guide::ShowGameInvite(PlayerIndex::One,std::vector<Gamer*>(101,gamer(1))),System::ArgumentException);
    session=NetworkSession::Create(NetworkSessionType::PlayerMatch,std::vector<SignedInGamer*>{gamer(0),gamer(2)},6,0,{});
    Guide::ShowGameInvite(PlayerIndex::One,std::vector<Gamer*>{gamer(1)});
    ASSERT_EQ("invite",Ui::currentScreenForTesting());
    EXPECT_THROW(Guide::ShowGameInvite(PlayerIndex::One,std::vector<Gamer*>{gamer(1)}),GuideAlreadyVisibleException);
    // The recipients arrive chosen; Y sends.
    Ui::sendForTesting(Ui::Command::Y);
    EXPECT_FALSE(Guide::getIsVisibleProperty());
    until([&]{return service->sessionDirectory().listInvites("b",0,32).invites.size()==1;});
    const auto sent=service->sessionDirectory().listInvites("b",0,32).invites.front();
    EXPECT_EQ("Alice",sent.senderGamertag);EXPECT_EQ(Service::ServiceInvitationState::Pending,sent.state);
    // Bob is signed in here too: his invitation prompt appears; closing it leaves it pending.
    prompted();Ui::clickForTesting(1);
    until([&]{return stateOf("b",sent.invite)==Service::ServiceInvitationState::Dismissed;});
    // Gamer card for Dana offers "Invite to game" after the friendship action while in a session.
    Guide::ShowGamerCard(PlayerIndex::One,gamer(3));ASSERT_EQ("gamerCard",Ui::currentScreenForTesting());
    until([&]{return Ui::labelsForTesting().size()>2;});
    EXPECT_EQ("Invite to game",Ui::labelsForTesting().at(2));
    Ui::clickForTesting(1);
    until([&]{return service->sessionDirectory().listInvites("d",0,32).invites.size()==1;});
    // The card stays up (a notification confirms the invitation); Back closes the Guide.
    Ui::sendForTesting(Ui::Command::Back);
    EXPECT_FALSE(Guide::getIsVisibleProperty());
    session->Dispose();
    EXPECT_THROW(Guide::ShowGameInvite(PlayerIndex::One,std::vector<Gamer*>{gamer(1)}),System::InvalidOperationException);
}
#endif

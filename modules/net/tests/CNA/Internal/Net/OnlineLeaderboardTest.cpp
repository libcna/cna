// SPDX-License-Identifier: MS-PL
#ifndef __EMSCRIPTEN__
#include "OnlineSessionTestFixture.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardIdentity.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardWriter.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardEntry.hpp"
#include "Microsoft/Xna/Framework/Net/WriteLeaderboardsEventArgs.hpp"

namespace {
using namespace OnlineSessionTesting;
using Microsoft::Xna::Framework::Net::GameEndedEventArgs;
using Microsoft::Xna::Framework::Net::GameStartedEventArgs;
using Microsoft::Xna::Framework::Net::NetworkSessionEndedEventArgs;
using Microsoft::Xna::Framework::Net::NetworkSessionState;
using Microsoft::Xna::Framework::Net::NetworkSessionType;
using Microsoft::Xna::Framework::Net::WriteLeaderboardsEventArgs;
using OnlineLeaderboardTest=OnlineSessionTest;
LeaderboardIdentity board(){return LeaderboardIdentity::Create(LeaderboardKey::BestScoreLifeTime);}
void write(Microsoft::Xna::Framework::Net::NetworkGamer* gamer,long long value) {
    gamer->getLeaderboardWriterProperty().GetLeaderboard(board())->setRatingProperty(value);
}
}

TEST_F(OnlineLeaderboardTest, HostMachineWritesItsLocalGamersAtEndGameAndPublishesAfterItsTransition) {
    session=NetworkSession::Create(NetworkSessionType::PlayerMatch,std::vector<SignedInGamer*>{gamer(0),gamer(2)},6,0,{});
    privatePeer(true,sessionId("b"));until([&]{return peer->ready()&&session->getAllGamersProperty().getCountProperty()==4;});
    auto* alice=session->getLocalGamersProperty()[0];auto* charlie=session->getLocalGamersProperty()[1];
    NetworkSessionState seen=NetworkSessionState::Ended;int finals=0;std::string order;
    session->GameStarted+=[&](auto*,const GameStartedEventArgs&){seen=session->getSessionStateProperty();order+='S';};
    session->WriteUnarbitratedLeaderboard+=[&](auto*,const WriteLeaderboardsEventArgs& args) {
        ++finals;order+='W';EXPECT_FALSE(args.getIsLeavingProperty());
        if(args.getGamerProperty()==charlie)write(charlie,300);
    };
    session->GameEnded+=[&](auto*,const GameEndedEventArgs&){order+='E';};
    session->StartGame();
    // Nothing is published to the directory before the host's own transition.
    EXPECT_EQ(Service::ServiceSessionState::Lobby,service->sessionDirectory().get("a",peer->snapshot().session).state);
    session->Update();EXPECT_EQ(NetworkSessionState::Playing,seen);
    write(alice,500);EXPECT_EQ(-1,rating("Alice"));
    until([&]{return peer->snapshot().state==Service::ServiceSessionState::Playing;});
    session->EndGame();session->Update();
    EXPECT_EQ("SWWE",order);EXPECT_EQ(2,finals);
    EXPECT_EQ(500,rating("Alice"));EXPECT_EQ(300,rating("Charlie"));EXPECT_EQ(-1,rating("Bob"));
    until([&]{return peer->snapshot().state==Service::ServiceSessionState::Lobby;});
}

TEST_F(OnlineLeaderboardTest, ClientMachineCommitsItsOwnGamersWhenTheHostEndsTheGame) {
    privatePeer(false);
    auto found=NetworkSession::Find(NetworkSessionType::PlayerMatch,std::vector<SignedInGamer*>{gamer(1),gamer(3)},{});
    ASSERT_EQ(1,found.getCountProperty());
    std::unique_ptr<System::IAsyncResult> result(NetworkSession::BeginJoin(&std::as_const(found)[0],{}, {}));
    until([&]{return result->getIsCompletedProperty();});session=NetworkSession::EndJoin(result.get());
    auto* bob=session->getLocalGamersProperty()[0];auto* dana=session->getLocalGamersProperty()[1];
    int started=0,ended=0,finals=0;
    session->GameStarted+=[&](auto*,const GameStartedEventArgs&){++started;};
    session->GameEnded+=[&](auto*,const GameEndedEventArgs&){++ended;EXPECT_EQ(2,finals);};
    session->WriteUnarbitratedLeaderboard+=[&](auto*,const WriteLeaderboardsEventArgs& args){++finals;if(args.getGamerProperty()==dana)write(dana,650);};
    Service::ServiceSessionSettings settings;settings.maxGamers=6;settings.properties[2]=5;
    settings.state=Service::ServiceSessionState::Playing;peer->publish(settings);
    until([&]{return started==1;});write(bob,700);
    settings.state=Service::ServiceSessionState::Lobby;peer->publish(settings);
    until([&]{return ended==1;});
    EXPECT_EQ(700,rating("Bob"));EXPECT_EQ(650,rating("Dana"));EXPECT_EQ(-1,rating("Alice"));
    // A second game opens a fresh epoch; leaving mid-game submits the leaving gamers' final writes.
    settings.state=Service::ServiceSessionState::Playing;peer->publish(settings);
    until([&]{return started==2;});finals=0;int leaving=0;
    session->WriteUnarbitratedLeaderboard+=[&](auto*,const WriteLeaderboardsEventArgs& args){if(args.getIsLeavingProperty())++leaving;if(args.getGamerProperty()==bob)write(bob,900);};
    session->Dispose();EXPECT_EQ(2,leaving);EXPECT_EQ(900,rating("Bob"));
}

TEST_F(OnlineLeaderboardTest, LosingTheHostWhilePlayingOffersLeavingWritesBeforeSessionEnded) {
    privatePeer(false);
    auto found=NetworkSession::Find(NetworkSessionType::PlayerMatch,std::vector<SignedInGamer*>{gamer(1),gamer(3)},{});
    std::unique_ptr<System::IAsyncResult> result(NetworkSession::BeginJoin(&std::as_const(found)[0],{}, {}));
    until([&]{return result->getIsCompletedProperty();});session=NetworkSession::EndJoin(result.get());
    auto* bob=session->getLocalGamersProperty()[0];int started=0,leaving=0;std::string order;
    session->GameStarted+=[&](auto*,const GameStartedEventArgs&){++started;};
    session->WriteUnarbitratedLeaderboard+=[&](auto*,const WriteLeaderboardsEventArgs& args) {
        order+='W';EXPECT_TRUE(args.getIsLeavingProperty());++leaving;if(args.getGamerProperty()==bob)write(bob,420);
    };
    session->SessionEnded+=[&](auto*,const NetworkSessionEndedEventArgs&){order+='X';};
    Service::ServiceSessionSettings settings;settings.maxGamers=6;settings.properties[2]=5;
    settings.state=Service::ServiceSessionState::Playing;peer->publish(settings);
    until([&]{return started==1;});
    peer.reset();
    until([&]{return !order.empty()&&order.back()=='X';});
    EXPECT_EQ("WWX",order);EXPECT_EQ(420,rating("Bob"));
    EXPECT_EQ(NetworkSessionState::Ended,session->getSessionStateProperty());
}
#endif

// SPDX-License-Identifier: MS-PL
#ifndef __EMSCRIPTEN__
#include "OnlineSessionTestFixture.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardIdentity.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardWriter.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardEntry.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardReader.hpp"
#include "System/InvalidOperationException.hpp"
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
#ifndef __EMSCRIPTEN__
namespace {
LeaderboardIdentity kills(){LeaderboardIdentity identity;identity.setKeyProperty("Kills");identity.setGameModeProperty(0);return identity;}
void writeKills(Microsoft::Xna::Framework::Net::NetworkGamer* gamer,long long value) {
    gamer->getLeaderboardWriterProperty().GetLeaderboard(kills())->setRatingProperty(value);
}
// The other machine's Ranked report, as its own CNA client would commit it.
void report(Service::IGamerServicesBackend& service,const std::string& session,int revision,
    const std::vector<std::pair<std::string,long long>>& rows) {
    const auto gameplay=service.beginLeaderboardGame({"b","d"});std::vector<Service::ServiceLeaderboardWrite> writes;
    for(const auto& [user,value]:rows){Service::ServiceLeaderboardWrite row;row.userId=user;row.key="Kills";row.rating=value;writes.push_back(row);}
    service.commitLeaderboardGame(gameplay,"b",writes,Service::ServiceArbitration{session,revision});
}
}

TEST_F(OnlineLeaderboardTest, RankedMachinesReportArbitratedAndTrueSkillForEveryGamerAndAgreementCommits) {
    kind=Service::ServiceSessionKind::Ranked;
    session=NetworkSession::Create(NetworkSessionType::Ranked,std::vector<SignedInGamer*>{gamer(0),gamer(2)},6,0,{});
    privatePeer(true,sessionId("b"));until([&]{return peer->ready()&&session->getAllGamersProperty().getCountProperty()==4;});
    std::vector<std::string> arbitrated,trueSkill,unarbitrated;
    session->WriteArbitratedLeaderboard+=[&](auto*,const WriteLeaderboardsEventArgs& args){arbitrated.push_back(args.getGamerProperty()->getGamertagProperty());};
    session->WriteTrueSkill+=[&](auto*,const WriteLeaderboardsEventArgs& args){trueSkill.push_back(args.getGamerProperty()->getGamertagProperty());};
    session->WriteUnarbitratedLeaderboard+=[&](auto*,const WriteLeaderboardsEventArgs& args){unarbitrated.push_back(args.getGamerProperty()->getGamertagProperty());};
    session->StartGame();session->Update();
    until([&]{return peer->snapshot().state==Service::ServiceSessionState::Playing;});
    const auto playing=peer->snapshot().revision;
    // Every machine writes arbitrated statistics for every gamer, remote ones included.
    const auto& all=session->getAllGamersProperty();
    std::map<std::string,long long> values{{"Alice",4},{"Charlie",2},{"Bob",7},{"Dana",8}};
    for(auto* gamer:all)writeKills(gamer,values.at(gamer->getGamertagProperty()));
    session->EndGame();session->Update();
    EXPECT_EQ((std::vector<std::string>{"Alice","Charlie"}),unarbitrated);
    EXPECT_EQ(4u,arbitrated.size());EXPECT_EQ(4u,trueSkill.size());
    // Nothing is committed until the other machine reports; its disagreement on Dana discards her row.
    EXPECT_EQ(-1,rating("Alice","Kills"));
    report(*service,peer->snapshot().session,playing,{{"a",4},{"c",2},{"b",7},{"d",9}});
    EXPECT_EQ(4,rating("Alice","Kills"));EXPECT_EQ(2,rating("Charlie","Kills"));EXPECT_EQ(7,rating("Bob","Kills"));
    EXPECT_EQ(-1,rating("Dana","Kills"));
}

TEST_F(OnlineLeaderboardTest, RankedDepartureRaisesLeavingEventsAndTheDepartedGamerStaysInTheReport) {
    kind=Service::ServiceSessionKind::Ranked;
    session=NetworkSession::Create(NetworkSessionType::Ranked,std::vector<SignedInGamer*>{gamer(0),gamer(2)},6,0,{});
    privatePeer(true,sessionId("b"));until([&]{return peer->ready()&&session->getAllGamersProperty().getCountProperty()==4;});
    std::vector<std::pair<std::string,bool>> leaving;
    session->WriteArbitratedLeaderboard+=[&](auto*,const WriteLeaderboardsEventArgs& args) {
        leaving.emplace_back(args.getGamerProperty()->getGamertagProperty(),args.getIsLeavingProperty());
        if(args.getIsLeavingProperty())writeKills(args.getGamerProperty(),0);
    };
    session->StartGame();session->Update();
    until([&]{return peer->snapshot().state==Service::ServiceSessionState::Playing;});
    const auto id=peer->snapshot().session;const auto playing=peer->snapshot().revision;
    for(auto* gamer:session->getLocalGamersProperty())writeKills(gamer,5);
    // The departing machine commits its own final report before leaving.
    report(*service,id,playing,{{"b",0},{"d",0}});
    peer.reset();until([&]{return session->getRemoteGamersProperty().getCountProperty()==0;});
    ASSERT_EQ(2u,leaving.size());EXPECT_TRUE(leaving[0].second&&leaving[1].second);
    leaving.clear();session->EndGame();session->Update();
    EXPECT_EQ(2u,leaving.size());
    EXPECT_EQ(5,rating("Alice","Kills"));EXPECT_EQ(0,rating("Bob","Kills"));EXPECT_EQ(0,rating("Dana","Kills"));
}

TEST_F(OnlineLeaderboardTest, OutsideRankedOnlyTheHostReportsTrueSkillAndRemoteRowsAreNotSubmitted) {
    session=NetworkSession::Create(NetworkSessionType::PlayerMatch,std::vector<SignedInGamer*>{gamer(0),gamer(2)},6,0,{});
    privatePeer(true,sessionId("b"));until([&]{return peer->ready()&&session->getAllGamersProperty().getCountProperty()==4;});
    int arbitratedEvents=0;std::vector<std::string> trueSkill;
    session->WriteArbitratedLeaderboard+=[&](auto*,const WriteLeaderboardsEventArgs&){++arbitratedEvents;};
    session->WriteTrueSkill+=[&](auto*,const WriteLeaderboardsEventArgs& args) {
        trueSkill.push_back(args.getGamerProperty()->getGamertagProperty());
        args.getGamerProperty()->getLeaderboardWriterProperty().GetLeaderboard(board())->setRatingProperty(77);
    };
    session->StartGame();session->Update();session->EndGame();session->Update();
    EXPECT_EQ(0,arbitratedEvents);EXPECT_EQ(4u,trueSkill.size());
    EXPECT_EQ(77,rating("Alice"));EXPECT_EQ(77,rating("Charlie"));EXPECT_EQ(-1,rating("Bob"));
}
// XNA Stream columns: a writer's entry hands out a writable stream (there is no SetValue(Stream)),
// committed with the entry; a read hands back a read-only stream of the same bytes.
TEST_F(OnlineLeaderboardTest, AStreamColumnIsWrittenThroughItsStreamAndReadBack) {
    session=NetworkSession::Create(NetworkSessionType::PlayerMatch,std::vector<SignedInGamer*>{gamer(0)},4,0,{});
    auto* alice=session->getLocalGamersProperty()[0];
    session->StartGame();session->Update();
    auto* entry=alice->getLeaderboardWriterProperty().GetLeaderboard(board());
    entry->setRatingProperty(42);
    auto* stream=entry->getColumnsProperty().GetValueStream("Ghost");
    ASSERT_NE(nullptr,stream);EXPECT_TRUE(stream->getCanWriteProperty());
    const std::vector<SharpRuntime::bytecs> ghost{1,2,3,250};
    stream->Write(ghost.data(),0,static_cast<int>(ghost.size()));
    // The same stream every time, so a game may keep writing it.
    EXPECT_EQ(stream,entry->getColumnsProperty().GetValueStream("Ghost"));
    session->EndGame();session->Update();
    EXPECT_EQ(42,rating("Alice"));
    const auto page=service->readLeaderboard("BestScoreLifeTime",0,0,10,"",std::vector<std::string>{"Alice"});
    ASSERT_EQ(1u,page.entries.size());
    EXPECT_EQ("stream",page.entries.front().columns.at("Ghost").type);
    auto reader=LeaderboardReader::Read(board(),0,10);
    const auto& entries=reader.getEntriesProperty();ASSERT_EQ(1,entries.getCountProperty());
    auto* read=entries[0].getColumnsProperty().GetValueStream("Ghost");
    ASSERT_NE(nullptr,read);EXPECT_FALSE(read->getCanWriteProperty());
    std::vector<SharpRuntime::bytecs> back(8);
    EXPECT_EQ(4,read->Read(back.data(),0,static_cast<int>(back.size())));
    back.resize(4);EXPECT_EQ(ghost,back);
    // Outside a game a writer's columns refuse, as every other leaderboard write does.
    EXPECT_THROW((void)alice->getLeaderboardWriterProperty().GetLeaderboard(board())->getColumnsProperty().GetValueStream("Ghost"),
        System::InvalidOperationException);
}
#endif

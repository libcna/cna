// SPDX-License-Identifier: MS-PL
#ifndef __EMSCRIPTEN__
#include "OnlineSessionTestFixture.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionJoinException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include <optional>

namespace {
using namespace OnlineSessionTesting;
using Microsoft::Xna::Framework::Net::AvailableNetworkSessionCollection;
using Microsoft::Xna::Framework::Net::GameEndedEventArgs;
using Microsoft::Xna::Framework::Net::GameStartedEventArgs;
using Microsoft::Xna::Framework::Net::GamerJoinedEventArgs;
using Microsoft::Xna::Framework::Net::GamerLeftEventArgs;
using Microsoft::Xna::Framework::Net::LocalNetworkGamer;
using Microsoft::Xna::Framework::Net::NetworkGamer;
using Microsoft::Xna::Framework::Net::NetworkSessionEndedEventArgs;
using Microsoft::Xna::Framework::Net::NetworkSessionEndReason;
using Microsoft::Xna::Framework::Net::NetworkSessionJoinError;
using Microsoft::Xna::Framework::Net::NetworkSessionJoinException;
using Microsoft::Xna::Framework::Net::NetworkSessionType;
using OnlineNetworkSessionTest=OnlineSessionTest;
}

TEST_F(OnlineNetworkSessionTest, PublicCreateCompletesAtUpdateAndProjectsTheLocalGroup) {
    int callbacks=0;const auto owner=std::this_thread::get_id();
    std::unique_ptr<System::IAsyncResult> result(NetworkSession::BeginCreate(NetworkSessionType::PlayerMatch,
        std::vector<SignedInGamer*>{gamer(0),gamer(2)},6,1,properties(),[&](auto& value) {
            ++callbacks;EXPECT_EQ(owner,std::this_thread::get_id());EXPECT_TRUE(value.getIsCompletedProperty());
        },7));
    EXPECT_FALSE(result->getCompletedSynchronouslyProperty());EXPECT_FALSE(result->getIsCompletedProperty());
    EXPECT_FALSE(result->getAsyncWaitHandleProperty().WaitOne(0));EXPECT_EQ(0,callbacks);
    EXPECT_THROW((void)NetworkSession::BeginFind(NetworkSessionType::PlayerMatch,1,{}, {},{}),System::InvalidOperationException);
    until([&]{return result->getIsCompletedProperty();});
    EXPECT_EQ(1,callbacks);EXPECT_TRUE(result->getAsyncWaitHandleProperty().WaitOne(0));
    session=NetworkSession::EndCreate(result.get());
    ASSERT_NE(nullptr,session);EXPECT_TRUE(session->getIsHostProperty());
    EXPECT_EQ(NetworkSessionType::PlayerMatch,session->getSessionTypeProperty());
    EXPECT_EQ(6,session->getMaxGamersProperty());EXPECT_EQ(1,session->getPrivateGamerSlotsProperty());
    EXPECT_EQ(5,session->getSessionPropertiesProperty().getItem(2));
    const auto& locals=session->getLocalGamersProperty();ASSERT_EQ(2,locals.getCountProperty());
    EXPECT_EQ("Alice",locals[0]->getGamertagProperty());EXPECT_EQ("Charlie",locals[1]->getGamertagProperty());
    EXPECT_EQ(1,locals[0]->getIdProperty());EXPECT_EQ(2,locals[1]->getIdProperty());
    EXPECT_TRUE(locals[0]->getIsHostProperty());EXPECT_FALSE(locals[1]->getIsHostProperty());
    EXPECT_EQ(locals[0],session->getHostProperty());
    EXPECT_EQ(&locals[0]->getMachineProperty(),&locals[1]->getMachineProperty());
    EXPECT_EQ(2,locals[0]->getMachineProperty().getGamersProperty().getCountProperty());
    EXPECT_EQ(7,std::any_cast<int>(result->getAsyncStateProperty()));
    EXPECT_THROW((void)NetworkSession::EndCreate(result.get()),System::InvalidOperationException);
    const auto directory=service->sessionDirectory().get("a",sessionId("b"));
    EXPECT_EQ(2,directory.currentGamers);EXPECT_EQ(1,directory.privateSlots);EXPECT_EQ(5,directory.properties[2]);
}

TEST_F(OnlineNetworkSessionTest, HostProjectsRemoteGroupsDataStateAndDepartureAtUpdate) {
    session=NetworkSession::Create(NetworkSessionType::PlayerMatch,std::vector<SignedInGamer*>{gamer(0),gamer(2)},6,0,properties());
    std::vector<std::string> joined,left;int started=0;
    session->GamerJoined+=[&](auto*,const GamerJoinedEventArgs& args){joined.push_back(args.getGamerProperty()->getGamertagProperty());};
    session->GamerLeft+=[&](auto*,const GamerLeftEventArgs& args){left.push_back(args.getGamerProperty()->getGamertagProperty());};
    session->GameStarted+=[&](auto*,const GameStartedEventArgs&){++started;};
    EXPECT_EQ((std::vector<std::string>{"Alice","Charlie"}),joined);
    privatePeer(true,sessionId("b"));
    until([&]{return peer->ready()&&session->getAllGamersProperty().getCountProperty()==4;});
    until([&]{return joined.size()==4;});
    EXPECT_EQ((std::vector<std::string>{"Alice","Charlie","Bob","Dana"}),joined);
    const auto& remote=session->getRemoteGamersProperty();ASSERT_EQ(2,remote.getCountProperty());
    EXPECT_FALSE(remote[0]->getIsLocalProperty());EXPECT_FALSE(remote[0]->getIsHostProperty());
    EXPECT_EQ(3,remote[0]->getIdProperty());EXPECT_EQ(4,remote[1]->getIdProperty());
    EXPECT_EQ(&remote[0]->getMachineProperty(),&remote[1]->getMachineProperty());
    EXPECT_NE(&remote[0]->getMachineProperty(),&session->getLocalGamersProperty()[0]->getMachineProperty());
    EXPECT_EQ(remote[0],session->FindGamerById(3));

    auto* alice=session->getLocalGamersProperty()[0];auto* charlie=session->getLocalGamersProperty()[1];
    alice->SendData(std::vector<SharpRuntime::bytecs>{1,2,3},Microsoft::Xna::Framework::Net::SendDataOptions::Reliable,remote[0]);
    session->Update();
    until([&]{return count(ServiceENetObservation::Type::Data)==1;});
    auto data=std::find_if(observed.begin(),observed.end(),[](const auto& value){return value.data.has_value();});
    EXPECT_EQ(1,data->data->SenderWireId);EXPECT_EQ(3,data->data->TargetWireId);
    EXPECT_EQ((std::vector<unsigned char>{1,2,3}),data->data->Payload);
    peer->send(4,2,{9,8},Microsoft::Xna::Framework::Net::SendDataOptions::ReliableInOrder);
    until([&]{return charlie->getIsDataAvailableProperty();});
    std::vector<SharpRuntime::bytecs> buffer(8);NetworkGamer* sender=nullptr;
    EXPECT_EQ(2,charlie->ReceiveData(buffer,sender));EXPECT_EQ(remote[1],sender);EXPECT_EQ(9,buffer[0]);

    session->getSessionPropertiesProperty()[6]=44;session->StartGame();
    until([&]{return started==1;});
    until([&]{auto value=service->sessionDirectory().get("b",peer->snapshot().session);
        return value.state==Service::ServiceSessionState::Playing&&value.properties[6]==44;});
    until([&]{return peer->snapshot().state==Service::ServiceSessionState::Playing;});

    auto* bob=remote[0];peer.reset();
    until([&]{return left.size()==2;});
    EXPECT_EQ((std::vector<std::string>{"Bob","Dana"}),left);
    EXPECT_TRUE(bob->getHasLeftSessionProperty());EXPECT_EQ(2,session->getPreviousGamersProperty().getCountProperty());
    EXPECT_EQ(0,session->getRemoteGamersProperty().getCountProperty());
    const auto id=sessionId("b");session->Dispose();
    EXPECT_THROW((void)service->sessionDirectory().get("a",id),Service::ServiceOperationError);
}

TEST_F(OnlineNetworkSessionTest, LobbyReadinessCrossesTheServiceSessionAndClearsWhenTheGameEnds) {
    session=NetworkSession::Create(NetworkSessionType::PlayerMatch,std::vector<SignedInGamer*>{gamer(0),gamer(2)},6,0,properties());
    int started=0,ended=0;
    session->GameStarted+=[&](auto*,const GameStartedEventArgs&){++started;};
    session->GameEnded+=[&](auto*,const GameEndedEventArgs&){++ended;};
    privatePeer(true,sessionId("b"));
    until([&]{return peer->ready()&&session->getAllGamersProperty().getCountProperty()==4;});
    const auto reported=[&](unsigned char id){
        std::optional<bool> value;
        for(const auto& event:observed)if(event.type==ServiceENetObservation::Type::Readiness)
            for(const auto& entry:event.readiness)if(entry.WireId==id)value=entry.IsReady;
        return value;
    };
    auto* alice=session->getLocalGamersProperty()[0];auto* charlie=session->getLocalGamersProperty()[1];
    auto* bob=session->getRemoteGamersProperty()[0];auto* dana=session->getRemoteGamersProperty()[1];
    EXPECT_THROW(bob->setIsReadyProperty(true),System::InvalidOperationException);
    alice->setIsReadyProperty(true);charlie->setIsReadyProperty(true);
    until([&]{return reported(1)==true&&reported(2)==true;});
    peer->publishReady({{3,true}});
    until([&]{return bob->getIsReadyProperty();});
    EXPECT_FALSE(dana->getIsReadyProperty());EXPECT_FALSE(session->getIsEveryoneReadyProperty());
    peer->publishReady({{4,true}});
    until([&]{return session->getIsEveryoneReadyProperty();});

    session->StartGame();until([&]{return started==1;});
    EXPECT_THROW(alice->setIsReadyProperty(false),System::InvalidOperationException);
    session->EndGame();until([&]{return ended==1;});
    for(auto* gamer:std::array<NetworkGamer*,4>{alice,charlie,bob,dana})EXPECT_FALSE(gamer->getIsReadyProperty());
    EXPECT_FALSE(session->getIsEveryoneReadyProperty());
    // The host may clear anyone's readiness; the report reaches the other machine.
    until([&]{return peer->snapshot().state==Service::ServiceSessionState::Lobby;});
    peer->publishReady({{3,true}});until([&]{return bob->getIsReadyProperty();});
    session->ResetReady();EXPECT_FALSE(bob->getIsReadyProperty());
    until([&]{return reported(3)==false;});
}

// Reference NetworkMachine.RemoveFromSession over the service: CNA's host removes a peer, and a
// peer host removes CNA's session.
TEST_F(OnlineNetworkSessionTest, TheHostRemovesAMachineWhoseGamersLeave) {
    session=NetworkSession::Create(NetworkSessionType::PlayerMatch,std::vector<SignedInGamer*>{gamer(0),gamer(2)},6,0,properties());
    std::vector<std::string> left;
    session->GamerLeft+=[&](auto*,const GamerLeftEventArgs& args){left.push_back(args.getGamerProperty()->getGamertagProperty());};
    privatePeer(true,sessionId("b"));
    until([&]{return peer->ready()&&session->getAllGamersProperty().getCountProperty()==4;});
    EXPECT_THROW(session->getLocalGamersProperty()[0]->getMachineProperty().RemoveFromSession(),System::InvalidOperationException);
    session->getRemoteGamersProperty()[0]->getMachineProperty().RemoveFromSession();
    until([&]{return left.size()==2&&count(ServiceENetObservation::Type::Failed)==1;});
    EXPECT_EQ((std::vector<std::string>{"Bob","Dana"}),left);
    auto failed=std::find_if(observed.begin(),observed.end(),[](const auto& value){return value.type==ServiceENetObservation::Type::Failed;});
    EXPECT_EQ("REMOVED_BY_HOST",failed->failure);
}

// An online session reports its traffic and each remote gamer's round trip, as a SystemLink one does.
TEST_F(OnlineNetworkSessionTest, TrafficAndRoundTripsReachTheSession) {
    session=NetworkSession::Create(NetworkSessionType::PlayerMatch,std::vector<SignedInGamer*>{gamer(0),gamer(2)},6,0,properties());
    privatePeer(true,sessionId("b"));
    until([&]{return peer->ready()&&session->getAllGamersProperty().getCountProperty()==4;});
    until([&]{return session->getBytesPerSecondSentProperty()>0&&session->getBytesPerSecondReceivedProperty()>0;});
    for(NetworkGamer* remote:session->getRemoteGamersProperty())
        until([&]{return remote->getRoundtripTimeProperty()>System::TimeSpan::Zero;});
    EXPECT_EQ(System::TimeSpan::Zero,session->getLocalGamersProperty()[0]->getRoundtripTimeProperty());
}
TEST_F(OnlineNetworkSessionTest, AMachineTheHostRemovesEndsWithRemovedByHost) {
    privatePeer(false);
    auto found=NetworkSession::Find(NetworkSessionType::PlayerMatch,std::vector<SignedInGamer*>{gamer(1),gamer(3)},{});
    ASSERT_EQ(1,found.getCountProperty());
    // The peer host must keep running while CNA joins, so the join is asynchronous here.
    std::unique_ptr<System::IAsyncResult> result(NetworkSession::BeginJoin(&std::as_const(found)[0],{},{}));
    until([&]{return result->getIsCompletedProperty();});
    session=NetworkSession::EndJoin(result.get());
    std::optional<NetworkSessionEndReason> reason;
    session->SessionEnded+=[&](auto*,const NetworkSessionEndedEventArgs& args){reason=args.getEndReasonProperty();};
    until([&]{return peer->snapshot().currentGamers==4;});
    std::string joined;for(const auto& row:peer->snapshot().members)if(row.machine!=peer->snapshot().machine)joined=row.machine;
    peer->removeMachine(joined);
    until([&]{return reason.has_value();});
    EXPECT_EQ(NetworkSessionEndReason::RemovedByHost,*reason);
}

TEST_F(OnlineNetworkSessionTest, PublicJoinUsesTheFindGroupAndFollowsDirectoryAuthority) {
    privatePeer(false);
    auto found=NetworkSession::Find(NetworkSessionType::PlayerMatch,std::vector<SignedInGamer*>{gamer(1),gamer(3)},{});
    ASSERT_EQ(1,found.getCountProperty());
    int callbacks=0;
    std::unique_ptr<System::IAsyncResult> result(NetworkSession::BeginJoin(&std::as_const(found)[0],[&](auto&){++callbacks;},{}));
    EXPECT_FALSE(result->getCompletedSynchronouslyProperty());
    until([&]{return result->getIsCompletedProperty();});
    session=NetworkSession::EndJoin(result.get());EXPECT_EQ(1,callbacks);
    EXPECT_FALSE(session->getIsHostProperty());
    const auto& locals=session->getLocalGamersProperty();ASSERT_EQ(2,locals.getCountProperty());
    EXPECT_EQ("Bob",locals[0]->getGamertagProperty());EXPECT_EQ(3,locals[0]->getIdProperty());
    EXPECT_FALSE(locals[0]->getIsHostProperty());EXPECT_FALSE(locals[1]->getIsHostProperty());
    ASSERT_EQ(4,session->getAllGamersProperty().getCountProperty());
    auto* host=session->getHostProperty();ASSERT_NE(nullptr,host);
    EXPECT_EQ("Alice",host->getGamertagProperty());EXPECT_TRUE(host->getIsHostProperty());EXPECT_FALSE(host->getIsLocalProperty());
    EXPECT_EQ(5,session->getSessionPropertiesProperty().getItem(2));
    std::vector<std::string> joined;int started=0,ended=0;std::optional<NetworkSessionEndReason> reason;
    session->GamerJoined+=[&](auto*,const GamerJoinedEventArgs& args){joined.push_back(args.getGamerProperty()->getGamertagProperty());};
    session->GameStarted+=[&](auto*,const GameStartedEventArgs&){++started;};
    session->GameEnded+=[&](auto*,const GameEndedEventArgs&){++ended;};
    session->SessionEnded+=[&](auto*,const NetworkSessionEndedEventArgs& args){reason=args.getEndReasonProperty();};
    EXPECT_EQ(4u,joined.size());for(int index=0;index<5;++index)tick();EXPECT_EQ(4u,joined.size());
    // Reference GamerCollection order: session index on every machine, so the host's group first.
    EXPECT_EQ((std::vector<std::string>{"Alice","Charlie","Bob","Dana"}),joined);
    EXPECT_EQ(host,session->getAllGamersProperty()[0]);

    // A client is not the host: host-only setters refuse without changing authority.
    EXPECT_THROW(session->setMaxGamersProperty(8),System::InvalidOperationException);
    EXPECT_THROW(session->setAllowJoinInProgressProperty(true),System::InvalidOperationException);
    EXPECT_THROW(session->getSessionPropertiesProperty()[0]=1,System::InvalidOperationException);
    EXPECT_THROW(session->StartGame(),System::InvalidOperationException);

    peer->send(1,4,{5},Microsoft::Xna::Framework::Net::SendDataOptions::Reliable);
    until([&]{return locals[1]->getIsDataAvailableProperty();});
    std::vector<SharpRuntime::bytecs> buffer(4);NetworkGamer* sender=nullptr;
    EXPECT_EQ(1,locals[1]->ReceiveData(buffer,sender));EXPECT_EQ(host,sender);

    Service::ServiceSessionSettings settings;settings.maxGamers=8;settings.properties[2]=5;settings.properties[4]=12;
    settings.state=Service::ServiceSessionState::Playing;peer->publish(settings);
    until([&]{return started==1;});
    EXPECT_EQ(Microsoft::Xna::Framework::Net::NetworkSessionState::Playing,session->getSessionStateProperty());
    EXPECT_EQ(8,session->getMaxGamersProperty());EXPECT_EQ(12,session->getSessionPropertiesProperty().getItem(4));
    settings.state=Service::ServiceSessionState::Lobby;peer->publish(settings);
    until([&]{return ended==1;});

    peer.reset();
    until([&]{return reason.has_value();});
    EXPECT_EQ(NetworkSessionEndReason::HostEndedSession,*reason);
    EXPECT_EQ(Microsoft::Xna::Framework::Net::NetworkSessionState::Ended,session->getSessionStateProperty());
}

TEST_F(OnlineNetworkSessionTest, RankedCreateRefusesJoinInProgressAndPrivateSlotsFollowReferenceBounds) {
    EXPECT_THROW((void)NetworkSession::BeginCreate(NetworkSessionType::Ranked,1,4,4,{}, {},{}),System::ArgumentOutOfRangeException);
    EXPECT_THROW((void)NetworkSession::BeginCreate(NetworkSessionType::SystemLink,1,4,4,{}, {},{}),System::ArgumentOutOfRangeException);
    session=NetworkSession::Create(NetworkSessionType::Ranked,1,4,3,{});
    ASSERT_NE(nullptr,session);EXPECT_EQ(NetworkSessionType::Ranked,session->getSessionTypeProperty());
    EXPECT_EQ(1,session->getLocalGamersProperty().getCountProperty());
    EXPECT_NO_THROW(session->setAllowJoinInProgressProperty(false));
    EXPECT_THROW(session->setAllowJoinInProgressProperty(true),System::NotSupportedException);
    EXPECT_THROW(session->setMaxGamersProperty(3),System::ArgumentOutOfRangeException);
    EXPECT_THROW(session->setPrivateGamerSlotsProperty(4),System::ArgumentOutOfRangeException);
    session->setPrivateGamerSlotsProperty(1);session->setMaxGamersProperty(5);
    kind=Service::ServiceSessionKind::Ranked;
    // No public slot is advertised until the new capacity reaches the directory.
    until([&]{const auto id=sessionId("b",1);if(id.empty())return false;
        const auto value=service->sessionDirectory().get("a",id);return value.maxGamers==5&&value.privateSlots==1;});
    // Created for one local gamer: the local limit refuses another, as for SystemLink.
    EXPECT_THROW(session->AddLocalGamer(gamer(1)),System::InvalidOperationException);
}

TEST_F(OnlineNetworkSessionTest, JoinFailuresMapToReferenceExceptionFamilies) {
    Service::ServiceSessionSettings settings;settings.maxGamers=2;
    service->sessionDirectory().create("a",{"a"},Service::ServiceSessionKind::PlayerMatch,settings);
    auto found=NetworkSession::Find(NetworkSessionType::PlayerMatch,std::vector<SignedInGamer*>{gamer(1)},{});
    ASSERT_EQ(1,found.getCountProperty());
    const auto id=sessionId("d",1);ASSERT_FALSE(id.empty());
    // Another machine fills the only public slot between Find and Join.
    service->sessionDirectory().join("c",{"c"},id);
    try{(void)NetworkSession::Join(&std::as_const(found)[0]);FAIL()<<"Expected a full session";}
    catch(const NetworkSessionJoinException& error){EXPECT_EQ(NetworkSessionJoinError::SessionFull,error.getJoinErrorProperty());}
    EXPECT_EQ(0,NetworkSession::GetActiveActionInstanceCountForTesting());
    // The host closes the session: the stale listing is no longer found.
    EXPECT_TRUE(service->sessionDirectory().leave("a",id));
    try{(void)NetworkSession::Join(&std::as_const(found)[0]);FAIL()<<"Expected a missing session";}
    catch(const NetworkSessionJoinException& error){EXPECT_EQ(NetworkSessionJoinError::SessionNotFound,error.getJoinErrorProperty());}
    EXPECT_EQ(0,NetworkSession::GetActiveActionInstanceCountForTesting());
}

TEST_F(OnlineNetworkSessionTest, AbandonedPendingCreateRollsBackMembershipAndReleasesTheSingleton) {
    {
        std::unique_ptr<System::IAsyncResult> result(NetworkSession::BeginCreate(NetworkSessionType::PlayerMatch,1,4,{}, {}));
        EXPECT_FALSE(result->getIsCompletedProperty());
    }
    for(int index=0;index<20;++index)tick();
    EXPECT_EQ(0,NetworkSession::GetActiveActionInstanceCountForTesting());
    EXPECT_TRUE(sessionId("b").empty());
    session=NetworkSession::Create(NetworkSessionType::PlayerMatch,1,4);
    EXPECT_TRUE(session->getIsHostProperty());
}

TEST_F(OnlineNetworkSessionTest, CallbackMayEndTheCreateAndTheSessionIsImmediatelyUsable) {
    NetworkSession* created=nullptr;
    std::unique_ptr<System::IAsyncResult> result(NetworkSession::BeginCreate(NetworkSessionType::PlayerMatch,2,4,
        [&](auto& value){created=NetworkSession::EndCreate(&value);},{}));
    until([&]{return created!=nullptr;});session=created;
    EXPECT_EQ(2,session->getLocalGamersProperty().getCountProperty());
    EXPECT_THROW((void)NetworkSession::EndCreate(result.get()),System::InvalidOperationException);
    session->Update();EXPECT_TRUE(session->getIsHostProperty());
}

namespace {
using Microsoft::Xna::Framework::Net::HostChangedEventArgs;
// One more private machine for three-machine sessions: its own engine over the fixture's routes.
struct Machine {
    std::unique_ptr<ServiceENetSession> engine;
    std::vector<ServiceENetObservation> observed;
};
}

TEST_F(OnlineNetworkSessionTest, AHostLeavingWithMigrationAllowedHandsTheSessionToTheJoiner) {
    privatePeer(false);
    Service::ServiceSessionSettings settings;settings.maxGamers=6;settings.properties[2]=5;settings.allowHostMigration=true;
    peer->publish(settings);
    until([&]{const auto id=sessionId("b");return !id.empty()&&service->sessionDirectory().get("a",id).allowHostMigration;});
    auto found=NetworkSession::Find(NetworkSessionType::PlayerMatch,std::vector<SignedInGamer*>{gamer(1),gamer(3)},{});
    ASSERT_EQ(1,found.getCountProperty());
    std::unique_ptr<System::IAsyncResult> joining(NetworkSession::BeginJoin(&std::as_const(found)[0],{},{}));
    until([&]{return joining->getIsCompletedProperty();});
    session=NetworkSession::EndJoin(joining.get());
    ASSERT_NE(nullptr,session);EXPECT_FALSE(session->getIsHostProperty());
    // The host's value reaches every machine, as XNA's SessionInfo does.
    EXPECT_TRUE(session->getAllowHostMigrationProperty());
    auto* oldHost=session->getHostProperty();ASSERT_NE(nullptr,oldHost);EXPECT_EQ("Alice",oldHost->getGamertagProperty());
    std::vector<std::string> left;std::optional<std::pair<NetworkGamer*,NetworkGamer*>> changed;int ended=0;
    session->GamerLeft+=[&](auto*,const GamerLeftEventArgs& args){left.push_back(args.getGamerProperty()->getGamertagProperty());};
    session->HostChanged+=[&](auto*,const HostChangedEventArgs& args) {
        // The old host's gamers have left before the host changes.
        EXPECT_EQ(2u,left.size());changed=std::pair{args.getOldHostProperty(),args.getNewHostProperty()};
    };
    session->SessionEnded+=[&](auto*,const NetworkSessionEndedEventArgs&){++ended;};

    peer.reset();
    until([&]{return changed.has_value();});
    EXPECT_EQ(oldHost,changed->first);
    const auto& locals=session->getLocalGamersProperty();
    EXPECT_EQ(locals[0],changed->second);EXPECT_EQ(locals[0],session->getHostProperty());
    EXPECT_TRUE(session->getIsHostProperty());EXPECT_TRUE(locals[0]->getIsHostProperty());EXPECT_FALSE(locals[1]->getIsHostProperty());
    EXPECT_EQ((std::vector<std::string>{"Alice","Charlie"}),left);
    EXPECT_EQ(2,session->getAllGamersProperty().getCountProperty());
    // IDs are the directory's ordinals, so the survivors keep theirs.
    EXPECT_EQ(3,locals[0]->getIdProperty());EXPECT_EQ(4,locals[1]->getIdProperty());
    for(int index=0;index<20;++index)tick();
    EXPECT_EQ(0,ended);
    // The new host holds the host's authority: its settings reach the directory.
    session->setMaxGamersProperty(8);
    const auto id=service->sessionDirectory().find("a",kind,1,{},0,32).sessions.front().session;
    until([&]{return service->sessionDirectory().get("b",id).maxGamers==8;});
    const auto directory=service->sessionDirectory().find("a",kind,1,{},0,32);
    ASSERT_EQ(1u,directory.sessions.size());EXPECT_EQ("b",directory.sessions.front().hostId);
}

TEST_F(OnlineNetworkSessionTest, WithoutMigrationTheHostLeavingStillEndsTheSession) {
    privatePeer(false);
    auto found=NetworkSession::Find(NetworkSessionType::PlayerMatch,std::vector<SignedInGamer*>{gamer(1),gamer(3)},{});
    ASSERT_EQ(1,found.getCountProperty());
    std::unique_ptr<System::IAsyncResult> joining(NetworkSession::BeginJoin(&std::as_const(found)[0],{},{}));
    until([&]{return joining->getIsCompletedProperty();});
    session=NetworkSession::EndJoin(joining.get());
    EXPECT_FALSE(session->getAllowHostMigrationProperty());
    std::optional<NetworkSessionEndReason> reason;int changes=0;
    session->SessionEnded+=[&](auto*,const NetworkSessionEndedEventArgs& args){reason=args.getEndReasonProperty();};
    session->HostChanged+=[&](auto*,const HostChangedEventArgs&){++changes;};
    peer.reset();
    until([&]{return reason.has_value();});
    EXPECT_EQ(NetworkSessionEndReason::HostEndedSession,*reason);EXPECT_EQ(0,changes);
}

TEST_F(OnlineNetworkSessionTest, AClientFollowsTheDirectoryToANewRemoteHost) {
    // Three machines: Alice hosts, Bob joins second, then this process's Charlie and Dana.
    auto machine=[&](OnlineSessionRequest::Operation operation,std::vector<std::string> users,std::vector<std::string> names,const std::string& target) {
        OnlineSessionRequest request;request.operation=operation;request.users=users;request.owner=users.front();
        request.kind=kind;request.settings.maxGamers=6;request.settings.allowHostMigration=true;request.session=target;
        OnlineSessionPreparation preparing(service,request,{},preparation());
        until([&]{return preparing.complete();});
        return std::make_unique<ServiceENetSession>(preparing.take(),names,routes());
    };
    Machine alice,bob;
    alice.engine=machine(OnlineSessionRequest::Operation::Create,{"a"},{"Alice"},{});
    const auto id=sessionId("b",1);ASSERT_FALSE(id.empty());
    bob.engine=machine(OnlineSessionRequest::Operation::Join,{"b"},{"Bob"},id);
    auto pump=[&]{for(auto* each:{&alice,&bob})if(each->engine)for(auto& event:each->engine->update())each->observed.push_back(std::move(event));};
    until([&]{pump();return bob.engine->ready();});
    auto found=NetworkSession::Find(NetworkSessionType::PlayerMatch,std::vector<SignedInGamer*>{gamer(2),gamer(3)},{});
    ASSERT_EQ(1,found.getCountProperty());
    std::unique_ptr<System::IAsyncResult> joining(NetworkSession::BeginJoin(&std::as_const(found)[0],{},{}));
    until([&]{pump();return joining->getIsCompletedProperty();});
    session=NetworkSession::EndJoin(joining.get());
    until([&]{pump();return session->getAllGamersProperty().getCountProperty()==4;});
    EXPECT_TRUE(session->getAllowHostMigrationProperty());
    EXPECT_EQ("Alice",session->getHostProperty()->getGamertagProperty());
    std::optional<std::pair<std::string,std::string>> changed;
    session->HostChanged+=[&](auto*,const HostChangedEventArgs& args) {
        changed=std::pair{args.getOldHostProperty()->getGamertagProperty(),args.getNewHostProperty()->getGamertagProperty()};
    };
    alice.engine.reset();
    until([&]{pump();return changed.has_value();});
    EXPECT_EQ((std::pair<std::string,std::string>{"Alice","Bob"}),*changed);
    auto* host=session->getHostProperty();
    EXPECT_EQ("Bob",host->getGamertagProperty());EXPECT_FALSE(host->getIsLocalProperty());EXPECT_TRUE(host->getIsHostProperty());
    EXPECT_FALSE(session->getIsHostProperty());
    EXPECT_EQ(3,session->getAllGamersProperty().getCountProperty());
    // Data still flows both ways through the new host.
    auto* charlie=session->getLocalGamersProperty()[0];
    charlie->SendData(std::vector<SharpRuntime::bytecs>{7},Microsoft::Xna::Framework::Net::SendDataOptions::Reliable,host);
    until([&]{pump();session->Update();return std::any_of(bob.observed.begin(),bob.observed.end(),[](const auto& value){return value.data.has_value();});});
    EXPECT_TRUE(std::any_of(bob.observed.begin(),bob.observed.end(),[](const auto& value){return value.type==ServiceENetObservation::Type::HostChanged;}));
    bob.engine->send(host->getIdProperty(),charlie->getIdProperty(),{9},Microsoft::Xna::Framework::Net::SendDataOptions::Reliable);
    until([&]{pump();return charlie->getIsDataAvailableProperty();});
}
TEST_F(OnlineNetworkSessionTest, AHostsAddedLocalGamerJoinsAtUpdateOnEveryMachine) {
    session=NetworkSession::Create(NetworkSessionType::PlayerMatch,std::vector<SignedInGamer*>{gamer(0)},6,0,properties());
    privatePeer(true,sessionId("b"));
    until([&]{return peer->ready()&&session->getAllGamersProperty().getCountProperty()==3;});
    std::vector<std::string> joined;
    session->GamerJoined+=[&](auto*,const GamerJoinedEventArgs& args){joined.push_back(args.getGamerProperty()->getGamertagProperty());};
    joined.clear();
    session->AddLocalGamer(gamer(2));
    // As the reference: the gamer arrives later, through GamerJoined.
    EXPECT_EQ(1,session->getLocalGamersProperty().getCountProperty());
    EXPECT_THROW(session->AddLocalGamer(gamer(2)),System::ArgumentException);
    until([&]{return joined.size()==1;});
    EXPECT_EQ("Charlie",joined.front());
    const auto& locals=session->getLocalGamersProperty();ASSERT_EQ(2,locals.getCountProperty());
    auto* charlie=locals[1];
    EXPECT_TRUE(charlie->getIsLocalProperty());EXPECT_FALSE(charlie->getIsHostProperty());EXPECT_FALSE(charlie->getIsPrivateSlotProperty());
    EXPECT_EQ(4,charlie->getIdProperty());
    EXPECT_EQ(&locals[0]->getMachineProperty(),&charlie->getMachineProperty());
    EXPECT_EQ(charlie,session->FindGamerById(4));
    // The other machine learns of it from the host.
    until([&]{return std::any_of(observed.begin(),observed.end(),[](const auto& value){
        return value.type==ServiceENetObservation::Type::Joined&&!value.gamers.empty()&&value.gamers.front().Gamertag!="Alice"
            &&std::any_of(value.gamers.begin(),value.gamers.end(),[](const auto& entry){return entry.Gamertag=="Charlie";});});});
    auto* bob=session->getRemoteGamersProperty()[0];
    charlie->SendData(std::vector<SharpRuntime::bytecs>{4,2},Microsoft::Xna::Framework::Net::SendDataOptions::Reliable,bob);
    until([&]{session->Update();return std::any_of(observed.begin(),observed.end(),[](const auto& value){return value.data&&value.data->SenderWireId==4;});});
    peer->send(bob->getIdProperty(),4,{8},Microsoft::Xna::Framework::Net::SendDataOptions::Reliable);
    until([&]{return charlie->getIsDataAvailableProperty();});
    const auto directory=service->sessionDirectory().get("c",Service::activeOnlineSession()->session);
    EXPECT_TRUE(std::any_of(directory.members.begin(),directory.members.end(),[&](const auto& row){
        return row.userId=="c"&&row.machine==directory.hostMachine&&row.ordinal==3;}));
}

TEST_F(OnlineNetworkSessionTest, AClientsAddedLocalGamerReachesTheHost) {
    privatePeer(false);
    auto found=NetworkSession::Find(NetworkSessionType::PlayerMatch,std::vector<SignedInGamer*>{gamer(1)},{});
    ASSERT_EQ(1,found.getCountProperty());
    std::unique_ptr<System::IAsyncResult> joining(NetworkSession::BeginJoin(&std::as_const(found)[0],{},{}));
    until([&]{return joining->getIsCompletedProperty();});
    session=NetworkSession::EndJoin(joining.get());
    until([&]{return session->getAllGamersProperty().getCountProperty()==3;});
    int joinedEvents=0;
    session->GamerJoined+=[&](auto*,const GamerJoinedEventArgs&){++joinedEvents;};
    joinedEvents=0;
    session->AddLocalGamer(gamer(3));
    until([&]{return session->getLocalGamersProperty().getCountProperty()==2;});
    auto* dana=session->getLocalGamersProperty()[1];
    EXPECT_EQ("Dana",dana->getGamertagProperty());EXPECT_FALSE(session->getIsHostProperty());
    // The host reads the grown group from the directory and announces it.
    until([&]{return std::any_of(observed.begin(),observed.end(),[&](const auto& value){
        return value.type==ServiceENetObservation::Type::Joined&&std::any_of(value.gamers.begin(),value.gamers.end(),
            [&](const auto& entry){return entry.Gamertag=="Dana"&&entry.WireId==dana->getIdProperty();});});});
    auto* host=session->getHostProperty();
    dana->SendData(std::vector<SharpRuntime::bytecs>{3},Microsoft::Xna::Framework::Net::SendDataOptions::Reliable,host);
    until([&]{session->Update();return std::any_of(observed.begin(),observed.end(),[&](const auto& value){
        return value.data&&value.data->SenderWireId==dana->getIdProperty();});});
    peer->send(host->getIdProperty(),dana->getIdProperty(),{6},Microsoft::Xna::Framework::Net::SendDataOptions::Reliable);
    until([&]{return dana->getIsDataAvailableProperty();});
    EXPECT_EQ(1,joinedEvents);
}

TEST_F(OnlineNetworkSessionTest, AnAddTheServiceRefusesLeavesTheGamerOut) {
    session=NetworkSession::Create(NetworkSessionType::PlayerMatch,std::vector<SignedInGamer*>{gamer(0)},6,0,properties());
    // Charlie already belongs to another session of this title.
    Service::ServiceSessionSettings settings;settings.maxGamers=2;
    service->sessionDirectory().create("c",{"c"},Service::ServiceSessionKind::PlayerMatch,settings);
    int joined=0;session->GamerJoined+=[&](auto*,const GamerJoinedEventArgs&){++joined;};
    joined=0;
    session->AddLocalGamer(gamer(2));
    for(int frame=0;frame<200;++frame){tick();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    EXPECT_EQ(0,joined);EXPECT_EQ(1,session->getLocalGamersProperty().getCountProperty());
    // Nothing is left pending: the same gamer may be asked for again.
    EXPECT_NO_THROW(session->AddLocalGamer(gamer(2)));
}
#endif

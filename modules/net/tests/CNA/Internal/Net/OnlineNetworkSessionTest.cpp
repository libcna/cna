// SPDX-License-Identifier: MS-PL
#ifndef __EMSCRIPTEN__
#include <gtest/gtest.h>
#include "../../../../src/Internal/OnlineSessionBinding.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionJoinException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/IServiceProvider.hpp"
#include <algorithm>
#include <array>
#include <map>
#include <thread>

namespace {
using namespace CNA::Internal::Net;
namespace Service=CNA::Internal::GamerServices;
using namespace Microsoft::Xna::Framework::GamerServices;
using Microsoft::Xna::Framework::Net::AvailableNetworkSessionCollection;
using Microsoft::Xna::Framework::Net::GameEndedEventArgs;
using Microsoft::Xna::Framework::Net::GameStartedEventArgs;
using Microsoft::Xna::Framework::Net::GamerJoinedEventArgs;
using Microsoft::Xna::Framework::Net::GamerLeftEventArgs;
using Microsoft::Xna::Framework::Net::LocalNetworkGamer;
using Microsoft::Xna::Framework::Net::NetworkGamer;
using Microsoft::Xna::Framework::Net::NetworkSession;
using Microsoft::Xna::Framework::Net::NetworkSessionEndedEventArgs;
using Microsoft::Xna::Framework::Net::NetworkSessionEndReason;
using Microsoft::Xna::Framework::Net::NetworkSessionJoinError;
using Microsoft::Xna::Framework::Net::NetworkSessionJoinException;
using Microsoft::Xna::Framework::Net::NetworkSessionProperties;
using Microsoft::Xna::Framework::Net::NetworkSessionType;

struct Provider final : System::IServiceProvider {void* GetService(const std::type_info&)const override{return nullptr;}};
struct Portal {std::map<std::string,std::uint16_t> ports;};
// Deterministic stand-in for the verified relay: real loopback ENet, routes published through a portal.
class Transport final : public IPreparedOnlineTransport {
public:
    Transport(std::shared_ptr<Portal> portal,std::string machine):host_(ENetHostHandle::CreateRelayHost()),portal_(std::move(portal)),machine_(std::move(machine)) {
        portal_->ports[machine_]=host_.getBoundPortProperty();
    }
    ~Transport()override{portal_->ports.erase(machine_);}
    ENetHostHandle& host()override{return host_;}
    RelayTransport* relay()override{return nullptr;}
    RelayTransportStatus status()const override{RelayTransportStatus value;value.state=RelayTransportState::Ready;return value;}
private:
    ENetHostHandle host_;
    std::shared_ptr<Portal> portal_;
    std::string machine_;
};

class OnlineNetworkSessionTest : public ::testing::Test {
protected:
    void SetUp() override {
        previous_=Service::backend();
        std::vector<Service::ServiceIdentity> identities;
        for(auto [id,tag]:{std::pair{"a","Alice"},{"b","Bob"},{"c","Charlie"},{"d","Dana"}}) {
            Service::ServiceIdentity identity;identity.userId=id;identity.gamertag=tag;identity.allowOnlineSessions=true;
            identities.push_back(identity);
        }
        service=Service::makeFakeBackend(std::move(identities));Service::setBackendForTesting(service);
        if(!GamerServicesDispatcher::getIsInitializedProperty())GamerServicesDispatcher::Initialize(provider_);
        for(int slot=0;slot<4;++slot)service->signIn(slot,std::array{"Alice","Bob","Charlie","Dana"}[slot],"fixture");
        GamerServicesDispatcher::Update();
        ASSERT_EQ(4,Gamer::getSignedInGamersProperty()->getCountProperty());
        OnlineSessionFixture fixture;
        fixture.preparation=[this]{return preparation();};
        fixture.realtime=[this]{return routes();};
        setOnlineSessionFixtureForTesting(std::move(fixture));
    }
    void TearDown() override {
        peer.reset();
        if(session){if(!session->getIsDisposedProperty())session->Dispose();delete session;session=nullptr;}
        setOnlineSessionFixtureForTesting({});
        for(int slot=0;slot<4;++slot)service->signOut(slot);
        GamerServicesDispatcher::Update();Service::setBackendForTesting(previous_);
    }
    OnlinePreparationDependencies preparation() {
        OnlinePreparationDependencies dependencies;
        dependencies.configuration=[](const auto&){CNA::GamerServices::Configuration value;value.endpoint="https://fixture.invalid/cna/v1";value.gameId="one";return value;};
        dependencies.transport=[portal=portal](const auto&,auto ticket,const auto&){return std::make_unique<Transport>(portal,ticket.machine);};
        return dependencies;
    }
    ServiceENetDependencies routes() {
        ServiceENetDependencies dependencies;dependencies.setRoutes=[](const auto&){};
        dependencies.routePort=[portal=portal](const auto& machine){auto found=portal->ports.find(machine);return found==portal->ports.end()?0:found->second;};
        return dependencies;
    }
    SignedInGamer* gamer(int slot){return (*Gamer::getSignedInGamersProperty())[slot];}
    // A private realtime peer driven directly, so one process can host both sides of a session.
    void privatePeer(bool joining,const std::string& target={}) {
        OnlineSessionRequest request;
        request.operation=joining?OnlineSessionRequest::Operation::Join:OnlineSessionRequest::Operation::Create;
        request.users=joining?std::vector<std::string>{"b","d"}:std::vector<std::string>{"a","c"};request.owner=request.users.front();
        request.kind=kind;request.settings.maxGamers=6;request.settings.properties[2]=5;request.session=target;
        OnlineSessionPreparation preparing(service,request,{},preparation());
        until([&]{return preparing.complete();});
        peer=std::make_unique<ServiceENetSession>(preparing.take(),joining?std::vector<std::string>{"Bob","Dana"}
            :std::vector<std::string>{"Alice","Charlie"},routes());
    }
    std::string sessionId(const std::string& actor,int locals=2) {
        auto page=service->sessionDirectory().find(actor,kind,locals,{},0,32);
        return page.sessions.empty()?std::string{}:page.sessions.front().session;
    }
    template<class Work>void until(Work done,int line=__builtin_LINE()) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(!done()) {
            if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("fixture deadline at line "+std::to_string(line));
            tick();std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    void tick() {
        GamerServicesDispatcher::Update();
        if(session&&!session->getIsDisposedProperty())session->Update();
        if(peer)for(auto& event:peer->update())observed.push_back(std::move(event));
    }
    int count(ServiceENetObservation::Type type) const {
        return static_cast<int>(std::count_if(observed.begin(),observed.end(),[&](const auto& value){return value.type==type;}));
    }
    NetworkSessionProperties properties() {NetworkSessionProperties value;value.setItem(2,5);return value;}
    Provider provider_;
    std::shared_ptr<Service::IGamerServicesBackend> previous_,service;
    std::shared_ptr<Portal> portal=std::make_shared<Portal>();
    std::unique_ptr<ServiceENetSession> peer;
    std::vector<ServiceENetObservation> observed;
    NetworkSession* session=nullptr;
    Service::ServiceSessionKind kind=Service::ServiceSessionKind::PlayerMatch;
};
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
    EXPECT_THROW(session->AddLocalGamer(gamer(1)),System::NotSupportedException);
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
#endif

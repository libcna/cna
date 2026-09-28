// SPDX-License-Identifier: MS-PL
#ifndef __EMSCRIPTEN__
#include <gtest/gtest.h>
#include "../../../../src/Internal/ServiceENetSession.hpp"
#include "../../../../src/Internal/RelayEnetPolicy.hpp"
#include "System/InvalidOperationException.hpp"
#include "CnaService/Protocol.hpp"
#include <algorithm>
#include <map>
#include <array>
#include <thread>

namespace {
using namespace CNA::Internal::Net;
namespace Service=CNA::Internal::GamerServices;
struct Portal {std::map<std::string,std::uint16_t> ports;RelayTransportState status=RelayTransportState::Ready;int destroyed=0;};
class Transport final : public IPreparedOnlineTransport {
public:
    Transport(std::shared_ptr<Portal> portal,std::string machine):host_(ENetHostHandle::CreateRelayHost()),portal_(std::move(portal)),machine_(std::move(machine)) {
        portal_->ports[machine_]=host_.getBoundPortProperty();
    }
    ~Transport()override{portal_->ports.erase(machine_);++portal_->destroyed;}
    ENetHostHandle& host()override{return host_;}
    RelayTransport* relay()override{return nullptr;}
    RelayTransportStatus status()const override{RelayTransportStatus value;value.state=portal_->status;return value;}
private:
    ENetHostHandle host_;
    std::shared_ptr<Portal> portal_;
    std::string machine_;
};
struct Fixture {
    std::shared_ptr<Service::IGamerServicesBackend> backend;
    std::shared_ptr<Portal> portal=std::make_shared<Portal>();
    std::unique_ptr<ServiceENetSession> host,client;
    std::vector<ServiceENetObservation> hostEvents,clientEvents;
    std::string session;
    Fixture() {
        std::vector<Service::ServiceIdentity> identities;
        for(const auto& [id,tag]:std::array{std::pair{"a","Alice"},std::pair{"b","Bob"},std::pair{"c","Charlie"},std::pair{"d","Dana"}}) {
            Service::ServiceIdentity value;value.userId=id;value.gamertag=tag;value.allowOnlineSessions=true;identities.push_back(value);
        }
        backend=Service::makeFakeBackend(std::move(identities));
        for(int slot=0;slot<4;++slot)backend->signIn(slot,std::array{"Alice","Bob","Charlie","Dana"}[slot],"fixture");pump();
    }
    void pump(){for(auto& event:backend->pump())if(event.completion)event.completion();}
    std::unique_ptr<PreparedOnlineSession> prepare(bool joining=false,std::vector<std::string> users={}) {
        OnlineSessionRequest request;request.operation=joining?OnlineSessionRequest::Operation::Join:OnlineSessionRequest::Operation::Create;
        request.owner=joining?"b":"a";request.users=joining?std::vector<std::string>{"b","d"}:std::vector<std::string>{"a","c"};
        if(!users.empty()){request.users=std::move(users);request.owner=request.users.front();}
        request.settings.maxGamers=6;request.settings.properties[7]=73;request.session=joining?session:"";
        OnlinePreparationDependencies dependencies;
        dependencies.configuration=[](const auto&){CNA::GamerServices::Configuration value;value.endpoint="https://fixture.invalid/cna/v1";value.gameId="one";return value;};
        dependencies.transport=[portal=portal](const auto&,auto ticket,const auto&){return std::make_unique<Transport>(portal,ticket.machine);};
        OnlineSessionPreparation operation(backend,request,{},std::move(dependencies));pump();auto lease=operation.take();session=lease->snapshot().session;return lease;
    }
    ServiceENetDependencies routes() {
        ServiceENetDependencies dependencies;dependencies.setRoutes=[](const auto&){};
        dependencies.routePort=[portal=portal](const auto& machine){auto found=portal->ports.find(machine);return found==portal->ports.end()?0:found->second;};return dependencies;
    }
    void pair() {
        host=std::make_unique<ServiceENetSession>(prepare(),std::vector<std::string>{"Alice","Charlie"},routes());
        client=std::make_unique<ServiceENetSession>(prepare(true),std::vector<std::string>{"Bob","Dana"},routes());
        until([&]{return client->ready();});
    }
    void tick() {
        pump();
        if(host){auto events=host->update();hostEvents.insert(hostEvents.end(),std::make_move_iterator(events.begin()),std::make_move_iterator(events.end()));}
        if(client){auto events=client->update();clientEvents.insert(clientEvents.end(),std::make_move_iterator(events.begin()),std::make_move_iterator(events.end()));}
    }
    template<class Work>void until(Work work) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!work()){if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("fixture deadline");tick();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    }
    static int count(const std::vector<ServiceENetObservation>& events,ServiceENetObservation::Type type) {
        return static_cast<int>(std::count_if(events.begin(),events.end(),[&](const auto& value){return value.type==type;}));
    }
};
}
TEST(ServiceENetSessionTest, ExactWelcomeProducesStableIdsForTwoLocalGroupsOnTheOwner) {
    Fixture fixture;fixture.pair();EXPECT_TRUE(fixture.host->ready());EXPECT_TRUE(fixture.client->ready());
    EXPECT_EQ(1,Fixture::count(fixture.hostEvents,ServiceENetObservation::Type::Ready));
    EXPECT_EQ(1,Fixture::count(fixture.clientEvents,ServiceENetObservation::Type::Ready));
    EXPECT_EQ(1,Fixture::count(fixture.hostEvents,ServiceENetObservation::Type::Joined));
    auto ready=std::find_if(fixture.clientEvents.begin(),fixture.clientEvents.end(),[](const auto& event){return event.type==ServiceENetObservation::Type::Ready;});
    ASSERT_NE(fixture.clientEvents.end(),ready);EXPECT_EQ((std::vector<unsigned char>{3,4}),ready->ids);
    ASSERT_EQ(2,ready->gamers.size());EXPECT_EQ("Alice",ready->gamers[0].Gamertag);EXPECT_TRUE(ready->gamers[0].IsHost);EXPECT_FALSE(ready->gamers[1].IsHost);
    for(int index=0;index<5;++index)fixture.tick();EXPECT_EQ(1,Fixture::count(fixture.clientEvents,ServiceENetObservation::Type::Ready));
}
TEST(ServiceENetSessionTest, ReliableFragmentedAndUnreliableDataReachBothLocalAccountSlots) {
    Fixture fixture;fixture.pair();std::vector<unsigned char> large(32768,43);
    fixture.host->send(1,3,{1,2,3},SendDataOptions::Reliable);fixture.client->send(4,2,large,SendDataOptions::ReliableInOrder);
    fixture.host->send(2,4,{7},SendDataOptions::InOrder);fixture.client->send(3,1,{9},SendDataOptions::None);
    fixture.until([&]{return Fixture::count(fixture.hostEvents,ServiceENetObservation::Type::Data)==2&&Fixture::count(fixture.clientEvents,ServiceENetObservation::Type::Data)==2;});
    auto found=std::find_if(fixture.hostEvents.begin(),fixture.hostEvents.end(),[](const auto& event){return event.data&&event.data->SenderWireId==4;});
    ASSERT_NE(fixture.hostEvents.end(),found);EXPECT_EQ(large,found->data->Payload);EXPECT_EQ(2,found->data->TargetWireId);
}
TEST(ServiceENetSessionTest, LocalDeliveryIsQueuedUntilUpdateAndBoundedByCountAndBytes) {
    Fixture fixture;fixture.host=std::make_unique<ServiceENetSession>(fixture.prepare(),std::vector<std::string>{"Alice","Charlie"},fixture.routes());
    for(int index=0;index<128;++index)fixture.host->send(1,2,{7},SendDataOptions::Reliable);
    EXPECT_THROW(fixture.host->send(1,2,{7},SendDataOptions::Reliable),Service::ServiceOperationError);
    auto events=fixture.host->update();EXPECT_EQ(128,Fixture::count(events,ServiceENetObservation::Type::Data));
    EXPECT_EQ(1,Fixture::count(events,ServiceENetObservation::Type::Ready));
    const std::vector<unsigned char> bytes(MaxRelayGamePacketBytes-4,4);
    for(int index=0;index<4;++index)fixture.host->send(1,2,bytes,SendDataOptions::Reliable);
    EXPECT_THROW(fixture.host->send(1,2,bytes,SendDataOptions::Reliable),Service::ServiceOperationError);
    EXPECT_EQ(4,Fixture::count(fixture.host->update(),ServiceENetObservation::Type::Data));
}
TEST(ServiceENetSessionTest, ForeignSenderUnknownTargetAndInvalidPayloadAreRefusedBeforeSending) {
    Fixture fixture;fixture.pair();
    EXPECT_THROW(fixture.host->send(3,1,{1},SendDataOptions::Reliable),Service::ServiceOperationError);
    EXPECT_THROW(fixture.host->send(1,31,{1},SendDataOptions::Reliable),Service::ServiceOperationError);
    EXPECT_THROW(fixture.host->send(1,3,{1},static_cast<SendDataOptions>(9)),Service::ServiceOperationError);
    EXPECT_THROW(fixture.host->send(1,3,std::vector<unsigned char>(MaxRelayGamePacketBytes),SendDataOptions::Reliable),Service::ServiceOperationError);
    EXPECT_EQ(0,Fixture::count(fixture.hostEvents,ServiceENetObservation::Type::Data));
}
TEST(ServiceENetSessionTest, DestroyedRemoteLeaseRaisesOneCompleteGroupLeaveAndReleasesMembership) {
    Fixture fixture;fixture.pair();fixture.client.reset();
    fixture.until([&]{return Fixture::count(fixture.hostEvents,ServiceENetObservation::Type::Left)==1;});
    auto found=std::find_if(fixture.hostEvents.begin(),fixture.hostEvents.end(),[](const auto& event){return event.type==ServiceENetObservation::Type::Left;});
    ASSERT_NE(fixture.hostEvents.end(),found);EXPECT_EQ((std::vector<unsigned char>{3,4}),found->ids);
    EXPECT_EQ(2,fixture.backend->sessionDirectory().get("a",fixture.session).currentGamers);
    for(int index=0;index<5;++index)fixture.tick();EXPECT_EQ(1,Fixture::count(fixture.hostEvents,ServiceENetObservation::Type::Left));
}
TEST(ServiceENetSessionTest, HostClosureProducesOneFailureAndStopsFurtherDelivery) {
    Fixture fixture;fixture.pair();fixture.host.reset();
    fixture.until([&]{return Fixture::count(fixture.clientEvents,ServiceENetObservation::Type::Failed)==1;});
    EXPECT_FALSE(fixture.client->ready());EXPECT_THROW(fixture.client->send(3,1,{1},SendDataOptions::Reliable),Service::ServiceOperationError);
    for(int index=0;index<5;++index)fixture.tick();EXPECT_EQ(1,Fixture::count(fixture.clientEvents,ServiceENetObservation::Type::Failed));
}
TEST(ServiceENetSessionTest, RelayFailureIsObservedOnceWithNoRawDiagnosticAndRetainedOriginReleases) {
    Fixture fixture;fixture.pair();fixture.portal->status=RelayTransportState::Failed;fixture.tick();
    EXPECT_EQ(1,Fixture::count(fixture.hostEvents,ServiceENetObservation::Type::Failed));EXPECT_FALSE(fixture.host->ready());
    std::weak_ptr<Service::IGamerServicesBackend> origin=fixture.backend;fixture.backend.reset();EXPECT_FALSE(origin.expired());
    fixture.host.reset();fixture.client.reset();EXPECT_TRUE(origin.expired());EXPECT_EQ(2,fixture.portal->destroyed);
}
TEST(ServiceENetSessionTest, InvalidConstructorClaimsRollBackOwnedHostPreparation) {
    Fixture fixture;auto lease=fixture.prepare();
    EXPECT_THROW(ServiceENetSession(std::move(lease),{"Alice"},fixture.routes()),CnaService::Error);
    EXPECT_EQ(1,fixture.portal->destroyed);
    EXPECT_TRUE(fixture.backend->sessionDirectory().find("b",Service::ServiceSessionKind::PlayerMatch,1,{},0,32).sessions.empty());
    EXPECT_THROW(ServiceENetSession(nullptr,{"Alice"},fixture.routes()),Service::ServiceOperationError);
}
TEST(ServiceENetSessionTest, FakeRoutesAreExplicitAndForeignThreadWorkIsRefused) {
    Fixture fixture;EXPECT_THROW(ServiceENetSession(fixture.prepare(),{"Alice","Charlie"}),Service::ServiceOperationError);
    fixture.pair();bool refused=false;
    std::thread worker([&]{try{(void)fixture.host->update();}catch(const System::InvalidOperationException&){refused=true;}});
    worker.join();EXPECT_TRUE(refused);EXPECT_TRUE(fixture.host->ready());
}
TEST(ServiceENetSessionTest, ClientCannotCompleteWithoutWelcomeAndTimeoutIsObservedOnce) {
    Fixture fixture;auto hostLease=fixture.prepare();auto clock=std::chrono::steady_clock::now();auto routes=fixture.routes();routes.clock=[&]{return clock;};
    fixture.client=std::make_unique<ServiceENetSession>(fixture.prepare(true),std::vector<std::string>{"Bob","Dana"},routes);
    EXPECT_FALSE(fixture.client->ready());EXPECT_THROW(fixture.client->send(3,1,{1},SendDataOptions::Reliable),Service::ServiceOperationError);
    clock+=std::chrono::seconds(11);fixture.tick();
    ASSERT_EQ(1,Fixture::count(fixture.clientEvents,ServiceENetObservation::Type::Failed));EXPECT_EQ("JOIN_TIMED_OUT",fixture.clientEvents.back().failure);
    fixture.tick();EXPECT_EQ(1,Fixture::count(fixture.clientEvents,ServiceENetObservation::Type::Failed));
}
TEST(ServiceENetSessionTest, AuthenticatedRemovalPrecedesAnyFurtherDeliveryToTheDepartedGroup) {
    Fixture fixture;fixture.pair();EXPECT_FALSE(fixture.backend->sessionDirectory().leave("b",fixture.session));
    fixture.until([&]{return Fixture::count(fixture.hostEvents,ServiceENetObservation::Type::Left)==1;});
    EXPECT_THROW(fixture.host->send(1,3,{1},SendDataOptions::Reliable),Service::ServiceOperationError);
    EXPECT_EQ(1,Fixture::count(fixture.hostEvents,ServiceENetObservation::Type::Left));
}
TEST(ServiceENetSessionTest, ReliableOutgoingDataIsBoundedUntilAcknowledgedAndCleanupReleasesIt) {
    Fixture fixture;fixture.pair();const std::vector<unsigned char> large(MaxRelayGamePacketBytes-4,17);
    for(int index=0;index<4;++index)fixture.host->send(1,3,large,SendDataOptions::Reliable);
    EXPECT_THROW(fixture.host->send(1,3,large,SendDataOptions::Reliable),Service::ServiceOperationError);
    fixture.host.reset();fixture.client.reset();EXPECT_EQ(2,fixture.portal->destroyed);
}
TEST(ServiceENetSessionTest, ThreeMachinesRelayBetweenClientsAndPublishEachFullGroupOnce) {
    Fixture fixture;fixture.host=std::make_unique<ServiceENetSession>(fixture.prepare(false,{"a"}),std::vector<std::string>{"Alice"},fixture.routes());
    fixture.client=std::make_unique<ServiceENetSession>(fixture.prepare(true,{"b"}),std::vector<std::string>{"Bob"},fixture.routes());
    fixture.until([&]{return fixture.client->ready();});auto lease=fixture.prepare(true,{"c","d"});
    fixture.until([&]{return fixture.host->snapshot().currentGamers==4&&fixture.client->snapshot().currentGamers==4;});
    auto third=std::make_unique<ServiceENetSession>(std::move(lease),std::vector<std::string>{"Charlie","Dana"},fixture.routes());
    std::vector<ServiceENetObservation> thirdEvents;
    const auto collect=[&]{auto events=third->update();thirdEvents.insert(thirdEvents.end(),std::make_move_iterator(events.begin()),std::make_move_iterator(events.end()));};
    fixture.until([&]{collect();return third->ready()&&Fixture::count(fixture.clientEvents,ServiceENetObservation::Type::Joined)==1;});
    EXPECT_EQ(2,Fixture::count(fixture.hostEvents,ServiceENetObservation::Type::Joined));
    third->send(3,2,{31},SendDataOptions::Reliable);fixture.client->send(2,4,{42},SendDataOptions::Reliable);
    fixture.until([&]{collect();return Fixture::count(thirdEvents,ServiceENetObservation::Type::Data)==1&&Fixture::count(fixture.clientEvents,ServiceENetObservation::Type::Data)==1;});
    EXPECT_EQ(0,Fixture::count(fixture.hostEvents,ServiceENetObservation::Type::Data));
    auto received=std::find_if(thirdEvents.begin(),thirdEvents.end(),[](const auto& event){return event.data.has_value();});
    ASSERT_NE(thirdEvents.end(),received);EXPECT_EQ(2,received->data->SenderWireId);EXPECT_EQ(4,received->data->TargetWireId);EXPECT_EQ((std::vector<unsigned char>{42}),received->data->Payload);
    third.reset();fixture.until([&]{return Fixture::count(fixture.clientEvents,ServiceENetObservation::Type::Left)==1;});
    EXPECT_EQ(1,Fixture::count(fixture.hostEvents,ServiceENetObservation::Type::Left));
}
#endif

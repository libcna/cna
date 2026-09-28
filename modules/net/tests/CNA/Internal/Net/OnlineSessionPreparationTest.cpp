// SPDX-License-Identifier: MS-PL
#ifndef __EMSCRIPTEN__
#include <gtest/gtest.h>
#include "../../../../src/Internal/OnlineSessionPreparation.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp"
#include "System/InvalidOperationException.hpp"
#include <condition_variable>
#include <thread>

namespace {
using namespace CNA::Internal::Net;
namespace Service=CNA::Internal::GamerServices;
struct Observations {int created=0,destroyed=0,callbacks=0;std::function<RelayTransportStatus()> status;};
class FixtureTransport final : public IPreparedOnlineTransport {
public:
    explicit FixtureTransport(std::shared_ptr<Observations> observations)
        :host_(ENetHostHandle::CreateRelayHost()),observations_(std::move(observations)){++observations_->created;}
    ~FixtureTransport() override {++observations_->destroyed;}
    ENetHostHandle& host() override{return host_;}
    RelayTransport* relay() override{return nullptr;}
    RelayTransportStatus status() const override {
        if(observations_->status)return observations_->status();
        RelayTransportStatus result;result.state=RelayTransportState::Ready;return result;
    }
    void reconnect(Service::ServiceRelayTicket) override {}
private:
    ENetHostHandle host_;
    std::shared_ptr<Observations> observations_;
};
struct Fixture {
    std::shared_ptr<Service::IGamerServicesBackend> backend;
    std::shared_ptr<Observations> observations=std::make_shared<Observations>();
    OnlinePreparationDependencies dependencies;
    OnlineSessionRequest request;
    Fixture() {
        std::vector<Service::ServiceIdentity> identities;
        for(const std::string user: {"a","b","c","d"}) {
            Service::ServiceIdentity identity;identity.userId=user;identity.gamertag="Tag"+user;
            identity.allowOnlineSessions=true;identities.push_back(std::move(identity));
        }
        backend=Service::makeFakeBackend(std::move(identities));
        for(int index=0;index<4;++index)backend->signIn(index,std::string("Tag")+static_cast<char>('a'+index),"fixture");
        (void)backend->pump();request.owner="a";request.users={"a","b"};request.settings.maxGamers=6;
        dependencies.configuration=[](const auto&){CNA::GamerServices::Configuration value;value.endpoint="https://fixture.invalid/cna/v1";value.gameId="one";return value;};
        dependencies.transport=[observations=observations](const auto&,auto ticket,const auto&) {
            EXPECT_EQ(64U,ticket.ticket.size());return std::make_unique<FixtureTransport>(observations);
        };
    }
    std::unique_ptr<OnlineSessionPreparation> begin() {
        return std::make_unique<OnlineSessionPreparation>(backend,request,
            [observations=observations]{++observations->callbacks;},dependencies);
    }
    void pump(){for(auto& event:backend->pump())if(event.completion)event.completion();}
    std::size_t listings(){return backend->sessionDirectory().find("c",request.kind,1,{},0,32).sessions.size();}
};
template<class Work> void refused(Work work,const char* code) {
    try{work();FAIL()<<"Expected preparation refusal";}
    catch(const Service::ServiceOperationError& error){EXPECT_EQ(code,error.code);}
}
}
TEST(OnlineSessionPreparationTest, NativeDefaultsRefuseFakeNetworkAuthorityBeforeMutation) {
    Fixture f;
    EXPECT_THROW(OnlineSessionPreparation(f.backend,f.request),Microsoft::Xna::Framework::GamerServices::GamerServicesNotAvailableException);
    EXPECT_EQ(0U,f.listings());EXPECT_EQ(0,f.observations->created);
}
TEST(OnlineSessionPreparationTest, InvalidLocalRequestsFailBeforeQueueOrMembership) {
    Fixture f;f.request.users={"a","a"};refused([&]{(void)f.begin();},"INVALID_ARGUMENT");
    f.request.users={"a","b","c","d","e"};refused([&]{(void)f.begin();},"INVALID_ARGUMENT");
    f.request.users={"b"};refused([&]{(void)f.begin();},"INVALID_ARGUMENT");
    f.request.users={"a"};f.request.kind=static_cast<Service::ServiceSessionKind>(99);
    refused([&]{(void)f.begin();},"INVALID_ARGUMENT");
    f.request.kind=Service::ServiceSessionKind::PlayerMatch;
    refused([&]{OnlineSessionPreparation value(nullptr,f.request,{},f.dependencies);},"INVALID_ARGUMENT");
    EXPECT_EQ(0U,f.listings());EXPECT_EQ(0,f.observations->created);
}
TEST(OnlineSessionPreparationTest, CompletionPublishesOnlyAtOwnerPumpAndTakeConsumesOnce) {
    Fixture f;auto pending=f.begin();EXPECT_FALSE(pending->complete());
    EXPECT_THROW((void)pending->take(),System::InvalidOperationException);
    auto events=f.backend->pump();EXPECT_FALSE(pending->complete());EXPECT_EQ(0,f.observations->callbacks);
    for(auto& event:events)if(event.completion)event.completion();
    EXPECT_TRUE(pending->complete());EXPECT_EQ(1,f.observations->callbacks);
    auto lease=pending->take();EXPECT_EQ(2,lease->snapshot().currentGamers);
    EXPECT_EQ(f.backend,lease->backend());EXPECT_GT(lease->transport().host().getBoundPortProperty(),0);
    EXPECT_THROW((void)pending->take(),System::InvalidOperationException);
    pending.reset();EXPECT_EQ(1U,f.listings());EXPECT_EQ(0,f.observations->destroyed);
    EXPECT_TRUE(lease->release());EXPECT_TRUE(lease->release());EXPECT_EQ(0U,f.listings());
    EXPECT_EQ(1,f.observations->destroyed);EXPECT_THROW((void)lease->transport(),System::InvalidOperationException);
}
TEST(OnlineSessionPreparationTest, MalformedJoinAndUnusedCreateIdentifiersNeverReachMembership) {
    Fixture f;f.request.session=std::string(32,'a');refused([&]{(void)f.begin();},"INVALID_ARGUMENT");
    f.request.operation=OnlineSessionRequest::Operation::Join;f.request.session=std::string(33,'a');
    refused([&]{(void)f.begin();},"INVALID_ARGUMENT");f.request.session=std::string(32,'A');
    refused([&]{(void)f.begin();},"INVALID_ARGUMENT");f.request.session=std::string(32,'a');f.request.invite="bad";
    refused([&]{(void)f.begin();},"INVALID_ARGUMENT");EXPECT_EQ(0,f.observations->created);
}
TEST(OnlineSessionPreparationTest, CompletionReplayCannotInvokeTheOwnerTwice) {
    Fixture f;auto pending=f.begin();auto events=f.backend->pump();ASSERT_EQ(1U,events.size());
    events[0].completion();events[0].completion();EXPECT_EQ(1,f.observations->callbacks);
    auto lease=pending->take();EXPECT_TRUE(lease->release());
}
TEST(OnlineSessionPreparationTest, UnpublishedReadyValueDoesNotKeepItsLastBackendOwnerAlive) {
    Fixture f;auto pending=f.begin();auto events=f.backend->pump();
    std::weak_ptr<Service::IGamerServicesBackend> weak=f.backend;f.backend.reset();
    pending.reset();EXPECT_TRUE(weak.expired());EXPECT_EQ(1,f.observations->destroyed);
    for(auto& event:events)if(event.completion)event.completion();EXPECT_EQ(0,f.observations->callbacks);
}
TEST(OnlineSessionPreparationTest, ConsumedLeaseRetainsOriginAfterResultAndExternalOwnerDrop) {
    Fixture f;auto pending=f.begin();f.pump();auto lease=pending->take();
    std::weak_ptr<Service::IGamerServicesBackend> weak=f.backend;pending.reset();f.backend.reset();
    EXPECT_FALSE(weak.expired());EXPECT_TRUE(lease->release());lease.reset();EXPECT_TRUE(weak.expired());
    EXPECT_EQ(1,f.observations->destroyed);
}
TEST(OnlineSessionPreparationTest, DroppingQueuedResultCreatesNoMembershipAndNoBackendCycle) {
    Fixture f;auto pending=f.begin();pending.reset();f.pump();
    EXPECT_EQ(0U,f.listings());EXPECT_EQ(0,f.observations->callbacks);EXPECT_EQ(0,f.observations->created);
    pending=f.begin();std::weak_ptr<Service::IGamerServicesBackend> weak=f.backend;
    f.backend.reset();EXPECT_FALSE(weak.expired());pending.reset();EXPECT_TRUE(weak.expired());
}
TEST(OnlineSessionPreparationTest, DroppingReadyUnpublishedResultClosesAndRollsBackBeforeCallback) {
    Fixture f;auto pending=f.begin();auto events=f.backend->pump();EXPECT_EQ(1U,f.listings());
    EXPECT_FALSE(pending->complete());pending.reset();EXPECT_EQ(0U,f.listings());EXPECT_EQ(1,f.observations->destroyed);
    for(auto& event:events)if(event.completion)event.completion();EXPECT_EQ(0,f.observations->callbacks);
}
TEST(OnlineSessionPreparationTest, DroppingCompletedUnconsumedResultRollsBackMembership) {
    Fixture f;auto pending=f.begin();f.pump();EXPECT_EQ(1,f.observations->callbacks);
    pending.reset();EXPECT_EQ(0U,f.listings());EXPECT_EQ(1,f.observations->destroyed);
}
TEST(OnlineSessionPreparationTest, FactoryFailureRollsBackAndPreservesOriginalError) {
    Fixture f;f.dependencies.transport=[](const auto&,auto,const auto&) -> std::unique_ptr<IPreparedOnlineTransport> {throw Service::ServiceOperationError("TEST_TRANSPORT_FAILURE");};
    auto pending=f.begin();f.pump();EXPECT_TRUE(pending->complete());
    refused([&]{(void)pending->take();},"TEST_TRANSPORT_FAILURE");EXPECT_EQ(0U,f.listings());
    EXPECT_THROW((void)pending->take(),System::InvalidOperationException);
}
TEST(OnlineSessionPreparationTest, FailedRelayClosesResourcesAndRollsBackBeforeCompletion) {
    Fixture f;f.observations->status=[] {RelayTransportStatus value;value.state=RelayTransportState::Failed;value.error="secret-fixture";return value;};
    auto pending=f.begin();f.pump();refused([&]{(void)pending->take();},"RELAY_TRANSPORT_UNAVAILABLE");
    EXPECT_EQ(0U,f.listings());EXPECT_EQ(1,f.observations->destroyed);
}
TEST(OnlineSessionPreparationTest, ReadinessTimeoutClosesResourcesAndRollsBackMembership) {
    Fixture f;f.observations->status=[] {return RelayTransportStatus{};};
    auto pending=f.begin();f.pump();EXPECT_TRUE(pending->complete());
    refused([&]{(void)pending->take();},"RELAY_TRANSPORT_UNAVAILABLE");
    EXPECT_EQ(0U,f.listings());EXPECT_EQ(1,f.observations->destroyed);
}
TEST(OnlineSessionPreparationTest, CancellationDuringReadinessCannotPublishOrCallTarget) {
    Fixture f;auto pending=f.begin();f.observations->status=[&] {
        pending->cancel();RelayTransportStatus value;value.state=RelayTransportState::Ready;return value;
    };
    f.pump();EXPECT_TRUE(pending->complete());EXPECT_EQ(0,f.observations->callbacks);
    refused([&]{(void)pending->take();},"OPERATION_CANCELED");EXPECT_EQ(0U,f.listings());EXPECT_EQ(1,f.observations->destroyed);
}
TEST(OnlineSessionPreparationTest, OwnerCancellationRacesActiveWorkerWithoutDanglingTarget) {
    Fixture f;std::mutex mutex;std::condition_variable condition;bool entered=false,resume=false;
    f.observations->status=[&] {
        std::unique_lock lock(mutex);entered=true;condition.notify_one();condition.wait(lock,[&]{return resume;});
        RelayTransportStatus value;value.state=RelayTransportState::Ready;return value;
    };
    auto pending=f.begin();std::vector<Service::BackendEvent> events;
    std::jthread worker([&]{events=f.backend->pump();});
    bool reached=false;
    {std::unique_lock lock(mutex);reached=condition.wait_for(lock,std::chrono::seconds(5),[&]{return entered;});}
    pending.reset();{std::lock_guard lock(mutex);resume=true;}condition.notify_one();worker.join();
    ASSERT_TRUE(reached);
    for(auto& event:events)if(event.completion)event.completion();
    EXPECT_EQ(0U,f.listings());EXPECT_EQ(0,f.observations->callbacks);EXPECT_EQ(1,f.observations->destroyed);
}
TEST(OnlineSessionPreparationTest, JoinedLeaseReleaseRemovesOnlyItsOwnLocalMachine) {
    Fixture f;auto& directory=f.backend->sessionDirectory();
    const auto host=directory.create("c",{"c","d"},f.request.kind,f.request.settings);
    f.request.operation=OnlineSessionRequest::Operation::Join;f.request.session=host.session;
    auto pending=f.begin();f.pump();auto lease=pending->take();
    EXPECT_NE(lease->snapshot().machine,host.machine);EXPECT_EQ(4,lease->snapshot().currentGamers);
    EXPECT_TRUE(lease->release());EXPECT_EQ(2,directory.get("c",host.session).currentGamers);
    EXPECT_EQ(host.machine,directory.get("d",host.session).machine);
    EXPECT_EQ(1U,directory.find("a",f.request.kind,1,{},0,32).sessions.size());
}
TEST(OnlineSessionPreparationTest, LostAuthenticationStillClosesTransportAndReportsFailedLeave) {
    Fixture f;auto pending=f.begin();f.pump();auto lease=pending->take();f.backend->signOut(0);f.pump();
    EXPECT_FALSE(lease->release());EXPECT_FALSE(lease->release());EXPECT_EQ(1,f.observations->destroyed);
}
TEST(OnlineSessionPreparationTest, ThrowingOwnerCallbackDoesNotLosePreparedOwnership) {
    Fixture f;OnlineSessionPreparation pending(f.backend,f.request,[]{throw std::runtime_error("callback fixture");},f.dependencies);
    auto events=f.backend->pump();ASSERT_EQ(1U,events.size());EXPECT_THROW(events[0].completion(),std::runtime_error);
    EXPECT_TRUE(pending.complete());auto lease=pending.take();EXPECT_TRUE(lease->release());EXPECT_EQ(0U,f.listings());
}

#endif

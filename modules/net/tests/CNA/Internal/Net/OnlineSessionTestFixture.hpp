// SPDX-License-Identifier: MS-PL
#pragma once
// Shared deterministic fixture: fake directory accounts, real loopback ENet and explicit routes.
#include <gtest/gtest.h>
#include "../../../../src/Internal/OnlineSessionBinding.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/Internal/GamerServices/ServiceInvitations.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkGamer.hpp"
#include "System/IServiceProvider.hpp"
#include <algorithm>
#include <array>
#include <map>
#include <thread>

namespace OnlineSessionTesting {
using namespace CNA::Internal::Net;
namespace Service=CNA::Internal::GamerServices;
using namespace Microsoft::Xna::Framework::GamerServices;
using Microsoft::Xna::Framework::Net::NetworkSession;
using Microsoft::Xna::Framework::Net::NetworkSessionProperties;

struct Provider final : System::IServiceProvider {void* GetService(const std::type_info&)const override{return nullptr;}};
struct Portal {std::map<std::string,std::uint16_t> ports;RelayTransportState status=RelayTransportState::Ready;int reconnects=0;};
// Deterministic stand-in for the verified relay: real loopback ENet, routes published through a portal.
class Transport final : public IPreparedOnlineTransport {
public:
    Transport(std::shared_ptr<Portal> portal,std::string machine):host_(ENetHostHandle::CreateRelayHost()),portal_(std::move(portal)),machine_(std::move(machine)) {
        portal_->ports[machine_]=host_.getBoundPortProperty();
    }
    ~Transport()override{portal_->ports.erase(machine_);}
    ENetHostHandle& host()override{return host_;}
    RelayTransport* relay()override{return nullptr;}
    RelayTransportStatus status()const override{RelayTransportStatus value;value.state=portal_->status;return value;}
    void reconnect(Service::ServiceRelayTicket ticket)override{EXPECT_EQ(machine_,ticket.machine);++portal_->reconnects;portal_->status=RelayTransportState::Ready;}
private:
    ENetHostHandle host_;
    std::shared_ptr<Portal> portal_;
    std::string machine_;
};

class OnlineSessionTest : public ::testing::Test {
protected:
    void SetUp() override {
        previous_=Service::backend();
        std::vector<Service::ServiceIdentity> identities;
        for(auto [id,tag]:{std::pair{"a","Alice"},{"b","Bob"},{"c","Charlie"},{"d","Dana"}}) {
            Service::ServiceIdentity identity;identity.userId=id;identity.gamertag=tag;identity.allowOnlineSessions=true;
            identities.push_back(identity);
        }
        Service::ServiceLeaderboardFixture board;board.key="BestScoreLifeTime";
        Service::ServiceLeaderboardFixture kills;kills.key="Kills";kills.arbitrated=true;
        service=Service::makeFakeBackend(std::move(identities),{},{board,kills});Service::setBackendForTesting(service);
        Service::resetInvitationsForTesting();
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
        Guide::ResetPendingMessageBoxForTestingEXT();Service::resetInvitationsForTesting();
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
    long long rating(const std::string& tag,const std::string& key="BestScoreLifeTime") {
        const auto page=service->readLeaderboard(key,0,0,10,"",std::vector<std::string>{tag});
        return page.entries.empty()?-1:page.entries.front().rating;
    }
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

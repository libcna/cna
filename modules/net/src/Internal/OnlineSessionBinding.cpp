// SPDX-License-Identifier: MS-PL
#include "OnlineSessionBinding.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPrivilegeException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/NetworkException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/NetworkNotAvailableException.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionJoinException.hpp"
#include "System/ArgumentException.hpp"
#include "System/InvalidOperationException.hpp"
#include "CNA/Internal/GamerServices/ServiceInvitations.hpp"
#include <algorithm>
#include <mutex>

namespace CNA::Internal::Net {
namespace {
using namespace GamerServices;
using Microsoft::Xna::Framework::Net::LocalNetworkGamer;
using Microsoft::Xna::Framework::Net::NetworkGamer;
using Microsoft::Xna::Framework::Net::NetworkMachine;
using Microsoft::Xna::Framework::Net::NetworkSession;
using Microsoft::Xna::Framework::Net::NetworkSessionEndReason;
using Microsoft::Xna::Framework::Net::NetworkSessionJoinError;
using Microsoft::Xna::Framework::Net::NetworkSessionJoinException;
std::mutex fixtureMutex;
OnlineSessionFixture fixtureValue;
NetworkSessionState publicState(ServiceSessionState state) {
    return state==ServiceSessionState::Playing?NetworkSessionState::Playing:NetworkSessionState::Lobby;
}
bool sameSettings(const ServiceSessionSettings& left,const ServiceSessionSettings& right) {
    return left.maxGamers==right.maxGamers&&left.privateSlots==right.privateSlots&&left.state==right.state
        &&left.allowJoinInProgress==right.allowJoinInProgress&&left.properties==right.properties;
}
const ServiceSessionMember* member(const ServiceSessionSnapshot& snapshot,unsigned char id) {
    for(const auto& row:snapshot.members)if(row.ordinal+1==id)return &row;
    return nullptr;
}
}
void setOnlineSessionFixtureForTesting(OnlineSessionFixture fixture) {
    std::lock_guard lock(fixtureMutex);fixtureValue=std::move(fixture);
}
OnlineSessionFixture onlineSessionFixture() {
    std::lock_guard lock(fixtureMutex);return fixtureValue;
}
void throwOnlineEndFailure(std::exception_ptr error,bool joining) {
    using Microsoft::Xna::Framework::GamerServices::GamerPrivilegeException;
    using Microsoft::Xna::Framework::GamerServices::NetworkException;
    using Microsoft::Xna::Framework::GamerServices::NetworkNotAvailableException;
    try {std::rethrow_exception(error);}
    catch(const ServiceOperationError& failure) {
        // Kernel result families from the managed NetworkSessionErrorHandler/ErrorHandler.
        const auto& code=failure.code;
        if(joining&&code=="SESSION_FULL")
            throw NetworkSessionJoinException("The network session is full.",NetworkSessionJoinError::SessionFull);
        if(joining&&(code=="NOT_FOUND"||code=="HOST_ENDED_SESSION"||code=="JOIN_TIMED_OUT"))
            throw NetworkSessionJoinException("The requested network session could not be found.",NetworkSessionJoinError::SessionNotFound);
        if(joining&&code=="INVALID_STATE")
            throw NetworkSessionJoinException("The network session is not joinable.",NetworkSessionJoinError::SessionNotJoinable);
        if(code=="INVALID_STATE")
            throw System::InvalidOperationException("A local gamer already belongs to an online network session.");
        if(code=="NOT_AUTHORIZED"||code=="UNAUTHENTICATED")
            throw GamerPrivilegeException("A local gamer is not signed in with the privilege this network session requires.",error);
        if(code=="NOT_SUPPORTED")throw NetworkNotAvailableException("The CNA online session service is not available.",error);
        throw NetworkException("A network communication failure prevented the network session from starting.",error);
    }
}

OnlineSessionBinding::OnlineSessionBinding(NetworkSession& session,EstablishedOnlineSession established,std::vector<std::string> localUsers)
    :session_(session),engine_(std::move(established.engine)),users_(std::move(localUsers)) {
    auto ready=std::find_if(established.observations.begin(),established.observations.end(),
        [](const auto& value){return value.type==ServiceENetObservation::Type::Ready;});
    if(!engine_||ready==established.observations.end()||!ready->snapshot)throw ServiceOperationError("INVALID_RESPONSE");
    project(*ready);
    for(auto item=std::next(ready);item!=established.observations.end();++item)convert(std::move(*item));
}
OnlineSessionBinding::~OnlineSessionBinding(){close();}
std::shared_ptr<NetworkMachine> OnlineSessionBinding::machine(const std::string& id) {
    auto& shared=machines_[id];
    if(!shared)shared=std::make_shared<NetworkMachine>(NetworkMachine::CreateInternal());
    return shared;
}
void OnlineSessionBinding::project(const ServiceENetObservation& ready) {
    snapshot_=*ready.snapshot;host_=snapshot_.machine==snapshot_.hostMachine;
    const auto locals=session_.localGamers_.getCountProperty();
    if(locals<1||static_cast<std::size_t>(locals)!=users_.size()||ready.ids.size()!=users_.size())
        throw ServiceOperationError("INVALID_RESPONSE");
    // Local gamers already share the constructor's machine; register it under this machine's service ID.
    machines_[snapshot_.machine]=session_.localGamers_[0]->GetSharedMachine();
    NetworkGamer* hostGamer=nullptr;
    for(int index=0;index<locals;++index) {
        auto* gamer=session_.localGamers_[index];const auto id=ready.ids[static_cast<std::size_t>(index)];
        const auto* row=member(snapshot_,id);
        if(!row||row->machine!=snapshot_.machine||row->userId!=users_[static_cast<std::size_t>(index)]||gamers_.contains(id))
            throw ServiceOperationError("INVALID_RESPONSE");
        gamer->SetId(id);gamer->SetIsPrivateSlot(row->privateSlot);
        gamer->SetIsHost(host_&&row->userId==snapshot_.hostId);
        if(gamer->getIsHostProperty())hostGamer=gamer;
        gamers_[id]=gamer;
    }
    for(const auto& entry:ready.gamers) {
        auto* gamer=addRemote(entry);
        session_.remoteGamers_.Add(gamer);session_.allGamers_.Add(gamer);
        if(entry.IsHost)hostGamer=gamer;
    }
    session_.OrderGamersInternal();
    if(!hostGamer||(host_!=hostGamer->getIsLocalProperty()))throw ServiceOperationError("INVALID_RESPONSE");
    session_.host_=hostGamer;session_.isHost_=host_;
    apply(snapshot_,true);
    desiredState_=observedState_=snapshot_.state;
    if(host_)requested_=desired();
    // Guide invitations target the session this process currently belongs to.
    setActiveOnlineSession(ActiveOnlineSession{snapshot_.session,snapshot_.kind,users_,engine_->origin()});
}
NetworkGamer* OnlineSessionBinding::addRemote(const RosterEntry& entry) {
    const auto* row=member(snapshot_,entry.WireId);
    if(!row||row->machine==snapshot_.machine||row->gamertag!=entry.Gamertag||gamers_.contains(entry.WireId))
        throw ServiceOperationError("INVALID_RESPONSE");
    std::unique_ptr<NetworkGamer> owned(new NetworkGamer(NetworkGamer::CreateInternal(&session_,entry.Gamertag)));
    auto* gamer=owned.get();remote_.push_back(std::move(owned));
    gamer->SetId(entry.WireId);gamer->SetIsHost(entry.IsHost);gamer->SetIsPrivateSlot(row->privateSlot);
    // Service identity makes its leaderboard writer a service writer instead of the offline store.
    GamerAccess::setUserId(*gamer,row->userId);
    rememberRecentPlayer(entry.Gamertag);
    auto shared=machine(row->machine);gamer->SetSharedMachine(shared);shared->AddGamerInternal(gamer);
    gamers_[entry.WireId]=gamer;return gamer;
}
void OnlineSessionBinding::apply(const ServiceSessionSnapshot& value,bool initial) {
    snapshot_=value;
    for(const auto& [id,gamer]:gamers_)if(const auto* row=member(snapshot_,id))gamer->SetIsPrivateSlot(row->privateSlot);
    // The host authors these settings; an older snapshot must not revert newer local values.
    if(host_&&!initial)return;
    session_.maxGamers_=value.maxGamers;session_.privateGamerSlots_=value.privateSlots;
    session_.allowJoinInProgress_=value.allowJoinInProgress;
    Microsoft::Xna::Framework::Net::NetworkSessionProperties properties;
    for(std::size_t index=0;index<value.properties.size();++index)properties.setItem(static_cast<int>(index),value.properties[index]);
    session_.SetSessionPropertiesFromTransport(std::move(properties));
    if(initial){session_.sessionState_=publicState(value.state);return;}
    if(value.state==observedState_)return;
    observedState_=value.state;
    NetworkSession::NetworkEvent event;event.Type=NetworkSession::NetworkEventType::StateChange;
    event.State=publicState(value.state);session_.SendNetworkEvent(std::move(event));
}
void OnlineSessionBinding::convert(ServiceENetObservation observation) {
    using Type=ServiceENetObservation::Type;
    switch(observation.type) {
        case Type::Joined:
            for(const auto& entry:observation.gamers) {
                if(gamers_.contains(entry.WireId))continue;
                if(session_.allGamers_.getCountProperty()>=session_.maxGamers_){end("INVALID_RESPONSE");return;}
                session_.AddRemoteGamer(addRemote(entry));
            }
            break;
        case Type::Left:
            for(auto id:observation.ids) {
                auto found=gamers_.find(id);
                if(found==gamers_.end()||found->second->getIsLocalProperty())continue;
                auto* gamer=found->second;gamers_.erase(found);
                gamer->GetSharedMachine()->RemoveGamerInternal(gamer);
                session_.RemoveGamer(gamer,NetworkSessionEndReason::Disconnected);
            }
            break;
        case Type::Data: {
            if(!observation.data)break;
            auto sender=gamers_.find(observation.data->SenderWireId),target=gamers_.find(observation.data->TargetWireId);
            if(sender==gamers_.end()||target==gamers_.end()||!target->second->getIsLocalProperty())break;
            NetworkSession::NetworkEvent event;event.Type=NetworkSession::NetworkEventType::PacketSend;
            event.Gamer=target->second;event.Sender=sender->second;event.Packet=std::move(observation.data->Payload);
            event.Reliable=observation.data->Options;session_.SendNetworkEvent(std::move(event));
            break;
        }
        case Type::Snapshot:
            if(observation.snapshot)apply(*observation.snapshot,false);
            break;
        case Type::Readiness:
            if(session_.sessionState_!=NetworkSessionState::Lobby)break;
            for(const auto& entry:observation.readiness) {
                auto found=gamers_.find(entry.WireId);
                if(found!=gamers_.end())NetworkSession::ApplyGamerReadyInternal(*found->second,entry.IsReady);
            }
            break;
        case Type::Failed:
            end(observation.failure);
            break;
        case Type::Ready:
            break;
    }
}
void OnlineSessionBinding::end(const std::string& failure) {
    if(ended_)return;ended_=true;
    auto reason=NetworkSessionEndReason::Disconnected;
    if(failure=="HOST_ENDED_SESSION"||failure=="NOT_FOUND")reason=NetworkSessionEndReason::HostEndedSession;
    else if(failure=="UNAUTHENTICATED"||failure=="NOT_AUTHORIZED")reason=NetworkSessionEndReason::ClientSignedOut;
    else if(failure=="REMOVED_BY_HOST")reason=NetworkSessionEndReason::RemovedByHost;
    NetworkSession::NetworkEvent event;event.Type=NetworkSession::NetworkEventType::StateChange;
    event.State=NetworkSessionState::Ended;event.Reason=reason;session_.SendNetworkEvent(std::move(event));
}
ServiceSessionSettings OnlineSessionBinding::desired() const {
    ServiceSessionSettings value;value.maxGamers=session_.maxGamers_;value.privateSlots=session_.privateGamerSlots_;
    value.state=desiredState_;value.allowJoinInProgress=session_.allowJoinInProgress_;
    for(std::size_t index=0;index<value.properties.size();++index)
        value.properties[index]=session_.sessionProperties_.getItem(static_cast<int>(index));
    return value;
}
void OnlineSessionBinding::pump() {
    if(ended_||!engine_)return;
    std::vector<ServiceENetObservation> observations;
    try {
        if(host_) {
            auto want=desired();
            if(!requested_||!sameSettings(*requested_,want)){engine_->publish(want);requested_=want;}
        }
        observations=engine_->update();
    }catch(const ServiceOperationError& error){end(error.code);return;}
    for(auto& observation:observations){convert(std::move(observation));if(ended_)break;}
    if(ended_)return;
    const auto [sent,received]=engine_->traffic();
    if(traffic_.sample(std::chrono::steady_clock::now(),sent,received))
        session_.SetTrafficFromTransport(traffic_.sentPerSecond(),traffic_.receivedPerSecond());
    for(const auto& [id,milliseconds]:engine_->roundTrips()) {
        auto found=gamers_.find(id);
        if(found!=gamers_.end()&&!found->second->getIsLocalProperty())
            found->second->SetRoundtripTime(System::TimeSpan::FromMilliseconds(static_cast<double>(milliseconds)));
    }
}
void OnlineSessionBinding::send(NetworkGamer* sender,NetworkGamer* target,const std::vector<SharpRuntime::bytecs>& payload,SendDataOptions options) {
    if(ended_||!engine_)return;
    // A recipient that left between SendData and this Update simply receives nothing.
    auto found=gamers_.find(target->getIdProperty());
    if(found==gamers_.end()||found->second!=target||!gamers_.contains(sender->getIdProperty()))return;
    try {engine_->send(sender->getIdProperty(),target->getIdProperty(),payload,options);}
    catch(const ServiceOperationError& error) {
        if(error.code=="LIMIT_EXCEEDED")throw Microsoft::Xna::Framework::GamerServices::NetworkException("The network packet queue is full.");
        if(error.code=="INVALID_ARGUMENT")throw System::ArgumentException("data");
        // Any other refusal races a transport failure that the next observation reports as SessionEnded.
    }
}
void OnlineSessionBinding::publishReady(const std::vector<NetworkGamer*>& gamers) {
    if(ended_||!engine_)return;
    std::vector<GamerReadyEntry> entries;
    for(auto* gamer:gamers) {
        auto found=gamers_.find(gamer->getIdProperty());
        if(found!=gamers_.end()&&found->second==gamer)entries.push_back(GamerReadyEntry{gamer->getIdProperty(),gamer->getIsReadyProperty()});
    }
    // A transport failure is reported as SessionEnded by the next observation.
    try{engine_->publishReady(entries);}catch(const ServiceOperationError&){}
}
void OnlineSessionBinding::removeMachine(NetworkGamer* gamer) {
    if(ended_||!engine_)return;
    const auto* row=member(snapshot_,gamer->getIdProperty());
    if(!row||row->machine==snapshot_.machine)return;
    // A refusal races a transport failure that the next observation reports as SessionEnded.
    try{engine_->removeMachine(row->machine);}catch(const ServiceOperationError&){}
}
void OnlineSessionBinding::requestState(NetworkSessionState state) {
    desiredState_=state==NetworkSessionState::Playing?ServiceSessionState::Playing:ServiceSessionState::Lobby;
}
void OnlineSessionBinding::close() noexcept {
    if(const auto& active=activeOnlineSession();active&&active->session==snapshot_.session)setActiveOnlineSession(std::nullopt);
    engine_.reset();
}
}

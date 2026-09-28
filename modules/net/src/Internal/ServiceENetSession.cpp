// SPDX-License-Identifier: MS-PL
#include "ServiceENetSession.hpp"
#include "ServiceGamePacketPolicy.hpp"
#include "ServiceSessionPump.hpp"
#include "RelayEnetPolicy.hpp"
#include "CnaService/Protocol.hpp"
#include "System/InvalidOperationException.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <thread>

namespace CNA::Internal::Net {
namespace {
using namespace GamerServices;
using Time=std::chrono::steady_clock::time_point;
[[noreturn]] void invalid(){throw ServiceOperationError("INVALID_ARGUMENT");}
struct PacketDelete {void operator()(ENetPacket* value)const{if(value)enet_packet_destroy(value);}};
struct OutgoingBudget {std::size_t dataBytes=0,dataCount=0,controlBytes=0,controlCount=0;};
struct OutgoingAllocation {std::shared_ptr<OutgoingBudget> budget;std::size_t bytes;bool data;};
void releasedPacket(ENetPacket* packet) {
    std::unique_ptr<OutgoingAllocation> value(static_cast<OutgoingAllocation*>(packet->userData));
    if(value->data){value->budget->dataBytes-=value->bytes;--value->budget->dataCount;}
    else{value->budget->controlBytes-=value->bytes;--value->budget->controlCount;}
}
}
struct ServiceENetSession::Impl {
    struct Peer {std::string machine;bool admitted=false;Time deadline;};
    std::unique_ptr<PreparedOnlineSession> lease;
    std::unique_ptr<ServiceSessionPump> control;
    ServiceSessionSnapshot current;
    std::unique_ptr<ServiceRoster> roster;
    std::unique_ptr<ServiceGamePacketPolicy> policy;
    std::vector<std::string> locals;
    std::vector<unsigned char> localIds;
    ServiceENetDependencies dependencies;
    std::thread::id owner=std::this_thread::get_id();
    ENetAddress loopback{};
    ENetPeer* upstream=nullptr;
    std::map<ENetPeer*,Peer> peers;
    std::map<unsigned char,RosterEntry> remoteGamers;
    std::vector<ServiceENetObservation> observations;
    std::shared_ptr<OutgoingBudget> outgoing=std::make_shared<OutgoingBudget>();
    std::size_t queuedBytes=0,queuedData=0;
    std::uint64_t rejected=0;
    bool host=false,ready=false,stopped=false,recoverRoster=false;
    Time deadline,nextHello;

    Impl(std::unique_ptr<PreparedOnlineSession> prepared,std::vector<std::string> names,ServiceENetDependencies providers)
        :lease(std::move(prepared)),locals(std::move(names)),dependencies(std::move(providers)) {
        if(!lease || locals.empty() || locals.size()>4)invalid();
        current=lease->snapshot();roster=std::make_unique<ServiceRoster>(current);
        policy=std::make_unique<ServiceGamePacketPolicy>(current);localIds=roster->idsFor(current.machine,locals);
        if(!lease->transport().host().IsValid() || lease->transport().status().state!=RelayTransportState::Ready)
            throw ServiceOperationError("RELAY_TRANSPORT_UNAVAILABLE");
        auto* relay=lease->transport().relay();
        if(!dependencies.setRoutes){if(!relay)invalid();dependencies.setRoutes=[relay](const auto& machines){relay->setRoutes(machines);};}
        if(!dependencies.routePort){if(!relay)invalid();dependencies.routePort=[relay](const auto& machine){return relay->routePort(machine);};}
        if(!dependencies.clock)dependencies.clock=[]{return std::chrono::steady_clock::now();};
        if(enet_address_set_host_ip(&loopback,"127.0.0.1")!=0)throw ServiceOperationError("GAME_TRANSPORT_UNAVAILABLE");
        auto actor=std::find_if(current.members.begin(),current.members.end(),[&](const auto& row){return row.machine==current.machine&&row.gamertag==locals.front();});
        if(actor==current.members.end())invalid();control=std::make_unique<ServiceSessionPump>(lease->backend(),actor->userId,current,dependencies.clock);
        dependencies.setRoutes(roster->remoteMachines());host=current.machine==current.hostMachine;
        deadline=now()+std::chrono::seconds(10);nextHello=now();
        if(host){ready=true;ServiceENetObservation event;event.type=ServiceENetObservation::Type::Ready;event.ids=localIds;event.snapshot=current;emit(std::move(event));}
        else {
            const auto port=dependencies.routePort(current.hostMachine);if(!port)throw ServiceOperationError("GAME_TRANSPORT_UNAVAILABLE");
            upstream=transport().Connect("127.0.0.1",port,2);
            if(!upstream)throw ServiceOperationError("GAME_TRANSPORT_UNAVAILABLE");
            peers.emplace(upstream,Peer{current.hostMachine,false,deadline});
        }
    }
    ~Impl() {
        if(control)control->cancel();
        if(lease) {
            try {for(const auto& [peer,value]:peers)transport().Disconnect(peer,0);transport().Flush();}catch(...){}
            (void)lease->release();
        }
    }
    void checkOwner()const{if(owner!=std::this_thread::get_id())throw System::InvalidOperationException("Service ENet requires its owner thread.");}
    Time now()const{return dependencies.clock();}
    ENetHostHandle& transport(){return lease->transport().host();}
    std::string machineFor(unsigned char id)const {
        for(const auto& row:current.members)if(row.ordinal+1==id)return row.machine;return {};
    }
    std::vector<std::string> admitted()const {
        std::set<std::string> machines;
        if(host){for(const auto& [peer,value]:peers)if(value.admitted)machines.insert(value.machine);}
        else {for(const auto& [id,row]:remoteGamers)machines.insert(machineFor(id));}
        return {machines.begin(),machines.end()};
    }
    std::string source(const ENetAddress& address)const {
        if(address.host!=loopback.host)return {};
        std::string result;
        for(const auto& machine:roster->remoteMachines())if(dependencies.routePort(machine)==address.port&&address.port) {
            if(!result.empty())return {};result=machine;
        }
        return result;
    }
    void emit(ServiceENetObservation event) {
        if(observations.size()>=256 || (event.data&&(queuedData>=128||queuedBytes+event.data->Payload.size()>MaxRelayWaitingBytes))) {
            ++rejected;return;
        }
        if(event.data){queuedBytes+=event.data->Payload.size();++queuedData;}observations.push_back(std::move(event));
    }
    void fail(const std::string& code) {
        if(stopped)return;stopped=true;ready=false;control->cancel();
        ServiceENetObservation event;event.type=ServiceENetObservation::Type::Failed;event.failure=code;emit(std::move(event));
    }
    bool transmit(ENetPeer* peer,const std::vector<unsigned char>& bytes,SendDataOptions options=SendDataOptions::Reliable) {
        const bool data=!bytes.empty()&&bytes.front()==static_cast<unsigned char>(MessageTag::AppData);
        if((data&&(outgoing->dataCount>=128||outgoing->dataBytes+bytes.size()>MaxRelayWaitingBytes))
            ||(!data&&(outgoing->controlCount>=64||outgoing->controlBytes+bytes.size()>64*4096))){++rejected;return false;}
        const unsigned char channel=options==SendDataOptions::None||options==SendDataOptions::InOrder?1:0;
        auto allocation=std::make_unique<OutgoingAllocation>(OutgoingAllocation{outgoing,bytes.size(),data});
        auto* packet=enet_packet_create(bytes.data(),bytes.size(),NetPacketCodec::SendDataOptionsToEnetFlags(options));
        if(!packet)throw ServiceOperationError("GAME_TRANSPORT_UNAVAILABLE");
        if(data){outgoing->dataBytes+=bytes.size();++outgoing->dataCount;}
        else{outgoing->controlBytes+=bytes.size();++outgoing->controlCount;}
        packet->userData=allocation.release();packet->freeCallback=releasedPacket;
        if(enet_peer_send(peer,channel,packet)<0){enet_packet_destroy(packet);throw ServiceOperationError("GAME_TRANSPORT_UNAVAILABLE");}
        return true;
    }
    void hello() {if(upstream&&upstream->state==ENET_PEER_STATE_CONNECTED){transmit(upstream,NetPacketCodec::Encode(ClientHelloMessage{locals}));nextHello=now()+std::chrono::milliseconds(500);}}
    void add(const std::vector<RosterEntry>& rows,bool initial=false) {
        ServiceENetObservation event;event.type=ServiceENetObservation::Type::Joined;
        for(const auto& row:rows)if(!remoteGamers.contains(row.WireId)){remoteGamers.emplace(row.WireId,row);event.gamers.push_back(row);}
        if(!initial&&!event.gamers.empty())emit(std::move(event));
    }
    void remove(const std::vector<unsigned char>& ids,bool broadcast) {
        GamerLeaveBroadcastMessage leave;
        for(auto id:ids)if(remoteGamers.erase(id))leave.WireIds.push_back(id);
        if(leave.WireIds.empty())return;
        ServiceENetObservation event;event.type=ServiceENetObservation::Type::Left;event.ids=leave.WireIds;emit(std::move(event));
        if(broadcast){const auto bytes=NetPacketCodec::Encode(leave);for(const auto& [peer,value]:peers)if(value.admitted)transmit(peer,bytes);}
    }
    void apply(ServiceSessionSnapshot value) {
        auto next=std::make_unique<ServiceRoster>(value);
        if(value.hostId!=current.hostId||value.hostMachine!=current.hostMachine||next->idsFor(value.machine,locals)!=localIds)
            throw ServiceOperationError("INVALID_RESPONSE");
        std::set<std::string> changed;
        for(const auto& row:current.members)if(row.machine!=current.machine) {
            auto replacement=std::find_if(value.members.begin(),value.members.end(),[&](const auto& other){return other.userId==row.userId
                &&other.gamertag==row.gamertag&&other.machine==row.machine&&other.ordinal==row.ordinal;});
            if(replacement==value.members.end())changed.insert(row.machine);
        }
        if(!host&&changed.contains(current.hostMachine)){fail("HOST_ENDED_SESSION");return;}
        for(auto it=peers.begin();it!=peers.end();) {
            if(changed.contains(it->second.machine)){transport().Disconnect(it->first,0);it=peers.erase(it);}else ++it;
        }
        std::vector<unsigned char> departed;for(const auto& [id,row]:remoteGamers)if(changed.contains(machineFor(id)))departed.push_back(id);
        remove(departed,host);
        const bool revised=value.revision!=current.revision;
        const bool stateChanged=value.state!=current.state;
        const bool settingsChanged=value.properties!=current.properties||value.maxGamers!=current.maxGamers
            ||value.privateSlots!=current.privateSlots||value.allowJoinInProgress!=current.allowJoinInProgress;
        current=std::move(value);roster=std::move(next);
        policy=std::make_unique<ServiceGamePacketPolicy>(current);dependencies.setRoutes(roster->remoteMachines());
        if(host)identify();
        if(revised){ServiceENetObservation event;event.type=ServiceENetObservation::Type::Snapshot;event.snapshot=current;emit(std::move(event));
            if(!host&&ready){recoverRoster=true;nextHello=now();}}
        if(host)hint(stateChanged,settingsChanged);
    }
    // Admitted clients treat these broadcasts only as a prompt to reread directory authority.
    void hint(bool state,bool settings) {
        std::vector<std::vector<unsigned char>> messages;
        if(state)messages.push_back(NetPacketCodec::Encode(StateChangeBroadcastMessage{current.state==ServiceSessionState::Playing
            ?NetworkSessionState::Playing:NetworkSessionState::Lobby}));
        if(settings) {
            SessionPropertiesBroadcastMessage message;
            for(std::size_t index=0;index<current.properties.size();++index)
                message.SessionProperties.setItem(static_cast<int>(index),current.properties[index]);
            messages.push_back(NetPacketCodec::Encode(message));
        }
        for(const auto& bytes:messages)for(const auto& [peer,value]:peers)if(value.admitted)transmit(peer,bytes);
    }
    void connect(ENetPeer* peer) {
        const auto machine=source(peer->address);
        if(host&&machine.empty()&&peers.size()<static_cast<std::size_t>(CnaService::MaxSessionGamers-1)) {
            // A newly joined machine can connect before this host's next authoritative read names
            // its route. Keep it unidentified (no hello is accepted) under the admission deadline.
            peers.try_emplace(peer,Peer{{},false,now()+std::chrono::seconds(10)});control->expedite();return;
        }
        if(machine.empty()||(!host&&(peer!=upstream||machine!=current.hostMachine))){++rejected;transport().Disconnect(peer,0);return;}
        if(host) {
            for(const auto& [other,value]:peers)if(other!=peer&&value.machine==machine){++rejected;transport().Disconnect(peer,0);return;}
            peers.try_emplace(peer,Peer{machine,false,now()+std::chrono::seconds(10)});
        }else hello();
    }
    void identify() {
        for(auto it=peers.begin();it!=peers.end();) {
            if(!it->second.machine.empty()){++it;continue;}
            const auto machine=source(it->first->address);
            const bool duplicate=!machine.empty()&&std::any_of(peers.begin(),peers.end(),[&](const auto& other){return other.second.machine==machine;});
            if(duplicate){++rejected;transport().Disconnect(it->first,0);it=peers.erase(it);continue;}
            it->second.machine=machine;++it;
        }
    }
    void receive(ENetPeer* peer,ENetPacket* packet,unsigned char channel) {
        auto found=peers.find(peer);
        if(found==peers.end()||found->second.machine.empty()||source(peer->address)!=found->second.machine){++rejected;return;}
        const std::span<const unsigned char> bytes(packet->data,packet->dataLength);
        if(bytes.empty()){++rejected;return;}
        if(!host&&(bytes[0]==static_cast<unsigned char>(MessageTag::StateChangeBroadcast)
            ||bytes[0]==static_cast<unsigned char>(MessageTag::SessionPropertiesBroadcast))) {
            // The directory stays authoritative: a bounded, well-formed host hint only expedites a read.
            try{(void)validateServiceControlPacket(bytes);}catch(const CnaService::Error&){++rejected;return;}
            if(channel!=0||found->second.machine!=current.hostMachine||!ready){++rejected;return;}
            control->expedite();return;
        }
        try {
            if(bytes[0]==static_cast<unsigned char>(MessageTag::AppData)) {
                auto message=policy->application(found->second.machine,bytes,channel,ready,admitted());
                const auto target=machineFor(message.TargetWireId);
                if(target==current.machine){ServiceENetObservation event;event.type=ServiceENetObservation::Type::Data;event.data=std::move(message);emit(std::move(event));}
                else {
                    auto destination=std::find_if(peers.begin(),peers.end(),[&](const auto& item){return item.second.admitted&&item.second.machine==target;});
                    if(destination==peers.end()){++rejected;return;}transmit(destination->first,NetPacketCodec::Encode(message),message.Options);
                }
                return;
            }
            auto message=policy->control(found->second.machine,bytes,channel,ready,locals,admitted());
            if(auto* helloMessage=std::get_if<ClientHelloMessage>(&message)) {
                auto connected=admitted();std::erase(connected,found->second.machine);
                const auto welcome=roster->welcomeFor(found->second.machine,helloMessage->LocalGamertags,connected);
                if(!transmit(peer,NetPacketCodec::Encode(welcome)))return;
                if(found->second.admitted)return;
                found->second.admitted=true;GamerJoinBroadcastMessage joined;
                for(const auto& row:current.members)if(row.machine==found->second.machine)
                    joined.NewGamers.push_back({static_cast<unsigned char>(row.ordinal+1),row.gamertag,false});
                add(joined.NewGamers);const auto encoded=NetPacketCodec::Encode(joined);
                for(const auto& [other,value]:peers)if(other!=peer&&value.admitted)transmit(other,encoded);
            }else if(auto* welcome=std::get_if<ServerWelcomeMessage>(&message)) {
                found->second.admitted=true;const bool initial=!ready;ready=true;recoverRoster=false;
                add(welcome->ExistingRoster,initial);
                if(initial){ServiceENetObservation event;event.type=ServiceENetObservation::Type::Ready;event.ids=localIds;
                    event.gamers=welcome->ExistingRoster;event.snapshot=current;emit(std::move(event));}
            }else if(auto* join=std::get_if<GamerJoinBroadcastMessage>(&message))add(join->NewGamers);
            else if(auto* leave=std::get_if<GamerLeaveBroadcastMessage>(&message))remove(leave->WireIds,false);
            // State/properties are published through the directory observation, already validated above.
        }catch(const ServiceOperationError&){fail("GAME_TRANSPORT_UNAVAILABLE");}
        catch(const CnaService::Error&) {
            ++rejected;
            if(!host && found->second.machine==current.hostMachine
                &&(bytes[0]==static_cast<unsigned char>(MessageTag::ServerWelcome)||bytes[0]==static_cast<unsigned char>(MessageTag::GamerJoinBroadcast))) {
                recoverRoster=true;if(now()>=nextHello)hello();
            }
        }
    }
    void disconnect(ENetPeer* peer) {
        auto found=peers.find(peer);if(found==peers.end())return;
        const auto machine=found->second.machine;peers.erase(found);
        if(peer==upstream){upstream=nullptr;fail("HOST_ENDED_SESSION");return;}
        std::vector<unsigned char> departed;
        for(const auto& [id,row]:remoteGamers)if(machineFor(id)==machine)departed.push_back(id);
        remove(departed,host);
    }
    std::vector<ServiceENetObservation> update() {
        checkOwner();if(!stopped) {
            pumpRetainedCompletions(lease->backend());
            const auto status=lease->transport().status();
            if(status.state!=RelayTransportState::Ready)fail("RELAY_TRANSPORT_UNAVAILABLE");
            if(!stopped)if(auto observation=control->update()) {
                if(!observation->failure.empty())fail(observation->failure);
                else try{apply(std::move(*observation->snapshot));}catch(const ServiceOperationError&){fail("INVALID_RESPONSE");}
            }
            if(!stopped) {
                ENetEvent event{};
                for(int count=0;count<64;++count) {
                    const int result=transport().Service(0,event);if(result==0)break;
                    if(result<0){fail("GAME_TRANSPORT_UNAVAILABLE");break;}
                    if(event.type==ENET_EVENT_TYPE_CONNECT)connect(event.peer);
                    else if(event.type==ENET_EVENT_TYPE_RECEIVE){std::unique_ptr<ENetPacket,PacketDelete> packet(event.packet);receive(event.peer,packet.get(),event.channelID);}
                    else if(event.type==ENET_EVENT_TYPE_DISCONNECT)disconnect(event.peer);
                    if(stopped)break;
                }
                if(!stopped&&!host&&(!ready||recoverRoster)&&now()>=nextHello)hello();
                if(!stopped&&!ready&&now()>=deadline)fail("JOIN_TIMED_OUT");
                if(!stopped){for(auto& [peer,value]:peers)if(!value.admitted&&now()>=value.deadline)transport().Disconnect(peer,0);transport().Flush();}
            }
        }
        auto result=std::move(observations);observations.clear();queuedBytes=0;queuedData=0;return result;
    }
    void send(unsigned char sender,unsigned char target,const std::vector<unsigned char>& payload,SendDataOptions options) {
        checkOwner();
        if(payload.size()>MaxRelayGamePacketBytes-4 || static_cast<unsigned char>(options)>static_cast<unsigned char>(SendDataOptions::Chat))invalid();
        if(!ready||stopped)throw ServiceOperationError("INVALID_STATE");
        if(std::find(localIds.begin(),localIds.end(),sender)==localIds.end())throw ServiceOperationError("NOT_AUTHORIZED");
        const bool local=std::find(localIds.begin(),localIds.end(),target)!=localIds.end();
        if(!local&&!remoteGamers.contains(target))throw ServiceOperationError("NOT_AUTHORIZED");
        AppDataMessage message{sender,target,options,payload};
        if(local) {
            if(queuedData>=128||observations.size()>=256||queuedBytes+payload.size()>MaxRelayWaitingBytes)throw ServiceOperationError("LIMIT_EXCEEDED");
            ServiceENetObservation event;event.type=ServiceENetObservation::Type::Data;event.data=std::move(message);emit(std::move(event));return;
        }
        ENetPeer* destination=upstream;
        if(host){const auto machine=machineFor(target);destination=nullptr;for(const auto& [peer,value]:peers)if(value.admitted&&value.machine==machine){destination=peer;break;}}
        if(!destination)throw ServiceOperationError("INVALID_STATE");
        if(!transmit(destination,NetPacketCodec::Encode(message),options))throw ServiceOperationError("LIMIT_EXCEEDED");
        transport().Flush();
    }
};
void ServiceENetSession::publish(const ServiceSessionSettings& settings) {
    impl_->checkOwner();if(!impl_->host)throw ServiceOperationError("NOT_AUTHORIZED");
    if(!impl_->stopped)impl_->control->publish(settings);
}
ServiceENetSession::ServiceENetSession(std::unique_ptr<PreparedOnlineSession> value,std::vector<std::string> names,ServiceENetDependencies dependencies)
    :impl_(std::make_unique<Impl>(std::move(value),std::move(names),std::move(dependencies))) {}
ServiceENetSession::~ServiceENetSession()=default;
std::vector<ServiceENetObservation> ServiceENetSession::update(){return impl_->update();}
void ServiceENetSession::send(unsigned char sender,unsigned char target,const std::vector<unsigned char>& bytes,SendDataOptions options){impl_->send(sender,target,bytes,options);}
bool ServiceENetSession::ready()const{impl_->checkOwner();return impl_->ready;}
const ServiceSessionSnapshot& ServiceENetSession::snapshot()const{impl_->checkOwner();return impl_->current;}
std::uint64_t ServiceENetSession::rejected()const{impl_->checkOwner();return impl_->rejected;}
}

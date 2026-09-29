// SPDX-License-Identifier: MS-PL
#include "ServiceENetSession.hpp"
#include "ServiceGamePacketPolicy.hpp"
#include "ServiceSessionPump.hpp"
#include "RelayEnetPolicy.hpp"
#include "CnaService/Protocol.hpp"
#include "System/InvalidOperationException.hpp"
#include <algorithm>
#include <deque>
#include <map>
#include <set>
#include <mutex>
#include <thread>

namespace CNA::Internal::Net {
namespace {
using namespace GamerServices;
using Time=std::chrono::steady_clock::time_point;
[[noreturn]] void invalid(){throw ServiceOperationError("INVALID_ARGUMENT");}
struct PacketDelete {void operator()(ENetPacket* value)const{if(value)enet_packet_destroy(value);}};
struct OutgoingBudget {std::size_t dataBytes=0,dataCount=0,controlBytes=0,controlCount=0;};
// A relay or directory outage shorter than this is repaired without ending the session; ENet peers
// on relay routes tolerate a slightly longer silence so they are not the first to give up.
constexpr auto RecoveryWindow=std::chrono::seconds(15);
// How long a session whose host vanished waits for the directory to name a new one: past the
// service's 20-second relay grace for a crashed host, plus a read.
constexpr auto MigrationWindow=std::chrono::seconds(30);
constexpr enet_uint32 PeerTimeoutMinimum=20000,PeerTimeoutMaximum=30000;
struct Recovery {std::mutex mutex;bool pending=false;std::optional<ServiceRelayTicket> ticket;std::string refused;};
// An AddLocalGamer in flight on the service executor.
struct Adding {
    std::mutex mutex;bool done=false;
    std::vector<std::string> names,users;
    std::optional<ServiceSessionSnapshot> snapshot;std::string failure;
};
bool transient(const std::string& code){return code=="SESSION_SERVICE_UNAVAILABLE"||code=="RATE_LIMITED";}
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
    // Machines this host asked the directory to remove, disconnected with that reason once gone.
    std::set<std::string> removing;
    // Lobby readiness by gamer ID, as last published or reported; cleared when a game starts or ends.
    std::map<unsigned char,bool> readiness;
    // Client: the host's round trips to the gamers on client machines, as last reported.
    std::map<unsigned char,std::uint16_t> hostRoundtrips;
    // Host: when those round trips are next sent.
    Time nextStats{};
    std::vector<ServiceENetObservation> observations;
    std::shared_ptr<OutgoingBudget> outgoing=std::make_shared<OutgoingBudget>();
    std::size_t queuedBytes=0,queuedData=0;
    std::uint64_t rejected=0;
    bool host=false,ready=false,stopped=false,recoverRoster=false;
    Time deadline,nextHello;
    std::string account;std::vector<std::string> users;
    std::shared_ptr<Recovery> recovery=std::make_shared<Recovery>();
    bool recovering=false,controlFailed=false;
    Time recoveryDeadline,nextTicket,nextControlRetry;
    // Client whose host vanished in a session allowing migration, until the directory names a new one.
    bool migrating=false;
    Time migrationDeadline;
    std::deque<std::shared_ptr<Adding>> adds;

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
        account=actor->userId;
        for(const auto& name:locals)for(const auto& row:current.members)if(row.machine==current.machine&&row.gamertag==name)users.push_back(row.userId);
        dependencies.setRoutes(roster->remoteMachines());host=current.machine==current.hostMachine;
        deadline=now()+std::chrono::seconds(10);nextHello=now();
        if(host){ready=true;ServiceENetObservation event;event.type=ServiceENetObservation::Type::Ready;event.ids=localIds;event.snapshot=current;emit(std::move(event));}
        else {
            const auto port=dependencies.routePort(current.hostMachine);if(!port)throw ServiceOperationError("GAME_TRANSPORT_UNAVAILABLE");
            upstream=transport().Connect("127.0.0.1",port,2);
            if(!upstream)throw ServiceOperationError("GAME_TRANSPORT_UNAVAILABLE");
            enet_peer_timeout(upstream,0,PeerTimeoutMinimum,PeerTimeoutMaximum);
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
    void beginRecovery() {
        if(recovering)return;
        recovering=true;recoveryDeadline=now()+RecoveryWindow;nextTicket=now();
    }
    // Reconnects a failed relay with fresh one-use authority; routes (and ENet peers) are kept.
    void recoverRelay(const RelayTransportStatus& status) {
        beginRecovery();
        std::optional<ServiceRelayTicket> ticket;bool pending=false;std::string refused;
        {std::lock_guard lock(recovery->mutex);ticket=std::move(recovery->ticket);recovery->ticket.reset();pending=recovery->pending;refused=recovery->refused;}
        // Revoked or removed authority is final; only an unreachable service is retried.
        if(!refused.empty()){fail(refused);return;}
        if(ticket&&status.state==RelayTransportState::Failed) {
            try{lease->transport().reconnect(std::move(*ticket));}catch(...){}
            nextTicket=now()+std::chrono::seconds(1);return;
        }
        if(pending||status.state!=RelayTransportState::Failed||now()<nextTicket)return;
        nextTicket=now()+std::chrono::seconds(1);
        auto state=recovery;auto* executor=lease->backend().get();
        {std::lock_guard lock(state->mutex);state->pending=true;}
        try {
            lease->backend()->submit([state,executor,account=account,users=users,session=current.session] {
                std::optional<ServiceRelayTicket> issued;std::string refused;
                try{issued=executor->sessionDirectory().issueRelayTicket(account,users,session);}
                catch(const ServiceOperationError& error) {
                    if(error.code=="NOT_AUTHORIZED"||error.code=="UNAUTHENTICATED"||error.code=="NOT_FOUND"||error.code=="REMOVED_BY_HOST")refused=error.code;
                }catch(...){}
                std::lock_guard lock(state->mutex);state->pending=false;state->ticket=std::move(issued);state->refused=refused;
            },{});
        }catch(...){std::lock_guard lock(state->mutex);state->pending=false;}
    }
    void checkOwner()const{if(owner!=std::this_thread::get_id())throw System::InvalidOperationException("Service ENet requires its owner thread.");}
    Time now()const{return dependencies.clock();}
    ENetHostHandle& transport(){return lease->transport().host();}
    std::string machineFor(unsigned char id)const {
        for(const auto& row:current.members)if(row.ordinal+1==id)return row.machine;return {};
    }
    // The recipient's own gamers are left out: it is their authority, and an echo could undo a change still in flight.
    void sendReadiness(ENetPeer* peer,const std::string& machine) {
        GamerReadyMessage message;
        for(const auto& [id,isReady]:readiness) {
            const auto owner=machineFor(id);
            if(!owner.empty()&&owner!=machine)message.Entries.push_back(GamerReadyEntry{id,isReady});
        }
        if(!message.Entries.empty())transmit(peer,NetPacketCodec::Encode(message));
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
        for(auto id:ids){readiness.erase(id);if(remoteGamers.erase(id))leave.WireIds.push_back(id);}
        if(leave.WireIds.empty())return;
        ServiceENetObservation event;event.type=ServiceENetObservation::Type::Left;event.ids=leave.WireIds;emit(std::move(event));
        if(broadcast){const auto bytes=NetPacketCodec::Encode(leave);for(const auto& [peer,value]:peers)if(value.admitted)transmit(peer,bytes);}
    }
    // The directory's host migration: only in a session that allows it, and only once the old host's
    // machine is gone; every other gamer keeps its ID.
    bool handoverIn(const ServiceSessionSnapshot& value)const {
        if(value.hostMachine==current.hostMachine) {
            if(value.hostId!=current.hostId)throw ServiceOperationError("INVALID_RESPONSE");
            return false;
        }
        if(!current.allowHostMigration&&!value.allowHostMigration)throw ServiceOperationError("INVALID_RESPONSE");
        for(const auto& row:value.members)if(row.machine==current.hostMachine)throw ServiceOperationError("INVALID_RESPONSE");
        return true;
    }
    void awaitHost() {
        migrating=true;migrationDeadline=now()+MigrationWindow;hostRoundtrips.clear();control->expedite();
    }
    // Becomes the host, or reconnects to the new one; the roster and IDs carry over unchanged.
    void takeOver() {
        migrating=false;hostRoundtrips.clear();
        if(upstream){transport().Disconnect(upstream,0);peers.erase(upstream);upstream=nullptr;}
        host=current.machine==current.hostMachine;
        if(host){identify();nextStats=now();}
        else {
            const auto port=dependencies.routePort(current.hostMachine);
            if(port)upstream=transport().Connect("127.0.0.1",port,2);
            if(!upstream){fail("GAME_TRANSPORT_UNAVAILABLE");return;}
            enet_peer_timeout(upstream,0,PeerTimeoutMinimum,PeerTimeoutMaximum);
            peers.emplace(upstream,Peer{current.hostMachine,false,now()+std::chrono::seconds(10)});
            recoverRoster=true;nextHello=now();
        }
        ServiceENetObservation event;event.type=ServiceENetObservation::Type::HostChanged;event.snapshot=current;emit(std::move(event));
    }
    void apply(ServiceSessionSnapshot value) {
        auto next=std::make_unique<ServiceRoster>(value);
        const bool handover=handoverIn(value);
        if(next->idsFor(value.machine,locals)!=localIds)throw ServiceOperationError("INVALID_RESPONSE");
        std::set<std::string> changed;
        for(const auto& row:current.members)if(row.machine!=current.machine) {
            auto replacement=std::find_if(value.members.begin(),value.members.end(),[&](const auto& other){return other.userId==row.userId
                &&other.gamertag==row.gamertag&&other.machine==row.machine&&other.ordinal==row.ordinal;});
            if(replacement==value.members.end())changed.insert(row.machine);
        }
        if(!host&&!handover&&changed.contains(current.hostMachine)){fail("HOST_ENDED_SESSION");return;}
        for(auto it=peers.begin();it!=peers.end();) {
            if(changed.contains(it->second.machine)) {
                // A machine this host removed is told so; one that left needs no reason.
                transport().Disconnect(it->first,removing.erase(it->second.machine)?DisconnectRemovedByHost:0);
                if(it->first==upstream)upstream=nullptr;
                it=peers.erase(it);
            }else ++it;
        }
        std::vector<unsigned char> departed;for(const auto& [id,row]:remoteGamers)if(changed.contains(machineFor(id)))departed.push_back(id);
        remove(departed,host);
        // Host: a machine already playing that grew (its AddLocalGamer) is announced as a whole group.
        std::set<std::string> grown;
        if(host)for(const auto& row:value.members) {
            if(row.machine==value.machine||changed.contains(row.machine))continue;
            const bool known=std::any_of(current.members.begin(),current.members.end(),[&](const auto& other){return other.userId==row.userId;});
            const bool admittedMachine=std::any_of(peers.begin(),peers.end(),[&](const auto& item){return item.second.admitted&&item.second.machine==row.machine;});
            if(!known&&admittedMachine)grown.insert(row.machine);
        }
        const bool revised=value.revision!=current.revision;
        const bool stateChanged=value.state!=current.state;
        if(stateChanged)readiness.clear();
        const bool settingsChanged=value.properties!=current.properties||value.maxGamers!=current.maxGamers
            ||value.privateSlots!=current.privateSlots||value.allowJoinInProgress!=current.allowJoinInProgress;
        current=std::move(value);roster=std::move(next);
        policy=std::make_unique<ServiceGamePacketPolicy>(current);dependencies.setRoutes(roster->remoteMachines());
        if(host)identify();
        if(revised){ServiceENetObservation event;event.type=ServiceENetObservation::Type::Snapshot;event.snapshot=current;emit(std::move(event));
            if(!host&&ready){recoverRoster=true;nextHello=now();}}
        if(handover){takeOver();if(stopped)return;}
        for(const auto& machine:grown)announce(machine);
        if(host)hint(stateChanged,settingsChanged);
    }
    // Host: tells every other admitted machine about a machine's complete group; receivers add the
    // gamers they do not know yet.
    void announce(const std::string& machine) {
        GamerJoinBroadcastMessage joined;
        for(const auto& row:current.members)if(row.machine==machine)
            joined.NewGamers.push_back({static_cast<unsigned char>(row.ordinal+1),row.gamertag,false});
        if(joined.NewGamers.empty())return;
        if(machine!=current.machine)add(joined.NewGamers);
        const auto encoded=NetPacketCodec::Encode(joined);
        for(const auto& [peer,value]:peers)if(value.admitted&&value.machine!=machine)transmit(peer,encoded);
    }
    // Owner thread: completes AddLocalGamer calls the service has answered, oldest first.
    void completeAdds() {
        while(!adds.empty()) {
            auto& request=*adds.front();
            std::optional<ServiceSessionSnapshot> value;std::string failure;
            {std::lock_guard lock(request.mutex);if(!request.done)return;value=std::move(request.snapshot);failure=request.failure;}
            auto names=std::move(request.names);auto accounts=std::move(request.users);adds.pop_front();
            ServiceENetObservation event;
            if(!value){event.type=ServiceENetObservation::Type::AddFailed;event.failure=failure;emit(std::move(event));continue;}
            std::vector<RosterEntry> added;
            for(std::size_t index=0;index<names.size();++index) {
                auto row=std::find_if(value->members.begin(),value->members.end(),[&](const auto& item){return item.userId==accounts[index]&&item.machine==value->machine;});
                if(row==value->members.end()){fail("INVALID_RESPONSE");return;}
                locals.push_back(names[index]);users.push_back(accounts[index]);localIds.push_back(static_cast<unsigned char>(row->ordinal+1));
                added.push_back({static_cast<unsigned char>(row->ordinal+1),names[index],false});
            }
            try{apply(std::move(*value));}catch(const ServiceOperationError&){fail("INVALID_RESPONSE");return;}
            if(stopped)return;
            event.type=ServiceENetObservation::Type::LocalAdded;event.gamers=added;
            for(const auto& entry:added)event.ids.push_back(entry.WireId);
            emit(std::move(event));
            // The host tells the others itself; a client's host reads the directory, and this machine's
            // next hello names its grown group.
            if(host)announce(current.machine);else{recoverRoster=true;nextHello=now();control->expedite();}
        }
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
        enet_peer_timeout(peer,0,PeerTimeoutMinimum,PeerTimeoutMaximum);
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
        if(!host&&bytes[0]==static_cast<unsigned char>(MessageTag::NetworkStatsBroadcast)) {
            try{(void)validateServiceControlPacket(bytes);}catch(const CnaService::Error&){++rejected;return;}
            if(found->second.machine!=current.hostMachine||!ready){++rejected;return;}
            hostRoundtrips.clear();
            for(const auto& entry:NetPacketCodec::DecodeNetworkStats(std::vector<unsigned char>(bytes.begin(),bytes.end())).Entries)
                if(remoteGamers.contains(entry.WireId))hostRoundtrips[entry.WireId]=entry.Milliseconds;
            return;
        }
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
                // Readiness follows every welcome, including a client's re-hello after a directory revision, so a
                // report dropped while that client's directory view lagged behind a new gamer still converges.
                sendReadiness(peer,found->second.machine);
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
            else if(auto* ready=std::get_if<GamerReadyMessage>(&message)) {
                // The XNA session applies a report only in its own Lobby state, which a host reaches at EndGame
                // before the directory records it; gating here on the directory's state would drop that report.
                for(const auto& entry:ready->Entries)readiness[entry.WireId]=entry.IsReady;
                ServiceENetObservation event;event.type=ServiceENetObservation::Type::Readiness;event.readiness=ready->Entries;emit(std::move(event));
                if(host) {
                    // The host passes a client's report on to the other clients.
                    const auto encoded=NetPacketCodec::Encode(*ready);
                    for(const auto& [other,value]:peers)if(other!=peer&&value.admitted)transmit(other,encoded);
                }
            }
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
    void disconnect(ENetPeer* peer,std::uint32_t data) {
        auto found=peers.find(peer);if(found==peers.end())return;
        const auto machine=found->second.machine;peers.erase(found);
        if(peer==upstream) {
            upstream=nullptr;
            if(data==DisconnectRemovedByHost){fail("REMOVED_BY_HOST");return;}
            // With migration allowed the directory, not this disconnect, decides whether the session ends.
            if(current.allowHostMigration){awaitHost();return;}
            fail("HOST_ENDED_SESSION");return;
        }
        std::vector<unsigned char> departed;
        for(const auto& [id,row]:remoteGamers)if(machineFor(id)==machine)departed.push_back(id);
        remove(departed,host);
    }
    // Host: once a second, the round trip to each gamer on a client machine. A client measures its own
    // round trip to this host; one to a gamer on another client, relayed through this host, also
    // needs this host's.
    void publishStats() {
        nextStats=now()+std::chrono::seconds(1);
        NetworkStatsMessage message;
        for(const auto& [peer,value]:peers) {
            if(!value.admitted)continue;
            const auto milliseconds=static_cast<std::uint16_t>(std::min<enet_uint32>(peer->roundTripTime,65535));
            for(const auto& row:current.members)if(row.machine==value.machine)
                message.Entries.push_back({static_cast<unsigned char>(row.ordinal+1),milliseconds});
        }
        if(message.Entries.empty())return;
        const auto bytes=NetPacketCodec::Encode(message);
        for(const auto& [peer,value]:peers)if(value.admitted)transmit(peer,bytes,SendDataOptions::None);
    }
    std::map<unsigned char,std::uint32_t> roundTrips() const {
        std::map<unsigned char,std::uint32_t> result;
        if(host) {
            for(const auto& [peer,value]:peers)if(value.admitted)
                for(const auto& row:current.members)if(row.machine==value.machine)
                    result[static_cast<unsigned char>(row.ordinal+1)]=peer->roundTripTime;
        }else if(upstream&&upstream->state==ENET_PEER_STATE_CONNECTED) {
            for(const auto& [id,row]:remoteGamers) {
                const auto relayed=hostRoundtrips.find(id);
                result[id]=upstream->roundTripTime+(relayed!=hostRoundtrips.end()?relayed->second:0u);
            }
        }
        return result;
    }
    std::vector<ServiceENetObservation> update() {
        checkOwner();if(!stopped) {
            pumpRetainedCompletions(lease->backend());
            // Before the directory read: a read after the add must find this group already grown.
            completeAdds();
            const auto status=lease->transport().status();
            if(status.state!=RelayTransportState::Ready)recoverRelay(status);
            if(!stopped)if(auto observation=control->update()) {
                if(!observation->failure.empty()) {
                    // Service outages are retried inside the recovery window; lost authority is final.
                    if(transient(observation->failure)){beginRecovery();controlFailed=true;nextControlRetry=now()+std::chrono::seconds(1);}
                    else fail(observation->failure);
                }
                else {
                    controlFailed=false;
                    // An add the service finished since the check above precedes any read that shows it.
                    completeAdds();
                    if(!stopped)try{apply(std::move(*observation->snapshot));}catch(const ServiceOperationError&){fail("INVALID_RESPONSE");}
                }
            }
            if(!stopped&&controlFailed&&now()>=nextControlRetry) {
                try{control->retry();}catch(const System::InvalidOperationException&){}
                nextControlRetry=now()+std::chrono::seconds(1);
            }
            if(!stopped&&migrating&&now()>=migrationDeadline)fail("HOST_ENDED_SESSION");
            if(!stopped&&recovering) {
                if(!controlFailed&&lease->transport().status().state==RelayTransportState::Ready)recovering=false;
                else if(now()>=recoveryDeadline)fail(controlFailed?"SESSION_SERVICE_UNAVAILABLE":"RELAY_TRANSPORT_UNAVAILABLE");
            }
            if(!stopped) {
                ENetEvent event{};
                for(int count=0;count<64;++count) {
                    const int result=transport().Service(0,event);if(result==0)break;
                    if(result<0){fail("GAME_TRANSPORT_UNAVAILABLE");break;}
                    if(event.type==ENET_EVENT_TYPE_CONNECT)connect(event.peer);
                    else if(event.type==ENET_EVENT_TYPE_RECEIVE){std::unique_ptr<ENetPacket,PacketDelete> packet(event.packet);receive(event.peer,packet.get(),event.channelID);}
                    else if(event.type==ENET_EVENT_TYPE_DISCONNECT)disconnect(event.peer,event.data);
                    if(stopped)break;
                }
                if(!stopped&&!host&&upstream&&(!ready||recoverRoster)&&now()>=nextHello)hello();
                if(!stopped&&!ready&&now()>=deadline)fail("JOIN_TIMED_OUT");
                if(!stopped&&host&&now()>=nextStats)publishStats();
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
void ServiceENetSession::publishReady(const std::vector<GamerReadyEntry>& entries) {
    impl_->checkOwner();
    auto& impl=*impl_;
    if(entries.empty()||impl.stopped||!impl.ready)return;
    for(const auto& entry:entries) {
        // A client reports only its own gamers; the host may report anyone (ResetReady).
        if(!impl.host&&std::find(impl.localIds.begin(),impl.localIds.end(),entry.WireId)==impl.localIds.end())
            throw ServiceOperationError("NOT_AUTHORIZED");
        impl.readiness[entry.WireId]=entry.IsReady;
    }
    const auto bytes=NetPacketCodec::Encode(GamerReadyMessage{entries});
    if(impl.host){for(const auto& [peer,value]:impl.peers)if(value.admitted)impl.transmit(peer,bytes);}
    else if(impl.upstream)impl.transmit(impl.upstream,bytes);
    impl.transport().Flush();
}
void ServiceENetSession::removeMachine(const std::string& machine) {
    impl_->checkOwner();auto& impl=*impl_;
    if(!impl.host)throw ServiceOperationError("NOT_AUTHORIZED");
    if(machine==impl.current.machine)throw ServiceOperationError("INVALID_ARGUMENT");
    if(impl.stopped)return;
    const bool member=std::any_of(impl.current.members.begin(),impl.current.members.end(),[&](const auto& row){return row.machine==machine;});
    if(!member)return;
    impl.removing.insert(machine);impl.control->remove(machine);
}
void ServiceENetSession::addLocal(std::vector<std::string> names,std::vector<std::string> users) {
    impl_->checkOwner();auto& impl=*impl_;
    if(impl.stopped)throw ServiceOperationError("INVALID_STATE");
    if(names.empty()||names.size()!=users.size()||impl.locals.size()+names.size()>4)throw ServiceOperationError("INVALID_ARGUMENT");
    auto request=std::make_shared<Adding>();request->names=names;request->users=users;
    auto* executor=impl.lease->backend().get();
    impl.lease->backend()->submit([request,executor,owner=impl.account,users=std::move(users),session=impl.current.session] {
        std::optional<ServiceSessionSnapshot> value;std::string failure;
        try{value=executor->sessionDirectory().addMembers(owner,users,session);}
        catch(const ServiceOperationError& error){failure=error.code;}
        catch(...){failure="SESSION_SERVICE_UNAVAILABLE";}
        std::lock_guard lock(request->mutex);request->snapshot=std::move(value);request->failure=failure;request->done=true;
    },{});
    impl.adds.push_back(std::move(request));
}
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
std::map<unsigned char,std::uint32_t> ServiceENetSession::roundTrips()const{impl_->checkOwner();return impl_->roundTrips();}
std::pair<std::uint32_t,std::uint32_t> ServiceENetSession::traffic()const {
    impl_->checkOwner();auto& host=impl_->transport();
    return {host.getTotalSentDataProperty(),host.getTotalReceivedDataProperty()};
}
const std::shared_ptr<IGamerServicesBackend>& ServiceENetSession::origin()const{return impl_->lease->backend();}
}

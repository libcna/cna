// SPDX-License-Identifier: MS-PL
#include "ServiceGamePacketPolicy.hpp"
#include "RelayEnetPolicy.hpp"
#include "CnaService/Protocol.hpp"
#include <algorithm>
#include <set>

namespace CNA::Internal::Net {
namespace {
[[noreturn]] void packet(){throw CnaService::Error("INVALID_SERVICE_PACKET");}
[[noreturn]] void authority(){throw CnaService::Error("INVALID_SERVICE_ROSTER");}
}
ServiceGamePacketPolicy::ServiceGamePacketPolicy(const GamerServices::ServiceSessionSnapshot& snapshot)
    :roster_(snapshot),snapshot_(snapshot) {
    for(const auto& row:snapshot.members) {
        machinesById_.emplace(static_cast<unsigned char>(row.ordinal+1),row.machine);++groupSizes_[row.machine];
    }
}
void ServiceGamePacketPolicy::sourceGuard(const std::string& source) const {
    if(source==snapshot_.machine || !groupSizes_.contains(source))authority();
}
ServiceGameControl ServiceGamePacketPolicy::control(const std::string& source,std::span<const unsigned char> bytes,
    unsigned char channel,bool established,const std::vector<std::string>& localNames,const std::vector<std::string>& admitted) const {
    if(channel!=0)packet();const auto tag=validateServiceControlPacket(bytes);sourceGuard(source);
    const bool host=snapshot_.machine==snapshot_.hostMachine;
    if(tag==MessageTag::GamerReadyBroadcast) {
        // A client reports only its own gamers to the host; the host may report any gamer.
        if(!established || (!host && source!=snapshot_.hostMachine))authority();
        auto ready=NetPacketCodec::DecodeGamerReady(std::vector<unsigned char>(bytes.begin(),bytes.end()));
        std::set<unsigned char> ids;
        for(const auto& entry:ready.Entries) {
            const auto member=machinesById_.find(entry.WireId);
            if(member==machinesById_.end() || !ids.insert(entry.WireId).second || (host && member->second!=source))authority();
        }
        return ready;
    }
    if(host ? tag!=MessageTag::ClientHello : (source!=snapshot_.hostMachine || tag==MessageTag::ClientHello))authority();
    if(!host && tag!=MessageTag::ServerWelcome && !established)authority();
    const std::vector<unsigned char> value(bytes.begin(),bytes.end());
    switch(tag) {
        case MessageTag::ClientHello: {
            auto hello=NetPacketCodec::DecodeClientHello(value);(void)roster_.idsFor(source,hello.LocalGamertags);return hello;
        }
        case MessageTag::ServerWelcome: {
            auto welcome=NetPacketCodec::DecodeServerWelcome(value);roster_.validateWelcome(welcome,localNames);return welcome;
        }
        case MessageTag::GamerJoinBroadcast: {
            auto join=NetPacketCodec::DecodeGamerJoinBroadcast(value);roster_.validateEntries(join.NewGamers);
            std::map<std::string,std::size_t> counts;
            for(const auto& row:join.NewGamers)++counts[machinesById_.at(row.WireId)];
            for(const auto& [machine,count]:counts)if(count!=groupSizes_.at(machine))authority();
            return join;
        }
        case MessageTag::GamerLeaveBroadcast: {
            if(admitted.size()>static_cast<std::size_t>(CnaService::MaxSessionGamers-1))authority();
            std::set<std::string> active;
            for(const auto& machine:admitted)
                if(machine==snapshot_.machine || !groupSizes_.contains(machine) || !active.insert(machine).second)authority();
            auto leave=NetPacketCodec::DecodeGamerLeaveBroadcast(value);
            std::map<std::string,std::size_t> counts;std::set<unsigned char> ids;
            for(const auto id:leave.WireIds) {
                const auto member=machinesById_.find(id);
                if(member==machinesById_.end() || member->second==snapshot_.machine || member->second==snapshot_.hostMachine
                    || !active.contains(member->second) || !ids.insert(id).second)authority();
                ++counts[member->second];
            }
            for(const auto& [machine,count]:counts)if(count!=groupSizes_.at(machine))authority();return leave;
        }
        case MessageTag::StateChangeBroadcast: {
            auto state=NetPacketCodec::DecodeStateChangeBroadcast(value);
            const auto expected=snapshot_.state==GamerServices::ServiceSessionState::Playing
                ?NetworkSessionState::Playing:NetworkSessionState::Lobby;
            if(state.NewState!=expected)authority();return state;
        }
        case MessageTag::SessionPropertiesBroadcast: {
            auto properties=NetPacketCodec::DecodeSessionPropertiesBroadcast(value);
            if(properties.SessionProperties.getCountProperty()!=static_cast<int>(snapshot_.properties.size()))authority();
            for(std::size_t index=0;index<snapshot_.properties.size();++index)
                if(properties.SessionProperties.getItem(static_cast<int>(index))!=snapshot_.properties[index])authority();
            return properties;
        }
        default:packet();
    }
}
void ServiceGamePacketPolicy::routeGuard(const std::string& source,unsigned char senderId,unsigned char targetId,bool established,
    const std::vector<std::string>& admitted) const {
    sourceGuard(source);if(!established || admitted.size()>static_cast<std::size_t>(CnaService::MaxSessionGamers-1))authority();
    std::set<std::string> active;
    for(const auto& machine:admitted) {
        if(machine==snapshot_.machine || !groupSizes_.contains(machine) || !active.insert(machine).second)authority();
    }
    if(!active.contains(source))authority();
    const auto sender=machinesById_.find(senderId),target=machinesById_.find(targetId);
    if(sender==machinesById_.end() || target==machinesById_.end())authority();
    if(snapshot_.machine==snapshot_.hostMachine) {
        if(sender->second!=source || (target->second!=snapshot_.machine && !active.contains(target->second)))authority();
    }else {
        // The authenticated host relays other admitted groups; it cannot claim our own local sender.
        if(source!=snapshot_.hostMachine || sender->second==snapshot_.machine
            || !active.contains(sender->second) || target->second!=snapshot_.machine)authority();
    }
}
VoiceDataMessage ServiceGamePacketPolicy::voice(const std::string& source,std::span<const unsigned char> bytes,
    unsigned char channel,bool established,const std::vector<std::string>& admitted) const {
    // Voice is unreliable: only the second channel carries it.
    if(bytes.size()<6 || bytes.size()>6+MaxVoicePayloadBytes || channel!=1
        || bytes[0]!=static_cast<unsigned char>(MessageTag::VoiceData))packet();
    VoiceDataMessage frame;
    try{frame=NetPacketCodec::DecodeVoiceData(bytes);}catch(const std::runtime_error&){packet();}
    routeGuard(source,frame.SenderWireId,frame.TargetWireId,established,admitted);
    return frame;
}
AppDataMessage ServiceGamePacketPolicy::application(const std::string& source,std::span<const unsigned char> bytes,
    unsigned char channel,bool established,const std::vector<std::string>& admitted) const {
    // The existing connected-channel AppData codec has a four-byte tag/sender/target/options prefix.
    if(bytes.size()<4 || bytes.size()>MaxRelayGamePacketBytes || channel>1
        ||bytes[0]!=static_cast<unsigned char>(MessageTag::AppData) || bytes[3]>static_cast<unsigned char>(SendDataOptions::Chat))packet();
    routeGuard(source,bytes[1],bytes[2],established,admitted);
    AppDataMessage result;result.SenderWireId=bytes[1];result.TargetWireId=bytes[2];
    result.Options=static_cast<SendDataOptions>(bytes[3]);result.Payload.assign(bytes.begin()+4,bytes.end());return result;
}
}

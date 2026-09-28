// SPDX-License-Identifier: MS-PL
#include "ServiceRoster.hpp"
#include "CnaService/Protocol.hpp"
#include "CnaService/RelayProtocol.hpp"
#include <algorithm>
#include <set>

namespace CNA::Internal::Net {
namespace {
[[noreturn]] void invalid(){throw CnaService::Error("INVALID_SERVICE_ROSTER");}
[[noreturn]] void packet(){throw CnaService::Error("INVALID_SERVICE_PACKET");}
bool opaque(std::string_view value) {
    if(value.size()!=32)return false;
    for(char c:value)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
    return true;
}
void text(std::string_view value,std::size_t maximum) {
    if(value.empty()||value.size()>maximum||value.find('\0')!=std::string_view::npos)invalid();
    // Strict existing JSON encoding checks UTF-8 after the byte ceiling, without an unbounded decoder.
    try{(void)CnaService::Json(value).dump();}catch(...){invalid();}
}
struct Cursor {
    std::span<const unsigned char> bytes;std::size_t position=0;
    std::span<const unsigned char> take(std::size_t count) {
        if(count>bytes.size()-position)packet();auto result=bytes.subspan(position,count);position+=count;return result;
    }
    unsigned char byte(){return take(1)[0];}
    int count(int minimum,int maximum){const int value=byte();if(value<minimum||value>maximum)packet();return value;}
    void id(){(void)count(1,CnaService::MaxSessionGamers);}
    bool boolean(){return count(0,1)!=0;}
    void name() {
        // All valid <=32-byte UTF-8 names have a canonical one-byte BinaryWriter length prefix.
        const auto value=take(static_cast<std::size_t>(count(1,32)));
        text(std::string_view(reinterpret_cast<const char*>(value.data()),value.size()),32);
    }
    void roster() {id();name();(void)boolean();}
    void properties() {
        (void)count(static_cast<int>(CnaService::SessionPropertyCount),static_cast<int>(CnaService::SessionPropertyCount));
        for(std::size_t slot=0;slot<CnaService::SessionPropertyCount;++slot)if(boolean())(void)take(4);
    }
};
}
MessageTag validateServiceControlPacket(std::span<const unsigned char> bytes) {
    try {
        if(bytes.empty()||bytes.size()>4096)packet();Cursor cursor{bytes};const auto tag=static_cast<MessageTag>(cursor.byte());
        switch(tag) {
            case MessageTag::ClientHello: {
                const auto count=cursor.count(1,4);for(int index=0;index<count;++index)cursor.name();break;
            }
            case MessageTag::ServerWelcome: {
                const auto assigned=cursor.count(1,4);for(int index=0;index<assigned;++index)cursor.id();
                const auto count=cursor.count(1,CnaService::MaxSessionGamers);for(int index=0;index<count;++index)cursor.roster();
                cursor.properties();break;
            }
            case MessageTag::GamerJoinBroadcast: {
                const auto count=cursor.count(1,CnaService::MaxSessionGamers);for(int index=0;index<count;++index)cursor.roster();break;
            }
            case MessageTag::GamerLeaveBroadcast: {
                const auto count=cursor.count(1,CnaService::MaxSessionGamers);for(int index=0;index<count;++index)cursor.id();break;
            }
            case MessageTag::StateChangeBroadcast:(void)cursor.count(0,2);break;
            case MessageTag::SessionPropertiesBroadcast:cursor.properties();break;
            default:packet();
        }
        if(cursor.position!=bytes.size())packet();return tag;
    }catch(...){packet();}
}
ServiceRoster::ServiceRoster(const GamerServices::ServiceSessionSnapshot& value) {
    if(!opaque(value.session)||value.maxGamers<2||value.maxGamers>CnaService::MaxSessionGamers||
       value.currentGamers<1||value.currentGamers>value.maxGamers||value.members.size()!=static_cast<std::size_t>(value.currentGamers)||
       value.privateSlots<0||value.privateSlots>value.maxGamers||value.openPrivateSlots<0||value.openPrivateSlots>value.privateSlots||
       value.openPublicSlots<0||value.openPublicSlots>value.maxGamers-value.privateSlots||
       value.currentGamers+value.openPrivateSlots+value.openPublicSlots!=value.maxGamers||value.revision<1||
       (value.kind!=GamerServices::ServiceSessionKind::PlayerMatch&&value.kind!=GamerServices::ServiceSessionKind::Ranked)||
       (value.kind==GamerServices::ServiceSessionKind::Ranked&&value.allowJoinInProgress)||
       (value.state!=GamerServices::ServiceSessionState::Lobby&&value.state!=GamerServices::ServiceSessionState::Playing))invalid();
    try{(void)CnaService::relayMachineId(value.machine);(void)CnaService::relayMachineId(value.hostMachine);}catch(...){invalid();}
    text(value.hostId,64);text(value.hostGamertag,32);
    std::set<std::string> users;int privateCount=0;bool host=false;
    for(const auto& row:value.members) {
        text(row.userId,64);text(row.gamertag,32);
        try{(void)CnaService::relayMachineId(row.machine);}catch(...){invalid();}
        if(row.ordinal<0||row.ordinal>=CnaService::MaxSessionGamers||!users.insert(row.userId).second)invalid();
        const auto id=static_cast<unsigned char>(row.ordinal+1);
        if(members_.contains(id)||tags_.contains(row.gamertag)||machines_[row.machine].size()>=4)invalid();
        const bool isHost=row.userId==value.hostId;
        if(isHost){if(row.machine!=value.hostMachine||row.gamertag!=value.hostGamertag)invalid();host=true;}
        members_.emplace(id,Member{row.userId,row.gamertag,row.machine,isHost});tags_.emplace(row.gamertag,id);
        machines_[row.machine].push_back(id);if(row.privateSlot)++privateCount;
    }
    if(!host||!machines_.contains(value.machine)||privateCount!=value.privateSlots-value.openPrivateSlots)invalid();
    for(auto& [machine,ids]:machines_)std::sort(ids.begin(),ids.end());
    snapshot_=value;
}
std::vector<unsigned char> ServiceRoster::idsFor(const std::string& machine,const std::vector<std::string>& names) const {
    const auto group=machines_.find(machine);
    if(group==machines_.end()||names.size()!=group->second.size())invalid();
    std::set<unsigned char> unique;std::vector<unsigned char> result;
    for(const auto& name:names) {
        const auto found=tags_.find(name);
        if(found==tags_.end()||members_.at(found->second).machine!=machine||!unique.insert(found->second).second)invalid();
        result.push_back(found->second);
    }
    return result;
}
std::vector<std::string> ServiceRoster::remoteMachines() const {
    std::vector<std::string> result;for(const auto& [machine,ids]:machines_)if(machine!=snapshot_.machine)result.push_back(machine);return result;
}
ServerWelcomeMessage ServiceRoster::welcomeFor(const std::string& machine,const std::vector<std::string>& names,
    const std::vector<std::string>& connected) const {
    if(snapshot_.machine!=snapshot_.hostMachine||machine==snapshot_.machine||connected.size()>30)invalid();
    ServerWelcomeMessage result;result.AssignedWireIds=idsFor(machine,names);
    std::set<std::string> included{snapshot_.hostMachine};
    for(const auto& id:connected)if(!machines_.contains(id)||id==machine||!included.insert(id).second)invalid();
    for(const auto& [id,member]:members_)if(included.contains(member.machine))result.ExistingRoster.push_back({id,member.tag,member.host});
    for(std::size_t slot=0;slot<snapshot_.properties.size();++slot)result.SessionProperties.setItem(static_cast<int>(slot),snapshot_.properties[slot]);
    return result;
}
void ServiceRoster::validateEntries(const std::vector<RosterEntry>& entries) const {
    if(entries.empty()||entries.size()>static_cast<std::size_t>(CnaService::MaxSessionGamers))invalid();
    std::set<unsigned char> ids;
    for(const auto& row:entries) {
        const auto found=members_.find(row.WireId);
        if(found==members_.end()||row.Gamertag!=found->second.tag||row.IsHost!=found->second.host||
           found->second.machine==snapshot_.machine||!ids.insert(row.WireId).second)invalid();
    }
}
void ServiceRoster::validateWelcome(const ServerWelcomeMessage& welcome,const std::vector<std::string>& locals) const {
    if(snapshot_.machine==snapshot_.hostMachine||welcome.AssignedWireIds!=idsFor(snapshot_.machine,locals))invalid();
    if(welcome.SessionProperties.getCountProperty()!=static_cast<int>(CnaService::SessionPropertyCount))invalid();
    for(std::size_t slot=0;slot<snapshot_.properties.size();++slot)
        if(welcome.SessionProperties.getItem(static_cast<int>(slot))!=snapshot_.properties[slot])invalid();
    validateEntries(welcome.ExistingRoster);std::map<std::string,std::size_t> counts;
    for(const auto& row:welcome.ExistingRoster)++counts[members_.at(row.WireId).machine];
    if(!counts.contains(snapshot_.hostMachine))invalid();
    for(const auto& [machine,count]:counts)if(count!=machines_.at(machine).size())invalid();
}
}

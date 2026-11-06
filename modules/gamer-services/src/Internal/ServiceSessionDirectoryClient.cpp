// SPDX-License-Identifier: MS-PL
#include "ServiceSessionDirectoryClient.hpp"
#include "Protocol/CnaService/RelayProtocol.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <mutex>

namespace CNA::Internal::GamerServices {
ServiceOperationError::ServiceOperationError(std::string reason)
    :GamerServicesNotAvailableException("CNA service operation failed."),code(std::move(reason)) {}
namespace {
using CnaService::Json;
[[noreturn]] void invalid(){throw ServiceOperationError("INVALID_RESPONSE");}
void argument(bool valid){if(!valid)throw ServiceOperationError("INVALID_ARGUMENT");}
void fields(const Json& value,std::initializer_list<const char*> required) {
    if(!value.is_object()||value.size()!=required.size())invalid();
    for(const auto* key:required)if(!value.contains(key))invalid();
}
long long number(const Json& value,const char* key,long long low,long long high) {
    if(!value.contains(key)||!value[key].is_number_integer()||value[key]<low||value[key]>high)invalid();
    return value[key].get<long long>();
}
bool boolean(const Json& value,const char* key) {
    if(!value.contains(key)||!value[key].is_boolean())invalid();return value[key].get<bool>();
}
void actorGuard(const std::string& actor){argument(!actor.empty()&&actor.size()<=64);}
bool opaque(const std::string& id){return id.size()==32&&id.find_first_not_of("0123456789abcdef")==std::string::npos;}
std::string text(const Json& value,const char* key,std::size_t maximum) {
    const auto result=CnaService::stringField(value,key,maximum);if(result.empty())invalid();return result;
}
std::string id(const Json& value,const char* key) {const auto result=text(value,key,32);if(!opaque(result))invalid();return result;}
ServiceSessionKind kind(const Json& value) {
    const auto name=text(value,"kind",16);
    if(name=="player")return ServiceSessionKind::PlayerMatch;
    if(name=="ranked")return ServiceSessionKind::Ranked;invalid();
}
std::string kindName(ServiceSessionKind value) {
    argument(value==ServiceSessionKind::PlayerMatch||value==ServiceSessionKind::Ranked);
    return value==ServiceSessionKind::PlayerMatch?"player":"ranked";
}
Json properties(const ServiceSessionProperties& values) {
    static_assert(std::tuple_size_v<ServiceSessionProperties> == CnaService::SessionPropertyCount);
    Json result=Json::array();for(const auto& value:values)result.push_back(value?Json(*value):Json(nullptr));return result;
}
void settingsGuard(const ServiceSessionSettings& value) {
    argument(value.maxGamers>=2&&value.maxGamers<=CnaService::MaxSessionGamers&&
        value.privateSlots>=0&&value.privateSlots<=value.maxGamers&&
        (value.state==ServiceSessionState::Lobby||value.state==ServiceSessionState::Playing));
}
void usersGuard(const std::string& actor,const std::vector<std::string>& users) {
    argument(!actor.empty()&&actor.size()<=64&&!users.empty()&&users.size()<=4);
    std::set<std::string> seen;for(const auto& user:users)argument(!user.empty()&&user.size()<=64&&seen.insert(user).second);
    argument(seen.contains(actor));
}
ServiceSessionSnapshot snapshot(const Json& wire,bool roster) {
    auto value=wire;const bool typed=value.is_object()&&value.contains("guestProtocol");
    const int protocol=typed?static_cast<int>(number(value,"guestProtocol",0,1)):0;
    if(typed)value.erase("guestProtocol");
    // A member snapshot names allowHostMigration only while it is set (host-migration).
    const bool migrating=roster&&value.is_object()&&value.contains("allowHostMigration");
    if(roster&&migrating)fields(value,{"session","kind","state","maxGamers","privateSlots","properties","allowJoinInProgress",
        "revision","hostId","hostGamertag","hostMachine","currentGamers","openPublicSlots","openPrivateSlots","members","machine",
        "allowHostMigration"});
    else if(roster)fields(value,{"session","kind","state","maxGamers","privateSlots","properties","allowJoinInProgress",
        "revision","hostId","hostGamertag","hostMachine","currentGamers","openPublicSlots","openPrivateSlots","members","machine"});
    else fields(value,{"session","kind","state","maxGamers","privateSlots","properties","allowJoinInProgress",
        "revision","hostId","hostGamertag","hostMachine","currentGamers","openPublicSlots","openPrivateSlots"});
    ServiceSessionSnapshot result;result.guestProtocol=protocol;
    if(migrating&&!boolean(value,"allowHostMigration"))invalid();
    result.allowHostMigration=migrating;
    result.session=id(value,"session");result.hostMachine=id(value,"hostMachine");result.hostId=text(value,"hostId",64);
    result.hostGamertag=text(value,"hostGamertag",32);result.kind=kind(value);
    const auto state=text(value,"state",16);
    if(state!="lobby"&&state!="playing")invalid();result.state=state=="lobby"?ServiceSessionState::Lobby:ServiceSessionState::Playing;
    result.maxGamers=static_cast<int>(number(value,"maxGamers",2,CnaService::MaxSessionGamers));
    result.privateSlots=static_cast<int>(number(value,"privateSlots",0,result.maxGamers));
    result.currentGamers=static_cast<int>(number(value,"currentGamers",1,result.maxGamers));
    result.openPublicSlots=static_cast<int>(number(value,"openPublicSlots",0,result.maxGamers-result.privateSlots));
    result.openPrivateSlots=static_cast<int>(number(value,"openPrivateSlots",0,result.privateSlots));
    if(result.currentGamers+result.openPublicSlots+result.openPrivateSlots!=result.maxGamers)invalid();
    result.revision=static_cast<int>(number(value,"revision",1,2147483647));
    result.allowJoinInProgress=boolean(value,"allowJoinInProgress");
    if(result.kind==ServiceSessionKind::Ranked&&result.allowJoinInProgress)invalid();
    CnaService::validateSessionProperties(value.at("properties"));
    for(std::size_t index=0;index<result.properties.size();++index)
        if(!value["properties"][index].is_null())result.properties[index]=value["properties"][index].get<int>();
    if(!roster)return result;
    result.machine=id(value,"machine");const auto& members=value.at("members");
    if(!members.is_array()||members.size()!=static_cast<std::size_t>(result.currentGamers))invalid();
    std::set<std::string> users;std::set<int> ordinals;std::map<std::string,int> machines;
    int privateCount=0;bool host=false,ownMachine=false;
    for(const auto& row:members) {
        if(typed)fields(row,{"userId","gamertag","machine","privateSlot","ordinal","isGuest","parentId"});
        else fields(row,{"userId","gamertag","machine","privateSlot","ordinal"});
        ServiceSessionMember member;
        if(typed){member.isGuest=boolean(row,"isGuest");if(!row["parentId"].is_null())member.parentId=text(row,"parentId",64);}

        member.userId=text(row,"userId",64);member.gamertag=text(row,"gamertag",32);member.machine=id(row,"machine");
        member.privateSlot=boolean(row,"privateSlot");member.ordinal=static_cast<int>(number(row,"ordinal",0,CnaService::MaxSessionGamers-1));
        if(!users.insert(member.userId).second||!ordinals.insert(member.ordinal).second||++machines[member.machine]>4)invalid();
        if(member.privateSlot)++privateCount;
        if(member.userId==result.hostId) {
            if(member.machine!=result.hostMachine||member.gamertag!=result.hostGamertag)invalid();host=true;
        }
        if(member.machine==result.machine)ownMachine=true;
        result.members.push_back(std::move(member));
    }
    for(const auto& row:result.members) {
        if(row.isGuest) {
            if(protocol!=1||row.userId.size()!=34||!row.userId.starts_with("g:")||!opaque(row.userId.substr(2))||row.userId==result.hostId)invalid();
            const auto parent=std::find_if(result.members.begin(),result.members.end(),[&](const auto& p){return p.userId==row.parentId&&!p.isGuest&&p.machine==row.machine;});
            if(parent==result.members.end())invalid();
        }else if(!row.parentId.empty()||row.userId.starts_with("g:"))invalid();
    }
    if(!host||!ownMachine||privateCount!=result.privateSlots-result.openPrivateSlots)invalid();return result;
}
void memberGuard(const ServiceSessionSnapshot& value,const std::string& actor) {
    if(std::none_of(value.members.begin(),value.members.end(),[&](const auto& row){return row.userId==actor&&row.machine==value.machine;}))invalid();
}
void groupGuard(const ServiceSessionSnapshot& value,const std::string& actor,const std::vector<std::string>& users) {
    memberGuard(value,actor);std::set<std::string> actual;
    for(const auto& row:value.members)if(row.machine==value.machine)actual.insert(row.userId);
    if(actual!=std::set<std::string>(users.begin(),users.end()))invalid();
}
ServiceInvitation invitation(const Json& value) {
    fields(value,{"invite","session","senderId","senderGamertag","kind","status","created","expires","acceptedAt"});
    ServiceInvitation result;result.invite=id(value,"invite");result.session=id(value,"session");result.senderId=text(value,"senderId",64);
    result.senderGamertag=text(value,"senderGamertag",32);result.kind=kind(value);
    const auto state=text(value,"status",16);
    if(state=="pending")result.state=ServiceInvitationState::Pending;
    else if(state=="accepted")result.state=ServiceInvitationState::Accepted;
    else if(state=="dismissed")result.state=ServiceInvitationState::Dismissed;
    else if(state=="used")result.state=ServiceInvitationState::Used;else invalid();
    result.created=number(value,"created",1,253402300799LL);result.expires=number(value,"expires",result.created+1,253402300799LL);
    if(result.expires-result.created>CnaService::InviteLifetimeSeconds)invalid();
    result.acceptedAt=number(value,"acceptedAt",0,result.expires-1);
    if(result.acceptedAt!=0&&result.acceptedAt<result.created)invalid();
    if(result.state==ServiceInvitationState::Pending&&result.acceptedAt!=0)invalid();
    if((result.state==ServiceInvitationState::Accepted||result.state==ServiceInvitationState::Used)&&result.acceptedAt==0)invalid();
    return result;
}
    template<class Work> static auto decode(Work work) {
        try{return work();}catch(const ServiceOperationError&){throw;}catch(const Microsoft::Xna::Framework::GamerServices::GamerServicesNotAvailableException&){throw;}catch(...){invalid();}
    }
class SessionDirectoryClient final : public IServiceSessionDirectory {
public:
    SessionDirectoryClient(DirectoryRequest request,std::function<bool(const std::string&)> capability)
        :request_(std::move(request)),capability_(std::move(capability)){}
    ServiceSessionSnapshot create(const std::string& actor,const std::vector<std::string>& users,
        ServiceSessionKind category,const ServiceSessionSettings& settings) override {
        usersGuard(actor,users);settingsGuard(settings);argument(settings.state==ServiceSessionState::Lobby&&users.size()<=static_cast<std::size_t>(settings.maxGamers));
        argument(category!=ServiceSessionKind::Ranked||!settings.allowJoinInProgress);
        auto args=settingsArgs(settings);args.erase("state");args["kind"]=kindName(category);
        auto value=decode([&]{return snapshot(wireRequest("sessions.create",args,actor,users),true);});
        groupGuard(value,actor,users);
        if(value.hostId!=actor||value.hostMachine!=value.machine||value.kind!=category||value.state!=ServiceSessionState::Lobby||
            value.maxGamers!=settings.maxGamers||value.privateSlots!=settings.privateSlots||value.properties!=settings.properties||
            value.allowJoinInProgress!=settings.allowJoinInProgress||value.currentGamers!=static_cast<int>(users.size())||value.revision!=1||
            value.allowHostMigration!=(migration()&&settings.allowHostMigration))invalid();
        return value;
    }
    ServiceSessionPage find(const std::string& actor,ServiceSessionKind category,int locals,
        const ServiceSessionProperties& filters,int start,int limit) override {
        actorGuard(actor);argument(locals>=1&&locals<=4&&start>=0&&start<=1024&&limit>=1&&limit<=32);
        const bool qos=start==0&&capability_&&capability_("qos-prejoin-v1");
        return decode([&]{const auto data=wireRequest(qos?"sessions.findQos":"sessions.find",{{"kind",kindName(category)},{"localCount",locals},
            {"properties",properties(filters)},{"start",start},{"limit",limit}},actor,{});
            fields(data,{"sessions","start","more"});ServiceSessionPage page;
            page.start=static_cast<int>(number(data,"start",start,start));page.more=boolean(data,"more");
            const auto& rows=data.at("sessions");if(!rows.is_array()||rows.size()>static_cast<std::size_t>(limit)||(page.more&&rows.size()!=static_cast<std::size_t>(limit)))invalid();
            std::set<std::string> ids;for(const auto& row:rows) {
                if(qos)fields(row,{"listing","permit","lifetimeMs"});
                auto value=snapshot(qos?row.at("listing"):row,false);
                if(qos) {
                    value.qosPermit=text(row,"permit",64);
                    if(value.qosPermit.size()!=64||value.qosPermit.find_first_not_of("0123456789abcdef")!=std::string::npos)invalid();
                    (void)number(row,"lifetimeMs",30000,30000);
                    value.qosExpires=std::chrono::steady_clock::now()+std::chrono::seconds(30);
                }
                if(value.kind!=category||value.openPublicSlots<locals||(value.state==ServiceSessionState::Playing&&!value.allowJoinInProgress)||!ids.insert(value.session).second)invalid();
                for(std::size_t i=0;i<filters.size();++i)if(filters[i]&&value.properties[i]!=filters[i])invalid();
                page.sessions.push_back(std::move(value));
            }return page;
        });
    }
    ServiceSessionSnapshot join(const std::string& actor,const std::vector<std::string>& users,
        const std::string& session,const std::string& invite) override {
        usersGuard(actor,users);argument(opaque(session)&&(invite.empty()||opaque(invite)));
        Json args{{"session",session}};if(!invite.empty())args["invite"]=invite;
        auto value=decode([&]{return snapshot(wireRequest(invite.empty()?"sessions.join":"sessions.joinInvited",args,actor,users),true);});
        if(value.session!=session)invalid();groupGuard(value,actor,users);return value;
    }
    ServiceSessionSnapshot get(const std::string& actor,const std::string& session) override {return memberRequest("sessions.get",actor,session);}
    ServiceSessionSnapshot touch(const std::string& actor,const std::string& session) override {return memberRequest("sessions.touch",actor,session);}
    ServiceSessionSnapshot update(const std::string& actor,const std::string& session,int revision,const ServiceSessionSettings& settings) override {
        actorGuard(actor);argument(opaque(session)&&revision>=1&&revision<=2147483646);settingsGuard(settings);
        auto args=settingsArgs(settings);args["session"]=session;args["revision"]=revision;
        auto value=decode([&]{return snapshot(wireRequest("sessions.update",args,actor,{}),true);});memberGuard(value,actor);
        if(value.session!=session||value.hostId!=actor||value.hostMachine!=value.machine||value.revision!=revision+1||
            value.state!=settings.state||value.maxGamers!=settings.maxGamers||value.privateSlots!=settings.privateSlots||
            value.properties!=settings.properties||value.allowJoinInProgress!=settings.allowJoinInProgress||
            (migration()&&value.allowHostMigration!=settings.allowHostMigration))invalid();return value;
    }
    ServiceSessionSnapshot remove(const std::string& actor,const std::string& session,const std::string& machine) override {
        actorGuard(actor);argument(opaque(session)&&opaque(machine));
        auto value=decode([&]{return snapshot(wireRequest("sessions.remove",{{"session",session},{"machine",machine}},actor,{}),true);});
        memberGuard(value,actor);
        if(value.session!=session||value.hostId!=actor||value.hostMachine!=value.machine||machine==value.machine)invalid();
        for(const auto& row:value.members)if(row.machine==machine)invalid();
        return value;
    }
    ServiceSessionSnapshot addMembers(const std::string& actor,const std::vector<std::string>& users,const std::string& session) override {
        actorGuard(actor);argument(opaque(session)&&!users.empty()&&users.size()<=3);
        std::set<std::string> seen;for(const auto& user:users)argument(!user.empty()&&user.size()<=64&&user!=actor&&seen.insert(user).second);
        if(!capability_||!capability_("session-add-members"))throw ServiceOperationError("NOT_SUPPORTED");
        auto value=decode([&]{return snapshot(wireRequest("sessions.addMembers",{{"session",session}},actor,users),true);});
        if(value.session!=session)invalid();memberGuard(value,actor);
        for(const auto& user:users)
            if(std::none_of(value.members.begin(),value.members.end(),[&](const auto& row){return row.userId==user&&row.machine==value.machine;}))invalid();
        return value;
    }
    bool leave(const std::string& actor,const std::string& session) override {
        actorGuard(actor);argument(opaque(session));return decode([&]{const auto value=wireRequest("sessions.leave",{{"session",session}},actor,{});fields(value,{"ended"});return boolean(value,"ended");});
    }
    ServiceRelayTicket issueRelayTicket(const std::string& actor,const std::vector<std::string>& users,const std::string& session) override {
        usersGuard(actor,users);argument(opaque(session));
        return decode([&] {
            const auto data=wireRequest("sessions.relayTicket",{{"session",session}},actor,users);
            fields(data,{"ticket","session","machine","expires","serverTime","relayVersion","maxDatagramBytes"});
            ServiceRelayTicket result;result.ticket=text(data,"ticket",64);result.session=id(data,"session");result.machine=id(data,"machine");
            if(result.ticket.size()!=64||result.ticket.find_first_not_of("0123456789abcdef")!=std::string::npos||result.session!=session)invalid();
            result.issuedAt=number(data,"serverTime",0,253402300799LL);
            result.expires=number(data,"expires",result.issuedAt+1,253402300799LL);
            if(result.expires-result.issuedAt>CnaService::RelayTicketLifetimeSeconds)invalid();
            (void)number(data,"relayVersion",CnaService::RelayVersion,CnaService::RelayVersion);
            (void)number(data,"maxDatagramBytes",CnaService::MaxRelayDatagramBytes,CnaService::MaxRelayDatagramBytes);
            return result;
        });
    }
    ServiceQosTicket issueQosTicket(const std::string& actor,const std::string& permit) override {
        actorGuard(actor);argument(permit.size()==64&&permit.find_first_not_of("0123456789abcdef")==std::string::npos);
        return qosTicket(actor,{},{{"permit",permit}},false);
    }
    ServiceQosTicket issueQosHostTicket(const std::string& actor,const std::vector<std::string>& users,const std::string& session) override {
        usersGuard(actor,users);argument(opaque(session));return qosTicket(actor,users,{{"session",session}},true);
    }
    ServiceInvitation sendInvite(const std::string& actor,const std::string& session,const std::string& gamertag) override {
        actorGuard(actor);argument(opaque(session)&&!gamertag.empty()&&gamertag.size()<=32);
        auto value=decode([&]{return invitation(wireRequest("invites.send",{{"session",session},{"gamertag",gamertag}},actor,{}));});
        if(value.session!=session||value.senderId!=actor||(value.state!=ServiceInvitationState::Pending&&value.state!=ServiceInvitationState::Accepted))invalid();return value;
    }
    ServiceInvitation requestJoin(const std::string& actor,const std::string& gamertag) override {
        actorGuard(actor);argument(!gamertag.empty()&&gamertag.size()<=32);
        auto value=decode([&]{return invitation(wireRequest("invites.joinFriend",{{"gamertag",gamertag}},actor,{}));});
        if(value.senderId==actor||value.state!=ServiceInvitationState::Pending&&value.state!=ServiceInvitationState::Accepted)invalid();return value;
    }
    ServiceInvitationPage listInvites(const std::string& actor,int start,int limit) override {
        actorGuard(actor);argument(start>=0&&start<=CnaService::MaxIncomingInvites&&limit>=1&&limit<=32);
        return decode([&]{const auto data=wireRequest("invites.list",{{"start",start},{"limit",limit}},actor,{});
            fields(data,{"invites","start","more"});ServiceInvitationPage page;page.start=static_cast<int>(number(data,"start",start,start));page.more=boolean(data,"more");
            const auto& rows=data.at("invites");if(!rows.is_array()||rows.size()>static_cast<std::size_t>(limit)||(page.more&&rows.size()!=static_cast<std::size_t>(limit)))invalid();
            std::set<std::string> ids;for(const auto& row:rows) {
                auto value=invitation(row);if(!ids.insert(value.invite).second||value.senderId==actor||
                    (value.state!=ServiceInvitationState::Pending&&value.state!=ServiceInvitationState::Accepted))invalid();page.invites.push_back(std::move(value));
            }return page;
        });
    }
    ServiceInvitation getInvite(const std::string& actor,const std::string& invite) override {return inviteRequest("invites.get",actor,invite);}
    ServiceInvitation acceptInvite(const std::string& actor,const std::string& invite) override {
        auto value=inviteRequest("invites.accept",actor,invite);if(value.state!=ServiceInvitationState::Accepted)invalid();return value;
    }
    ServiceInvitation dismissInvite(const std::string& actor,const std::string& invite) override {
        auto value=inviteRequest("invites.dismiss",actor,invite);if(value.state!=ServiceInvitationState::Dismissed)invalid();return value;
    }
private:
    std::mutex modeMutex_;
    std::map<std::string,int> modes_;
    Json wireRequest(const std::string& operation,Json args,const std::string& actor,const std::vector<std::string>& users) {
        const bool supported=capability_&&capability_("online-guests-v1");
        const bool hasGuests=std::any_of(users.begin(),users.end(),[](const auto& id){return id.starts_with("g:");});
        bool guest=supported&&operation.starts_with("sessions.")&&operation!="sessions.qosTicket";
        // Creation fixes the session mode for its lifetime. Keep account-only groups
        // accessible to legacy peers even when this server supports online guests.
        if(operation=="sessions.create")guest=guest&&hasGuests;
        bool known=false;
        const auto session=args.value("session",std::string{});
        if(!session.empty()){std::lock_guard lock(modeMutex_);auto found=modes_.find(session);if(found!=modes_.end()){known=true;guest=guest&&found->second==1;}}
        if(supported&&!guest&&hasGuests)throw ServiceOperationError("NOT_SUPPORTED");
        Json result;
        try {result=request_(guest?operation+"Guests":operation,args,actor,users);}
        catch(const ServiceOperationError& error) {
            if(!guest||known||session.empty()||error.code!="NOT_SUPPORTED"||hasGuests)throw;
            result=request_(operation,args,actor,users);guest=false;
        }
        auto remember=[&](const Json& value) {
            // Only directory snapshots/listings establish immutable roster mode. Tickets
            // and invitations also name a session, but do not carry its protocol version.
            if(!value.is_object()||!value.contains("revision")||!value.contains("kind")
                ||!value.contains("session")||!value["session"].is_string())return;
            const auto id=value["session"].get<std::string>();const int mode=value.value("guestProtocol",0);
            std::lock_guard lock(modeMutex_);if(modes_.size()>=1024&&!modes_.contains(id))modes_.erase(modes_.begin());modes_[id]=mode;
        };
        if(result.is_object()) {
            remember(result);
            if(result.contains("sessions")&&result["sessions"].is_array())for(const auto& row:result["sessions"])remember(row.is_object()&&row.contains("listing")?row["listing"]:row);
        }
        return result;
    }
    ServiceQosTicket qosTicket(const std::string& actor,const std::vector<std::string>& users,Json args,bool host) {
        if(!capability_||!capability_("qos-prejoin-v1"))throw ServiceOperationError("NOT_SUPPORTED");
        return decode([&] {
            const auto data=wireRequest(host?"sessions.qosHostTicket":"sessions.qosTicket",std::move(args),actor,users);
            if(host)fields(data,{"ticket","lifetimeMs"});else fields(data,{"ticket","probe","lifetimeMs"});
            ServiceQosTicket t;t.host=host;t.ticket=text(data,"ticket",64);
            if(t.ticket.size()!=64||t.ticket.find_first_not_of("0123456789abcdef")!=std::string::npos)invalid();
            (void)number(data,"lifetimeMs",5000,5000);if(!host){t.probe=id(data,"probe");if(t.probe==std::string(32,'0'))invalid();}return t;
        });
    }
    bool migration() const {return capability_&&capability_("host-migration");}
    Json settingsArgs(const ServiceSessionSettings& settings) const {
        Json args{{"maxGamers",settings.maxGamers},{"privateSlots",settings.privateSlots},
            {"state",settings.state==ServiceSessionState::Lobby?"lobby":"playing"},
            {"allowJoinInProgress",settings.allowJoinInProgress},{"properties",properties(settings.properties)}};
        // A service without host migration refuses the field; its sessions end with their host.
        if(migration())args["allowHostMigration"]=settings.allowHostMigration;
        return args;
    }
    ServiceSessionSnapshot memberRequest(const char* op,const std::string& actor,const std::string& session) {
        actorGuard(actor);argument(opaque(session));auto value=decode([&]{return snapshot(wireRequest(op,{{"session",session}},actor,{}),true);});
        if(value.session!=session)invalid();memberGuard(value,actor);return value;
    }
    ServiceInvitation inviteRequest(const char* op,const std::string& actor,const std::string& invite) {
        actorGuard(actor);argument(opaque(invite));auto value=decode([&]{return invitation(wireRequest(op,{{"invite",invite}},actor,{}));});
        if(value.invite!=invite||value.senderId==actor)invalid();return value;
    }
    DirectoryRequest request_;
    std::function<bool(const std::string&)> capability_;
};
}
std::unique_ptr<IServiceSessionDirectory> makeSessionDirectoryClient(DirectoryRequest request,std::function<bool(const std::string&)> capability) {
    if(!request)throw ServiceOperationError("INVALID_ARGUMENT");
    return std::make_unique<SessionDirectoryClient>(std::move(request),std::move(capability));
}
}

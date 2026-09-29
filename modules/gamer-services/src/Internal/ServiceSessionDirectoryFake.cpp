// SPDX-License-Identifier: MS-PL
#include "ServiceSessionDirectoryFake.hpp"
#include "CnaService/Protocol.hpp"
#include "Protocol/CnaService/RelayProtocol.hpp"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>

namespace CNA::Internal::GamerServices {
namespace {
[[noreturn]] void fail(const char* code){throw ServiceOperationError(code);}
void require(bool value,const char* code="INVALID_ARGUMENT"){if(!value)fail(code);}
class FakeSessionDirectory final : public IServiceSessionDirectory {
public:
    FakeSessionDirectory(std::function<void(const std::string&)> authorize,
        std::function<std::string(const std::string&)> tag,std::function<std::string(const std::string&)> user,
        std::function<long long()> clock):authorize_(std::move(authorize)),tag_(std::move(tag)),user_(std::move(user)),clock_(std::move(clock)){}
    ServiceSessionSnapshot create(const std::string& actor,const std::vector<std::string>& users,
        ServiceSessionKind kind,const ServiceSessionSettings& settings) override {
        prune();participants(actor,users);settingsGuard(settings);kindGuard(kind);
        require(kind!=ServiceSessionKind::Ranked||!settings.allowJoinInProgress);
        require(settings.state==ServiceSessionState::Lobby&&users.size()<=static_cast<std::size_t>(settings.maxGamers));
        require(sessions_.size()<1024,"LIMIT_EXCEEDED");int hosts=0;
        for(const auto& [id,session]:sessions_){(void)id;if(session.value.hostId==actor)++hosts;}
        require(hosts<16,"LIMIT_EXCEEDED");unoccupied(users);
        Session session;auto& value=session.value;value.session=nextId();value.hostId=actor;value.hostGamertag=tag_(actor);
        value.hostMachine=nextId();value.kind=kind;apply(value,settings);session.owners[value.hostMachine]=actor;session.leases[value.hostMachine]=now()+90;
        for(std::size_t i=0;i<users.size();++i)value.members.push_back({users[i],tag_(users[i]),value.hostMachine,
            i>=static_cast<std::size_t>(settings.maxGamers-settings.privateSlots),static_cast<int>(i)});
        recount(value);const auto id=value.session;sessions_.emplace(id,std::move(session));return view(sessions_.at(id),valueMachine(id,actor));
    }
    ServiceSessionPage find(const std::string& actor,ServiceSessionKind kind,int locals,
        const ServiceSessionProperties& filters,int start,int limit) override {
        authorize_(actor);prune();kindGuard(kind);require(locals>=1&&locals<=4&&start>=0&&start<=1024&&limit>=1&&limit<=32);
        ServiceSessionPage page;page.start=start;int skipped=0;
        for(const auto& [id,session]:sessions_) {
            (void)id;const auto& value=session.value;
            if(value.kind!=kind||value.openPublicSlots<locals||
                (value.state==ServiceSessionState::Playing&&!value.allowJoinInProgress)||hasMember(value,actor))continue;
            bool match=true;for(std::size_t i=0;i<filters.size();++i)if(filters[i]&&value.properties[i]!=filters[i])match=false;
            if(!match||skipped++<start)continue;
            if(page.sessions.size()==static_cast<std::size_t>(limit)){page.more=true;break;}
            auto advertisement=value;advertisement.members.clear();advertisement.machine.clear();advertisement.allowHostMigration=false;
            page.sessions.push_back(std::move(advertisement));
        }return page;
    }
    ServiceSessionSnapshot join(const std::string& actor,const std::vector<std::string>& users,
        const std::string& id,const std::string& invite) override {
        prune();participants(actor,users);auto& session=lookup(id);auto& value=session.value;
        Invitation* invitation=nullptr;
        if(!invite.empty()) {
            invitation=&inviteLookup(actor,invite);
            require(invitation->value.session==id,"NOT_FOUND");
            require(invitation->value.state==ServiceInvitationState::Accepted||invitation->value.state==ServiceInvitationState::Used,"INVALID_STATE");
        }
        for(const auto& [machine,owner]:session.owners)if(owner==actor) {
            std::set<std::string> group;for(const auto& row:value.members)if(row.machine==machine)group.insert(row.userId);
            require(group==std::set<std::string>(users.begin(),users.end()),"INVALID_STATE");
            if(invitation){require(invitation->usedMachine.empty()||invitation->usedMachine==machine,"INVALID_STATE");consume(*invitation,machine);}
            return view(session,machine);
        }
        if(invitation)require(invitation->value.state!=ServiceInvitationState::Used,"INVALID_STATE");
        require(value.state!=ServiceSessionState::Playing||(value.kind==ServiceSessionKind::PlayerMatch&&value.allowJoinInProgress),"INVALID_STATE");
        int privateFree=invitation?value.openPrivateSlots:0;
        require(users.size()<=static_cast<std::size_t>(value.openPublicSlots+privateFree),"SESSION_FULL");unoccupied(users);
        const auto machine=nextId();std::set<int> ordinals;for(const auto& row:value.members)ordinals.insert(row.ordinal);
        auto members=value.members;for(const auto& user:users) {
            int ordinal=0;while(ordinals.contains(ordinal))++ordinal;ordinals.insert(ordinal);
            members.push_back({user,tag_(user),machine,privateFree>0,ordinal});if(privateFree>0)--privateFree;
        }
        value.members=std::move(members);session.owners[machine]=actor;session.leases[machine]=now()+90;++value.revision;recount(value);
        if(invitation)consume(*invitation,machine);return view(session,machine);
    }
    ServiceSessionSnapshot get(const std::string& actor,const std::string& id) override {
        authorize_(actor);prune();auto& session=lookup(id);return view(session,memberMachine(session,actor));
    }
    ServiceSessionSnapshot touch(const std::string& actor,const std::string& id) override {
        authorize_(actor);prune();auto& session=lookup(id);const auto machine=ownedMachine(session,actor);session.leases[machine]=now()+90;return view(session,machine);
    }
    ServiceSessionSnapshot update(const std::string& actor,const std::string& id,int revision,const ServiceSessionSettings& settings) override {
        authorize_(actor);prune();settingsGuard(settings);require(revision>=1&&revision<=2147483646);
        auto& session=lookup(id);const auto machine=memberMachine(session,actor);auto& value=session.value;
        require(value.hostId==actor&&value.hostMachine==machine,"NOT_AUTHORIZED");require(value.revision==revision,"CONFLICT");
        require(value.kind!=ServiceSessionKind::Ranked||!settings.allowJoinInProgress);
        require(settings.privateSlots>=value.privateSlots-value.openPrivateSlots&&
            settings.maxGamers-settings.privateSlots>=value.maxGamers-value.privateSlots-value.openPublicSlots);
        apply(value,settings);++value.revision;recount(value);session.leases[machine]=now()+90;return view(session,machine);
    }
    bool leave(const std::string& actor,const std::string& id) override {
        authorize_(actor);prune();auto& session=lookup(id);const auto machine=ownedMachine(session,actor);
        if(machine==session.value.hostMachine&&!(session.value.allowHostMigration&&migrate(session,machine))){close(id);return true;}
        removeMachine(session,machine);return false;
    }
    ServiceSessionSnapshot remove(const std::string& actor,const std::string& id,const std::string& machine) override {
        authorize_(actor);prune();auto& session=lookup(id);const auto own=ownedMachine(session,actor);
        require(session.value.hostId==actor&&session.value.hostMachine==own,"NOT_AUTHORIZED");
        require(machine!=own);require(session.owners.contains(machine),"NOT_FOUND");
        for(const auto& row:session.value.members)if(row.machine==machine)session.removed.insert(row.userId);
        removeMachine(session,machine);return view(session,own);
    }
    ServiceSessionSnapshot addMembers(const std::string& actor,const std::vector<std::string>& users,const std::string& id) override {
        authorize_(actor);prune();require(!users.empty()&&users.size()<=3);
        std::set<std::string> seen;for(const auto& user:users){require(user!=actor&&seen.insert(user).second);authorize_(user);}
        auto& session=lookup(id);const auto machine=ownedMachine(session,actor);auto& value=session.value;
        const auto group=std::count_if(value.members.begin(),value.members.end(),[&](const auto& row){return row.machine==machine;});
        require(group+static_cast<long>(users.size())<=4,"LIMIT_EXCEEDED");
        require(value.state!=ServiceSessionState::Playing||(value.kind==ServiceSessionKind::PlayerMatch&&value.allowJoinInProgress),"INVALID_STATE");
        require(users.size()<=static_cast<std::size_t>(value.openPublicSlots),"SESSION_FULL");unoccupied(users);
        std::set<int> ordinals;for(const auto& row:value.members)ordinals.insert(row.ordinal);
        for(const auto& user:users) {
            int ordinal=0;while(ordinals.contains(ordinal))++ordinal;ordinals.insert(ordinal);
            value.members.push_back({user,tag_(user),machine,false,ordinal});
        }
        ++value.revision;recount(value);return view(session,machine);
    }
    ServiceRelayTicket issueRelayTicket(const std::string& actor,const std::vector<std::string>& users,const std::string& id) override {
        prune();participants(actor,users);auto& session=lookup(id);const auto machine=ownedMachine(session,actor);
        std::set<std::string> group;for(const auto& member:session.value.members)if(member.machine==machine)group.insert(member.userId);
        require(group==std::set<std::string>(users.begin(),users.end()),"NOT_AUTHORIZED");
        // Fixture authority is intentionally deterministic and cannot authenticate to a real relay.
        return {std::string(32,'0')+nextId(),id,machine,now(),now()+CnaService::RelayTicketLifetimeSeconds};
    }
    ServiceInvitation sendInvite(const std::string& actor,const std::string& id,const std::string& tag) override {
        authorize_(actor);prune();auto& session=lookup(id);(void)memberMachine(session,actor);
        require(!tag.empty()&&tag.size()<=32);const auto recipient=user_(tag);require(recipient!=actor);
        require(!hasMember(session.value,recipient),"INVALID_STATE");int incoming=0;
        for(const auto& [key,invitation]:invitations_) {
            (void)key;if(!live(invitation))continue;
            if(invitation.recipient==recipient)++incoming;
            if(invitation.recipient==recipient&&invitation.value.session==id&&invitation.value.senderId==actor)return invitation.value;
        }
        require(incoming<CnaService::MaxIncomingInvites&&invitations_.size()<CnaService::MaxTitleInvites,"LIMIT_EXCEEDED");
        auto& quota=quotas_[actor];if(quota.second==0||now()-quota.first>=3600)quota={now(),0};require(quota.second<CnaService::MaxHourlyInvites,"RATE_LIMITED");
        Invitation invitation;invitation.recipient=recipient;auto& value=invitation.value;value.invite=nextId();value.session=id;
        value.senderId=actor;value.senderGamertag=tag_(actor);value.kind=session.value.kind;value.created=now();value.expires=now()+CnaService::InviteLifetimeSeconds;
        const auto key=value.invite;auto result=value;invitations_.emplace(key,std::move(invitation));++quota.second;return result;
    }
    ServiceInvitation requestJoin(const std::string& actor,const std::string& tag) override {
        // The fixture has no friends list: any account's joinable player-match game may be asked for.
        authorize_(actor);prune();require(!tag.empty()&&tag.size()<=32);const auto host=user_(tag);require(host!=actor);
        for(auto& [id,session]:sessions_) {
            const auto& value=session.value;
            if(!hasMember(value,host)||value.kind!=ServiceSessionKind::PlayerMatch)continue;
            require(!hasMember(value,actor),"INVALID_STATE");
            if((value.state!=ServiceSessionState::Lobby&&!value.allowJoinInProgress)||value.openPublicSlots<=0)continue;
            for(const auto& [key,invitation]:invitations_) {
                (void)key;if(live(invitation)&&invitation.recipient==actor&&invitation.value.session==id&&invitation.value.senderId==host)return invitation.value;
            }
            Invitation invitation;invitation.recipient=actor;auto& result=invitation.value;result.invite=nextId();result.session=id;
            result.senderId=host;result.senderGamertag=tag_(host);result.kind=value.kind;result.created=now();result.expires=now()+CnaService::InviteLifetimeSeconds;
            invitation.requested=true;
            const auto key=result.invite;auto copy=result;invitations_.emplace(key,std::move(invitation));return copy;
        }
        throw ServiceOperationError("NOT_FOUND");
    }
    ServiceInvitationPage listInvites(const std::string& actor,int start,int limit) override {
        authorize_(actor);prune();require(start>=0&&start<=CnaService::MaxIncomingInvites&&limit>=1&&limit<=32);
        ServiceInvitationPage page;page.start=start;int skipped=0;
        for(const auto& [key,invitation]:invitations_) {
            (void)key;if(invitation.recipient!=actor||!live(invitation)||invitation.requested)continue;
            if(skipped++<start)continue;
            if(page.invites.size()==static_cast<std::size_t>(limit)){page.more=true;break;}page.invites.push_back(invitation.value);
        }return page;
    }
    ServiceInvitation getInvite(const std::string& actor,const std::string& id) override {authorize_(actor);prune();return inviteLookup(actor,id).value;}
    ServiceInvitation acceptInvite(const std::string& actor,const std::string& id) override {
        authorize_(actor);prune();auto& value=inviteLookup(actor,id).value;
        require(value.state==ServiceInvitationState::Pending||value.state==ServiceInvitationState::Accepted,"INVALID_STATE");
        if(value.state==ServiceInvitationState::Pending){value.state=ServiceInvitationState::Accepted;value.acceptedAt=now();}return value;
    }
    ServiceInvitation dismissInvite(const std::string& actor,const std::string& id) override {
        authorize_(actor);prune();auto& value=inviteLookup(actor,id).value;require(value.state!=ServiceInvitationState::Used,"INVALID_STATE");
        value.state=ServiceInvitationState::Dismissed;return value;
    }
private:
    struct Session {ServiceSessionSnapshot value;std::map<std::string,std::string> owners;std::map<std::string,long long> leases;std::set<std::string> removed;};
    struct Invitation {ServiceInvitation value;std::string recipient,usedMachine;bool requested=false;};
    long long now() const{return clock_();}
    std::string nextId(){std::ostringstream stream;stream<<std::hex<<std::setfill('0')<<std::setw(32)<<++sequence_;return stream.str();}
    static void kindGuard(ServiceSessionKind kind){require(kind==ServiceSessionKind::PlayerMatch||kind==ServiceSessionKind::Ranked);}
    static void settingsGuard(const ServiceSessionSettings& value) {
        require(value.maxGamers>=2&&value.maxGamers<=CnaService::MaxSessionGamers&&value.privateSlots>=0&&value.privateSlots<=value.maxGamers&&
            (value.state==ServiceSessionState::Lobby||value.state==ServiceSessionState::Playing));
    }
    void participants(const std::string& actor,const std::vector<std::string>& users) {
        require(!users.empty()&&users.size()<=4);std::set<std::string> seen;
        for(const auto& user:users){require(!user.empty()&&seen.insert(user).second);authorize_(user);}require(seen.contains(actor));authorize_(actor);
    }
    void unoccupied(const std::vector<std::string>& users) const {
        for(const auto& [id,session]:sessions_){(void)id;for(const auto& user:users)require(!hasMember(session.value,user),"INVALID_STATE");}
    }
    static bool hasMember(const ServiceSessionSnapshot& value,const std::string& user) {
        return std::any_of(value.members.begin(),value.members.end(),[&](const auto& row){return row.userId==user;});
    }
    static void apply(ServiceSessionSnapshot& value,const ServiceSessionSettings& settings) {
        value.maxGamers=settings.maxGamers;value.privateSlots=settings.privateSlots;value.state=settings.state;
        value.allowJoinInProgress=settings.allowJoinInProgress;value.allowHostMigration=settings.allowHostMigration;
        value.properties=settings.properties;
    }
    // As the service: the machine holding the lowest remaining ordinal hosts, its owner the host account.
    bool migrate(Session& session,const std::string& departing) {
        const ServiceSessionMember* next=nullptr;
        for(const auto& row:session.value.members)
            if(row.machine!=departing&&session.leases.at(row.machine)>now()&&(!next||row.ordinal<next->ordinal))next=&row;
        if(!next)return false;
        session.value.hostMachine=next->machine;session.value.hostId=session.owners.at(next->machine);
        session.value.hostGamertag=tag_(session.value.hostId);++session.value.revision;return true;
    }
    static void recount(ServiceSessionSnapshot& value) {
        value.currentGamers=static_cast<int>(value.members.size());int privateCount=0;for(const auto& row:value.members)if(row.privateSlot)++privateCount;
        value.openPrivateSlots=value.privateSlots-privateCount;value.openPublicSlots=value.maxGamers-value.privateSlots-(value.currentGamers-privateCount);
    }
    static ServiceSessionSnapshot view(const Session& session,const std::string& machine){auto value=session.value;value.machine=machine;return value;}
    Session& lookup(const std::string& id){const auto found=sessions_.find(id);if(found==sessions_.end())fail("NOT_FOUND");return found->second;}
    std::string valueMachine(const std::string& id,const std::string& actor){return memberMachine(lookup(id),actor);}
    static std::string memberMachine(const Session& session,const std::string& actor) {
        for(const auto& row:session.value.members)if(row.userId==actor)return row.machine;
        fail(session.removed.contains(actor)?"REMOVED_BY_HOST":"NOT_AUTHORIZED");
    }
    static std::string ownedMachine(const Session& session,const std::string& actor) {
        const auto machine=memberMachine(session,actor);require(session.owners.at(machine)==actor,"NOT_AUTHORIZED");return machine;
    }
    static void removeMachine(Session& session,const std::string& machine,bool revise=true) {
        std::erase_if(session.value.members,[&](const auto& row){return row.machine==machine;});session.owners.erase(machine);session.leases.erase(machine);if(revise)++session.value.revision;recount(session.value);
    }
    void close(const std::string& id){sessions_.erase(id);std::erase_if(invitations_,[&](const auto& pair){return pair.second.value.session==id;});}
    void prune() {
        std::vector<std::string> closed;
        for(auto& [id,session]:sessions_) {
            if(session.value.allowHostMigration&&session.value.revision<2147483646&&session.leases.at(session.value.hostMachine)<=now())
                (void)migrate(session,session.value.hostMachine);
            if(session.leases.at(session.value.hostMachine)<=now()||session.value.revision>=2147483647){closed.push_back(id);continue;}
            std::vector<std::string> expired;for(const auto& [machine,deadline]:session.leases)if(deadline<=now())expired.push_back(machine);
            for(const auto& machine:expired)removeMachine(session,machine,false);
            if(!expired.empty())++session.value.revision;
        }
        for(const auto& id:closed)close(id);
        std::erase_if(invitations_,[&](const auto& pair){return pair.second.value.created<=now()-86400;});
    }
    bool live(const Invitation& value) const {
        return value.value.expires>now()&&(value.value.state==ServiceInvitationState::Pending||value.value.state==ServiceInvitationState::Accepted);
    }
    Invitation& inviteLookup(const std::string& actor,const std::string& id) {
        const auto found=invitations_.find(id);if(found==invitations_.end())fail("NOT_FOUND");auto& value=found->second;
        require(value.recipient==actor,"NOT_AUTHORIZED");require(value.value.expires>now(),"INVALID_STATE");return value;
    }
    static void consume(Invitation& value,const std::string& machine){value.value.state=ServiceInvitationState::Used;value.usedMachine=machine;}
    std::function<void(const std::string&)> authorize_;
    std::function<std::string(const std::string&)> tag_,user_;
    std::function<long long()> clock_;
    unsigned long long sequence_=0;
    std::map<std::string,Session> sessions_;
    std::map<std::string,Invitation> invitations_;
    std::map<std::string,std::pair<long long,int>> quotas_;
};
}
std::unique_ptr<IServiceSessionDirectory> makeFakeSessionDirectory(
    std::function<void(const std::string&)> authorize,std::function<std::string(const std::string&)> gamertag,
    std::function<std::string(const std::string&)> userId,std::function<long long()> clock) {
    require(static_cast<bool>(authorize)&&static_cast<bool>(gamertag)&&static_cast<bool>(userId));
    if(!clock)clock=[] {return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();};
    return std::make_unique<FakeSessionDirectory>(std::move(authorize),std::move(gamertag),std::move(userId),std::move(clock));
}
}

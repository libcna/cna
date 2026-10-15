// SPDX-License-Identifier: MS-PL
// The deterministic in-process Gamer Services fixture the tests sign in to (makeFakeBackend).
//
// Link boundary: kept out of GamerServicesBackend.cpp so that only programs which create a fake
// backend -- the test suites -- link it. It used to share that object file with the production
// service client and so shipped in every game that touched Gamer Services.
#include "QueuedBackend.hpp"
#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include "CNA/Internal/GamerServices/ContentHash.hpp"
#include "ServiceSessionDirectoryFake.hpp"
#include "CnaService/Protocol.hpp"
#include "System/InvalidOperationException.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace CNA::Internal::GamerServices {
using CnaService::Json;
namespace {
using Unavailable=Microsoft::Xna::Framework::GamerServices::GamerServicesNotAvailableException;
using ServiceError=ServiceOperationError;
class FakeBackend final : public QueuedBackend {
public:
    FakeBackend(std::vector<ServiceIdentity> identities,std::vector<ServiceAchievement> catalog,std::vector<ServiceLeaderboardFixture> boards):QueuedBackend(false),identities_(std::move(identities)),catalog_(std::move(catalog)),boards_(std::move(boards)) {
        for(const auto& person:identities_)if(!person.avatar.empty())revisions_[person.userId]=1;
        directory_=makeFakeSessionDirectory([this](const std::string& user){require(user);if(!profileById(user).allowOnlineSessions)throw ServiceError("NOT_AUTHORIZED");},
            [this](const std::string& user){return profileById(user).gamertag;},
            [this](const std::string& tag){const auto person=profile(tag);if(!person.allowOnlineSessions)throw ServiceError("NOT_AUTHORIZED");return person.userId;});
    }
    bool serviceEnabled() const override{return true;}
    IServiceSessionDirectory& sessionDirectory() override {return *directory_;}
    void signIn(int slot,std::string username,std::string password) override {
        slotGuard(slot);std::fill(password.begin(),password.end(),'\0');
        queue([this,slot,username=std::move(username)] {
            BackendEvent event;event.slot=slot;
            for(const auto& person:identities_)if(person.gamertag==username) {
                for(int i=0;i<4;++i)if(i!=slot&&slots_[i]==person.userId){event.error="Already signed in.";return event;}
                slots_[slot]=person.userId;event.type=BackendEvent::Type::SignedIn;event.identity=person;
                event.identity.blocked=blockedTags(person.userId);return event;
            }event.error="Sign-in failed.";return event;
        });
    }
    void signOut(int slot) override {
        slotGuard(slot);queue([this,slot]{slots_[slot].clear();BackendEvent event;event.slot=slot;event.type=BackendEvent::Type::SignedOut;return event;});
    }
    ServiceIdentity profile(const std::string& tag) override {
        for(const auto& person:identities_)if(person.gamertag==tag) {
            auto value=person;for(const auto& entry:catalog_)if(earned_[person.userId][entry.key]){value.gamerScore+=entry.score;++value.totalAchievements;}
            if(value.totalAchievements>0)value.titlesPlayed=std::max(value.titlesPlayed,1);
            return value;
        }throw Unavailable("Gamer not found.");
    }
    std::vector<ServiceAchievement> achievements(const std::string& user) override {
        require(user);auto values=catalog_;for(auto& entry:values)entry.earnedTicks=earned_[user][entry.key];return values;
    }
    std::string award(const std::string& user,const std::string& key) override {
        require(user);
        for(const auto& entry:catalog_)if(entry.key==key) {
            if(earned_[user][key])return {};
            earned_[user][key]=638000000000000000LL;return entry.name;
        }
        throw Unavailable("Achievement not found.");
    }
    std::vector<ServiceFriend> friends(const std::string& user) override {
        {std::lock_guard guard(avatarLock_);if(avatarsUnreachable_)throw Unavailable("Fake fixture is unreachable.");}
        require(user);std::vector<ServiceFriend> result;
        for(const auto& target:identities_)if(target.userId!=user) {
            const bool sent=edges_.contains({user,target.userId}),received=edges_.contains({target.userId,user});
            if(!sent&&!received)continue;
            ServiceFriend value;value.userId=target.userId;value.gamertag=target.gamertag;value.accepted=sent&&received;
            value.requestSent=sent&&!received;value.requestReceived=received&&!sent;
            value.online=value.accepted&&(std::find(slots_.begin(),slots_.end(),target.userId)!=slots_.end()||remoteOnline_.contains(target.userId));
            value.presence=value.online?presence_[target.userId]:"";
            value.away=value.online&&status_[target.userId]=="away";value.busy=value.online&&status_[target.userId]=="busy";
            value.joinable=value.online&&joinable_.contains(target.userId);
            value.hasVoice=value.online&&voice_.contains(target.userId);
            result.push_back(std::move(value));
        }return result;
    }
    void changeFriend(const std::string& user,const std::string& tag,const std::string& action) override {
        require(user);auto target=profile(tag).userId;if(target==user)throw Unavailable("Self friendship.");
        if(action!="remove"&&blockedBetween(user,target))throw ServiceOperationError("NOT_AUTHORIZED");
        if(action=="accept"&&!edges_.contains({target,user}))throw Unavailable("No incoming request.");
        if(action=="remove"){edges_.erase({target,user});edges_.erase({user,target});}
        else if(action=="add"||action=="accept")edges_.insert({user,target});
        else throw Unavailable("Invalid friendship operation.");
    }
    void setPresence(const std::string& user,int,const std::string& text) override {require(user);presence_[user]=text;}
    void setPresenceStatus(const std::string& user,const std::string& status) override {
        require(user);if(status!="online"&&status!="away"&&status!="busy")throw ServiceOperationError("INVALID_ARGUMENT");
        status_[user]=status;
    }
    void setGamerZone(const std::string& user,const std::string& zone) override {
        require(user);
        static const char* const zones[]={"unknown","recreation","pro","family","underground"};
        const auto found=std::find(std::begin(zones),std::end(zones),zone);
        if(found==std::end(zones))throw ServiceOperationError("INVALID_ARGUMENT");
        for(auto& person:identities_)if(person.userId==user)person.gamerZone=static_cast<int>(found-std::begin(zones));
    }
    std::vector<std::string> blockedTags(const std::string& user) {
        std::vector<std::string> tags;for(const auto& id:blocks_[user])tags.push_back(profileById(id).gamertag);return tags;
    }
    bool blockedBetween(const std::string& a,const std::string& b) {return blocks_[a].contains(b)||blocks_[b].contains(a);}
    std::vector<std::string> blockedPlayers(const std::string& user) override {require(user);return blockedTags(user);}
    void setBlocked(const std::string& user,const std::string& tag,bool blocked) override {
        require(user);const auto target=profile(tag).userId;if(target==user)throw ServiceOperationError("INVALID_ARGUMENT");
        if(!blocked){blocks_[user].erase(target);return;}
        blocks_[user].insert(target);edges_.erase({target,user});edges_.erase({user,target});
    }
    void sendMessage(const std::string& user,const std::vector<std::string>& tags,const std::string& text) override {
        require(user);if(tags.empty()||tags.size()>100||text.size()>256)throw ServiceOperationError("INVALID_ARGUMENT");
        for(const auto& tag:tags)if(blockedBetween(user,profile(tag).userId))throw ServiceOperationError("NOT_AUTHORIZED");
        for(const auto& tag:tags){const auto target=profile(tag).userId;if(target==user)throw ServiceOperationError("INVALID_ARGUMENT");
            ServiceMessage message;message.id=std::string(28,'0')+std::to_string(1000+(++messageSequence_%9000));message.sender=profileById(user).gamertag;
            message.text=text;message.created=messageSequence_;inbox_[target].insert(inbox_[target].begin(),std::move(message));}
    }
    ServiceMessagePage messages(const std::string& user,int start,int limit) override {
        require(user);
        // The service's own bounds (protocol v1, messages.list: start 0..100, limit 1..32): a client that asks for more is refused.
        if(start<0||start>100||limit<1||limit>32)throw ServiceOperationError("INVALID_ARGUMENT");
        ServiceMessagePage page;page.start=start;const auto& box=inbox_[user];page.total=static_cast<int>(box.size());
        for(const auto& message:box)if(!message.read)++page.unread;
        for(int index=start;index<static_cast<int>(box.size())&&index<start+limit;++index)page.messages.push_back(box[static_cast<std::size_t>(index)]);
        return page;
    }
    void updateMessage(const std::string& user,const std::string& message,bool remove) override {
        require(user);auto& box=inbox_[user];
        const auto found=std::find_if(box.begin(),box.end(),[&](const auto& value){return value.id==message;});
        if(found==box.end())throw ServiceOperationError("NOT_FOUND");
        if(remove)box.erase(found);else found->read=true;
    }
    void reviewPlayer(const std::string& user,const std::string& tag,const std::string& rating) override {
        require(user);const auto target=profile(tag).userId;if(target==user)throw ServiceOperationError("INVALID_ARGUMENT");
        if(rating=="clear")reviews_.erase({user,target});
        else if(rating=="prefer"||rating=="avoid")reviews_[{user,target}]=rating;
        else throw ServiceOperationError("INVALID_ARGUMENT");
    }
    /** @brief Deterministic view of recorded reviews for tests. */
    const std::map<std::pair<std::string,std::string>,std::string>& reviews()const{return reviews_;}
    ServiceParty party(const std::string& user) override {
        require(user);
        ServiceParty value;
        if(const auto found=partyOf_.find(user);found!=partyOf_.end()) {
            const auto& party=parties_.at(found->second);
            value.id=found->second;value.leaderId=party.leader;
            for(const auto& id:party.members) {
                const auto person=std::find_if(identities_.begin(),identities_.end(),[&](const auto& p){return p.userId==id;});
                ServicePartyMember member;member.userId=id;member.gamertag=person==identities_.end()?id:person->gamertag;
                member.online=std::find(slots_.begin(),slots_.end(),id)!=slots_.end()||remoteOnline_.contains(id);
                member.presence=member.online?presence_[id]:"";
                member.away=member.online&&status_[id]=="away";member.busy=member.online&&status_[id]=="busy";
                member.joinable=member.online&&joinable_.contains(id);
                value.members.push_back(std::move(member));
            }
        }
        for(const auto& [id,party]:parties_)
            if(const auto invited=party.invitations.find(user);invited!=party.invitations.end()) {
                const auto sender=std::find_if(identities_.begin(),identities_.end(),[&](const auto& p){return p.userId==invited->second;});
                value.invitations.push_back({id,invited->second,sender==identities_.end()?invited->second:sender->gamertag,static_cast<int>(party.members.size())});
            }
        return value;
    }
    ServiceParty changeParty(const std::string& user,const std::string& action,const std::string& argument) override {
        require(user);
        auto leave=[&](const std::string& party) {
            auto& value=parties_.at(party);
            std::erase(value.members,user);partyOf_.erase(user);
            if(value.members.empty()){parties_.erase(party);return;}
            if(value.leader==user)value.leader=value.members.front();
        };
        if(action=="invite") {
            const auto target=profile(argument).userId;
            if(target==user)throw ServiceOperationError("INVALID_ARGUMENT");
            if(!edges_.contains({user,target})||!edges_.contains({target,user}))throw ServiceOperationError("NOT_AUTHORIZED");
            if(!partyOf_.contains(user)) {
                const auto id=std::string(28,'0')+std::to_string(1000+(++partySequence_)).substr(0,4);
                parties_[id]=FakeParty{user,{user},{}};partyOf_[user]=id;
            }
            auto& party=parties_.at(partyOf_.at(user));
            if(std::find(party.members.begin(),party.members.end(),target)!=party.members.end())throw ServiceOperationError("CONFLICT");
            if(party.members.size()+party.invitations.size()>=8&&!party.invitations.contains(target))throw ServiceOperationError("LIMIT_EXCEEDED");
            party.invitations[target]=user;
        } else if(action=="accept") {
            const auto found=parties_.find(argument);
            if(found==parties_.end()||!found->second.invitations.contains(user))throw ServiceOperationError("NOT_FOUND");
            if(partyOf_[user]!=argument) {
                if(found->second.members.size()>=8)throw ServiceOperationError("LIMIT_EXCEEDED");
                if(const auto previous=partyOf_.find(user);previous!=partyOf_.end()&&!previous->second.empty())leave(previous->second);
                parties_.at(argument).members.push_back(user);partyOf_[user]=argument;
            }
            parties_.at(argument).invitations.erase(user);
        } else if(action=="decline") {
            if(const auto found=parties_.find(argument);found!=parties_.end())found->second.invitations.erase(user);
        } else if(action=="leave") {
            if(const auto found=partyOf_.find(user);found!=partyOf_.end())leave(found->second);
        } else {
            throw ServiceOperationError("INVALID_ARGUMENT");
        }
        std::erase_if(partyOf_,[](const auto& entry){return entry.second.empty();});
        return party(user);
    }
    /** @brief Fixture: whether an account is in a joinable game (friends and party members see it). */
    void setJoinable(const std::string& user,bool joinable){if(joinable)joinable_.insert(user);else joinable_.erase(user);}
    void setVoice(const std::string& user,bool voice){if(voice)voice_.insert(user);else voice_.erase(user);}
    std::vector<ServiceLeaderboardInfo> leaderboards() override {
        if(std::all_of(slots_.begin(),slots_.end(),[](const auto& id){return id.empty();}))throw Unavailable("No authenticated fixture gamer.");
        std::vector<ServiceLeaderboardInfo> list;
        for(const auto& board:boards_)list.push_back({board.key,board.mode,board.ascending,board.arbitrated,static_cast<long long>(board.entries.size())});
        std::ranges::sort(list,[](const auto& a,const auto& b){return std::tie(a.key,a.mode)<std::tie(b.key,b.mode);});
        return list;
    }
    ServiceLeaderboardPage readLeaderboard(const std::string& key,int mode,int start,int size,const std::string& pivot,const std::optional<std::vector<std::string>>& gamers) override {
        if(std::all_of(slots_.begin(),slots_.end(),[](const auto& id){return id.empty();}))throw Unavailable("No authenticated fixture gamer.");
        const auto board=std::find_if(boards_.begin(),boards_.end(),[&](const auto& value){return value.key==key&&value.mode==mode;});
        if(board==boards_.end())throw Unavailable("Fixture leaderboard not found.");
        auto entries=board->entries;
        std::sort(entries.begin(),entries.end(),[&](const auto& a,const auto& b){return a.rating==b.rating?a.userId<b.userId:(board->ascending?a.rating<b.rating:a.rating>b.rating);});
        for(std::size_t i=0;i<entries.size();++i)entries[i].rank=static_cast<int>(i)+1;
        if(gamers)std::erase_if(entries,[&](const auto& row){return std::find(gamers->begin(),gamers->end(),row.gamertag)==gamers->end();});
        if(!pivot.empty()) {
            const auto found=std::find_if(entries.begin(),entries.end(),[&](const auto& row){return row.gamertag==pivot;});
            start=found==entries.end()?0:std::max(0,static_cast<int>(std::distance(entries.begin(),found))-size/2);
        }
        ServiceLeaderboardPage page;page.start=start;page.total=static_cast<int>(entries.size());
        for(int i=start;i<page.total&&static_cast<long long>(i)<static_cast<long long>(start)+size;++i)page.entries.push_back(entries[static_cast<std::size_t>(i)]);
        return page;
    }
    std::string beginLeaderboardGame(const std::vector<std::string>& users) override {
        if(users.empty()||users.size()>4)throw Unavailable("Invalid fixture game membership.");
        for(const auto& user:users)require(user);const auto id="fixture-game-"+std::to_string(++gameSequence_);games_[id]=users;return id;
    }
    void abortLeaderboardGame(const std::string& gameplay,const std::string& owner) override {
        require(owner);if(!games_.contains(gameplay))return;
        if(games_.at(gameplay).front()!=owner)throw Unavailable("Invalid fixture game owner.");games_.erase(gameplay);
    }
    void commitLeaderboardGame(const std::string& gameplay,const std::string& owner,const std::vector<ServiceLeaderboardWrite>& rows,
        const std::optional<ServiceArbitration>& arbitration) override {
        require(owner);if(!games_.contains(gameplay)||games_[gameplay].front()!=owner)throw Unavailable("Invalid fixture game scope.");
        // Deterministic model of the server's Ranked policy: one report per machine, strict majority.
        // The server fixes a round's roster when play starts; the fixture fixes it at the first report.
        std::optional<ServiceSessionSnapshot> round;
        if(arbitration) {
            ServiceSessionSnapshot current;
            try{current=directory_->get(owner,arbitration->session);}catch(const ServiceOperationError&){throw ServiceOperationError("NOT_FOUND");}
            if(current.kind!=ServiceSessionKind::Ranked)throw ServiceOperationError("NOT_FOUND");
            auto& roster=rounds_.try_emplace(arbitration->session,current).first->second;
            round=roster;round->machine=current.machine;
        }
        std::vector<ServiceLeaderboardWrite> direct,reported;
        for(const auto& row:rows) {
            const auto board=std::find_if(boards_.begin(),boards_.end(),[&](const auto& value){return value.key==row.key&&value.mode==row.mode;});
            if(board==boards_.end())throw Unavailable("Fixture board not found.");
            if(board->arbitrated) {
                const bool member=round&&std::any_of(round->members.begin(),round->members.end(),[&](const auto& value){return value.userId==row.userId;});
                if(!member)throw ServiceOperationError("NOT_AUTHORIZED");
                reported.push_back(row);continue;
            }
            if(std::find(games_[gameplay].begin(),games_[gameplay].end(),row.userId)==games_[gameplay].end())throw ServiceOperationError("NOT_AUTHORIZED");
            direct.push_back(row);
        }
        for(const auto& row:direct)apply(row);
        if(round) {
            auto& reports=arbitration_[arbitration->session];reports[round->machine]=reported;
            std::set<std::string> machines;for(const auto& member:round->members)machines.insert(member.machine);
            if(reports.size()>=machines.size()) {
                std::map<std::tuple<std::string,std::string,int>,std::vector<ServiceLeaderboardWrite>> votes;
                for(const auto& [machine,report]:reports)for(const auto& row:report)votes[{row.userId,row.key,row.mode}].push_back(row);
                for(const auto& [identity,candidates]:votes)for(const auto& candidate:candidates) {
                    const auto agreeing=std::count(candidates.begin(),candidates.end(),candidate);
                    // Strict majority among the machines that reported this row, as the server does.
                    if(agreeing*2>static_cast<long>(candidates.size())){apply(candidate);break;}
                }
                arbitration_.erase(arbitration->session);rounds_.erase(arbitration->session);
            }
        }
        games_.erase(gameplay);
    }
    std::vector<ServiceAvatarRecord> avatars(const std::vector<std::string>& ids,const AvatarClientCatalogs&) override {
        std::lock_guard guard(avatarLock_);
        if(avatarsUnreachable_)throw Unavailable("Fake fixture is unreachable.");
        ++traffic_.avatarReads;
        std::vector<ServiceAvatarRecord> out;
        for(const auto& id:ids) {
            auto person=std::find_if(identities_.begin(),identities_.end(),[&](const auto& value){return value.userId==id;});
            ServiceAvatarRecord record;
            if(person!=identities_.end()&&!person->avatar.empty()) {
                record.description=person->avatar;
                record.revision=revisions_[id];
            }
            out.push_back(std::move(record));
        }
        return out;
    }
    long long setAvatar(const std::string& userId,const std::vector<unsigned char>& description) override {
        std::lock_guard guard(avatarLock_);
        if(avatarsUnreachable_)throw Unavailable("Fake fixture is unreachable.");
        for(auto& person:identities_)if(person.userId==userId){person.avatar=description;return ++revisions_[userId];}
        throw ServiceError("NOT_FOUND");
    }
    void replaceAvatar(const std::string& userId,std::vector<unsigned char> description,bool newRevision) {
        std::lock_guard guard(avatarLock_);
        for(auto& person:identities_)if(person.userId==userId){person.avatar=std::move(description);if(newRevision)++revisions_[userId];}
    }
    void setAvatarsUnreachable(bool failing){std::lock_guard guard(avatarLock_);avatarsUnreachable_=failing;}
    std::string avatarCatalog(int) override {
        std::lock_guard guard(avatarLock_);
        if(avatarCatalog_.empty())throw Unavailable("Fake fixture has no avatar catalog.");
        return avatarCatalog_;
    }
    int newestAvatarCatalog() override {
        std::lock_guard guard(avatarLock_);
        if(avatarCatalog_.empty())throw Unavailable("Fake fixture has no avatar catalog.");
        // As the service client does: a development catalog below the baseline is not a catalog.
        const int version=Json::parse(avatarCatalog_).value("catalogVersion",0);
        if(!Avatars::isSupportedCatalogVersion(version))throw Unavailable("Invalid avatar catalog response.");
        return version;
    }
    std::vector<unsigned char> asset(const std::string& hash) override {
        return catalogFile(hash,0);
    }
    std::string avatarCatalogPack(int version) override {
        std::lock_guard guard(avatarLock_);
        ++traffic_.packReads;
        if(avatarCatalog_.empty())throw Unavailable("Fake fixture has no avatar catalog.");
        // The descriptor the service derives from an imported catalog.
        const auto manifest=Json::parse(avatarCatalog_,nullptr,false);
        if(manifest.is_discarded()||!manifest.is_object()||manifest.value("catalogVersion",0)!=version)throw ServiceError("NOT_FOUND");
        unsigned long long total=0;
        for(const auto& asset:manifest.at("assets"))total+=asset.value("size",0ull);
        // Format 3 names the kinds it introduced and the items a revision marks so.
        Json formats=Json::array({1,2});
        auto extended=[](const Json& entry){
            static const std::set<std::string> kinds{"ears","earrings","wristwear","gloves","rings","eyebrows","eyeShadow","eyeliner","blush","lips"};
            return entry.value("format",1)==3||kinds.contains(entry.value("slot",std::string()));
        };
        for(const auto* list:{"items","featureItems","faceStyles"})
            if(manifest.contains(list)&&formats.size()==2&&std::ranges::any_of(manifest[list],extended))formats.push_back(3);
        return Json{{"version",version},{"packFormat",1},{"reader",manifest.value("reader",1)},{"descriptionFormats",formats},
                    {"manifestSha256",manifestHash()},{"manifestSize",avatarCatalog_.size()},{"totalBytes",total}}.dump();
    }
    std::vector<unsigned char> catalogFile(const std::string& hash,std::size_t) override {
        std::lock_guard guard(avatarLock_);
        if(fileSuccesses_==0)throw Unavailable("Fake fixture's file endpoint is off.");
        if(fileSuccesses_>0)--fileSuccesses_;
        ++traffic_.fileDownloads;
        // The manifest is a file too, found by its hash like every other.
        if(!avatarCatalog_.empty()&&hash==manifestHash())return {avatarCatalog_.begin(),avatarCatalog_.end()};
        auto found=avatarAssets_.find(hash);
        if(found==avatarAssets_.end())throw Unavailable("Fake fixture has no such asset.");
        return found->second;
    }
    std::string manifestHash() const {
        return sha256Hex(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(avatarCatalog_.data()),avatarCatalog_.size()));
    }
    AvatarCatalogPolicy avatarCatalogPolicy() const override {std::lock_guard guard(avatarLock_);return policy_;}
    void setAvatarCatalog(std::string manifest,std::map<std::string,std::vector<unsigned char>> assets) {
        std::lock_guard guard(avatarLock_);
        avatarCatalog_=std::move(manifest);avatarAssets_=std::move(assets);
    }
    FakeAvatarTraffic traffic() const {std::lock_guard guard(avatarLock_);return traffic_;}
    void setFileSuccesses(int successes) {std::lock_guard guard(avatarLock_);fileSuccesses_=successes;}
    void setPolicy(AvatarCatalogPolicy policy) {std::lock_guard guard(avatarLock_);policy_=policy;}
    void setRemotePresence(const std::string& user,bool online,const std::string& presence,const std::string& status) {
        if(online)remoteOnline_.insert(user);else remoteOnline_.erase(user);
        presence_[user]=presence;status_[user]=status;
    }
private:
    ServiceIdentity profileById(const std::string& id) {for(const auto& person:identities_)if(person.userId==id)return person;throw Unavailable("Unknown fixture gamer.");}
    void apply(const ServiceLeaderboardWrite& row) {
        auto& board=*std::find_if(boards_.begin(),boards_.end(),[&](const auto& value){return value.key==row.key&&value.mode==row.mode;});
        auto entry=std::find_if(board.entries.begin(),board.entries.end(),[&](const auto& value){return value.userId==row.userId;});
        if(entry==board.entries.end()){ServiceLeaderboardEntry value;value.userId=row.userId;value.gamertag=profileById(row.userId).gamertag;board.entries.push_back(value);entry=std::prev(board.entries.end());}
        else if(board.ascending?row.rating>=entry->rating:row.rating<=entry->rating)return;
        entry->rating=row.rating;entry->columns=row.columns;
    }
    std::string avatarCatalog_;
    mutable std::mutex avatarLock_;
    bool avatarsUnreachable_=false;
    std::map<std::string,long long> revisions_;
    FakeAvatarTraffic traffic_;
    int fileSuccesses_=-1;
    AvatarCatalogPolicy policy_;
    std::map<std::string,std::vector<unsigned char>> avatarAssets_;
    std::map<std::string,std::vector<ServiceMessage>> inbox_;
    std::map<std::pair<std::string,std::string>,std::string> reviews_;
    int messageSequence_=0;
    std::map<std::string,std::map<std::string,std::vector<ServiceLeaderboardWrite>>> arbitration_;
    std::map<std::string,ServiceSessionSnapshot> rounds_;
    std::map<std::string,std::vector<std::string>> games_;
    int gameSequence_=0;
    std::unique_ptr<IServiceSessionDirectory> directory_;
    // Signed in here, or (setFakeRemotePresence) online elsewhere.
    void require(const std::string& user) {if(user.empty()||(std::find(slots_.begin(),slots_.end(),user)==slots_.end()&&!remoteOnline_.contains(user)))throw Unavailable("Gamer signed out.");}
    std::vector<ServiceIdentity> identities_;
    std::vector<ServiceAchievement> catalog_;
    std::vector<ServiceLeaderboardFixture> boards_;
    std::array<std::string,4> slots_{};
    std::map<std::string,std::map<std::string,long long>> earned_;
    std::set<std::pair<std::string,std::string>> edges_;
    std::map<std::string,std::set<std::string>> blocks_;
    std::map<std::string,std::string> presence_,status_;
    std::set<std::string> remoteOnline_;
    struct FakeParty {std::string leader;std::vector<std::string> members;std::map<std::string,std::string> invitations;};
    std::map<std::string,FakeParty> parties_;
    std::map<std::string,std::string> partyOf_;
    std::set<std::string> joinable_;
    std::set<std::string> voice_;
    int partySequence_=0;
};
}
void setFakeAvatar(IGamerServicesBackend& fake,const std::string& userId,std::vector<unsigned char> description,bool newRevision) {
    auto* backend=dynamic_cast<FakeBackend*>(&fake);
    if(!backend)throw System::InvalidOperationException("Not a fake Gamer Services backend.");
    backend->replaceAvatar(userId,std::move(description),newRevision);
}
void setFakeAvatarsUnreachable(IGamerServicesBackend& fake,bool failing) {
    auto* backend=dynamic_cast<FakeBackend*>(&fake);
    if(!backend)throw System::InvalidOperationException("Not a fake Gamer Services backend.");
    backend->setAvatarsUnreachable(failing);
}
void setFakeAvatarCatalog(IGamerServicesBackend& fake,std::string manifest,std::map<std::string,std::vector<unsigned char>> assets) {
    auto* backend=dynamic_cast<FakeBackend*>(&fake);
    if(!backend)throw System::InvalidOperationException("Not a fake Gamer Services backend.");
    backend->setAvatarCatalog(std::move(manifest),std::move(assets));
}
FakeAvatarTraffic fakeAvatarTraffic(IGamerServicesBackend& fake) {
    auto* backend=dynamic_cast<FakeBackend*>(&fake);
    if(!backend)throw System::InvalidOperationException("Not a fake Gamer Services backend.");
    return backend->traffic();
}
void setFakeCatalogFileFailures(IGamerServicesBackend& fake,int successes) {
    auto* backend=dynamic_cast<FakeBackend*>(&fake);
    if(!backend)throw System::InvalidOperationException("Not a fake Gamer Services backend.");
    backend->setFileSuccesses(successes);
}
void setFakeRemotePresence(IGamerServicesBackend& fake,const std::string& userId,bool online,const std::string& presence,const std::string& status) {
    auto* backend=dynamic_cast<FakeBackend*>(&fake);
    if(!backend)throw System::InvalidOperationException("Not a fake Gamer Services backend.");
    backend->setRemotePresence(userId,online,presence,status);
}
void setFakeJoinable(IGamerServicesBackend& fake,const std::string& userId,bool joinable) {
    auto* backend=dynamic_cast<FakeBackend*>(&fake);
    if(!backend)throw System::InvalidOperationException("Not a fake Gamer Services backend.");
    backend->setJoinable(userId,joinable);
}
void setFakeVoice(IGamerServicesBackend& fake,const std::string& userId,bool voice) {
    auto* backend=dynamic_cast<FakeBackend*>(&fake);
    if(!backend)throw System::InvalidOperationException("Not a fake Gamer Services backend.");
    backend->setVoice(userId,voice);
}
void setFakeAvatarCatalogPolicy(IGamerServicesBackend& fake,AvatarCatalogPolicy policy) {
    auto* backend=dynamic_cast<FakeBackend*>(&fake);
    if(!backend)throw System::InvalidOperationException("Not a fake Gamer Services backend.");
    backend->setPolicy(policy);
}
std::shared_ptr<IGamerServicesBackend> makeFakeBackend(std::vector<ServiceIdentity> people,std::vector<ServiceAchievement> catalog,std::vector<ServiceLeaderboardFixture> boards) {
    return std::make_shared<FakeBackend>(std::move(people),std::move(catalog),std::move(boards));
}
}

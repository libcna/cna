// SPDX-License-Identifier: MS-PL
#include "QueuedBackend.hpp"
#include "CNA/Internal/GamerServices/VoiceMutes.hpp"
#include "CNA/Internal/GamerServices/AssetDiskCache.hpp"
#include "CNA/Internal/GamerServices/ContentHash.hpp"
// Header-only: CatalogReaderLevel, isSupportedCatalogVersion. The service client must not call into
// the avatar catalog.
#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include "CNA/Internal/GamerServices/BackendConfiguration.hpp"
#include "CNA/GamerServices/Configuration.hpp"
#include "CnaService/Protocol.hpp"
#include "CredentialStore.hpp"
#include "CNA/Internal/GamerServices/ServiceInvitations.hpp"
#include "ServiceSessionDirectoryClient.hpp"
#include <chrono>
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#ifdef CNA_SERVICE_TLS_TRANSPORT
#include <curl/curl.h>
#endif
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <set>
#include <mutex>
#include <random>
#include <thread>
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <limits>

namespace CNA::Internal::GamerServices {
using CnaService::Json;
namespace {
using Unavailable=Microsoft::Xna::Framework::GamerServices::GamerServicesNotAvailableException;
using ServiceError=ServiceOperationError;
// A single-threaded browser pumps queued failures at the ordinary Update boundary.
#ifdef __EMSCRIPTEN__
constexpr bool backgroundServiceWork = false;
#else
constexpr bool backgroundServiceWork = true;
#endif
// The CNA service's transport is libcurl's TLS. A build without it -- a browser, or an Android NDK
// build with no libcurl -- refuses the service instead of pretending to reach it.
#ifdef __EMSCRIPTEN__
constexpr const char* NoServiceTransport = "CNA browser service transport is not implemented.";
#else
constexpr const char* NoServiceTransport =
    "This build of CNA has no TLS transport for the CNA service: libcurl was not found when it was configured.";
#endif
std::atomic<int> heartbeatSeconds{30};
long long unixTime(){return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
ServiceIdentity identity(const Json& j) {
    ServiceIdentity value;
    value.userId=CnaService::stringField(j,"userId",64);value.gamertag=CnaService::stringField(j,"gamertag",32);
    value.displayName=CnaService::stringField(j,"displayName",128);value.motto=CnaService::stringField(j,"motto",256);
    value.region=CnaService::stringField(j,"region",16);value.picture=CnaService::stringField(j,"picture",64);
    for(auto field:{"gamerScore","totalAchievements"})
        if(!j.contains(field)||!j[field].is_number_integer()||j[field]<0||j[field]>2147483647)throw CnaService::Error("INVALID_RESPONSE");
    if(!j.contains("allowOnlineSessions")||!j["allowOnlineSessions"].is_boolean())throw CnaService::Error("INVALID_RESPONSE");
    value.gamerScore=j["gamerScore"].get<int>();value.totalAchievements=j["totalAchievements"].get<int>();
    // A server older than titlesPlayed leaves it out; one that sends it sends a valid count.
    if(j.contains("titlesPlayed")) {
        if(!j["titlesPlayed"].is_number_integer()||j["titlesPlayed"]<0||j["titlesPlayed"]>2147483647)throw CnaService::Error("INVALID_RESPONSE");
        value.titlesPlayed=j["titlesPlayed"].get<int>();
    }
    value.allowOnlineSessions=j["allowOnlineSessions"].get<bool>();
    // Servers older than these leave them out.
    if(j.contains("gamerZone")) {
        static const char* const zones[]={"unknown","recreation","pro","family","underground"};
        const auto zone=CnaService::stringField(j,"gamerZone",16);
        const auto found=std::find(std::begin(zones),std::end(zones),zone);
        if(found==std::end(zones))throw CnaService::Error("INVALID_RESPONSE");
        value.gamerZone=static_cast<int>(found-std::begin(zones));
    }
    if(j.contains("reputation")) {
        if(!j["reputation"].is_number()||j["reputation"].get<double>()<0.0||j["reputation"].get<double>()>5.0)throw CnaService::Error("INVALID_RESPONSE");
        value.reputation=j["reputation"].get<float>();
    }
    return value;
}
class OnlineBackend final : public QueuedBackend {
public:
    explicit OnlineBackend(CNA::GamerServices::Configuration config):QueuedBackend(backgroundServiceWork),config_(std::move(config)),credentials_(config_) {
        directory_=makeSessionDirectoryClient([this](const std::string& op,Json args,const std::string& actor,const std::vector<std::string>& users) {
            return request(op,std::move(args),tokenFor(actor),users);
        },[this](const std::string& name){return capabilities_.contains(name);});
        std::set<std::string> seen;
        for(int slot=0;slot<4;++slot)if(const auto stored=credentials_.load(slot)) {
            if(!seen.insert(stored->refreshToken).second||stored->expires<=unixTime()){credentials_.remove(slot);continue;}
            {std::lock_guard lock(slotMutex_);slots_[slot].refresh=stored->refreshToken;slots_[slot].refreshExpires=stored->expires;}
        }
    }
    ~OnlineBackend() override {
#ifdef CNA_SERVICE_TLS_TRANSPORT
        events_.request_stop();if(events_.joinable())events_.join();
#endif
        stop();
    }
    bool serviceEnabled() const override {return !config_.endpoint.empty();}
    const CNA::GamerServices::Configuration& configuration() const {return config_;}
    IServiceSessionDirectory& sessionDirectory() override {return *directory_;}
    void signIn(int slot,std::string username,std::string password) override {
        slotGuard(slot);
        unsigned long long generation;{std::lock_guard lock(slotMutex_);generation=++slots_[slot].generation;slots_[slot].busy=true;}
        queue([this,slot,generation,username=std::move(username),password=std::move(password)]() mutable {
            BackendEvent event;event.slot=slot;std::string issuedToken;
            try {
                const auto result=request("auth.login",{{"username",username},{"password",password}},{});
                auto person=identity(result.at("identity"));issuedToken=CnaService::stringField(result,"token",128);
                applyPrivileges(person,result);
                if(issuedToken.size()!=64)throw CnaService::Error("INVALID_RESPONSE");
                std::lock_guard transport(transportMutex_);
                std::string previous,signedInToken;
                {std::lock_guard lock(slotMutex_);
                 for(int i=0;i<4;++i)if(i!=slot&&slots_[i].identity.userId==person.userId)throw CnaService::Error("ALREADY_SIGNED_IN");
                 if(slots_[slot].generation!=generation)throw CnaService::Error("STALE_AUTHENTICATION");
                 previous=slots_[slot].token;
                 auto replacement=decodeCredentials(result);replacement.generation=generation;signedInToken=replacement.token;slots_[slot]=std::move(replacement);}
                persist(slot);
                {std::lock_guard lock(slotMutex_);slots_[slot].busy=false;}
                issuedToken.clear();
                if(!previous.empty()){try{(void)exchange("auth.logout",Json::object(),previous);}catch(...){}}
                readGameDefaults(person,signedInToken);
                readBlocked(person,signedInToken);
                event.type=BackendEvent::Type::SignedIn;event.identity=std::move(person);
            }catch(const std::exception& failure){
                if(issuedToken.size()==64){try{(void)request("auth.logout",Json::object(),issuedToken);}catch(...){}}
                {std::lock_guard lock(slotMutex_);if(slots_[slot].generation==generation)slots_[slot].busy=false;}
                event.type=BackendEvent::Type::Failed;event.error="Sign-in failed.";
                // The one reason the player can act on: the service no longer accepts this game version.
                if(const auto* refused=dynamic_cast<const ServiceError*>(&failure);refused&&refused->code=="UPDATE_REQUIRED")
                    event.error="UPDATE_REQUIRED";
            }
            std::fill(password.begin(),password.end(),'\0');return event;
        });
    }
    void signOut(int slot) override {
        slotGuard(slot);
        unsigned long long generation;{std::lock_guard lock(slotMutex_);generation=++slots_[slot].generation;slots_[slot].busy=true;}
        queue([this,slot,generation]{
            BackendEvent event;event.type=BackendEvent::Type::SignedOut;event.slot=slot;
            Slot previous;{std::lock_guard lock(slotMutex_);previous=slots_[slot];slots_[slot]={};slots_[slot].generation=generation;}
            credentials_.remove(slot);event.identity=previous.identity;
            if(!previous.token.empty()){try{(void)request("auth.logout",Json::object(),previous.token);}catch(...){}}
            return event;
        });
    }
    std::vector<BackendEvent> pump() override {
        const auto timestamp=unixTime();
        startEvents();
        for(int slot=0;slot<4;++slot) {
            unsigned long long generation=0;bool schedule=false;
            {std::lock_guard lock(slotMutex_);auto& state=slots_[slot];
             if(!state.busy&&state.retryAt<=timestamp&&(!state.token.empty()||!state.refresh.empty())&&
                (state.expires<=timestamp+300||state.heartbeatAt<=timestamp)) {state.busy=true;generation=state.generation;schedule=true;}}
            if(schedule)try {
                queue([this,slot,generation]{
                    bool succeeded=false;
                    try {
                        std::lock_guard transport(transportMutex_);negotiateLocked();
                        Slot state;{std::lock_guard lock(slotMutex_);state=slots_[slot];}
                        if(state.generation==generation) {
                            if(state.expires<=unixTime()+300&&!state.refresh.empty())renewLocked(slot,generation);
                            else if(capabilities_.contains("heartbeat")&&!state.token.empty()) {
                                // With the heartbeat the client says whether it can talk now (XNA
                                // FriendGamer.HasVoice as friends see it); one boolean, no device detail.
                                Json ping=Json::object();
                                if(capabilities_.contains("friend-voice"))
                                    ping["voice"]=localVoiceCapable()&&state.identity.communication!="blocked";
                                try{publishPolicyLocked(slot,generation,exchange("auth.ping",ping,state.token));}
                                catch(const ServiceError& error){if(error.code!="UNAUTHENTICATED")throw;renewLocked(slot,generation);}
                            }
                            succeeded=true;
                        }
                    }catch(const ServiceError& error) {if(error.code=="UNAUTHENTICATED")invalidateSlot(slot,generation);}
                     catch(...) {}
                    {std::lock_guard lock(slotMutex_);auto& state=slots_[slot];if(state.generation==generation){state.busy=false;
                        if(succeeded){state.failures=0;state.retryAt=0;state.heartbeatAt=unixTime()+heartbeatSeconds;}
                        else {state.failures=std::min(state.failures+1,6);state.retryAt=unixTime()+std::min(300,10*(1<<state.failures));}}}
                    BackendEvent event;event.type=BackendEvent::Type::Completion;return event;
                });
            }catch(...) {std::lock_guard lock(slotMutex_);slots_[slot].busy=false;slots_[slot].retryAt=timestamp+5;}
        }
        return QueuedBackend::pump();
    }
    ServiceIdentity profile(const std::string& tag,const std::string& actor = {}) override {return identity(request("profile.get",{{"gamertag",tag}},tokenFor(actor)));}
    std::vector<ServiceAchievement> achievements(const std::string& user) override {
        const auto result=request("achievements.list",Json::object(),tokenFor(user));
        const auto& entries=result.at("achievements");if(!entries.is_array()||entries.size()>128)throw Unavailable("Invalid achievement response.");
        std::vector<ServiceAchievement> values;
        for(const auto& entry:entries) {
            ServiceAchievement a;a.key=CnaService::stringField(entry,"key",64);a.name=CnaService::stringField(entry,"name",128);
            a.description=CnaService::stringField(entry,"description",1024);a.howToEarn=CnaService::stringField(entry,"howToEarn",1024);
            a.picture=CnaService::stringField(entry,"picture",64);
            if(!entry.at("score").is_number_integer()||entry["score"]<0||entry["score"]>1000||
               !entry.at("earnedTicks").is_number_integer()||entry["earnedTicks"]<0||!entry.at("display").is_boolean())throw Unavailable("Invalid achievement response.");
            a.score=entry["score"].get<int>();a.earnedTicks=entry["earnedTicks"].get<long long>();a.displayBeforeEarned=entry["display"].get<bool>();values.push_back(std::move(a));
        }return values;
    }
    std::string award(const std::string& user,const std::string& key) override {
        const auto result=request("achievements.award",{{"key",key}},tokenFor(user));
        // A service older than these fields answers with an empty object.
        if(!result.is_object()||!result.contains("awarded")||!result["awarded"].is_boolean()||!result["awarded"].get<bool>())return {};
        return CnaService::stringField(result,"name",128);
    }
    std::vector<ServiceFriend> friends(const std::string& user) override {
        const auto result=request("friends.list",Json::object(),tokenFor(user));const auto& entries=result.at("friends");
        if(!entries.is_array()||entries.size()>256)throw Unavailable("Invalid friend response.");
        std::vector<ServiceFriend> values;for(const auto& e:entries) {
            if(!e.at("online").is_boolean())throw Unavailable("Invalid friend response.");
            ServiceFriend friendState;friendState.gamertag=CnaService::stringField(e,"gamertag",32);friendState.online=e["online"].get<bool>();
            if(e.contains("userId"))friendState.userId=CnaService::stringField(e,"userId",64);
            for(const auto* key:{"accepted","requestSent","requestReceived"})if(!e.at(key).is_boolean())throw Unavailable("Invalid friend response.");
            friendState.accepted=e["accepted"].get<bool>();friendState.requestSent=e["requestSent"].get<bool>();friendState.requestReceived=e["requestReceived"].get<bool>();
            // A server older than these flags leaves them out; one that sends them sends booleans.
            for(auto [key,target]:{std::pair{"joinable",&friendState.joinable},std::pair{"inviteReceivedFrom",&friendState.inviteReceivedFrom},
                std::pair{"inviteSentTo",&friendState.inviteSentTo},std::pair{"inviteAccepted",&friendState.inviteAccepted},
                std::pair{"inviteRejected",&friendState.inviteRejected},std::pair{"away",&friendState.away},std::pair{"busy",&friendState.busy},
                std::pair{"hasVoice",&friendState.hasVoice}}) {
                if(!e.contains(key))continue;
                if(!e[key].is_boolean())throw Unavailable("Invalid friend response.");
                *target=e[key].get<bool>();
            }
            friendState.presence=CnaService::stringField(e,"presenceText",256);values.push_back(std::move(friendState));
        }return values;
    }
    void changeFriend(const std::string& user,const std::string& tag,const std::string& action) override {
        if(action!="add"&&action!="accept"&&action!="remove")throw Unavailable("Invalid friendship operation.");
        (void)request("friends."+action,{{"gamertag",tag}},tokenFor(user));
    }
    void setPresence(const std::string& user,int mode,const std::string& text) override {
        (void)request("presence.set",{{"mode",mode},{"text",text}},tokenFor(user));
    }
    void setGamerZone(const std::string& user,const std::string& zone) override {
        if(!capabilities_.contains("gamer-zone"))throw Unavailable("CNA service gamer-zone capability missing.");
        (void)request("profile.setGamerZone",{{"gamerZone",zone}},tokenFor(user));
    }
    void setPresenceStatus(const std::string& user,const std::string& status) override {
        if(!capabilities_.contains("presence-status"))throw Unavailable("CNA service presence-status capability missing.");
        if(status!="online"&&status!="away"&&status!="busy")throw Unavailable("Invalid presence status.");
        (void)request("presence.status",{{"status",status}},tokenFor(user));
    }
    void sendMessage(const std::string& user,const std::vector<std::string>& tags,const std::string& text) override {
        (void)request("messages.send",{{"gamertags",tags},{"text",text}},tokenFor(user));
    }
    std::vector<std::string> blockedPlayers(const std::string& user) override {
        return parseBlocked(request("privacy.list",Json::object(),tokenFor(user)));
    }
    void setBlocked(const std::string& user,const std::string& tag,bool blocked) override {
        (void)request(blocked?"privacy.block":"privacy.unblock",{{"gamertag",tag}},tokenFor(user));
    }
    ServiceMessagePage messages(const std::string& user,int start,int limit) override {
        const auto result=request("messages.list",{{"start",start},{"limit",limit}},tokenFor(user));
        ServiceMessagePage page;
        for(const auto* key:{"start","total","unread"})if(!result.at(key).is_number_integer()||result[key]<0||result[key]>100000)throw Unavailable("Invalid message response.");
        page.start=result["start"].get<int>();page.total=result["total"].get<int>();page.unread=result["unread"].get<int>();
        const auto& rows=result.at("messages");
        if(page.start!=start||!rows.is_array()||rows.size()>static_cast<std::size_t>(limit)||page.unread>page.total)throw Unavailable("Invalid message response.");
        for(const auto& row:rows) {
            ServiceMessage message;message.id=CnaService::stringField(row,"message",32);message.sender=CnaService::stringField(row,"sender",32);
            message.text=CnaService::stringField(row,"text",256);
            if(message.id.size()!=32||!row.at("created").is_number_integer()||!row.at("read").is_boolean())throw Unavailable("Invalid message response.");
            message.created=row["created"].get<long long>();message.read=row["read"].get<bool>();page.messages.push_back(std::move(message));
        }
        return page;
    }
    void updateMessage(const std::string& user,const std::string& message,bool remove) override {
        (void)request(remove?"messages.delete":"messages.read",{{"message",message}},tokenFor(user));
    }
    void reviewPlayer(const std::string& user,const std::string& tag,const std::string& rating) override {
        if(rating!="prefer"&&rating!="avoid"&&rating!="clear")throw Unavailable("Invalid player review.");
        (void)request("reviews.submit",{{"gamertag",tag},{"rating",rating}},tokenFor(user));
    }
    static ServiceParty partyFrom(const Json& result) {
        ServiceParty value;
        const auto& party=result.at("party");
        auto flag=[](const Json& object,const char* key){const auto& v=object.at(key);if(!v.is_boolean())throw Unavailable("Invalid party response.");return v.get<bool>();};
        if(!party.is_null()) {
            value.id=CnaService::stringField(party,"id",32);value.leaderId=CnaService::stringField(party,"leaderId",64);
            const auto& members=party.at("members");
            if(value.id.size()!=32||!members.is_array()||members.empty()||members.size()>8)throw Unavailable("Invalid party response.");
            for(const auto& row:members) {
                ServicePartyMember member;
                member.userId=CnaService::stringField(row,"userId",64);member.gamertag=CnaService::stringField(row,"gamertag",32);
                member.online=flag(row,"online");member.presence=CnaService::stringField(row,"presenceText",256);
                member.away=flag(row,"away");member.busy=flag(row,"busy");member.joinable=flag(row,"joinable");
                value.members.push_back(std::move(member));
            }
        }
        const auto& invitations=result.at("invitations");
        if(!invitations.is_array()||invitations.size()>16)throw Unavailable("Invalid party response.");
        for(const auto& row:invitations) {
            ServicePartyInvitation invitation;
            invitation.party=CnaService::stringField(row,"party",32);invitation.senderId=CnaService::stringField(row,"senderId",64);
            invitation.senderGamertag=CnaService::stringField(row,"senderGamertag",32);
            const auto& members=row.at("members");
            if(invitation.party.size()!=32||!members.is_number_integer()||members.get<long long>()<0||members.get<long long>()>8)throw Unavailable("Invalid party response.");
            invitation.members=static_cast<int>(members.get<long long>());
            value.invitations.push_back(std::move(invitation));
        }
        return value;
    }
    ServiceParty party(const std::string& user) override {return partyFrom(request("parties.get",Json::object(),tokenFor(user)));}
    ServiceParty changeParty(const std::string& user,const std::string& action,const std::string& argument) override {
        if(action=="invite")return partyFrom(request("parties.invite",{{"gamertag",argument}},tokenFor(user)));
        if(action=="accept"||action=="decline")return partyFrom(request("parties."+action,{{"party",argument}},tokenFor(user)));
        if(action=="leave")return partyFrom(request("parties.leave",Json::object(),tokenFor(user)));
        throw Unavailable("Invalid party operation.");
    }
    std::vector<ServiceLeaderboardInfo> leaderboards() override {
        const auto result=request("leaderboards.list",Json::object(),tokenFor({}));
        const auto& boards=result.at("boards");
        if(!boards.is_array()||boards.size()>256)throw Unavailable("Invalid leaderboard list.");
        std::vector<ServiceLeaderboardInfo> list;
        for(const auto& board:boards) {
            ServiceLeaderboardInfo info;
            info.key=CnaService::stringField(board,"key",64);
            if(!CnaService::identifier(info.key))throw Unavailable("Invalid leaderboard key.");
            const auto& mode=board.at("mode");
            const auto& entries=board.at("entries");
            if(!mode.is_number_integer()||mode.get<long long>()<-2147483648LL||mode.get<long long>()>2147483647LL||
               !entries.is_number_integer()||entries.get<long long>()<0||!board.at("ascending").is_boolean()||!board.at("arbitrated").is_boolean())
                throw Unavailable("Invalid leaderboard list.");
            info.mode=static_cast<int>(mode.get<long long>());
            info.entries=entries.get<long long>();
            info.ascending=board["ascending"].get<bool>();
            info.arbitrated=board["arbitrated"].get<bool>();
            list.push_back(std::move(info));
        }
        return list;
    }
    ServiceLeaderboardPage readLeaderboard(const std::string& key,int mode,int start,int size,const std::string& pivot,const std::optional<std::vector<std::string>>& gamers) override {
        Json args{{"key",key},{"mode",mode},{"start",start},{"size",size}};
        if(!pivot.empty())args["pivot"]=pivot;
        if(gamers)args["gamers"]=*gamers;
        const auto result=request("leaderboards.read",std::move(args),tokenFor({}));
        auto integer=[](const Json& object,const char* name,long long low,long long high) {
            const auto& value=object.at(name);
            if(!value.is_number_integer()||(value.is_number_unsigned()&&value.get<unsigned long long>()>9223372036854775807ULL))throw Unavailable("Invalid leaderboard integer.");
            const auto number=value.get<long long>();if(number<low||number>high)throw Unavailable("Invalid leaderboard integer.");return number;
        };
        ServiceLeaderboardPage page;page.start=static_cast<int>(integer(result,"start",0,2147483647));page.total=static_cast<int>(integer(result,"total",0,2147483647));
        const auto& entries=result.at("entries");if(!entries.is_array()||entries.size()>static_cast<std::size_t>(size))throw Unavailable("Invalid leaderboard page.");
        if(static_cast<long long>(page.start)+entries.size()>page.total&&!entries.empty())throw Unavailable("Invalid leaderboard total.");
        std::set<std::string> seen;
        for(const auto& row:entries) {
            ServiceLeaderboardEntry entry;entry.userId=CnaService::stringField(row,"userId",64);entry.gamertag=CnaService::stringField(row,"gamertag",32);
            if(entry.userId.empty()||entry.gamertag.empty()||!seen.insert(entry.userId).second)throw Unavailable("Invalid leaderboard identity.");
            entry.rating=integer(row,"rating",std::numeric_limits<long long>::min(),std::numeric_limits<long long>::max());entry.rank=static_cast<int>(integer(row,"rank",1,2147483647));
            const auto& columns=row.at("columns");if(!columns.is_object()||columns.size()>32||columns.dump().size()>2048)throw Unavailable("Invalid leaderboard columns.");
            for(const auto& [name,field]:columns.items()) {
                if(!CnaService::identifier(name)||name.size()>64||!field.is_object()||field.size()!=2)throw Unavailable("Invalid leaderboard column.");
                ServiceLeaderboardColumn column;column.type=CnaService::stringField(field,"type",16);
                if(column.type=="string")column.value=CnaService::stringField(field,"value",256);
                else if(column.type=="stream") {
                    auto hex=CnaService::stringField(field,"value",MaxLeaderboardStreamBytes*2);
                    if(hex.size()%2||hex.find_first_not_of("0123456789abcdef")!=std::string::npos)throw Unavailable("Invalid leaderboard stream value.");
                    column.value=std::move(hex);
                }
                else if(column.type=="single"||column.type=="double") {
                    if(!field.at("value").is_number())throw Unavailable("Invalid leaderboard floating value.");
                    const auto number=field["value"].get<double>();if(!std::isfinite(number)||(column.type=="single"&&std::abs(number)>std::numeric_limits<float>::max()))throw Unavailable("Invalid leaderboard floating value.");column.value=number;
                }else {
                    long long low=std::numeric_limits<long long>::min(),high=std::numeric_limits<long long>::max();
                    if(column.type=="int32"){low=-2147483648LL;high=2147483647LL;}
                    else if(column.type=="datetime"){low=0;high=3155378975999999999LL;}
                    else if(column.type=="outcome"){low=0;high=3;}
                    else if(column.type!="int64"&&column.type!="timespan")throw Unavailable("Unknown leaderboard column type.");
                    column.value=integer(field,"value",low,high);
                }
                entry.columns.emplace(name,std::move(column));
            }
            page.entries.push_back(std::move(entry));
        }
        return page;
    }
    std::string beginLeaderboardGame(const std::vector<std::string>& users) override {
        if(users.empty()||users.size()>4)throw Unavailable("Invalid local leaderboard membership.");
        const auto result=request("leaderboards.game.begin",{{"kind","local"}},tokenFor(users.front()),users);
        auto id=CnaService::stringField(result,"gameplay",32);
        if(id.size()!=32||id.find_first_not_of("0123456789abcdef")!=std::string::npos)throw Unavailable("Invalid leaderboard gameplay identifier.");return id;
    }
    void abortLeaderboardGame(const std::string& gameplay,const std::string& owner) override {
        (void)request("leaderboards.game.abort",{{"gameplay",gameplay}},tokenFor(owner));
    }
    void commitLeaderboardGame(const std::string& gameplay,const std::string& owner,const std::vector<ServiceLeaderboardWrite>& rows,
        const std::optional<ServiceArbitration>& arbitration) override {
        if(arbitration&&!capabilities_.contains("ranked-arbitration"))throw Unavailable("CNA service ranked arbitration capability missing.");
        Json entries=Json::array();
        for(const auto& row:rows) {
            Json columns=Json::object();for(const auto& [name,column]:row.columns) {
                Json value;std::visit([&](const auto& scalar){value=scalar;},column.value);columns[name]={{"type",column.type},{"value",value}};
            }
            entries.push_back(Json{{"userId",row.userId},{"key",row.key},{"mode",row.mode},{"rating",row.rating},{"columns",columns}});
        }
        Json args{{"gameplay",gameplay},{"entries",entries}};
        if(arbitration)args["arbitration"]={{"session",arbitration->session},{"revision",arbitration->revision}};
        (void)request("leaderboards.game.commit",args,tokenFor(owner));
    }
    std::vector<ServiceAvatarRecord> avatars(const std::vector<std::string>& ids,const AvatarClientCatalogs& client) override {
        if(!capabilities_.contains("avatars"))throw Unavailable("CNA service avatars capability missing.");
        if(ids.empty()||ids.size()>16)throw Unavailable("Invalid avatar request.");
        // What this client can draw: the description formats it reads, the catalogs it has, and
        // whether (and how large) it installs the one a description names. The service answers
        // with the stored avatar when that is drawable here, else with a marked projection.
        Json args{{"userIds",ids},{"formats",Json::array({1,2,3})},{"reader",Avatars::CatalogReaderLevel}};
        // A caller that names no catalogs only reads descriptions; the service refuses an empty list.
        if(capabilities_.contains("avatar-catalog-packs")&&!client.versions.empty()) {
            Json catalogs=Json::array();
            for(auto version:client.versions)catalogs.push_back(version);
            const auto policy=avatarCatalogPolicy();
            args["catalogs"]=std::move(catalogs);
            args["catalogUpdates"]=policy.updates&&client.canInstall;
            args["maxCatalogBytes"]=policy.maximumBytes;
        }
        const auto result=request("avatars.get",args,tokenFor({}));
        const auto& entries=result.at("avatars");
        if(!entries.is_array()||entries.size()!=ids.size())throw Unavailable("Invalid avatar response.");
        std::vector<ServiceAvatarRecord> out;
        for(std::size_t index=0;index<ids.size();++index) {
            const auto& entry=entries[index];
            if(!entry.is_object()||!entry.contains("userId")||entry["userId"]!=ids[index]||!entry.contains("description"))
                throw Unavailable("Invalid avatar response.");
            ServiceAvatarRecord record;
            if(entry.contains("revision")&&entry["revision"].is_number_integer())record.revision=entry["revision"].get<long long>();
            record.projected=entry.contains("projected")&&entry["projected"].is_boolean()&&entry["projected"].get<bool>();
            const auto& text=entry["description"];
            if(text.is_null()){out.push_back(std::move(record));continue;}
            if(!text.is_string()||text.get_ref<const std::string&>().size()!=2042)throw Unavailable("Invalid avatar response.");
            const auto& hex=text.get_ref<const std::string&>();
            auto nibble=[](char c)->int{return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
            record.description.reserve(1021);
            for(std::size_t i=0;i<hex.size();i+=2) {
                const int high=nibble(hex[i]),low=nibble(hex[i+1]);
                if(high<0||low<0)throw Unavailable("Invalid avatar response.");
                record.description.push_back(static_cast<unsigned char>(high<<4|low));
            }
            out.push_back(std::move(record));
        }
        return out;
    }
    long long setAvatar(const std::string& userId,const std::vector<unsigned char>& description) override {
        if(!capabilities_.contains("avatars"))throw Unavailable("CNA service avatars capability missing.");
        if(description.size()!=1021)throw Unavailable("Invalid avatar description.");
        constexpr char digits[]="0123456789abcdef";std::string hex;hex.reserve(2042);
        for(auto byte:description){hex+=digits[byte>>4];hex+=digits[byte&15];}
        const auto result=request("avatars.set",{{"description",hex}},tokenFor(userId));
        if(!result.contains("revision")||!result["revision"].is_number_integer())throw Unavailable("Invalid avatar response.");
        return result["revision"].get<long long>();
    }
    std::string avatarCatalog(int version) override {
        if(!capabilities_.contains("avatars"))throw Unavailable("CNA service avatars capability missing.");
        // 0 asks for the newest; a development catalog below the baseline is never asked for or accepted.
        if(version!=0&&!Avatars::isSupportedCatalogVersion(version))throw Unavailable("Invalid avatar catalog version.");
        Json args=Json::object();if(version)args["version"]=version;
        const auto result=request("avatars.catalog",args,tokenFor({}));
        if(!result.contains("version")||!result["version"].is_number_integer()||(version&&result["version"]!=version)||
           !Avatars::isSupportedCatalogVersion(result["version"].get<long long>()))throw Unavailable("Invalid avatar catalog response.");
        if(result.contains("manifest")&&result["manifest"].is_object())return result["manifest"].dump();
        // A manifest larger than a message is named rather than sent, and read by hash like a pack's.
        if(!result.contains("manifestSha256")||!result["manifestSha256"].is_string()||!result.contains("manifestSize")||
           !result["manifestSize"].is_number_unsigned())throw Unavailable("Invalid avatar catalog response.");
        const auto bytes=catalogFile(result["manifestSha256"].get<std::string>(),result["manifestSize"].get<std::size_t>());
        return std::string(bytes.begin(),bytes.end());
    }
    int newestAvatarCatalog() override {
        if(!capabilities_.contains("avatars"))throw Unavailable("CNA service avatars capability missing.");
        // The version alone: a service from before catalog revisions sends the manifest anyway.
        const auto result=request("avatars.catalog",{{"manifest",false}},tokenFor({}));
        if(!result.contains("version")||!result["version"].is_number_integer()||
           !Avatars::isSupportedCatalogVersion(result["version"].get<long long>()))throw Unavailable("Invalid avatar catalog response.");
        return result["version"].get<int>();
    }
    std::vector<unsigned char> asset(const std::string& hash) override {
        if(hash.size()!=64||hash.find_first_not_of("0123456789abcdef")!=std::string::npos)throw Unavailable("Invalid service asset identifier.");
        const auto token=tokenFor({});
        std::lock_guard cacheLock(cacheMutex_);
        const AssetDiskCache cache(AssetDiskCache::defaultRoot(),prefix_);
        if(auto cached=cache.read(hash)) {
            // Cached content is immutable; the requester's authority is not. A one-byte read
            // applies the same current token/title/picture policy as an uncached download.
            const auto grant=request("assets.read",{{"hash",hash},{"offset",0},{"length",1}},token);
            if(CnaService::stringField(grant,"hash",64)!=hash||!grant.at("size").is_number_integer()||grant["size"]!=cached->size()||
               !grant.at("offset").is_number_integer()||grant["offset"]!=0)throw Unavailable("Invalid cached asset authorization response.");
            const auto mime=CnaService::stringField(grant,"mime",64),hex=CnaService::stringField(grant,"hex",2);
            constexpr char digits[]="0123456789abcdef";
            if((mime!="image/png"&&mime!="image/jpeg"&&mime!="model/gltf-binary")||hex.size()!=2||
               hex[0]!=digits[cached->front()>>4]||hex[1]!=digits[cached->front()&15])throw Unavailable("Invalid cached asset authorization response.");
            return std::move(*cached);
        }
        std::vector<unsigned char> bytes;long long expected=0;std::string mime;
        while(bytes.empty()||static_cast<long long>(bytes.size())<expected) {
            const auto part=request("assets.read",{{"hash",hash},{"offset",bytes.size()},{"length",12288}},token);
            if(CnaService::stringField(part,"hash",64)!=hash||!part.at("size").is_number_integer()||part["size"]<1||part["size"]>16777216||
               !part.at("offset").is_number_integer()||part["offset"]!=bytes.size())throw Unavailable("Invalid asset response.");
            const auto size=part["size"].get<long long>();const auto type=CnaService::stringField(part,"mime",64);
            if(type!="image/png"&&type!="image/jpeg"&&type!="model/gltf-binary")throw Unavailable("Unsupported service asset type.");
            if(expected&&(expected!=size||mime!=type))throw Unavailable("Changing immutable asset response.");
            expected=size;mime=type;const auto hex=CnaService::stringField(part,"hex",24576);
            if(hex.size()/2!=std::min<std::size_t>(12288,static_cast<std::size_t>(expected)-bytes.size())||hex.empty()||hex.size()%2||hex.find_first_not_of("0123456789abcdef")!=std::string::npos||bytes.size()+hex.size()/2>static_cast<std::size_t>(expected))
                throw Unavailable("Invalid asset chunk.");
            bytes.reserve(static_cast<std::size_t>(expected));
            auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
            for(std::size_t i=0;i<hex.size();i+=2)bytes.push_back(static_cast<unsigned char>((digit(hex[i])<<4)|digit(hex[i+1])));
        }
        if(sha256Hex(std::span<const std::uint8_t>(bytes.data(),bytes.size()))!=hash)throw Unavailable("Corrupt service asset.");
        // A full or unwritable cache still returns the asset.
        cache.write(hash,bytes);
        return bytes;
    }
    std::string avatarCatalogPack(int version) override {
        if(!Avatars::isSupportedCatalogVersion(version))throw Unavailable("Invalid avatar catalog version.");
        return request("avatars.catalogPack",{{"version",version}},tokenFor({})).dump();
    }
    std::vector<unsigned char> catalogFile(const std::string& hash,std::size_t size) override {
        if(hash.size()!=64||hash.find_first_not_of("0123456789abcdef")!=std::string::npos||size==0||size>(16u<<20))
            throw Unavailable("Invalid catalog file.");
        const auto token=tokenFor({});
        std::vector<unsigned char> bytes;
        {
            std::lock_guard lock(transportMutex_);
            try {negotiateLocked();} catch(const Unavailable&) {throw;} catch(...) {throw Unavailable("CNA service negotiation failed.");}
            if(!capabilities_.contains("files"))throw Unavailable("CNA service file capability missing.");
            bytes=download("/files/"+hash,size,latestToken(token));
        }
        if(bytes.size()!=size||sha256Hex(std::span<const std::uint8_t>(bytes.data(),bytes.size()))!=hash)
            throw Unavailable("Corrupt catalog file.");
        return bytes;
    }
    AvatarCatalogPolicy avatarCatalogPolicy() const override {
        return AvatarCatalogPolicy{config_.avatarCatalogUpdates,config_.maxAvatarCatalogBytes};
    }
private:
    struct Slot {
        ServiceIdentity identity;std::string token,previousToken,refresh;
        long long expires=0,refreshExpires=0,heartbeatAt=0,retryAt=0;
        int failures=0;
        unsigned long long generation=0;bool busy=false;
    };
    std::string tokenFor(const std::string& user) {
        std::lock_guard lock(slotMutex_);
        for(const auto& slot:slots_)if(!slot.token.empty()&&(user.empty()||slot.identity.userId==user))return slot.token;
        // Keep authentication failure distinguishable from transport unavailability
        // so each public API can apply its own XNA exception contract.
        throw ServiceError("UNAUTHENTICATED");
    }
    static std::size_t write(char* data,std::size_t size,std::size_t count,void* context) noexcept {
        auto& text=*static_cast<std::string*>(context);
        if(size && count>CnaService::MaxMessageBytes/size)return 0;
        const auto bytes=size*count;if(bytes>CnaService::MaxMessageBytes-text.size())return 0;
        try{text.append(data,bytes);}catch(...){return 0;}return bytes;
    }
    Json exchange(const std::string& op,Json args,const std::string& token) {
#ifndef CNA_SERVICE_TLS_TRANSPORT
        (void)op; (void)args; (void)token;
        throw Unavailable(NoServiceTransport);
#else
        static std::once_flag initialized;
        std::call_once(initialized,[]{if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK)throw Unavailable("Secure transport initialization failed.");});
        if(!(curl_version_info(CURLVERSION_NOW)->features&CURL_VERSION_SSL))throw Unavailable("libcurl has no TLS support.");
        if(config_.endpoint.empty())throw Unavailable("No CNA service endpoint configured.");
        const auto id=prefix_+"-"+std::to_string(++sequence_);
        Json request{{"v",1},{"id",id},{"game",config_.gameId},{"op",op},{"args",std::move(args)}};
        if(!token.empty())request["token"]=token;
        // Stated to a service that checks it; an older one would refuse the unknown field.
        if(!config_.titleVersion.empty()&&capabilities_.contains("title-version"))request["titleVersion"]=config_.titleVersion;
        const auto bytes=request.dump();if(bytes.size()>CnaService::MaxMessageBytes)throw Unavailable("Service request limit exceeded.");
        // One handle for every exchange (all under transportMutex_) keeps its connection open
        // between requests, sparing a TCP and TLS handshake each.
        if(!curl_)curl_.reset(curl_easy_init());
        if(!curl_)throw Unavailable("Service transport unavailable.");
        CURL* curl=curl_.get();
        std::string output;
        curl_easy_setopt(curl,CURLOPT_URL,config_.endpoint.c_str());
        curl_easy_setopt(curl,CURLOPT_PROTOCOLS_STR,config_.insecureLoopback?"https,http":"https");
        curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,0L);curl_easy_setopt(curl,CURLOPT_PROXY,"");
        curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L);
        curl_easy_setopt(curl,CURLOPT_SSLVERSION,CURL_SSLVERSION_TLSv1_2);
        if(!config_.caBundle.empty())curl_easy_setopt(curl,CURLOPT_CAINFO,config_.caBundle.c_str());
        curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT_MS,3000L);
        curl_easy_setopt(curl,CURLOPT_TIMEOUT_MS,10000L);
        curl_easy_setopt(curl,CURLOPT_POSTFIELDS,bytes.data());curl_easy_setopt(curl,CURLOPT_POSTFIELDSIZE,static_cast<long>(bytes.size()));
        curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,&OnlineBackend::write);curl_easy_setopt(curl,CURLOPT_WRITEDATA,&output);
        curl_slist* headers=curl_slist_append(nullptr,"Content-Type: application/json");
        std::unique_ptr<curl_slist,decltype(&curl_slist_free_all)> owned(headers,curl_slist_free_all);
        if(!headers)throw Unavailable("Service transport unavailable.");
        curl_easy_setopt(curl,CURLOPT_HTTPHEADER,headers);
        auto performed=curl_easy_perform(curl);
        // A connection lost after the request left may have lost only the response. A service that
        // keeps request outcomes commits an ID with its change, so asking once more with the same ID
        // either runs a request that never committed or answers from the record; it never runs one
        // twice. Results carrying a secret are not kept, so those are not asked again.
        if((performed==CURLE_GOT_NOTHING||performed==CURLE_SEND_ERROR||performed==CURLE_RECV_ERROR)&&
           capabilities_.contains("request-outcomes")&&op!="auth.login"&&op!="auth.refresh"&&op!="sessions.relayTicket") {
            output.clear();performed=curl_easy_perform(curl);
        }
        if(performed!=CURLE_OK)throw Unavailable("CNA service connection failed.");
        long status=0;curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);if(status!=200)throw Unavailable("CNA service HTTP failure.");
        const auto response=CnaService::parse(output,(op=="privacy.list"||op=="auth.ping")?1024:256);
        if(!response.is_object()||response.size()!=4||response.at("v")!=1||CnaService::stringField(response,"id",64)!=id||!response.at("result").is_object())
            throw Unavailable("CNA service protocol mismatch.");
        const auto error=CnaService::stringField(response,"error",64);
        if(error!="OK")throw ServiceError(error);
        return response["result"];
#endif
    }
    struct DownloadTarget {std::vector<unsigned char>* bytes;std::size_t limit;};
    static std::size_t receive(char* data,std::size_t size,std::size_t count,void* context) noexcept {
        auto& target=*static_cast<DownloadTarget*>(context);
        if(size&&count>target.limit/size)return 0;
        const auto bytes=size*count;if(bytes>target.limit-target.bytes->size())return 0;
        try{target.bytes->insert(target.bytes->end(),data,data+bytes);}catch(...){return 0;}return bytes;
    }
    // A binary GET of one immutable file (the service's /files route), on the shared connection.
    std::vector<unsigned char> download(const std::string& path,std::size_t size,const std::string& token) {
#ifndef CNA_SERVICE_TLS_TRANSPORT
        (void)path; (void)size; (void)token;
        throw Unavailable(NoServiceTransport);
#else
        if(!curl_)curl_.reset(curl_easy_init());
        if(!curl_)throw Unavailable("Service transport unavailable.");
        CURL* curl=curl_.get();
        std::vector<unsigned char> bytes;bytes.reserve(size);
        DownloadTarget target{&bytes,size};
        const auto url=config_.endpoint+path;
        curl_easy_setopt(curl,CURLOPT_URL,url.c_str());
        curl_easy_setopt(curl,CURLOPT_PROTOCOLS_STR,config_.insecureLoopback?"https,http":"https");
        curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,0L);curl_easy_setopt(curl,CURLOPT_PROXY,"");
        curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L);
        curl_easy_setopt(curl,CURLOPT_SSLVERSION,CURL_SSLVERSION_TLSv1_2);
        if(!config_.caBundle.empty())curl_easy_setopt(curl,CURLOPT_CAINFO,config_.caBundle.c_str());
        curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT_MS,3000L);
        curl_easy_setopt(curl,CURLOPT_TIMEOUT_MS,60000L);
        curl_easy_setopt(curl,CURLOPT_HTTPGET,1L);
        curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,&OnlineBackend::receive);curl_easy_setopt(curl,CURLOPT_WRITEDATA,&target);
        const auto authorization="Authorization: Bearer "+token, game="X-CNA-Game: "+config_.gameId;
        curl_slist* headers=curl_slist_append(nullptr,authorization.c_str());
        std::unique_ptr<curl_slist,decltype(&curl_slist_free_all)> owned(headers,curl_slist_free_all);
        if(!headers||!(headers=curl_slist_append(headers,game.c_str())))throw Unavailable("Service transport unavailable.");
        owned.release();owned.reset(headers);
        curl_easy_setopt(curl,CURLOPT_HTTPHEADER,headers);
        const auto result=curl_easy_perform(curl);
        curl_easy_setopt(curl,CURLOPT_HTTPHEADER,nullptr);
        if(result!=CURLE_OK)throw Unavailable("CNA service connection failed.");
        long status=0;curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);
        if(status==429)throw ServiceError("RATE_LIMITED");
        if(status!=200)throw Unavailable("CNA service file unavailable.");
        return bytes;
#endif
    }
    Json request(const std::string& op,Json args,const std::string& token,const std::vector<std::string>& participants={}) {
        std::lock_guard lock(transportMutex_);
        try {
            negotiateLocked();
            if(op=="auth.refresh"&&!capabilities_.contains("session-refresh"))throw Unavailable("CNA service refresh capability missing.");
            if(op.starts_with("friends.")&&!capabilities_.contains("friend-requests"))throw Unavailable("CNA service friend-request capability missing.");
            if(op.starts_with("messages.")&&!capabilities_.contains("messages"))throw Unavailable("CNA service messages capability missing.");
            if(op=="reviews.submit"&&!capabilities_.contains("player-reviews"))throw Unavailable("CNA service player-review capability missing.");
            if(op=="leaderboards.game.abort"&&!capabilities_.contains("leaderboard-epoch-abort"))throw Unavailable("CNA service leaderboard abort capability missing.");
            if(op.starts_with("leaderboards.game.")&&!capabilities_.contains("local-leaderboard-commit"))throw Unavailable("CNA service local leaderboard commit capability missing.");
            if(op=="leaderboards.list"&&!capabilities_.contains("leaderboard-list"))throw Unavailable("CNA service leaderboard-list capability missing.");
            if(op.starts_with("parties.")&&!capabilities_.contains("parties"))throw ServiceError("NOT_SUPPORTED");
            if(op.starts_with("privacy.")&&!capabilities_.contains("privacy"))throw ServiceError("NOT_SUPPORTED");
            if(op=="invites.joinFriend"&&!capabilities_.contains("join-friend"))throw ServiceError("NOT_SUPPORTED");
            if(op.starts_with("leaderboards.")&&!capabilities_.contains("leaderboard-reads"))throw Unavailable("CNA service leaderboard-read capability missing.");
            if(op=="assets.read"&&!capabilities_.contains("assets"))throw Unavailable("CNA service asset capability missing.");
            if(op=="avatars.catalogPack"&&!capabilities_.contains("avatar-catalog-packs"))throw Unavailable("CNA service catalog pack capability missing.");
            if(op=="presence.set"&&!capabilities_.contains("presence"))throw Unavailable("CNA service presence capability missing.");
            if(op.starts_with("sessions.")&&!capabilities_.contains("session-directory"))throw Unavailable("CNA service directory capability missing.");
            if(op=="sessions.relayTicket"&&!capabilities_.contains("relay-tickets"))throw Unavailable("CNA service relay-ticket capability missing.");
            if(op=="sessions.remove"&&!capabilities_.contains("session-removal"))throw Unavailable("CNA service session-removal capability missing.");
            if((op.starts_with("invites.")||op=="sessions.joinInvited")&&!capabilities_.contains("session-invitations"))throw Unavailable("CNA service invitation capability missing.");
            auto participantArguments=[&] {
                if(participants.empty())return;
                Json values=Json::array();for(const auto& user:participants)values.push_back(tokenFor(user));
                args["participants"]=std::move(values);
            };
            participantArguments();
            auto current=latestToken(token);
            try {return exchange(op,args,current);}
            catch(const ServiceError& error) {
                if(error.code=="NOT_AUTHORIZED"&&!participants.empty()) {
                    renewParticipantsLocked(participants);participantArguments();
                    return exchange(op,args,latestToken(current));
                }
                if(error.code!="UNAUTHENTICATED"||current.empty())throw;
                int slot=-1;unsigned long long generation=0;bool refresh=false;
                {std::lock_guard lock(slotMutex_);for(int i=0;i<4;++i)if(slots_[i].token==current){slot=i;generation=slots_[i].generation;refresh=!slots_[i].refresh.empty();break;}}
                if(slot>=0&&refresh) {
                    try {renewLocked(slot,generation);}
                    catch(const ServiceError& failed){if(failed.code=="UNAUTHENTICATED")invalidateSlot(slot,generation);throw;}
                    if(!participants.empty()){renewParticipantsLocked(participants);participantArguments();}
                    try{return exchange(op,args,latestToken(current));}
                    catch(const ServiceError& failed){if(failed.code=="UNAUTHENTICATED")invalidateSlot(slot,generation);throw;}
                }
                if(slot>=0)invalidateSlot(slot,generation);throw;
            }
        }catch(const Unavailable&){throw;}
         catch(...){throw Unavailable("CNA service response validation failed.");}
    }
    void negotiateLocked() {
            if(!negotiated_) {
                const auto hello=exchange("hello",Json::object(),{});
                if(hello.at("version")!=1||!hello.at("capabilities").is_array()||hello["capabilities"].size()>64)throw Unavailable("CNA service negotiation failed.");
                for(const auto* required:{"identity","authentication","achievements"}) {
                    const auto& caps=hello["capabilities"];
                    if(std::find(caps.begin(),caps.end(),Json(required))==caps.end())throw Unavailable("CNA service capability missing.");
                }
                for(const auto& capability:hello["capabilities"]) {
                    if(!capability.is_string()||capability.get_ref<const std::string&>().size()>64)throw Unavailable("Invalid service capability.");
                    capabilities_.insert(capability.get<std::string>());
                }
                negotiated_=true;
                eventsCapable_=capabilities_.contains("events");
            }
    }
    void renewParticipantsLocked(const std::vector<std::string>& users) {
        if(!capabilities_.contains("heartbeat"))throw Unavailable("CNA service participant validation capability missing.");
        for(const auto& user:users) {
            const auto credential=tokenFor(user);
            try{(void)exchange("auth.ping",Json::object(),credential);}
            catch(const ServiceError& error) {
                if(error.code!="UNAUTHENTICATED")throw;
                int slot=-1;unsigned long long generation=0;
                {std::lock_guard lock(slotMutex_);for(int i=0;i<4;++i)if(slots_[i].token==credential&&slots_[i].identity.userId==user){slot=i;generation=slots_[i].generation;break;}}
                if(slot<0)throw;
                try{renewLocked(slot,generation);}
                catch(const ServiceError& failed){if(failed.code=="UNAUTHENTICATED")invalidateSlot(slot,generation);throw;}
            }
        }
    }
    Slot decodeCredentials(const Json& result) {
        Slot state;state.identity=identity(result.at("identity"));applyPrivileges(state.identity,result);state.token=CnaService::stringField(result,"token",64);
        if(state.token.size()!=64||state.token.find_first_not_of("0123456789abcdef")!=std::string::npos)throw Unavailable("Invalid access credential.");
        const auto localNow=unixTime();long long serverNow=localNow;
        if(result.contains("serverTime")) {
            if(!result["serverTime"].is_number_integer()||result["serverTime"]<0||result["serverTime"]>253402300799LL)throw Unavailable("Invalid service time.");
            serverNow=result["serverTime"].get<long long>();
        }
        if(!result.contains("expires")||!result["expires"].is_number_integer()||result["expires"]<=serverNow||result["expires"]>serverNow+3600)throw Unavailable("Invalid credential expiry.");
        state.expires=localNow+(result["expires"].get<long long>()-serverNow);state.heartbeatAt=localNow+heartbeatSeconds;
        if(capabilities_.contains("session-refresh")) {
            state.refresh=CnaService::stringField(result,"refreshToken",64);
            if(state.refresh.size()!=64||state.refresh.find_first_not_of("0123456789abcdef")!=std::string::npos||!result.contains("refreshExpires")||!result["refreshExpires"].is_number_integer()||result["refreshExpires"]<result["expires"]||result["refreshExpires"]>serverNow+30LL*86400)throw Unavailable("Invalid refresh credential.");
            state.refreshExpires=localNow+(result["refreshExpires"].get<long long>()-serverNow);
        }
        return state;
    }
    void persist(int slot) {
        StoredCredential stored;{std::lock_guard lock(slotMutex_);stored={slots_[slot].refresh,slots_[slot].refreshExpires};}
        if(!stored.refreshToken.empty())(void)credentials_.save(slot,stored);else credentials_.remove(slot);
    }
    std::string latestToken(const std::string& token) {
        if(token.empty())return token;
        std::lock_guard lock(slotMutex_);for(const auto& state:slots_)if(state.previousToken==token)return state.token;return token;
    }
    void invalidateSlot(int slot,unsigned long long generation) {
        BackendEvent event;event.type=BackendEvent::Type::SignedOut;event.slot=slot;
        {std::lock_guard lock(slotMutex_);if(slots_[slot].generation!=generation)return;
         event.identity=slots_[slot].identity;slots_[slot]={};slots_[slot].generation=generation+1;}
        credentials_.remove(slot);if(!event.identity.userId.empty())ready(std::move(event));
    }
    void publishPolicyLocked(int slot,unsigned long long generation,const Json& result) {
        if(!capabilities_.contains("policy-refresh"))return;
        Slot previous;{std::lock_guard lock(slotMutex_);previous=slots_[slot];}
        if(previous.generation!=generation||previous.token.empty())return;
        auto person=previous.identity;
        if(!result.contains("privileges"))throw Unavailable("Missing current policy.");
        applyPrivileges(person,result);person.blocked=parseBlocked(result);
        {std::lock_guard lock(slotMutex_);if(slots_[slot].generation!=generation)return;slots_[slot].identity=person;}
        BackendEvent event;event.type=BackendEvent::Type::PolicyChanged;event.slot=slot;event.identity=std::move(person);ready(std::move(event));
    }
    void renewLocked(int slot,unsigned long long generation) {
        Slot previous;{std::lock_guard lock(slotMutex_);previous=slots_[slot];}
        if(previous.generation!=generation)return;
        if(!capabilities_.contains("session-refresh")||previous.refresh.empty())throw ServiceError("UNAUTHENTICATED");
        const auto result=exchange("auth.refresh",{{"refreshToken",previous.refresh}},{});
        auto renewed=decodeCredentials(result);
        if(!previous.identity.userId.empty()&&renewed.identity.userId!=previous.identity.userId)throw Unavailable("Refresh identity mismatch.");
        renewed.identity.blocked=previous.identity.blocked;
        readBlocked(renewed.identity,renewed.token);
        renewed.previousToken=previous.token;renewed.generation=generation;
        bool stale=false,duplicate=false;
        {std::lock_guard lock(slotMutex_);
         stale=slots_[slot].generation!=generation;
         for(int i=0;i<4;++i)if(i!=slot&&slots_[i].identity.userId==renewed.identity.userId)duplicate=true;
         if(!stale&&!duplicate)slots_[slot]=renewed;}
        if(stale||duplicate) {
            try{(void)exchange("auth.logout",Json::object(),renewed.token);}catch(...){}
            if(duplicate){invalidateSlot(slot,generation);throw ServiceError("UNAUTHENTICATED");}return;
        }
        persist(slot);
        if(previous.identity.userId.empty()) {
            readGameDefaults(renewed.identity,renewed.token);
            BackendEvent event;event.type=BackendEvent::Type::SignedIn;event.slot=slot;event.identity=std::move(renewed.identity);ready(std::move(event));
        } else {
            BackendEvent event;event.type=BackendEvent::Type::PolicyChanged;event.slot=slot;event.identity=std::move(renewed.identity);ready(std::move(event));
        }
    }
    // The signed-in account's own privileges (XNA GamerPrivileges); a service older than them
    // sends none, and every privilege stays allowed.
    static void applyPrivileges(ServiceIdentity& person,const Json& result) {
        if(!result.contains("privileges"))return;
        const auto& p=result["privileges"];
        auto setting=[&](const char* key,std::string& out) {
            const auto value=CnaService::stringField(p,key,16);
            if(value!="everyone"&&value!="friends"&&value!="blocked")throw CnaService::Error("INVALID_RESPONSE");
            out=value;
        };
        auto allowed=[&](const char* key,bool& out) {
            if(!p.contains(key)||!p[key].is_boolean())throw CnaService::Error("INVALID_RESPONSE");
            out=p[key].get<bool>();
        };
        setting("communication",person.communication);setting("profileViewing",person.profileViewing);setting("userContent",person.userContent);
        allowed("trade",person.tradeContent);allowed("purchase",person.purchaseContent);allowed("premium",person.premiumContent);
    }
    // The signed-in account's block list; a service without one, or a failed read, leaves none.
    // Callers hold transportMutex_, hence exchange rather than request.
    void readBlocked(ServiceIdentity& person,const std::string& token) {
        if(!capabilities_.contains("privacy"))return;
        try{person.blocked=parseBlocked(exchange("privacy.list",Json::object(),token));}catch(...){}
    }
    static std::vector<std::string> parseBlocked(const Json& result) {
        const auto& rows=result.at("blocked");
        if(!rows.is_array()||rows.size()>1024)throw Unavailable("Invalid block list.");
        std::vector<std::string> tags;for(const auto& row:rows)tags.push_back(row.get<std::string>());
        return tags;
    }
    // The signed-in account's game defaults; a service without them, or a failed read, leaves none.
    // Callers hold transportMutex_ (sign-in and renewal), hence exchange rather than request.
    void readGameDefaults(ServiceIdentity& person,const std::string& token) {
        if(!capabilities_.contains("game-defaults"))return;
        try {
            const auto result=exchange("profile.gameDefaults",Json::object(),token);
            if(result.contains("gameDefaults")&&result["gameDefaults"].is_object()){auto text=result["gameDefaults"].dump();if(text.size()<=4096)person.gameDefaults=std::move(text);}
        }catch(...){}
    }
#ifdef CNA_SERVICE_TLS_TRANSPORT
    // Push hints (capability "events"): one WebSocket per signed-in account, served by a thread of its
    // own. A hint only moves the next read of the invitation, party or social watcher forward, so a
    // channel that is down costs nothing but that read's interval. CNA_GAMER_SERVICES_EVENTS=0 turns
    // it off.
    struct EventLink {
        std::unique_ptr<CURL,decltype(&curl_easy_cleanup)> handle{nullptr,curl_easy_cleanup};
        std::string token,partial;
        long long retryAt=0;
        int failures=0;
    };
    void startEvents() {
        static const bool disabled=[]{const char* value=std::getenv("CNA_GAMER_SERVICES_EVENTS");return value&&std::string(value)=="0";}();
        if(disabled||!eventsCapable_||events_.joinable())return;
        events_=std::jthread([this](std::stop_token stop){runEvents(stop);});
    }
    std::string eventsUrl() const {
        auto url=config_.endpoint;
        if(url.starts_with("https://"))url="wss://"+url.substr(8);
        else if(url.starts_with("http://"))url="ws://"+url.substr(7);
        while(url.ends_with("/"))url.pop_back();
        return url+"/events";
    }
    static bool sendText(CURL* curl,const std::string& text,std::stop_token stop) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        std::size_t offset=0;
        while(offset<text.size()) {
            if(stop.stop_requested()||std::chrono::steady_clock::now()>=deadline)return false;
            std::size_t sent=0;
            const auto code=curl_ws_send(curl,text.data()+offset,text.size()-offset,&sent,0,CURLWS_TEXT);
            if(code!=CURLE_OK&&code!=CURLE_AGAIN)return false;
            offset+=std::min(sent,text.size()-offset);
            if(code==CURLE_AGAIN)std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return true;
    }
    // One complete text message, or nothing yet; false when the channel is gone.
    static bool receiveText(EventLink& link,std::optional<std::string>& message) {
        for(int frames=0;frames<64;++frames) {
            std::array<char,1024> scratch{};std::size_t count=0;const curl_ws_frame* metadata=nullptr;
            const auto code=curl_ws_recv(link.handle.get(),scratch.data(),scratch.size(),&count,&metadata);
            if(code==CURLE_AGAIN)return true;
            if(code!=CURLE_OK||!metadata||(metadata->flags&CURLWS_CLOSE))return false;
            if(!(metadata->flags&(CURLWS_TEXT|CURLWS_CONT)))continue;
            if(link.partial.size()+count>4096)return false;
            link.partial.append(scratch.data(),count);
            if(metadata->bytesleft==0&&!(metadata->flags&CURLWS_CONT)){message=std::move(link.partial);link.partial.clear();return true;}
        }
        return true;
    }
    bool connectEvents(EventLink& link,const std::string& token,std::stop_token stop) {
        link.handle.reset(curl_easy_init());link.partial.clear();
        CURL* curl=link.handle.get();
        if(!curl)return false;
        const auto url=eventsUrl();
        curl_easy_setopt(curl,CURLOPT_URL,url.c_str());
        curl_easy_setopt(curl,CURLOPT_PROTOCOLS_STR,config_.insecureLoopback?"wss,ws":"wss");
        curl_easy_setopt(curl,CURLOPT_CONNECT_ONLY,2L);curl_easy_setopt(curl,CURLOPT_HTTP_VERSION,CURL_HTTP_VERSION_1_1);
        curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,0L);curl_easy_setopt(curl,CURLOPT_PROXY,"");
        curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L);
        curl_easy_setopt(curl,CURLOPT_SSLVERSION,CURL_SSLVERSION_TLSv1_2);
        if(!config_.caBundle.empty())curl_easy_setopt(curl,CURLOPT_CAINFO,config_.caBundle.c_str());
        curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT_MS,3000L);curl_easy_setopt(curl,CURLOPT_TIMEOUT_MS,5000L);
        long status=0;
        if(curl_easy_perform(curl)!=CURLE_OK||curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status)!=CURLE_OK||status!=101){link.handle.reset();return false;}
        Json hello{{"v",1},{"id","events"},{"game",config_.gameId},{"token",token}};
        auto text=hello.dump();
        const bool sent=sendText(curl,text,stop);
        std::fill(text.begin(),text.end(),'\0');
        if(!sent){link.handle.reset();return false;}
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!stop.stop_requested()&&std::chrono::steady_clock::now()<deadline) {
            std::optional<std::string> message;
            if(!receiveText(link,message)){link.handle.reset();return false;}
            if(message) {
                try {
                    const auto welcome=CnaService::parse(*message);
                    if(welcome.value("error","")=="OK"){link.token=token;return true;}
                }catch(...){}
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        link.handle.reset();return false;
    }
    void runEvents(std::stop_token stop) {
        std::map<std::string,EventLink> links;
        while(!stop.stop_requested()) {
            std::map<std::string,std::string> tokens;
            {std::lock_guard lock(slotMutex_);
             for(const auto& slot:slots_)if(!slot.token.empty()&&!slot.identity.userId.empty())tokens.emplace(slot.identity.userId,slot.token);}
            std::erase_if(links,[&](const auto& entry){return !tokens.contains(entry.first);});
            const auto now=unixTime();
            for(const auto& [user,token]:tokens) {
                auto& link=links[user];
                // A renewed token: the channel reconnects with it (the server closes one that expired).
                if(link.handle&&link.token!=token)link.handle.reset();
                if(!link.handle&&now>=link.retryAt) {
                    if(connectEvents(link,token,stop))link.failures=0;
                    else {link.failures=std::min(link.failures+1,5);link.retryAt=now+std::min(60,2<<link.failures);}
                }
                if(!link.handle)continue;
                std::optional<std::string> message;
                if(!receiveText(link,message)){link.handle.reset();link.retryAt=now+2;continue;}
                if(!message)continue;
                try {
                    const auto hint=CnaService::parse(*message);
                    if(hint.value("v",0)==1&&hint.contains("topics")&&hint["topics"].is_array())
                        for(const auto& topic:hint["topics"])if(topic.is_string())serviceHint(topic.get<std::string>());
                }catch(...){}
            }
            for(int wait=0;wait<10&&!stop.stop_requested();++wait)std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
#else
    void startEvents() {}
#endif
    static std::string prefix() {
        std::random_device source;std::string value;constexpr char hex[]="0123456789abcdef";
        for(int i=0;i<16;++i){auto byte=source();value+=hex[(byte>>4)&15];value+=hex[byte&15];}return value;
    }
    CNA::GamerServices::Configuration config_;
    CredentialStore credentials_;
    std::unique_ptr<IServiceSessionDirectory> directory_;
    std::mutex slotMutex_,transportMutex_,cacheMutex_;
#ifdef CNA_SERVICE_TLS_TRANSPORT
    std::unique_ptr<CURL,decltype(&curl_easy_cleanup)> curl_{nullptr,curl_easy_cleanup};
#endif
    std::array<Slot,4> slots_{};
    std::string prefix_=prefix();
    unsigned long long sequence_=0;
    bool negotiated_=false;
    std::set<std::string> capabilities_;
    std::atomic<bool> eventsCapable_{false};
#ifdef CNA_SERVICE_TLS_TRANSPORT
    // Last: stopped (in the destructor) before anything it reads.
    std::jthread events_;
#endif
};
thread_local int serviceRestrictionDepth=0;
std::mutex registryMutex;
std::shared_ptr<IGamerServicesBackend> current;
}
bool serviceCallsRestricted(){return serviceRestrictionDepth>0;}
void withRestrictedServiceCalls(const std::function<void()>& callback) {
    ++serviceRestrictionDepth;try{callback();}catch(...){--serviceRestrictionDepth;throw;}--serviceRestrictionDepth;
}
std::shared_ptr<IGamerServicesBackend> backend() {
    if(serviceCallsRestricted())throw System::InvalidOperationException("Other GamerServices calls are forbidden inside a final leaderboard write handler.");
    std::lock_guard lock(registryMutex);if(!current)current=std::make_shared<OnlineBackend>(CNA::GamerServices::resolveConfiguration());return current;
}
CNA::GamerServices::Configuration configurationForBackend(const IGamerServicesBackend& origin) {
    const auto* online=dynamic_cast<const OnlineBackend*>(&origin);
    if(!online||!online->serviceEnabled())throw Unavailable("This Gamer Services backend has no configured relay authority.");
    return online->configuration();
}
void setBackendForTesting(std::shared_ptr<IGamerServicesBackend> value) {std::lock_guard lock(registryMutex);current=std::move(value);}
void setHeartbeatIntervalForTesting(int seconds){heartbeatSeconds=seconds;}
}

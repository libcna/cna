// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/Internal/GamerServices/AssetDiskCache.hpp"
#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include "CNA/Internal/GamerServices/BackendConfiguration.hpp"
#include "CNA/GamerServices/Configuration.hpp"
#include "CnaService/Protocol.hpp"
#include "CredentialStore.hpp"
#include "ServiceSessionDirectoryClient.hpp"
#include "ServiceSessionDirectoryFake.hpp"
#include <chrono>
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#ifndef __EMSCRIPTEN__
#include <curl/curl.h>
#endif
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
long long unixTime(){return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
void slotGuard(int slot) {if(slot<0||slot>3)throw System::ArgumentOutOfRangeException("slot");}
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
class QueuedBackend : public IGamerServicesBackend {
public:
    explicit QueuedBackend(bool background):background_(background) {
        if(background_)worker_=std::jthread([this](std::stop_token stop){
            while(!stop.stop_requested()) {
                std::function<BackendEvent()> job;
                {std::unique_lock lock(queueMutex_);condition_.wait(lock,[&]{return stop.stop_requested()||!jobs_.empty();});
                 if(stop.stop_requested())return;
                 job=std::move(jobs_.front());jobs_.pop_front();++active_;}
                auto event=execute(job);
                {std::lock_guard lock(queueMutex_);--active_;ready_.push_back(std::move(event));}
            }
        });
    }
    ~QueuedBackend() override {stop();}
    void stop() {if(worker_.joinable()){worker_.request_stop();condition_.notify_all();worker_.join();}}
    void submit(std::function<void()> work,std::function<void()> completion) override {
        queue([work=std::move(work),completion=std::move(completion)]() mutable {
            work();BackendEvent event;event.type=BackendEvent::Type::Completion;event.completion=std::move(completion);return event;
        });
    }
    void signInLocal(int slot,const std::string& gamertag) override {
        slotGuard(slot);
        BackendEvent event;event.type=BackendEvent::Type::SignedIn;event.slot=slot;event.signedInToLive=false;
        event.identity.gamertag=gamertag;event.identity.displayName=gamertag;
        ready(std::move(event));
    }
    void signInGuest(int slot,const std::string& gamertag,int host) override {
        slotGuard(slot);slotGuard(host);
        if(host==slot||gamertag.empty()||gamertag.size()>36)throw Unavailable("Invalid guest.");
        BackendEvent event;event.type=BackendEvent::Type::SignedIn;event.slot=slot;event.signedInToLive=true;event.guestOf=host;
        event.identity.gamertag=gamertag;event.identity.displayName=gamertag;
        ready(std::move(event));
    }
    void signOutGuest(int slot) override {
        slotGuard(slot);BackendEvent event;event.type=BackendEvent::Type::SignedOut;event.slot=slot;ready(std::move(event));
    }
    std::vector<BackendEvent> pump() override {
        if(!background_) {
            for(int i=0;i<32;++i) {
                std::function<BackendEvent()> job;
                {std::lock_guard lock(queueMutex_);if(jobs_.empty())break;job=std::move(jobs_.front());jobs_.pop_front();}
                auto event=execute(job);{std::lock_guard lock(queueMutex_);ready_.push_back(std::move(event));}
            }
        }
        std::lock_guard lock(queueMutex_);std::vector<BackendEvent> result;
        result.reserve(ready_.size());while(!ready_.empty()){result.push_back(std::move(ready_.front()));ready_.pop_front();}return result;
    }
protected:
    static BackendEvent execute(const std::function<BackendEvent()>& job) {
        try {return job();} catch(...) {
            const auto error=std::current_exception();BackendEvent event;event.type=BackendEvent::Type::Completion;
            event.completion=[error]{std::rethrow_exception(error);};return event;
        }
    }
    void queue(std::function<BackendEvent()> job) {
        std::lock_guard lock(queueMutex_);
        if(jobs_.size()+ready_.size()+active_>=128)throw Unavailable("CNA service queue limit exceeded.");
        jobs_.push_back(std::move(job));condition_.notify_one();
    }
    void ready(BackendEvent event) {std::lock_guard lock(queueMutex_);if(ready_.size()<128)ready_.push_back(std::move(event));}
private:
    bool background_;
    std::jthread worker_;
    std::mutex queueMutex_;
    std::condition_variable condition_;
    std::deque<std::function<BackendEvent()>> jobs_;
    std::deque<BackendEvent> ready_;
    std::size_t active_=0;
};
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
    ~OnlineBackend() override {stop();}
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
                event.type=BackendEvent::Type::SignedIn;event.identity=std::move(person);
            }catch(const std::exception&){
                if(issuedToken.size()==64){try{(void)request("auth.logout",Json::object(),issuedToken);}catch(...){}}
                {std::lock_guard lock(slotMutex_);if(slots_[slot].generation==generation)slots_[slot].busy=false;}
                event.type=BackendEvent::Type::Failed;event.error="Sign-in failed.";
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
                                try{(void)exchange("auth.ping",Json::object(),state.token);}
                                catch(const ServiceError& error){if(error.code!="UNAUTHENTICATED")throw;renewLocked(slot,generation);}
                            }
                            succeeded=true;
                        }
                    }catch(const ServiceError& error) {if(error.code=="UNAUTHENTICATED")invalidateSlot(slot,generation);}
                     catch(...) {}
                    {std::lock_guard lock(slotMutex_);auto& state=slots_[slot];if(state.generation==generation){state.busy=false;
                        if(succeeded){state.failures=0;state.retryAt=0;state.heartbeatAt=unixTime()+30;}
                        else {state.failures=std::min(state.failures+1,6);state.retryAt=unixTime()+std::min(300,10*(1<<state.failures));}}}
                    BackendEvent event;event.type=BackendEvent::Type::Completion;return event;
                });
            }catch(...) {std::lock_guard lock(slotMutex_);slots_[slot].busy=false;slots_[slot].retryAt=timestamp+5;}
        }
        return QueuedBackend::pump();
    }
    ServiceIdentity profile(const std::string& tag) override {return identity(request("profile.get",{{"gamertag",tag}},tokenFor({})));}
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
            for(const auto* key:{"accepted","requestSent","requestReceived"})if(!e.at(key).is_boolean())throw Unavailable("Invalid friend response.");
            friendState.accepted=e["accepted"].get<bool>();friendState.requestSent=e["requestSent"].get<bool>();friendState.requestReceived=e["requestReceived"].get<bool>();
            // A server older than these flags leaves them out; one that sends them sends booleans.
            for(auto [key,target]:{std::pair{"joinable",&friendState.joinable},std::pair{"inviteReceivedFrom",&friendState.inviteReceivedFrom},
                std::pair{"inviteSentTo",&friendState.inviteSentTo},std::pair{"inviteAccepted",&friendState.inviteAccepted},
                std::pair{"inviteRejected",&friendState.inviteRejected},std::pair{"away",&friendState.away},std::pair{"busy",&friendState.busy}}) {
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
    std::vector<std::vector<unsigned char>> avatars(const std::vector<std::string>& ids) override {
        if(!capabilities_.contains("avatars"))throw Unavailable("CNA service avatars capability missing.");
        if(ids.empty()||ids.size()>16)throw Unavailable("Invalid avatar request.");
        // This build reads description formats 1 and 2 (a service gives older readers a format 1 copy).
        const auto result=request("avatars.get",{{"userIds",ids},{"formats",Json::array({1,2})}},tokenFor({}));
        const auto& entries=result.at("avatars");
        if(!entries.is_array()||entries.size()!=ids.size())throw Unavailable("Invalid avatar response.");
        std::vector<std::vector<unsigned char>> out;
        for(std::size_t index=0;index<ids.size();++index) {
            const auto& entry=entries[index];
            if(!entry.is_object()||!entry.contains("userId")||entry["userId"]!=ids[index]||!entry.contains("description"))
                throw Unavailable("Invalid avatar response.");
            const auto& text=entry["description"];
            if(text.is_null()){out.emplace_back();continue;}
            if(!text.is_string()||text.get_ref<const std::string&>().size()!=2042)throw Unavailable("Invalid avatar response.");
            std::vector<unsigned char> bytes;bytes.reserve(1021);
            const auto& hex=text.get_ref<const std::string&>();
            auto nibble=[](char c)->int{return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
            for(std::size_t i=0;i<hex.size();i+=2) {
                const int high=nibble(hex[i]),low=nibble(hex[i+1]);
                if(high<0||low<0)throw Unavailable("Invalid avatar response.");
                bytes.push_back(static_cast<unsigned char>(high<<4|low));
            }
            out.push_back(std::move(bytes));
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
        if(version<0||version>65535)throw Unavailable("Invalid avatar catalog version.");
        Json args=Json::object();if(version)args["version"]=version;
        const auto result=request("avatars.catalog",args,tokenFor({}));
        if(!result.contains("version")||!result["version"].is_number_integer()||(version&&result["version"]!=version)||
           !result.contains("manifest")||!result["manifest"].is_object())throw Unavailable("Invalid avatar catalog response.");
        return result["manifest"].dump();
    }
    std::vector<unsigned char> asset(const std::string& hash) override {
        if(hash.size()!=64||hash.find_first_not_of("0123456789abcdef")!=std::string::npos)throw Unavailable("Invalid service asset identifier.");
        const auto token=tokenFor({});
        std::lock_guard cacheLock(cacheMutex_);
        const AssetDiskCache cache(AssetDiskCache::defaultRoot(),prefix_);
        if(auto cached=cache.read(hash))return std::move(*cached);
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
        if(Avatars::sha256Hex(std::span<const std::uint8_t>(bytes.data(),bytes.size()))!=hash)throw Unavailable("Corrupt service asset.");
        // A full or unwritable cache still returns the asset.
        cache.write(hash,bytes);
        return bytes;
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
        throw Unavailable("No authenticated CNA gamer for this operation.");
    }
    static std::size_t write(char* data,std::size_t size,std::size_t count,void* context) noexcept {
        auto& text=*static_cast<std::string*>(context);
        if(size && count>CnaService::MaxMessageBytes/size)return 0;
        const auto bytes=size*count;if(bytes>CnaService::MaxMessageBytes-text.size())return 0;
        try{text.append(data,bytes);}catch(...){return 0;}return bytes;
    }
    Json exchange(const std::string& op,Json args,const std::string& token) {
#ifdef __EMSCRIPTEN__
        (void)op; (void)args; (void)token;
        throw Unavailable("CNA browser account transport is not implemented.");
#else
        static std::once_flag initialized;
        std::call_once(initialized,[]{if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK)throw Unavailable("Secure transport initialization failed.");});
        if(!(curl_version_info(CURLVERSION_NOW)->features&CURL_VERSION_SSL))throw Unavailable("libcurl has no TLS support.");
        if(config_.endpoint.empty())throw Unavailable("No CNA service endpoint configured.");
        const auto id=prefix_+"-"+std::to_string(++sequence_);
        Json request{{"v",1},{"id",id},{"game",config_.gameId},{"op",op},{"args",std::move(args)}};
        if(!token.empty())request["token"]=token;
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
        if(curl_easy_perform(curl)!=CURLE_OK)throw Unavailable("CNA service connection failed.");
        long status=0;curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);if(status!=200)throw Unavailable("CNA service HTTP failure.");
        const auto response=CnaService::parse(output);
        if(!response.is_object()||response.size()!=4||response.at("v")!=1||CnaService::stringField(response,"id",64)!=id||!response.at("result").is_object())
            throw Unavailable("CNA service protocol mismatch.");
        const auto error=CnaService::stringField(response,"error",64);
        if(error!="OK")throw ServiceError(error);
        return response["result"];
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
            if(op.starts_with("leaderboards.")&&!capabilities_.contains("leaderboard-reads"))throw Unavailable("CNA service leaderboard-read capability missing.");
            if(op=="assets.read"&&!capabilities_.contains("assets"))throw Unavailable("CNA service asset capability missing.");
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
        Slot state;state.identity=identity(result.at("identity"));state.token=CnaService::stringField(result,"token",64);
        if(state.token.size()!=64||state.token.find_first_not_of("0123456789abcdef")!=std::string::npos)throw Unavailable("Invalid access credential.");
        const auto localNow=unixTime();long long serverNow=localNow;
        if(result.contains("serverTime")) {
            if(!result["serverTime"].is_number_integer()||result["serverTime"]<0||result["serverTime"]>253402300799LL)throw Unavailable("Invalid service time.");
            serverNow=result["serverTime"].get<long long>();
        }
        if(!result.contains("expires")||!result["expires"].is_number_integer()||result["expires"]<=serverNow||result["expires"]>serverNow+3600)throw Unavailable("Invalid credential expiry.");
        state.expires=localNow+(result["expires"].get<long long>()-serverNow);state.heartbeatAt=localNow+30;
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
    void renewLocked(int slot,unsigned long long generation) {
        Slot previous;{std::lock_guard lock(slotMutex_);previous=slots_[slot];}
        if(previous.generation!=generation)return;
        if(!capabilities_.contains("session-refresh")||previous.refresh.empty())throw ServiceError("UNAUTHENTICATED");
        const auto result=exchange("auth.refresh",{{"refreshToken",previous.refresh}},{});
        auto renewed=decodeCredentials(result);
        if(!previous.identity.userId.empty()&&renewed.identity.userId!=previous.identity.userId)throw Unavailable("Refresh identity mismatch.");
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
        }
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
    static std::string prefix() {
        std::random_device source;std::string value;constexpr char hex[]="0123456789abcdef";
        for(int i=0;i<16;++i){auto byte=source();value+=hex[(byte>>4)&15];value+=hex[byte&15];}return value;
    }
    CNA::GamerServices::Configuration config_;
    CredentialStore credentials_;
    std::unique_ptr<IServiceSessionDirectory> directory_;
    std::mutex slotMutex_,transportMutex_,cacheMutex_;
#ifndef __EMSCRIPTEN__
    std::unique_ptr<CURL,decltype(&curl_easy_cleanup)> curl_{nullptr,curl_easy_cleanup};
#endif
    std::array<Slot,4> slots_{};
    std::string prefix_=prefix();
    unsigned long long sequence_=0;
    bool negotiated_=false;
    std::set<std::string> capabilities_;
};
class FakeBackend final : public QueuedBackend {
public:
    FakeBackend(std::vector<ServiceIdentity> identities,std::vector<ServiceAchievement> catalog,std::vector<ServiceLeaderboardFixture> boards):QueuedBackend(false),identities_(std::move(identities)),catalog_(std::move(catalog)),boards_(std::move(boards)) {
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
                slots_[slot]=person.userId;event.type=BackendEvent::Type::SignedIn;event.identity=person;return event;
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
        require(user);std::vector<ServiceFriend> result;
        for(const auto& target:identities_)if(target.userId!=user) {
            const bool sent=edges_.contains({user,target.userId}),received=edges_.contains({target.userId,user});
            if(!sent&&!received)continue;
            ServiceFriend value;value.gamertag=target.gamertag;value.accepted=sent&&received;
            value.requestSent=sent&&!received;value.requestReceived=received&&!sent;
            value.online=value.accepted&&std::find(slots_.begin(),slots_.end(),target.userId)!=slots_.end();
            value.presence=value.online?presence_[target.userId]:"";
            value.away=value.online&&status_[target.userId]=="away";value.busy=value.online&&status_[target.userId]=="busy";
            result.push_back(std::move(value));
        }return result;
    }
    void changeFriend(const std::string& user,const std::string& tag,const std::string& action) override {
        require(user);auto target=profile(tag).userId;if(target==user)throw Unavailable("Self friendship.");
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
    void sendMessage(const std::string& user,const std::vector<std::string>& tags,const std::string& text) override {
        require(user);if(tags.empty()||tags.size()>100||text.size()>256)throw ServiceOperationError("INVALID_ARGUMENT");
        for(const auto& tag:tags){const auto target=profile(tag).userId;if(target==user)throw ServiceOperationError("INVALID_ARGUMENT");
            ServiceMessage message;message.id=std::string(28,'0')+std::to_string(1000+(++messageSequence_%9000));message.sender=profileById(user).gamertag;
            message.text=text;message.created=messageSequence_;inbox_[target].insert(inbox_[target].begin(),std::move(message));}
    }
    ServiceMessagePage messages(const std::string& user,int start,int limit) override {
        require(user);ServiceMessagePage page;page.start=start;const auto& box=inbox_[user];page.total=static_cast<int>(box.size());
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
    std::vector<std::vector<unsigned char>> avatars(const std::vector<std::string>& ids) override {
        std::lock_guard guard(avatarLock_);
        if(avatarsUnreachable_)throw Unavailable("Fake fixture is unreachable.");
        std::vector<std::vector<unsigned char>> out;
        for(const auto& id:ids) {
            auto person=std::find_if(identities_.begin(),identities_.end(),[&](const auto& value){return value.userId==id;});
            out.push_back(person==identities_.end()?std::vector<unsigned char>{}:person->avatar);
        }
        return out;
    }
    long long setAvatar(const std::string& userId,const std::vector<unsigned char>& description) override {
        std::lock_guard guard(avatarLock_);
        if(avatarsUnreachable_)throw Unavailable("Fake fixture is unreachable.");
        for(auto& person:identities_)if(person.userId==userId){person.avatar=description;return ++avatarRevision_;}
        throw ServiceError("NOT_FOUND");
    }
    void replaceAvatar(const std::string& userId,std::vector<unsigned char> description) {
        std::lock_guard guard(avatarLock_);
        for(auto& person:identities_)if(person.userId==userId)person.avatar=std::move(description);
    }
    void setAvatarsUnreachable(bool failing){std::lock_guard guard(avatarLock_);avatarsUnreachable_=failing;}
    std::string avatarCatalog(int) override {
        if(avatarCatalog_.empty())throw Unavailable("Fake fixture has no avatar catalog.");
        return avatarCatalog_;
    }
    std::vector<unsigned char> asset(const std::string& hash) override {
        auto found=avatarAssets_.find(hash);
        if(found==avatarAssets_.end())throw Unavailable("Fake fixture has no such asset.");
        return found->second;
    }
    void setAvatarCatalog(std::string manifest,std::map<std::string,std::vector<unsigned char>> assets) {
        avatarCatalog_=std::move(manifest);avatarAssets_=std::move(assets);
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
    std::mutex avatarLock_;
    bool avatarsUnreachable_=false;
    long long avatarRevision_=0;
    std::map<std::string,std::vector<unsigned char>> avatarAssets_;
    std::map<std::string,std::vector<ServiceMessage>> inbox_;
    std::map<std::pair<std::string,std::string>,std::string> reviews_;
    int messageSequence_=0;
    std::map<std::string,std::map<std::string,std::vector<ServiceLeaderboardWrite>>> arbitration_;
    std::map<std::string,ServiceSessionSnapshot> rounds_;
    std::map<std::string,std::vector<std::string>> games_;
    int gameSequence_=0;
    std::unique_ptr<IServiceSessionDirectory> directory_;
    void require(const std::string& user) {if(user.empty()||std::find(slots_.begin(),slots_.end(),user)==slots_.end())throw Unavailable("Gamer signed out.");}
    std::vector<ServiceIdentity> identities_;
    std::vector<ServiceAchievement> catalog_;
    std::vector<ServiceLeaderboardFixture> boards_;
    std::array<std::string,4> slots_{};
    std::map<std::string,std::map<std::string,long long>> earned_;
    std::set<std::pair<std::string,std::string>> edges_;
    std::map<std::string,std::string> presence_,status_;
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
void setFakeAvatar(IGamerServicesBackend& fake,const std::string& userId,std::vector<unsigned char> description) {
    auto* backend=dynamic_cast<FakeBackend*>(&fake);
    if(!backend)throw System::InvalidOperationException("Not a fake Gamer Services backend.");
    backend->replaceAvatar(userId,std::move(description));
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
std::shared_ptr<IGamerServicesBackend> makeFakeBackend(std::vector<ServiceIdentity> people,std::vector<ServiceAchievement> catalog,std::vector<ServiceLeaderboardFixture> boards) {
    return std::make_shared<FakeBackend>(std::move(people),std::move(catalog),std::move(boards));
}
}

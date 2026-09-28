// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/GamerServices/Configuration.hpp"
#include "CnaService/Protocol.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include <curl/curl.h>
#include <condition_variable>
#include <deque>
#include <map>
#include <set>
#include <mutex>
#include <random>
#include <thread>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <cmath>
#include <limits>
#include "System/Security/Cryptography/SHA256.hpp"

namespace CNA::Internal::GamerServices {
using CnaService::Json;
namespace {
using Unavailable=Microsoft::Xna::Framework::GamerServices::GamerServicesNotAvailableException;
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
    value.allowOnlineSessions=j["allowOnlineSessions"].get<bool>();return value;
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
    explicit OnlineBackend(CNA::GamerServices::Configuration config):QueuedBackend(true),config_(std::move(config)) {}
    ~OnlineBackend() override {stop();}
    bool serviceEnabled() const override {return !config_.endpoint.empty();}
    void signIn(int slot,std::string username,std::string password) override {
        slotGuard(slot);
        queue([this,slot,username=std::move(username),password=std::move(password)]() mutable {
            BackendEvent event;event.slot=slot;std::string issuedToken;
            try {
                const auto result=request("auth.login",{{"username",username},{"password",password}},{});
                auto person=identity(result.at("identity"));issuedToken=CnaService::stringField(result,"token",128);
                if(issuedToken.size()!=64)throw CnaService::Error("INVALID_RESPONSE");
                std::string previous;
                {std::lock_guard lock(slotMutex_);
                 for(int i=0;i<4;++i)if(i!=slot&&slots_[i].identity.userId==person.userId)throw CnaService::Error("ALREADY_SIGNED_IN");
                 previous=slots_[slot].token;slots_[slot]={person,issuedToken};}
                issuedToken.clear();
                if(!previous.empty()){try{(void)request("auth.logout",Json::object(),previous);}catch(...){}}
                event.type=BackendEvent::Type::SignedIn;event.identity=std::move(person);
            }catch(const std::exception&){
                if(issuedToken.size()==64){try{(void)request("auth.logout",Json::object(),issuedToken);}catch(...){}}
                event.type=BackendEvent::Type::Failed;event.error="Sign-in failed.";
            }
            std::fill(password.begin(),password.end(),'\0');return event;
        });
    }
    void signOut(int slot) override {
        slotGuard(slot);
        queue([this,slot]{
            BackendEvent event;event.type=BackendEvent::Type::SignedOut;event.slot=slot;
            Slot previous;{std::lock_guard lock(slotMutex_);previous=slots_[slot];slots_[slot]={};}
            event.identity=previous.identity;
            if(!previous.token.empty()){try{(void)request("auth.logout",Json::object(),previous.token);}catch(...){}}
            return event;
        });
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
    void award(const std::string& user,const std::string& key) override {(void)request("achievements.award",{{"key",key}},tokenFor(user));}
    std::vector<ServiceFriend> friends(const std::string& user) override {
        const auto result=request("friends.list",Json::object(),tokenFor(user));const auto& entries=result.at("friends");
        if(!entries.is_array()||entries.size()>256)throw Unavailable("Invalid friend response.");
        std::vector<ServiceFriend> values;for(const auto& e:entries) {
            if(!e.at("online").is_boolean())throw Unavailable("Invalid friend response.");
            ServiceFriend friendState;friendState.gamertag=CnaService::stringField(e,"gamertag",32);friendState.online=e["online"].get<bool>();
            for(const auto* key:{"accepted","requestSent","requestReceived"})if(!e.at(key).is_boolean())throw Unavailable("Invalid friend response.");
            friendState.accepted=e["accepted"].get<bool>();friendState.requestSent=e["requestSent"].get<bool>();friendState.requestReceived=e["requestReceived"].get<bool>();
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
        Json participants=Json::array();for(const auto& user:users)participants.push_back(tokenFor(user));
        const auto result=request("leaderboards.game.begin",{{"kind","local"},{"participants",participants}},tokenFor(users.front()));
        auto id=CnaService::stringField(result,"gameplay",32);
        if(id.size()!=32||id.find_first_not_of("0123456789abcdef")!=std::string::npos)throw Unavailable("Invalid leaderboard gameplay identifier.");return id;
    }
    void commitLeaderboardGame(const std::string& gameplay,const std::string& owner,const std::vector<ServiceLeaderboardWrite>& rows) override {
        Json entries=Json::array();
        for(const auto& row:rows) {
            Json columns=Json::object();for(const auto& [name,column]:row.columns) {
                Json value;std::visit([&](const auto& scalar){value=scalar;},column.value);columns[name]={{"type",column.type},{"value",value}};
            }
            entries.push_back(Json{{"userId",row.userId},{"key",row.key},{"mode",row.mode},{"rating",row.rating},{"columns",columns}});
        }
        (void)request("leaderboards.game.commit",{{"gameplay",gameplay},{"entries",entries}},tokenFor(owner));
    }
    std::vector<unsigned char> asset(const std::string& hash) override {
        if(hash.size()!=64||hash.find_first_not_of("0123456789abcdef")!=std::string::npos)throw Unavailable("Invalid service asset identifier.");
        const auto token=tokenFor({});
        std::lock_guard cacheLock(cacheMutex_);
        std::filesystem::path root;
        if(const auto* configured=std::getenv("CNA_GAMER_SERVICES_CACHE_DIR");configured&&*configured)root=configured;
        else if(const auto* xdg=std::getenv("XDG_CACHE_HOME");xdg&&*xdg)root=std::filesystem::path(xdg)/"cna/gamer-services/assets";
        else if(const auto* home=std::getenv("HOME");home&&*home)root=std::filesystem::path(home)/".cache/cna/gamer-services/assets";
        const auto path=root/hash;
        auto valid=[&hash](const std::vector<unsigned char>& bytes) {
            System::Security::Cryptography::SHA256 algorithm;const auto digest=algorithm.ComputeHash(bytes);
            constexpr char digits[]="0123456789abcdef";std::string actual;
            for(auto byte:digest){actual+=digits[byte>>4];actual+=digits[byte&15];}return actual==hash;
        };
        if(!root.empty()) {
            std::error_code error;
            const auto size=std::filesystem::file_size(path,error);
            if(!error&&size>0&&size<=16777216&&!std::filesystem::is_symlink(path,error)) {
                std::ifstream stream(path,std::ios::binary);std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
                if(stream.read(reinterpret_cast<char*>(bytes.data()),bytes.size())&&valid(bytes))return bytes;
            }
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
        if(!valid(bytes))throw Unavailable("Corrupt service asset.");
        if(!root.empty()) {
            std::error_code error;std::filesystem::create_directories(root,error);
            if(!error) {
                std::uintmax_t cachedBytes=0;
                for(std::filesystem::directory_iterator entry(root,error),end;!error&&entry!=end;entry.increment(error)) {
                    const auto name=entry->path().filename().string();
                    if(name.size()!=64||name.find_first_not_of("0123456789abcdef")!=std::string::npos||!entry->is_regular_file(error))continue;
                    cachedBytes+=entry->file_size(error);if(cachedBytes>268435456)return bytes;
                }
                if(error||bytes.size()>268435456-cachedBytes)return bytes;
                const auto temporary=root/(hash+"."+prefix_+".tmp");
                {std::ofstream output(temporary,std::ios::binary);output.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
                 if(!output){std::filesystem::remove(temporary,error);return bytes;}}
                std::filesystem::rename(temporary,path,error);if(error)std::filesystem::remove(temporary,error);
            }
        }
        return bytes;
    }
private:
    struct Slot {ServiceIdentity identity;std::string token;};
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
        static std::once_flag initialized;
        std::call_once(initialized,[]{if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK)throw Unavailable("Secure transport initialization failed.");});
        if(!(curl_version_info(CURLVERSION_NOW)->features&CURL_VERSION_SSL))throw Unavailable("libcurl has no TLS support.");
        if(config_.endpoint.empty())throw Unavailable("No CNA service endpoint configured.");
        const auto id=prefix_+"-"+std::to_string(++sequence_);
        Json request{{"v",1},{"id",id},{"game",config_.gameId},{"op",op},{"args",std::move(args)}};
        if(!token.empty())request["token"]=token;
        const auto bytes=request.dump();if(bytes.size()>CnaService::MaxMessageBytes)throw Unavailable("Service request limit exceeded.");
        std::unique_ptr<CURL,decltype(&curl_easy_cleanup)> curl(curl_easy_init(),curl_easy_cleanup);
        if(!curl)throw Unavailable("Service transport unavailable.");
        std::string output;
        curl_easy_setopt(curl.get(),CURLOPT_URL,config_.endpoint.c_str());
        curl_easy_setopt(curl.get(),CURLOPT_PROTOCOLS_STR,config_.insecureLoopback?"https,http":"https");
        curl_easy_setopt(curl.get(),CURLOPT_FOLLOWLOCATION,0L);curl_easy_setopt(curl.get(),CURLOPT_PROXY,"");
        curl_easy_setopt(curl.get(),CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(curl.get(),CURLOPT_SSL_VERIFYHOST,2L);
        curl_easy_setopt(curl.get(),CURLOPT_SSLVERSION,CURL_SSLVERSION_TLSv1_2);
        if(!config_.caBundle.empty())curl_easy_setopt(curl.get(),CURLOPT_CAINFO,config_.caBundle.c_str());
        curl_easy_setopt(curl.get(),CURLOPT_NOSIGNAL,1L);curl_easy_setopt(curl.get(),CURLOPT_CONNECTTIMEOUT_MS,3000L);
        curl_easy_setopt(curl.get(),CURLOPT_TIMEOUT_MS,10000L);
        curl_easy_setopt(curl.get(),CURLOPT_POSTFIELDS,bytes.data());curl_easy_setopt(curl.get(),CURLOPT_POSTFIELDSIZE,static_cast<long>(bytes.size()));
        curl_easy_setopt(curl.get(),CURLOPT_WRITEFUNCTION,&OnlineBackend::write);curl_easy_setopt(curl.get(),CURLOPT_WRITEDATA,&output);
        curl_slist* headers=curl_slist_append(nullptr,"Content-Type: application/json");
        std::unique_ptr<curl_slist,decltype(&curl_slist_free_all)> owned(headers,curl_slist_free_all);
        if(!headers)throw Unavailable("Service transport unavailable.");
        curl_easy_setopt(curl.get(),CURLOPT_HTTPHEADER,headers);
        if(curl_easy_perform(curl.get())!=CURLE_OK)throw Unavailable("CNA service connection failed.");
        long status=0;curl_easy_getinfo(curl.get(),CURLINFO_RESPONSE_CODE,&status);if(status!=200)throw Unavailable("CNA service HTTP failure.");
        const auto response=CnaService::parse(output);
        if(!response.is_object()||response.size()!=4||response.at("v")!=1||CnaService::stringField(response,"id",64)!=id||!response.at("result").is_object())
            throw Unavailable("CNA service protocol mismatch.");
        const auto error=CnaService::stringField(response,"error",64);
        if(error=="UNAUTHENTICATED"&&!token.empty()) {
            std::lock_guard lock(slotMutex_);
            for(int i=0;i<4;++i)if(slots_[i].token==token) {
                BackendEvent event;event.type=BackendEvent::Type::SignedOut;event.slot=i;event.identity=slots_[i].identity;
                slots_[i]={};ready(std::move(event));
            }
        }
        if(error!="OK")throw Unavailable("CNA service error: "+error);
        return response["result"];
    }
    Json request(const std::string& op,Json args,const std::string& token) {
        std::lock_guard lock(transportMutex_);
        try {
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
            if(op.starts_with("friends.")&&!capabilities_.contains("friend-requests"))throw Unavailable("CNA service friend-request capability missing.");
            if(op.starts_with("leaderboards.game.")&&!capabilities_.contains("local-leaderboard-commit"))throw Unavailable("CNA service local leaderboard commit capability missing.");
            if(op.starts_with("leaderboards.")&&!capabilities_.contains("leaderboard-reads"))throw Unavailable("CNA service leaderboard-read capability missing.");
            if(op=="assets.read"&&!capabilities_.contains("assets"))throw Unavailable("CNA service asset capability missing.");
            if(op=="presence.set"&&!capabilities_.contains("presence"))throw Unavailable("CNA service presence capability missing.");
            return exchange(op,std::move(args),token);
        }catch(const Unavailable&){throw;}
         catch(...){throw Unavailable("CNA service response validation failed.");}
    }
    static std::string prefix() {
        std::random_device source;std::string value;constexpr char hex[]="0123456789abcdef";
        for(int i=0;i<16;++i){auto byte=source();value+=hex[(byte>>4)&15];value+=hex[byte&15];}return value;
    }
    CNA::GamerServices::Configuration config_;
    std::mutex slotMutex_,transportMutex_,cacheMutex_;
    std::array<Slot,4> slots_{};
    std::string prefix_=prefix();
    unsigned long long sequence_=0;
    bool negotiated_=false;
    std::set<std::string> capabilities_;
};
class FakeBackend final : public QueuedBackend {
public:
    FakeBackend(std::vector<ServiceIdentity> identities,std::vector<ServiceAchievement> catalog,std::vector<ServiceLeaderboardFixture> boards):QueuedBackend(false),identities_(std::move(identities)),catalog_(std::move(catalog)),boards_(std::move(boards)) {}
    bool serviceEnabled() const override{return true;}
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
            return value;
        }throw Unavailable("Gamer not found.");
    }
    std::vector<ServiceAchievement> achievements(const std::string& user) override {
        require(user);auto values=catalog_;for(auto& entry:values)entry.earnedTicks=earned_[user][entry.key];return values;
    }
    void award(const std::string& user,const std::string& key) override {
        require(user);for(const auto& entry:catalog_)if(entry.key==key){if(!earned_[user][key])earned_[user][key]=638000000000000000LL;return;}
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
            value.presence=value.online?presence_[target.userId]:"";result.push_back(std::move(value));
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
    void commitLeaderboardGame(const std::string& gameplay,const std::string& owner,const std::vector<ServiceLeaderboardWrite>& rows) override {
        require(owner);if(!games_.contains(gameplay)||games_[gameplay].front()!=owner)throw Unavailable("Invalid fixture game scope.");
        for(const auto& row:rows) {
            if(std::find(games_[gameplay].begin(),games_[gameplay].end(),row.userId)==games_[gameplay].end())throw Unavailable("Nonmember fixture write.");
            const auto board=std::find_if(boards_.begin(),boards_.end(),[&](const auto& value){return value.key==row.key&&value.mode==row.mode;});
            if(board==boards_.end())throw Unavailable("Fixture board not found.");
        }
        for(const auto& row:rows) {
            auto& board=*std::find_if(boards_.begin(),boards_.end(),[&](const auto& value){return value.key==row.key&&value.mode==row.mode;});
            auto entry=std::find_if(board.entries.begin(),board.entries.end(),[&](const auto& value){return value.userId==row.userId;});
            if(entry==board.entries.end()){ServiceLeaderboardEntry value;value.userId=row.userId;value.gamertag=profileById(row.userId).gamertag;board.entries.push_back(value);entry=std::prev(board.entries.end());}
            else if(board.ascending?row.rating>=entry->rating:row.rating<=entry->rating)continue;
            entry->rating=row.rating;entry->columns=row.columns;
        }
        games_.erase(gameplay);
    }
    std::vector<unsigned char> asset(const std::string&) override {throw Unavailable("Fake fixture has no assets.");}
private:
    ServiceIdentity profileById(const std::string& id) {for(const auto& person:identities_)if(person.userId==id)return person;throw Unavailable("Unknown fixture gamer.");}
    std::map<std::string,std::vector<std::string>> games_;
    int gameSequence_=0;
    void require(const std::string& user) {if(user.empty()||std::find(slots_.begin(),slots_.end(),user)==slots_.end())throw Unavailable("Gamer signed out.");}
    std::vector<ServiceIdentity> identities_;
    std::vector<ServiceAchievement> catalog_;
    std::vector<ServiceLeaderboardFixture> boards_;
    std::array<std::string,4> slots_{};
    std::map<std::string,std::map<std::string,long long>> earned_;
    std::set<std::pair<std::string,std::string>> edges_;
    std::map<std::string,std::string> presence_;
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
void setBackendForTesting(std::shared_ptr<IGamerServicesBackend> value) {std::lock_guard lock(registryMutex);current=std::move(value);}
std::shared_ptr<IGamerServicesBackend> makeFakeBackend(std::vector<ServiceIdentity> people,std::vector<ServiceAchievement> catalog,std::vector<ServiceLeaderboardFixture> boards) {
    return std::make_shared<FakeBackend>(std::move(people),std::move(catalog),std::move(boards));
}
}

// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/GamerServices/Configuration.hpp"
#include "CnaService/Protocol.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include <curl/curl.h>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <random>
#include <thread>
#include <algorithm>

namespace CNA::Internal::GamerServices {
using CnaService::Json;
namespace {
using Unavailable=Microsoft::Xna::Framework::GamerServices::GamerServicesNotAvailableException;
void slotGuard(int slot) {if(slot<0||slot>3)throw System::ArgumentOutOfRangeException("slot");}
ServiceIdentity identity(const Json& j) {
    ServiceIdentity value;
    value.userId=CnaService::stringField(j,"userId",64);value.gamertag=CnaService::stringField(j,"gamertag",32);
    value.displayName=CnaService::stringField(j,"displayName",128);value.motto=CnaService::stringField(j,"motto",256);
    value.region=CnaService::stringField(j,"region",16);
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
            values.push_back({CnaService::stringField(e,"gamertag",32),e["online"].get<bool>()});
        }return values;
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
                if(hello.at("version")!=1||!hello.at("capabilities").is_array())throw Unavailable("CNA service negotiation failed.");
                for(const auto* required:{"identity","authentication","achievements"}) {
                    const auto& caps=hello["capabilities"];
                    if(std::find(caps.begin(),caps.end(),Json(required))==caps.end())throw Unavailable("CNA service capability missing.");
                }
                negotiated_=true;
            }
            return exchange(op,std::move(args),token);
        }catch(const Unavailable&){throw;}
         catch(...){throw Unavailable("CNA service response validation failed.");}
    }
    static std::string prefix() {
        std::random_device source;std::string value;constexpr char hex[]="0123456789abcdef";
        for(int i=0;i<16;++i){auto byte=source();value+=hex[(byte>>4)&15];value+=hex[byte&15];}return value;
    }
    CNA::GamerServices::Configuration config_;
    std::mutex slotMutex_,transportMutex_;
    std::array<Slot,4> slots_{};
    std::string prefix_=prefix();
    unsigned long long sequence_=0;
    bool negotiated_=false;
};
class FakeBackend final : public QueuedBackend {
public:
    FakeBackend(std::vector<ServiceIdentity> identities,std::vector<ServiceAchievement> catalog):QueuedBackend(false),identities_(std::move(identities)),catalog_(std::move(catalog)) {}
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
    ServiceIdentity profile(const std::string& tag) override {for(const auto& person:identities_)if(person.gamertag==tag)return person;throw Unavailable("Gamer not found.");}
    std::vector<ServiceAchievement> achievements(const std::string& user) override {
        require(user);auto values=catalog_;for(auto& entry:values)entry.earnedTicks=earned_[user][entry.key];return values;
    }
    void award(const std::string& user,const std::string& key) override {
        require(user);for(const auto& entry:catalog_)if(entry.key==key){if(!earned_[user][key])earned_[user][key]=638000000000000000LL;return;}
        throw Unavailable("Achievement not found.");
    }
    std::vector<ServiceFriend> friends(const std::string& user) override {require(user);return {};}
private:
    void require(const std::string& user) {if(user.empty()||std::find(slots_.begin(),slots_.end(),user)==slots_.end())throw Unavailable("Gamer signed out.");}
    std::vector<ServiceIdentity> identities_;
    std::vector<ServiceAchievement> catalog_;
    std::array<std::string,4> slots_{};
    std::map<std::string,std::map<std::string,long long>> earned_;
};
std::mutex registryMutex;
std::shared_ptr<IGamerServicesBackend> current;
}
std::shared_ptr<IGamerServicesBackend> backend() {
    std::lock_guard lock(registryMutex);if(!current)current=std::make_shared<OnlineBackend>(CNA::GamerServices::resolveConfiguration());return current;
}
void setBackendForTesting(std::shared_ptr<IGamerServicesBackend> value) {std::lock_guard lock(registryMutex);current=std::move(value);}
std::shared_ptr<IGamerServicesBackend> makeFakeBackend(std::vector<ServiceIdentity> people,std::vector<ServiceAchievement> catalog) {
    return std::make_shared<FakeBackend>(std::move(people),std::move(catalog));
}
}

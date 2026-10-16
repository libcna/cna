// SPDX-License-Identifier: MS-PL
// A response lost with its connection is asked for once more under the same request ID, which a
// service keeping request outcomes answers from its record. A loopback HTTP service stands in for
// the CNA service and drops chosen connections after reading the request. It closes every
// connection after one exchange: libcurl itself re-sends a request, once, when a reused keep-alive
// connection dies before any answer, and that would hide which attempt is whose.
#include <gtest/gtest.h>
#include "System/Environment.hpp"
#include <optional>
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "System/IServiceProvider.hpp"
#if defined(__unix__) || defined(__APPLE__)
#include "CNA/Internal/GamerServices/BackendConfiguration.hpp"
#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include "CNA/Internal/GamerServices/AssetDiskCache.hpp"
#include <filesystem>
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/Internal/GamerServices/VoiceMutes.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp"
#include <nlohmann/json.hpp>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
namespace Service=CNA::Internal::GamerServices;
namespace Deployment=CNA::GamerServices;
using Json=nlohmann::json;
using Unavailable=Microsoft::Xna::Framework::GamerServices::GamerServicesNotAvailableException;

class LoopbackService {
public:
    explicit LoopbackService(bool outcomes,bool privacy=false,bool refresh=false):outcomes_(outcomes),privacy_(privacy),refresh_(refresh) {
        listener_=socket(AF_INET,SOCK_STREAM,0);
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        EXPECT_EQ(0,bind(listener_,reinterpret_cast<sockaddr*>(&address),sizeof(address)));
        socklen_t size=sizeof(address);getsockname(listener_,reinterpret_cast<sockaddr*>(&address),&size);
        port_=ntohs(address.sin_port);EXPECT_EQ(0,listen(listener_,8));
        thread_=std::thread([this]{serve();});
    }
    ~LoopbackService() {
        stop_=true;shutdown(listener_,SHUT_RDWR);
        // Darwin refuses shutdown() on a listening socket (ENOTCONN) and leaves accept() blocked,
        // so a connection of our own is what reliably returns it; where shutdown() already woke
        // it, this connect is simply refused.
        if(const int wake=socket(AF_INET,SOCK_STREAM,0);wake>=0) {
            sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(port_);
            (void)connect(wake,reinterpret_cast<sockaddr*>(&address),sizeof(address));close(wake);
        }
        if(const int open=connection_.load();open>=0)shutdown(open,SHUT_RDWR);
        thread_.join();close(listener_);
    }
    std::string endpoint() const {return "http://127.0.0.1:"+std::to_string(port_)+"/cna/v1";}
    void revokeNextPing() { std::lock_guard lock(mutex_); revokePing_=true; }
    void assetPolicy(const std::string& code) {std::lock_guard lock(mutex_);assetCode_=code;}
    void policy(std::string communication, Json blocked) { std::lock_guard lock(mutex_); communication_=std::move(communication); blocked_=std::move(blocked); }
    // The next `count` requests of `op` are read, then their connection closes without an answer.
    void drop(const std::string& op,int count) {std::lock_guard lock(mutex_);drops_[op]=count;}
    std::vector<std::string> ids(const std::string& op) {std::lock_guard lock(mutex_);return seen_[op];}
    std::vector<Json> arguments(const std::string& op) {std::lock_guard lock(mutex_);return args_[op];}
    // Offers avatars and catalog packs; avatars.catalog answers with `newest` as the newest catalog.
    void avatars(long long newest) {std::lock_guard lock(mutex_);avatars_=true;newestCatalog_=newest;}
private:
    void serve() {
        while(!stop_) {
            const int connection=accept(listener_,nullptr,nullptr);
            if(connection<0)continue;
            connection_=connection;
            if(stop_){shutdown(connection,SHUT_RDWR);}
            std::string buffer;
            while(!stop_) {
                const auto request=readRequest(connection,buffer);
                if(request.empty())break;
                const auto body=Json::parse(request);const auto op=body.at("op").get<std::string>();
                bool dropped=false;
                {std::lock_guard lock(mutex_);seen_[op].push_back(body.at("id").get<std::string>());args_[op].push_back(body.value("args",Json::object()));
                 if(drops_[op]>0){--drops_[op];dropped=true;}}
                if(dropped)break;
                std::string code="OK";
                {std::lock_guard lock(mutex_);if(op=="assets.read")code=assetCode_;if(op=="auth.ping"&&revokePing_){code="UNAUTHENTICATED";revokePing_=false;}}
                const auto text=Json{{"v",1},{"id",body.at("id")},{"error",code},{"result",code=="OK"?result(op,body.at("args")):Json::object()}}.dump();
                const auto reply="HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "+
                    std::to_string(text.size())+"\r\n\r\n"+text;
                (void)send(connection,reply.data(),reply.size(),MSG_NOSIGNAL);
                break;
            }
            connection_=-1;close(connection);
        }
    }
    static std::string readRequest(int connection,std::string& buffer) {
        char chunk[4096];
        for(;;) {
            if(const auto end=buffer.find("\r\n\r\n");end!=std::string::npos) {
                const auto at=buffer.find("Content-Length: ");
                if(at==std::string::npos||at>end)return {};
                const auto length=std::stoul(buffer.substr(at+16));
                if(buffer.size()>=end+4+length) {
                    auto body=buffer.substr(end+4,length);buffer.erase(0,end+4+length);return body;
                }
            }
            const auto count=recv(connection,chunk,sizeof(chunk),0);
            if(count<=0)return {};
            buffer.append(chunk,static_cast<std::size_t>(count));
        }
    }
    Json result(const std::string& op,const Json& args) {
        const auto now=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        if(op=="hello") {
            Json capabilities=Json::array({"identity","authentication","achievements","assets"});
            if(outcomes_)capabilities.push_back("request-outcomes");
            if(refresh_)capabilities.push_back("session-refresh");
            if(privacy_)for(const auto* name:{"privacy","friend-voice","heartbeat","policy-refresh"})capabilities.push_back(name);
            {std::lock_guard lock(mutex_);if(avatars_)for(const auto* name:{"avatars","avatar-catalog-packs","files"})capabilities.push_back(name);}
            return {{"version",1},{"capabilities",capabilities},{"maxMessageBytes",65536}};
        }
        if(op=="auth.login"||op=="auth.refresh") {
            std::lock_guard lock(mutex_);
            Json credentials= {{"identity",{{"userId","u1"},{"gamertag","Alice"},{"displayName","Alice"},{"motto",""},{"region","US"},
                {"picture",""},{"gamerScore",0},{"totalAchievements",0},{"allowOnlineSessions",true}}},
                {"token",std::string(64,op=="auth.refresh"?'b':'a')},{"expires",now+3600},{"serverTime",now},
                {"privileges",{{"communication",communication_},{"profileViewing","blocked"},{"userContent","everyone"},
                    {"trade",false},{"purchase",true},{"premium",false}}}};
            if(refresh_) {credentials["refreshToken"]=std::string(64,'c');credentials["refreshExpires"]=now+86400;}
            return credentials;
        }
        if(op=="assets.read") {
            const std::string bytes="private-picture";
            const auto offset=args.at("offset").get<std::size_t>(),length=args.at("length").get<std::size_t>();
            std::string hex;constexpr char digits[]="0123456789abcdef";
            for(unsigned char byte:bytes.substr(offset,length)){hex+=digits[byte>>4];hex+=digits[byte&15];}
            return {{"hash",args.at("hash")},{"size",bytes.size()},{"mime","image/png"},{"offset",offset},{"hex",hex}};
        }
        if(op=="auth.ping" || op=="privacy.list") {
            std::lock_guard lock(mutex_);
            if(op=="privacy.list")return {{"blocked",blocked_}};
            return {{"privileges",{{"communication",communication_},{"profileViewing","blocked"},{"userContent","everyone"},
                {"trade",false},{"purchase",true},{"premium",false}}},{"blocked",blocked_}};
        }
        if(op=="achievements.award")return {{"awarded",true},{"name","First"}};
        if(op=="avatars.get") {
            Json avatars=Json::array();
            for(const auto& id:args.at("userIds"))avatars.push_back({{"userId",id},{"description",nullptr},{"revision",0}});
            return {{"avatars",avatars}};
        }
        if(op=="avatars.catalog") {std::lock_guard lock(mutex_);return {{"version",args.value("version",newestCatalog_)},{"manifest",Json::object()}};}
        if(op=="avatars.catalogPack")return {{"version",args.at("version")}};
        return Json::object();
    }
    bool outcomes_,privacy_,refresh_,revokePing_=false,avatars_=false;long long newestCatalog_=3;int listener_=-1;unsigned short port_=0;std::atomic<bool> stop_{false};std::atomic<int> connection_{-1};std::thread thread_;
    std::string assetCode_="OK", communication_="friends";
    Json blocked_=Json::array({"Bob","Carol"});
    std::mutex mutex_;std::map<std::string,int> drops_;std::map<std::string,std::vector<std::string>> seen_;std::map<std::string,std::vector<Json>> args_;
};

class ServiceRequestRetryTest : public ::testing::Test {
protected:
    void SetUp() override {previous=Service::backend();System::Environment::SetEnvironmentVariable("CNA_GAMER_SERVICES_CREDENTIALS_DIR", std::string("0"));}
    void TearDown() override {
        Service::setBackendForTesting(previous);Deployment::setConfigurationOverride({});
        System::Environment::SetEnvironmentVariable("CNA_GAMER_SERVICES_CREDENTIALS_DIR", std::nullopt);
    }
    std::shared_ptr<Service::IGamerServicesBackend> signedIn(const LoopbackService& service,Service::ServiceIdentity* identity=nullptr) {
        Deployment::setConfigurationOverride(Deployment::Configuration{service.endpoint(),"retry",{},true});
        Service::setBackendForTesting({});
        auto backend=Service::backend();
        backend->signIn(0,"alice","alice-password");
        for(int i=0;i<500;++i) {
            for(const auto& event:backend->pump())
                if(event.type==Service::BackendEvent::Type::SignedIn){if(identity)*identity=event.identity;return backend;}
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        ADD_FAILURE()<<"sign-in did not complete";return backend;
    }
    std::shared_ptr<Service::IGamerServicesBackend> previous;
};
}

TEST_F(ServiceRequestRetryTest, ADevelopmentCatalogIsNeverAskedForOrAccepted) {
    // Catalog v3 is the baseline: the client never asks the service for catalog 1 or 2, and a
    // service whose newest catalog is one is not a catalog this client edits against.
    LoopbackService service(true);
    service.avatars(2);
    const auto backend=signedIn(service);
    for(int legacy:{1,2}) {
        EXPECT_THROW((void)backend->avatarCatalogPack(legacy),Unavailable) << legacy;
        EXPECT_THROW((void)backend->avatarCatalog(legacy),Unavailable) << legacy;
    }
    EXPECT_TRUE(service.arguments("avatars.catalogPack").empty());
    EXPECT_THROW((void)backend->avatarCatalog(0),Unavailable);
    ASSERT_EQ(1u,service.arguments("avatars.catalog").size());
    EXPECT_THROW((void)backend->newestAvatarCatalog(),Unavailable);
    ASSERT_EQ(2u,service.arguments("avatars.catalog").size());
    EXPECT_EQ(false,service.arguments("avatars.catalog")[1]["manifest"]);
    EXPECT_NO_THROW((void)backend->avatarCatalogPack(3));
    EXPECT_NO_THROW((void)backend->avatarCatalog(3));
}

TEST_F(ServiceRequestRetryTest, AnAvatarReadThatNamesNoCatalogsSendsNoCatalogList) {
    // A caller that only reads descriptions (the editor's read-back) names no catalogs; the service
    // refuses an empty list, so none is sent. A caller that names its catalogs negotiates.
    LoopbackService service(true);
    service.avatars(3);
    const auto backend=signedIn(service);
    ASSERT_EQ(1u,backend->avatars({"u1"}).size());
    ASSERT_EQ(1u,backend->avatars({"u1"},Service::AvatarClientCatalogs{{3},true}).size());
    const auto reads=service.arguments("avatars.get");
    ASSERT_EQ(2u,reads.size());
    EXPECT_FALSE(reads[0].contains("catalogs"));
    EXPECT_EQ(Json::array({1,2,3}),reads[0]["formats"]);
    // Every read states the catalog contract level this client reads, with or without catalogs.
    EXPECT_EQ(Service::Avatars::CatalogReaderLevel,reads[0]["reader"]);
    EXPECT_EQ(Service::Avatars::CatalogReaderLevel,reads[1]["reader"]);
    EXPECT_EQ(Json::array({3}),reads[1]["catalogs"]);
    EXPECT_TRUE(reads[1]["catalogUpdates"].is_boolean());
}

TEST_F(ServiceRequestRetryTest, ALostResponseIsAskedForOnceMoreUnderTheSameRequestId) {
    LoopbackService service(true);
    const auto backend=signedIn(service);
    service.drop("achievements.award",1);
    EXPECT_EQ("First",backend->award("u1","first"));
    const auto ids=service.ids("achievements.award");
    ASSERT_EQ(2u,ids.size());
    EXPECT_EQ(ids[0],ids[1]);
}
TEST_F(ServiceRequestRetryTest, OnlyOnceThenTheServiceIsUnavailable) {
    LoopbackService service(true);
    const auto backend=signedIn(service);
    service.drop("achievements.award",2);
    EXPECT_THROW((void)backend->award("u1","first"),Unavailable);
    EXPECT_EQ(2u,service.ids("achievements.award").size());
}
TEST_F(ServiceRequestRetryTest, AServiceThatKeepsNoOutcomesIsNotAskedAgain) {
    LoopbackService service(false);
    const auto backend=signedIn(service);
    service.drop("achievements.award",1);
    EXPECT_THROW((void)backend->award("u1","first"),Unavailable);
    EXPECT_EQ(1u,service.ids("achievements.award").size());
}
TEST_F(ServiceRequestRetryTest, ASignInWhoseResultIsASecretIsNotAskedAgain) {
    LoopbackService service(true);
    Deployment::setConfigurationOverride(Deployment::Configuration{service.endpoint(),"retry",{},true});
    Service::setBackendForTesting({});
    const auto backend=Service::backend();
    service.drop("auth.login",1);
    backend->signIn(0,"alice","alice-password");
    bool failed=false;
    for(int i=0;i<500&&!failed;++i) {
        for(const auto& event:backend->pump())failed=failed||event.type==Service::BackendEvent::Type::Failed;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(failed);
    EXPECT_EQ(1u,service.ids("auth.login").size());
}
#endif

#if defined(__unix__) || defined(__APPLE__)
// GSH-03: the account's privileges come with its credentials, and its block list is read at sign-in.
TEST_F(ServiceRequestRetryTest, SignInCarriesTheAccountsPrivilegesAndBlockList) {
    LoopbackService service(true,true);
    Service::ServiceIdentity person;
    (void)signedIn(service,&person);
    EXPECT_EQ("friends",person.communication);
    EXPECT_EQ("blocked",person.profileViewing);
    EXPECT_EQ("everyone",person.userContent);
    EXPECT_FALSE(person.tradeContent);
    EXPECT_TRUE(person.purchaseContent);
    EXPECT_FALSE(person.premiumContent);
    EXPECT_EQ((std::vector<std::string>{"Bob","Carol"}),person.blocked);
}
#endif

#if defined(__unix__) || defined(__APPLE__)
// GSH-04: the heartbeat says whether this machine can talk now, which friends see as
// FriendGamer.HasVoice; it follows the capability as it changes.
TEST_F(ServiceRequestRetryTest, TheHeartbeatSaysWhetherThisMachineCanTalk) {
    LoopbackService service(true,true);
    std::atomic<bool> capable{true};
    const auto previousProvider=Service::setLocalVoiceCapabilityProvider([&]{return capable.load();});
    Service::setHeartbeatIntervalForTesting(1);
    struct Restore {
        std::function<bool()> provider;
        ~Restore(){Service::setHeartbeatIntervalForTesting(30);(void)Service::setLocalVoiceCapabilityProvider(provider);}
    } restore{previousProvider};
    const auto backend=signedIn(service);
    auto awaitPing=[&](bool voice) {
        for(int i=0;i<400;++i) {
            (void)backend->pump();
            const auto pings=service.arguments("auth.ping");
            if(!pings.empty()&&pings.back().contains("voice")&&pings.back()["voice"]==voice)return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    };
    EXPECT_TRUE(awaitPing(true));
    capable=false;
    EXPECT_TRUE(awaitPing(false));
}
#endif


#if defined(__unix__) || defined(__APPLE__)
namespace {
struct PictureCacheEnvironment {
    std::filesystem::path path;
    std::optional<std::string> previous;
    PictureCacheEnvironment() {
        if(const auto* value=std::getenv("CNA_GAMER_SERVICES_CACHE_DIR"))previous=value;
        path=std::filesystem::temp_directory_path()/("cna-picture-policy-"+std::to_string(getpid()));
        std::filesystem::remove_all(path);
        System::Environment::SetEnvironmentVariable("CNA_GAMER_SERVICES_CACHE_DIR", path);
    }
    ~PictureCacheEnvironment() {
        if(previous)System::Environment::SetEnvironmentVariable("CNA_GAMER_SERVICES_CACHE_DIR", *previous);else System::Environment::SetEnvironmentVariable("CNA_GAMER_SERVICES_CACHE_DIR", std::nullopt);
        std::filesystem::remove_all(path);
    }
};
std::vector<unsigned char> PictureBytes() {const std::string text="private-picture";return {text.begin(),text.end()};}
std::string PictureHash() {const auto bytes=PictureBytes();return Service::Avatars::sha256Hex(bytes);}
}
TEST_F(ServiceRequestRetryTest, CachedPictureRechecksPolicyAndDoesNotCacheDenial) {
    PictureCacheEnvironment cache;
    LoopbackService service(true);
    const auto backend=signedIn(service);
    EXPECT_EQ(PictureBytes(),backend->asset(PictureHash()));
    ASSERT_EQ(1u,service.ids("assets.read").size());
    EXPECT_EQ(PictureBytes(),backend->asset(PictureHash()));
    ASSERT_EQ(2u,service.ids("assets.read").size());
    EXPECT_EQ(1,service.arguments("assets.read").back()["length"]);
    service.assetPolicy("NOT_AUTHORIZED");
    EXPECT_THROW((void)backend->asset(PictureHash()),Unavailable);
    EXPECT_EQ(3u,service.ids("assets.read").size());
    EXPECT_TRUE(std::filesystem::exists(cache.path/PictureHash()));
    service.assetPolicy("OK");
    EXPECT_EQ(PictureBytes(),backend->asset(PictureHash()));
}
TEST_F(ServiceRequestRetryTest, AnotherBackendCannotUseCachedPictureWithoutCurrentAuthorization) {
    PictureCacheEnvironment cache;
    LoopbackService allowed(true),denied(true);
    auto first=signedIn(allowed);
    EXPECT_EQ(PictureBytes(),first->asset(PictureHash()));
    first.reset();
    denied.assetPolicy("NOT_AUTHORIZED");
    const auto second=signedIn(denied);
    EXPECT_THROW((void)second->asset(PictureHash()),Unavailable);
    EXPECT_EQ(1u,denied.ids("assets.read").size());
}
TEST_F(ServiceRequestRetryTest, InvalidatedTokenCannotReadCachedPicture) {
    PictureCacheEnvironment cache;
    LoopbackService service(true);
    const auto backend=signedIn(service);
    EXPECT_EQ(PictureBytes(),backend->asset(PictureHash()));
    service.assetPolicy("UNAUTHENTICATED");
    EXPECT_THROW((void)backend->asset(PictureHash()),Unavailable);
    EXPECT_EQ(2u,service.ids("assets.read").size());
    EXPECT_THROW((void)backend->asset(PictureHash()),Unavailable);
    EXPECT_EQ(2u,service.ids("assets.read").size());
}
#endif

#if defined(__unix__) || defined(__APPLE__)
TEST_F(ServiceRequestRetryTest, HeartbeatRefreshesPublishedPolicyWithoutReplacingTheGamer) {
    using namespace Microsoft::Xna::Framework::GamerServices;
    struct Provider : System::IServiceProvider { void* GetService(const std::type_info&) const override { return nullptr; } } provider;
    LoopbackService service(true,true);
    Service::setHeartbeatIntervalForTesting(1);
    struct Restore { ~Restore(){Service::setHeartbeatIntervalForTesting(30);} } restore;
    Deployment::setConfigurationOverride(Deployment::Configuration{service.endpoint(),"retry",{},true});
    Service::setBackendForTesting({});
    auto backend=Service::backend();
    if(!GamerServicesDispatcher::getIsInitializedProperty()) GamerServicesDispatcher::Initialize(provider);
    auto await=[&](auto condition) {
        for(int i=0;i<500;++i) { GamerServicesDispatcher::Update(); if(condition())return true; std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
        return false;
    };
    backend->signIn(0,"alice","alice-password");
    ASSERT_TRUE(await([&]{return Gamer::getSignedInGamersProperty()->getCountProperty()==1;}));
    auto* gamer=(*Gamer::getSignedInGamersProperty())[0];
    EXPECT_EQ(GamerPrivilegeSetting::FriendsOnly,gamer->getPrivilegesProperty().getAllowCommunicationProperty());
    Service::setVoiceMuted("Alice","Dave",true);
    service.policy("blocked",Json::array({"Dave"}));
    EXPECT_TRUE(await([&]{return gamer->getPrivilegesProperty().getAllowCommunicationProperty()==GamerPrivilegeSetting::Blocked && Service::playerBlocked("Alice","Dave");}));
    EXPECT_FALSE(Service::playerBlocked("Alice","Bob"));
    EXPECT_EQ(gamer,(*Gamer::getSignedInGamersProperty())[0]);
    EXPECT_FALSE(gamer->getIsDisposedProperty());
    service.policy("everyone",Json::array());
    EXPECT_TRUE(await([&]{return gamer->getPrivilegesProperty().getAllowCommunicationProperty()==GamerPrivilegeSetting::Everyone && !Service::playerBlocked("Alice","Dave");}));
    EXPECT_TRUE(Service::voiceMuted("Alice","Dave")) << "policy refresh must preserve the local mute";
    backend->signOut(0);
    EXPECT_TRUE(await([&]{return Gamer::getSignedInGamersProperty()->getCountProperty()==0;}));
    Service::resetVoiceMutesForTesting();
}
#endif

#if defined(__unix__) || defined(__APPLE__)
TEST_F(ServiceRequestRetryTest, CredentialRenewalPublishesCurrentPrivilegesAndBlocks) {
    LoopbackService service(true,true,true);
    Service::setHeartbeatIntervalForTesting(1);
    struct Restore { ~Restore(){Service::setHeartbeatIntervalForTesting(30);} } restore;
    auto backend=signedIn(service);
    service.policy("blocked",Json::array({"Dave"}));service.revokeNextPing();
    bool changed=false;
    for(int n=0;n<500&&!changed;++n) {
        for(const auto& event:backend->pump())
            if(event.identity.userId=="u1" && event.identity.communication=="blocked" && event.identity.blocked==std::vector<std::string>{"Dave"})changed=true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(changed);
    EXPECT_EQ(1u,service.ids("auth.refresh").size());
}
#endif

#if defined(__unix__) || defined(__APPLE__)
TEST_F(ServiceRequestRetryTest, AFullServiceBlockListIsNotSilentlyDroppedAtSignIn) {
    LoopbackService service(true,true);
    Json blocked=Json::array();
    for(int n=0;n<1024;++n)blocked.push_back("Blocked-"+std::to_string(n));
    service.policy("friends",blocked);
    Service::ServiceIdentity person;
    (void)signedIn(service,&person);
    ASSERT_EQ(1024u,person.blocked.size());
    EXPECT_EQ("Blocked-0",person.blocked.front());
    EXPECT_EQ("Blocked-1023",person.blocked.back());
}
#endif

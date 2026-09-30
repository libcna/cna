// SPDX-License-Identifier: MS-PL
// A response lost with its connection is asked for once more under the same request ID, which a
// service keeping request outcomes answers from its record. A loopback HTTP service stands in for
// the CNA service and drops chosen connections after reading the request. It closes every
// connection after one exchange: libcurl itself re-sends a request, once, when a reused keep-alive
// connection dies before any answer, and that would hide which attempt is whose.
#include <gtest/gtest.h>
#if defined(__unix__) || defined(__APPLE__)
#include "CNA/Internal/GamerServices/BackendConfiguration.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
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
    explicit LoopbackService(bool outcomes):outcomes_(outcomes) {
        listener_=socket(AF_INET,SOCK_STREAM,0);
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        EXPECT_EQ(0,bind(listener_,reinterpret_cast<sockaddr*>(&address),sizeof(address)));
        socklen_t size=sizeof(address);getsockname(listener_,reinterpret_cast<sockaddr*>(&address),&size);
        port_=ntohs(address.sin_port);EXPECT_EQ(0,listen(listener_,8));
        thread_=std::thread([this]{serve();});
    }
    ~LoopbackService() {
        stop_=true;shutdown(listener_,SHUT_RDWR);
        if(const int open=connection_.load();open>=0)shutdown(open,SHUT_RDWR);
        thread_.join();close(listener_);
    }
    std::string endpoint() const {return "http://127.0.0.1:"+std::to_string(port_)+"/cna/v1";}
    // The next `count` requests of `op` are read, then their connection closes without an answer.
    void drop(const std::string& op,int count) {std::lock_guard lock(mutex_);drops_[op]=count;}
    std::vector<std::string> ids(const std::string& op) {std::lock_guard lock(mutex_);return seen_[op];}
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
                {std::lock_guard lock(mutex_);seen_[op].push_back(body.at("id").get<std::string>());
                 if(drops_[op]>0){--drops_[op];dropped=true;}}
                if(dropped)break;
                const auto text=Json{{"v",1},{"id",body.at("id")},{"error","OK"},{"result",result(op)}}.dump();
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
    Json result(const std::string& op) const {
        const auto now=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        if(op=="hello") {
            Json capabilities=Json::array({"identity","authentication","achievements"});
            if(outcomes_)capabilities.push_back("request-outcomes");
            return {{"version",1},{"capabilities",capabilities},{"maxMessageBytes",65536}};
        }
        if(op=="auth.login")
            return {{"identity",{{"userId","u1"},{"gamertag","Alice"},{"displayName","Alice"},{"motto",""},{"region","US"},
                {"picture",""},{"gamerScore",0},{"totalAchievements",0},{"allowOnlineSessions",true}}},
                {"token",std::string(64,'a')},{"expires",now+3600},{"serverTime",now}};
        if(op=="achievements.award")return {{"awarded",true},{"name","First"}};
        return Json::object();
    }
    bool outcomes_;int listener_=-1;unsigned short port_=0;std::atomic<bool> stop_{false};std::atomic<int> connection_{-1};std::thread thread_;
    std::mutex mutex_;std::map<std::string,int> drops_;std::map<std::string,std::vector<std::string>> seen_;
};

class ServiceRequestRetryTest : public ::testing::Test {
protected:
    void SetUp() override {previous=Service::backend();setenv("CNA_GAMER_SERVICES_CREDENTIALS_DIR","0",1);}
    void TearDown() override {
        Service::setBackendForTesting(previous);Deployment::setConfigurationOverride({});
        unsetenv("CNA_GAMER_SERVICES_CREDENTIALS_DIR");
    }
    std::shared_ptr<Service::IGamerServicesBackend> signedIn(const LoopbackService& service) {
        Deployment::setConfigurationOverride(Deployment::Configuration{service.endpoint(),"retry",{},true});
        Service::setBackendForTesting({});
        auto backend=Service::backend();
        backend->signIn(0,"alice","alice-password");
        for(int i=0;i<500;++i) {
            for(const auto& event:backend->pump())
                if(event.type==Service::BackendEvent::Type::SignedIn)return backend;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        ADD_FAILURE()<<"sign-in did not complete";return backend;
    }
    std::shared_ptr<Service::IGamerServicesBackend> previous;
};
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

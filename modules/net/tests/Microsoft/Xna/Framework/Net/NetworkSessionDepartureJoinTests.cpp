// SPDX-License-Identifier: MS-PL
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <gtest/gtest.h>
#include "CNA/Internal/Net/ENetBackend.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <poll.h>
#include <spawn.h>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;
namespace {
using namespace Microsoft::Xna::Framework::Net;
using namespace Microsoft::Xna::Framework::GamerServices;
using CNA::Internal::Net::ENetBackend;
struct Child {
    pid_t pid=-1; int output=-1; int status=-1; bool reaped=false;
    void wait() {
        if (pid<=0 || reaped) return;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while (waitpid(pid,&status,WNOHANG)==0) {
            if (std::chrono::steady_clock::now()>=deadline) {kill(pid,SIGKILL);waitpid(pid,&status,0);break;}
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        reaped=true;
    }
    ~Child(){wait();if(output>=0)close(output);}
};
}

// SOCKETS. Owner 2026-11-08: a later public Join/EndJoin must complete against the
// *running* successor host. Two exec'd processes preserve NetworkSession's one-per-process rule.
TEST(NetworkSessionDepartureJoinTest, LaterEndJoinCompletesAgainstTheSameHostMachine) {
    if (std::getenv("CNA_HND_NET_SUCCESSOR_CHILD")) {
        auto first=SignedInGamer::CreateInternal("A");auto second=SignedInGamer::CreateInternal("B");
        std::unique_ptr<NetworkSession> host(NetworkSession::Create(NetworkSessionType::SystemLink,
            std::vector<SignedInGamer*>{&first,&second},8,0,{}));
        host->RemoveGamer(host->getHostProperty(),NetworkSessionEndReason::ClientSignedOut);host->Update();
        ASSERT_NE(nullptr,host->getHostProperty());EXPECT_EQ("B",host->getHostProperty()->getGamertagProperty());
        std::printf("HND_PORT %u\n",ENetBackend::GetBoundPort(host.get()));std::fflush(stdout);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(4);
        bool sawJoin=false;
        while(std::chrono::steady_clock::now()<deadline){host->Update();sawJoin|=host->getRemoteGamersProperty().getCountProperty()==1;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        EXPECT_TRUE(sawJoin);
        return;
    }
    int pipeFds[2];ASSERT_EQ(0,pipe(pipeFds));Child child;child.output=pipeFds[0];
    posix_spawn_file_actions_t actions;ASSERT_EQ(0,posix_spawn_file_actions_init(&actions));
    posix_spawn_file_actions_adddup2(&actions,pipeFds[1],STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions,pipeFds[1],STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions,pipeFds[0]);posix_spawn_file_actions_addclose(&actions,pipeFds[1]);
    std::vector<std::string> environment;
    for(char** e=environ;*e;++e)environment.emplace_back(*e);
    environment.emplace_back("CNA_HND_NET_SUCCESSOR_CHILD=1");std::vector<char*> env;
    for(auto& entry:environment)env.push_back(entry.data());env.push_back(nullptr);
    char executable[]="/proc/self/exe";
    char filter[]="--gtest_filter=NetworkSessionDepartureJoinTest.LaterEndJoinCompletesAgainstTheSameHostMachine";
    char* argv[]={executable,filter,nullptr};
    const int spawned=posix_spawn(&child.pid,executable,&actions,nullptr,argv,env.data());
    posix_spawn_file_actions_destroy(&actions);close(pipeFds[1]);ASSERT_EQ(0,spawned);
    std::string output;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    while(output.find("HND_PORT ")==std::string::npos&&std::chrono::steady_clock::now()<deadline){
        pollfd descriptor{child.output,POLLIN,0};if(poll(&descriptor,1,100)<=0)continue;
        char bytes[1024];const auto size=read(child.output,bytes,sizeof(bytes));if(size<=0)break;output.append(bytes,static_cast<std::size_t>(size));
    }
    const auto marker=output.find("HND_PORT ");ASSERT_NE(std::string::npos,marker)<<output;
    // Complete the line if the pipe split the decimal port across reads.
    while(output.find('\n',marker)==std::string::npos){char bytes[256];const auto size=read(child.output,bytes,sizeof(bytes));ASSERT_GT(size,0);output.append(bytes,static_cast<std::size_t>(size));}
    const auto port=static_cast<uint16_t>(std::stoi(output.substr(marker+9)));ASSERT_GT(port,0);
    auto local=SignedInGamer::CreateInternal("Later");
    Gamer::setSignedInGamersProperty(new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({&local})));
    struct Restore {~Restore(){Gamer::setSignedInGamersProperty(new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({})));}} restore;
    const auto listing=AvailableNetworkSession::CreateInternal(1,"B",0,7,{},QualityOfService::CreateInternal(),"127.0.0.1",port);
    std::unique_ptr<System::IAsyncResult> pending(NetworkSession::BeginJoin(&listing,{},{}));
    std::unique_ptr<NetworkSession> joined(NetworkSession::EndJoin(pending.get()));
    ASSERT_NE(nullptr,joined);ASSERT_NE(nullptr,joined->getHostProperty());
    EXPECT_EQ("B",joined->getHostProperty()->getGamertagProperty());EXPECT_FALSE(joined->getIsHostProperty());
    EXPECT_EQ(2,joined->getAllGamersProperty().getCountProperty());joined.reset();
    child.wait();EXPECT_TRUE(WIFEXITED(child.status));EXPECT_EQ(0,WEXITSTATUS(child.status))<<output;
}
#endif

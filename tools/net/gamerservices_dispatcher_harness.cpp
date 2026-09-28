// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include "CNA/GamerServices/Configuration.hpp"
#include "System/IServiceProvider.hpp"
#include "System/InvalidOperationException.hpp"
#include <iostream>
using namespace Microsoft::Xna::Framework::GamerServices;
class NullServiceProvider : public System::IServiceProvider {
public: void* GetService(const std::type_info&) const override {return nullptr;}
};
int main(int argc,char** argv) {
    try {
        CNA::GamerServices::setConfigurationOverride(CNA::GamerServices::Configuration{});
        NullServiceProvider services;int events=0;
        auto token=SignedInGamer::SignedIn.Add([&](auto*,const auto&){++events;});
        GamerServicesDispatcher::Initialize(services);
        SignedInGamer::SignedIn.Remove(token);
        const std::string mode=argc==2?argv[1]:"--mode=network-session";
        if(mode=="--mode=initialize-population-check") {
            if(Gamer::getSignedInGamersProperty()->getCountProperty()!=0||events!=0)return 2;
        } else if(mode=="--mode=initialize-leak-check") {
            bool refused=false;try{GamerServicesDispatcher::Initialize(services);}catch(const System::InvalidOperationException&){refused=true;}
            if(!refused||GamerServicesDispatcher::GetFreedGamerCountForTesting()!=0)return 2;
        } else if(mode=="--mode=get-achievements") {
            auto gamer=SignedInGamer::CreateInternal("HarnessPlayer");if(gamer.GetAchievements().getCountProperty()!=0)return 2;
        } else if(mode=="--mode=network-session") {
            using namespace Microsoft::Xna::Framework::Net;
            auto gamer=SignedInGamer::CreateInternal("HarnessPlayer");
            auto* session=NetworkSession::Create(NetworkSessionType::Local,std::vector<SignedInGamer*>{&gamer},8,0,NetworkSessionProperties{});
            if(!session||session->getSessionStateProperty()!=NetworkSessionState::Lobby)return 2;session->Dispose();delete session;
        } else return 64;
        return 0;
    } catch(...) {std::cerr<<"Dispatcher regression failed\n";return 2;}
}

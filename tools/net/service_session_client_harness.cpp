// SPDX-License-Identifier: MS-PL
// Standard-API acceptance client: every GamerServices/Net operation below is the public XNA surface.
// Only keyboard entry into the Guide sign-in overlay is simulated, through the CNA text-input hook.
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include "Microsoft/Xna/Framework/Net/PacketReader.hpp"
#include "Microsoft/Xna/Framework/Net/PacketWriter.hpp"
#include "System/IServiceProvider.hpp"
#include "System/NotSupportedException.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <thread>
#include <poll.h>
#include <unistd.h>

using namespace Microsoft::Xna::Framework::GamerServices;
using namespace Microsoft::Xna::Framework::Net;
namespace {
using Clock=std::chrono::steady_clock;
int checks=0;const char* phase="initial";std::string detail;
void check(bool value,const char* reason){++checks;if(!value)throw std::runtime_error(reason);}
class Provider : public System::IServiceProvider {public:void* GetService(const std::type_info&)const override{return nullptr;}};
NetworkSession* session=nullptr;
void enter(const std::string& value) {
    for(unsigned char character:value)Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextInput(character);
    Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextInput(u'\r');
}
template<class Work>void until(Work work,int seconds=20) {
    const auto deadline=Clock::now()+std::chrono::seconds(seconds);
    while(!work()) {
        check(Clock::now()<deadline,"deadline");GamerServicesDispatcher::Update();
        if(session&&!session->getIsDisposedProperty())session->Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}
// A game keeps calling Update every frame; parent barriers must not stop the session pump either.
// Unbuffered so no later boundary line can hide inside a stream buffer.
std::string line() {
    std::string result;
    const auto deadline=Clock::now()+std::chrono::seconds(60);
    while(true) {
        pollfd input{0,POLLIN,0};
        if(poll(&input,1,2)>0) {
            char character=0;check(read(0,&character,1)==1,"parent input");
            if(character=='\n')return result;
            result.push_back(character);check(result.size()<=256,"parent input length");continue;
        }
        check(Clock::now()<deadline,"parent input deadline");GamerServicesDispatcher::Update();
        if(session&&!session->getIsDisposedProperty())session->Update();
    }
}
void command(){check(line()=="continue","parent boundary");}
// Payload identifies its sender and recipient so each receiver can verify the reported sender.
std::vector<SharpRuntime::bytecs> payload(const std::string& from,const std::string& to,std::size_t size) {
    std::vector<SharpRuntime::bytecs> result(size);const auto label=from+">"+to;
    for(std::size_t index=0;index<size;++index)result[index]=static_cast<SharpRuntime::bytecs>(
        index<label.size()?label[index]:(index*31+from.size()*7+to.size())%251);
    return result;
}
}
int main(int argc,char** argv) {
    try {
        check(argc==3,"arguments");const std::string role=argv[1],kind=argv[2];
        check(role=="host"||role=="join","role");check(kind=="player"||kind=="ranked","kind");
        const bool host=role=="host";const auto type=kind=="player"?NetworkSessionType::PlayerMatch:NetworkSessionType::Ranked;
        const std::array<std::string,2> accounts=host?std::array<std::string,2>{"alice","charlie"}:std::array<std::string,2>{"bob","dana"};
        Provider provider;GamerServicesDispatcher::Initialize(provider);auto* signedIn=Gamer::getSignedInGamersProperty();
        check(signedIn->getCountProperty()==0,"no fabricated identity");Guide::ShowSignIn(2,true);phase="sign-in";
        for(int index=0;index<2;++index) {
            const auto password=line();enter(accounts[index]);enter(password);
            until([&]{return signedIn->getCountProperty()==index+1;},15);
        }
        const std::vector<SignedInGamer*> gamers{(*signedIn)[0],(*signedIn)[1]};
        NetworkSessionProperties properties;properties[7]=73;properties[0]=kind=="player"?1:2;
        if(host) {
            phase="create";int callbacks=0;const auto owner=std::this_thread::get_id();
            std::unique_ptr<System::IAsyncResult> result(NetworkSession::BeginCreate(type,gamers,5,1,properties,[&](System::IAsyncResult& value) {
                ++callbacks;check(std::this_thread::get_id()==owner&&value.getIsCompletedProperty(),"create callback on the update owner");
            },std::any{}));
            check(!result->getCompletedSynchronouslyProperty()&&!result->getIsCompletedProperty(),"online create begins pending");
            until([&]{return result->getIsCompletedProperty();});check(callbacks==1,"create callback once");
            session=NetworkSession::EndCreate(result.get());
            check(session->getIsHostProperty()&&session->getHostProperty()==session->getLocalGamersProperty()[0],"host is the first local gamer");
            check(session->getMaxGamersProperty()==5&&session->getPrivateGamerSlotsProperty()==1,"host capacity");
            if(type==NetworkSessionType::Ranked) {
                bool refused=false;try{session->setAllowJoinInProgressProperty(true);}catch(const System::NotSupportedException&){refused=true;}
                check(refused&&!session->getAllowJoinInProgressProperty(),"Ranked refuses join-in-progress");
            }
            std::cout<<"session-created\n"<<std::flush;
        }else {
            phase="find";auto mismatched=properties;mismatched[7]=74;
            check(NetworkSession::Find(type,gamers,mismatched).getCountProperty()==0,"property filter excludes the session");
            auto found=NetworkSession::Find(type,gamers,properties);
            check(found.getCountProperty()==1,"one matching online session");const auto& listing=std::as_const(found)[0];
            check(listing.getHostGamertagProperty()=="Alice"&&listing.getCurrentGamerCountProperty()==2,"listing host and count");
            check(listing.getOpenPrivateGamerSlotsProperty()==1&&listing.getOpenPublicGamerSlotsProperty()==2,"listing slots");
            phase="join";int callbacks=0;
            std::unique_ptr<System::IAsyncResult> result(NetworkSession::BeginJoin(&listing,[&](System::IAsyncResult&){++callbacks;},std::any{}));
            check(!result->getCompletedSynchronouslyProperty(),"online join begins pending");
            until([&]{return result->getIsCompletedProperty();});check(callbacks==1,"join callback once");
            session=NetworkSession::EndJoin(result.get());
            check(!session->getIsHostProperty()&&session->getAllGamersProperty().getCountProperty()==4,"joined complete roster");
            auto* hostGamer=session->getHostProperty();
            check(hostGamer&&!hostGamer->getIsLocalProperty()&&hostGamer->getGamertagProperty()=="Alice"&&hostGamer->getIsHostProperty(),"remote host identity");
            check(session->getSessionPropertiesProperty().getItem(7)==73,"joined host properties");
            std::cout<<"session-joined\n"<<std::flush;
        }
        std::vector<std::string> joined,left;int started=0,ended=0;std::optional<NetworkSessionEndReason> reason;
        session->GamerJoined+=[&](auto*,const GamerJoinedEventArgs& args){joined.push_back(args.getGamerProperty()->getGamertagProperty());};
        session->GamerLeft+=[&](auto*,const GamerLeftEventArgs& args){left.push_back(args.getGamerProperty()->getGamertagProperty());};
        session->GameStarted+=[&](auto*,const GameStartedEventArgs&){++started;};
        session->GameEnded+=[&](auto*,const GameEndedEventArgs&){++ended;};
        session->SessionEnded+=[&](auto*,const NetworkSessionEndedEventArgs& args){reason=args.getEndReasonProperty();};
        phase="roster";until([&]{return joined.size()==4;});
        check(std::set<std::string>(joined.begin(),joined.end())==std::set<std::string>{"Alice","Bob","Charlie","Dana"},"GamerJoined for every gamer once");
        const auto& locals=session->getLocalGamersProperty();const auto& remotes=session->getRemoteGamersProperty();
        check(locals.getCountProperty()==2&&remotes.getCountProperty()==2,"two local and two remote gamers");
        check(&locals[0]->getMachineProperty()==&locals[1]->getMachineProperty()&&&remotes[0]->getMachineProperty()==&remotes[1]->getMachineProperty()
            &&&locals[0]->getMachineProperty()!=&remotes[0]->getMachineProperty(),"shared per-machine views");
        std::set<int> ids;for(auto* gamer:session->getAllGamersProperty())ids.insert(gamer->getIdProperty());check(ids.size()==4,"distinct gamer ids");
        for(auto* gamer:session->getAllGamersProperty())check(session->FindGamerById(gamer->getIdProperty())==gamer,"FindGamerById");
        std::cout<<"session-roster\n"<<std::flush;command();

        phase="exchange";
        // Every local gamer sends one reliable packet to each remote gamer; the first also sends 32KiB
        // through PacketWriter and the second a small in-order unreliable packet.
        for(auto* local:locals)for(auto* remote:remotes)
            local->SendData(payload(local->getGamertagProperty(),remote->getGamertagProperty(),24),SendDataOptions::Reliable,remote);
        {PacketWriter writer;const auto big=payload(locals[0]->getGamertagProperty(),remotes[0]->getGamertagProperty(),32768);
            for(auto byte:big)writer.Write(static_cast<SharpRuntime::bytecs>(byte));
            locals[0]->SendData(writer,SendDataOptions::ReliableInOrder,remotes[0]);}
        locals[1]->SendData(payload(locals[1]->getGamertagProperty(),remotes[1]->getGamertagProperty(),9),SendDataOptions::InOrder,remotes[1]);
        std::map<std::string,int> received;
        until([&] {
            for(auto* local:locals)while(local->getIsDataAvailableProperty()) {
                std::vector<SharpRuntime::bytecs> buffer(40000);NetworkGamer* sender=nullptr;
                const int length=local->ReceiveData(buffer,sender);buffer.resize(static_cast<std::size_t>(length));
                check(sender&&!sender->getIsLocalProperty(),"remote sender identity");
                const auto expected=payload(sender->getGamertagProperty(),local->getGamertagProperty(),buffer.size());
                check(buffer==expected,"unaltered payload with its true sender");
                ++received[sender->getGamertagProperty()+">"+local->getGamertagProperty()+":"+std::to_string(buffer.size())];
            }
            return received.size()==6;
        });
        for(const auto& [key,count]:received)check(count==1,"each packet delivered once");
        std::cout<<"session-exchanged "<<received.size()<<"\n"<<std::flush;command();

        phase="state";
        if(host) {
            session->getSessionPropertiesProperty()[1]=11;
            if(type==NetworkSessionType::PlayerMatch)session->setAllowJoinInProgressProperty(true);
            session->StartGame();
        }
        until([&]{detail="started="+std::to_string(started)+" state="+std::to_string(static_cast<int>(session->getSessionStateProperty()))
            +" property="+std::to_string(session->getSessionPropertiesProperty().getItem(1).value_or(-1))
            +" jip="+std::to_string(session->getAllowJoinInProgressProperty())
            +" reason="+(reason?std::to_string(static_cast<int>(*reason)):std::string("none"));
            return started==1&&session->getSessionStateProperty()==NetworkSessionState::Playing
            &&session->getSessionPropertiesProperty().getItem(1)==11
            &&session->getAllowJoinInProgressProperty()==(type==NetworkSessionType::PlayerMatch);});
        std::cout<<"session-playing\n"<<std::flush;command();
        if(host)session->EndGame();
        until([&]{return ended==1&&session->getSessionStateProperty()==NetworkSessionState::Lobby;});
        std::cout<<"session-lobby\n"<<std::flush;command();

        phase="departure";
        const bool leaveFirst=(type==NetworkSessionType::PlayerMatch)!=host;
        if(leaveFirst) {
            session->Dispose();check(session->getIsDisposedProperty(),"disposed session");
        }else if(host) {
            until([&]{return left.size()==2;});
            check(std::set<std::string>(left.begin(),left.end())==std::set<std::string>{"Bob","Dana"},"complete remote group departure");
            check(session->getPreviousGamersProperty().getCountProperty()==2&&session->getRemoteGamersProperty().getCountProperty()==0,"previous gamers");
            for(auto* gamer:session->getPreviousGamersProperty())check(gamer->getHasLeftSessionProperty(),"departed gamers flagged");
            session->Dispose();
        }else {
            until([&]{return reason.has_value();});
            check(*reason==NetworkSessionEndReason::HostEndedSession,"host departure ends the session");
            check(session->getSessionStateProperty()==NetworkSessionState::Ended,"ended state");
            session->Dispose();
        }
        delete session;session=nullptr;
        std::cout<<"session-done "<<checks<<" checks\n"<<std::flush;return 0;
    }catch(const std::exception& error){std::cerr<<"CNA public session probe failed at "<<phase<<": "<<error.what()<<" "<<detail<<"\n";return 1;}
}

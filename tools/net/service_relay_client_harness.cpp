// SPDX-License-Identifier: MS-PL
#include "../../modules/net/src/Internal/RelayTransport.hpp"
#include "CNA/Internal/Net/ENetLibrary.hpp"
#include "CNA/Internal/Net/ENetHostHandle.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "System/IServiceProvider.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <set>
#include <thread>

namespace Service=CNA::Internal::GamerServices;
namespace Transport=CNA::Internal::Net;
using namespace Microsoft::Xna::Framework::GamerServices;
namespace {
using Clock=std::chrono::steady_clock;
int checks=0;const char* phase="initial";
void check(bool value,const char* reason){++checks;if(!value)throw std::runtime_error(reason);}
void command(){std::string line;check(static_cast<bool>(std::getline(std::cin,line))&&line=="continue","parent boundary");}
class Provider : public System::IServiceProvider {public:void* GetService(const std::type_info&)const override{return nullptr;}};
void enter(const std::string& value) {
    for(unsigned char character:value)Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextInput(character);
    Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextInput(u'\r');
}
template<class Work>void until(Work work,int seconds=12) {
    const auto deadline=Clock::now()+std::chrono::seconds(seconds);
    while(!work()) {check(Clock::now()<deadline,"deadline");GamerServicesDispatcher::Update();std::this_thread::sleep_for(std::chrono::milliseconds(2));}
}
struct NativeHost {
    std::unique_ptr<ENetHost,decltype(&enet_host_destroy)> host{nullptr,enet_host_destroy};
    NativeHost() {
        Transport::ENetLibrary::EnsureInitialized();ENetAddress address{};
        check(enet_address_set_host_ip(&address,"127.0.0.1")==0,"loopback address");
        host.reset(enet_host_create(&address,4,2,0,0));check(static_cast<bool>(host),"native host");
    }
};
std::vector<unsigned char> payload(int index,bool host) {
    const std::array<std::size_t,4> sizes{1,1400,32768,13};std::vector<unsigned char> result(sizes[index]+1);
    result[0]=static_cast<unsigned char>(index);
    for(std::size_t offset=1;offset<result.size();++offset)result[offset]=static_cast<unsigned char>((offset*17+index+(host?11:37))%256);
    return result;
}
void exchange(NativeHost& native,Transport::RelayTransport& bridge,const std::string& remote,bool host) {
    ENetPeer* connected=nullptr;
    if(!host) {
        ENetAddress destination{};check(enet_address_set_host_ip(&destination,"127.0.0.1")==0,"relay address");
        destination.port=bridge.routePort(remote);check(destination.port!=0,"authorized route");
        check(enet_host_connect(native.host.get(),&destination,2,0)!=nullptr,"connect native ENet");
    }
    std::set<int> received;
    until([&] {
        check(bridge.status().state==Transport::RelayTransportState::Ready,"relay remained ready");
        ENetEvent event{};
        while(enet_host_service(native.host.get(),&event,0)>0) {
            if(event.type==ENET_EVENT_TYPE_CONNECT) {
                check(connected==nullptr,"one authenticated peer");connected=event.peer;
                for(int index=0;index<4;++index) {
                    const auto bytes=payload(index,host);
                    auto* packet=enet_packet_create(bytes.data(),bytes.size(),index==3?0:ENET_PACKET_FLAG_RELIABLE);
                    check(packet!=nullptr,"packet allocation");
                    if(enet_peer_send(connected,index==3?1:0,packet)!=0){enet_packet_destroy(packet);check(false,"packet queue");}
                    ++checks;
                }
                enet_host_flush(native.host.get());
            }else if(event.type==ENET_EVENT_TYPE_RECEIVE) {
                std::unique_ptr<ENetPacket,decltype(&enet_packet_destroy)> packet(event.packet,enet_packet_destroy);
                check(packet->dataLength>0,"packet length");const int index=packet->data[0];check(index>=0&&index<4,"packet index");
                const auto expected=payload(index,!host);
                check(packet->dataLength==expected.size()&&std::equal(expected.begin(),expected.end(),packet->data),"unaltered game payload");
                check(event.channelID==(index==3?1:0),"ENet channel");check(received.insert(index).second,"duplicate application packet");
            }else if(event.type==ENET_EVENT_TYPE_DISCONNECT)check(false,"unexpected ENet disconnect");
        }
        return received.size()==4;
    });
    const auto status=bridge.status();check(status.sent>0&&status.received>0&&status.queued<=64,"bounded bidirectional relay traffic");
}
}
int main(int argc,char** argv) {
    try {
        check(argc==3,"arguments");const std::string role=argv[1],kind=argv[2];NativeHost native;
        const auto configuration=CNA::GamerServices::resolveConfiguration();
        if(role=="refusal") {
            phase="tls-refusal";
            Service::ServiceRelayTicket fake{std::string(64,'a'),std::string(32,'1'),std::string(32,'2'),1,61};
            Transport::RelayTransport bridge(configuration,std::move(fake),native.host->address.port,{});
            const auto deadline=Clock::now()+std::chrono::seconds(8);
            while(bridge.status().state==Transport::RelayTransportState::Connecting&&Clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(2));
            const auto status=bridge.status();check(status.state==Transport::RelayTransportState::Failed&&status.error=="RELAY_TRANSPORT_UNAVAILABLE","verified TLS refusal");
            std::cout<<"relay-refused "<<checks<<" checks\n"<<std::flush;return 0;
        }
        check(role=="host"||role=="join","role");check(kind=="player"||kind=="ranked","kind");const bool host=role=="host";
        const auto category=kind=="player"?Service::ServiceSessionKind::PlayerMatch:Service::ServiceSessionKind::Ranked;
        const std::array<std::string,2> accounts=host?std::array<std::string,2>{"alice","charlie"}:std::array<std::string,2>{"bob","dana"};
        Provider provider;GamerServicesDispatcher::Initialize(provider);auto* signedIn=Gamer::getSignedInGamersProperty();
        check(signedIn->getCountProperty()==0,"no fabricated identity");Guide::ShowSignIn(2,true);phase="sign-in";
        for(int index=0;index<2;++index) {
            std::string password;check(static_cast<bool>(std::getline(std::cin,password)),"credential input");enter(accounts[index]);enter(password);
            until([&]{return signedIn->getCountProperty()==index+1;},15);
        }
        auto backend=Service::backend();auto& directory=backend->sessionDirectory();std::vector<std::string> users;
        for(int index=0;index<2;++index)users.push_back(backend->profile((*signedIn)[index]->getGamertagProperty()).userId);
        Service::ServiceSessionSnapshot session;
        if(host) {
            phase="create";Service::ServiceSessionSettings settings;settings.maxGamers=4;settings.properties[7]=73;
            session=directory.create(users[0],users,category,settings);
            std::cout<<"relay-host "<<session.session<<' '<<session.machine<<'\n'<<std::flush;
        }else {
            phase="join";std::string id;check(static_cast<bool>(std::getline(std::cin,id)),"directory session");
            session=directory.join(users[0],users,id);std::cout<<"relay-join "<<session.machine<<'\n'<<std::flush;
        }
        std::string remote;
        if(host) {
            check(static_cast<bool>(std::getline(std::cin,remote)),"directory remote machine");session=directory.get(users[0],session.session);
        }else remote=session.hostMachine;
        check(std::count_if(session.members.begin(),session.members.end(),[&](const auto& member){return member.machine==remote;})==2,"authoritative two-user remote roster");
        phase="relay-connect";Transport::RelayTransport bridge(configuration,directory.issueRelayTicket(users[0],users,session.session),native.host->address.port,{remote});
        until([&]{const auto state=bridge.status().state;check(state!=Transport::RelayTransportState::Failed,"relay connect failure");return state==Transport::RelayTransportState::Ready;});
        const auto port=bridge.routePort(remote);bridge.setRoutes({remote});check(bridge.routePort(remote)==port,"stable route port");
        phase="udp-guards";
        ENetAddress guardDestination{};check(enet_address_set_host_ip(&guardDestination,"127.0.0.1")==0,"guard address");guardDestination.port=port;
        ENetSocket foreign=enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);check(foreign!=ENET_SOCKET_NULL,"foreign local sender");
        std::array<unsigned char,4097> oversized{};ENetBuffer guardBuffer{};guardBuffer.data=oversized.data();guardBuffer.dataLength=1;
        const auto before=bridge.status().dropped;
        const auto foreignSent=enet_socket_send(foreign,&guardDestination,&guardBuffer,1);enet_socket_destroy(foreign);
        check(foreignSent==1,"foreign UDP fixture");guardBuffer.dataLength=oversized.size();
        check(enet_socket_send(native.host->socket,&guardDestination,&guardBuffer,1)==4097,"oversized local UDP fixture");
        until([&]{return bridge.status().dropped>=before+2;});
        std::cout<<"relay-ready\n"<<std::flush;command();phase="exchange";exchange(native,bridge,remote,host);
        std::cout<<"relay-exchanged\n"<<std::flush;command();phase="failure";
        if(host&&kind=="player") {
            check(bridge.status().state==Transport::RelayTransportState::Ready,"secondary peer revocation preserves host authority");
            std::cout<<"relay-still-open\n"<<std::flush;command();
        }
        until([&]{return bridge.status().state==Transport::RelayTransportState::Failed;},10);
        check(!bridge.status().error.empty(),"safe update-boundary failure");bridge.stop();check(bridge.status().state==Transport::RelayTransportState::Stopped,"joined worker");
        std::cout<<"relay-done "<<checks<<" checks\n"<<std::flush;return 0;
    }catch(const std::exception&){std::cerr<<"CNA relay probe failed at "<<phase<<"\n";return 1;}
}

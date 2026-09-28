// SPDX-License-Identifier: MS-PL
#include "../../modules/net/src/Internal/RelayTransport.hpp"
#include "../../modules/net/src/Internal/ServiceRoster.hpp"
#include "../../modules/net/src/Internal/RelayEnetPolicy.hpp"
#include "CnaService/Protocol.hpp"
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
#include <cstring>

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
        host->maximumPacketSize=Transport::MaxRelayGamePacketBytes;host->maximumWaitingData=Transport::MaxRelayWaitingBytes;
    }
};
std::vector<unsigned char> payload(int index,bool host) {
    const std::array<std::size_t,4> sizes{1,1400,32768,13};std::vector<unsigned char> result(sizes[index]+1);
    result[0]=static_cast<unsigned char>(index);
    for(std::size_t offset=1;offset<result.size();++offset)result[offset]=static_cast<unsigned char>((offset*17+index+(host?11:37))%256);
    return result;
}
void exchange(NativeHost& native,Transport::RelayTransport& bridge,const std::string& remote,bool host,
    const Transport::ServiceRoster& authority,const std::string& machine,const std::vector<std::string>& locals,
    const std::vector<std::string>& remotes) {
    const auto localIds=authority.idsFor(machine,locals),remoteIds=authority.idsFor(remote,remotes);
    ENetProtocolSendFragment malformed{};malformed.header.command=ENET_PROTOCOL_COMMAND_SEND_FRAGMENT;
    malformed.dataLength=ENET_HOST_TO_NET_16(1);malformed.fragmentCount=ENET_HOST_TO_NET_32(1048576);
    malformed.totalLength=ENET_HOST_TO_NET_32(1);
    std::array<unsigned char,sizeof(enet_uint16)+sizeof(malformed)+1> malformedBytes{};
    std::memcpy(malformedBytes.data()+sizeof(enet_uint16),&malformed,sizeof(malformed));
    ENetAddress malformedDestination{};check(enet_address_set_host_ip(&malformedDestination,"127.0.0.1")==0,"malformed route address");
    malformedDestination.port=bridge.routePort(remote);ENetBuffer malformedBuffer{};
    malformedBuffer.data=malformedBytes.data();malformedBuffer.dataLength=malformedBytes.size();
    check(enet_socket_send(native.host->socket,&malformedDestination,&malformedBuffer,1)==static_cast<int>(malformedBytes.size()),"untrusted fragment fixture");
    ENetPeer* connected=nullptr;
    bool handshaked=false,applicationSent=false;int rejectedClaims=0;
    const auto send=[&](const std::vector<unsigned char>& bytes,int channel,bool reliable) {
        auto* packet=enet_packet_create(bytes.data(),bytes.size(),reliable?ENET_PACKET_FLAG_RELIABLE:0);
        check(packet!=nullptr,"packet allocation");
        if(enet_peer_send(connected,static_cast<enet_uint8>(channel),packet)!=0){enet_packet_destroy(packet);check(false,"packet queue");}
        ++checks;
    };
    const auto application=[&] {
        check(!applicationSent,"single application batch");applicationSent=true;
        for(int index=0;index<4;++index) {
            const auto options=index==3?Microsoft::Xna::Framework::Net::SendDataOptions::InOrder:Microsoft::Xna::Framework::Net::SendDataOptions::ReliableInOrder;
            send(Transport::NetPacketCodec::Encode(Transport::AppDataMessage{localIds[0],remoteIds[0],options,payload(index,host)}),index==3?1:0,index!=3);
        }
        enet_host_flush(native.host.get());
    };
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
                if(!host) {
                    auto forged=locals;forged[0]="Alice";
                    send(Transport::NetPacketCodec::Encode(Transport::ClientHelloMessage{forged}),0,true);
                    send(Transport::NetPacketCodec::Encode(Transport::ClientHelloMessage{locals}),0,true);
                    enet_host_flush(native.host.get());
                }
            }else if(event.type==ENET_EVENT_TYPE_RECEIVE) {
                std::unique_ptr<ENetPacket,decltype(&enet_packet_destroy)> packet(event.packet,enet_packet_destroy);
                check(packet->dataLength>0,"packet length");
                check(event.peer==connected&&event.peer->address.port==bridge.routePort(remote),"authenticated service source route");
                const auto tag=static_cast<Transport::MessageTag>(packet->data[0]);
                if(tag!=Transport::MessageTag::AppData)Transport::validateServiceControlPacket(std::span(packet->data,packet->dataLength));
                else check(packet->dataLength<=65536,"bounded native test application packet");
                std::vector<unsigned char> bytes(packet->data,packet->data+packet->dataLength);
                if(tag==Transport::MessageTag::ClientHello) {
                    check(host&&!handshaked,"host handshake direction");
                    const auto hello=Transport::NetPacketCodec::DecodeClientHello(bytes);
                    try{(void)authority.idsFor(remote,hello.LocalGamertags);}
                    catch(const CnaService::Error& error){check(error.code()=="INVALID_SERVICE_ROSTER","forged claim refused before mutation");++rejectedClaims;continue;}
                    send(Transport::NetPacketCodec::Encode(authority.welcomeFor(remote,hello.LocalGamertags)),0,true);
                    handshaked=true;application();continue;
                }
                if(tag==Transport::MessageTag::ServerWelcome) {
                    check(!host&&!handshaked,"client handshake direction");
                    authority.validateWelcome(Transport::NetPacketCodec::DecodeServerWelcome(bytes),locals);
                    handshaked=true;application();continue;
                }
                check(tag==Transport::MessageTag::AppData,"application packet tag");const auto message=Transport::NetPacketCodec::DecodeAppData(bytes);
                check(message.SenderWireId==remoteIds[0]&&message.TargetWireId==localIds[0],"authority-bound application IDs");
                check(!message.Payload.empty(),"application payload length");const int index=message.Payload[0];check(index>=0&&index<4,"packet index");
                const auto expected=payload(index,!host);
                check(message.Payload==expected,"unaltered game payload");
                check(event.channelID==(index==3?1:0),"ENet channel");check(received.insert(index).second,"duplicate application packet");
            }else if(event.type==ENET_EVENT_TYPE_DISCONNECT)check(false,"unexpected ENet disconnect");
        }
        return received.size()==4&&handshaked&&bridge.status().rejectedEnet>=1;
    });
    check(!host||rejectedClaims==1,"cross-machine gamertag spoof test");
    check(bridge.status().rejectedEnet>=1,"malformed remote fragment refused before ENet allocation");
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
        std::vector<std::string> locals,remotes;
        for(int index=0;index<2;++index)locals.push_back((*signedIn)[index]->getGamertagProperty());
        for(const auto& member:session.members)if(member.machine==remote)remotes.push_back(member.gamertag);
        const Transport::ServiceRoster authority(session);
        std::cout<<"relay-ready\n"<<std::flush;command();phase="exchange";exchange(native,bridge,remote,host,authority,session.machine,locals,remotes);
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

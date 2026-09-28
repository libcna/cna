// SPDX-License-Identifier: MS-PL
#include "../../modules/net/src/Internal/RelayTransport.hpp"
#include "../../modules/net/src/Internal/ServiceRoster.hpp"
#include "../../modules/net/src/Internal/ServiceGamePacketPolicy.hpp"
#include "../../modules/net/src/Internal/ServiceENetSession.hpp"
#include "../../modules/net/src/Internal/RelayEnetPolicy.hpp"
#include "CnaService/Protocol.hpp"
#include "CNA/Internal/Net/ENetLibrary.hpp"
#include "CNA/Internal/Net/ENetHostHandle.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/Internal/GamerServices/BackendConfiguration.hpp"
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
#include <cstdlib>

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
    const Transport::ServiceRoster& authority,const Transport::ServiceGamePacketPolicy& policy,const std::string& machine,const std::vector<std::string>& locals,
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
    bool handshaked=false,applicationSent=false;int rejectedClaims=0,rejectedApplication=0;
    const auto send=[&](const std::vector<unsigned char>& bytes,int channel,bool reliable) {
        auto* packet=enet_packet_create(bytes.data(),bytes.size(),reliable?ENET_PACKET_FLAG_RELIABLE:0);
        check(packet!=nullptr,"packet allocation");
        if(enet_peer_send(connected,static_cast<enet_uint8>(channel),packet)!=0){enet_packet_destroy(packet);check(false,"packet queue");}
        ++checks;
    };
    const auto application=[&] {
        check(!applicationSent,"single application batch");applicationSent=true;
        if(!host) {
            send(Transport::NetPacketCodec::Encode(Transport::AppDataMessage{remoteIds[0],remoteIds[0],Microsoft::Xna::Framework::Net::SendDataOptions::Reliable,{0}}),0,true);
            send(Transport::NetPacketCodec::Encode(Transport::AppDataMessage{localIds[0],31,Microsoft::Xna::Framework::Net::SendDataOptions::Reliable,{0}}),0,true);
        }
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
                std::optional<Transport::ServiceGameControl> control;
                if(tag!=Transport::MessageTag::AppData) {
                    try{control=policy.control(remote,std::span(packet->data,packet->dataLength),event.channelID,handshaked,locals);}
                    catch(const CnaService::Error& error) {
                        if(host&&tag==Transport::MessageTag::ClientHello&&!handshaked&&error.code()=="INVALID_SERVICE_ROSTER") {
                            ++rejectedClaims;continue;
                        }
                        throw;
                    }
                }
                if(tag==Transport::MessageTag::ClientHello) {
                    check(host&&!handshaked,"host handshake direction");
                    const auto& hello=std::get<Transport::ClientHelloMessage>(*control);
                    send(Transport::NetPacketCodec::Encode(authority.welcomeFor(remote,hello.LocalGamertags)),0,true);
                    handshaked=true;application();continue;
                }
                if(tag==Transport::MessageTag::ServerWelcome) {
                    check(!host&&!handshaked,"client handshake direction");
                    check(std::holds_alternative<Transport::ServerWelcomeMessage>(*control),"policy admitted complete welcome");
                    handshaked=true;application();continue;
                }
                check(tag==Transport::MessageTag::AppData,"application packet tag");Transport::AppDataMessage message;
                try{message=policy.application(remote,std::span(packet->data,packet->dataLength),event.channelID,handshaked,{remote});}
                catch(const CnaService::Error& error) {
                    if(host&&handshaked&&error.code()=="INVALID_SERVICE_ROSTER"){++rejectedApplication;continue;}
                    throw;
                }
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
    check(!host||rejectedApplication==2,"forged existing sender and unknown target refused before delivery");
    check(bridge.status().rejectedEnet>=1,"malformed remote fragment refused before ENet allocation");
    const auto status=bridge.status();check(status.sent>0&&status.received>0&&status.queued<=64,"bounded bidirectional relay traffic");
}
void ownedExchange(Transport::ServiceENetSession& engine,bool host,const std::string& remote,
    const std::vector<std::string>& locals,const std::vector<std::string>& remotes) {
    const auto owner=std::this_thread::get_id();
    const Transport::ServiceRoster authority(engine.snapshot());
    const auto localIds=authority.idsFor(engine.snapshot().machine,locals),remoteIds=authority.idsFor(remote,remotes);
    bool readySeen=false,remoteSeen=false,sent=false;std::set<int> received;
    until([&] {
        for(auto& event:engine.update()) {
            check(std::this_thread::get_id()==owner,"owned observations stay on owner");
            check(event.type!=Transport::ServiceENetObservation::Type::Failed,"owned exchange remained available");
            if(event.type==Transport::ServiceENetObservation::Type::Ready) {
                check(!readySeen&&event.ids==localIds,"one exact owned welcome");readySeen=true;
                if(!host){check(event.gamers.size()==2,"complete owned welcome host group");remoteSeen=true;}
            }else if(event.type==Transport::ServiceENetObservation::Type::Joined) {
                check(host&&!remoteSeen&&event.gamers.size()==2,"complete owned remote group join");remoteSeen=true;
            }else if(event.type==Transport::ServiceENetObservation::Type::Data) {
                check(event.data.has_value(),"owned payload observation");const auto& message=*event.data;
                check(!message.Payload.empty(),"owned game payload length");const int index=message.Payload.front();
                check(index>=0&&index<4,"owned packet index");
                check(message.SenderWireId==remoteIds[(index+1)%2]&&message.TargetWireId==localIds[index%2],"both owned local account slots");
                check(message.Payload==payload(index,!host),"owned unaltered game payload");
                check(message.Options==(index==3?Microsoft::Xna::Framework::Net::SendDataOptions::InOrder:Microsoft::Xna::Framework::Net::SendDataOptions::ReliableInOrder),"owned game delivery options");
                check(received.insert(index).second,"owned duplicate payload");
            }
        }
        if(readySeen&&remoteSeen&&!sent) {
            check(engine.ready(),"owned handshake establishment precedes data");
            for(int index=0;index<4;++index)engine.send(localIds[(index+1)%2],remoteIds[index%2],payload(index,host),
                index==3?Microsoft::Xna::Framework::Net::SendDataOptions::InOrder:Microsoft::Xna::Framework::Net::SendDataOptions::ReliableInOrder);
            sent=true;
        }
        return received.size()==4&&sent;
    });
    for(const auto ids:{std::pair{remoteIds[0],localIds[0]},std::pair{localIds[0],static_cast<unsigned char>(31)}}) {
        bool denied=false;try{engine.send(ids.first,ids.second,{0},Microsoft::Xna::Framework::Net::SendDataOptions::Reliable);}
        catch(const Service::ServiceOperationError& error){denied=error.code=="NOT_AUTHORIZED";}
        check(denied,"owned foreign sender/target send refused");
    }
}
}
int main(int argc,char** argv) {
    try {
        check(argc==3,"arguments");const std::string role=argv[1],kind=argv[2];
        const auto* mode=std::getenv("CNA_SERVICE_RELAY_OWNED");const bool owned=mode&&std::string(mode)=="1";
        std::unique_ptr<NativeHost> native;if(!owned||role=="refusal")native=std::make_unique<NativeHost>();
        const auto configuration=CNA::GamerServices::resolveConfiguration();
        if(role=="refusal") {
            phase="tls-refusal";
            Service::ServiceRelayTicket fake{std::string(64,'a'),std::string(32,'1'),std::string(32,'2'),1,61};
            Transport::RelayTransport bridge(configuration,std::move(fake),native->host->address.port,{});
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
        std::vector<std::string> locals,remotes;
        for(int index=0;index<2;++index)locals.push_back((*signedIn)[index]->getGamertagProperty());
        for(const auto& member:session.members)if(member.machine==remote)remotes.push_back(member.gamertag);
        if(owned) {
            phase="owned-preparation";Transport::OnlineSessionRequest request;request.operation=Transport::OnlineSessionRequest::Operation::Join;
            request.owner=users[0];request.users=users;request.kind=category;request.session=session.session;
            Transport::OnlineSessionPreparation pending(backend,std::move(request));
            check(!pending.complete(),"owned preparation begins pending");until([&]{return pending.complete();},20);
            Transport::ServiceENetSession engine(pending.take(),locals);
            check(engine.snapshot().session==session.session&&engine.snapshot().machine==session.machine,"owned membership replay authority");
            check(engine.ready()==host,"client completion waits for verified welcome");
            std::cout<<"relay-ready\n"<<std::flush;command();phase="owned-exchange";ownedExchange(engine,host,remote,locals,remotes);
            std::cout<<"relay-exchanged\n"<<std::flush;command();phase="owned-failure";
            if(host&&kind=="player") {
                bool left=false;until([&] {
                    for(const auto& event:engine.update()) {
                        check(event.type!=Transport::ServiceENetObservation::Type::Failed,"secondary revocation preserves owned host");
                        if(event.type==Transport::ServiceENetObservation::Type::Left){check(!left&&event.ids.size()==2,"owned full remote group departure once");left=true;}
                    }
                    return left;
                });
                check(engine.ready(),"secondary revocation preserves established host");
                std::cout<<"relay-still-open\n"<<std::flush;command();
            }
            bool failed=false;until([&] {
                for(const auto& event:engine.update())if(event.type==Transport::ServiceENetObservation::Type::Failed) {
                    check(!failed&&!event.failure.empty(),"owned safe failure once");failed=true;
                }
                return failed;
            },15);
            check(!engine.ready(),"owned failure closes session readiness");
            check(engine.update().empty(),"owned failure does not repeat");
            std::cout<<"relay-done "<<checks<<" checks\n"<<std::flush;return 0;
        }
        phase="relay-connect";Transport::RelayTransport bridge(Service::configurationForBackend(*backend),directory.issueRelayTicket(users[0],users,session.session),native->host->address.port,{remote});
        until([&]{const auto state=bridge.status().state;check(state!=Transport::RelayTransportState::Failed,"relay connect failure");return state==Transport::RelayTransportState::Ready;});
        const auto port=bridge.routePort(remote);bridge.setRoutes({remote});check(bridge.routePort(remote)==port,"stable route port");
        phase="udp-guards";
        ENetAddress guardDestination{};check(enet_address_set_host_ip(&guardDestination,"127.0.0.1")==0,"guard address");guardDestination.port=port;
        ENetSocket foreign=enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);check(foreign!=ENET_SOCKET_NULL,"foreign local sender");
        std::array<unsigned char,4097> oversized{};ENetBuffer guardBuffer{};guardBuffer.data=oversized.data();guardBuffer.dataLength=1;
        const auto before=bridge.status().dropped;
        const auto foreignSent=enet_socket_send(foreign,&guardDestination,&guardBuffer,1);enet_socket_destroy(foreign);
        check(foreignSent==1,"foreign UDP fixture");guardBuffer.dataLength=oversized.size();
        check(enet_socket_send(native->host->socket,&guardDestination,&guardBuffer,1)==4097,"oversized local UDP fixture");
        until([&]{return bridge.status().dropped>=before+2;});
        const Transport::ServiceRoster authority(session);const Transport::ServiceGamePacketPolicy policy(session);
        std::cout<<"relay-ready\n"<<std::flush;command();phase="exchange";exchange(*native,bridge,remote,host,authority,policy,session.machine,locals,remotes);
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

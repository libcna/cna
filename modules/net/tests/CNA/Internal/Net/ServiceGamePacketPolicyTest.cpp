// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "../../../../src/Internal/ServiceGamePacketPolicy.hpp"
#include "../../../../src/Internal/RelayEnetPolicy.hpp"
#include "CnaService/Protocol.hpp"
#include <random>

namespace {
using namespace CNA::Internal::Net;
namespace Service=CNA::Internal::GamerServices;
const std::string host(32,'1'),remote(32,'2'),third(32,'3'),unknown(32,'4');
Service::ServiceSessionSnapshot fixture(bool client=false) {
    Service::ServiceSessionSnapshot value;value.session=std::string(32,'a');value.machine=client?remote:host;
    value.hostMachine=host;value.hostId="alice";value.hostGamertag="Alice";
    value.maxGamers=8;value.privateSlots=2;value.currentGamers=6;value.openPublicSlots=0;value.openPrivateSlots=2;
    value.members={{"alice","Alice",host,false,0},{"charlie","Charlie",host,false,1},
        {"bob","Bob",remote,false,2},{"dana","Dana",remote,false,3},
        {"eve","Eve",third,false,4},{"frank","Frank",third,false,5}};
    value.properties[0]=-2147483647-1;value.properties[7]=2147483647;return value;
}
template<class Work>void refused(Work work,const char* code="INVALID_SERVICE_ROSTER") {
    try{work();FAIL()<<"Expected packet refusal";}
    catch(const CnaService::Error& error){EXPECT_EQ(code,error.code());EXPECT_EQ(std::string::npos,std::string(error.what()).find("secret-marker"));}
}
std::vector<unsigned char> data(unsigned char sender=3,unsigned char target=1,SendDataOptions options=SendDataOptions::Reliable) {
    return NetPacketCodec::Encode(AppDataMessage{sender,target,options,{1,2,3}});
}
}
TEST(ServiceGamePacketPolicyTest, HostAcceptsOnlyExactSourceLocalHelloBeforeHandshakeMutation) {
    ServiceGamePacketPolicy policy(fixture());
    auto value=policy.control(remote,NetPacketCodec::Encode(ClientHelloMessage{{"Dana","Bob"}}),0,false,{});
    EXPECT_EQ((std::vector<std::string>{"Dana","Bob"}),std::get<ClientHelloMessage>(value).LocalGamertags);
    for(const auto names:{std::vector<std::string>{"Bob"},std::vector<std::string>{"Alice","Dana"},
        std::vector<std::string>{"Bob","Bob"},std::vector<std::string>{"Bob","secret-marker"}})
        refused([&]{(void)policy.control(remote,NetPacketCodec::Encode(ClientHelloMessage{names}),0,false,{});});
    refused([&]{(void)policy.control(third,NetPacketCodec::Encode(ClientHelloMessage{{"Bob","Dana"}}),0,false,{});});
    // GSH-02: online sessions admit no guests; the strict parser refuses the guest block outright.
    refused([&]{(void)policy.control(remote,NetPacketCodec::Encode(ClientHelloMessage{{"Dana","Bob"},{false,true}}),0,false,{});},
        "INVALID_SERVICE_PACKET");
}
TEST(ServiceGamePacketPolicyTest, ClientWelcomeRequiresAuthenticatedHostAndItsExactLocalAssignments) {
    ServiceRoster roster(fixture());ServiceGamePacketPolicy policy(fixture(true));
    const auto bytes=NetPacketCodec::Encode(roster.welcomeFor(remote,{"Bob","Dana"},{third}));
    auto value=policy.control(host,bytes,0,false,{"Bob","Dana"});EXPECT_EQ((std::vector<unsigned char>{3,4}),std::get<ServerWelcomeMessage>(value).AssignedWireIds);
    refused([&]{(void)policy.control(third,bytes,0,false,{"Bob","Dana"});});
    refused([&]{(void)policy.control(host,bytes,0,false,{"Dana","Bob"});});
    refused([&]{(void)ServiceGamePacketPolicy(fixture()).control(remote,bytes,0,true,{});});
}
TEST(ServiceGamePacketPolicyTest, SourceAndControlDirectionCannotBeChosenByThePacket) {
    auto bytes=NetPacketCodec::Encode(ClientHelloMessage{{"Bob","Dana"}});ServiceGamePacketPolicy policy(fixture());
    refused([&]{(void)policy.control(unknown,bytes,0,false,{});});
    refused([&]{(void)policy.control(host,bytes,0,false,{});});
    refused([&]{(void)ServiceGamePacketPolicy(fixture(true)).control(host,bytes,0,true,{"Bob","Dana"});});
    refused([&]{(void)policy.control(remote,bytes,1,false,{});},"INVALID_SERVICE_PACKET");
    bytes.push_back(0);refused([&]{(void)policy.control(remote,bytes,0,false,{});},"INVALID_SERVICE_PACKET");
}
TEST(ServiceGamePacketPolicyTest, CompleteServiceGroupsAreRequiredInHostBroadcasts) {
    ServiceGamePacketPolicy policy(fixture(true));GamerJoinBroadcastMessage join{{{5,"Eve",false},{6,"Frank",false}}};
    auto value=policy.control(host,NetPacketCodec::Encode(join),0,true,{});EXPECT_EQ(2,std::get<GamerJoinBroadcastMessage>(value).NewGamers.size());
    refused([&]{(void)policy.control(host,NetPacketCodec::Encode(join),0,false,{});});
    join.NewGamers.pop_back();refused([&]{(void)policy.control(host,NetPacketCodec::Encode(join),0,true,{});});
    join.NewGamers.push_back({6,"Frank",true});refused([&]{(void)policy.control(host,NetPacketCodec::Encode(join),0,true,{});});
}
TEST(ServiceGamePacketPolicyTest, StateChangesMustMatchTheAuthenticatedDirectory) {
    auto snapshot=fixture(true);ServiceGamePacketPolicy policy(snapshot);
    auto bytes=NetPacketCodec::Encode(StateChangeBroadcastMessage{NetworkSessionState::Lobby});
    EXPECT_EQ(NetworkSessionState::Lobby,std::get<StateChangeBroadcastMessage>(policy.control(host,bytes,0,true,{})).NewState);
    for(auto state:{NetworkSessionState::Playing,NetworkSessionState::Ended})
        refused([&]{(void)policy.control(host,NetPacketCodec::Encode(StateChangeBroadcastMessage{state}),0,true,{});});
    snapshot.state=Service::ServiceSessionState::Playing;
    EXPECT_EQ(NetworkSessionState::Playing,std::get<StateChangeBroadcastMessage>(ServiceGamePacketPolicy(snapshot).control(host,
        NetPacketCodec::Encode(StateChangeBroadcastMessage{NetworkSessionState::Playing}),0,true,{})).NewState);
}
TEST(ServiceGamePacketPolicyTest, PropertiesMustMatchTheCompleteNullableDirectorySnapshot) {
    auto snapshot=fixture(true);ServiceGamePacketPolicy policy(snapshot);NetworkSessionProperties properties;
    for(int index=0;index<8;++index)properties.setItem(index,snapshot.properties[index]);
    auto bytes=NetPacketCodec::Encode(SessionPropertiesBroadcastMessage{properties});
    EXPECT_EQ(-2147483647-1,std::get<SessionPropertiesBroadcastMessage>(policy.control(host,bytes,0,true,{})).SessionProperties.getItem(0));
    properties[0]=12;refused([&]{(void)policy.control(host,NetPacketCodec::Encode(SessionPropertiesBroadcastMessage{properties}),0,true,{});});
}
TEST(ServiceGamePacketPolicyTest, LeaveRequiresACompleteAdmittedGroupAndEndedCannotOverrideDirectory) {
    ServiceGamePacketPolicy policy(fixture(true));
    refused([&]{(void)policy.control(host,NetPacketCodec::Encode(GamerLeaveBroadcastMessage{{5,6}}),0,true,{});});
    refused([&]{(void)policy.control(host,NetPacketCodec::Encode(StateChangeBroadcastMessage{NetworkSessionState::Ended}),0,true,{});});
}
TEST(ServiceGamePacketPolicyTest, HostApplicationRequiresSenderOwnershipAndAdmittedTarget) {
    ServiceGamePacketPolicy policy(fixture());
    auto received=policy.application(remote,data(),0,true,{remote,third});EXPECT_EQ(3,received.SenderWireId);EXPECT_EQ(1,received.TargetWireId);
    EXPECT_EQ((std::vector<unsigned char>{1,2,3}),received.Payload);
    EXPECT_EQ(5,policy.application(remote,data(3,5),1,true,{remote,third}).TargetWireId);
    for(auto sender:{static_cast<unsigned char>(1),static_cast<unsigned char>(5),static_cast<unsigned char>(31),static_cast<unsigned char>(0)})
        refused([&]{(void)policy.application(remote,data(sender),0,true,{remote,third});});
    refused([&]{(void)policy.application(remote,data(3,5),0,true,{remote});});
    refused([&]{(void)policy.application(remote,data(3,31),0,true,{remote,third});});
}
TEST(ServiceGamePacketPolicyTest, DirectoryReservationWithoutRealtimeAdmissionIsNotAConnectedSource) {
    ServiceGamePacketPolicy policy(fixture());
    refused([&]{(void)policy.application(remote,data(),0,false,{remote});});
    refused([&]{(void)policy.application(remote,data(),0,true,{});});
    refused([&]{(void)policy.application(unknown,data(),0,true,{remote});});
    refused([&]{(void)policy.application(host,data(),0,true,{remote});});
    refused([&]{(void)policy.application(remote,data(),0,true,{remote,remote});});
    refused([&]{(void)policy.application(remote,data(),0,true,{remote,unknown});});
}
TEST(ServiceGamePacketPolicyTest, ClientsAcceptHostRelayOnlyForRemoteSendersAndTheirOwnTargets) {
    ServiceGamePacketPolicy policy(fixture(true));
    EXPECT_EQ(1,policy.application(host,data(1,3),0,true,{host,third}).SenderWireId);
    EXPECT_EQ(5,policy.application(host,data(5,4),1,true,{host,third}).SenderWireId);
    refused([&]{(void)policy.application(third,data(5,3),0,true,{host,third});});
    refused([&]{(void)policy.application(host,data(3,4),0,true,{host,third});});
    refused([&]{(void)policy.application(host,data(1,5),0,true,{host,third});});
    refused([&]{(void)policy.application(host,data(5,3),0,true,{host});});
}
TEST(ServiceGamePacketPolicyTest, ApplicationHeaderLengthOptionsAndChannelsAreBoundedBeforePayloadCopy) {
    ServiceGamePacketPolicy policy(fixture());
    auto value=data();for(std::size_t size=0;size<4;++size)
        refused([&]{(void)policy.application(remote,std::span(value).first(size),0,true,{remote});},"INVALID_SERVICE_PACKET");
    value[0]=0x7f;refused([&]{(void)policy.application(remote,value,0,true,{remote});},"INVALID_SERVICE_PACKET");
    value=data();value[3]=5;refused([&]{(void)policy.application(remote,value,0,true,{remote});},"INVALID_SERVICE_PACKET");
    refused([&]{(void)policy.application(remote,data(),2,true,{remote});},"INVALID_SERVICE_PACKET");
    value=data();value.resize(MaxRelayGamePacketBytes+1);
    refused([&]{(void)policy.application(remote,value,0,true,{remote});},"INVALID_SERVICE_PACKET");
    value.resize(MaxRelayGamePacketBytes);EXPECT_EQ(MaxRelayGamePacketBytes-4,policy.application(remote,value,0,true,{remote}).Payload.size());
}
TEST(ServiceGamePacketPolicyTest, EmptyPayloadAndEveryDeliveryOptionRoundTripWithoutInventedFlags) {
    ServiceGamePacketPolicy policy(fixture());
    for(int option=0;option<=4;++option) {
        auto bytes=data(3,1,static_cast<SendDataOptions>(option));bytes.resize(4);
        auto value=policy.application(remote,bytes,static_cast<unsigned char>(option%2),true,{remote});
        EXPECT_TRUE(value.Payload.empty());EXPECT_EQ(option,static_cast<int>(value.Options));EXPECT_EQ(bytes,NetPacketCodec::Encode(value));
    }
}
TEST(ServiceGamePacketPolicyTest, RemovingAuthorityMakesStaleGroupAndIdsUnusable) {
    auto snapshot=fixture();snapshot.members.resize(4);snapshot.currentGamers=4;snapshot.openPublicSlots=2;++snapshot.revision;
    ServiceGamePacketPolicy policy(snapshot);
    refused([&]{(void)policy.application(third,data(5,1),0,true,{remote,third});});
    refused([&]{(void)policy.application(remote,data(3,5),0,true,{remote});});
    EXPECT_EQ(3,policy.application(remote,data(),0,true,{remote}).SenderWireId);
}
TEST(ServiceGamePacketPolicyTest, TenThousandMutationsRejectSafelyOrRoundTripAdmittedApplicationBytes) {
    ServiceGamePacketPolicy policy(fixture());std::mt19937 random(48151);auto seed=data();int accepted=0,rejected=0;
    for(int index=0;index<10000;++index) {
        auto bytes=seed;bytes.resize(static_cast<std::size_t>(random()%48));
        for(auto& byte:bytes)if(random()%3==0)byte=static_cast<unsigned char>(random());
        try {auto message=policy.application(remote,bytes,static_cast<unsigned char>(random()%4),true,{remote,third});
            EXPECT_EQ(bytes,NetPacketCodec::Encode(message));++accepted;}
        catch(const CnaService::Error& error){EXPECT_TRUE(error.code()=="INVALID_SERVICE_ROSTER"||error.code()=="INVALID_SERVICE_PACKET");++rejected;}
    }
    EXPECT_GT(accepted,0);EXPECT_GT(rejected,0);
}
TEST(ServiceGamePacketPolicyTest, AllThirtyOneIdsRemainUsableAndOversizedAdmissionSetsAreRefused) {
    auto snapshot=fixture();snapshot.maxGamers=31;snapshot.currentGamers=31;snapshot.privateSlots=0;
    snapshot.openPrivateSlots=0;snapshot.openPublicSlots=0;snapshot.members.clear();snapshot.hostId="u0";snapshot.hostGamertag="Gamer0";
    for(int index=0;index<31;++index)snapshot.members.push_back({"u"+std::to_string(index),"Gamer"+std::to_string(index),
        std::string(32,static_cast<char>('1'+index/4)),false,index});
    const auto last=snapshot.members.back().machine;ServiceGamePacketPolicy policy(snapshot);
    EXPECT_EQ(31,policy.application(last,data(31,1),0,true,{last}).SenderWireId);
    refused([&]{(void)policy.application(last,data(31,1),0,true,std::vector<std::string>(31,last));});
    snapshot.machine=last;
    EXPECT_EQ(31,ServiceGamePacketPolicy(snapshot).application(host,data(1,31),1,true,{host}).TargetWireId);
}
TEST(ServiceGamePacketPolicyTest, TenThousandControlMutationsPreserveCanonicalAdmittedBytes) {
    ServiceGamePacketPolicy server(fixture()),client(fixture(true));ServiceRoster roster(fixture());
    const auto hello=NetPacketCodec::Encode(ClientHelloMessage{{"Bob","Dana"}});
    const auto welcome=NetPacketCodec::Encode(roster.welcomeFor(remote,{"Bob","Dana"},{third}));
    std::mt19937 random(7718);int accepted=0,rejected=0;
    for(int index=0;index<10000;++index) {
        const bool hosting=index%2==0;auto bytes=hosting?hello:welcome;
        for(auto& byte:bytes)if(random()%19==0)byte=static_cast<unsigned char>(random());
        if(random()%4==0)bytes.resize(static_cast<std::size_t>(random()%(bytes.size()+4)));
        try {
            auto message=hosting?server.control(remote,bytes,0,true,{"Alice","Charlie"})
                :client.control(host,bytes,0,true,{"Bob","Dana"});
            EXPECT_EQ(bytes,std::visit([](const auto& value){return NetPacketCodec::Encode(value);},message));++accepted;
        }catch(const CnaService::Error& error){EXPECT_TRUE(error.code()=="INVALID_SERVICE_ROSTER"||error.code()=="INVALID_SERVICE_PACKET");++rejected;}
    }
    EXPECT_GT(accepted,0);EXPECT_GT(rejected,0);
}

TEST(ServiceGamePacketPolicyTest, HostLeaveBroadcastRemovesOnlyCompleteAdmittedRemoteGroups) {
    ServiceGamePacketPolicy policy(fixture(true));
    auto message=policy.control(host,NetPacketCodec::Encode(GamerLeaveBroadcastMessage{{5,6}}),0,true,{}, {host,third});
    EXPECT_EQ((std::vector<unsigned char>{5,6}),std::get<GamerLeaveBroadcastMessage>(message).WireIds);
    for(const auto ids:{std::vector<unsigned char>{5},std::vector<unsigned char>{5,5},std::vector<unsigned char>{1,2},
        std::vector<unsigned char>{3,4},std::vector<unsigned char>{5,31}})
        refused([&]{(void)policy.control(host,NetPacketCodec::Encode(GamerLeaveBroadcastMessage{ids}),0,true,{}, {host,third});});
    refused([&]{(void)policy.control(third,NetPacketCodec::Encode(GamerLeaveBroadcastMessage{{5,6}}),0,true,{}, {host,third});});
}
TEST(ServiceGamePacketPolicyTest, VoiceFramesFollowGameDataAuthorityOnTheUnreliableChannelOnly) {
    const auto voice=[](unsigned char sender,unsigned char target){
        return NetPacketCodec::Encode(VoiceDataMessage{sender,target,VoiceFlagTalking,12,{5,6,7}});
    };
    ServiceGamePacketPolicy hostPolicy(fixture());
    const auto frame=hostPolicy.voice(remote,voice(3,1),1,true,{remote,third});
    EXPECT_EQ(3,frame.SenderWireId);EXPECT_EQ(1,frame.TargetWireId);EXPECT_EQ(12,frame.Sequence);
    EXPECT_EQ(5,hostPolicy.voice(remote,voice(3,5),1,true,{remote,third}).TargetWireId);
    // Only the sender's own machine, only once admitted, only on channel 1.
    refused([&]{(void)hostPolicy.voice(third,voice(3,1),1,true,{remote,third});});
    refused([&]{(void)hostPolicy.voice(remote,voice(3,1),1,false,{remote,third});});
    refused([&]{(void)hostPolicy.voice(remote,voice(3,1),0,true,{remote,third});},"INVALID_SERVICE_PACKET");
    auto flags=voice(3,1);flags[3]=0x02;
    refused([&]{(void)hostPolicy.voice(remote,flags,1,true,{remote,third});},"INVALID_SERVICE_PACKET");
    // A client takes voice only from the host, for its own gamers, from another admitted machine.
    ServiceGamePacketPolicy clientPolicy(fixture(true));
    EXPECT_EQ(3,clientPolicy.voice(host,voice(1,3),1,true,{host,third}).TargetWireId);
    EXPECT_EQ(5,clientPolicy.voice(host,voice(5,4),1,true,{host,third}).SenderWireId);
    refused([&]{(void)clientPolicy.voice(host,voice(3,4),1,true,{host,third});});
    refused([&]{(void)clientPolicy.voice(host,voice(1,5),1,true,{host,third});});
    refused([&]{(void)clientPolicy.voice(third,voice(5,3),1,true,{host,third});});
}

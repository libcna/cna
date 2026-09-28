// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "../../../../../src/Internal/ServiceRoster.hpp"
#include "CnaService/Protocol.hpp"
#include <algorithm>
#include <random>

namespace {
using namespace CNA::Internal::Net;
namespace Service=CNA::Internal::GamerServices;
const std::string host(32,'1'),remote(32,'2'),third(32,'3');
Service::ServiceSessionSnapshot fixture(bool client=false) {
    Service::ServiceSessionSnapshot value;value.session=std::string(32,'a');value.machine=client?remote:host;
    value.hostMachine=host;value.hostId="alice";value.hostGamertag="Alice";
    value.maxGamers=6;value.privateSlots=2;value.currentGamers=4;value.openPublicSlots=0;value.openPrivateSlots=2;
    value.members={{"alice","Alice",host,false,0},{"charlie","Charlie",host,false,1},
        {"bob","Bob",remote,false,2},{"dana","Dana",remote,false,3}};
    value.properties[0]=-2147483647-1;value.properties[7]=2147483647;return value;
}
template<class Work>void refused(Work work,const char* code="INVALID_SERVICE_ROSTER") {
    try{work();FAIL()<<"Expected roster refusal";}
    catch(const CnaService::Error& error){EXPECT_EQ(code,error.code());EXPECT_EQ(std::string::npos,std::string(error.what()).find("secret-marker"));}
}
std::vector<unsigned char> roundTrip(const std::vector<unsigned char>& bytes) {
    switch(validateServiceControlPacket(bytes)) {
        case MessageTag::ClientHello:return NetPacketCodec::Encode(NetPacketCodec::DecodeClientHello(bytes));
        case MessageTag::ServerWelcome:return NetPacketCodec::Encode(NetPacketCodec::DecodeServerWelcome(bytes));
        case MessageTag::GamerJoinBroadcast:return NetPacketCodec::Encode(NetPacketCodec::DecodeGamerJoinBroadcast(bytes));
        case MessageTag::GamerLeaveBroadcast:return NetPacketCodec::Encode(NetPacketCodec::DecodeGamerLeaveBroadcast(bytes));
        case MessageTag::StateChangeBroadcast:return NetPacketCodec::Encode(NetPacketCodec::DecodeStateChangeBroadcast(bytes));
        case MessageTag::SessionPropertiesBroadcast:return NetPacketCodec::Encode(NetPacketCodec::DecodeSessionPropertiesBroadcast(bytes));
        default:throw std::runtime_error("unreachable test control");
    }
}
}
TEST(ServiceRosterTest, ExactMachineGroupsOwnStableIdsAndOnlyOwnerHostFlag) {
    ServiceRoster authority(fixture());
    EXPECT_EQ((std::vector<unsigned char>{1,2}),authority.idsFor(host,{"Alice","Charlie"}));
    EXPECT_EQ((std::vector<unsigned char>{4,3}),authority.idsFor(remote,{"Dana","Bob"}));
    EXPECT_EQ((std::vector<std::string>{remote}),authority.remoteMachines());
    const auto welcome=authority.welcomeFor(remote,{"Bob","Dana"});
    ASSERT_EQ(2,welcome.ExistingRoster.size());EXPECT_TRUE(welcome.ExistingRoster[0].IsHost);EXPECT_FALSE(welcome.ExistingRoster[1].IsHost);
    ServiceRoster(fixture(true)).validateWelcome(welcome,{"Bob","Dana"});
    for(const auto names:{std::vector<std::string>{},std::vector<std::string>{"Bob"},std::vector<std::string>{"Bob","Bob"},
        std::vector<std::string>{"Bob","Alice"},std::vector<std::string>{"bob","Dana"},std::vector<std::string>{"secret-marker","Dana"}})
        refused([&]{(void)authority.idsFor(remote,names);});
    refused([&]{(void)authority.idsFor(third,{"Bob","Dana"});});
    refused([&]{(void)authority.welcomeFor(host,{"Alice","Charlie"});});
    refused([&]{(void)ServiceRoster(fixture(true)).welcomeFor(remote,{"Bob","Dana"});});
}
TEST(ServiceRosterTest, RankedAuthorityNeverPermitsJoinInProgress) {
    auto value=fixture();value.kind=Service::ServiceSessionKind::Ranked;
    EXPECT_NO_THROW((void)ServiceRoster(value));value.allowJoinInProgress=true;
    refused([&]{(void)ServiceRoster(value);});
}
TEST(ServiceRosterTest, AllThirtyOneSlotsAndFourLocalAccountsRoundTripWithoutReducedIds) {
    auto value=fixture();value.maxGamers=31;value.currentGamers=31;value.privateSlots=0;value.openPrivateSlots=0;value.openPublicSlots=0;value.members.clear();
    value.hostId="u0";value.hostGamertag="Gamer0";value.hostMachine=std::string(32,'1');value.machine=value.hostMachine;
    std::vector<std::string> names;
    for(int index=0;index<31;++index) {
        const std::string machine(32,static_cast<char>('1'+index/4));
        value.members.push_back({"u"+std::to_string(index),"Gamer"+std::to_string(index),machine,false,index});
        if(index>=4&&index<8)names.push_back("Gamer"+std::to_string(index));
    }
    ServiceRoster authority(value);const auto group=value.members[4].machine;
    const auto welcome=authority.welcomeFor(group,names,{value.members[8].machine});
    EXPECT_EQ((std::vector<unsigned char>{5,6,7,8}),welcome.AssignedWireIds);
    value.machine=group;ServiceRoster client(value);client.validateWelcome(welcome,names);
    for(int start=0;start<31;start+=4) {
        std::vector<std::string> local;
        for(int index=start;index<std::min(31,start+4);++index)local.push_back("Gamer"+std::to_string(index));
        const auto ids=client.idsFor(value.members[start].machine,local);
        for(std::size_t index=0;index<ids.size();++index)EXPECT_EQ(start+static_cast<int>(index)+1,ids[index]);
    }
    auto unicode=fixture();unicode.members[3].gamertag="Příliš";ServiceRoster utf8(unicode);EXPECT_EQ((std::vector<unsigned char>{3,4}),utf8.idsFor(remote,{"Bob","Příliš"}));
}
TEST(ServiceRosterTest, MalformedAuthorityCannotCreateAnIdentityIndex) {
    std::vector<Service::ServiceSessionSnapshot> cases;
    auto edit=[&](auto work){auto value=fixture();work(value);cases.push_back(value);};
    edit([](auto& v){v.session="../secret-marker";});edit([](auto& v){v.machine=third;});edit([](auto& v){v.hostMachine=remote;});
    edit([](auto& v){v.hostId="unknown";});edit([](auto& v){v.hostGamertag="Other";});
    edit([](auto& v){v.maxGamers=32;});edit([](auto& v){v.maxGamers=1;});edit([](auto& v){v.currentGamers=0;});
    edit([](auto& v){v.currentGamers=3;});edit([](auto& v){v.members.clear();});edit([](auto& v){v.revision=0;});
    edit([](auto& v){v.privateSlots=-1;});edit([](auto& v){v.openPrivateSlots=-1;});edit([](auto& v){v.openPublicSlots=4;});
    edit([](auto& v){v.kind=static_cast<Service::ServiceSessionKind>(99);});edit([](auto& v){v.state=static_cast<Service::ServiceSessionState>(99);});
    edit([](auto& v){v.members[3].userId=v.members[2].userId;});edit([](auto& v){v.members[3].gamertag="Alice";});
    edit([](auto& v){v.members[3].ordinal=2;});edit([](auto& v){v.members[3].ordinal=-1;});edit([](auto& v){v.members[3].ordinal=31;});
    edit([](auto& v){v.members[3].machine=std::string(32,'0');});edit([](auto& v){v.members[3].privateSlot=true;});
    edit([](auto& v){v.members[3].gamertag=std::string(33,'x');});edit([](auto& v){v.members[3].gamertag="\xff";});
    edit([](auto& v){v.members[3].gamertag=std::string("x\0y",3);});edit([](auto& v){v.members[3].userId=std::string(65,'x');});
    edit([](auto& v){v.maxGamers=7;++v.currentGamers;v.members.push_back({"eve","Eve",host,false,4});v.members[2].machine=host;v.members[3].machine=host;});
    for(const auto& value:cases)refused([&]{(void)ServiceRoster(value);});
}
TEST(ServiceRosterTest, WelcomeRejectsSpoofedAssignmentsFlagsPartialGroupsAndProperties) {
    ServiceRoster authority(fixture()),client(fixture(true));const auto good=authority.welcomeFor(remote,{"Bob","Dana"});
    std::vector<ServerWelcomeMessage> cases;auto edit=[&](auto work){auto value=good;work(value);cases.push_back(value);};
    edit([](auto& v){v.AssignedWireIds={1,2};});edit([](auto& v){v.AssignedWireIds={4,3};});
    edit([](auto& v){v.ExistingRoster[1].IsHost=true;});edit([](auto& v){v.ExistingRoster[0].IsHost=false;});
    edit([](auto& v){v.ExistingRoster[0].Gamertag="secret-marker";});edit([](auto& v){v.ExistingRoster[0].WireId=3;});
    edit([](auto& v){v.ExistingRoster.push_back(v.ExistingRoster[0]);});edit([](auto& v){v.ExistingRoster.pop_back();});
    edit([](auto& v){v.ExistingRoster.clear();});edit([](auto& v){v.SessionProperties.setItem(0,37);});
    edit([](auto& v){v.ExistingRoster.push_back({3,"Bob",false});});
    for(const auto& value:cases)refused([&]{client.validateWelcome(value,{"Bob","Dana"});});
    client.validateEntries(good.ExistingRoster);refused([&]{client.validateEntries({});});
    refused([&]{(void)authority.welcomeFor(remote,{"Bob","Dana"},{third});});
    refused([&]{(void)authority.welcomeFor(remote,{"Bob","Dana"},{remote});});
}
TEST(ServiceRosterTest, ControlPreflightBoundsEveryFieldBeforeExistingDecoder) {
    const auto welcome=ServiceRoster(fixture()).welcomeFor(remote,{"Bob","Dana"});
    std::vector<std::vector<unsigned char>> valid{NetPacketCodec::Encode(ClientHelloMessage{{"Bob","Dana"}}),NetPacketCodec::Encode(welcome),
        NetPacketCodec::Encode(GamerJoinBroadcastMessage{{{3,"Bob",false},{4,"Dana",false}}}),NetPacketCodec::Encode(GamerLeaveBroadcastMessage{{3,4}}),
        NetPacketCodec::Encode(StateChangeBroadcastMessage{NetworkSessionState::Playing}),NetPacketCodec::Encode(SessionPropertiesBroadcastMessage{welcome.SessionProperties})};
    for(const auto& bytes:valid) {
        EXPECT_EQ(bytes,roundTrip(bytes));
        for(std::size_t length=0;length<bytes.size();++length)refused([&]{(void)validateServiceControlPacket(std::span(bytes).first(length));},"INVALID_SERVICE_PACKET");
        auto extra=bytes;extra.push_back(0);refused([&]{(void)validateServiceControlPacket(extra);},"INVALID_SERVICE_PACKET");
    }
    for(const auto& bytes:std::vector<std::vector<unsigned char>>{{1,0},{1,5},{1,1,128,128,128,128,127},{1,1,33},{1,1,1,255},{1,1,1,0},
        {2,1,0},{2,1,32},{6,3},{7,7},{7,8,2},{16,1,2,0},std::vector<unsigned char>(4097,1)})
        refused([&]{(void)validateServiceControlPacket(bytes);},"INVALID_SERVICE_PACKET");
    auto flags=NetPacketCodec::Encode(GamerJoinBroadcastMessage{{{1,"Alice",true}}});flags.back()=2;
    refused([&]{(void)validateServiceControlPacket(flags);},"INVALID_SERVICE_PACKET");
}
TEST(ServiceRosterTest, TenThousandMutationsRejectSafelyOrCanonicalDecodeRoundTrip) {
    const auto seed=NetPacketCodec::Encode(ServiceRoster(fixture()).welcomeFor(remote,{"Bob","Dana"}));std::mt19937 random(0x7007e1);
    for(int trial=0;trial<10000;++trial) {
        auto bytes=seed;
        if(trial%3==0)bytes.resize(random()%(seed.size()+10));
        for(int change=0;change<1+trial%4&&!bytes.empty();++change)bytes[random()%bytes.size()]=static_cast<unsigned char>(random());
        try{EXPECT_EQ(bytes,roundTrip(bytes));}
        catch(const CnaService::Error& error){EXPECT_EQ("INVALID_SERVICE_PACKET",error.code());}
    }
}

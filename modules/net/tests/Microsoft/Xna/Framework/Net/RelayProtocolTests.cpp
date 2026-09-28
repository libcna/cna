// SPDX-License-Identifier: MS-PL
#include "../../../../../src/Internal/Protocol/CnaService/RelayProtocol.hpp"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <random>
#include <enet/enet.h>
#include <enet/protocol.h>
namespace {
using namespace CnaService;
std::vector<unsigned char> decodeHex(const std::string& hex) {
    std::vector<unsigned char> bytes;
    for(std::size_t index=0;index<hex.size();index+=2)
        bytes.push_back(static_cast<unsigned char>(std::stoul(hex.substr(index,2),nullptr,16)));
    return bytes;
}
template<class Work> void rejected(Work work,const std::string& expected) {
    try{work();FAIL()<<"Expected "<<expected;}catch(const RelayError& error){EXPECT_EQ(expected,error.code());}
}
}
TEST(RelayProtocolTest, CanonicalGoldenMessagesMatchServerAndBorrowPayload) {
    std::ifstream input(CNA_RELAY_PROTOCOL_VECTORS);
    ASSERT_TRUE(input.good());nlohmann::json corpus;input>>corpus;ASSERT_EQ(1,corpus["version"]);
    for(const auto& vector:corpus["vectors"]) {
        SCOPED_TRACE(vector["name"].get<std::string>());const auto bytes=decodeHex(vector["hex"]);
        const auto error=vector["error"].get<std::string>();
        if(error!="OK"){rejected([&]{(void)parseRelayFrame(bytes);},error);continue;}
        const auto parsed=parseRelayFrame(bytes);
        EXPECT_EQ(vector["machine"].get<std::string>(),relayMachineName(parsed.machine));
        EXPECT_EQ(decodeHex(vector["payload"]),std::vector<unsigned char>(parsed.datagram.begin(),parsed.datagram.end()));
        EXPECT_EQ(bytes.data()+RelayHeaderBytes,parsed.datagram.data());EXPECT_EQ(bytes,encodeRelayFrame(parsed.machine,parsed.datagram));
    }
}
TEST(RelayProtocolTest, LimitsCoverEnetMtuAndRejectInvalidIdsBeforeAllocation) {
    static_assert(MaxRelayDatagramBytes==ENET_PROTOCOL_MAXIMUM_MTU);
    const auto id=relayMachineId("00112233445566778899aabbccddeeff");
    std::vector<unsigned char> datagram(MaxRelayDatagramBytes,0xff);auto frame=encodeRelayFrame(id,datagram);
    EXPECT_EQ(MaxRelayFrameBytes,frame.size());EXPECT_EQ(MaxRelayDatagramBytes,parseRelayFrame(frame).datagram.size());
    frame.push_back(0);rejected([&]{(void)parseRelayFrame(frame);},"RELAY_TOO_LARGE");
    datagram.push_back(0);rejected([&]{(void)encodeRelayFrame(id,datagram);},"RELAY_TOO_LARGE");
    rejected([&]{(void)encodeRelayFrame(id,{});},"RELAY_EMPTY_PAYLOAD");
    rejected([&]{(void)relayMachineName({});},"RELAY_MACHINE_ID");
    rejected([&]{(void)encodeRelayFrame({},std::array<unsigned char,1>{0});},"RELAY_MACHINE_ID");
    for(const auto* invalid:{"","../file","00000000000000000000000000000000","00112233445566778899AABBCCDDEEFF","00112233445566778899aabbccddeefg"})
        rejected([&]{(void)relayMachineId(invalid);},"RELAY_MACHINE_ID");
    EXPECT_EQ("00112233445566778899aabbccddeeff",relayMachineName(id));
    EXPECT_EQ(64U,MaxRelayQueuedFrames);EXPECT_EQ(MaxRelayFrameBytes*MaxRelayQueuedFrames,MaxRelayQueuedBytes);
}
TEST(RelayProtocolTest, TenThousandMutationsEitherRejectSafelyOrRoundTripExactly) {
    std::mt19937 random(0xc0a108);const auto machine=relayMachineId("00112233445566778899aabbccddeeff");
    const auto base=encodeRelayFrame(machine,std::array<unsigned char,4>{0,1,2,255});
    for(int index=0;index<10000;++index) {
        auto candidate=base;
        if(random()%3==0)candidate.resize(random()%(MaxRelayFrameBytes+2),0x55);
        else candidate[random()%candidate.size()]=static_cast<unsigned char>(random());
        try{const auto parsed=parseRelayFrame(candidate);EXPECT_EQ(candidate,encodeRelayFrame(parsed.machine,parsed.datagram));}
        catch(const RelayError& error){EXPECT_TRUE(error.code().starts_with("RELAY_"));}
    }
}

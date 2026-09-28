// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "../../../../src/Internal/RelayEnetPolicy.hpp"
#include "CNA/Internal/Net/ENetHostHandle.hpp"
#include <cstring>
#include <random>
#include <vector>

namespace {
using namespace CNA::Internal::Net;
std::vector<unsigned char> fragment(bool unreliable=false) {
    ENetProtocolHeader header{};header.peerID=ENET_HOST_TO_NET_16(ENET_PROTOCOL_HEADER_FLAG_SENT_TIME);
    ENetProtocolSendFragment command{};
    command.header.command=unreliable?ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE_FRAGMENT:
        static_cast<int>(ENET_PROTOCOL_COMMAND_SEND_FRAGMENT)|ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
    command.dataLength=ENET_HOST_TO_NET_16(4);command.fragmentCount=ENET_HOST_TO_NET_32(2);
    command.totalLength=ENET_HOST_TO_NET_32(8);
    std::vector<unsigned char> bytes(sizeof(header)+sizeof(command)+4);
    std::memcpy(bytes.data(),&header,sizeof(header));std::memcpy(bytes.data()+sizeof(header),&command,sizeof(command));
    return bytes;
}
void field(std::vector<unsigned char>& bytes,std::size_t offset,std::uint32_t value) {
    value=ENET_HOST_TO_NET_32(value);std::memcpy(bytes.data()+sizeof(ENetProtocolHeader)+offset,&value,sizeof(value));
}
}
TEST(RelayEnetPolicyTest, BoundedReliableAndUnreliableFragmentsAndEveryTruncation) {
    for(bool unreliable:{false,true}) {
        const auto bytes=fragment(unreliable);EXPECT_TRUE(validateRelayEnetDatagram(bytes));
        for(std::size_t size=0;size<bytes.size();++size)EXPECT_FALSE(validateRelayEnetDatagram(std::span(bytes).first(size)))<<size;
    }
}
TEST(RelayEnetPolicyTest, FragmentBitmapAndLogicalPacketCannotEscapeByteLimits) {
    const auto rejects=[](std::size_t offset,std::uint32_t value) {
        auto bytes=fragment();field(bytes,offset,value);EXPECT_FALSE(validateRelayEnetDatagram(bytes))<<offset<<":"<<value;
    };
    for(auto count:{0u,9u,static_cast<unsigned int>(MaxRelayGameFragments+1),1048576u,0xffffffffu})
        rejects(offsetof(ENetProtocolSendFragment,fragmentCount),count);
    for(auto total:{0u,1u,3u,static_cast<unsigned int>(MaxRelayGamePacketBytes+1),0xffffffffu})
        rejects(offsetof(ENetProtocolSendFragment,totalLength),total);
    for(auto number:{2u,0xffffffffu})rejects(offsetof(ENetProtocolSendFragment,fragmentNumber),number);
    for(auto start:{5u,8u,0xffffffffu})rejects(offsetof(ENetProtocolSendFragment,fragmentOffset),start);
}
TEST(RelayEnetPolicyTest, RejectsUnconfiguredCompressionUnknownTagsAndCommandOverflow) {
    auto bytes=fragment();enet_uint16 compressed=ENET_HOST_TO_NET_16(ENET_PROTOCOL_HEADER_FLAG_COMPRESSED|ENET_PROTOCOL_HEADER_FLAG_SENT_TIME);
    std::memcpy(bytes.data(),&compressed,sizeof(compressed));EXPECT_FALSE(validateRelayEnetDatagram(bytes));
    for(int type:{0,13,15,0x26}) {bytes=fragment();bytes[sizeof(ENetProtocolHeader)]=static_cast<unsigned char>(type);EXPECT_FALSE(validateRelayEnetDatagram(bytes));}
    bytes={0,0};ENetProtocolPing ping{};ping.header.command=ENET_PROTOCOL_COMMAND_PING;
    const auto* data=reinterpret_cast<const unsigned char*>(&ping);
    for(int i=0;i<ENET_PROTOCOL_MAXIMUM_PACKET_COMMANDS;++i)bytes.insert(bytes.end(),data,data+sizeof(ping));
    EXPECT_TRUE(validateRelayEnetDatagram(bytes));bytes.insert(bytes.end(),data,data+sizeof(ping));EXPECT_FALSE(validateRelayEnetDatagram(bytes));
    bytes.resize(4097);EXPECT_FALSE(validateRelayEnetDatagram(bytes));
}
TEST(RelayEnetPolicyTest, LengthMutationsNeverPermitUnboundedFragments) {
    std::mt19937 random(1735);int accepted=0;
    for(int iteration=0;iteration<10000;++iteration) {
        auto bytes=fragment();const auto count=random()%(MaxRelayGameFragments*2);
        const auto total=random()%(MaxRelayGamePacketBytes+1024);
        field(bytes,offsetof(ENetProtocolSendFragment,fragmentCount),count);
        field(bytes,offsetof(ENetProtocolSendFragment,totalLength),total);
        if(validateRelayEnetDatagram(bytes)) {++accepted;EXPECT_LE(count,MaxRelayGameFragments);EXPECT_LE(count,total);EXPECT_LE(total,MaxRelayGamePacketBytes);}
        bytes.resize(random()%bytes.size());(void)validateRelayEnetDatagram(bytes);
    }
    EXPECT_GT(accepted,1000);
}
TEST(RelayEnetPolicyTest, MaximumPacketFitsMinimumNegotiatedEnetMtu) {
    constexpr auto payload=ENET_PROTOCOL_MINIMUM_MTU-sizeof(ENetProtocolHeader)-sizeof(ENetProtocolSendFragment);
    constexpr auto fragments=(MaxRelayGamePacketBytes+payload-1)/payload;
    static_assert(fragments<=MaxRelayGameFragments);
    auto bytes=fragment();field(bytes,offsetof(ENetProtocolSendFragment,fragmentCount),fragments);
    field(bytes,offsetof(ENetProtocolSendFragment,totalLength),MaxRelayGamePacketBytes);
    EXPECT_TRUE(validateRelayEnetDatagram(bytes));
}

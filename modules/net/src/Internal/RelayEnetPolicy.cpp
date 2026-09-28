// SPDX-License-Identifier: MS-PL
#include "RelayEnetPolicy.hpp"
#include "CNA/Internal/Net/ENetHostHandle.hpp"
#include "CnaService/RelayProtocol.hpp"
#include <array>
#include <cstring>

namespace CNA::Internal::Net {
namespace {
constexpr std::array<std::size_t,ENET_PROTOCOL_COMMAND_COUNT> sizes{
    0,sizeof(ENetProtocolAcknowledge),sizeof(ENetProtocolConnect),sizeof(ENetProtocolVerifyConnect),
    sizeof(ENetProtocolDisconnect),sizeof(ENetProtocolPing),sizeof(ENetProtocolSendReliable),
    sizeof(ENetProtocolSendUnreliable),sizeof(ENetProtocolSendFragment),sizeof(ENetProtocolSendUnsequenced),
    sizeof(ENetProtocolBandwidthLimit),sizeof(ENetProtocolThrottleConfigure),sizeof(ENetProtocolSendFragment)};
}
bool validateRelayEnetDatagram(std::span<const unsigned char> bytes) noexcept {
    if(bytes.size()<sizeof(enet_uint16)||bytes.size()>CnaService::MaxRelayDatagramBytes)return false;
    enet_uint16 header;std::memcpy(&header,bytes.data(),sizeof(header));header=ENET_NET_TO_HOST_16(header);
    // Relay hosts never configure compression or a checksum; alternate encodings cannot be
    // preflighted as this packet layout before the dependency's own allocation path.
    if(header&ENET_PROTOCOL_HEADER_FLAG_COMPRESSED)return false;
    std::size_t offset=(header&ENET_PROTOCOL_HEADER_FLAG_SENT_TIME)?sizeof(ENetProtocolHeader):sizeof(header);
    if(offset>=bytes.size())return false;
    int commands=0;
    while(offset<bytes.size()) {
        if(++commands>ENET_PROTOCOL_MAXIMUM_PACKET_COMMANDS||bytes.size()-offset<sizeof(ENetProtocolCommandHeader))return false;
        const unsigned char flags=bytes[offset];const auto type=flags&ENET_PROTOCOL_COMMAND_MASK;
        constexpr int allowed=static_cast<int>(ENET_PROTOCOL_COMMAND_MASK)|ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE|ENET_PROTOCOL_COMMAND_FLAG_UNSEQUENCED;
        if((flags&~allowed)||type==0||type>=ENET_PROTOCOL_COMMAND_COUNT)return false;
        const auto size=sizes[type];if(size>bytes.size()-offset)return false;
        ENetProtocol command{};std::memcpy(&command,bytes.data()+offset,size);offset+=size;
        std::size_t payload=0;
        switch(type) {
            case ENET_PROTOCOL_COMMAND_SEND_RELIABLE:payload=ENET_NET_TO_HOST_16(command.sendReliable.dataLength);break;
            case ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE:payload=ENET_NET_TO_HOST_16(command.sendUnreliable.dataLength);break;
            case ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED:payload=ENET_NET_TO_HOST_16(command.sendUnsequenced.dataLength);break;
            case ENET_PROTOCOL_COMMAND_SEND_FRAGMENT:
            case ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE_FRAGMENT: {
                payload=ENET_NET_TO_HOST_16(command.sendFragment.dataLength);
                const auto count=ENET_NET_TO_HOST_32(command.sendFragment.fragmentCount);
                const auto number=ENET_NET_TO_HOST_32(command.sendFragment.fragmentNumber);
                const auto total=ENET_NET_TO_HOST_32(command.sendFragment.totalLength);
                const auto start=ENET_NET_TO_HOST_32(command.sendFragment.fragmentOffset);
                if(!payload||!count||count>MaxRelayGameFragments||count>total||number>=count||
                    total>MaxRelayGamePacketBytes||start>=total||payload>total-start)return false;
                break;
            }
            default:break;
        }
        if(payload>bytes.size()-offset)return false;
        offset+=payload;
    }
    return true;
}
}

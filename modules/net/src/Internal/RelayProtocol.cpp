// SPDX-License-Identifier: MIT
#include "CnaService/RelayProtocol.hpp"
#include <algorithm>

namespace CnaService {
RelayError::RelayError(std::string code):std::runtime_error("CNA relay frame rejected"),code_(std::move(code)){}
const std::string& RelayError::code() const noexcept{return code_;}
namespace {
void machineGuard(const RelayMachineId& id) {
    if(std::all_of(id.begin(),id.end(),[](unsigned char value){return value==0;}))throw RelayError("RELAY_MACHINE_ID");
}
unsigned char digit(char value) {
    if(value>='0'&&value<='9')return static_cast<unsigned char>(value-'0');
    if(value>='a'&&value<='f')return static_cast<unsigned char>(value-'a'+10);
    throw RelayError("RELAY_MACHINE_ID");
}
}
RelayFrameView parseRelayFrame(std::span<const unsigned char> bytes) {
    if(bytes.size()<RelayHeaderBytes)throw RelayError("RELAY_TRUNCATED");
    if(bytes.size()>MaxRelayFrameBytes)throw RelayError("RELAY_TOO_LARGE");
    if(bytes[0]!='C'||bytes[1]!='N'||bytes[2]!='R')throw RelayError("RELAY_MAGIC");
    if(bytes[3]!=RelayVersion)throw RelayError("RELAY_VERSION");
    if(bytes[4]!=1)throw RelayError("RELAY_MESSAGE_TYPE");
    if(bytes[5]!=0||bytes[6]!=0||bytes[7]!=0)throw RelayError("RELAY_RESERVED");
    if(bytes.size()==RelayHeaderBytes)throw RelayError("RELAY_EMPTY_PAYLOAD");
    RelayFrameView result;std::copy_n(bytes.begin()+8,result.machine.size(),result.machine.begin());machineGuard(result.machine);
    result.datagram=bytes.subspan(RelayHeaderBytes);return result;
}
std::vector<unsigned char> encodeRelayFrame(const RelayMachineId& machine,std::span<const unsigned char> datagram) {
    machineGuard(machine);
    if(datagram.empty())throw RelayError("RELAY_EMPTY_PAYLOAD");
    if(datagram.size()>MaxRelayDatagramBytes)throw RelayError("RELAY_TOO_LARGE");
    std::vector<unsigned char> bytes;bytes.reserve(RelayHeaderBytes+datagram.size());
    bytes.insert(bytes.end(),{'C','N','R',RelayVersion,1,0,0,0});
    bytes.insert(bytes.end(),machine.begin(),machine.end());bytes.insert(bytes.end(),datagram.begin(),datagram.end());return bytes;
}
RelayMachineId relayMachineId(std::string_view hex) {
    if(hex.size()!=32)throw RelayError("RELAY_MACHINE_ID");
    RelayMachineId result{};
    for(std::size_t i=0;i<result.size();++i)result[i]=static_cast<unsigned char>((digit(hex[i*2])<<4)|digit(hex[i*2+1]));
    machineGuard(result);return result;
}
std::string relayMachineName(const RelayMachineId& machine) {
    machineGuard(machine);constexpr char digits[]="0123456789abcdef";std::string result(32,'0');
    for(std::size_t i=0;i<machine.size();++i){result[i*2]=digits[machine[i]>>4];result[i*2+1]=digits[machine[i]&15];}return result;
}
}

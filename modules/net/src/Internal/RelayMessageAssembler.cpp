// SPDX-License-Identifier: MS-PL
#include "RelayMessageAssembler.hpp"
#include "CnaService/Protocol.hpp"
#include <set>

namespace CNA::Internal::Net {
namespace {
bool control(RelayMessageKind kind) {
    return kind==RelayMessageKind::Ping||kind==RelayMessageKind::Pong||kind==RelayMessageKind::Close;
}
bool hexId(std::string_view value) {
    if(value.size()!=32)return false;
    for(auto c:value)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
    return true;
}
}
RelayMessageAssembler::RelayMessageAssembler(std::size_t maxBytes,std::size_t maxFragments)
    :maxBytes_(maxBytes),maxFragments_(maxFragments) {
    if(maxBytes==0||maxBytes>CnaService::MaxRelayFrameBytes||maxFragments==0||maxFragments>64)
        throw CnaService::RelayError("RELAY_RECEIVE_LIMIT");
}
void RelayMessageAssembler::reset() noexcept {
    frame_.reset();messageKind_.reset();fragments_=0;data_.clear();control_.clear();
}
[[noreturn]] void RelayMessageAssembler::fail(const char* code) {
    reset();throw CnaService::RelayError(code);
}
std::optional<RelayMessage> RelayMessageAssembler::push(const RelayChunk& chunk) {
    auto kind=chunk.kind;
    if(kind==RelayMessageKind::Continuation) {
        if(!messageKind_)fail("RELAY_FRAGMENT_TYPE");
        kind=*messageKind_;
    }
    const bool isControl=control(kind);
    if(!isControl&&kind!=RelayMessageKind::Text&&kind!=RelayMessageKind::Binary)fail("RELAY_MESSAGE_TYPE");
    const auto ceiling=isControl?std::size_t{125}:maxBytes_;
    if(chunk.offset>ceiling||chunk.bytes.size()>ceiling-chunk.offset||chunk.bytesLeft>ceiling-chunk.offset-chunk.bytes.size())
        fail("RELAY_TOO_LARGE");
    if(isControl&&chunk.moreFragments)fail("RELAY_FRAGMENT_TYPE");
    if(frame_) {
        if(frame_->kind!=kind||frame_->more!=chunk.moreFragments||frame_->next!=chunk.offset||
           frame_->remaining!=chunk.bytes.size()+chunk.bytesLeft)fail("RELAY_CHUNK_ORDER");
    }else {
        if(chunk.offset!=0)fail("RELAY_CHUNK_ORDER");
        if(!isControl) {
            if(messageKind_&&*messageKind_!=kind)fail("RELAY_FRAGMENT_TYPE");
            if(fragments_>=maxFragments_)fail("RELAY_FRAGMENT_LIMIT");
            ++fragments_;messageKind_=kind;
        }
        frame_=Frame{kind,chunk.moreFragments,0,chunk.bytes.size()+chunk.bytesLeft};
    }
    auto& target=isControl?control_:data_;
    if(chunk.bytes.size()>ceiling-target.size()||chunk.bytesLeft>ceiling-target.size()-chunk.bytes.size())fail("RELAY_TOO_LARGE");
    target.insert(target.end(),chunk.bytes.begin(),chunk.bytes.end());
    frame_->next+=chunk.bytes.size();frame_->remaining=chunk.bytesLeft;
    if(chunk.bytesLeft!=0)return {};
    frame_.reset();
    if(isControl){RelayMessage result{kind,std::move(control_)};control_.clear();return result;}
    if(chunk.moreFragments)return {};
    RelayMessage result{kind,std::move(data_)};data_.clear();messageKind_.reset();fragments_=0;return result;
}
void validateRelayWelcome(std::string_view bytes,std::string_view requestId,std::string_view session,std::string_view machine) {
    try {
        if(bytes.empty()||bytes.size()>CnaService::MaxRelayHelloBytes||!CnaService::identifier(requestId)||!hexId(session)||!hexId(machine))
            throw CnaService::Error("INVALID_ARGUMENT");
        const auto response=CnaService::parse(bytes);
        if(!response.is_object()||response.size()!=4||!response.contains("v")||!response["v"].is_number_integer()||
           response["v"]!=CnaService::RelayVersion||CnaService::stringField(response,"id",64)!=requestId||
           CnaService::stringField(response,"error",64)!="OK")throw CnaService::Error("MALFORMED_MESSAGE");
        const auto& result=response.at("result");
        if(!result.is_object()||result.size()!=5||CnaService::stringField(result,"session",32)!=session||
           CnaService::stringField(result,"machine",32)!=machine||!result.at("maxDatagramBytes").is_number_integer()||
           result["maxDatagramBytes"]!=CnaService::MaxRelayDatagramBytes||!result.at("maxQueuedFrames").is_number_integer()||
           result["maxQueuedFrames"]!=CnaService::MaxRelayQueuedFrames)throw CnaService::Error("MALFORMED_MESSAGE");
        (void)CnaService::relayMachineId(machine);
        const auto& capabilities=result.at("capabilities");
        if(!capabilities.is_array()||capabilities.empty()||capabilities.size()>16)throw CnaService::Error("MALFORMED_MESSAGE");
        std::set<std::string> seen;
        for(const auto& capability:capabilities) {
            if(!capability.is_string())throw CnaService::Error("MALFORMED_MESSAGE");
            const auto& name=capability.get_ref<const std::string&>();
            if(!CnaService::identifier(name)||!seen.insert(name).second)throw CnaService::Error("MALFORMED_MESSAGE");
        }
        if(!seen.contains("enet-datagrams"))throw CnaService::Error("MALFORMED_MESSAGE");
    }catch(...) {throw CnaService::RelayError("RELAY_WELCOME_MISMATCH");}
}
}

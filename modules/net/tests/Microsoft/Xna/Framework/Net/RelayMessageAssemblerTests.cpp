// SPDX-License-Identifier: MS-PL
#include "../../../../../src/Internal/RelayMessageAssembler.hpp"
#include "CnaService/Protocol.hpp"
#include <gtest/gtest.h>
#include <array>
#include <limits>
#include <random>

namespace {
using namespace CNA::Internal::Net;
using CnaService::Json;
using Kind=RelayMessageKind;
const std::string session(32,'1'),machine(32,'2');
template<class Work> void refused(Work work,const char* code) {
    try {work();FAIL()<<"Expected bounded relay refusal";}catch(const CnaService::RelayError& error){EXPECT_EQ(code,error.code());}
}
Json welcome() {
    return Json{{"v",1},{"id","relay-connect"},{"error","OK"},{"result",{
        {"session",session},{"machine",machine},{"capabilities",Json::array({"enet-datagrams"})},
        {"maxDatagramBytes",CnaService::MaxRelayDatagramBytes},{"maxQueuedFrames",CnaService::MaxRelayQueuedFrames}}}};
}
}
TEST(RelayMessageAssemblerTest, CompleteMessagesOwnInputAndResetBetweenMessages) {
    RelayMessageAssembler receiver;std::array<unsigned char,3> bytes{0,128,255};
    auto result=receiver.push({Kind::Binary,false,0,0,bytes});ASSERT_TRUE(result);
    EXPECT_EQ(Kind::Binary,result->kind);bytes[0]=44;EXPECT_EQ(0,result->bytes[0]);
    result=receiver.push({Kind::Text,false,0,0,bytes});ASSERT_TRUE(result);EXPECT_EQ(Kind::Text,result->kind);EXPECT_EQ(44,result->bytes[0]);
    result=receiver.push({Kind::Binary,false,0,0,{}});ASSERT_TRUE(result);EXPECT_TRUE(result->bytes.empty());
    EXPECT_NO_THROW((void)RelayMessageAssembler(CnaService::MaxRelayHelloBytes,1));
    refused([]{(void)RelayMessageAssembler(0);},"RELAY_RECEIVE_LIMIT");
    refused([]{(void)RelayMessageAssembler(CnaService::MaxRelayFrameBytes+1);},"RELAY_RECEIVE_LIMIT");
    refused([]{(void)RelayMessageAssembler(1,0);},"RELAY_RECEIVE_LIMIT");
    refused([]{(void)RelayMessageAssembler(1,65);},"RELAY_RECEIVE_LIMIT");
}
TEST(RelayMessageAssemblerTest, ExactLimitChunksFragmentsAndEmptyFinalAreAccepted) {
    RelayMessageAssembler receiver;std::vector<unsigned char> bytes(CnaService::MaxRelayFrameBytes,0x91);
    EXPECT_FALSE(receiver.push({Kind::Binary,true,0,21,std::span(bytes).first(3)}));
    EXPECT_FALSE(receiver.push({Kind::Binary,true,3,0,std::span(bytes).subspan(3,21)}));
    EXPECT_FALSE(receiver.push({Kind::Continuation,true,0,0,std::span(bytes).subspan(24)}));
    auto result=receiver.push({Kind::Continuation,false,0,0,{}});ASSERT_TRUE(result);EXPECT_EQ(bytes,result->bytes);
    EXPECT_FALSE(receiver.push({Kind::Binary,false,0,bytes.size()-1,std::span(bytes).first(1)}));
    result=receiver.push({Kind::Binary,false,1,0,std::span(bytes).subspan(1)});ASSERT_TRUE(result);EXPECT_EQ(bytes,result->bytes);
    bytes.push_back(0);refused([&]{(void)receiver.push({Kind::Binary,false,0,0,bytes});},"RELAY_TOO_LARGE");
}
TEST(RelayMessageAssemblerTest, ControlMessagesCanInterleaveOnlyBetweenDataFrames) {
    RelayMessageAssembler receiver;std::array<unsigned char,2> first{1,2},last{3,4};
    EXPECT_FALSE(receiver.push({Kind::Binary,true,0,0,first}));
    for(auto kind:{Kind::Ping,Kind::Pong,Kind::Close}) {
        EXPECT_FALSE(receiver.push({kind,false,0,1,std::span(first).first(1)}));
        auto event=receiver.push({kind,false,1,0,std::span(first).last(1)});ASSERT_TRUE(event);EXPECT_EQ(kind,event->kind);
        EXPECT_EQ((std::vector<unsigned char>{1,2}),event->bytes);
    }
    auto result=receiver.push({Kind::Binary,false,0,0,last});ASSERT_TRUE(result);EXPECT_EQ((std::vector<unsigned char>{1,2,3,4}),result->bytes);
    EXPECT_FALSE(receiver.push({Kind::Binary,true,0,1,std::span(first).first(1)}));
    refused([&]{(void)receiver.push({Kind::Ping,false,0,0,last});},"RELAY_CHUNK_ORDER");
}
TEST(RelayMessageAssemblerTest, InvalidMetadataAndOverflowAreRefusedBeforeCopyAndClearPartialState) {
    std::array<unsigned char,2> bytes{1,2};
    for(const auto invalid:{RelayChunk{Kind::Binary,false,1,0,bytes},
        RelayChunk{Kind::Binary,false,std::numeric_limits<std::size_t>::max(),0,bytes},
        RelayChunk{Kind::Binary,false,0,std::numeric_limits<std::size_t>::max(),bytes},
        RelayChunk{static_cast<Kind>(99),false,0,0,bytes},RelayChunk{Kind::Continuation,false,0,0,bytes},
        RelayChunk{Kind::Ping,true,0,0,bytes}}) {
        RelayMessageAssembler receiver;
        EXPECT_THROW((void)receiver.push(invalid),CnaService::RelayError);
        auto valid=receiver.push({Kind::Binary,false,0,0,bytes});ASSERT_TRUE(valid);EXPECT_EQ(2U,valid->bytes.size());
    }
    std::vector<unsigned char> control(126,0);
    RelayMessageAssembler receiver;refused([&]{(void)receiver.push({Kind::Ping,false,0,0,control});},"RELAY_TOO_LARGE");
    for(const auto invalid:{RelayChunk{Kind::Binary,true,1,0,std::span(bytes).last(1)},
        RelayChunk{Kind::Binary,false,0,0,std::span(bytes).last(1)},
        RelayChunk{Kind::Binary,false,1,1,std::span(bytes).last(1)}}) {
        EXPECT_FALSE(receiver.push({Kind::Binary,false,0,1,std::span(bytes).first(1)}));
        refused([&]{(void)receiver.push(invalid);},"RELAY_CHUNK_ORDER");
    }
    EXPECT_FALSE(receiver.push({Kind::Binary,true,0,0,bytes}));
    refused([&]{(void)receiver.push({Kind::Text,false,0,0,bytes});},"RELAY_FRAGMENT_TYPE");
}
TEST(RelayMessageAssemblerTest, AggregateSizeKnownRemaindersAndEmptyFragmentCountsAreBounded) {
    RelayMessageAssembler receiver(4,3);std::array<unsigned char,2> bytes{1,2};
    EXPECT_FALSE(receiver.push({Kind::Binary,true,0,0,bytes}));
    refused([&]{(void)receiver.push({Kind::Binary,false,0,1,bytes});},"RELAY_TOO_LARGE");
    for(int index=0;index<3;++index)EXPECT_FALSE(receiver.push({Kind::Binary,true,0,0,{}}));
    refused([&]{(void)receiver.push({Kind::Binary,false,0,0,{}});},"RELAY_FRAGMENT_LIMIT");
    EXPECT_FALSE(receiver.push({Kind::Binary,true,0,0,bytes}));receiver.reset();
    auto result=receiver.push({Kind::Text,false,0,0,bytes});ASSERT_TRUE(result);EXPECT_EQ(2U,result->bytes.size());
}
TEST(RelayMessageAssemblerTest, WelcomeCorrelatesAuthorityAndCapabilitiesBeforeReadiness) {
    EXPECT_NO_THROW(validateRelayWelcome(welcome().dump(),"relay-connect",session,machine));
    auto extended=welcome();extended["result"]["capabilities"].push_back("optional-future-capability");
    EXPECT_NO_THROW(validateRelayWelcome(extended.dump(),"relay-connect",session,machine));
    refused([&]{validateRelayWelcome(welcome().dump(),"other-request",session,machine);},"RELAY_WELCOME_MISMATCH");
    refused([&]{validateRelayWelcome(welcome().dump(),"relay-connect",std::string(32,'3'),machine);},"RELAY_WELCOME_MISMATCH");
    refused([&]{validateRelayWelcome(welcome().dump(),"relay-connect",session,std::string(32,'4'));},"RELAY_WELCOME_MISMATCH");
}
TEST(RelayMessageAssemblerTest, MalformedWelcomeNeverLeaksServerInput) {
    std::vector<Json> cases;
    auto edit=[&](auto work){auto value=welcome();work(value);cases.push_back(value);};
    edit([](auto& j){j["v"]=2;});edit([](auto& j){j["v"]=1.0;});edit([](auto& j){j["v"]=true;});
    edit([](auto& j){j["error"]="credential-must-not-appear";});edit([](auto& j){j["id"]="another-request";});
    edit([](auto& j){j["extra"]=1;});edit([](auto& j){j.erase("error");j["unknown"]="OK";});
    edit([](auto& j){j["result"]["session"]=std::string(32,'3');});edit([](auto& j){j["result"]["machine"]="../path";});
    edit([](auto& j){j["result"]["extra"]=false;});
    for(const auto& key:{"maxDatagramBytes","maxQueuedFrames"}) {
        edit([&](auto& j){j["result"][key]=0;});edit([&](auto& j){j["result"][key]=18446744073709551615ULL;});
        edit([&](auto& j){j["result"][key]=true;});edit([&](auto& j){j["result"].erase(key);});
    }
    edit([](auto& j){j["result"]["capabilities"]=Json::array();});
    edit([](auto& j){j["result"]["capabilities"]=Json::array({"other-capability"});});
    edit([](auto& j){j["result"]["capabilities"]=Json::array({"enet-datagrams","enet-datagrams"});});
    edit([](auto& j){j["result"]["capabilities"].push_back(5);});
    edit([](auto& j){j["result"]["capabilities"].push_back("../bad");});
    edit([](auto& j){for(int index=0;index<16;++index)j["result"]["capabilities"].push_back("cap"+std::to_string(index));});
    for(const auto& value:cases)refused([&]{validateRelayWelcome(value.dump(),"relay-connect",session,machine);},"RELAY_WELCOME_MISMATCH");
    for(const auto bytes:{std::string(),std::string(1025,'x'),std::string("{\"v\":1,\"v\":1}"),std::string("{\"v\":\"\xff\"}")}) {
        try{validateRelayWelcome(bytes,"relay-connect",session,machine);FAIL();}
        catch(const CnaService::RelayError& error){EXPECT_EQ("RELAY_WELCOME_MISMATCH",error.code());EXPECT_STREQ("CNA relay frame rejected",error.what());}
    }
}
TEST(RelayMessageAssemblerTest, ThousandDeterministicValidFragmentChunkLayoutsRoundTrip) {
    std::mt19937 random(0xc0a80c1);
    for(int trial=0;trial<1000;++trial) {
        std::vector<unsigned char> bytes(random()%(CnaService::MaxRelayFrameBytes+1));
        for(auto& byte:bytes)byte=static_cast<unsigned char>(random());
        RelayMessageAssembler receiver;std::size_t offset=0;std::optional<RelayMessage> result;
        do {
            const auto size=std::min<std::size_t>(bytes.size()-offset,1+random()%512);
            const bool more=offset+size<bytes.size();std::size_t chunkOffset=0;
            do {
                const auto count=std::min<std::size_t>(size-chunkOffset,1+random()%71);
                result=receiver.push({Kind::Binary,more,chunkOffset,size-chunkOffset-count,std::span(bytes).subspan(offset+chunkOffset,count)});
                chunkOffset+=count;
            }while(chunkOffset<size);
            offset+=size;
            if(more) {
                auto ping=receiver.push({Kind::Ping,false,0,0,{}});ASSERT_TRUE(ping);EXPECT_EQ(Kind::Ping,ping->kind);
            }
        }while(offset<bytes.size());
        ASSERT_TRUE(result);EXPECT_EQ(bytes,result->bytes);
    }
}

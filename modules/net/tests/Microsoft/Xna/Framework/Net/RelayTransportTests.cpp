// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "../../../../../src/Internal/RelayTransport.hpp"
#include "../../../../../src/Internal/RelayWebSocket.hpp"
#include "CnaService/RelayProtocol.hpp"
#include <chrono>
#include <thread>

namespace {
using namespace CNA::Internal::Net;
CNA::GamerServices::Configuration settings(){return {"http://127.0.0.1:1/cna/v1","relay-tests","",true};}
CNA::Internal::GamerServices::ServiceRelayTicket ticket(){return {std::string(64,'a'),std::string(32,'1'),std::string(32,'2'),1,61};}
const std::string other(32,'3'),third(32,'4');
template<class Work>void rejected(Work work,const char* code) {
    try{work();FAIL()<<"Expected relay refusal";}
    catch(const CnaService::RelayError& error){EXPECT_EQ(code,error.code());EXPECT_STREQ("CNA relay frame rejected",error.what());}
}
}
TEST(RelayTransportTest, DerivesOnlyConfiguredValidatedAuthority) {
    auto config=settings();EXPECT_EQ("ws://127.0.0.1:1/cna/relay/v1",relayEndpoint(config));
    config.endpoint="https://service.example:9443/cna/v1";config.insecureLoopback=false;
    EXPECT_EQ("wss://service.example:9443/cna/relay/v1",relayEndpoint(config));
    config.endpoint="https://[::1]:9443/cna/v1";EXPECT_EQ("wss://[::1]:9443/cna/relay/v1",relayEndpoint(config));
    config.endpoint.clear();rejected([&]{(void)relayEndpoint(config);},"RELAY_NOT_CONFIGURED");
    for(const auto* endpoint:{"http://service.example/cna/v1","http://127.0.0.1/cna/v1","https://service.example/other",
        "https://secret@service.example/cna/v1","https://service.example/cna/v1?token=secret","https://service.example/cna/v1#secret"}) {
        config.endpoint=endpoint;EXPECT_ANY_THROW((void)relayEndpoint(config));
    }
}
TEST(RelayTransportTest, ValidatesRouteAuthorityAndPortBeforeWorkerCreation) {
    rejected([&]{RelayTransport bridge(settings(),ticket(),0,{});},"RELAY_LOCAL_PORT_INVALID");
    rejected([&]{RelayTransport bridge(settings(),ticket(),12000,{std::string(32,'2')});},"RELAY_ROUTE_INVALID");
    rejected([&]{RelayTransport bridge(settings(),ticket(),12000,{other,other});},"RELAY_ROUTE_INVALID");
    rejected([&]{RelayTransport bridge(settings(),ticket(),12000,std::vector<std::string>(31,other));},"RELAY_ROUTE_LIMIT");
    EXPECT_ANY_THROW((RelayTransport(settings(),ticket(),12000,{"../../untrusted"})));
}
TEST(RelayTransportTest, RoutesAreStableAtomicBoundedAndStopIsIdempotent) {
    RelayTransport bridge(settings(),ticket(),12000,{other,third});
    const auto first=bridge.routePort(other),second=bridge.routePort(third);
    EXPECT_NE(0,first);EXPECT_NE(0,second);EXPECT_NE(first,second);
    bridge.setRoutes({third,other});EXPECT_EQ(first,bridge.routePort(other));EXPECT_EQ(second,bridge.routePort(third));
    rejected([&]{bridge.setRoutes({other,other});},"RELAY_ROUTE_INVALID");
    EXPECT_EQ(first,bridge.routePort(other));EXPECT_EQ(second,bridge.routePort(third));
    bridge.setRoutes({third});EXPECT_EQ(0,bridge.routePort(other));EXPECT_EQ(second,bridge.routePort(third));
    bridge.setRoutes({});EXPECT_EQ(0,bridge.routePort(third));
    bridge.stop();bridge.stop();const auto status=bridge.status();EXPECT_EQ(RelayTransportState::Stopped,status.state);EXPECT_EQ(0,status.queued);
    rejected([&]{bridge.setRoutes({other});},"RELAY_STOPPED");EXPECT_EQ(0,bridge.routePort(other));
}
TEST(RelayTransportTest, ConnectionFailureIsObservedWithoutCallbackOrDetachedWorker) {
    RelayTransport bridge(settings(),ticket(),12000,{});
    const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    while(bridge.status().state==RelayTransportState::Connecting&&std::chrono::steady_clock::now()<limit)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto status=bridge.status();EXPECT_EQ(RelayTransportState::Failed,status.state);
    // A build that cannot attempt the connection at all -- its libcurl has no ws/wss, as Apple's
    // system libcurl has not -- fails the same way, observed with that refusal instead.
    const char* refusal=relayTransportRefusal();
    EXPECT_EQ(refusal!=nullptr?refusal:"RELAY_TRANSPORT_UNAVAILABLE",status.error);EXPECT_EQ(0,status.sent);EXPECT_EQ(0,status.received);EXPECT_EQ(0,status.queued);
    bridge.stop();EXPECT_EQ(RelayTransportState::Stopped,bridge.status().state);
}

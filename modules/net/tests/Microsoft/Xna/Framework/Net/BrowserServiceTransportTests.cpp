// SPDX-License-Identifier: MS-PL
#ifdef __EMSCRIPTEN__
#include <gtest/gtest.h>
#include "CNA/GamerServices/Configuration.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/Internal/GamerServices/BackendConfiguration.hpp"
#include "../../../../../src/Internal/RelayWebSocket.hpp"
#include "CnaService/Protocol.hpp"

namespace {
namespace Deployment = CNA::GamerServices;
namespace Service = CNA::Internal::GamerServices;
namespace Net = CNA::Internal::Net;

TEST(BrowserServiceTransportTest, EmptyDeploymentIsUsableWithoutWorkerThreads) {
    Deployment::setConfigurationOverride(Deployment::Configuration{});
    Service::setBackendForTesting({});
    auto backend = Service::backend();
    EXPECT_FALSE(backend->serviceEnabled());
    EXPECT_TRUE(backend->pump().empty());
    Service::setBackendForTesting({});
    Deployment::setConfigurationOverride(std::nullopt);
}

TEST(BrowserServiceTransportTest, ConfiguredAuthorityIsExplicitlyRejected) {
    for (const auto* endpoint : {"https://service.example/cna/v1",
                                "http://127.0.0.1/cna/v1"}) {
        try {
            Deployment::validateConfiguration({endpoint, "browser-tests", {}, true});
            FAIL() << "Browser transport must not accept a deployment yet";
        } catch (const CnaService::Error& error) {
            EXPECT_EQ(error.code(), "BROWSER_SERVICE_TRANSPORT_UNAVAILABLE");
        }
    }
}

TEST(BrowserServiceTransportTest, UnavailableAuthenticationCannotProduceIdentity) {
    Deployment::setConfigurationOverride(Deployment::Configuration{});
    Service::setBackendForTesting({});
    auto backend = Service::backend();
    backend->signIn(0, "unavailable-test-user", "unavailable-test-password");
    const auto events = backend->pump();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events.front().type, Service::BackendEvent::Type::Failed);
    EXPECT_TRUE(events.front().identity.userId.empty());
    EXPECT_FALSE(backend->serviceEnabled());
    Service::setBackendForTesting({});
    Deployment::setConfigurationOverride(std::nullopt);
}

TEST(BrowserServiceTransportTest, RelayAuthorityFailsBeforeNetworkWork) {
    try {
        (void)Net::relayEndpoint({});
        FAIL() << "A browser relay must not be invented";
    } catch (const CnaService::RelayError& error) {
        EXPECT_EQ(error.code(), "BROWSER_RELAY_TRANSPORT_UNAVAILABLE");
    }
}

TEST(BrowserServiceTransportTest, RelayConstructionFailsBeforeNetworkWork) {
    try {
        Net::RelayWebSocket connection({}, {}, {});
        FAIL() << "Unsupported browser relay must refuse construction";
    } catch (const CnaService::RelayError& error) {
        EXPECT_EQ(error.code(), "BROWSER_RELAY_TRANSPORT_UNAVAILABLE");
    }
}
}
#endif

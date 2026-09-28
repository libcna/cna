// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/Internal/GamerServices/BackendConfiguration.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp"
#include <memory>

namespace {
namespace Service=CNA::Internal::GamerServices;
namespace Deployment=CNA::GamerServices;
using Unavailable=Microsoft::Xna::Framework::GamerServices::GamerServicesNotAvailableException;
class ServiceBackendAuthorityTest : public ::testing::Test {
protected:
    void SetUp() override {previous=Service::backend();}
    void TearDown() override {Service::setBackendForTesting(previous);Deployment::setConfigurationOverride({});}
    std::shared_ptr<Service::IGamerServicesBackend> configured(Deployment::Configuration value) {
        Deployment::setConfigurationOverride(std::move(value));Service::setBackendForTesting({});return Service::backend();
    }
    std::shared_ptr<Service::IGamerServicesBackend> previous;
};
}
TEST_F(ServiceBackendAuthorityTest, LaterDeploymentOverrideCannotRedirectAnExistingBackend) {
    const Deployment::Configuration first{"https://first.example/cna/v1","first"};
    const auto origin=configured(first);
    Deployment::setConfigurationOverride(Deployment::Configuration{"https://second.example/cna/v1","second","/tmp/unused-second-trust.pem"});
    EXPECT_EQ("second",Deployment::resolveConfiguration().gameId);
    const auto captured=Service::configurationForBackend(*origin);
    EXPECT_EQ(first.endpoint,captured.endpoint);EXPECT_EQ(first.gameId,captured.gameId);
    EXPECT_EQ(first.caBundle,captured.caBundle);EXPECT_FALSE(captured.insecureLoopback);
}
TEST_F(ServiceBackendAuthorityTest, ReplacementAndReturnedCopyDoNotMutateOriginalTitleAuthority) {
    const auto first=configured({"https://first.example/cna/v1","first"});
    const auto second=configured({"https://second.example/cna/v1","second"});
    auto copy=Service::configurationForBackend(*first);copy.endpoint="https://other.example/cna/v1";copy.gameId="other";
    EXPECT_EQ("first",Service::configurationForBackend(*first).gameId);
    EXPECT_EQ("https://first.example/cna/v1",Service::configurationForBackend(*first).endpoint);
    EXPECT_EQ("second",Service::configurationForBackend(*second).gameId);
}
TEST_F(ServiceBackendAuthorityTest, FakeAndUnconfiguredBackendsHaveNoImplicitNetworkAuthority) {
    const auto fake=Service::makeFakeBackend({});EXPECT_THROW((void)Service::configurationForBackend(*fake),Unavailable);
    const auto offline=configured({});EXPECT_THROW((void)Service::configurationForBackend(*offline),Unavailable);
}
TEST_F(ServiceBackendAuthorityTest, ExplicitLoopbackDevelopmentChoiceIsBoundToItsOrigin) {
    const auto origin=configured({"http://127.0.0.1:61199/cna/v1","development",{},true});
    Deployment::setConfigurationOverride(Deployment::Configuration{"https://second.example/cna/v1","second"});
    const auto captured=Service::configurationForBackend(*origin);
    EXPECT_EQ("http://127.0.0.1:61199/cna/v1",captured.endpoint);EXPECT_EQ("development",captured.gameId);
    EXPECT_TRUE(captured.insecureLoopback);
}

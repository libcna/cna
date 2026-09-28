// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/GamerServices/Configuration.hpp"
using namespace CNA::GamerServices;
TEST(ServiceConfigurationTest, AbsentEndpointIsValidOfflineConfiguration) {
    EXPECT_NO_THROW(validateConfiguration({}));
}
TEST(ServiceConfigurationTest, RequiresStableTitleAndSecureEndpoint) {
    EXPECT_THROW(validateConfiguration({"https://service.example/cna/v1",""}), std::runtime_error);
    EXPECT_THROW(validateConfiguration({"http://service.example/cna/v1","title",{},true}), std::runtime_error);
    EXPECT_NO_THROW(validateConfiguration({"https://service.example/cna/v1","title"}));
}
TEST(ServiceConfigurationTest, InsecureModeIsExplicitAndNumericLoopbackOnly) {
    EXPECT_THROW(validateConfiguration({"http://127.0.0.1/cna/v1","title"}), std::runtime_error);
    EXPECT_NO_THROW(validateConfiguration({"http://127.0.0.1/cna/v1","title",{},true}));
    EXPECT_NO_THROW(validateConfiguration({"http://[::1]/cna/v1","title",{},true}));
    EXPECT_THROW(validateConfiguration({"http://localhost/cna/v1","title",{},true}), std::runtime_error);
    EXPECT_THROW(validateConfiguration({"http://192.168.1.2/cna/v1","title",{},true}), std::runtime_error);
}
TEST(ServiceConfigurationTest, RejectsCredentialUrlsAndUnexpectedPaths) {
    for (const auto* url : {"https://user:password@service.example/cna/v1", "https://service.example/other", "https://service.example/cna/v1?token=secret", "https://service.example/cna/v1#fragment", "ftp://service.example/cna/v1"})
        EXPECT_THROW(validateConfiguration({url,"title"}), std::runtime_error);
}
TEST(ServiceConfigurationTest, ProgrammaticOverrideIsCompleteAndReversible) {
    setConfigurationOverride(Configuration{"https://service.example/cna/v1","title"});
    EXPECT_EQ(resolveConfiguration().gameId,"title");
    EXPECT_EQ(resolveConfiguration().endpoint,"https://service.example/cna/v1");
    setConfigurationOverride(std::nullopt);
}

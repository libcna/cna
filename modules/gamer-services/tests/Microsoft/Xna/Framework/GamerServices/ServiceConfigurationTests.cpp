// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/GamerServices/Configuration.hpp"
#include <cstdlib>
#include <string>
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
TEST(ServiceConfigurationTest, AvatarCatalogUpdatesAreOnByDefaultAndCanBeTurnedOff) {
    const Configuration defaults;
    EXPECT_TRUE(defaults.avatarCatalogUpdates);
    EXPECT_EQ(defaults.maxAvatarCatalogBytes, std::uint64_t{64} << 20);
    const auto* previous = std::getenv("CNA_AVATAR_CATALOG_UPDATES");
    const std::string saved = previous ? previous : "";
    setenv("CNA_AVATAR_CATALOG_UPDATES", "0", 1);
    EXPECT_FALSE(resolveConfiguration().avatarCatalogUpdates);
    setenv("CNA_AVATAR_CATALOG_UPDATES", "maybe", 1);
    EXPECT_THROW((void)resolveConfiguration(), std::runtime_error);
    if (previous) setenv("CNA_AVATAR_CATALOG_UPDATES", saved.c_str(), 1);
    else unsetenv("CNA_AVATAR_CATALOG_UPDATES");
}

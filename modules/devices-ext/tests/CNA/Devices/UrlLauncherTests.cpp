// SPDX-License-Identifier: MS-PL
#ifdef CNA_DEVICES

#include <gtest/gtest.h>

#include "CNA/Devices/UrlLauncher.hpp"
#include "CNA/Platform/CannedSystemInfo.hpp"

using CNA::Devices::UrlLauncher;

// URL handling is injected: no test invokes the host browser or xdg-open.

namespace
{
    class UrlLauncherTests : public ::testing::Test
    {
    protected:
        CNA::Platform::Testing::CannedSystemInfoPlatform platform;
        CNA::Platform::Testing::ScopedCurrentPlatform current{platform};
    };
}

TEST_F(UrlLauncherTests, OpenWithEmptyUrlDoesNotCrash)
{
    EXPECT_NO_THROW({ (void)UrlLauncher::Open(""); });
}

TEST_F(UrlLauncherTests, OpenWithWellFormedUrlDoesNotCrash)
{
    EXPECT_NO_THROW({ (void)UrlLauncher::Open("https://example.com"); });
}

TEST_F(UrlLauncherTests, OpenWithMalformedUrlDoesNotCrash)
{
    EXPECT_NO_THROW({ (void)UrlLauncher::Open("not a url at all"); });
}

TEST_F(UrlLauncherTests, ExactUrlAndProviderAcceptanceAreForwardedOnEveryCall)
{
    // Oracle: UrlLauncher.hpp: result means handler acceptance. URL policy belongs to the host.
    EXPECT_FALSE(UrlLauncher::Open("https://example.com/a%20b?q=1#section"));
    platform.acceptsUrl = true;
    EXPECT_TRUE(UrlLauncher::Open("mailto:test@example.com"));
    platform.acceptsUrl = false;
    EXPECT_FALSE(UrlLauncher::Open(""));
    EXPECT_EQ(platform.urls, (std::vector<std::string>{
        "https://example.com/a%20b?q=1#section", "mailto:test@example.com", ""}));
}

#endif // CNA_DEVICES

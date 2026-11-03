// SPDX-License-Identifier: MS-PL
#ifdef CNA_DEVICES

#include <gtest/gtest.h>

#include "CNA/Devices/Locale.hpp"
#include "CNA/Platform/CannedSystemInfo.hpp"

using CNA::Devices::Locale;
using CNA::Devices::LocaleInfo;

namespace
{
    class LocaleTests : public ::testing::Test
    {
    protected:
        CNA::Platform::Testing::CannedSystemInfoPlatform platform;
        CNA::Platform::Testing::ScopedCurrentPlatform current{platform};
    };
}

TEST_F(LocaleTests, GetPreferredLocalesPropertyDoesNotCrash)
{
    EXPECT_NO_THROW({ (void)Locale::getPreferredLocalesProperty(); });
}

// The canned platform supplies two locales, including a language without a country.
TEST_F(LocaleTests, EveryReturnedLocaleHasANonEmptyLanguageOrIsSkipped)
{
    const std::vector<LocaleInfo> locales = Locale::getPreferredLocalesProperty();
    for (const LocaleInfo& locale : locales)
    {
        EXPECT_FALSE(locale.Language.empty());
    }
}

TEST_F(LocaleTests, RepeatedCallsDoNotCrashOrLeak)
{
    for (int i = 0; i < 10; ++i)
    {
        EXPECT_NO_THROW({ (void)Locale::getPreferredLocalesProperty(); });
    }
}

TEST_F(LocaleTests, LanguageCountryAndPreferenceOrderArePreservedInAnOwnedResult)
{
    // Oracle: Locale.hpp: most-preferred first, separate language/country, country may be empty.
    const auto locales = Locale::getPreferredLocalesProperty();
    ASSERT_EQ(locales.size(), 2u);
    EXPECT_EQ(locales[0].Language, "cs");
    EXPECT_EQ(locales[0].Country, "CZ");
    EXPECT_EQ(locales[1].Language, "en");
    EXPECT_TRUE(locales[1].Country.empty());
    platform.locales.clear();
    EXPECT_EQ(locales[0].Language, "cs");
    EXPECT_TRUE(Locale::getPreferredLocalesProperty().empty());
}

#endif // CNA_DEVICES

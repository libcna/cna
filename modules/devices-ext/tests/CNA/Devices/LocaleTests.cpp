// SPDX-License-Identifier: MS-PL
#ifdef CNA_DEVICES

#include <gtest/gtest.h>

#include "CNA/Devices/Locale.hpp"

using CNA::Devices::Locale;
using CNA::Devices::LocaleInfo;

TEST(LocaleTests, GetPreferredLocalesPropertyDoesNotCrash)
{
    EXPECT_NO_THROW({ (void)Locale::getPreferredLocalesProperty(); });
}

// This Linux container is expected to report at least one locale (the C library's own
// locale environment always resolves to *something*, even if it is "C"/"POSIX" with no
// SDL3-recognized language) -- but this is not guaranteed by SDL3's own contract (an
// empty result is documented as possible), so this test only asserts on structure, not
// non-emptiness.
TEST(LocaleTests, EveryReturnedLocaleHasANonEmptyLanguageOrIsSkipped)
{
    const std::vector<LocaleInfo> locales = Locale::getPreferredLocalesProperty();
    for (const LocaleInfo& locale : locales)
    {
        // SDL3 documents `language` as always present for a returned entry (only
        // `country` may be empty/unset) -- this would only fail if SDL3's own
        // contract changed or this platform's locale backend is misbehaving.
        EXPECT_FALSE(locale.Language.empty());
    }
}

TEST(LocaleTests, RepeatedCallsDoNotCrashOrLeak)
{
    for (int i = 0; i < 10; ++i)
    {
        EXPECT_NO_THROW({ (void)Locale::getPreferredLocalesProperty(); });
    }
}

#endif // CNA_DEVICES

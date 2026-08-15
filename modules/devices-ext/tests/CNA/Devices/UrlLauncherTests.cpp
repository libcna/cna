// SPDX-License-Identifier: MS-PL
#ifdef CNA_DEVICES

#include <gtest/gtest.h>

#include "CNA/Devices/UrlLauncher.hpp"

using CNA::Devices::UrlLauncher;

// This headless container has no real browser/desktop session to observe launching,
// and no test here can verify a URL actually opened -- only that the call completes
// (does not crash or hang) and returns a bool, per the platform contract's documented
// contract ("a successful result does not mean the URL loaded, just that we launched
// *something* to handle it"). Honestly documenting this limitation rather than
// claiming more coverage than exists.

TEST(UrlLauncherTests, OpenWithEmptyUrlDoesNotCrash)
{
    EXPECT_NO_THROW({ (void)UrlLauncher::Open(""); });
}

TEST(UrlLauncherTests, OpenWithWellFormedUrlDoesNotCrash)
{
    // Deliberately does not assert on the return value: whether this container has
    // any URL handler at all (xdg-open, a browser, etc.) is environment-dependent and
    // not something this test controls or should assume either way.
    EXPECT_NO_THROW({ (void)UrlLauncher::Open("https://example.com"); });
}

TEST(UrlLauncherTests, OpenWithMalformedUrlDoesNotCrash)
{
    EXPECT_NO_THROW({ (void)UrlLauncher::Open("not a url at all"); });
}

#endif // CNA_DEVICES

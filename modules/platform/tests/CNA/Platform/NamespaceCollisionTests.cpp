// SPDX-License-Identifier: MS-PL
//
// A translation unit that includes both the build-target enum and the platform contract.
//
// These two spent a while unable to coexist. `CNA::TargetPlatform` was named `CNA::Platform`, and
// `modules/platform` introduced a *namespace* of the same name in the same scope. That is
// ill-formed — "redeclared as different kind of entity" — so any file including both headers
// failed to compile, and the build stayed green only because no file happened to do it. One
// `#include` in `FileDialog.cpp` would have found it.
//
// This file is the guard. It contains almost no test because it does not need to: if the names
// ever collide again, this translation unit stops compiling, which is the loudest possible signal
// and arrives at build time rather than in whichever unlucky file adds the include next.

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/PlatformCapability.hpp"
#include "CNA/Platform/PlatformEvent.hpp"
#include "CNA/TargetPlatform.hpp"

#include <gtest/gtest.h>

TEST(PlatformNamespaceCollisionTests, TheBuildTargetEnumAndThePlatformContractCoexist)
{
    // Both names are used, not merely included, so a future rename that leaves one of them
    // unreachable from here is also caught.
    constexpr CNA::TargetPlatform target = CNA::getCurrentPlatform();
    EXPECT_TRUE(target == CNA::TargetPlatform::Desktop || target == CNA::TargetPlatform::Android ||
                target == CNA::TargetPlatform::iOS || target == CNA::TargetPlatform::Web);

    EXPECT_FALSE(CNA::Platform::ToString(CNA::Platform::PlatformCapability::HighDpi).empty());
}

// SPDX-License-Identifier: MS-PL
//
// The ambient platform accessor that the static XNA API surface reaches through.

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

using namespace CNA::Platform;

class CurrentPlatformTest : public ::testing::Test
{
protected:
    void SetUp() override { previouslyInstalled_ = HasCurrentPlatform(); }

    void TearDown() override
    {
        // Process-wide state: leaving an installed platform behind would leak a dangling pointer
        // into every later test in this binary once the local instance is destroyed.
        SetCurrentPlatform(nullptr);
    }

    bool previouslyInstalled_ = false;
};

TEST_F(CurrentPlatformTest, AnInstalledPlatformIsWhatGetReturns)
{
    const std::unique_ptr<IPlatform> headless = PlatformFactory::Create("Headless");
    SetCurrentPlatform(headless.get());

    EXPECT_TRUE(HasCurrentPlatform());
    EXPECT_EQ(&GetCurrentPlatform(), headless.get());
    EXPECT_EQ(GetCurrentPlatform().GetName(), "Headless");
}

TEST_F(CurrentPlatformTest, InstallingOverridesTheLazyDefault)
{
    // The static API must follow whatever Game installed, not a default it created earlier on
    // first use -- otherwise storage and input would silently target different platforms.
    (void)GetCurrentPlatform();  // force the lazy default into existence

    const std::unique_ptr<IPlatform> headless = PlatformFactory::Create("Headless");
    SetCurrentPlatform(headless.get());
    EXPECT_EQ(&GetCurrentPlatform(), headless.get());
}

TEST_F(CurrentPlatformTest, ClearingFallsBackToTheDefaultRatherThanFailing)
{
    const std::unique_ptr<IPlatform> headless = PlatformFactory::Create("Headless");
    SetCurrentPlatform(headless.get());
    SetCurrentPlatform(nullptr);

    // XNA code calls StorageDevice with no ceremony at all, so the accessor has to keep working
    // after an install is withdrawn.
    EXPECT_NO_THROW((void)GetCurrentPlatform());
}

TEST_F(CurrentPlatformTest, GetIsStableAcrossCalls)
{
    // Callers may cache the reference; a different instance per call would split state.
    EXPECT_EQ(&GetCurrentPlatform(), &GetCurrentPlatform());
}

TEST_F(CurrentPlatformTest, HasCurrentDoesNotItselfCreateAPlatform)
{
    // Teardown paths need to distinguish "never created" from "created" without constructing one
    // while the process is shutting down.
    ResetCurrentPlatform();
    const bool before = HasCurrentPlatform();
    EXPECT_FALSE(before);
    EXPECT_FALSE(HasCurrentPlatform()) << "querying must not have created one";
}

} // namespace

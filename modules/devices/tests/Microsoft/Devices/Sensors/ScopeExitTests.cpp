// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <stdexcept>

#include "Microsoft/Devices/Sensors/Detail/PlatformSensorSubsystem.hpp"

using Microsoft::Devices::Sensors::Detail::MakeScopeExit;

// Task P7-5: ScopeExit is a small enough utility that it was previously only
// ever exercised indirectly, through PlatformSensorSubsystem's own dispatch
// cleanup — these tests exercise it directly, independent of any sensor
// class.

TEST(ScopeExitTests, RunsCleanupOnNormalScopeExit)
{
    bool ranCleanup = false;
    {
        auto guard = MakeScopeExit([&ranCleanup] { ranCleanup = true; });
        EXPECT_FALSE(ranCleanup);
    }
    EXPECT_TRUE(ranCleanup);
}

TEST(ScopeExitTests, RunsCleanupDuringExceptionUnwinding)
{
    bool ranCleanup = false;

    EXPECT_THROW(
        {
            auto guard = MakeScopeExit([&ranCleanup] { ranCleanup = true; });
            throw std::runtime_error("unwind test");
        },
        std::runtime_error);

    EXPECT_TRUE(ranCleanup);
}

// Task P7-5: a cleanup callable that itself throws must not let that
// exception escape the destructor — doing so while another exception is
// already propagating (or even on its own, from a destructor) would call
// std::terminate(). If this regresses, this test aborts the whole process
// rather than failing a clean assertion — matching this project's
// established pattern for testing "must not crash" invariants.
TEST(ScopeExitTests, SwallowsExceptionThrownByCleanupCallable)
{
    bool cleanupStarted = false;

    EXPECT_NO_THROW(
        {
            auto guard = MakeScopeExit([&cleanupStarted]
            {
                cleanupStarted = true;
                throw std::runtime_error("cleanup failure");
            });
        });

    EXPECT_TRUE(cleanupStarted);
}

TEST(ScopeExitTests, SwallowsCleanupExceptionEvenDuringUnwinding)
{
    bool cleanupStarted = false;

    EXPECT_THROW(
        {
            auto guard = MakeScopeExit([&cleanupStarted]
            {
                cleanupStarted = true;
                throw std::runtime_error("cleanup failure during unwind");
            });
            throw std::logic_error("primary exception");
        },
        std::logic_error);

    EXPECT_TRUE(cleanupStarted);
}

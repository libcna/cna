// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Devices/Detail/DevicesShutdownCoordinator.hpp"

using Microsoft::Devices::Detail::DevicesShutdownCoordinator;

namespace
{
    // Task SDLCORE-011 (2026-07-18, external audit
    // `audit_devices_2026-07-17.md`): DevicesShutdownCoordinator's state is
    // process-wide (static), not per-test -- every test must start from a
    // known "not shut down" state and clean up after itself, or an earlier
    // test's leftover shutdown flag would silently leak into a later,
    // unrelated test.
    class DevicesShutdownCoordinatorTest : public ::testing::Test
    {
    protected:
        void SetUp() override { DevicesShutdownCoordinator::ResetForTesting(); }
        void TearDown() override { DevicesShutdownCoordinator::ResetForTesting(); }
    };
} // namespace

TEST_F(DevicesShutdownCoordinatorTest, StartsNotShutDown)
{
    EXPECT_FALSE(DevicesShutdownCoordinator::IsShutdown());
}

TEST_F(DevicesShutdownCoordinatorTest, ShutdownMarksItShutDown)
{
    DevicesShutdownCoordinator::Shutdown();
    EXPECT_TRUE(DevicesShutdownCoordinator::IsShutdown());
}

TEST_F(DevicesShutdownCoordinatorTest, ShutdownIsIdempotent)
{
    DevicesShutdownCoordinator::Shutdown();
    EXPECT_NO_THROW(DevicesShutdownCoordinator::Shutdown());
    EXPECT_TRUE(DevicesShutdownCoordinator::IsShutdown());
}

TEST_F(DevicesShutdownCoordinatorTest, ResetForTestingReturnsToNotShutDown)
{
    DevicesShutdownCoordinator::Shutdown();
    ASSERT_TRUE(DevicesShutdownCoordinator::IsShutdown());

    DevicesShutdownCoordinator::ResetForTesting();

    EXPECT_FALSE(DevicesShutdownCoordinator::IsShutdown());
}

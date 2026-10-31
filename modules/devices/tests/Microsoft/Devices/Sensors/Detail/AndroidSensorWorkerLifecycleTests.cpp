// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include "Microsoft/Devices/Sensors/Detail/AndroidSensorWorkerLifecycle.hpp"

#include <thread>

using Microsoft::Devices::Sensors::Detail::AndroidSensorWorkerLifecycle;

namespace
{
    std::thread::id DistinctThreadId()
    {
        std::thread worker([] {});
        const std::thread::id id = worker.get_id();
        worker.join();
        return id;
    }
}

TEST(AndroidSensorWorkerLifecycleTests, InitialStateAcceptsExactlyOneStart)
{
    AndroidSensorWorkerLifecycle lifecycle;

    EXPECT_EQ(lifecycle.GetRunState(), AndroidSensorWorkerLifecycle::RunState::NotRunning);
    EXPECT_TRUE(lifecycle.TryBeginStart());
    EXPECT_EQ(lifecycle.GetRunState(), AndroidSensorWorkerLifecycle::RunState::Running);
    EXPECT_FALSE(lifecycle.TryBeginStart());
}

TEST(AndroidSensorWorkerLifecycleTests, SelfStopClaimsDetachAndBlocksRestartUntilConfirmedExit)
{
    AndroidSensorWorkerLifecycle lifecycle;
    ASSERT_TRUE(lifecycle.TryBeginStart());
    const std::thread::id workerId = DistinctThreadId();
    lifecycle.SetWorkerThreadId(workerId);

    const auto stop = lifecycle.RequestStop(workerId);
    EXPECT_TRUE(stop.IsWorkerThread);
    EXPECT_TRUE(stop.ShouldRequestStop);
    EXPECT_TRUE(stop.ShouldReclaimWorker);
    EXPECT_EQ(lifecycle.GetRunState(), AndroidSensorWorkerLifecycle::RunState::Stopping);
    EXPECT_FALSE(lifecycle.TryBeginStart());

    lifecycle.MarkExited();
    EXPECT_TRUE(lifecycle.TryBeginStart());
}

TEST(AndroidSensorWorkerLifecycleTests, SelfStopThenExternalStopNeverDoubleClaimsThreadObject)
{
    AndroidSensorWorkerLifecycle lifecycle;
    ASSERT_TRUE(lifecycle.TryBeginStart());
    const std::thread::id workerId = DistinctThreadId();
    lifecycle.SetWorkerThreadId(workerId);

    const auto selfStop = lifecycle.RequestStop(workerId);
    const auto externalStop = lifecycle.RequestStop(std::this_thread::get_id());

    EXPECT_TRUE(selfStop.ShouldReclaimWorker);
    EXPECT_FALSE(externalStop.ShouldRequestStop);
    EXPECT_FALSE(externalStop.ShouldReclaimWorker);
    EXPECT_FALSE(externalStop.IsWorkerThread);
}

TEST(AndroidSensorWorkerLifecycleTests, ExternalStopThenNestedSelfStopNeverDoubleClaimsThreadObject)
{
    AndroidSensorWorkerLifecycle lifecycle;
    ASSERT_TRUE(lifecycle.TryBeginStart());
    const std::thread::id workerId = DistinctThreadId();
    lifecycle.SetWorkerThreadId(workerId);

    const auto externalStop = lifecycle.RequestStop(std::this_thread::get_id());
    const auto nestedSelfStop = lifecycle.RequestStop(workerId);

    EXPECT_TRUE(externalStop.ShouldRequestStop);
    EXPECT_TRUE(externalStop.ShouldReclaimWorker);
    EXPECT_TRUE(nestedSelfStop.IsWorkerThread);
    EXPECT_FALSE(nestedSelfStop.ShouldRequestStop);
    EXPECT_FALSE(nestedSelfStop.ShouldReclaimWorker);
}

TEST(AndroidSensorWorkerLifecycleTests, NaturalExitRequiresThreadReclamationBeforeRestart)
{
    AndroidSensorWorkerLifecycle lifecycle;
    ASSERT_TRUE(lifecycle.TryBeginStart());
    lifecycle.SetWorkerThreadId(DistinctThreadId());

    lifecycle.MarkExited();
    EXPECT_FALSE(lifecycle.TryBeginStart());

    const auto reclaim = lifecycle.RequestStop(std::this_thread::get_id());
    EXPECT_FALSE(reclaim.ShouldRequestStop);
    EXPECT_TRUE(reclaim.ShouldReclaimWorker);
    EXPECT_TRUE(lifecycle.TryBeginStart());
}

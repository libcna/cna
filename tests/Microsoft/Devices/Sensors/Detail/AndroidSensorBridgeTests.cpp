// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.hpp"
#include "System/TimeSpan.hpp"

using Microsoft::Devices::Sensors::Detail::AndroidSensorBridge;
using Microsoft::Devices::Sensors::Detail::ConvertTimeBetweenUpdatesToSensorEventRateMicroseconds;
using System::TimeSpan;

// Task DEVICES-0081/0083: ConvertTimeBetweenUpdatesToSensorEventRateMicroseconds()
// is a pure function extracted specifically so this math is testable on any
// host, without a real Android sensor queue (the real bridge's Run() loop is
// #ifdef __ANDROID__-only and cannot run in this desktop container).

TEST(AndroidSensorBridgeTests, TwoMillisecondsConvertsToTwoThousandMicroseconds)
{
    EXPECT_EQ(ConvertTimeBetweenUpdatesToSensorEventRateMicroseconds(TimeSpan::FromMilliseconds(2.0)), 2000);
}

TEST(AndroidSensorBridgeTests, OneHundredMillisecondsConvertsToOneHundredThousandMicroseconds)
{
    EXPECT_EQ(ConvertTimeBetweenUpdatesToSensorEventRateMicroseconds(TimeSpan::FromMilliseconds(100.0)), 100000);
}

// TimeSpan::Zero must not produce 0 or a negative microsecond value — the
// NDK does not document defined behavior for either.
TEST(AndroidSensorBridgeTests, ZeroTimeBetweenUpdatesFloorsToOneMicrosecond)
{
    EXPECT_EQ(ConvertTimeBetweenUpdatesToSensorEventRateMicroseconds(TimeSpan::Zero), 1);
}

TEST(AndroidSensorBridgeTests, SubMicrosecondIntervalFloorsToOneMicrosecond)
{
    EXPECT_EQ(ConvertTimeBetweenUpdatesToSensorEventRateMicroseconds(TimeSpan::FromMilliseconds(0.0001)), 1);
}

// Task DEVICES-0074/0084: on this desktop (non-Android) build, every
// AndroidSensorBridge method must be a safe, inert no-op — no
// android/sensor.h or JNI dependency exists in this translation unit at
// all on this platform (confirmed separately by this file compiling and
// running here, and by DEVICES-0075's Android cross-compile + llvm-nm
// check confirming the real ASensorManager/ALooper symbols are pulled in
// only on that platform).

TEST(AndroidSensorBridgeTests, IsAvailableIsFalseOnNonAndroidPlatform)
{
    AndroidSensorBridge bridge(1 /* arbitrary sensor type */);
    EXPECT_FALSE(bridge.IsAvailable());
}

TEST(AndroidSensorBridgeTests, StartReturnsFalseOnNonAndroidPlatform)
{
    AndroidSensorBridge bridge(1);
    bool invoked = false;
    EXPECT_FALSE(bridge.Start(TimeSpan::FromMilliseconds(2.0), [&invoked](const auto&) { invoked = true; }));
    EXPECT_FALSE(invoked);
}

TEST(AndroidSensorBridgeTests, StopWithoutStartDoesNotCrash)
{
    AndroidSensorBridge bridge(1);
    EXPECT_NO_THROW(bridge.Stop());
}

TEST(AndroidSensorBridgeTests, DestructorWithoutStartDoesNotCrash)
{
    EXPECT_NO_THROW({ AndroidSensorBridge bridge(1); });
}

// Stabilization pass, Task 1 (repeated Start/Stop safety): on this
// (non-Android) host, Start() always returns false regardless of prior
// calls, so this cannot exercise the real fix (reassigning an
// already-joinable std::thread would only be reachable on Android, where
// Start() can actually succeed) -- but it locks in the API contract that
// calling Start() twice in a row is always safe to attempt, on every
// platform, and never crashes. The real Android code path was verified
// separately via a cross-compile + llvm-nm symbol check (not runtime-
// exercised -- no Android device in this environment).
TEST(AndroidSensorBridgeTests, StartTwiceInARowNeverCrashesOnNonAndroidPlatform)
{
    AndroidSensorBridge bridge(1);
    EXPECT_FALSE(bridge.Start(TimeSpan::FromMilliseconds(2.0), [](const auto&) {}));
    EXPECT_FALSE(bridge.Start(TimeSpan::FromMilliseconds(2.0), [](const auto&) {}));
}

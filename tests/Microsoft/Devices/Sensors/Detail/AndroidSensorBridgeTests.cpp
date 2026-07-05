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

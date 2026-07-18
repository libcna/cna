// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "Microsoft/Devices/Sensors/Detail/AndroidSensorBridge.hpp"
#include "System/TimeSpan.hpp"

using Microsoft::Devices::Sensors::Detail::AndroidSensorBridge;
using Microsoft::Devices::Sensors::Detail::ConvertTimeBetweenUpdatesToSensorEventRateMicroseconds;
using Microsoft::Devices::Sensors::Detail::GetValueCountForAndroidSensorType;
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

// Task SENSORBASE-008: setTimeBetweenUpdatesProperty() does not reject a
// negative TimeSpan (matches the real WP7 API's own undocumented/unvalidated
// setter contract, confirmed via MSDN hh220884/hh239315 — see
// SensorBaseTests.cpp's identical citation). A negative value can therefore
// reach this conversion function in practice; it must floor to the same safe
// 1-microsecond minimum as zero/sub-microsecond values, not produce a zero or
// negative argument to ASensorEventQueue_setEventRate() (undocumented
// behavior for either).
TEST(AndroidSensorBridgeTests, NegativeTimeBetweenUpdatesFloorsToOneMicrosecond)
{
    EXPECT_EQ(ConvertTimeBetweenUpdatesToSensorEventRateMicroseconds(TimeSpan::FromMilliseconds(-10.0)), 1);
}

// Micro-cleanup pass, Task 4: a requested interval converted to
// microseconds as a double can exceed what std::int32_t can represent
// (e.g. TimeSpan::MaxValue is on the order of 10^17 milliseconds) --
// static_cast-ing an out-of-range double to std::int32_t is undefined
// behavior, not a saturating truncation, so this must clamp explicitly
// rather than rely on the cast itself.
TEST(AndroidSensorBridgeTests, MaxValueTimeSpanClampsToInt32Max)
{
    EXPECT_EQ(
        ConvertTimeBetweenUpdatesToSensorEventRateMicroseconds(TimeSpan::MaxValue),
        std::numeric_limits<std::int32_t>::max());
}

TEST(AndroidSensorBridgeTests, HugeButNotMaxTimeSpanClampsToInt32Max)
{
    // 10^12 milliseconds (roughly 31,700 years) is comfortably beyond
    // INT32_MAX microseconds (~35.8 minutes) but far short of
    // TimeSpan::MaxValue -- checks the clamp triggers well before the
    // representable limit of TimeSpan itself, not only at the extreme end.
    EXPECT_EQ(
        ConvertTimeBetweenUpdatesToSensorEventRateMicroseconds(TimeSpan::FromMilliseconds(1e12)),
        std::numeric_limits<std::int32_t>::max());
}

// Task ANDROID-BRIDGE-001 (2026-07-06): GetValueCountForAndroidSensorType()
// -- confirmed against the vendored NDK's android/sensor.h directly (see the
// function's own doc comment for the exact citations), not guessed.

TEST(AndroidSensorBridgeTests, VectorSensorTypesReturnThreeValues)
{
    EXPECT_EQ(GetValueCountForAndroidSensorType(2), 3);  // ASENSOR_TYPE_MAGNETIC_FIELD
    EXPECT_EQ(GetValueCountForAndroidSensorType(9), 3);  // ASENSOR_TYPE_GRAVITY
    EXPECT_EQ(GetValueCountForAndroidSensorType(10), 3); // ASENSOR_TYPE_LINEAR_ACCELERATION
    EXPECT_EQ(GetValueCountForAndroidSensorType(4), 3);  // ASENSOR_TYPE_GYROSCOPE
}

TEST(AndroidSensorBridgeTests, RotationVectorSensorTypesReturnFiveValues)
{
    EXPECT_EQ(GetValueCountForAndroidSensorType(11), 5); // ASENSOR_TYPE_ROTATION_VECTOR
    EXPECT_EQ(GetValueCountForAndroidSensorType(15), 5); // ASENSOR_TYPE_GAME_ROTATION_VECTOR
}

TEST(AndroidSensorBridgeTests, UnrecognizedSensorTypeReturnsFullRawUnionSize)
{
    // ASENSOR_TYPE_ACCELEROMETER (1) is a real, valid NDK sensor type, but
    // this codebase's own Detail::AndroidSensorBridge users never construct
    // one with it (Accelerometer is SDL-backed, not NDK-backed) -- included
    // here specifically to confirm the fallback path, not to claim it as a
    // meaningfully-supported case.
    EXPECT_EQ(GetValueCountForAndroidSensorType(1), 16);
    EXPECT_EQ(GetValueCountForAndroidSensorType(-1), 16);
    EXPECT_EQ(GetValueCountForAndroidSensorType(9999), 16);
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

// Task ANDROID-BRIDGE-002: SetSampleInterval() on this (non-Android) host
// must be a safe, inert no-op, same discipline as every other method above
// — whether called before Start() (never started at all) or after a
// Start() call that itself always returns false on this platform.
TEST(AndroidSensorBridgeTests, SetSampleIntervalDoesNotCrashWhenNeverStarted)
{
    AndroidSensorBridge bridge(1);
    EXPECT_NO_THROW(bridge.SetSampleInterval(TimeSpan::FromMilliseconds(50.0)));
}

TEST(AndroidSensorBridgeTests, SetSampleIntervalDoesNotCrashAfterFailedStart)
{
    AndroidSensorBridge bridge(1);
    EXPECT_FALSE(bridge.Start(TimeSpan::FromMilliseconds(2.0), [](const auto&) {}));
    EXPECT_NO_THROW(bridge.SetSampleInterval(TimeSpan::FromMilliseconds(50.0)));
}

// Task ANDR2-009 (2026-07-17, external audit `audit_devices_2026-07-17.md`):
// on this (non-Android) host, GetDrainBatchLimitHitCountForTesting() always
// returns 0 -- there is no worker thread on this platform to ever hit
// Run()'s per-batch drain cap. The real Android drain-cap logic itself was
// verified separately via a cross-compile of AndroidSensorBridge.cpp (not
// runtime-exercised -- no Android device/emulator in this environment).
TEST(AndroidSensorBridgeTests, GetDrainBatchLimitHitCountForTestingIsZeroOnNonAndroidPlatform)
{
    const AndroidSensorBridge bridge(1);
    EXPECT_EQ(bridge.GetDrainBatchLimitHitCountForTesting(), 0);
}

TEST(AndroidSensorBridgeTests, GetDrainBatchLimitHitCountForTestingIsZeroAfterFailedStart)
{
    AndroidSensorBridge bridge(1);
    EXPECT_FALSE(bridge.Start(TimeSpan::FromMilliseconds(2.0), [](const auto&) {}));
    EXPECT_EQ(bridge.GetDrainBatchLimitHitCountForTesting(), 0);
}

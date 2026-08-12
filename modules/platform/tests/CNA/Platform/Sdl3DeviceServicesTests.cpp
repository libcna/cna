// SPDX-License-Identifier: MS-PL
//
// PLAT-84/85: the SDL3 haptics and sensor services.
//
// A build machine has no accelerometer and no rumble pack, so nothing here asserts that a
// reading arrives or that a motor spins. What it does assert is everything that must hold when
// the hardware is absent -- which is precisely the case these services get wrong in practice:
// an absent device must produce a determinate refusal or a false, never a phantom reading, a
// crash on an out-of-range index, or a leaked handle.

#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace CNA::Platform;

class Sdl3DeviceServicesTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const std::vector<std::string> available = PlatformFactory::GetAvailable();
        if (std::find(available.begin(), available.end(), "SDL3") == available.end())
        {
            GTEST_SKIP() << "built with CNA_PLATFORM != SDL3";
        }
        platform_ = PlatformFactory::Create("SDL3");
    }

    std::unique_ptr<IPlatform> platform_;
};

// --- the capability/service rule -------------------------------------------------------------

TEST_F(Sdl3DeviceServicesTest, BothServicesArePresentBecauseTheirCapabilitiesAreTrue)
{
    // The contract's central invariant: a service is null exactly when its capability is false.
    const PlatformCapabilities capabilities = platform_->GetCapabilities();
    EXPECT_EQ(platform_->GetSensors() != nullptr, capabilities.sensors);
    EXPECT_EQ(platform_->GetHaptics() != nullptr, capabilities.haptics);
}

// --- sensors (PLAT-85) ------------------------------------------------------------------------

TEST_F(Sdl3DeviceServicesTest, SensorAvailabilityIsAnswerableWithNoSensorSubsystemAcquired)
{
    // IsAvailable is the query a caller makes *before* deciding to acquire anything, so it must
    // not require the subsystem it is being used to decide about.
    IPlatformSensors* sensors = platform_->GetSensors();
    ASSERT_NE(sensors, nullptr);
    EXPECT_NO_THROW((void)sensors->IsAvailable(SensorKind::Accelerometer));
    EXPECT_NO_THROW((void)sensors->IsAvailable(SensorKind::Gyroscope));
}

TEST_F(Sdl3DeviceServicesTest, ReadingIsUnavailableBeforeStart)
{
    IPlatformSensors* sensors = platform_->GetSensors();
    ASSERT_NE(sensors, nullptr);

    SensorReading reading{};
    reading.x = 42.0f;
    EXPECT_FALSE(sensors->TryGetReading(SensorKind::Accelerometer, reading));
    // The out parameter is documented as untouched on false. A caller that ignores the return
    // value would otherwise act on whatever the service happened to leave behind.
    EXPECT_FLOAT_EQ(reading.x, 42.0f);
}

TEST_F(Sdl3DeviceServicesTest, StartingAnAbsentSensorRefusesNamingTheCapability)
{
    IPlatformSensors* sensors = platform_->GetSensors();
    ASSERT_NE(sensors, nullptr);

    if (sensors->IsAvailable(SensorKind::Gyroscope))
    {
        GTEST_SKIP() << "this machine has a gyroscope; the absent-device path cannot be exercised";
    }

    try
    {
        sensors->Start(SensorKind::Gyroscope);
        FAIL() << "starting a sensor the device does not have must refuse, not silently succeed";
    }
    catch (const PlatformNotSupportedException& exception)
    {
        // The refusal names *which* capability was missing, so a caller can branch on it rather
        // than string-matching a message.
        EXPECT_EQ(exception.GetCapability(), PlatformCapability::Sensors);
    }
}

TEST_F(Sdl3DeviceServicesTest, StoppingASensorThatWasNeverStartedIsANoOp)
{
    // Symmetric with ReleaseSubsystem: CNA has unconditional-teardown call sites, so an
    // unpaired Stop is tolerated by design.
    IPlatformSensors* sensors = platform_->GetSensors();
    ASSERT_NE(sensors, nullptr);
    EXPECT_NO_THROW(sensors->Stop(SensorKind::Accelerometer));
    EXPECT_NO_THROW(sensors->Stop(SensorKind::Accelerometer));
}

TEST_F(Sdl3DeviceServicesTest, StartingAnAvailableSensorTwiceIsIdempotent)
{
    IPlatformSensors* sensors = platform_->GetSensors();
    ASSERT_NE(sensors, nullptr);

    if (!sensors->IsAvailable(SensorKind::Accelerometer))
    {
        GTEST_SKIP() << "no accelerometer on this machine";
    }

    ASSERT_NO_THROW(sensors->Start(SensorKind::Accelerometer));
    // The second Start must not open a second handle -- that is how sensors leak.
    EXPECT_NO_THROW(sensors->Start(SensorKind::Accelerometer));
    sensors->Stop(SensorKind::Accelerometer);
}

TEST_F(Sdl3DeviceServicesTest, SensorReadingDefaultsAreZero)
{
    const SensorReading reading{};
    EXPECT_FLOAT_EQ(reading.x, 0.0f);
    EXPECT_FLOAT_EQ(reading.y, 0.0f);
    EXPECT_FLOAT_EQ(reading.z, 0.0f);
    EXPECT_EQ(reading.timestampNanoseconds, 0u);
}

// --- haptics (PLAT-84) ------------------------------------------------------------------------

TEST_F(Sdl3DeviceServicesTest, HapticCountIsNeverNegative)
{
    IPlatformHaptics* haptics = platform_->GetHaptics();
    ASSERT_NE(haptics, nullptr);
    EXPECT_GE(haptics->GetCount(), 0);
}

TEST_F(Sdl3DeviceServicesTest, HapticCountIsStableAcrossCallsWithNoHotplug)
{
    IPlatformHaptics* haptics = platform_->GetHaptics();
    ASSERT_NE(haptics, nullptr);
    EXPECT_EQ(haptics->GetCount(), haptics->GetCount());
}

TEST_F(Sdl3DeviceServicesTest, EveryOperationOnAnOutOfRangeIndexIsSafe)
{
    // Indices come from a count that changes on hotplug, so an out-of-range index is a normal
    // race, not a programming error. Each of these must answer, not crash and not throw.
    IPlatformHaptics* haptics = platform_->GetHaptics();
    ASSERT_NE(haptics, nullptr);

    for (const int index : {-1, haptics->GetCount(), haptics->GetCount() + 7, 4096})
    {
        EXPECT_EQ(haptics->GetName(index), std::string()) << "index " << index;
        EXPECT_FALSE(haptics->SupportsRumble(index)) << "index " << index;
        EXPECT_FALSE(haptics->PlayRumble(index, 1.0f, 10)) << "index " << index;
        EXPECT_FALSE(haptics->StopRumble(index)) << "index " << index;
    }
}

TEST_F(Sdl3DeviceServicesTest, OutOfRangeStrengthIsAcceptedRatherThanRejected)
{
    // Clamping, not validation: XNA's VibrateController clamps, and a throw here would turn a
    // rounding artefact in a caller's easing curve into a crash.
    IPlatformHaptics* haptics = platform_->GetHaptics();
    ASSERT_NE(haptics, nullptr);

    if (haptics->GetCount() == 0)
    {
        GTEST_SKIP() << "no haptic devices connected";
    }

    EXPECT_NO_THROW((void)haptics->PlayRumble(0, -5.0f, 1));
    EXPECT_NO_THROW((void)haptics->PlayRumble(0, 5.0f, 1));
    EXPECT_NO_THROW((void)haptics->StopRumble(0));
}

TEST_F(Sdl3DeviceServicesTest, StoppingADeviceThatWasNeverPlayedReportsFailureRatherThanThrowing)
{
    IPlatformHaptics* haptics = platform_->GetHaptics();
    ASSERT_NE(haptics, nullptr);

    if (haptics->GetCount() != 0)
    {
        GTEST_SKIP() << "a device is connected; the never-opened path cannot be exercised";
    }
    EXPECT_FALSE(haptics->StopRumble(0));
}

TEST_F(Sdl3DeviceServicesTest, ServicesOutliveRepeatedUseAndAreDestroyedCleanly)
{
    // The services cache open handles; this is the lifetime path ASan would flag if a cached
    // handle were closed twice or read after the platform went away.
    for (int i = 0; i < 3; ++i)
    {
        std::unique_ptr<IPlatform> platform = PlatformFactory::Create("SDL3");
        IPlatformHaptics* haptics = platform->GetHaptics();
        IPlatformSensors* sensors = platform->GetSensors();
        ASSERT_NE(haptics, nullptr);
        ASSERT_NE(sensors, nullptr);

        (void)haptics->GetCount();
        (void)haptics->GetName(0);
        (void)sensors->IsAvailable(SensorKind::Accelerometer);
        sensors->Stop(SensorKind::Accelerometer);
    }
    SUCCEED();
}

} // namespace

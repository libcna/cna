// SPDX-License-Identifier: MS-PL
//
// PLAT-77g: CNA::Input::Sensors reads the platform, not SDL.
//
// The canned readings used to come from an injected `ISystemSensorBackend`; they now come through
// `IPlatformSensors`. CI has no accelerometer and no gyroscope, so scripting the answer is the
// only way these paths are exercised at all — what changed is which seam does the scripting.

#include <gtest/gtest.h>

#include "CNA/Input/Sensors.hpp"
#include "CNA/Platform/CannedSensors.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <limits>
#include <memory>
#include <vector>

using CNA::Input::Sensors;
using CNA::Input::SensorInfoEXT;
using CNA::Input::SensorTypeEXT;
using CNA::Platform::SensorInfo;
using CNA::Platform::SensorKind;
using CNA::Platform::SensorReading;
using CNA::Platform::Testing::CannedSensorPlatform;
using CNA::Platform::Testing::ScopedCurrentPlatform;
using Microsoft::Xna::Framework::Vector3;

namespace
{
    class CnaInputSensorsTest : public ::testing::Test
    {
    protected:
        CannedSensorPlatform platform;
        std::unique_ptr<ScopedCurrentPlatform> installed;

        void SetUp() override { installed = std::make_unique<ScopedCurrentPlatform>(platform); }
        void TearDown() override { installed.reset(); }
    };
}

TEST_F(CnaInputSensorsTest, EnumerationForwardsSensorListWithTypes)
{
    platform.Canned().SetEnumeration({
        SensorInfo{1, SensorKind::Accelerometer, "Accelerometer"},
        SensorInfo{2, SensorKind::Gyroscope, "Gyroscope"},
    });

    const auto list = Sensors::GetSensorsEXT();
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0], (SensorInfoEXT{1, "Accelerometer", SensorTypeEXT::Accelerometer}));
    EXPECT_EQ(list[1].type, SensorTypeEXT::Gyroscope);
    EXPECT_EQ(platform.sensorSubsystemAcquisitions, 1);
    EXPECT_EQ(platform.sensorSubsystemBalance, 0);
}

TEST_F(CnaInputSensorsTest, EnumerationDropsIdsThePublicApiCannotRepresent)
{
    platform.Canned().SetEnumeration({
        SensorInfo{std::numeric_limits<std::uint32_t>::max(), SensorKind::Accelerometer, "last"},
        SensorInfo{static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1,
                   SensorKind::Gyroscope, "too-wide"},
    });

    const auto list = Sensors::GetSensorsEXT();
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].id, std::numeric_limits<std::uint32_t>::max());
    EXPECT_EQ(platform.sensorSubsystemBalance, 0);
}

TEST_F(CnaInputSensorsTest, EveryPlatformSensorKindMapsToItsExtensionCounterpart)
{
    // The sided variants are the reason the contract's SensorKind grew beyond two values:
    // collapsing a dual-sensor device's two accelerometers would make one of them unreachable.
    platform.Canned().SetEnumeration({
        SensorInfo{1, SensorKind::Accelerometer, "a"},
        SensorInfo{2, SensorKind::Gyroscope, "b"},
        SensorInfo{3, SensorKind::AccelerometerLeft, "c"},
        SensorInfo{4, SensorKind::GyroscopeLeft, "d"},
        SensorInfo{5, SensorKind::AccelerometerRight, "e"},
        SensorInfo{6, SensorKind::GyroscopeRight, "f"},
        SensorInfo{7, SensorKind::Unknown, "g"},
    });

    const auto list = Sensors::GetSensorsEXT();
    ASSERT_EQ(list.size(), 7u);
    EXPECT_EQ(list[0].type, SensorTypeEXT::Accelerometer);
    EXPECT_EQ(list[1].type, SensorTypeEXT::Gyroscope);
    EXPECT_EQ(list[2].type, SensorTypeEXT::AccelerometerLeft);
    EXPECT_EQ(list[3].type, SensorTypeEXT::GyroscopeLeft);
    EXPECT_EQ(list[4].type, SensorTypeEXT::AccelerometerRight);
    EXPECT_EQ(list[5].type, SensorTypeEXT::GyroscopeRight);
    EXPECT_EQ(list[6].type, SensorTypeEXT::Unknown);
}

TEST_F(CnaInputSensorsTest, AccelerometerAndGyroReadTheirSamplesWhenPresent)
{
    platform.Canned().Set(SensorKind::Accelerometer, SensorReading{0.0f, 9.81f, 0.0f, 0});
    platform.Canned().Set(SensorKind::Gyroscope, SensorReading{0.1f, -0.2f, 0.3f, 0});

    Vector3 accel;
    ASSERT_TRUE(Sensors::GetAccelerometerEXT(accel));
    EXPECT_EQ(accel, Vector3(0.0f, 9.81f, 0.0f));

    Vector3 gyro;
    ASSERT_TRUE(Sensors::GetGyroscopeEXT(gyro));
    EXPECT_EQ(gyro, Vector3(0.1f, -0.2f, 0.3f));

    EXPECT_EQ(platform.Canned().StartCount(SensorKind::Accelerometer), 1);
    EXPECT_EQ(platform.Canned().StopCount(SensorKind::Accelerometer), 1);
    EXPECT_FALSE(platform.Canned().IsStarted(SensorKind::Accelerometer));
    EXPECT_EQ(platform.Canned().StartCount(SensorKind::Gyroscope), 1);
    EXPECT_EQ(platform.Canned().StopCount(SensorKind::Gyroscope), 1);
    EXPECT_FALSE(platform.Canned().IsStarted(SensorKind::Gyroscope));
    EXPECT_EQ(platform.sensorSubsystemBalance, 0);
}

TEST_F(CnaInputSensorsTest, ReadsReturnFalseWhenSensorAbsent)
{
    Vector3 v(1.0f, 2.0f, 3.0f);
    EXPECT_FALSE(Sensors::GetAccelerometerEXT(v));
    EXPECT_FALSE(Sensors::GetGyroscopeEXT(v));
    EXPECT_EQ(v, Vector3(1.0f, 2.0f, 3.0f)) << "out-param left untouched when no sensor is read";
    EXPECT_EQ(platform.sensorSubsystemAcquisitions, 2);
    EXPECT_EQ(platform.sensorSubsystemBalance, 0);
}

TEST_F(CnaInputSensorsTest, AnAbsentSensorIsRefusedBeforeStartingRatherThanThrowing)
{
    // The platform's Start() throws for a sensor the device does not have. These accessors report
    // "no such sensor" with a false, as they always have, so an absent accelerometer must be
    // detected before the start rather than caught after it -- a game polling motion on a desktop
    // would otherwise take an exception every frame.
    platform.Canned().Set(SensorKind::Gyroscope, SensorReading{});

    Vector3 v;
    EXPECT_NO_THROW((void)Sensors::GetAccelerometerEXT(v));
    EXPECT_FALSE(Sensors::GetAccelerometerEXT(v));
    EXPECT_EQ(platform.Canned().StartCount(SensorKind::Accelerometer), 0);
    EXPECT_EQ(platform.sensorSubsystemBalance, 0);
}

TEST_F(CnaInputSensorsTest, RepeatedReadsKeepWorkingAndStayStateless)
{
    // These accessors look stateless to their caller and start the sensor on every read. Caching
    // "already started" would need an identity for the service, and the only one available is its
    // address -- which a destroyed platform hands back to the next one, so the cache would claim
    // a sensor was started on a service that has never seen it. This test exists because that
    // cache was written, and the suite caught it: the same test passed alone and failed in the
    // group, on a recycled address.
    platform.Canned().Set(SensorKind::Accelerometer, SensorReading{1.0f, 2.0f, 3.0f, 0});

    Vector3 v;
    for (int i = 0; i < 5; ++i)
    {
        ASSERT_TRUE(Sensors::GetAccelerometerEXT(v)) << "read " << i;
        EXPECT_EQ(v, Vector3(1.0f, 2.0f, 3.0f)) << "read " << i;
    }
    EXPECT_EQ(platform.Canned().StartCount(SensorKind::Accelerometer), 5);
    EXPECT_EQ(platform.Canned().StopCount(SensorKind::Accelerometer), 5);
    EXPECT_FALSE(platform.Canned().IsStarted(SensorKind::Accelerometer));
    EXPECT_EQ(platform.sensorSubsystemAcquisitions, 5);
    EXPECT_EQ(platform.sensorSubsystemBalance, 0);
}

TEST_F(CnaInputSensorsTest, EnumerationIsEmptyWhenThePlatformHasNoSensorService)
{
    // HEADLESS returns null from GetSensors(). The old backend was always present, so this branch
    // did not exist before the migration and has to be covered now.
    installed.reset();
    EXPECT_NO_THROW((void)Sensors::GetSensorsEXT());

    Vector3 v;
    EXPECT_NO_THROW((void)Sensors::GetAccelerometerEXT(v));
    EXPECT_NO_THROW((void)Sensors::GetGyroscopeEXT(v));
}

TEST(CnaInputSensorInfoEXTTest, EqualityComparesIdNameAndType)
{
    EXPECT_EQ((SensorInfoEXT{1, "a", SensorTypeEXT::Accelerometer}),
              (SensorInfoEXT{1, "a", SensorTypeEXT::Accelerometer}));
    EXPECT_NE((SensorInfoEXT{1, "a", SensorTypeEXT::Accelerometer}),
              (SensorInfoEXT{1, "a", SensorTypeEXT::Gyroscope}));
}

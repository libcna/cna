// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include "CNA/Platform/CannedSensors.hpp"
#include "Microsoft/Devices/Sensors/Accelerometer.hpp"
#include "Microsoft/Devices/Sensors/AccelerometerReading.hpp"
#include "Microsoft/Devices/Sensors/Gyroscope.hpp"
#include "Microsoft/Devices/Sensors/GyroscopeReading.hpp"
#include "Microsoft/Devices/Sensors/SensorReadingEventArgs.hpp"

#include <memory>

namespace
{
    using CNA::Platform::SensorKind;
    using CNA::Platform::SensorReading;
    using CNA::Platform::Testing::CannedSensorPlatform;
    using CNA::Platform::Testing::ScopedCurrentPlatform;
    using Microsoft::Devices::Sensors::Accelerometer;
    using Microsoft::Devices::Sensors::AccelerometerReading;
    using Microsoft::Devices::Sensors::Gyroscope;
    using Microsoft::Devices::Sensors::GyroscopeReading;
    using Microsoft::Devices::Sensors::SensorReadingEventArgs;
}

TEST(PlatformSensorMigrationTests, AccelerometerUsesCannedPlatformSessionAndBalancesSubsystem)
{
    CannedSensorPlatform platform;
    platform.Canned().Set(SensorKind::Accelerometer,
                          SensorReading{9.80665f, 19.6133f, -9.80665f, 123});
    ScopedCurrentPlatform current(platform);

    Accelerometer accelerometer;
    int callbacks = 0;
    AccelerometerReading received;
    accelerometer.CurrentValueChanged +=
        [&](System::Object*, const SensorReadingEventArgs<AccelerometerReading>& event)
        {
            ++callbacks;
            received = event.getSensorReadingProperty();
        };

    ASSERT_TRUE(Accelerometer::getIsSupportedProperty());
    accelerometer.Start();
    EXPECT_EQ(platform.sensorSubsystemBalance, 1);
    EXPECT_EQ(platform.sensorSubsystemAcquisitions, 3);

    platform.Canned().Dispatch(SensorKind::Accelerometer);

    EXPECT_EQ(callbacks, 1);
    EXPECT_FLOAT_EQ(received.getAccelerationProperty().X, 1.0f);
    EXPECT_FLOAT_EQ(received.getAccelerationProperty().Y, 2.0f);
    EXPECT_FLOAT_EQ(received.getAccelerationProperty().Z, -1.0f);

    accelerometer.Stop();
    platform.Canned().Dispatch(SensorKind::Accelerometer);
    EXPECT_EQ(callbacks, 1);
    accelerometer.Dispose();
    EXPECT_EQ(platform.sensorSubsystemBalance, 0);
}

TEST(PlatformSensorMigrationTests, GyroscopeUsesCannedPlatformSessionAndBalancesSubsystem)
{
    CannedSensorPlatform platform;
    platform.Canned().Set(SensorKind::Gyroscope, SensorReading{1.5f, -2.0f, 3.25f, 456});
    ScopedCurrentPlatform current(platform);

    Gyroscope gyroscope;
    int callbacks = 0;
    GyroscopeReading received;
    gyroscope.CurrentValueChanged +=
        [&](System::Object*, const SensorReadingEventArgs<GyroscopeReading>& event)
        {
            ++callbacks;
            received = event.getSensorReadingProperty();
        };

    gyroscope.Start();
    EXPECT_EQ(platform.sensorSubsystemBalance, 1);
    platform.Canned().Dispatch(SensorKind::Gyroscope);

    EXPECT_EQ(callbacks, 1);
    EXPECT_FLOAT_EQ(received.getRotationRateProperty().X, 1.5f);
    EXPECT_FLOAT_EQ(received.getRotationRateProperty().Y, -2.0f);
    EXPECT_FLOAT_EQ(received.getRotationRateProperty().Z, 3.25f);

    gyroscope.Dispose();
    EXPECT_EQ(platform.sensorSubsystemBalance, 0);
}

TEST(PlatformSensorMigrationTests, TwoInstancesShareOneStreamWithoutCrossStopping)
{
    CannedSensorPlatform platform;
    platform.Canned().Set(SensorKind::Accelerometer, SensorReading{9.80665f, 0.0f, 0.0f, 1});
    ScopedCurrentPlatform current(platform);

    Accelerometer first;
    Accelerometer second;
    int firstCallbacks = 0;
    int secondCallbacks = 0;
    first.CurrentValueChanged += [&](System::Object*, const auto&) { ++firstCallbacks; };
    second.CurrentValueChanged += [&](System::Object*, const auto&) { ++secondCallbacks; };
    first.setTimeBetweenUpdatesProperty(System::TimeSpan::Zero);
    second.setTimeBetweenUpdatesProperty(System::TimeSpan::Zero);

    first.Start();
    second.Start();
    EXPECT_EQ(platform.sensorSubsystemBalance, 2);
    platform.Canned().Dispatch(SensorKind::Accelerometer);
    EXPECT_EQ(firstCallbacks, 1);
    EXPECT_EQ(secondCallbacks, 1);

    first.Stop();
    platform.Canned().Dispatch(SensorKind::Accelerometer);
    EXPECT_EQ(firstCallbacks, 1);
    EXPECT_EQ(secondCallbacks, 2);

    first.Dispose();
    second.Dispose();
    EXPECT_EQ(platform.sensorSubsystemBalance, 0);
}

TEST(PlatformSensorMigrationTests, HandlerMayDisposeItsOwnSensorWithoutDeadlock)
{
    CannedSensorPlatform platform;
    platform.Canned().Set(SensorKind::Accelerometer, SensorReading{9.80665f, 0.0f, 0.0f, 1});
    ScopedCurrentPlatform current(platform);

    auto accelerometer = std::make_unique<Accelerometer>();
    bool disposedFromHandler = false;
    accelerometer->CurrentValueChanged +=
        [&](System::Object*, const auto&)
        {
            accelerometer->Dispose();
            disposedFromHandler = true;
        };

    accelerometer->Start();
    platform.Canned().Dispatch(SensorKind::Accelerometer);

    EXPECT_TRUE(disposedFromHandler);
    EXPECT_EQ(platform.sensorSubsystemBalance, 0);
    accelerometer.reset();
}

// SPDX-License-Identifier: MS-PL

#include "CNA/Platform/CannedSensors.hpp"

#include <gtest/gtest.h>

#include <memory>

namespace
{
    using namespace CNA::Platform;
    using CNA::Platform::Testing::CannedSensors;
}

TEST(SensorSessionContractTests, SessionsOfTheSameKindAreIndependent)
{
    CannedSensors sensors;
    sensors.Set(SensorKind::Accelerometer, SensorReading{1.0f, 2.0f, 3.0f, 4});
    int firstCalls = 0;
    int secondCalls = 0;

    auto first = sensors.OpenSensor(SensorKind::Accelerometer,
                                    [&](const SensorReading&) { ++firstCalls; });
    auto second = sensors.OpenSensor(SensorKind::Accelerometer,
                                     [&](const SensorReading&) { ++secondCalls; });
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    sensors.Dispatch(SensorKind::Accelerometer);
    EXPECT_EQ(firstCalls, 1);
    EXPECT_EQ(secondCalls, 1);

    first.reset();
    sensors.Dispatch(SensorKind::Accelerometer);
    EXPECT_EQ(firstCalls, 1);
    EXPECT_EQ(secondCalls, 2);
}

TEST(SensorSessionContractTests, PollingObservesTheLatestReading)
{
    CannedSensors sensors;
    sensors.Set(SensorKind::Gyroscope, SensorReading{1.0f, 2.0f, 3.0f, 4});
    auto session = sensors.OpenSensor(SensorKind::Gyroscope, {});
    ASSERT_NE(session, nullptr);

    sensors.Set(SensorKind::Gyroscope, SensorReading{5.0f, 6.0f, 7.0f, 8});
    SensorReading reading;
    ASSERT_TRUE(session->TryGetReading(reading));
    EXPECT_FLOAT_EQ(reading.x, 5.0f);
    EXPECT_EQ(reading.timestampNanoseconds, 8u);
}

TEST(SensorSessionContractTests, CallbackMayDestroyItsOwnSession)
{
    CannedSensors sensors;
    sensors.Set(SensorKind::Accelerometer, SensorReading{});
    std::unique_ptr<IPlatformSensorSession> session;
    bool called = false;
    session = sensors.OpenSensor(SensorKind::Accelerometer,
        [&](const SensorReading&)
        {
            called = true;
            session.reset();
        });

    sensors.Dispatch(SensorKind::Accelerometer);
    EXPECT_TRUE(called);
    EXPECT_EQ(session, nullptr);
    EXPECT_NO_THROW(sensors.Dispatch(SensorKind::Accelerometer));
}

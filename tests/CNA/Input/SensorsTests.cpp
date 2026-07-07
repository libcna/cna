// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Input/Sensors.hpp"
#include "CNA/Internal/Input/SystemSensorBackend.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <map>
#include <vector>

using CNA::Input::Sensors;
using CNA::Input::SensorInfoEXT;
using CNA::Input::SensorTypeEXT;
using CNA::Internal::Input::ISystemSensorBackend;
using CNA::Internal::Input::SetSystemSensorBackendForTests;
using Microsoft::Xna::Framework::Vector3;

namespace
{
    // A fake sensor source, so Sensors is exercised deterministically (CI has no motion sensors).
    class FakeSystemSensorBackend final : public ISystemSensorBackend
    {
    public:
        std::vector<SensorInfoEXT> sensors;
        std::map<SensorTypeEXT, Vector3> samples; // present type -> its canned reading

        std::vector<SensorInfoEXT> GetSensors() override { return sensors; }

        bool ReadSensor(SensorTypeEXT type, Vector3& out) override
        {
            const auto it = samples.find(type);
            if (it == samples.end())
                return false;
            out = it->second;
            return true;
        }
    };

    class CnaInputSensorsTest : public ::testing::Test
    {
    protected:
        FakeSystemSensorBackend fake;
        void SetUp() override { SetSystemSensorBackendForTests(&fake); }
        void TearDown() override { SetSystemSensorBackendForTests(nullptr); }
    };
}

TEST_F(CnaInputSensorsTest, EnumerationForwardsSensorList)
{
    fake.sensors = {
        {1, "Accelerometer", SensorTypeEXT::Accelerometer},
        {2, "Gyroscope", SensorTypeEXT::Gyroscope},
    };
    const auto list = Sensors::GetSensorsEXT();
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0], (SensorInfoEXT{1, "Accelerometer", SensorTypeEXT::Accelerometer}));
    EXPECT_EQ(list[1].type, SensorTypeEXT::Gyroscope);
}

TEST_F(CnaInputSensorsTest, AccelerometerAndGyroReadTheirSamplesWhenPresent)
{
    fake.samples[SensorTypeEXT::Accelerometer] = Vector3(0.0f, 9.81f, 0.0f);
    fake.samples[SensorTypeEXT::Gyroscope] = Vector3(0.1f, -0.2f, 0.3f);

    Vector3 accel;
    ASSERT_TRUE(Sensors::GetAccelerometerEXT(accel));
    EXPECT_EQ(accel, Vector3(0.0f, 9.81f, 0.0f));

    Vector3 gyro;
    ASSERT_TRUE(Sensors::GetGyroscopeEXT(gyro));
    EXPECT_EQ(gyro, Vector3(0.1f, -0.2f, 0.3f));
}

TEST_F(CnaInputSensorsTest, ReadsReturnFalseWhenSensorAbsent)
{
    // No samples registered -> both readers report "no such sensor".
    Vector3 v(1.0f, 2.0f, 3.0f);
    EXPECT_FALSE(Sensors::GetAccelerometerEXT(v));
    EXPECT_FALSE(Sensors::GetGyroscopeEXT(v));
    EXPECT_EQ(v, Vector3(1.0f, 2.0f, 3.0f)) << "out-param left untouched when no sensor is read";
}

TEST(CnaInputSensorInfoEXTTest, EqualityComparesIdNameAndType)
{
    EXPECT_EQ((SensorInfoEXT{1, "a", SensorTypeEXT::Accelerometer}),
              (SensorInfoEXT{1, "a", SensorTypeEXT::Accelerometer}));
    EXPECT_NE((SensorInfoEXT{1, "a", SensorTypeEXT::Accelerometer}),
              (SensorInfoEXT{1, "a", SensorTypeEXT::Gyroscope}));
}

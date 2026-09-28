// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/Platform/Input/KeyboardAccelerometer.hpp"
#include "CNA/Input/Sensors.hpp"
#include "Microsoft/Devices/Sensors/Accelerometer.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"
#include <chrono>
#include <future>
#include <cmath>

using namespace CNA::Platform;
using namespace Microsoft::Devices::Sensors;
namespace
{
    class KeyboardAccelerometerTest : public ::testing::Test
    {
    protected:
        void TearDown() override { Accelerometer::setKeyboardEmulationEnabledEXT(false); }
    };
}
TEST_F(KeyboardAccelerometerTest, PreconstructedObjectUsesNormalEventsAndUnits)
{
    EXPECT_FALSE(Accelerometer::getKeyboardEmulationEnabledEXT());
    Accelerometer sensor;
    Accelerometer::setKeyboardEmulationEnabledEXT(true);
    EXPECT_TRUE(Accelerometer::getIsSupportedProperty());
    sensor.setTimeBetweenUpdatesProperty(System::TimeSpan::Zero);
    std::vector<int> order;
    sensor.CurrentValueChanged += [&](System::Object*, const SensorReadingEventArgs<AccelerometerReading>&)
        { order.push_back(1); };
    sensor.ReadingChanged += [&](System::Object*, const AccelerometerReadingEventArgs&)
        { order.push_back(2); };
    sensor.Start();
    KeyboardAccelerometer::Update({{KeyCode::Right, KeyCode::Up}, 0}, true);
    EXPECT_TRUE(sensor.getIsDataValidProperty());
    const auto value = sensor.getCurrentValueProperty().getAccelerationProperty();
    EXPECT_NEAR(value.X, 1.0f/std::sqrt(3.0f), 0.00001f);
    EXPECT_NEAR(value.Y, value.X, 0.00001f);
    EXPECT_NEAR(value.Z, -value.X, 0.00001f);
    EXPECT_EQ(order, (std::vector<int>{1, 2}));
    EXPECT_GT(sensor.getCurrentValueProperty().getTimestampProperty().getUtcDateTimeProperty().getTicksProperty(), 0);
    EXPECT_THROW(Accelerometer::setKeyboardEmulationEnabledEXT(false), System::InvalidOperationException);
    KeyboardAccelerometer::Update({{KeyCode::Left, KeyCode::Right, KeyCode::Down, KeyCode::Up}, 0}, true);
    EXPECT_NEAR(sensor.getCurrentValueProperty().getAccelerationProperty().Z, -1, 0.00001f);
    KeyboardAccelerometer::Update({{KeyCode::Right}, 0}, false);
    EXPECT_EQ(sensor.getCurrentValueProperty().getAccelerationProperty().X, 0);
    sensor.Stop();
    const auto count = order.size();
    KeyboardAccelerometer::Update({}, true);
    EXPECT_EQ(order.size(), count);
    Accelerometer::setKeyboardEmulationEnabledEXT(false);
}
TEST_F(KeyboardAccelerometerTest, IndependentThrottleRestartAndReentrantDisposal)
{
    Accelerometer::setKeyboardEmulationEnabledEXT(true);
    Accelerometer fast, slow;
    fast.setTimeBetweenUpdatesProperty(System::TimeSpan::Zero);
    slow.setTimeBetweenUpdatesProperty(System::TimeSpan::FromSeconds(60));
    int fastCount = 0, slowCount = 0;
    fast.CurrentValueChanged += [&](System::Object*, const SensorReadingEventArgs<AccelerometerReading>&) { ++fastCount; };
    slow.CurrentValueChanged += [&](System::Object*, const SensorReadingEventArgs<AccelerometerReading>&) { ++slowCount; };
    fast.Start(); slow.Start();
    KeyboardAccelerometer::Update({}, true);
    KeyboardAccelerometer::Update({{KeyCode::Down}, 0}, true);
    EXPECT_EQ(fastCount, 2); EXPECT_EQ(slowCount, 1);
    slow.Stop(); slow.Start();
    KeyboardAccelerometer::Update({}, true);
    EXPECT_EQ(slowCount, 2);
    fast.Stop(); slow.Stop();
    Accelerometer reentrant;
    reentrant.CurrentValueChanged += [&](System::Object*, const SensorReadingEventArgs<AccelerometerReading>&) { reentrant.Dispose(); };
    reentrant.Start();
    EXPECT_NO_THROW(KeyboardAccelerometer::Update({}, true));
    EXPECT_THROW(reentrant.Start(), System::ObjectDisposedException);
}
TEST_F(KeyboardAccelerometerTest, PollingAndEnumerationUseSameSoftwareSensor)
{
    Accelerometer::setKeyboardEmulationEnabledEXT(true);
    KeyboardAccelerometer::Update({{KeyCode::Left}, 0}, true);
    Microsoft::Xna::Framework::Vector3 raw;
    ASSERT_TRUE(CNA::Input::Sensors::GetAccelerometerEXT(raw));
    EXPECT_NEAR(raw.X, -9.80665f/std::sqrt(2.0f), 0.00001f);
    bool found = false;
    for (const auto& info : CNA::Input::Sensors::GetSensorsEXT())
        if (info.name == "Keyboard-emulated accelerometer") found = true;
    EXPECT_TRUE(found);
    auto a = KeyboardAccelerometer::GetSensors().OpenSensor(SensorKind::Accelerometer, {});
    auto b = KeyboardAccelerometer::GetSensors().OpenSensor(SensorKind::Accelerometer, {});
    a.reset();
    SensorReading reading;
    EXPECT_TRUE(b->TryGetReading(reading));
    EXPECT_THROW(KeyboardAccelerometer::SetEnabled(false), System::InvalidOperationException);
    b.reset();
}
TEST_F(KeyboardAccelerometerTest, ClosingSessionWaitsForOtherThreadCallback)
{
    Accelerometer::setKeyboardEmulationEnabledEXT(true);
    std::promise<void> entered, release;
    auto wait = release.get_future().share();
    auto session = KeyboardAccelerometer::GetSensors().OpenSensor(SensorKind::Accelerometer,
        [&](const SensorReading&) { entered.set_value(); wait.wait(); });
    auto update = std::async(std::launch::async, [] { KeyboardAccelerometer::Update({}, true); });
    entered.get_future().wait();
    auto close = std::async(std::launch::async, [&] { session.reset(); });
    EXPECT_EQ(close.wait_for(std::chrono::milliseconds(30)), std::future_status::timeout);
    release.set_value();
    EXPECT_EQ(close.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    update.get(); close.get();
}

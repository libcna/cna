// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <string>

#include "Microsoft/Devices/Sensors/SensorBase.hpp"
#include "System/DateTimeOffset.hpp"
#include "System/EventArgs.hpp"
#include "System/Object.hpp"
#include "System/TimeSpan.hpp"

using Microsoft::Devices::Sensors::ISensorReading;
using Microsoft::Devices::Sensors::SensorBase;
using Microsoft::Devices::Sensors::SensorReadingEventArgs;
using System::DateTimeOffset;
using System::TimeSpan;

// Task P6-5: the audit found SensorBase<T>'s TimeBetweenUpdates default-init
// path and CurrentValueChanged's update-then-notify order were both correct,
// but had zero test coverage anywhere — every existing test only exercises
// SensorBase<T> indirectly through a concrete sensor class (Accelerometer/
// Gyroscope/Compass/Motion), none of which ever change TimeBetweenUpdates.
// This file tests SensorBase<T> directly via a minimal concrete subclass.
namespace
{
    class TestSensorReading final : public ISensorReading
    {
    public:
        explicit TestSensorReading(int value = 0)
            : value_(value)
        {
        }

        [[nodiscard]] int getValue() const
        {
            return value_;
        }

        [[nodiscard]] const DateTimeOffset& getTimestampProperty() const override
        {
            return timestamp_;
        }

    private:
        int value_;
        DateTimeOffset timestamp_;
    };

    // TimeBetweenUpdatesChanged is protected in the real WP7 API (see
    // SensorBase.hpp), so it cannot be subscribed to from a plain TEST()
    // function — a derived class can, which is the standard C++ technique
    // used here rather than adding new NOXNA test hooks to the public API.
    class TestSensorBase final : public SensorBase<TestSensorReading>
    {
    public:
        TestSensorBase()
        {
            TimeBetweenUpdatesChanged += [this](System::Object*, const System::EventArgs&)
            {
                ++timeBetweenUpdatesChangedCount;
            };
        }

        void Start() override
        {
        }

        void Stop() override
        {
        }

        [[nodiscard]] const std::string& GetTypeName() const override
        {
            static const std::string name = "TestSensorBase";
            return name;
        }

        void SetCurrentValueForTesting(const TestSensorReading& value)
        {
            setCurrentValueProperty(value);
        }

        void SetTimeBetweenUpdatesForTesting(const TimeSpan& value)
        {
            setTimeBetweenUpdatesProperty(value);
        }

        void SetSupportedForTesting(bool value)
        {
            setIsSupportedProperty(value);
        }

        int timeBetweenUpdatesChangedCount = 0;
    };
} // namespace

TEST(SensorBaseTests, DefaultTimeBetweenUpdatesIsTwoMilliseconds)
{
    const TestSensorBase sensor;
    EXPECT_EQ(sensor.getTimeBetweenUpdatesProperty(), TimeSpan::FromMilliseconds(2.0));
}

TEST(SensorBaseTests, SetTimeBetweenUpdatesPropertyToNewValueChangesGetterAndRaisesEvent)
{
    TestSensorBase sensor;

    sensor.SetTimeBetweenUpdatesForTesting(TimeSpan::FromMilliseconds(10.0));

    EXPECT_EQ(sensor.getTimeBetweenUpdatesProperty(), TimeSpan::FromMilliseconds(10.0));
    EXPECT_EQ(sensor.timeBetweenUpdatesChangedCount, 1);
}

TEST(SensorBaseTests, SetTimeBetweenUpdatesPropertyToSameValueDoesNotRaiseEvent)
{
    TestSensorBase sensor;
    const TimeSpan current = sensor.getTimeBetweenUpdatesProperty();

    sensor.SetTimeBetweenUpdatesForTesting(current);

    EXPECT_EQ(sensor.timeBetweenUpdatesChangedCount, 0);
}

TEST(SensorBaseTests, RepeatedSetTimeBetweenUpdatesPropertyToSameValueRaisesOnlyOnce)
{
    TestSensorBase sensor;

    sensor.SetTimeBetweenUpdatesForTesting(TimeSpan::FromMilliseconds(10.0));
    sensor.SetTimeBetweenUpdatesForTesting(TimeSpan::FromMilliseconds(10.0));
    sensor.SetTimeBetweenUpdatesForTesting(TimeSpan::FromMilliseconds(10.0));

    EXPECT_EQ(sensor.timeBetweenUpdatesChangedCount, 1);
}

// Confirms setCurrentValueProperty()'s update-then-notify order at the
// SensorBase<T> level directly: by the time CurrentValueChanged's handler
// runs, getCurrentValueProperty() must already reflect the new value, not
// the old one.
TEST(SensorBaseTests, SetCurrentValuePropertyUpdatesBeforeRaisingEvent)
{
    TestSensorBase sensor;
    sensor.SetSupportedForTesting(true);

    int valueSeenInsideHandler = -1;
    sensor.CurrentValueChanged += [&sensor, &valueSeenInsideHandler](
        System::Object*, const SensorReadingEventArgs<TestSensorReading>&)
    {
        valueSeenInsideHandler = sensor.getCurrentValueProperty().getValue();
    };

    sensor.SetCurrentValueForTesting(TestSensorReading(42));

    EXPECT_EQ(valueSeenInsideHandler, 42);
}

TEST(SensorBaseTests, CurrentValueChangedEventArgsCarryTheNewValue)
{
    TestSensorBase sensor;
    sensor.SetSupportedForTesting(true);

    bool invoked = false;
    int receivedValue = -1;
    sensor.CurrentValueChanged += [&invoked, &receivedValue](
        System::Object*, const SensorReadingEventArgs<TestSensorReading>& args)
    {
        invoked = true;
        receivedValue = args.getSensorReadingProperty().getValue();
    };

    sensor.SetCurrentValueForTesting(TestSensorReading(7));

    ASSERT_TRUE(invoked);
    EXPECT_EQ(receivedValue, 7);
}

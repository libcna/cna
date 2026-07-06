// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

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
            // Task P8-4: TimeBetweenUpdatesChanged fires outside SensorBase's
            // own mutex_ (correctly — see setTimeBetweenUpdatesProperty()'s
            // doc comment), so this counter can be incremented from more
            // than one thread concurrently once a test drives concurrent
            // setters that actually change the value (as
            // ConcurrentGetSetTimeBetweenUpdatesPropertyDoesNotCrash,
            // below, does) — confirmed as a real (if test-fixture-only, not
            // production-code) race by a ThreadSanitizer run during Task
            // P8-4. std::atomic closes it without needing its own mutex.
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

        // ShouldAcceptUpdateAt()/ResetUpdateThrottle() are protected (Task
        // SENSORBASE-001/ACCEL-005/GYRO-004), same reasoning as
        // TimeBetweenUpdatesChanged above — exercised here via a thin public
        // wrapper rather than a new NOXNA hook on Accelerometer/Gyroscope.
        bool ShouldAcceptUpdateForTesting(const DateTimeOffset& now)
        {
            return ShouldAcceptUpdateAt(now);
        }

        void ResetUpdateThrottleForTesting()
        {
            ResetUpdateThrottle();
        }

        std::atomic<int> timeBetweenUpdatesChangedCount{0};
    };
} // namespace

// Task DEVICES-0053: IsDataValid must start false — no reading has ever
// arrived yet. This was previously only ever exercised indirectly through a
// concrete sensor's own tests, never asserted at the SensorBase<T> level
// directly.
TEST(SensorBaseTests, IsDataValidDefaultsFalse)
{
    const TestSensorBase sensor;
    EXPECT_FALSE(sensor.getIsDataValidProperty());
}

// Task DEVICES-0052: getCurrentValueProperty() throwing InvalidOperationException
// is gated on isSupported_ alone — a *supported* sensor that has simply never
// had a reading delivered yet (no Start() call, or Start() succeeded but no
// event has arrived) must NOT throw; it returns a default-constructed reading.
// This distinction (unsupported vs. not-yet-started) was previously only
// implicit in the header's own doc comment, never asserted by a test.
TEST(SensorBaseTests, CurrentValueDoesNotThrowBeforeAnyReadingWhenSupported)
{
    TestSensorBase sensor;
    sensor.SetSupportedForTesting(true);

    TestSensorReading value;
    EXPECT_NO_THROW(value = sensor.getCurrentValueProperty());
    EXPECT_EQ(value.getValue(), 0);
}

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

// Task P8-2: timeBetweenUpdates_ was the one remaining field on this class read
// and written with no lock at all (currentValue_/isDataValid_/isSupported_/
// disposed_ were all already fixed across Tasks P5-2/P6-3). Stresses concurrent
// getTimeBetweenUpdatesProperty()/setTimeBetweenUpdatesProperty() calls from many
// threads; a regression would most likely show up as a crash or a TSan-detectable
// race, not a specific assertion failure — this test's value is in running clean
// under real concurrent contention, same as this project's other Start()/Stop()/
// Dispose() concurrency tests.
// Task SENSORBASE-001/ACCEL-005/GYRO-004/SDL-SENSOR-002: ShouldAcceptUpdateAt()
// is the shared throttle decision Accelerometer/Gyroscope now call from their
// real SDL dispatch path (ProcessSensorUpdateEvent()) to honor
// TimeBetweenUpdates. Tested here directly, at the SensorBase<T> level, with
// synthetic DateTimeOffset values — no real-time sleeps, so these tests are
// fast and cannot flake under machine load, matching this codebase's existing
// convention for platform-independent pure math (Detail::
// ConvertAndroidPortraitToXnaLandscape(), Detail::ExtractYawPitchRollFromQuaternion()).

TEST(SensorBaseTests, ShouldAcceptUpdateAtAcceptsTheVeryFirstCall)
{
    TestSensorBase sensor;
    const DateTimeOffset now = DateTimeOffset::getUtcNowProperty();

    EXPECT_TRUE(sensor.ShouldAcceptUpdateForTesting(now));
}

TEST(SensorBaseTests, ShouldAcceptUpdateAtRejectsASecondCallTooSoonAfterTheFirst)
{
    TestSensorBase sensor;
    sensor.SetTimeBetweenUpdatesForTesting(TimeSpan::FromMilliseconds(10.0));

    const DateTimeOffset first = DateTimeOffset::getUtcNowProperty();
    ASSERT_TRUE(sensor.ShouldAcceptUpdateForTesting(first));

    const DateTimeOffset tooSoon = first + TimeSpan::FromMilliseconds(5.0);
    EXPECT_FALSE(sensor.ShouldAcceptUpdateForTesting(tooSoon));
}

TEST(SensorBaseTests, ShouldAcceptUpdateAtAcceptsOnceTheIntervalHasFullyElapsed)
{
    TestSensorBase sensor;
    sensor.SetTimeBetweenUpdatesForTesting(TimeSpan::FromMilliseconds(10.0));

    const DateTimeOffset first = DateTimeOffset::getUtcNowProperty();
    ASSERT_TRUE(sensor.ShouldAcceptUpdateForTesting(first));

    const DateTimeOffset exactlyAtInterval = first + TimeSpan::FromMilliseconds(10.0);
    EXPECT_TRUE(sensor.ShouldAcceptUpdateForTesting(exactlyAtInterval));
}

TEST(SensorBaseTests, ShouldAcceptUpdateAtThrottlesIndependentlyPerInstance)
{
    TestSensorBase fast;
    TestSensorBase slow;
    fast.SetTimeBetweenUpdatesForTesting(TimeSpan::FromMilliseconds(1.0));
    slow.SetTimeBetweenUpdatesForTesting(TimeSpan::FromMilliseconds(100.0));

    const DateTimeOffset first = DateTimeOffset::getUtcNowProperty();
    ASSERT_TRUE(fast.ShouldAcceptUpdateForTesting(first));
    ASSERT_TRUE(slow.ShouldAcceptUpdateForTesting(first));

    const DateTimeOffset tenMillisecondsLater = first + TimeSpan::FromMilliseconds(10.0);
    EXPECT_TRUE(fast.ShouldAcceptUpdateForTesting(tenMillisecondsLater));
    EXPECT_FALSE(slow.ShouldAcceptUpdateForTesting(tenMillisecondsLater));
}

TEST(SensorBaseTests, ShouldAcceptUpdateAtMeasuresFromTheLastAcceptedCallNotTheLastAttempt)
{
    TestSensorBase sensor;
    sensor.SetTimeBetweenUpdatesForTesting(TimeSpan::FromMilliseconds(10.0));

    const DateTimeOffset first = DateTimeOffset::getUtcNowProperty();
    ASSERT_TRUE(sensor.ShouldAcceptUpdateForTesting(first));

    // Rejected attempt at +5ms must not reset the throttle's reference point.
    ASSERT_FALSE(sensor.ShouldAcceptUpdateForTesting(first + TimeSpan::FromMilliseconds(5.0)));

    // +9ms from the original accepted call is still too soon...
    EXPECT_FALSE(sensor.ShouldAcceptUpdateForTesting(first + TimeSpan::FromMilliseconds(9.0)));
    // ...but +10ms from the original accepted call is not.
    EXPECT_TRUE(sensor.ShouldAcceptUpdateForTesting(first + TimeSpan::FromMilliseconds(10.0)));
}

TEST(SensorBaseTests, ResetUpdateThrottleForTestingMakesTheNextCallAlwaysAccept)
{
    TestSensorBase sensor;
    sensor.SetTimeBetweenUpdatesForTesting(TimeSpan::FromMilliseconds(1000.0));

    const DateTimeOffset first = DateTimeOffset::getUtcNowProperty();
    ASSERT_TRUE(sensor.ShouldAcceptUpdateForTesting(first));
    ASSERT_FALSE(sensor.ShouldAcceptUpdateForTesting(first + TimeSpan::FromMilliseconds(1.0)));

    sensor.ResetUpdateThrottleForTesting();

    EXPECT_TRUE(sensor.ShouldAcceptUpdateForTesting(first + TimeSpan::FromMilliseconds(1.0)));
}

TEST(SensorBaseTests, ShouldAcceptUpdateAtWithZeroTimeBetweenUpdatesAlwaysAccepts)
{
    TestSensorBase sensor;
    sensor.SetTimeBetweenUpdatesForTesting(TimeSpan::Zero);

    const DateTimeOffset first = DateTimeOffset::getUtcNowProperty();
    ASSERT_TRUE(sensor.ShouldAcceptUpdateForTesting(first));
    EXPECT_TRUE(sensor.ShouldAcceptUpdateForTesting(first));
    EXPECT_TRUE(sensor.ShouldAcceptUpdateForTesting(first + TimeSpan::FromMilliseconds(1.0)));
}

TEST(SensorBaseTests, ConcurrentGetSetTimeBetweenUpdatesPropertyDoesNotCrash)
{
    TestSensorBase sensor;

    constexpr int ThreadCount = 8;
    constexpr int IterationsPerThread = 200;

    std::vector<std::thread> threads;
    threads.reserve(ThreadCount);

    for (int t = 0; t < ThreadCount; ++t)
    {
        threads.emplace_back([&sensor, t]()
        {
            for (int i = 0; i < IterationsPerThread; ++i)
            {
                sensor.SetTimeBetweenUpdatesForTesting(TimeSpan::FromMilliseconds(1.0 + (t % 4)));
                const TimeSpan current = sensor.getTimeBetweenUpdatesProperty();
                (void)current;
            }
        });
    }

    for (std::thread& thread : threads)
    {
        thread.join();
    }
}

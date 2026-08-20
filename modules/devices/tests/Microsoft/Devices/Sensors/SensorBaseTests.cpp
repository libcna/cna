// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
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
    // used here rather than adding new CNAEXT test hooks to the public API.
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

        void SetIsDataValidForTesting(bool value)
        {
            setIsDataValidProperty(value);
        }

        // Task BASE2-002 (2026-07-17, external audit
        // `audit_devices_2026-07-17.md`): exercises the new combined setter
        // directly.
        void SetCurrentValueAndMarkDataValidForTesting(const TestSensorReading& value)
        {
            SetCurrentValueAndMarkDataValid(value);
        }

        void SetTimeBetweenUpdatesForTesting(const TimeSpan& value)
        {
            setTimeBetweenUpdatesProperty(value);
        }

        void SetSupportedForTesting(bool value)
        {
            setIsSupportedProperty(value);
        }

        // Task LIFE-006 (2026-07-17, external audit
        // `audit_devices_2026-07-17.md`): mirrors the exact
        // ClaimDisposalOnce()/DisposalTerminalStateGuard/base-Dispose(bool)
        // pattern every real derived sensor class (Accelerometer/Gyroscope/
        // Compass/Motion) uses, so DisposalTerminalStateGuard's own
        // behavior can be tested directly against this isolated fixture --
        // with no shared, process-global instance-count counter to
        // accidentally pollute for every other test in the binary the way
        // deliberately making a real Accelerometer's own cleanup throw
        // would (it never reaches its own `--instanceCount_` decrement).
        void Dispose(bool disposing) override
        {
            if (!disposing)
            {
                SensorBase<TestSensorReading>::Dispose(disposing);
                return;
            }

            if (!ClaimDisposalOnce())
            {
                WaitForDisposalToComplete();
                return;
            }

            DisposalTerminalStateGuard terminalStateGuard(*this);

            if (disposalCleanupHookForTesting_)
            {
                disposalCleanupHookForTesting_();
            }

            SensorBase<TestSensorReading>::Dispose(disposing);
        }

        void SetDisposalCleanupHookForTesting(std::function<void()> hook)
        {
            disposalCleanupHookForTesting_ = std::move(hook);
        }

        // Declaring Dispose(bool) above hides the inherited no-argument
        // Dispose() (System::IDisposable) by C++ name-hiding rules -- brings
        // it back into scope, matching every real derived sensor class's
        // own identical `using` declaration.
        using SensorBase<TestSensorReading>::Dispose;

        // ShouldAcceptUpdateAt()/ResetUpdateThrottle() are protected (Task
        // SENSORBASE-001/ACCEL-005/GYRO-004), same reasoning as
        // TimeBetweenUpdatesChanged above — exercised here via a thin public
        // wrapper rather than a new CNAEXT hook on Accelerometer/Gyroscope.
        bool ShouldAcceptUpdateForTesting(const std::chrono::steady_clock::time_point& now)
        {
            return ShouldAcceptUpdateAt(now);
        }

        void ResetUpdateThrottleForTesting()
        {
            ResetUpdateThrottle();
        }

        std::atomic<int> timeBetweenUpdatesChangedCount{0};

    private:
        std::function<void()> disposalCleanupHookForTesting_;
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

// Task SENSORBASE-005: getCurrentValueProperty()/getIsDataValidProperty() are
// defined once, here on SensorBase<T> itself, and neither checks disposed_ —
// unlike Start()/Stop() (implemented separately per concrete class, each with
// its own explicit ObjectDisposedException::ThrowIf(getIsDisposedProperty(),
// ...) check). This is therefore already guaranteed byte-for-byte identical
// across Accelerometer/Gyroscope/Compass/Motion (all four inherit this same
// template code, none override these two getters), satisfying this task's
// "same base contract across all four" requirement by construction rather
// than by separately verifying each class. Locking in the current behavior
// here, not changing it: whether these getters *should* instead throw
// ObjectDisposedException after Dispose() (matching the conventional .NET
// IDisposable pattern, and this codebase's own Start()/Stop() precedent) is
// SENSORBASE-006's question ("Verify Dispose semantics"), not this task's.
TEST(SensorBaseTests, CurrentValueAndIsDataValidDoNotThrowAfterDispose)
{
    TestSensorBase sensor;
    sensor.SetSupportedForTesting(true);
    sensor.SetCurrentValueForTesting(TestSensorReading(7));
    sensor.SetIsDataValidForTesting(true);

    sensor.Dispose();

    TestSensorReading value;
    EXPECT_NO_THROW(value = sensor.getCurrentValueProperty());
    EXPECT_EQ(value.getValue(), 7);
    EXPECT_TRUE(sensor.getIsDataValidProperty());
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

// Task SENSORBASE-008: confirmed via the archived MSDN pages for
// SensorBase(TSensorReading).TimeBetweenUpdates (MSDN `hh220884`, vs.110) and
// the SensorBase(TSensorReading) class overview (MSDN `hh239315`, vs.105)
// that the real WP7 API's setter is a plain `public TimeSpan
// TimeBetweenUpdates { get; set; }` with no documented Exceptions section and
// no Remarks describing a valid range — unlike, e.g.,
// VibrateController.Start(TimeSpan), which does document an
// ArgumentOutOfRangeException contract. CNA's setTimeBetweenUpdatesProperty()
// accepting any value unchanged is therefore not an unvalidated gap but a
// faithful match to the real, documented (lack of) contract. These two tests
// lock that in explicitly, distinct from
// ShouldAcceptUpdateAtWithNegativeTimeBetweenUpdatesNeverThrottles above
// (which covers the throttle *decision*, not the property setter itself).
TEST(SensorBaseTests, SetTimeBetweenUpdatesPropertyAcceptsNegativeValueWithoutThrowing)
{
    TestSensorBase sensor;

    EXPECT_NO_THROW(sensor.SetTimeBetweenUpdatesForTesting(TimeSpan::FromMilliseconds(-10.0)));
    EXPECT_EQ(sensor.getTimeBetweenUpdatesProperty(), TimeSpan::FromMilliseconds(-10.0));
}

TEST(SensorBaseTests, SetTimeBetweenUpdatesPropertyAcceptsMaxValueWithoutThrowing)
{
    TestSensorBase sensor;

    EXPECT_NO_THROW(sensor.SetTimeBetweenUpdatesForTesting(TimeSpan::MaxValue));
    EXPECT_EQ(sensor.getTimeBetweenUpdatesProperty(), TimeSpan::MaxValue);
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

// Task BASE2-002 (2026-07-17, external audit `audit_devices_2026-07-17.md`):
// SetCurrentValueAndMarkDataValid() must publish both IsDataValid and
// CurrentValue together -- confirms both land correctly in the simple,
// single-threaded case (the concurrency proof is the stress test below).
TEST(SensorBaseTests, SetCurrentValueAndMarkDataValidSetsBothFields)
{
    TestSensorBase sensor;
    sensor.SetSupportedForTesting(true);

    ASSERT_FALSE(sensor.getIsDataValidProperty());

    sensor.SetCurrentValueAndMarkDataValidForTesting(TestSensorReading(99));

    EXPECT_TRUE(sensor.getIsDataValidProperty());
    EXPECT_EQ(sensor.getCurrentValueProperty().getValue(), 99);
}

TEST(SensorBaseTests, SetCurrentValueAndMarkDataValidRaisesCurrentValueChangedWithTheNewValue)
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

    sensor.SetCurrentValueAndMarkDataValidForTesting(TestSensorReading(13));

    ASSERT_TRUE(invoked);
    EXPECT_EQ(receivedValue, 13);
}

// The actual regression proof: before Task BASE2-002, a dispatch path called
// setIsDataValidProperty(true) and setCurrentValueProperty(value) as two
// independently-locked calls -- a concurrent reader could observe
// IsDataValid already true while CurrentValue still held the previous (here,
// default-constructed, value 0) reading. A writer thread repeatedly calls
// SetCurrentValueAndMarkDataValidForTesting() with a fixed, distinguishable
// value (never 0) while a reader thread repeatedly checks the invariant "if
// IsDataValid is true, CurrentValue must not be the default-constructed
// value" -- any observed violation fails the test immediately. Run under
// devices-tsan specifically: a genuine race here is exactly what TSan is
// built to catch, not something an assertion alone can reliably reproduce.
TEST(SensorBaseTests, SetCurrentValueAndMarkDataValidNeverExposesAnInconsistentSnapshot)
{
    TestSensorBase sensor;
    sensor.SetSupportedForTesting(true);

    std::atomic<bool> stop{false};
    std::atomic<bool> violationFound{false};
    constexpr int WriterIterations = 2000;

    std::thread reader([&sensor, &stop, &violationFound]()
    {
        while (!stop.load(std::memory_order_acquire))
        {
            if (sensor.getIsDataValidProperty() && sensor.getCurrentValueProperty().getValue() == 0)
            {
                violationFound.store(true, std::memory_order_release);
                return;
            }
        }
    });

    for (int i = 0; i < WriterIterations; ++i)
    {
        sensor.SetCurrentValueAndMarkDataValidForTesting(TestSensorReading(42));
    }

    stop.store(true, std::memory_order_release);
    reader.join();

    EXPECT_FALSE(violationFound.load(std::memory_order_acquire));
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
// synthetic std::chrono::steady_clock::time_point values — no real-time
// sleeps, so these tests are fast and cannot flake under machine load,
// matching this codebase's existing convention for platform-independent pure
// math (Detail::ConvertAndroidPortraitToXnaLandscape(),
// Detail::ExtractYawPitchRollFromQuaternion()). Task (2026-07-06
// stabilization pass): switched from synthetic DateTimeOffset values to
// std::chrono::steady_clock::time_point ones, matching ShouldAcceptUpdateAt()'s
// own signature change (see its doc comment for why the throttle decision
// itself uses a monotonic clock, not wall-clock time).

TEST(SensorBaseTests, ShouldAcceptUpdateAtAcceptsTheVeryFirstCall)
{
    TestSensorBase sensor;
    const auto now = std::chrono::steady_clock::now();

    EXPECT_TRUE(sensor.ShouldAcceptUpdateForTesting(now));
}

TEST(SensorBaseTests, ShouldAcceptUpdateAtRejectsASecondCallTooSoonAfterTheFirst)
{
    TestSensorBase sensor;
    sensor.SetTimeBetweenUpdatesForTesting(TimeSpan::FromMilliseconds(10.0));

    const auto first = std::chrono::steady_clock::now();
    ASSERT_TRUE(sensor.ShouldAcceptUpdateForTesting(first));

    const auto tooSoon = first + std::chrono::milliseconds(5);
    EXPECT_FALSE(sensor.ShouldAcceptUpdateForTesting(tooSoon));
}

TEST(SensorBaseTests, ShouldAcceptUpdateAtAcceptsOnceTheIntervalHasFullyElapsed)
{
    TestSensorBase sensor;
    sensor.SetTimeBetweenUpdatesForTesting(TimeSpan::FromMilliseconds(10.0));

    const auto first = std::chrono::steady_clock::now();
    ASSERT_TRUE(sensor.ShouldAcceptUpdateForTesting(first));

    const auto exactlyAtInterval = first + std::chrono::milliseconds(10);
    EXPECT_TRUE(sensor.ShouldAcceptUpdateForTesting(exactlyAtInterval));
}

TEST(SensorBaseTests, ShouldAcceptUpdateAtThrottlesIndependentlyPerInstance)
{
    TestSensorBase fast;
    TestSensorBase slow;
    fast.SetTimeBetweenUpdatesForTesting(TimeSpan::FromMilliseconds(1.0));
    slow.SetTimeBetweenUpdatesForTesting(TimeSpan::FromMilliseconds(100.0));

    const auto first = std::chrono::steady_clock::now();
    ASSERT_TRUE(fast.ShouldAcceptUpdateForTesting(first));
    ASSERT_TRUE(slow.ShouldAcceptUpdateForTesting(first));

    const auto tenMillisecondsLater = first + std::chrono::milliseconds(10);
    EXPECT_TRUE(fast.ShouldAcceptUpdateForTesting(tenMillisecondsLater));
    EXPECT_FALSE(slow.ShouldAcceptUpdateForTesting(tenMillisecondsLater));
}

TEST(SensorBaseTests, ShouldAcceptUpdateAtMeasuresFromTheLastAcceptedCallNotTheLastAttempt)
{
    TestSensorBase sensor;
    sensor.SetTimeBetweenUpdatesForTesting(TimeSpan::FromMilliseconds(10.0));

    const auto first = std::chrono::steady_clock::now();
    ASSERT_TRUE(sensor.ShouldAcceptUpdateForTesting(first));

    // Rejected attempt at +5ms must not reset the throttle's reference point.
    ASSERT_FALSE(sensor.ShouldAcceptUpdateForTesting(first + std::chrono::milliseconds(5)));

    // +9ms from the original accepted call is still too soon...
    EXPECT_FALSE(sensor.ShouldAcceptUpdateForTesting(first + std::chrono::milliseconds(9)));
    // ...but +10ms from the original accepted call is not.
    EXPECT_TRUE(sensor.ShouldAcceptUpdateForTesting(first + std::chrono::milliseconds(10)));
}

TEST(SensorBaseTests, ResetUpdateThrottleForTestingMakesTheNextCallAlwaysAccept)
{
    TestSensorBase sensor;
    sensor.SetTimeBetweenUpdatesForTesting(TimeSpan::FromMilliseconds(1000.0));

    const auto first = std::chrono::steady_clock::now();
    ASSERT_TRUE(sensor.ShouldAcceptUpdateForTesting(first));
    ASSERT_FALSE(sensor.ShouldAcceptUpdateForTesting(first + std::chrono::milliseconds(1)));

    sensor.ResetUpdateThrottleForTesting();

    EXPECT_TRUE(sensor.ShouldAcceptUpdateForTesting(first + std::chrono::milliseconds(1)));
}

TEST(SensorBaseTests, ShouldAcceptUpdateAtWithZeroTimeBetweenUpdatesAlwaysAccepts)
{
    TestSensorBase sensor;
    sensor.SetTimeBetweenUpdatesForTesting(TimeSpan::Zero);

    const auto first = std::chrono::steady_clock::now();
    ASSERT_TRUE(sensor.ShouldAcceptUpdateForTesting(first));
    EXPECT_TRUE(sensor.ShouldAcceptUpdateForTesting(first));
    EXPECT_TRUE(sensor.ShouldAcceptUpdateForTesting(first + std::chrono::milliseconds(1)));
}

// Task (2026-07-06 stabilization pass): setTimeBetweenUpdatesProperty() does
// not validate or reject a negative TimeSpan (no dedicated plans/plan_devices.md
// task exists yet for that minimum/maximum-value validation gap). This test
// locks in that ShouldAcceptUpdateAt() itself still behaves safely, rather
// than leaving the case as an untested assumption: a negative interval, once
// converted to a std::chrono duration, can never be "not yet elapsed" against
// elapsed time measured from a monotonic, non-decreasing clock (elapsed time
// is always >= 0, which is always >= any negative interval) — so a negative
// TimeBetweenUpdates degrades to the same "never throttle" behavior as
// TimeSpan::Zero, not a crash or an infinite-reject lockup.
TEST(SensorBaseTests, ShouldAcceptUpdateAtWithNegativeTimeBetweenUpdatesNeverThrottles)
{
    TestSensorBase sensor;
    sensor.SetTimeBetweenUpdatesForTesting(TimeSpan::FromMilliseconds(-10.0));

    const auto first = std::chrono::steady_clock::now();
    ASSERT_TRUE(sensor.ShouldAcceptUpdateForTesting(first));
    EXPECT_TRUE(sensor.ShouldAcceptUpdateForTesting(first));
    EXPECT_TRUE(sensor.ShouldAcceptUpdateForTesting(first + std::chrono::milliseconds(1)));
}

// Task BASE2-001 (2026-07-17, external audit `audit_devices_2026-07-17.md`):
// TimeSpan::MaxValue/MinValue were previously only tested through the plain
// setTimeBetweenUpdatesProperty() setter (SetTimeBetweenUpdatesPropertyAccepts
// MaxValueWithoutThrowing, above) -- confirming the value round-trips, but not
// that ShouldAcceptUpdateAt()'s own duration comparison (constructing a
// std::chrono::duration from TimeSpan::MaxValue's/MinValue's tick count, then
// comparing it against a std::chrono::steady_clock::duration of a different
// period) is itself free of overflow/UB for these extreme inputs -- "prevent
// ... integer/duration overflow" (this task's own required work). Run under
// devices-ubsan specifically to let UBSan's own signed-integer-overflow
// detector confirm this directly, not just "didn't crash".
TEST(SensorBaseTests, ShouldAcceptUpdateAtWithMaxValueTimeBetweenUpdatesNeverAcceptsASecondUpdate)
{
    TestSensorBase sensor;
    sensor.SetTimeBetweenUpdatesForTesting(TimeSpan::MaxValue);

    const auto first = std::chrono::steady_clock::now();
    ASSERT_TRUE(sensor.ShouldAcceptUpdateForTesting(first));
    // An interval this large can never have "fully elapsed" again within a
    // test's own runtime -- confirms this degrades to "never throttle again
    // after the first" (a sensible, non-crashing outcome for a
    // maximum-possible interval), not a crash or a wraparound that
    // incorrectly accepts.
    EXPECT_FALSE(sensor.ShouldAcceptUpdateForTesting(first + std::chrono::hours(24)));
}

TEST(SensorBaseTests, ShouldAcceptUpdateAtWithMinValueTimeBetweenUpdatesNeverThrottles)
{
    TestSensorBase sensor;
    sensor.SetTimeBetweenUpdatesForTesting(TimeSpan::MinValue);

    const auto first = std::chrono::steady_clock::now();
    ASSERT_TRUE(sensor.ShouldAcceptUpdateForTesting(first));
    // Same "always accept" degradation as the plain-negative case above, just
    // at the most extreme possible negative value.
    EXPECT_TRUE(sensor.ShouldAcceptUpdateForTesting(first));
    EXPECT_TRUE(sensor.ShouldAcceptUpdateForTesting(first + std::chrono::milliseconds(1)));
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

// Task LIFE-006 (2026-07-17, external audit `audit_devices_2026-07-17.md`):
// previously, if the winning Dispose() caller's own cleanup threw, disposed_
// never became true and a concurrently-racing loser (blocked in
// WaitForDisposalToComplete()) would hang forever — WaitForDisposalToComplete()'s
// own prior doc comment explicitly documented this as a known, accepted
// assumption rather than something actually enforced. Uses TestSensorBase's
// own ClaimDisposalOnce()/DisposalTerminalStateGuard-based Dispose(bool)
// override (mirroring every real derived sensor class) with a pause-then-
// release gate whose release action throws instead of completing normally —
// without DisposalTerminalStateGuard, the loser thread below would never
// return and this test would hang/timeout instead of completing. Uses this
// isolated fixture rather than a real Accelerometer specifically so a
// deliberately-thrown cleanup never reaches (and therefore never leaks) any
// shared, process-global instance-count counter another test elsewhere in
// this binary might depend on.
TEST(SensorBaseTests, WinningCleanupExceptionStillUnblocksConcurrentLosingDispose)
{
    auto sensor = std::make_shared<TestSensorBase>();

    std::mutex gateMutex;
    std::condition_variable gateCv;
    bool winnerPaused = false;
    bool releaseWinner = false;

    sensor->SetDisposalCleanupHookForTesting([&]()
    {
        {
            std::lock_guard<std::mutex> lock(gateMutex);
            winnerPaused = true;
        }
        gateCv.notify_all();

        {
            std::unique_lock<std::mutex> lock(gateMutex);
            gateCv.wait(lock, [&] { return releaseWinner; });
        }

        throw std::runtime_error("deliberate winning cleanup failure");
    });

    std::thread winnerThread([sensor]()
    {
        EXPECT_THROW(sensor->Dispose(), std::runtime_error);
    });

    // Wait until the winner has claimed disposal and is paused inside its
    // own cleanup, before starting the loser.
    {
        std::unique_lock<std::mutex> lock(gateMutex);
        gateCv.wait(lock, [&] { return winnerPaused; });
    }

    std::atomic<bool> loserReturned{false};
    std::thread loserThread([sensor, &loserReturned]()
    {
        EXPECT_NO_THROW(sensor->Dispose());
        loserReturned.store(true);
    });

    // The loser must not have returned yet -- the winner is still paused
    // inside cleanup (about to throw), so disposed_ must still be false.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(loserReturned.load());

    // Release the winner -- it throws immediately afterward. The loser must
    // still unblock (via DisposalTerminalStateGuard's destructor running
    // during the winner's own stack unwinding) and return normally, not
    // rethrow or hang, per this task's own explicit "losing Dispose() never
    // observes the winner's specific failure" decision.
    {
        std::lock_guard<std::mutex> lock(gateMutex);
        releaseWinner = true;
    }
    gateCv.notify_all();

    winnerThread.join();
    loserThread.join();

    EXPECT_TRUE(loserReturned.load());
}

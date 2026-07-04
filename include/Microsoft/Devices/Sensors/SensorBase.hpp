// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 6/7/25.
//

#pragma once

#include <condition_variable>
#include <mutex>
#include <type_traits>
#include <utility>

#include "Microsoft/Devices/Sensors/ISensorReading.hpp"
#include "Microsoft/Devices/Sensors/SensorReadingEventArgs.hpp"
#include "Microsoft/Devices/Sensors/SensorState.hpp"
#include "System/IDisposable.hpp"
#include "System/EventHandler.hpp"
#include "System/EventArgs.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "System/TimeSpan.hpp"

namespace Microsoft::Devices::Sensors
{
    /** @brief Abstract base class for device sensors; provides current-value and event-notification infrastructure. */
    template <typename TSensorReading>
    class SensorBase : public System::Object, public System::IDisposable
    {
        static_assert(std::is_base_of_v<ISensorReading, TSensorReading>,
                      "TSensorReading must derive from ISensorReading");

    private:
        bool disposed_;
        bool disposalClaimed_;
        bool isDataValid_;
        bool isSupported_;
        System::TimeSpan timeBetweenUpdates_;
        TSensorReading currentValue_;

        /**
         * Guards currentValue_/isDataValid_ (Task P5-2): for real,
         * SDL3-backed sensors (Accelerometer/Gyroscope), setCurrentValueProperty()/
         * setIsDataValidProperty() are called from DispatchSensorReading(),
         * which may run on whatever thread SDL invokes the sensor
         * event-watch callback on — not necessarily the game/user thread
         * that calls getCurrentValueProperty()/getIsDataValidProperty().
         * Never held while calling CurrentValueChanged.Raise() — a
         * subscriber's handler can legitimately call back into this sensor
         * (e.g. Dispose(), another getter), and raising under a lock risks
         * deadlock.
         */
        mutable std::mutex mutex_;

        /**
         * Signaled whenever disposed_ transitions to true (Task P7-2).
         * Lets a concurrent Dispose() call that lost ClaimDisposalOnce()
         * wait for the winning call's cleanup to actually finish, instead
         * of proceeding immediately — see WaitForDisposalToComplete().
         */
        std::condition_variable disposalFinishedCv_;

    protected:
        /**
         * @brief Event raised when TimeBetweenUpdates changes.
         *
         * In the original .NET version this event is protected.
         */
        System::EventHandler<System::EventArgs> TimeBetweenUpdatesChanged;

        /**
         * @brief Gets whether this instance has already been disposed.
         *
         * Task P6-3: guarded by mutex_ (shared with currentValue_/
         * isDataValid_/isSupported_) since this is read from both the
         * game/user thread and, for real SDL3-backed sensors, the SDL
         * sensor event-watch thread (Accelerometer/Gyroscope's
         * ProcessSensorUpdateEvent()). Previously unguarded — a real data
         * race under the C++ memory model.
         *
         * @return true if disposed; otherwise false.
         */
        [[nodiscard]] bool getIsDisposedProperty() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return disposed_;
        }

        /**
         * @brief Atomically claims this instance's one-time disposal cleanup (Task P6-3).
         *
         * Derived classes must call this instead of checking
         * getIsDisposedProperty() directly to decide whether to run their
         * own disposal cleanup (Stop(), releasing native resources,
         * decrementing a shared instance counter, etc.) — a plain "if
         * (!getIsDisposedProperty())" check-then-act is not atomic and lets
         * two threads calling Dispose() on the same instance concurrently
         * both pass the check and both run cleanup once each, e.g. both
         * decrementing a shared instance counter for what should be a
         * single logical disposal.
         *
         * Deliberately a separate flag from disposed_ (still set only by
         * Dispose(bool) itself, at its normal point in each derived
         * override): a derived Dispose(bool)'s own cleanup body may call
         * other public methods (e.g. Stop()) that themselves guard on
         * getIsDisposedProperty() being still false — claiming disposal
         * must not make the object appear fully disposed before its own
         * cleanup has actually run.
         *
         * Task P7-2: the caller that receives `false` back (the "loser" of
         * a concurrent Dispose() race) must not itself call the base
         * Dispose(bool) below — doing so would flip disposed_ to true while
         * the winning caller's own cleanup (which may itself call Stop(),
         * guarded by the same getIsDisposedProperty() precondition) is
         * still running, causing the winner's own Stop() call to observe
         * disposed_ == true and throw ObjectDisposedException mid-cleanup —
         * a real bug found by re-auditing this exact interaction (see
         * plan_devices_phase7.md's Audit finding B). The loser must instead
         * call WaitForDisposalToComplete() and then simply return.
         *
         * @return True if this call is the first to claim disposal (the
         * caller must run cleanup, then call the base Dispose(bool)); false
         * if another call already has, concurrently or previously (the
         * caller must call WaitForDisposalToComplete() instead).
         */
        bool ClaimDisposalOnce()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (disposalClaimed_)
            {
                return false;
            }
            disposalClaimed_ = true;
            return true;
        }

        /**
         * @brief Blocks until the winning concurrent Dispose() caller's
         * cleanup has actually completed (Task P7-2).
         *
         * Only meaningful for a caller that just received `false` from
         * ClaimDisposalOnce() — i.e. lost a race against another thread
         * concurrently calling Dispose() on this same instance. Waits for
         * disposed_ to become true (set by the base Dispose(bool) override,
         * called only by the winning caller once its own cleanup finishes)
         * rather than proceeding immediately, so a losing caller's
         * Dispose(bool) override returns only after the object is *actually*
         * fully disposed.
         *
         * Assumes the winning caller's cleanup path does not throw once it
         * has claimed disposal — matching this codebase's existing
         * assumption that Stop()/native-resource cleanup do not throw once
         * their disposed-state precondition is satisfied. If that
         * assumption were ever violated, a concurrent loser would wait here
         * indefinitely; this mirrors the same no-timeout wait convention
         * already used by Dispose(bool)'s own callbackFinished_ wait in
         * Accelerometer/Gyroscope.
         */
        void WaitForDisposalToComplete()
        {
            std::unique_lock<std::mutex> lock(mutex_);
            disposalFinishedCv_.wait(lock, [this] { return disposed_; });
        }

        /**
         * @brief Sets the current sensor reading and raises CurrentValueChanged.
         *
         * This mirrors the behavior of the .NET protected setter:
         * assigning CurrentValue updates the cached event args and raises the event.
         *
         * @param value New sensor reading.
         */
        void setCurrentValueProperty(const TSensorReading& value)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                currentValue_ = value;
            }

            if (!CurrentValueChanged.Empty())
            {
                // A local, per-dispatch event-args instance (Task P5-2) —
                // not a shared member — so a concurrent dispatch on another
                // thread can never mutate the args this call is still
                // raising with.
                SensorReadingEventArgs<TSensorReading> args(value);
                CurrentValueChanged.Raise(static_cast<System::Object*>(this), args);
            }
        }

        /**
         * @brief Sets the current sensor reading and raises CurrentValueChanged.
         *
         * @param value New sensor reading to move.
         */
        void setCurrentValueProperty(TSensorReading&& value)
        {
            TSensorReading valueForEvent{};
            bool shouldRaise = false;

            {
                std::lock_guard<std::mutex> lock(mutex_);
                currentValue_ = std::move(value);
                shouldRaise = !CurrentValueChanged.Empty();
                if (shouldRaise)
                {
                    // value is moved-from at this point; copy the
                    // now-authoritative currentValue_ instead, still under
                    // the lock, so the event gets the real new value.
                    valueForEvent = currentValue_;
                }
            }

            if (shouldRaise)
            {
                SensorReadingEventArgs<TSensorReading> args(std::move(valueForEvent));
                CurrentValueChanged.Raise(static_cast<System::Object*>(this), args);
            }
        }

        /**
         * @brief Sets whether the current sensor data is valid.
         *
         * @param value New validity flag.
         */
        void setIsDataValidProperty(bool value)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            isDataValid_ = value;
        }

        /**
         * @brief Sets whether the underlying sensor hardware is supported on this device.
         *
         * Derived classes must call this once from their constructor with the result of
         * their own static IsSupported check, since SensorBase has no access to it.
         *
         * Task P6-3: guarded by mutex_ — previously written here without a
         * lock but read under mutex_ inside getCurrentValueProperty(), an
         * inconsistent locking discipline on the same field.
         *
         * @param value New support flag.
         */
        void setIsSupportedProperty(bool value)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            isSupported_ = value;
        }

        /**
         * @brief Derived classes override this method to dispose managed-like
         * and unmanaged/native resources.
         *
         * @param disposing true when called from Dispose(); false when called
         *        from the destructor path.
         *
         * @note This mirrors the .NET pattern, but C++ destructor behavior is
         * not identical to .NET finalization.
         *
         * @note Task P7-2: derived overrides must call this (the base
         * implementation) only from the winning side of a ClaimDisposalOnce()
         * race — i.e. only after that caller's own cleanup has fully run.
         * This notifies any concurrent loser blocked in
         * WaitForDisposalToComplete().
         */
        virtual void Dispose(bool disposing)
        {
            (void)disposing;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                disposed_ = true;
            }
            disposalFinishedCv_.notify_all();
        }

    public:
        /**
         * @brief Event raised when CurrentValue changes.
         *
         * In the original .NET version this is a public event.
         */
        System::EventHandler<SensorReadingEventArgs<TSensorReading>> CurrentValueChanged;

        /**
         * @brief Initializes a new instance of the SensorBase class.
         *
         * The default TimeBetweenUpdates value matches the .NET source:
         * 2 milliseconds.
         */
        SensorBase()
            : disposed_(false),
              disposalClaimed_(false),
              isDataValid_(false),
              isSupported_(false),
              timeBetweenUpdates_(System::TimeSpan::Zero),
              currentValue_()
        {
            setTimeBetweenUpdatesProperty(System::TimeSpan::FromMilliseconds(2.0));
        }

        /**
         * @brief Virtual destructor.
         *
         * This attempts to mirror the .NET finalizer path by calling Dispose(false)
         * if the object has not yet been disposed.
         *
         * @note Unlike .NET, virtual dispatch from destructors in C++ does not behave
         * the same way for derived classes.
         */
        virtual ~SensorBase()
        {
            if (!getIsDisposedProperty())
            {
                Dispose(false);
            }
        }

        /**
         * @brief Gets the current sensor reading.
         *
         * Returns a copy (Task P5-2) rather than a reference to internal
         * state, since that state can be concurrently overwritten from the
         * SDL sensor event-watch callback thread for real, SDL3-backed
         * sensors — a caller holding a reference into currentValue_ across
         * such a write would see a torn/inconsistent value. This also
         * matches the real WP7 API more closely: the C# CurrentValue
         * property returns a value-type reading, not a reference.
         *
         * @return Current sensor reading.
         *
         * @throws System::InvalidOperationException if the sensor is not supported on
         * this device. Use IsSupported to check before accessing this property.
         */
        [[nodiscard]] TSensorReading getCurrentValueProperty() const
        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (!isSupported_)
            {
                throw System::InvalidOperationException(
                    "The sensor is not supported on this device. Check IsSupported before accessing CurrentValue.");
            }

            return currentValue_;
        }

        /**
         * @brief Gets whether the current sensor data is valid.
         *
         * @return true if valid; otherwise false.
         */
        [[nodiscard]] bool getIsDataValidProperty() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return isDataValid_;
        }

        /**
         * @brief Gets the time interval between sensor updates.
         *
         * @return Time between updates.
         */
        [[nodiscard]] const System::TimeSpan& getTimeBetweenUpdatesProperty() const
        {
            return timeBetweenUpdates_;
        }

        /**
         * @brief Sets the time interval between sensor updates.
         *
         * If the value changes, TimeBetweenUpdatesChanged is raised.
         *
         * @param value New update interval.
         */
        void setTimeBetweenUpdatesProperty(const System::TimeSpan& value)
        {
            if (!(timeBetweenUpdates_ != value))
            {
                return;
            }

            timeBetweenUpdates_ = value;

            if (!TimeBetweenUpdatesChanged.Empty())
            {
                System::EventArgs args;
                TimeBetweenUpdatesChanged.Raise(static_cast<System::Object*>(this), args);
            }
        }

        /**
         * @brief Releases the resources used by the current object.
         *
         * Mirrors the .NET Dispose() method. Calling Dispose() more than once
         * throws ObjectDisposedException, just like the decompiled source.
         *
         * @note Calling Dispose() concurrently from two different threads
         * on the *same* instance is not a fully supported usage pattern
         * (matching the conventional .NET IDisposable contract, which does
         * not generally require Dispose() itself to be thread-safe against
         * concurrent callers): the getIsDisposedProperty() check above and
         * the call into Dispose(true) below are not a single atomic
         * transaction, so both concurrent callers may reach Dispose(true)
         * without either seeing ObjectDisposedException here. Derived
         * classes' own cleanup is still safe in that race (Tasks P6-3/P7-2):
         * ClaimDisposalOnce() ensures only one of the two calls actually
         * runs the derived cleanup body (e.g. decrementing a shared
         * instance counter once, not twice); the losing call blocks in
         * WaitForDisposalToComplete() until the winner's cleanup has
         * genuinely finished (Task P7-2 — previously the loser proceeded
         * immediately and could flip disposed_ to true while the winner's
         * own cleanup, e.g. its internal Stop() call, was still relying on
         * disposed_ being false), then returns as a no-op rather than a
         * clean second-call exception.
         */
        void Dispose() override
        {
            if (getIsDisposedProperty())
            {
                throw System::ObjectDisposedException("SensorBase");
            }

            Dispose(true);
        }

        /**
         * @brief Starts the sensor.
         */
        virtual void Start() = 0;

        /**
         * @brief Stops the sensor.
         */
        virtual void Stop() = 0;
    };
} // namespace Microsoft::Devices::Sensors

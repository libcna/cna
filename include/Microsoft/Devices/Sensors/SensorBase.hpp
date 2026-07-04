// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 6/7/25.
//

#pragma once

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
         * @return true if disposed; otherwise false.
         */
        [[nodiscard]] bool getIsDisposedProperty() const
        {
            return disposed_;
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
         * @param value New support flag.
         */
        void setIsSupportedProperty(bool value)
        {
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
         */
        virtual void Dispose(bool disposing)
        {
            (void)disposing;
            disposed_ = true;
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
            if (!disposed_)
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
         */
        void Dispose() override
        {
            if (disposed_)
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

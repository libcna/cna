// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 6/7/25.
//

#pragma once

#include <type_traits>
#include <utility>

#include "Microsoft/Devices/Sensors/ISensorReading.hpp"
#include "Microsoft/Devices/Sensors/SensorReadingEventArgs.hpp"
#include "Microsoft/Devices/Sensors/SensorState.hpp"
#include "System/IDisposable.hpp"
#include "System/EventHandler.hpp"
#include "System/EventArgs.hpp"
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
        System::TimeSpan timeBetweenUpdates_;
        TSensorReading currentValue_;
        SensorReadingEventArgs<TSensorReading> eventArgs_;

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
            currentValue_ = value;

            if (!CurrentValueChanged.Empty())
            {
                eventArgs_.setSensorReadingProperty(value);
                CurrentValueChanged.Raise(static_cast<System::Object*>(this), eventArgs_);
            }
        }

        /**
         * @brief Sets the current sensor reading and raises CurrentValueChanged.
         *
         * @param value New sensor reading to move.
         */
        void setCurrentValueProperty(TSensorReading&& value)
        {
            currentValue_ = std::move(value);

            if (!CurrentValueChanged.Empty())
            {
                eventArgs_.setSensorReadingProperty(currentValue_);
                CurrentValueChanged.Raise(static_cast<System::Object*>(this), eventArgs_);
            }
        }

        /**
         * @brief Sets whether the current sensor data is valid.
         *
         * @param value New validity flag.
         */
        void setIsDataValidProperty(bool value)
        {
            isDataValid_ = value;
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
              timeBetweenUpdates_(System::TimeSpan::Zero),
              currentValue_(),
              eventArgs_(TSensorReading())
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
         * @return Current sensor reading.
         */
        [[nodiscard]] const TSensorReading& getCurrentValueProperty() const
        {
            return currentValue_;
        }

        /**
         * @brief Gets whether the current sensor data is valid.
         *
         * @return true if valid; otherwise false.
         */
        [[nodiscard]] bool getIsDataValidProperty() const
        {
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

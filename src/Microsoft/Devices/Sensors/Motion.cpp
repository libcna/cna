// SPDX-License-Identifier: MS-PL

#include "Microsoft/Devices/Sensors/Motion.hpp"

#include "System/ObjectDisposedException.hpp"

namespace Microsoft::Devices::Sensors
{
    int Motion::instanceCount_ = 0;
    std::mutex Motion::instanceCountMutex_;

    bool Motion::getIsSupportedProperty()
    {
        // TODO: wire up real sensor fusion (Accelerometer + Compass + Gyroscope)
        // once SDL3 gains a magnetometer/compass API and Compass::getIsSupportedProperty()
        // can return true.
        return false;
    }

    SensorState Motion::getStateProperty() const
    {
        System::ObjectDisposedException::ThrowIf(getIsDisposedProperty(), "Motion");
        return state_;
    }

    Motion::Motion()
        : state_(SensorState::NotSupported),
          started_(false)
    {
        // Task P6-1: previously unguarded, a real data race between
        // concurrent constructors/Dispose() calls on this shared static
        // counter.
        {
            std::lock_guard<std::mutex> lock(instanceCountMutex_);

            if (instanceCount_ >= MaxSensorCount)
            {
                throw SensorFailedException(
                    "The limit of 10 simultaneous instances of the Motion class per application has been exceeded.");
            }

            ++instanceCount_;
        }

        const bool supported = getIsSupportedProperty();
        state_ = supported ? SensorState::Initializing : SensorState::NotSupported;
        setIsSupportedProperty(supported);
    }

    Motion::~Motion()
    {
        if (!getIsDisposedProperty())
        {
            Dispose(true);
        }
    }

    void Motion::Start()
    {
        System::ObjectDisposedException::ThrowIf(getIsDisposedProperty(), "Motion");

        state_ = SensorState::NotSupported;
        throw SensorFailedException("Motion is not supported on this platform.");
    }

    void Motion::Stop()
    {
        System::ObjectDisposedException::ThrowIf(getIsDisposedProperty(), "Motion");

        started_ = false;
        state_ = SensorState::Disabled;
    }

    void Motion::Dispose(bool disposing)
    {
        if (!disposing)
        {
            SensorBase<MotionReading>::Dispose(disposing);
            return;
        }

        // Task P6-3/P7-2: see Compass::Dispose(bool)'s identical fix for
        // the full rationale.
        if (!ClaimDisposalOnce())
        {
            WaitForDisposalToComplete();
            return;
        }

        if (started_)
        {
            Stop();
        }

        {
            std::lock_guard<std::mutex> lock(instanceCountMutex_);
            --instanceCount_;
            if (instanceCount_ < 0)
            {
                instanceCount_ = 0;
            }
        }

        SensorBase<MotionReading>::Dispose(disposing);
    }

    GetTypeNameCPP(Motion, "Microsoft.Devices.Sensors.Motion")
} // namespace Microsoft::Devices::Sensors

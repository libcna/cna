// SPDX-License-Identifier: MS-PL

#include "Microsoft/Devices/Sensors/Compass.hpp"

#include "System/ObjectDisposedException.hpp"

namespace Microsoft::Devices::Sensors
{
    int Compass::instanceCount_ = 0;
    std::mutex Compass::instanceCountMutex_;

    bool Compass::getIsSupportedProperty()
    {
        // SDL3 exposes no magnetometer/compass API on any supported platform.
        return false;
    }

    SensorState Compass::getStateProperty() const
    {
        System::ObjectDisposedException::ThrowIf(getIsDisposedProperty(), "Compass");
        return state_;
    }

    Compass::Compass()
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
                    "The limit of 10 simultaneous instances of the Compass class per application has been exceeded.");
            }

            ++instanceCount_;
        }

        const bool supported = getIsSupportedProperty();
        state_ = supported ? SensorState::Initializing : SensorState::NotSupported;
        setIsSupportedProperty(supported);
    }

    Compass::~Compass()
    {
        if (!getIsDisposedProperty())
        {
            Dispose(true);
        }
    }

    void Compass::Start()
    {
        System::ObjectDisposedException::ThrowIf(getIsDisposedProperty(), "Compass");

        state_ = SensorState::NotSupported;
        throw SensorFailedException(
            "Failed to start compass data acquisition. The compass sensor is not supported on this platform.");
    }

    void Compass::Stop()
    {
        System::ObjectDisposedException::ThrowIf(getIsDisposedProperty(), "Compass");

        started_ = false;
        state_ = SensorState::Disabled;
    }

    void Compass::Dispose(bool disposing)
    {
        if (!getIsDisposedProperty() && disposing)
        {
            if (started_)
            {
                Stop();
            }

            std::lock_guard<std::mutex> lock(instanceCountMutex_);
            --instanceCount_;
            if (instanceCount_ < 0)
            {
                instanceCount_ = 0;
            }
        }

        SensorBase<CompassReading>::Dispose(disposing);
    }

    GetTypeNameCPP(Compass, "Microsoft.Devices.Sensors.Compass")
} // namespace Microsoft::Devices::Sensors

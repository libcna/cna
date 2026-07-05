// SPDX-License-Identifier: MS-PL

#include "Microsoft/Devices/Sensors/Motion.hpp"

#include "Microsoft/Devices/Sensors/Detail/AndroidMotionBackend.hpp"
#include "System/ObjectDisposedException.hpp"

namespace Microsoft::Devices::Sensors
{
    int Motion::instanceCount_ = 0;
    std::mutex Motion::instanceCountMutex_;

    bool Motion::getIsSupportedProperty()
    {
#if defined(__ANDROID__)
        // Stateless probe, independent of any Motion instance (matching the
        // real WP7 API's static IsSupported contract) — cheap, does not
        // hold any resource open.
        Detail::AndroidMotionBackend probe;
        return probe.IsSupported();
#else
        // SDL3 exposes no fused-orientation/motion API on any supported platform.
        return false;
#endif
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

#if defined(__ANDROID__)
        backend_ = std::make_unique<Detail::AndroidMotionBackend>();
#endif

        const bool supported = backend_ ? backend_->IsSupported() : getIsSupportedProperty();
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

        if (backend_ && backend_->IsSupported())
        {
            const bool started = backend_->Start(
                getTimeBetweenUpdatesProperty(),
                [this](const MotionReading& reading)
                {
                    setIsDataValidProperty(true);
                    setCurrentValueProperty(reading);
                });

            if (started)
            {
                started_ = true;
                state_ = SensorState::Ready;
                return;
            }
        }

        state_ = SensorState::NotSupported;
        throw SensorFailedException("Motion is not supported on this platform.");
    }

    void Motion::Stop()
    {
        System::ObjectDisposedException::ThrowIf(getIsDisposedProperty(), "Motion");

        if (backend_)
        {
            backend_->Stop();
        }

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

    void Motion::SetBackendForTesting(std::unique_ptr<Detail::IMotionBackend> backend)
    {
        backend_ = std::move(backend);
        setIsSupportedProperty(backend_ ? backend_->IsSupported() : getIsSupportedProperty());
    }

    GetTypeNameCPP(Motion, "Microsoft.Devices.Sensors.Motion")
} // namespace Microsoft::Devices::Sensors

// SPDX-License-Identifier: MS-PL

#include "Microsoft/Devices/Sensors/Compass.hpp"

#include "Microsoft/Devices/Sensors/Detail/AndroidCompassBackend.hpp"
#include "System/ObjectDisposedException.hpp"

namespace Microsoft::Devices::Sensors
{
    int Compass::instanceCount_ = 0;
    std::mutex Compass::instanceCountMutex_;

    bool Compass::getIsSupportedProperty()
    {
#if defined(__ANDROID__)
        // Stateless probe, independent of any Compass instance (matching
        // the real WP7 API's static IsSupported contract) — cheap, does
        // not hold any resource open (Detail::AndroidSensorBridge::
        // IsAvailable()'s own discipline).
        Detail::AndroidCompassBackend probe;
        return probe.IsSupported();
#else
        // SDL3 exposes no magnetometer/compass API on any supported platform.
        return false;
#endif
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

#if defined(__ANDROID__)
        backend_ = std::make_unique<Detail::AndroidCompassBackend>();
#endif

        const bool supported = backend_ ? backend_->IsSupported() : getIsSupportedProperty();
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

        if (backend_ && backend_->IsSupported())
        {
            const bool started = backend_->Start(
                getTimeBetweenUpdatesProperty(),
                [this](const CompassReading& reading)
                {
                    setIsDataValidProperty(true);
                    setCurrentValueProperty(reading);
                },
                [this]()
                {
                    if (!Calibrate.Empty())
                    {
                        CalibrationEventArgs args;
                        Calibrate.Raise(static_cast<System::Object*>(this), args);
                    }
                });

            if (started)
            {
                started_ = true;
                state_ = SensorState::Ready;
                return;
            }
        }

        state_ = SensorState::NotSupported;
        throw SensorFailedException(
            "Failed to start compass data acquisition. The compass sensor is not supported on this platform.");
    }

    void Compass::Stop()
    {
        System::ObjectDisposedException::ThrowIf(getIsDisposedProperty(), "Compass");

        if (backend_)
        {
            backend_->Stop();
        }

        started_ = false;
        state_ = SensorState::Disabled;
    }

    void Compass::Dispose(bool disposing)
    {
        if (!disposing)
        {
            SensorBase<CompassReading>::Dispose(disposing);
            return;
        }

        // Task P6-3/P7-2: ClaimDisposalOnce() closes a race where two
        // threads calling Dispose() on the same instance concurrently could
        // both pass the getIsDisposedProperty() check and both decrement
        // instanceCount_ for what should be a single logical disposal — see
        // SensorBase::Dispose()'s doc comment for the full rationale. The
        // loser waits for the winner's cleanup to actually finish (Task
        // P7-2) instead of proceeding immediately. Was latent when this was
        // first written (Start() always threw before setting started_ true,
        // so the Stop() call below was never actually reached) — fixed
        // uniformly with Accelerometer/Gyroscope anyway, anticipating a real
        // backend (plan_devices_phase7.md's Audit finding B). That
        // anticipated backend now exists (Task DEVICES-0095): on Android,
        // Start() can genuinely succeed and set started_ true, so this path
        // is real, not just defensive, on that platform.
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

        SensorBase<CompassReading>::Dispose(disposing);
    }

    void Compass::SetBackendForTesting(std::unique_ptr<Detail::ICompassBackend> backend)
    {
        backend_ = std::move(backend);
        setIsSupportedProperty(backend_ ? backend_->IsSupported() : getIsSupportedProperty());
    }

    GetTypeNameCPP(Compass, "Microsoft.Devices.Sensors.Compass")
} // namespace Microsoft::Devices::Sensors

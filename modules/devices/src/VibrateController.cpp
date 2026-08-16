// SPDX-License-Identifier: MS-PL

#include "Microsoft/Devices/VibrateController.hpp"

#include <algorithm>
#include <cmath>

#include "Detail/PlatformVibrateBackend.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

namespace Microsoft::Devices
{
    namespace
    {
        void ValidateVibrationDuration(const System::TimeSpan& duration)
        {
            static const System::TimeSpan MaxVibrationDuration = System::TimeSpan::FromSeconds(5);

            if (duration < System::TimeSpan::Zero || duration > MaxVibrationDuration)
            {
                throw System::ArgumentOutOfRangeException(
                    "duration",
                    duration.ToString(),
                    "'duration' must be between TimeSpan.Zero and TimeSpan.FromSeconds(5).");
            }
        }

        /**
         * @brief Clamps a vibration intensity/motor-magnitude value to `[0, 1]`, canonicalizing NaN.
         *
         * Task VIB2-002 (2026-07-17, external audit `audit_devices_2026-07-17.md`):
         * `std::clamp(v, 0.0f, 1.0f)` alone leaves NaN unchanged -- every
         * comparison against NaN is `false`, so `std::clamp`'s own
         * `v < lo ? lo : (hi < v ? hi : v)` falls through to returning `v`
         * itself. A native implementation must not receive NaN before converting
         * a normalized magnitude to its integer motor scale. True +/-infinity need no special
         * handling: both comparisons against a finite bound are
         * well-defined for infinity, so `std::clamp` already correctly
         * saturates it to `0.0f`/`1.0f`. Canonicalizes NaN to `0.0f`
         * ("no vibration") rather than throwing, matching this API's own
         * established policy of silently correcting out-of-range input
         * (see `StartWithOutOfRangeIntensityIsClampedSilentlyAndDoesNotThrow`).
         */
        [[nodiscard]] float CanonicalizeVibrationMagnitude(float value)
        {
            if (std::isnan(value))
            {
                return 0.0f;
            }
            return std::clamp(value, 0.0f, 1.0f);
        }
    } // namespace

    VibrateController* VibrateController::getDefaultProperty()
    {
        static VibrateController instance;
        return &instance;
    }

    VibrateController::VibrateController()
        : backend_(std::make_unique<Detail::PlatformVibrateBackend>())
    {
    }

    VibrateController::~VibrateController() = default;

    void VibrateController::ShutdownBackendForPlatform()
    {
        std::lock_guard<std::mutex> lock(backendMutex_);
        backend_.reset();
    }

    void VibrateController::SetBackendForTesting(std::unique_ptr<Detail::IVibrateBackend> backend)
    {
        std::lock_guard<std::mutex> lock(backendMutex_);

        if (backend != nullptr)
        {
            backend_ = std::move(backend);
        }
        else
        {
            backend_ = std::make_unique<Detail::PlatformVibrateBackend>();
        }
    }

    void VibrateController::Start(const System::TimeSpan& duration)
    {
        Start(duration, 1.0f);
    }

    void VibrateController::Start(const System::TimeSpan& duration, float intensity)
    {
        ValidateVibrationDuration(duration);
        const float clampedIntensity = CanonicalizeVibrationMagnitude(intensity);

        std::lock_guard<std::mutex> lock(backendMutex_);
        if (backend_ != nullptr)
        {
            backend_->Start(duration, clampedIntensity);
        }
    }

    void VibrateController::Stop()
    {
        std::lock_guard<std::mutex> lock(backendMutex_);
        if (backend_ != nullptr)
        {
            backend_->Stop();
        }
    }

    bool VibrateController::getIsSupportedProperty()
    {
        std::lock_guard<std::mutex> lock(backendMutex_);
        return backend_ != nullptr && backend_->IsSupported();
    }

    std::string VibrateController::getDeviceNameProperty()
    {
        std::lock_guard<std::mutex> lock(backendMutex_);
        return backend_ != nullptr ? backend_->GetDeviceName() : std::string{};
    }

    void VibrateController::StartLeftRight(float largeMotor, float smallMotor, const System::TimeSpan& duration)
    {
        ValidateVibrationDuration(duration);
        const float clampedLarge = CanonicalizeVibrationMagnitude(largeMotor);
        const float clampedSmall = CanonicalizeVibrationMagnitude(smallMotor);

        std::lock_guard<std::mutex> lock(backendMutex_);
        if (backend_ != nullptr)
        {
            backend_->StartLeftRight(clampedLarge, clampedSmall, duration);
        }
    }
} // namespace Microsoft::Devices

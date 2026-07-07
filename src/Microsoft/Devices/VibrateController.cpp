// SPDX-License-Identifier: MS-PL

#include "Microsoft/Devices/VibrateController.hpp"

#include <algorithm>

#include "Microsoft/Devices/Detail/SdlHapticVibrateBackend.hpp"
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
    } // namespace

    VibrateController* VibrateController::getDefaultProperty()
    {
        static VibrateController instance;
        return &instance;
    }

    VibrateController::VibrateController()
        : backend_(std::make_unique<Detail::SdlHapticVibrateBackend>())
    {
    }

    VibrateController::~VibrateController() = default;

    void VibrateController::SetBackendForTesting(std::unique_ptr<Detail::IVibrateBackend> backend)
    {
        std::lock_guard<std::mutex> lock(backendMutex_);

        if (backend != nullptr)
        {
            backend_ = std::move(backend);
        }
        else
        {
            backend_ = std::make_unique<Detail::SdlHapticVibrateBackend>();
        }
    }

    void VibrateController::Start(const System::TimeSpan& duration)
    {
        Start(duration, 1.0f);
    }

    void VibrateController::Start(const System::TimeSpan& duration, float intensity)
    {
        ValidateVibrationDuration(duration);
        const float clampedIntensity = std::clamp(intensity, 0.0f, 1.0f);

        std::lock_guard<std::mutex> lock(backendMutex_);
        backend_->Start(duration, clampedIntensity);
    }

    void VibrateController::Stop()
    {
        std::lock_guard<std::mutex> lock(backendMutex_);
        backend_->Stop();
    }

    bool VibrateController::getIsSupportedProperty()
    {
        std::lock_guard<std::mutex> lock(backendMutex_);
        return backend_->IsSupported();
    }

    std::string VibrateController::getDeviceNameProperty()
    {
        std::lock_guard<std::mutex> lock(backendMutex_);
        return backend_->GetDeviceName();
    }

    void VibrateController::StartLeftRight(float largeMotor, float smallMotor, const System::TimeSpan& duration)
    {
        ValidateVibrationDuration(duration);
        const float clampedLarge = std::clamp(largeMotor, 0.0f, 1.0f);
        const float clampedSmall = std::clamp(smallMotor, 0.0f, 1.0f);

        std::lock_guard<std::mutex> lock(backendMutex_);
        backend_->StartLeftRight(clampedLarge, clampedSmall, duration);
    }
} // namespace Microsoft::Devices

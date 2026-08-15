// SPDX-License-Identifier: MS-PL

#pragma once

#include <string>

#include "System/TimeSpan.hpp"

namespace Microsoft::Devices::Detail
{
    /**
     * @brief Platform/device vibration-motor backend interface (Task VIB-002).
     *
     * `VibrateController` selects `PlatformVibrateBackend` at construction time and calls
     * through this interface for every operation. Device selection and native details belong to
     * `IPlatformHaptics`; this seam keeps the controller testable and separates the strict
     * phone/device vibration target from richer platform haptic devices. Making the
     * backend swappable also lets tests inject a fake implementation
     * (`VibrateController::SetBackendForTesting()`) instead of depending on
     * whatever haptic hardware (or lack of it) happens to be present.
     *
     * `VibrateController` itself is responsible for validating/clamping
     * every parameter (duration range, intensity/magnitude clamping to
     * `[0, 1]`) before calling through this interface — implementations can
     * assume every parameter they receive is already valid and do not need
     * to re-validate or throw.
     */
    class IVibrateBackend
    {
    public:
        virtual ~IVibrateBackend() = default;

        /**
         * @brief Starts single-motor vibration for the given duration/intensity.
         *
         * Must be a silent no-op if no usable vibration device is available,
         * matching `VibrateController::Start()`'s own documented contract.
         *
         * @param duration Already-validated duration, in
         * `[System::TimeSpan::Zero, System::TimeSpan::FromSeconds(5)]`.
         * @param intensity Already-clamped rumble strength, in `[0.0f, 1.0f]`.
         */
        virtual void Start(const System::TimeSpan& duration, float intensity) = 0;

        /**
         * @brief Immediately stops any vibration started via Start()/StartLeftRight().
         *
         * Safe to call even if nothing is currently vibrating, or if no
         * usable vibration device is available.
         */
        virtual void Stop() = 0;

        /**
         * @brief Cheap probe: true if a usable vibration device is available.
         *
         * Must not hold a device open as a side effect of probing beyond
         * what is already open from a prior Start() call.
         */
        [[nodiscard]] virtual bool IsSupported() = 0;

        /**
         * @brief Gets the name of the device Start() would use.
         *
         * @return Device name, or an empty string if none is available.
         */
        [[nodiscard]] virtual std::string GetDeviceName() = 0;

        /**
         * @brief Starts independent large/small motor vibration for the given duration.
         *
         * Must be a silent no-op if the device exposes no dual-motor rumble
         * capability, matching `VibrateController::StartLeftRight()`'s own
         * documented contract.
         *
         * @param largeMotor Already-clamped low-frequency motor strength, in `[0.0f, 1.0f]`.
         * @param smallMotor Already-clamped high-frequency motor strength, in `[0.0f, 1.0f]`.
         * @param duration Already-validated duration, in
         * `[System::TimeSpan::Zero, System::TimeSpan::FromSeconds(5)]`.
         */
        virtual void StartLeftRight(float largeMotor, float smallMotor, const System::TimeSpan& duration) = 0;
    };
} // namespace Microsoft::Devices::Detail

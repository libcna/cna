// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/PlatformEvent.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace CNA::Platform {

    /** @brief Stable identity and simple-rumble capability of one haptic device. */
    struct HapticInfo
    {
        /** @brief Device id, stable while this haptic device remains connected. */
        DeviceId id = 0;
        /** @brief Human-readable device name, or empty when unavailable. */
        std::string name;
        /** @brief Whether the device supports the simple rumble convenience effect. */
        bool rumbleSupported = false;
    };

    /**
     * @brief Drives standalone haptic devices.
     *
     * Separate from `IPlatformGamepad::SetRumble` because the callers are genuinely different:
     * `Microsoft::Devices::VibrateController` drives the *device's own* haptics with no gamepad
     * involved, which is the normal case on a phone. PLAT-24 deferred this interface until a
     * distinct caller showed up; `modules/devices` and `modules/input` are two.
     *
     * Devices are addressed by the same stable-id convention as the other input services. An id
     * that is no longer connected reports failure rather than throwing — a device being unplugged
     * mid-effect is ordinary, not exceptional.
     */
    class IPlatformHaptics
    {
    public:
        /** @brief Destroys the service, closing any device it opened. */
        virtual ~IPlatformHaptics() = default;

        /**
         * @brief Gets connected devices in deterministic id order.
         * @return Device descriptors; empty when none are present.
         */
        [[nodiscard]] virtual std::vector<HapticInfo> GetHaptics() const = 0;

        /**
         * @brief Gets the platform's preferred non-gamepad vibration device.
         *
         * This is deliberately selected by the platform rather than by `VibrateController`:
         * identifying a phone vibration motor while excluding haptics owned by connected
         * gamepads requires native device correlation that must not leak into `Microsoft::Devices`.
         * The query does not retain an opened device or start an effect.
         *
         * @return The preferred device, or no value when the host has no suitable rumble device.
         */
        [[nodiscard]] virtual std::optional<HapticInfo> GetDefaultVibrationDevice() const = 0;

        /**
         * @brief Gets whether an id currently names a connected haptic device.
         * @param id The device to query.
         * @return True while the device is connected.
         */
        [[nodiscard]] virtual bool IsConnected(DeviceId id) const = 0;

        /**
         * @brief Gets whether a device can play a simple rumble effect.
         *
         * Not every haptic device supports rumble; some expose only richer effect types. A
         * caller checks this rather than discovering it from a failed play.
         *
         * @param id The device to query.
         * @return True if simple rumble is supported.
         */
        [[nodiscard]] virtual bool SupportsRumble(DeviceId id) const = 0;

        /**
         * @brief Opens a device if needed and initializes its simple-rumble effect.
         * @param id The device to initialize.
         * @return True if rumble is ready to play.
         */
        virtual bool InitializeRumble(DeviceId id) = 0;

        /**
         * @brief Plays a simple rumble effect.
         *
         * @param id The device to rumble.
         * @param strength Intensity in [0, 1]; values outside are clamped.
         * @param durationMilliseconds How long to rumble for.
         * @return True if the device accepted the effect.
         */
        virtual bool PlayRumble(DeviceId id, float strength,
                                std::uint32_t durationMilliseconds) = 0;

        /**
         * @brief Plays a two-motor low/high-frequency effect.
         *
         * Any simple-rumble or previous two-motor effect owned by this service on the same device
         * is stopped first, so the two paths never layer unintentionally.
         *
         * @param id The device to rumble.
         * @param largeMotor Low-frequency motor intensity in [0, 1]; clamped by the implementation.
         * @param smallMotor High-frequency motor intensity in [0, 1]; clamped by the implementation.
         * @param durationMilliseconds How long to rumble for.
         * @return True if the device accepted the effect; false when unsupported or disconnected.
         */
        virtual bool PlayLeftRight(DeviceId id, float largeMotor, float smallMotor,
                                   std::uint32_t durationMilliseconds) = 0;

        /**
         * @brief Stops a rumble effect in progress.
         *
         * @param id The device to stop.
         * @return True if the device accepted the request.
         */
        virtual bool StopRumble(DeviceId id) = 0;

        /**
         * @brief Stops every effect owned by this service on one device.
         * @param id The device to stop.
         * @return True if the device accepted the request.
         */
        virtual bool StopAll(DeviceId id) = 0;
    };

} // namespace CNA::Platform

// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/PlatformEvent.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace CNA::Platform {

    /** @brief Platform-neutral force-feedback effect family. */
    enum class HapticEffectType
    {
        Constant,
        Sine,
        Square,
        Triangle,
        SawtoothUp,
        SawtoothDown,
        Ramp,
        Spring,
        Damper,
        Inertia,
        Friction,
        LeftRight,
        Custom
    };

    /** @brief Coordinate system used by a force-feedback direction. */
    enum class HapticDirectionType
    {
        Polar,
        Cartesian,
        Spherical,
        SteeringAxis
    };

    /** @brief Direction of a force-feedback effect. */
    struct HapticDirection
    {
        HapticDirectionType type = HapticDirectionType::Polar;
        std::array<std::int32_t, 3> values{};
    };

    /**
     * @brief Platform-neutral force-feedback descriptor.
     *
     * Only the fields belonging to `type` are consumed. Magnitudes use the portable signed or
     * unsigned 16-bit ranges; durations are milliseconds. A platform implementation translates
     * this value into its native representation at the final boundary.
     */
    struct HapticEffect
    {
        HapticEffectType type = HapticEffectType::Constant;
        HapticDirection direction;
        std::uint32_t length = 0;
        std::uint16_t delay = 0;
        std::uint16_t button = 0;
        std::uint16_t interval = 0;
        std::int16_t level = 0;
        std::uint16_t period = 0;
        std::int16_t magnitude = 0;
        std::int16_t offset = 0;
        std::uint16_t phase = 0;
        std::int16_t rampStart = 0;
        std::int16_t rampEnd = 0;
        std::array<std::uint16_t, 3> rightSaturation{};
        std::array<std::uint16_t, 3> leftSaturation{};
        std::array<std::int16_t, 3> rightCoefficient{};
        std::array<std::int16_t, 3> leftCoefficient{};
        std::array<std::uint16_t, 3> deadband{};
        std::array<std::int16_t, 3> center{};
        std::uint16_t largeMagnitude = 0;
        std::uint16_t smallMagnitude = 0;
        std::uint8_t customChannels = 0;
        std::uint16_t customPeriod = 0;
        std::vector<std::uint16_t> customData;
        std::uint16_t attackLength = 0;
        std::uint16_t attackLevel = 0;
        std::uint16_t fadeLength = 0;
        std::uint16_t fadeLevel = 0;
    };

    /** @brief Static capabilities of one opened force-feedback device. */
    struct HapticDeviceCapabilities
    {
        std::string name;
        /** @brief Portable feature bits; values match CNA::Input::HapticFeatureEXT. */
        std::uint32_t features = 0;
        int axisCount = 0;
        int maxEffects = -1;
        int maxEffectsPlaying = -1;
        bool rumbleSupported = false;
    };

    /** @brief One independently owned opened force-feedback device. */
    class IPlatformHapticDevice
    {
    public:
        virtual ~IPlatformHapticDevice() = default;
        [[nodiscard]] virtual HapticDeviceCapabilities GetCapabilities() const = 0;
        [[nodiscard]] virtual bool IsEffectSupported(const HapticEffect& effect) const = 0;
        virtual bool InitializeRumble() = 0;
        virtual bool PlayRumble(float strength, std::uint32_t durationMilliseconds) = 0;
        virtual bool StopRumble() = 0;
        [[nodiscard]] virtual int CreateEffect(const HapticEffect& effect) = 0;
        virtual bool UpdateEffect(int effectId, const HapticEffect& effect) = 0;
        virtual bool RunEffect(int effectId, std::uint32_t iterations) = 0;
        virtual bool StopEffect(int effectId) = 0;
        virtual void DestroyEffect(int effectId) = 0;
        [[nodiscard]] virtual bool GetEffectStatus(int effectId) const = 0;
        virtual bool StopAllEffects() = 0;
        virtual bool SetGain(int gain) = 0;
        virtual bool SetAutocenter(int autocenter) = 0;
        virtual bool Pause() = 0;
        virtual bool Resume() = 0;
    };

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

        /** @brief Opens a standalone device for advanced effects. */
        [[nodiscard]] virtual std::unique_ptr<IPlatformHapticDevice> Open(DeviceId id)
        {
            (void)id;
            return nullptr;
        }

        /** @brief Opens the haptic device associated with a joystick. */
        [[nodiscard]] virtual std::unique_ptr<IPlatformHapticDevice> OpenFromJoystick(DeviceId id)
        {
            (void)id;
            return nullptr;
        }

        /** @brief Opens the host mouse haptic device. */
        [[nodiscard]] virtual std::unique_ptr<IPlatformHapticDevice> OpenFromMouse()
        {
            return nullptr;
        }

        /** @brief Gets whether a joystick exposes force feedback. */
        [[nodiscard]] virtual bool IsJoystickHaptic(DeviceId id) const
        {
            (void)id;
            return false;
        }

        /** @brief Gets whether the host mouse exposes force feedback. */
        [[nodiscard]] virtual bool IsMouseHaptic() const { return false; }
    };

} // namespace CNA::Platform

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
        /** @brief Coordinate system used to interpret `values`. */
        HapticDirectionType type = HapticDirectionType::Polar;
        /** @brief Direction components; interpretation and used dimensions depend on `type`. */
        std::array<std::int32_t, 3> values{};
    };

    /**
     * @brief Platform-neutral force-feedback descriptor.
     *
     * Only the fields belonging to `type` are consumed. Magnitudes use the portable signed or
     * unsigned 16-bit ranges; durations are milliseconds. Constant uses `level`; periodic effects
     * use `period`, `magnitude`, `offset` and `phase`; Ramp uses `rampStart`/`rampEnd`; condition
     * effects use the six per-axis arrays; LeftRight uses its two motor magnitudes; Custom uses its
     * channel, period and sample fields. Constant, periodic, Ramp and Custom may use the envelope.
     * A platform implementation translates this value into its native representation only at the
     * final boundary.
     */
    struct HapticEffect
    {
        /** @brief Effect family selecting which remaining fields are meaningful. */
        HapticEffectType type = HapticEffectType::Constant;
        /** @brief Direction the force comes from for directional effect families. */
        HapticDirection direction;
        /** @brief Effect duration in milliseconds; UINT32_MAX means unlimited. */
        std::uint32_t length = 0;
        /** @brief Delay before effect playback starts, in milliseconds. */
        std::uint16_t delay = 0;
        /** @brief One-based trigger button index, or zero when there is no trigger button. */
        std::uint16_t button = 0;
        /** @brief Minimum interval between button-triggered replays, in milliseconds. */
        std::uint16_t interval = 0;
        /** @brief Signed strength of a Constant effect. */
        std::int16_t level = 0;
        /** @brief Wave period of a periodic effect, in milliseconds. */
        std::uint16_t period = 0;
        /** @brief Peak periodic magnitude; a negative value adds 180 degrees of phase. */
        std::int16_t magnitude = 0;
        /** @brief Mean (DC offset) value of a periodic wave. */
        std::int16_t offset = 0;
        /** @brief Positive periodic phase shift in hundredths of a degree. */
        std::uint16_t phase = 0;
        /** @brief Starting signed strength of a Ramp effect. */
        std::int16_t rampStart = 0;
        /** @brief Ending signed strength of a Ramp effect. */
        std::int16_t rampEnd = 0;
        /** @brief Per-axis positive-side saturation for a condition effect. */
        std::array<std::uint16_t, 3> rightSaturation{};
        /** @brief Per-axis negative-side saturation for a condition effect. */
        std::array<std::uint16_t, 3> leftSaturation{};
        /** @brief Per-axis force growth rate toward the positive side. */
        std::array<std::int16_t, 3> rightCoefficient{};
        /** @brief Per-axis force growth rate toward the negative side. */
        std::array<std::int16_t, 3> leftCoefficient{};
        /** @brief Per-axis condition-effect dead-zone size. */
        std::array<std::uint16_t, 3> deadband{};
        /** @brief Per-axis condition-effect dead-zone center. */
        std::array<std::int16_t, 3> center{};
        /** @brief Low-frequency motor magnitude for a LeftRight effect. */
        std::uint16_t largeMagnitude = 0;
        /** @brief High-frequency motor magnitude for a LeftRight effect. */
        std::uint16_t smallMagnitude = 0;
        /** @brief Number of axes driven by a Custom waveform; zero describes no samples. */
        std::uint8_t customChannels = 0;
        /** @brief Sample period of a Custom waveform, in milliseconds. */
        std::uint16_t customPeriod = 0;
        /** @brief Interleaved Custom samples; size is channel count times sample count. */
        std::vector<std::uint16_t> customData;
        /** @brief Duration of the envelope attack ramp, in milliseconds. */
        std::uint16_t attackLength = 0;
        /** @brief Effect level at the start of the envelope attack. */
        std::uint16_t attackLevel = 0;
        /** @brief Duration of the envelope fade ramp, in milliseconds. */
        std::uint16_t fadeLength = 0;
        /** @brief Effect level at the end of the envelope fade. */
        std::uint16_t fadeLevel = 0;
    };

    /** @brief Static capabilities of one opened force-feedback device. */
    struct HapticDeviceCapabilities
    {
        /** @brief Human-readable native device name, or empty when unavailable. */
        std::string name;
        /** @brief Portable feature bits; values match CNA::Input::HapticFeatureEXT. */
        std::uint32_t features = 0;
        /** @brief Number of independently addressable force axes, or zero when unknown. */
        int axisCount = 0;
        /** @brief Maximum uploaded effects, or -1 when the implementation cannot report it. */
        int maxEffects = -1;
        /** @brief Maximum concurrent effects, or -1 when the implementation cannot report it. */
        int maxEffectsPlaying = -1;
        /** @brief Whether the simple strength-and-duration rumble path is supported. */
        bool rumbleSupported = false;
    };

    /** @brief One independently owned opened force-feedback device. */
    class IPlatformHapticDevice
    {
    public:
        virtual ~IPlatformHapticDevice() = default;

        /** @brief Gets the immutable capabilities of this opened device. */
        [[nodiscard]] virtual HapticDeviceCapabilities GetCapabilities() const = 0;

        /**
         * @brief Tests whether the device can represent an effect descriptor.
         * @param effect Descriptor to test without uploading it.
         * @return True when the device reports support for the selected family and parameters.
         */
        [[nodiscard]] virtual bool IsEffectSupported(const HapticEffect& effect) const = 0;

        /** @brief Initializes the simple-rumble path. @return True when rumble is ready. */
        virtual bool InitializeRumble() = 0;

        /**
         * @brief Plays the initialized simple-rumble effect.
         * @param strength Normalized strength in [0, 1].
         * @param durationMilliseconds Playback duration in milliseconds.
         * @return True when the device accepted the request.
         */
        virtual bool PlayRumble(float strength, std::uint32_t durationMilliseconds) = 0;

        /** @brief Stops simple rumble. @return True when the device accepted the request. */
        virtual bool StopRumble() = 0;

        /**
         * @brief Uploads an advanced effect.
         * @param effect Effect descriptor whose used fields are selected by its type.
         * @return A non-negative device-local effect id, or -1 on refusal/failure.
         */
        [[nodiscard]] virtual int CreateEffect(const HapticEffect& effect) = 0;

        /**
         * @brief Replaces the parameters of an uploaded effect.
         * @param effectId Id returned by `CreateEffect`.
         * @param effect Replacement descriptor.
         * @return True when the update succeeded.
         */
        virtual bool UpdateEffect(int effectId, const HapticEffect& effect) = 0;

        /**
         * @brief Plays an uploaded effect.
         * @param effectId Id returned by `CreateEffect`.
         * @param iterations Number of repetitions; one plays once.
         * @return True when playback started.
         */
        virtual bool RunEffect(int effectId, std::uint32_t iterations) = 0;

        /**
         * @brief Stops one uploaded effect.
         * @param effectId Id returned by `CreateEffect`.
         * @return True when the device accepted the request.
         */
        virtual bool StopEffect(int effectId) = 0;

        /**
         * @brief Frees one uploaded effect; the id is invalid afterwards.
         * @param effectId Id returned by `CreateEffect`.
         */
        virtual void DestroyEffect(int effectId) = 0;

        /**
         * @brief Gets whether one uploaded effect is currently playing.
         * @param effectId Id returned by `CreateEffect`.
         * @return True only while that effect is playing.
         */
        [[nodiscard]] virtual bool GetEffectStatus(int effectId) const = 0;

        /** @brief Stops every playing effect on this device. @return True on success. */
        virtual bool StopAllEffects() = 0;

        /**
         * @brief Sets the device-wide effect gain.
         * @param gain Gain from 0 (silent) to 100 (maximum).
         * @return True when the device accepted the value.
         */
        virtual bool SetGain(int gain) = 0;

        /**
         * @brief Sets device autocenter strength, such as steering-wheel centering.
         * @param autocenter Strength from 0 (disabled) to 100 (maximum).
         * @return True when the device accepted the value.
         */
        virtual bool SetAutocenter(int autocenter) = 0;

        /** @brief Pauses all effect playback. @return True when the device accepted the request. */
        virtual bool Pause() = 0;

        /** @brief Resumes effect playback after `Pause`. @return True on success. */
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

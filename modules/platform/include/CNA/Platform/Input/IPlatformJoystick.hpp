// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/PlatformEvent.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace CNA::Platform {

    /** @brief Broad physical class of an unmapped joystick. */
    enum class JoystickKind
    {
        Unknown,
        Gamepad,
        Wheel,
        ArcadeStick,
        FlightStick,
        DancePad,
        Guitar,
        DrumKit,
        ArcadePad,
        Throttle
    };

    /** @brief One of the nine reachable positions of a joystick POV hat. */
    enum class JoystickHat
    {
        Centered,
        Up,
        Right,
        Down,
        Left,
        RightUp,
        RightDown,
        LeftUp,
        LeftDown
    };

    /** @brief Battery and charge state of a raw joystick. */
    enum class JoystickPowerState
    {
        Error,
        Unknown,
        OnBattery,
        NoBattery,
        Charging,
        Charged
    };

    /** @brief Relative motion reported by one joystick trackball during an update. */
    struct JoystickBallDelta
    {
        /** @brief Horizontal relative motion. */
        int x = 0;
        /** @brief Vertical relative motion. */
        int y = 0;
    };

    /** @brief Stable identity of one connected raw joystick. */
    struct JoystickInfo
    {
        /** @brief Device id shared with joystick `DeviceEvent`s. */
        DeviceId id = 0;
        /** @brief Human-readable device name, or empty when unavailable. */
        std::string name;
        /** @brief Broad physical device class. */
        JoystickKind kind = JoystickKind::Unknown;
    };

    /** @brief Hardware shape, identity and current power state of a raw joystick. */
    struct JoystickCapabilities
    {
        /** @brief Whether the requested device is connected. */
        bool connected = false;
        /** @brief Number of raw axes. */
        int axisCount = 0;
        /** @brief Number of raw buttons. */
        int buttonCount = 0;
        /** @brief Number of POV hats. */
        int hatCount = 0;
        /** @brief Number of trackballs. */
        int ballCount = 0;
        /** @brief Broad physical device class. */
        JoystickKind kind = JoystickKind::Unknown;
        /** @brief Human-readable device name, or empty when unavailable. */
        std::string name;
        /** @brief Platform-neutral hexadecimal hardware GUID. */
        std::string guid;
        /** @brief Current battery or charge state. */
        JoystickPowerState powerState = JoystickPowerState::Unknown;
        /** @brief Charge percentage, or -1 when unknown. */
        int powerPercent = -1;
    };

    /** @brief One frame-stable snapshot of an unmapped joystick. */
    struct JoystickSnapshot
    {
        /** @brief Raw signed axis values, preserving their device-defined order. */
        std::vector<std::int16_t> axes;
        /** @brief Button states, preserving their device-defined order. */
        std::vector<bool> buttons;
        /** @brief POV hat states, preserving their device-defined order. */
        std::vector<JoystickHat> hats;
        /** @brief Per-trackball relative motion consumed by this update. */
        std::vector<JoystickBallDelta> balls;
    };

    /**
     * @brief Enumerates and snapshots raw, unmapped joysticks.
     *
     * This is deliberately separate from `IPlatformGamepad`: a mapped gamepad has a fixed
     * semantic vocabulary, while a raw wheel, HOTAS or arcade controller can expose arbitrary
     * axes, buttons, POV hats and trackballs. The same physical device may appear through both
     * services without the two views sharing state.
     */
    class IPlatformJoystick
    {
    public:
        /** @brief Destroys the service, closing every raw device it owns. */
        virtual ~IPlatformJoystick() = default;

        /** @brief Reconciles hotplug state and publishes one snapshot for every device. */
        virtual void Update() = 0;

        /** @brief Gets deterministic descriptors for all currently connected joysticks. */
        [[nodiscard]] virtual std::vector<JoystickInfo> GetJoysticks() const = 0;

        /** @brief Gets whether a device id currently names an opened joystick. */
        [[nodiscard]] virtual bool IsConnected(DeviceId id) const = 0;

        /** @brief Gets hardware capabilities, or a disconnected value for an unknown id. */
        [[nodiscard]] virtual JoystickCapabilities GetCapabilities(DeviceId id) const = 0;

        /** @brief Gets the last published state, or an empty value for an unknown id. */
        [[nodiscard]] virtual JoystickSnapshot GetSnapshot(DeviceId id) const = 0;
    };

} // namespace CNA::Platform

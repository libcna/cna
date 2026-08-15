// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/PlatformEvent.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace CNA::Platform {

    /** @brief Number of mapped gamepad axes in the platform contract. */
    inline constexpr std::size_t GamepadAxisCount = 6;

    /** @brief Maximum number of XNA-compatible player slots. */
    inline constexpr int GamepadSlotCount = 4;

    /** @brief Broad physical class of a mapped controller. */
    enum class GamepadKind
    {
        Unknown,
        Gamepad,
        Wheel,
        ArcadeStick,
        FlightStick,
        DancePad,
        Guitar,
        DrumKit,
        BigButtonPad
    };

    /** @brief Controller family used when identity depends on the emulated model. */
    enum class GamepadModel
    {
        Unknown,
        Standard,
        Xbox360,
        XboxOne,
        PlayStation3,
        PlayStation4,
        PlayStation5,
        NintendoSwitchPro,
        NintendoSwitchJoyConLeft,
        NintendoSwitchJoyConRight,
        NintendoSwitchJoyConPair,
        GameCube
    };

    /** @brief How a gamepad is physically attached. */
    enum class GamepadConnectionState
    {
        Unknown,
        Wired,
        Wireless
    };

    /** @brief Battery and charge state of a gamepad. */
    enum class GamepadPowerState
    {
        Unknown,
        Error,
        OnBattery,
        Charging,
        Charged,
        NoBattery
    };

    /** @brief Printed label of a controller face button. */
    enum class GamepadButtonLabel
    {
        Unknown,
        A,
        B,
        X,
        Y,
        Cross,
        Circle,
        Square,
        Triangle
    };

    /** @brief A motion sensor embedded in a gamepad. */
    enum class GamepadSensor
    {
        Gyroscope,
        Accelerometer
    };

    /** @brief One three-component gamepad sensor reading. */
    struct GamepadSensorReading
    {
        /** @brief X component in the native sensor's documented units. */
        float x = 0.0f;
        /** @brief Y component in the native sensor's documented units. */
        float y = 0.0f;
        /** @brief Z component in the native sensor's documented units. */
        float z = 0.0f;
    };

    /** @brief One touch contact reported by a gamepad touchpad. */
    struct GamepadTouchpadFinger
    {
        /** @brief Whether this contact currently touches the pad. */
        bool down = false;
        /** @brief Normalised horizontal coordinate. */
        float x = 0.0f;
        /** @brief Normalised vertical coordinate. */
        float y = 0.0f;
        /** @brief Normalised contact pressure. */
        float pressure = 0.0f;
    };

    /** @brief Battery state returned by a gamepad. */
    struct GamepadPowerInfo
    {
        /** @brief Current battery or charge state. */
        GamepadPowerState state = GamepadPowerState::Unknown;
        /** @brief Charge percentage, or -1 when unknown. */
        int percent = -1;
    };

    /** @brief Stable device identity and descriptive metadata for one connected slot. */
    struct GamepadInfo
    {
        /** @brief Human-readable device name. */
        std::string name;
        /** @brief Platform-specific device path. */
        std::string path;
        /** @brief Device serial number, or empty when unavailable. */
        std::string serial;
        /** @brief USB vendor identifier, or zero when unavailable. */
        std::uint16_t vendor = 0;
        /** @brief USB product identifier, or zero when unavailable. */
        std::uint16_t product = 0;
        /** @brief Device firmware revision, or zero when unavailable. */
        std::uint16_t firmwareVersion = 0;
        /** @brief Steam Input handle, or zero when unavailable. */
        std::uint64_t steamHandle = 0;
        /** @brief Current wired/wireless attachment state. */
        GamepadConnectionState connectionState = GamepadConnectionState::Unknown;
        /** @brief Controller family exposed by the platform mapping layer. */
        GamepadModel model = GamepadModel::Unknown;
    };

    /** @brief Features supported by one connected gamepad. */
    struct GamepadCapabilities
    {
        /** @brief Whether the slot contains a connected mapped controller. */
        bool connected = false;
        /** @brief Bitmask of supported `GamepadButton` values. */
        std::uint32_t buttons = 0;
        /** @brief Bitmask produced by `GamepadAxisBit` for supported axes. */
        std::uint8_t axes = 0;
        /** @brief Broad physical controller class. */
        GamepadKind kind = GamepadKind::Unknown;
        /** @brief Whether the ordinary low/high-frequency motors are supported. */
        bool rumble = false;
        /** @brief Whether separate trigger motors are supported. */
        bool triggerRumble = false;
        /** @brief Whether an RGB light bar is supported. */
        bool lightBar = false;
        /** @brief Whether at least one touchpad is present. */
        bool touchpad = false;
        /** @brief Whether the controller has a gyroscope. */
        bool gyroscope = false;
        /** @brief Whether the controller has an accelerometer. */
        bool accelerometer = false;
    };

    /** @brief Returns the stable bit representing an axis in `GamepadCapabilities::axes`. */
    [[nodiscard]] constexpr std::uint8_t GamepadAxisBit(const GamepadAxis axis)
    {
        return static_cast<std::uint8_t>(1u << static_cast<unsigned>(axis));
    }

    /** @brief A gamepad snapshot taken at one instant. */
    struct GamepadSnapshot
    {
        /** @brief Whether a device is connected at this index. */
        bool connected = false;
        /** @brief Bitmask of held buttons, using the underlying values of `GamepadButton`. */
        std::uint32_t buttons = 0;
        /** @brief Values indexed by `GamepadAxis`; sticks are [-1,1], triggers [0,1]. */
        std::array<float, GamepadAxisCount> axes{};
        /** @brief Changes only when this slot's published state changes. */
        std::uint32_t packetNumber = 0;
    };

    /**
     * @brief Reads gamepads and drives their actuators.
     *
     * Kept inside the platform contract rather than split into its own module. The SDL1
     * joystick, SDL2 GameController and SDL3 Gamepad APIs differ considerably, but that
     * difference is a *capability* difference, which `Gamepad`, `GamepadRumble` and
     * `GamepadSensors` already express. Splitting the seam is deferred until a second
     * implementation shows the capability model cannot carry it.
     */
    class IPlatformGamepad
    {
    public:
        /** @brief Destroys the service. */
        virtual ~IPlatformGamepad() = default;

        /** @brief Updates all gamepad snapshots from the platform's current state. */
        virtual void Update() = 0;

        /**
         * @brief Gets the number of gamepad slots this platform reports.
         *
         * @return The slot count; zero when the platform has no gamepad support at all.
         */
        [[nodiscard]] virtual int GetCount() const = 0;

        /**
         * @brief Gets the most recent snapshot for one slot.
         *
         * @param index The slot to read.
         * @return The state as of the last Update(); a disconnected slot reports `connected = false`
         * rather than throwing, because polling an empty slot is ordinary control flow.
         */
        [[nodiscard]] virtual const GamepadSnapshot& GetSnapshot(int index) const = 0;

        /**
         * @brief Gets a device's human-readable name.
         *
         * @param index The slot to read.
         * @return The device name, or an empty string when the slot is empty.
         */
        [[nodiscard]] virtual std::string GetName(int index) const = 0;

        /** @brief Gets the cached capabilities of one slot, or an empty value when absent. */
        [[nodiscard]] virtual const GamepadCapabilities& GetCapabilities(int index) const = 0;

        /** @brief Gets cached identity metadata, or an empty value when the slot is absent. */
        [[nodiscard]] virtual const GamepadInfo& GetInfo(int index) const = 0;

        /**
         * @brief Starts rumble on a device.
         *
         * @param index The slot to rumble.
         * @param lowFrequency Low-frequency motor strength in [0, 1].
         * @param highFrequency High-frequency motor strength in [0, 1].
         * @param durationMilliseconds How long to rumble for.
         * @return True if the device accepted the command.
         * @throws PlatformNotSupportedException If the platform reports no `GamepadRumble` capability.
         */
        virtual bool SetRumble(int index, float lowFrequency, float highFrequency,
                               std::uint32_t durationMilliseconds) = 0;

        /** @brief Starts trigger-motor rumble; false means absent or unsupported. */
        virtual bool SetTriggerRumble(int index, float left, float right,
                                      std::uint32_t durationMilliseconds) = 0;

        /** @brief Sets an RGB light bar; false means absent or unsupported. */
        virtual bool SetLightBar(int index, std::uint8_t red, std::uint8_t green,
                                 std::uint8_t blue) = 0;

        /** @brief Reads an embedded sensor; false zeroes `reading`. */
        [[nodiscard]] virtual bool TryGetSensor(int index, GamepadSensor sensor,
                                                GamepadSensorReading& reading) = 0;

        /** @brief Gets the device's player-number indicator, or -1 when absent/unset. */
        [[nodiscard]] virtual int GetPlayerIndex(int index) const = 0;

        /** @brief Sets the device's player-number indicator; false means refused. */
        virtual bool SetPlayerIndex(int index, int playerIndex) = 0;

        /** @brief Gets battery state; an absent slot reports `Error` and -1 percent. */
        [[nodiscard]] virtual GamepadPowerInfo GetPowerInfo(int index) const = 0;

        /** @brief Gets the printed label for one physical button, or `Unknown`. */
        [[nodiscard]] virtual GamepadButtonLabel GetButtonLabel(int index,
                                                                GamepadButton button) const = 0;

        /** @brief Gets the number of touchpads, or zero when unsupported/absent. */
        [[nodiscard]] virtual int GetTouchpadCount(int index) const = 0;

        /** @brief Gets one touchpad's finger capacity, or zero for an invalid request. */
        [[nodiscard]] virtual int GetTouchpadFingerCount(int index, int touchpad) const = 0;

        /** @brief Reads one touch contact; false zeroes `finger`. */
        [[nodiscard]] virtual bool TryGetTouchpadFinger(int index, int touchpad, int fingerIndex,
                                                        GamepadTouchpadFinger& finger) const = 0;
    };

} // namespace CNA::Platform

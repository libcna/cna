// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/PlatformEvent.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace CNA::Platform {

    /** @brief A gamepad snapshot taken at one instant. */
    struct GamepadSnapshot
    {
        /** @brief Whether a device is connected at this index. */
        bool connected = false;
        /** @brief Bitmask of held buttons, using the underlying values of `GamepadButton`. */
        std::uint32_t buttons = 0;
        /** @brief Thumbstick positions in `GamepadAxis` order from left X through right Y. */
        std::vector<float> axes;
        /** @brief Trigger positions in left-then-right `GamepadAxis` order, normalised to [0, 1]. */
        std::vector<float> triggers;
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
    };

} // namespace CNA::Platform

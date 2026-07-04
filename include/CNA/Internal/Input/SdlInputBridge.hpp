// SPDX-License-Identifier: MS-PL
#pragma once

#include <SDL3/SDL.h>
#include <cstdint>
#include <string>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadCapabilities.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace CNA::Internal::Input
{
    /**
     * @brief Bridge between SDL3 events and CNA internal input state.
     *
     * This bridge knows SDL types, but exposes them only internally.
     *
     * @note Status: PARTIAL
     */
    class SdlInputBridge
    {
    public:
        /**
         * @brief Processes one SDL event and propagates relevant changes to InputManager.
         */
        static void ProcessEvent(const SDL_Event& event);

        /**
         * @brief Triggers rumble on the gamepad for the given player.
         * @return true if vibration was successfully set.
         */
        static bool SetVibration(
            Microsoft::Xna::Framework::PlayerIndex playerIndex,
            float leftMotor,
            float rightMotor
        );

        /**
         * @brief Triggers trigger rumble on the gamepad for the given player.
         * @return true if trigger vibration was successfully set.
         */
        static bool SetTriggerVibration(
            Microsoft::Xna::Framework::PlayerIndex playerIndex,
            float leftTrigger,
            float rightTrigger
        );

        /**
         * @brief Sets the light bar color on the gamepad for the given player, if supported.
         * No-op if the player has no connected gamepad.
         */
        static void SetLightBar(
            Microsoft::Xna::Framework::PlayerIndex playerIndex,
            Microsoft::Xna::Framework::Color color
        );

        /**
         * @brief Returns the SDL GUID string for the gamepad at the given player slot.
         */
        static std::string GetGUID(Microsoft::Xna::Framework::PlayerIndex playerIndex);

        /**
         * @brief Formats an FNA-style GamePad GUID string from a device's USB vendor/product IDs.
         *
         * Mirrors FNA's `GetGamePadGUID` formatting (`SDL3_FNAPlatform.cs:2176-2191`): returns
         * `"xinput"` when both IDs are zero, otherwise 8 lowercase hex chars — the 16-bit vendor
         * then product IDs each emitted little-endian (low byte first). Exposed for unit testing;
         * `GetGUID()` calls it (and additionally applies FNA's Valve-controller overrides).
         *
         * @param vendor The USB vendor ID.
         * @param product The USB product ID.
         * @return The FNA-style GUID string.
         */
        static std::string FormatGamePadGUIDEXT(std::uint16_t vendor, std::uint16_t product);

        /**
         * @brief Reads gyroscope sensor data for the gamepad at the given player, enabling
         * the sensor on first use. Returns false if no gamepad is connected or the sensor
         * is unavailable.
         */
        static bool GetGyro(
            Microsoft::Xna::Framework::PlayerIndex playerIndex,
            Microsoft::Xna::Framework::Vector3& gyro
        );

        /**
         * @brief Reads accelerometer sensor data for the gamepad at the given player, enabling
         * the sensor on first use. Returns false if no gamepad is connected or the sensor
         * is unavailable.
         */
        static bool GetAccelerometer(
            Microsoft::Xna::Framework::PlayerIndex playerIndex,
            Microsoft::Xna::Framework::Vector3& accel
        );

        /**
         * @brief Queries SDL for the actual hardware capabilities of the gamepad.
         */
        static Microsoft::Xna::Framework::Input::GamePadCapabilities GetCapabilities(
            Microsoft::Xna::Framework::PlayerIndex playerIndex
        );

        /**
         * @brief Translates a US-layout Keys value to the Keys value the current keyboard
         * layout produces at that same physical key position. Returns Keys::None if no
         * mapping exists in either direction.
         */
        static Microsoft::Xna::Framework::Input::Keys GetKeyFromScancode(
            Microsoft::Xna::Framework::Input::Keys scancode
        );
    };
}

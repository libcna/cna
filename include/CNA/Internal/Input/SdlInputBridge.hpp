// SPDX-License-Identifier: MS-PL
#pragma once

#include <SDL3/SDL.h>
#include <cstddef>
#include <cstdint>
#include <string>

#include "CNA/Input/GamePadButtonLabel.hpp"
#include "CNA/Input/PowerState.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Input/Buttons.hpp"
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
     */
    class SdlInputBridge
    {
    public:
        /**
         * @brief Processes one SDL event and propagates relevant changes to InputManager.
         */
        static void ProcessEvent(const SDL_Event& event);

        /**
         * @brief Ensures the SDL gamepad subsystem is initialized so gamepad events are delivered.
         *
         * SDL_INIT_VIDEO does not imply SDL_INIT_GAMEPAD, so without this no
         * SDL_EVENT_GAMEPAD_ADDED/REMOVED/AXIS/BUTTON events are ever produced — gamepads would be
         * invisible to GamePad::GetState. Idempotent (ref-counted by SDL); safe to call repeatedly.
         * Initializing the subsystem makes SDL enumerate already-connected pads and queue
         * SDL_EVENT_GAMEPAD_ADDED for each, so pads connected before the first frame become visible.
         * ProcessEvent() calls this lazily on first use; startup code may also call it explicitly.
         */
        static void EnsureGamepadSubsystemInitialized();

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
         * @brief NOXNA/EXT: the SDL device player index (LED number) of the connected pad, or -1.
         */
        static int GetPlayerIndex(Microsoft::Xna::Framework::PlayerIndex playerIndex);

        /**
         * @brief NOXNA/EXT: sets the SDL device player index (LED number). Returns success.
         */
        static bool SetPlayerIndex(Microsoft::Xna::Framework::PlayerIndex playerIndex, int index);

        /**
         * @brief NOXNA/EXT: the battery/charge state of the connected pad; fills `percent`
         * (0-100, or -1 unknown). Returns PowerStateEXT::Error if no gamepad is connected.
         */
        static CNA::Input::PowerStateEXT GetPowerInfo(
            Microsoft::Xna::Framework::PlayerIndex playerIndex, int& percent);

        /**
         * @brief NOXNA/EXT: the printed glyph label for a face button on the connected pad, or
         * Unknown if no gamepad is connected or the button has no label on this controller.
         */
        static CNA::Input::GamePadButtonLabelEXT GetButtonLabel(
            Microsoft::Xna::Framework::PlayerIndex playerIndex,
            Microsoft::Xna::Framework::Input::Buttons button);

        /** @brief NOXNA/EXT: controller name, or "" if disconnected/unknown. */
        static std::string GetName(Microsoft::Xna::Framework::PlayerIndex playerIndex);
        /** @brief NOXNA/EXT: OS device path, or "" if disconnected/unknown. */
        static std::string GetPath(Microsoft::Xna::Framework::PlayerIndex playerIndex);
        /** @brief NOXNA/EXT: hardware serial, or "" if disconnected/unavailable. */
        static std::string GetSerial(Microsoft::Xna::Framework::PlayerIndex playerIndex);
        /** @brief NOXNA/EXT: firmware version, or 0 if disconnected/unavailable. */
        static std::uint16_t GetFirmwareVersion(Microsoft::Xna::Framework::PlayerIndex playerIndex);
        /** @brief NOXNA/EXT: Steam Input handle, or 0 if disconnected/not a Steam controller. */
        static std::uint64_t GetSteamHandle(Microsoft::Xna::Framework::PlayerIndex playerIndex);

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
        /**
         * @brief Test-only: forces scancode mode on/off, overriding the cached
         *        `FNA_KEYBOARD_USE_SCANCODES` env value (which can't be changed in-process).
         * @param enabled True to force scancode mode on, false to force it off.
         */
        static void SetScancodeModeForTests(bool enabled);

        /** @brief Test-only: reverts scancode mode to the `FNA_KEYBOARD_USE_SCANCODES` env value. */
        static void ClearScancodeModeForTests();

        /**
         * @brief Test-only: parses an `FNA_GAMEPAD_NUM_GAMEPADS` string exactly as the runtime does.
         * @param envValue The raw env string (may be nullptr).
         * @return The effective supported-gamepad count (0..4).
         */
        static std::size_t ParseGamepadCountForTests(const char* envValue);

        /**
         * @brief Test-only: overrides the effective supported-gamepad count (the cached
         *        `FNA_GAMEPAD_NUM_GAMEPADS` value can't be changed in-process).
         * @param count The number of gamepad slots to expose (clamped to the supported maximum).
         */
        static void SetGamepadCountForTests(std::size_t count);

        /** @brief Test-only: reverts the supported-gamepad count to the env-derived value. */
        static void ClearGamepadCountForTests();

        /**
         * @brief Test-only: resets `SdlInputBridge`'s process-wide file-static state — the
         *        text-input suppression + control-down flags, the SDL-finger-id→touch-id map (and
         *        its counter), and the scancode-mode override — so tests don't leak state into
         *        one another. Not part of the runtime input path.
         */
        static void ResetForTests();

        static Microsoft::Xna::Framework::Input::Keys GetKeyFromScancode(
            Microsoft::Xna::Framework::Input::Keys scancode
        );
    };
}

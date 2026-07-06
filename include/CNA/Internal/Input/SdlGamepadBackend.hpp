// SPDX-License-Identifier: MS-PL
#pragma once

#include <SDL3/SDL.h>

#include <string>

// Internal (CNA) seam over the SDL3 gamepad/joystick C API used by SdlInputBridge.
//
// This exists ONLY so gamepad runtime behavior (hot-plug, slot assignment, capabilities, rumble,
// sensors, GUID) can be unit-tested without real hardware: tests inject a fake implementation.
// It is NOT part of the XNA public API and must never be exposed there. Production uses the real
// SDL-backed implementation by default.
namespace CNA::Internal::Input
{
    /**
     * @brief Abstract seam over the SDL3 gamepad/joystick operations SdlInputBridge depends on.
     *
     * Each method forwards 1:1 to the corresponding `SDL_*` function in the real implementation.
     * Handles (`SDL_Gamepad*`, `SDL_Joystick*`) are opaque and only ever passed back to this seam,
     * so a fake may return any non-null sentinel it recognizes.
     */
    class ISdlGamepadBackend
    {
    public:
        virtual ~ISdlGamepadBackend() = default;

        /** @brief Whether the joystick instance id is a recognized gamepad (SDL_IsGamepad). */
        virtual bool IsGamepad(SDL_JoystickID instanceId) = 0;
        /** @brief Opens the gamepad for the instance id, or nullptr on failure (SDL_OpenGamepad). */
        virtual SDL_Gamepad* OpenGamepad(SDL_JoystickID instanceId) = 0;
        /** @brief Closes an opened gamepad handle (SDL_CloseGamepad). */
        virtual void CloseGamepad(SDL_Gamepad* gamepad) = 0;
        /** @brief Returns the underlying joystick for a gamepad (SDL_GetGamepadJoystick). */
        virtual SDL_Joystick* GetGamepadJoystick(SDL_Gamepad* gamepad) = 0;
        /** @brief Returns the joystick's type (SDL_GetJoystickType). */
        virtual SDL_JoystickType GetJoystickType(SDL_Joystick* joystick) = 0;
        /** @brief Whether the gamepad has the given button (SDL_GamepadHasButton). */
        virtual bool GamepadHasButton(SDL_Gamepad* gamepad, SDL_GamepadButton button) = 0;
        /** @brief Whether the gamepad has the given axis (SDL_GamepadHasAxis). */
        virtual bool GamepadHasAxis(SDL_Gamepad* gamepad, SDL_GamepadAxis axis) = 0;
        /** @brief Number of touchpads on the gamepad (SDL_GetNumGamepadTouchpads). */
        virtual int GetNumGamepadTouchpads(SDL_Gamepad* gamepad) = 0;
        /** @brief Whether the gamepad has the given sensor (SDL_GamepadHasSensor). */
        virtual bool GamepadHasSensor(SDL_Gamepad* gamepad, SDL_SensorType type) = 0;
        /** @brief Returns the gamepad's SDL property set id (SDL_GetGamepadProperties). */
        virtual SDL_PropertiesID GetGamepadProperties(SDL_Gamepad* gamepad) = 0;
        /** @brief Plays low/high-frequency rumble (SDL_RumbleGamepad). */
        virtual bool RumbleGamepad(SDL_Gamepad* gamepad, Uint16 lowFreq, Uint16 highFreq, Uint32 durationMs) = 0;
        /** @brief Plays left/right trigger rumble (SDL_RumbleGamepadTriggers). */
        virtual bool RumbleGamepadTriggers(SDL_Gamepad* gamepad, Uint16 left, Uint16 right, Uint32 durationMs) = 0;
        /** @brief Sets the RGB light bar (SDL_SetGamepadLED). */
        virtual bool SetGamepadLED(SDL_Gamepad* gamepad, Uint8 red, Uint8 green, Uint8 blue) = 0;
        /** @brief Whether the given sensor is currently enabled (SDL_GamepadSensorEnabled). */
        virtual bool GamepadSensorEnabled(SDL_Gamepad* gamepad, SDL_SensorType type) = 0;
        /** @brief Enables/disables a gamepad sensor (SDL_SetGamepadSensorEnabled). */
        virtual bool SetGamepadSensorEnabled(SDL_Gamepad* gamepad, SDL_SensorType type, bool enabled) = 0;
        /** @brief Reads `count` floats of sensor data (SDL_GetGamepadSensorData). */
        virtual bool GetGamepadSensorData(SDL_Gamepad* gamepad, SDL_SensorType type, float* data, int count) = 0;
        /** @brief USB vendor id of the joystick (SDL_GetJoystickVendor). */
        virtual Uint16 GetJoystickVendor(SDL_Joystick* joystick) = 0;
        /** @brief USB product id of the joystick (SDL_GetJoystickProduct). */
        virtual Uint16 GetJoystickProduct(SDL_Joystick* joystick) = 0;
        /** @brief High-level gamepad type (SDL_GetGamepadType). */
        virtual SDL_GamepadType GetGamepadType(SDL_Gamepad* gamepad) = 0;

        /** @brief NOXNA/EXT: the device's SDL player index (LED number, 0-based), or -1 if unset. */
        virtual int GetGamepadPlayerIndex(SDL_Gamepad* gamepad) = 0;

        /** @brief NOXNA/EXT: sets the device's SDL player index (LED number). Returns success. */
        virtual bool SetGamepadPlayerIndex(SDL_Gamepad* gamepad, int playerIndex) = 0;

        /** @brief NOXNA/EXT: battery/charge state; fills `*percent` (0-100, or -1 unknown). SDL_GetGamepadPowerInfo. */
        virtual SDL_PowerState GetGamepadPowerInfo(SDL_Gamepad* gamepad, int* percent) = 0;

        /** @brief NOXNA/EXT: printed glyph label for a face button on this controller. SDL_GetGamepadButtonLabel. */
        virtual SDL_GamepadButtonLabel GetGamepadButtonLabel(SDL_Gamepad* gamepad, SDL_GamepadButton button) = 0;

        /** @brief NOXNA/EXT: human-readable controller name, or "" if unknown. SDL_GetGamepadName. */
        virtual std::string GetGamepadName(SDL_Gamepad* gamepad) = 0;
        /** @brief NOXNA/EXT: OS device path, or "" if unknown. SDL_GetGamepadPath. */
        virtual std::string GetGamepadPath(SDL_Gamepad* gamepad) = 0;
        /** @brief NOXNA/EXT: hardware serial number, or "" if unavailable. SDL_GetGamepadSerial. */
        virtual std::string GetGamepadSerial(SDL_Gamepad* gamepad) = 0;
        /** @brief NOXNA/EXT: firmware version, or 0 if unavailable. SDL_GetGamepadFirmwareVersion. */
        virtual Uint16 GetGamepadFirmwareVersion(SDL_Gamepad* gamepad) = 0;
        /** @brief NOXNA/EXT: Steam Input handle, or 0 if not a Steam virtual controller. SDL_GetGamepadSteamHandle. */
        virtual Uint64 GetGamepadSteamHandle(SDL_Gamepad* gamepad) = 0;

        /** @brief NOXNA/EXT: wired/wireless connection state. SDL_GetGamepadConnectionState. */
        virtual SDL_JoystickConnectionState GetGamepadConnectionState(SDL_Gamepad* gamepad) = 0;
    };

    /**
     * @brief Returns the currently active SDL gamepad backend (the real SDL one unless overridden).
     */
    ISdlGamepadBackend& sdl_gamepad_backend();

    /**
     * @brief Test-only: swaps in a fake gamepad backend; pass nullptr to restore the real one.
     *
     * Not part of the runtime path. Reset back to the real backend between tests (the input
     * central reset does this).
     */
    void SetSdlGamepadBackendForTests(ISdlGamepadBackend* backend);
}

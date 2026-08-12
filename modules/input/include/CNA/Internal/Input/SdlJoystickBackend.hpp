// SPDX-License-Identifier: MS-PL
#pragma once

#include <SDL3/SDL.h>

#include <string>

// Internal (CNA) seam over the SDL3 raw joystick C API used by SdlInputBridge.
//
// Deliberately SEPARATE from IPlatformGamepad: a gamepad is a *mapped* view of a joystick device
// (button/axis semantics assigned by the platform's mapping database), while this seam is the *raw*
// device — arbitrary axis/button/hat/trackball counts with no semantic assignment. The same physical
// device may be opened independently through both views.
//
// This exists ONLY so joystick runtime behavior (hot-plug, capabilities, raw state) can be
// unit-tested without real hardware: tests inject a fake implementation. It is NOT part of the XNA
// public API and must never be exposed there. Production uses the real SDL-backed implementation.
namespace CNA::Internal::Input
{
    /**
     * @brief Abstract seam over the SDL3 raw joystick operations SdlInputBridge depends on.
     *
     * Each method forwards 1:1 to the corresponding `SDL_*` function in the real implementation.
     * The `SDL_Joystick*` handle is opaque and only ever passed back to this seam, so a fake may
     * return any non-null sentinel it recognizes.
     */
    class ISdlJoystickBackend
    {
    public:
        virtual ~ISdlJoystickBackend() = default;

        /** @brief Opens the joystick for the instance id, or nullptr on failure (SDL_OpenJoystick). */
        virtual SDL_Joystick* OpenJoystick(SDL_JoystickID instanceId) = 0;
        /** @brief Closes an opened joystick handle (SDL_CloseJoystick). */
        virtual void CloseJoystick(SDL_Joystick* joystick) = 0;

        /** @brief Human-readable device name, or "" if unknown (SDL_GetJoystickName). */
        virtual std::string GetJoystickName(SDL_Joystick* joystick) = 0;
        /** @brief The device's physical category (SDL_GetJoystickType). */
        virtual SDL_JoystickType GetJoystickType(SDL_Joystick* joystick) = 0;
        /** @brief The device's GUID, formatted as a lowercase hex string (SDL_GetJoystickGUID + SDL_GUIDToString). */
        virtual std::string GetJoystickGUID(SDL_Joystick* joystick) = 0;

        /** @brief Number of axes the device reports (SDL_GetNumJoystickAxes). */
        virtual int GetNumJoystickAxes(SDL_Joystick* joystick) = 0;
        /** @brief Number of buttons the device reports (SDL_GetNumJoystickButtons). */
        virtual int GetNumJoystickButtons(SDL_Joystick* joystick) = 0;
        /** @brief Number of POV hats the device reports (SDL_GetNumJoystickHats). */
        virtual int GetNumJoystickHats(SDL_Joystick* joystick) = 0;
        /** @brief Number of trackballs the device reports (SDL_GetNumJoystickBalls). */
        virtual int GetNumJoystickBalls(SDL_Joystick* joystick) = 0;

        /** @brief Raw value of the given axis, range -32768..32767 (SDL_GetJoystickAxis). */
        virtual Sint16 GetJoystickAxis(SDL_Joystick* joystick, int axis) = 0;
        /** @brief Down/up state of the given button (SDL_GetJoystickButton). */
        virtual bool GetJoystickButton(SDL_Joystick* joystick, int button) = 0;
        /** @brief Raw SDL_HAT_* bitmask position of the given hat (SDL_GetJoystickHat). */
        virtual Uint8 GetJoystickHat(SDL_Joystick* joystick, int hat) = 0;
        /** @brief Relative motion of the given trackball since the last read (SDL_GetJoystickBall). */
        virtual bool GetJoystickBall(SDL_Joystick* joystick, int ball, int* dx, int* dy) = 0;

        /** @brief Battery/charge state; fills `*percent` (0-100, or -1 unknown) (SDL_GetJoystickPowerInfo). */
        virtual SDL_PowerState GetJoystickPowerInfo(SDL_Joystick* joystick, int* percent) = 0;
    };

    /**
     * @brief Returns the currently active SDL joystick backend (the real SDL one unless overridden).
     */
    ISdlJoystickBackend& sdl_joystick_backend();

    /**
     * @brief Test-only: swaps in a fake joystick backend; pass nullptr to restore the real one.
     *
     * Not part of the runtime path. Reset back to the real backend between tests (the input
     * central reset does this).
     */
    void SetSdlJoystickBackendForTests(ISdlJoystickBackend* backend);
}

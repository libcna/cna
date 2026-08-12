// SPDX-License-Identifier: MS-PL
#pragma once

#include <SDL3/SDL.h>

#include "CNA/Platform/PlatformEvent.hpp"

#include <string>
#include <vector>

// Internal (CNA) seam over the SDL3 haptic (force-feedback) C API used by CNA::Input::Haptics /
// HapticDevice.
//
// Unlike IPlatformGamepad/IPlatformJoystick, this seam retains SDL's full effect structures: a
// HapticDevice is opened and closed explicitly by the caller (RAII). Joystick correlation uses a
// CNA DeviceId, so no SDL_Joystick pointer crosses from the platform service into this seam.
//
// This exists ONLY so haptic runtime behavior (enumeration, capabilities, effect building, rumble)
// can be unit-tested without real force-feedback hardware: tests inject a fake implementation. It is
// NOT part of the XNA public API and must never be exposed there. Production uses the real
// SDL-backed implementation.
namespace CNA::Internal::Input
{
    /**
     * @brief Abstract seam over the SDL3 haptic operations `CNA::Input::Haptics`/`HapticDevice`
     *        depend on.
     *
     * Each method forwards 1:1 to the corresponding `SDL_*` function in the real implementation.
     * The `SDL_Haptic*` handle is opaque and only ever passed back to this seam, so a fake may
     * return any non-null sentinel it recognizes.
     */
    class ISdlHapticBackend
    {
    public:
        virtual ~ISdlHapticBackend() = default;

        /** @brief Enumerates connected haptic device instance ids (SDL_GetHaptics). */
        virtual std::vector<SDL_HapticID> GetHaptics() = 0;
        /** @brief Human-readable name for an unopened device id, or "" if unknown (SDL_GetHapticNameForID). */
        virtual std::string GetHapticNameForID(SDL_HapticID instanceId) = 0;

        /** @brief Opens the haptic device for the instance id, or nullptr on failure (SDL_OpenHaptic). */
        virtual SDL_Haptic* OpenHaptic(SDL_HapticID instanceId) = 0;
        /** @brief Opens the haptic device backing a joystick id, or nullptr. */
        virtual SDL_Haptic* OpenHapticFromJoystick(CNA::Platform::DeviceId joystickId) = 0;
        /** @brief Opens the default mouse's haptic device, or nullptr (SDL_OpenHapticFromMouse). */
        virtual SDL_Haptic* OpenHapticFromMouse() = 0;
        /** @brief Whether a connected joystick id has haptic capability. */
        virtual bool IsJoystickHaptic(CNA::Platform::DeviceId joystickId) = 0;
        /** @brief Whether the default mouse has haptic capability (SDL_IsMouseHaptic). */
        virtual bool IsMouseHaptic() = 0;
        /** @brief Closes an opened haptic device handle (SDL_CloseHaptic). */
        virtual void CloseHaptic(SDL_Haptic* haptic) = 0;

        /** @brief Human-readable device name, or "" if unknown (SDL_GetHapticName). */
        virtual std::string GetHapticName(SDL_Haptic* haptic) = 0;
        /** @brief Supported effect-family/capability bitmask (SDL_GetHapticFeatures). */
        virtual Uint32 GetHapticFeatures(SDL_Haptic* haptic) = 0;
        /** @brief Number of axes the device reports (SDL_GetNumHapticAxes). */
        virtual int GetNumHapticAxes(SDL_Haptic* haptic) = 0;
        /** @brief Maximum number of effects the device can store (SDL_GetMaxHapticEffects). */
        virtual int GetMaxHapticEffects(SDL_Haptic* haptic) = 0;
        /** @brief Maximum number of effects the device can play at once (SDL_GetMaxHapticEffectsPlaying). */
        virtual int GetMaxHapticEffectsPlaying(SDL_Haptic* haptic) = 0;
        /** @brief Whether the simple rumble convenience is supported (SDL_HapticRumbleSupported). */
        virtual bool HapticRumbleSupported(SDL_Haptic* haptic) = 0;
        /** @brief Whether the device supports the given effect template (SDL_HapticEffectSupported). */
        virtual bool HapticEffectSupported(SDL_Haptic* haptic, const SDL_HapticEffect* effect) = 0;

        /** @brief Initializes the simple rumble feature (SDL_InitHapticRumble). */
        virtual bool InitHapticRumble(SDL_Haptic* haptic) = 0;
        /** @brief Plays the simple rumble at the given strength/duration (SDL_PlayHapticRumble). */
        virtual bool PlayHapticRumble(SDL_Haptic* haptic, float strength, Uint32 lengthMs) = 0;
        /** @brief Stops the simple rumble (SDL_StopHapticRumble). */
        virtual bool StopHapticRumble(SDL_Haptic* haptic) = 0;

        /** @brief Uploads a new effect, returning its id or -1 on failure (SDL_CreateHapticEffect). */
        virtual SDL_HapticEffectID CreateHapticEffect(SDL_Haptic* haptic, const SDL_HapticEffect* effect) = 0;
        /** @brief Updates an existing effect's parameters (SDL_UpdateHapticEffect). */
        virtual bool UpdateHapticEffect(SDL_Haptic* haptic, SDL_HapticEffectID effect, const SDL_HapticEffect* data) = 0;
        /** @brief Plays an effect for the given iteration count (SDL_RunHapticEffect). */
        virtual bool RunHapticEffect(SDL_Haptic* haptic, SDL_HapticEffectID effect, Uint32 iterations) = 0;
        /** @brief Stops a playing effect (SDL_StopHapticEffect). */
        virtual bool StopHapticEffect(SDL_Haptic* haptic, SDL_HapticEffectID effect) = 0;
        /** @brief Frees an uploaded effect (SDL_DestroyHapticEffect). */
        virtual void DestroyHapticEffect(SDL_Haptic* haptic, SDL_HapticEffectID effect) = 0;
        /** @brief Whether an effect is currently playing (SDL_GetHapticEffectStatus). */
        virtual bool GetHapticEffectStatus(SDL_Haptic* haptic, SDL_HapticEffectID effect) = 0;
        /** @brief Stops every playing effect on the device (SDL_StopHapticEffects). */
        virtual bool StopHapticEffects(SDL_Haptic* haptic) = 0;

        /** @brief Sets the overall effect gain, 0-100 (SDL_SetHapticGain). */
        virtual bool SetHapticGain(SDL_Haptic* haptic, int gain) = 0;
        /** @brief Sets the autocenter strength, 0-100 (0 disables) (SDL_SetHapticAutocenter). */
        virtual bool SetHapticAutocenter(SDL_Haptic* haptic, int autocenter) = 0;
        /** @brief Pauses all effect playback (SDL_PauseHaptic). */
        virtual bool PauseHaptic(SDL_Haptic* haptic) = 0;
        /** @brief Resumes effect playback after a pause (SDL_ResumeHaptic). */
        virtual bool ResumeHaptic(SDL_Haptic* haptic) = 0;
    };

    /**
     * @brief Returns the currently active SDL haptic backend (the real SDL one unless overridden).
     */
    ISdlHapticBackend& sdl_haptic_backend();

    /**
     * @brief Test-only: swaps in a fake haptic backend; pass nullptr to restore the real one.
     *
     * Not part of the runtime path. Reset back to the real backend between tests (the input
     * central reset does this).
     */
    void SetSdlHapticBackendForTests(ISdlHapticBackend* backend);
}

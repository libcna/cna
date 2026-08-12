// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Input/SdlHapticBackend.hpp"

#include <limits>
#include <unordered_map>

namespace CNA::Internal::Input
{
    namespace
    {
        // Real SDL-backed implementation: every method forwards 1:1 to the matching SDL3 function.
        class RealSdlHapticBackend final : public ISdlHapticBackend
        {
        public:
            std::vector<SDL_HapticID> GetHaptics() override
            {
                int count = 0;
                SDL_HapticID* ids = SDL_GetHaptics(&count);
                std::vector<SDL_HapticID> result;
                if (ids != nullptr)
                {
                    result.assign(ids, ids + count);
                    SDL_free(ids);
                }
                return result;
            }
            std::string GetHapticNameForID(SDL_HapticID instanceId) override
            {
                const char* s = SDL_GetHapticNameForID(instanceId);
                return s ? s : "";
            }
            SDL_Haptic* OpenHaptic(SDL_HapticID instanceId) override
            {
                return SDL_OpenHaptic(instanceId);
            }
            SDL_Haptic* OpenHapticFromJoystick(const CNA::Platform::DeviceId joystickId) override
            {
                if (joystickId == 0
                    || joystickId > std::numeric_limits<SDL_JoystickID>::max())
                {
                    return nullptr;
                }
                SDL_Joystick* joystick = SDL_OpenJoystick(
                    static_cast<SDL_JoystickID>(joystickId));
                if (joystick == nullptr)
                {
                    return nullptr;
                }
                SDL_Haptic* haptic = SDL_OpenHapticFromJoystick(joystick);
                if (haptic == nullptr)
                {
                    SDL_CloseJoystick(joystick);
                    return nullptr;
                }

                // SDL requires the haptic handle to be closed before the joystick it came from.
                // Retain this extra joystick reference until CloseHaptic rather than leaking a
                // native pointer through the platform contract.
                joystickOwners_.emplace(haptic, joystick);
                return haptic;
            }
            SDL_Haptic* OpenHapticFromMouse() override
            {
                return SDL_OpenHapticFromMouse();
            }
            bool IsJoystickHaptic(const CNA::Platform::DeviceId joystickId) override
            {
                if (joystickId == 0
                    || joystickId > std::numeric_limits<SDL_JoystickID>::max())
                {
                    return false;
                }
                SDL_Joystick* joystick = SDL_OpenJoystick(
                    static_cast<SDL_JoystickID>(joystickId));
                if (joystick == nullptr)
                {
                    return false;
                }
                const bool result = SDL_IsJoystickHaptic(joystick);
                SDL_CloseJoystick(joystick);
                return result;
            }
            bool IsMouseHaptic() override
            {
                return SDL_IsMouseHaptic();
            }
            void CloseHaptic(SDL_Haptic* haptic) override
            {
                const auto owner = joystickOwners_.find(haptic);
                SDL_Joystick* joystick = owner != joystickOwners_.end() ? owner->second : nullptr;
                SDL_CloseHaptic(haptic);
                if (joystick != nullptr)
                {
                    SDL_CloseJoystick(joystick);
                    joystickOwners_.erase(owner);
                }
            }
            std::string GetHapticName(SDL_Haptic* haptic) override
            {
                const char* s = SDL_GetHapticName(haptic);
                return s ? s : "";
            }
            Uint32 GetHapticFeatures(SDL_Haptic* haptic) override
            {
                return SDL_GetHapticFeatures(haptic);
            }
            int GetNumHapticAxes(SDL_Haptic* haptic) override
            {
                return SDL_GetNumHapticAxes(haptic);
            }
            int GetMaxHapticEffects(SDL_Haptic* haptic) override
            {
                return SDL_GetMaxHapticEffects(haptic);
            }
            int GetMaxHapticEffectsPlaying(SDL_Haptic* haptic) override
            {
                return SDL_GetMaxHapticEffectsPlaying(haptic);
            }
            bool HapticRumbleSupported(SDL_Haptic* haptic) override
            {
                return SDL_HapticRumbleSupported(haptic);
            }
            bool HapticEffectSupported(SDL_Haptic* haptic, const SDL_HapticEffect* effect) override
            {
                return SDL_HapticEffectSupported(haptic, effect);
            }
            bool InitHapticRumble(SDL_Haptic* haptic) override
            {
                return SDL_InitHapticRumble(haptic);
            }
            bool PlayHapticRumble(SDL_Haptic* haptic, float strength, Uint32 lengthMs) override
            {
                return SDL_PlayHapticRumble(haptic, strength, lengthMs);
            }
            bool StopHapticRumble(SDL_Haptic* haptic) override
            {
                return SDL_StopHapticRumble(haptic);
            }
            SDL_HapticEffectID CreateHapticEffect(SDL_Haptic* haptic, const SDL_HapticEffect* effect) override
            {
                return SDL_CreateHapticEffect(haptic, effect);
            }
            bool UpdateHapticEffect(SDL_Haptic* haptic, SDL_HapticEffectID effect, const SDL_HapticEffect* data) override
            {
                return SDL_UpdateHapticEffect(haptic, effect, data);
            }
            bool RunHapticEffect(SDL_Haptic* haptic, SDL_HapticEffectID effect, Uint32 iterations) override
            {
                return SDL_RunHapticEffect(haptic, effect, iterations);
            }
            bool StopHapticEffect(SDL_Haptic* haptic, SDL_HapticEffectID effect) override
            {
                return SDL_StopHapticEffect(haptic, effect);
            }
            void DestroyHapticEffect(SDL_Haptic* haptic, SDL_HapticEffectID effect) override
            {
                SDL_DestroyHapticEffect(haptic, effect);
            }
            bool GetHapticEffectStatus(SDL_Haptic* haptic, SDL_HapticEffectID effect) override
            {
                return SDL_GetHapticEffectStatus(haptic, effect);
            }
            bool StopHapticEffects(SDL_Haptic* haptic) override
            {
                return SDL_StopHapticEffects(haptic);
            }
            bool SetHapticGain(SDL_Haptic* haptic, int gain) override
            {
                return SDL_SetHapticGain(haptic, gain);
            }
            bool SetHapticAutocenter(SDL_Haptic* haptic, int autocenter) override
            {
                return SDL_SetHapticAutocenter(haptic, autocenter);
            }
            bool PauseHaptic(SDL_Haptic* haptic) override
            {
                return SDL_PauseHaptic(haptic);
            }
            bool ResumeHaptic(SDL_Haptic* haptic) override
            {
                return SDL_ResumeHaptic(haptic);
            }

        private:
            std::unordered_multimap<SDL_Haptic*, SDL_Joystick*> joystickOwners_;
        };

        RealSdlHapticBackend g_realBackend;
        ISdlHapticBackend*   g_currentBackend = &g_realBackend;
    }

    ISdlHapticBackend& sdl_haptic_backend()
    {
        return *g_currentBackend;
    }

    void SetSdlHapticBackendForTests(ISdlHapticBackend* backend)
    {
        g_currentBackend = (backend != nullptr) ? backend : &g_realBackend;
    }
}

// SPDX-License-Identifier: MS-PL
#pragma once

#include <SDL3/SDL.h>

#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "CNA/Internal/Input/SdlHapticBackend.hpp"

// Test-only fake implementation of the internal SDL haptic seam. Lets haptic runtime behavior
// (enumeration, capabilities, effect building/lifecycle, rumble) be exercised with NO real
// force-feedback hardware. The opaque SDL_Haptic* handle is just a reinterpret-cast FakeDevice
// pointer that is only ever handed back to this fake. NOT compiled into production.
namespace CNA::Internal::Input::test_support
{
    // Per-device fake description; register one per synthetic haptic instance id before calling
    // Haptics::OpenEXT for that id (or set joystickConfig/mouseConfig for the from-joystick/mouse
    // open paths, which have no enumeration id of their own).
    struct FakeHapticConfig
    {
        std::string name;
        Uint32 features = 0;
        int axisCount = 0;
        int maxEffects = 0;
        int maxEffectsPlaying = 0;
        bool rumbleSupported = false;
        bool effectSupported = true;   // what HapticEffectSupported reports for any effect
        bool openFails = false;        // simulate the corresponding SDL_Open* returning nullptr
    };

    class FakeSdlHapticBackend final : public ISdlHapticBackend
    {
    public:
        // --- test setup / introspection ---
        void Register(SDL_HapticID id, FakeHapticConfig cfg) { registered_[id] = std::move(cfg); }

        FakeHapticConfig joystickConfig;      // used by OpenHapticFromJoystick
        bool joystickIsHaptic = false;        // used by IsJoystickHaptic
        FakeHapticConfig mouseConfig;         // used by OpenHapticFromMouse
        bool mouseIsHaptic = false;           // used by IsMouseHaptic

        int openCount = 0;
        int closeCount = 0;
        int initRumbleCalls = 0;
        int playRumbleCalls = 0;
        int stopRumbleCalls = 0;
        float lastRumbleStrength = 0.0f;
        Uint32 lastRumbleLengthMs = 0;
        int createEffectCalls = 0;
        int destroyEffectCalls = 0;
        int lastGain = -1;
        int lastAutocenter = -1;
        int pauseCalls = 0;
        int resumeCalls = 0;
        CNA::Platform::DeviceId lastJoystickId = 0;
        SDL_HapticEffect lastCreatedEffect{};
        SDL_HapticEffect lastUpdatedEffect{};
        // Real SDL copies an effect's Custom sample data synchronously during Create/UpdateHapticEffect
        // (the caller's buffer need not outlive the call). Mirror that here: `lastCreatedEffect`/
        // `lastUpdatedEffect` are shallow copies of the union (so `.custom.data` would otherwise dangle
        // once the caller's temporary buffer is destroyed) -- these members own a snapshot and the
        // `.custom.data` pointer above is repointed at them, same lifetime as this fake object.
        std::vector<Uint16> lastCreatedCustomData;
        std::vector<Uint16> lastUpdatedCustomData;

        // --- ISdlHapticBackend ---
        std::vector<SDL_HapticID> GetHaptics() override
        {
            std::vector<SDL_HapticID> ids;
            for (const auto& [id, cfg] : registered_)
                ids.push_back(id);
            return ids;
        }

        std::string GetHapticNameForID(SDL_HapticID instanceId) override
        {
            const auto it = registered_.find(instanceId);
            return it != registered_.end() ? it->second.name : "";
        }

        SDL_Haptic* OpenHaptic(SDL_HapticID instanceId) override
        {
            const auto it = registered_.find(instanceId);
            if (it == registered_.end() || it->second.openFails)
                return nullptr;
            return MakeDevice(it->second);
        }

        SDL_Haptic* OpenHapticFromJoystick(const CNA::Platform::DeviceId joystickId) override
        {
            lastJoystickId = joystickId;
            if (joystickId == 0 || joystickConfig.openFails)
                return nullptr;
            return MakeDevice(joystickConfig);
        }

        SDL_Haptic* OpenHapticFromMouse() override
        {
            if (mouseConfig.openFails)
                return nullptr;
            return MakeDevice(mouseConfig);
        }

        bool IsJoystickHaptic(const CNA::Platform::DeviceId joystickId) override
        {
            lastJoystickId = joystickId;
            return joystickId != 0 && joystickIsHaptic;
        }

        bool IsMouseHaptic() override
        {
            return mouseIsHaptic;
        }

        void CloseHaptic(SDL_Haptic* haptic) override
        {
            if (FakeDevice* d = dev(haptic))
            {
                d->closed = true;
                ++closeCount;
            }
        }

        std::string GetHapticName(SDL_Haptic* haptic) override
        {
            FakeDevice* d = dev(haptic);
            return d ? d->cfg.name : "";
        }

        Uint32 GetHapticFeatures(SDL_Haptic* haptic) override
        {
            FakeDevice* d = dev(haptic);
            return d ? d->cfg.features : 0;
        }

        int GetNumHapticAxes(SDL_Haptic* haptic) override
        {
            FakeDevice* d = dev(haptic);
            return d ? d->cfg.axisCount : 0;
        }

        int GetMaxHapticEffects(SDL_Haptic* haptic) override
        {
            FakeDevice* d = dev(haptic);
            return d ? d->cfg.maxEffects : -1;
        }

        int GetMaxHapticEffectsPlaying(SDL_Haptic* haptic) override
        {
            FakeDevice* d = dev(haptic);
            return d ? d->cfg.maxEffectsPlaying : -1;
        }

        bool HapticRumbleSupported(SDL_Haptic* haptic) override
        {
            FakeDevice* d = dev(haptic);
            return d != nullptr && d->cfg.rumbleSupported;
        }

        bool HapticEffectSupported(SDL_Haptic* haptic, const SDL_HapticEffect* effect) override
        {
            FakeDevice* d = dev(haptic);
            (void)effect;
            return d != nullptr && d->cfg.effectSupported;
        }

        bool InitHapticRumble(SDL_Haptic* haptic) override
        {
            ++initRumbleCalls;
            FakeDevice* d = dev(haptic);
            return d != nullptr && d->cfg.rumbleSupported;
        }

        bool PlayHapticRumble(SDL_Haptic* haptic, float strength, Uint32 lengthMs) override
        {
            ++playRumbleCalls;
            lastRumbleStrength = strength;
            lastRumbleLengthMs = lengthMs;
            FakeDevice* d = dev(haptic);
            return d != nullptr && d->cfg.rumbleSupported;
        }

        bool StopHapticRumble(SDL_Haptic* haptic) override
        {
            ++stopRumbleCalls;
            return dev(haptic) != nullptr;
        }

        SDL_HapticEffectID CreateHapticEffect(SDL_Haptic* haptic, const SDL_HapticEffect* effect) override
        {
            ++createEffectCalls;
            if (effect != nullptr)
            {
                lastCreatedEffect = *effect;
                SnapshotCustomData(lastCreatedEffect, lastCreatedCustomData);
            }
            FakeDevice* d = dev(haptic);
            if (d == nullptr)
                return -1;
            const int id = d->nextEffectId++;
            d->liveEffects.insert(id);
            return id;
        }

        bool UpdateHapticEffect(SDL_Haptic* haptic, SDL_HapticEffectID effect, const SDL_HapticEffect* data) override
        {
            if (data != nullptr)
            {
                lastUpdatedEffect = *data;
                SnapshotCustomData(lastUpdatedEffect, lastUpdatedCustomData);
            }
            FakeDevice* d = dev(haptic);
            return d != nullptr && d->liveEffects.count(effect) > 0;
        }

        bool RunHapticEffect(SDL_Haptic* haptic, SDL_HapticEffectID effect, Uint32 iterations) override
        {
            (void)iterations;
            FakeDevice* d = dev(haptic);
            if (d == nullptr || d->liveEffects.count(effect) == 0)
                return false;
            d->playingEffects.insert(effect);
            return true;
        }

        bool StopHapticEffect(SDL_Haptic* haptic, SDL_HapticEffectID effect) override
        {
            FakeDevice* d = dev(haptic);
            if (d == nullptr || d->liveEffects.count(effect) == 0)
                return false;
            d->playingEffects.erase(effect);
            return true;
        }

        void DestroyHapticEffect(SDL_Haptic* haptic, SDL_HapticEffectID effect) override
        {
            ++destroyEffectCalls;
            if (FakeDevice* d = dev(haptic))
            {
                d->liveEffects.erase(effect);
                d->playingEffects.erase(effect);
            }
        }

        bool GetHapticEffectStatus(SDL_Haptic* haptic, SDL_HapticEffectID effect) override
        {
            FakeDevice* d = dev(haptic);
            return d != nullptr && d->playingEffects.count(effect) > 0;
        }

        bool StopHapticEffects(SDL_Haptic* haptic) override
        {
            FakeDevice* d = dev(haptic);
            if (d == nullptr)
                return false;
            d->playingEffects.clear();
            return true;
        }

        bool SetHapticGain(SDL_Haptic* haptic, int gain) override
        {
            lastGain = gain;
            return dev(haptic) != nullptr;
        }

        bool SetHapticAutocenter(SDL_Haptic* haptic, int autocenter) override
        {
            lastAutocenter = autocenter;
            return dev(haptic) != nullptr;
        }

        bool PauseHaptic(SDL_Haptic* haptic) override
        {
            ++pauseCalls;
            return dev(haptic) != nullptr;
        }

        bool ResumeHaptic(SDL_Haptic* haptic) override
        {
            ++resumeCalls;
            return dev(haptic) != nullptr;
        }

    private:
        struct FakeDevice
        {
            FakeHapticConfig cfg;
            bool closed = false;
            int nextEffectId = 0;
            std::set<int> liveEffects;
            std::set<int> playingEffects;
        };

        // Snapshots effect.custom.data into `storage` and repoints effect.custom.data at that
        // snapshot, so the introspected effect stays valid after the caller's own buffer is freed
        // (mirrors real SDL_haptic copying Custom sample data synchronously during the call).
        static void SnapshotCustomData(SDL_HapticEffect& effect, std::vector<Uint16>& storage)
        {
            if (effect.type != SDL_HAPTIC_CUSTOM || effect.custom.data == nullptr)
                return;
            const std::size_t count =
                static_cast<std::size_t>(effect.custom.channels) * effect.custom.samples;
            storage.assign(effect.custom.data, effect.custom.data + count);
            effect.custom.data = storage.data();
        }

        SDL_Haptic* MakeDevice(const FakeHapticConfig& cfg)
        {
            auto device = std::make_unique<FakeDevice>();
            device->cfg = cfg;
            ++openCount;
            FakeDevice* raw = device.get();
            devices_.push_back(std::move(device));
            return reinterpret_cast<SDL_Haptic*>(raw);
        }

        FakeDevice* dev(SDL_Haptic* h) { return reinterpret_cast<FakeDevice*>(h); }

        std::map<SDL_HapticID, FakeHapticConfig> registered_;
        std::deque<std::unique_ptr<FakeDevice>> devices_;
    };
}

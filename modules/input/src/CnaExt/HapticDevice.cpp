// SPDX-License-Identifier: MS-PL
#include "CNA/Input/HapticDevice.hpp"

#include "CNA/Internal/Input/SdlHapticBackend.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/Input/IPlatformHaptics.hpp"

#include <type_traits>
#include <utility>

namespace CNA::Input
{
    namespace
    {
        using CNA::Internal::Input::sdl_haptic_backend;

        SDL_Haptic* native_handle(void* handle)
        {
            return static_cast<SDL_Haptic*>(handle);
        }

        CNA::Platform::IPlatformHaptics* haptic_service(CNA::Platform::IPlatform* platform)
        {
            return platform != nullptr ? platform->GetHaptics() : nullptr;
        }

        Uint8 to_sdl_direction_type(const HapticDirectionTypeEXT type)
        {
            switch (type)
            {
            case HapticDirectionTypeEXT::Polar:        return SDL_HAPTIC_POLAR;
            case HapticDirectionTypeEXT::Cartesian:    return SDL_HAPTIC_CARTESIAN;
            case HapticDirectionTypeEXT::Spherical:    return SDL_HAPTIC_SPHERICAL;
            case HapticDirectionTypeEXT::SteeringAxis: return SDL_HAPTIC_STEERING_AXIS;
            }
            return SDL_HAPTIC_POLAR;
        }

        SDL_HapticDirection to_sdl_direction(const HapticDirectionEXT& direction)
        {
            SDL_HapticDirection out{};
            out.type = to_sdl_direction_type(direction.type);
            out.dir[0] = direction.values[0];
            out.dir[1] = direction.values[1];
            out.dir[2] = direction.values[2];
            return out;
        }

        Uint16 to_sdl_effect_type(const HapticEffectTypeEXT type)
        {
            switch (type)
            {
            case HapticEffectTypeEXT::Constant:     return SDL_HAPTIC_CONSTANT;
            case HapticEffectTypeEXT::Sine:         return SDL_HAPTIC_SINE;
            case HapticEffectTypeEXT::Square:       return SDL_HAPTIC_SQUARE;
            case HapticEffectTypeEXT::Triangle:     return SDL_HAPTIC_TRIANGLE;
            case HapticEffectTypeEXT::SawtoothUp:   return SDL_HAPTIC_SAWTOOTHUP;
            case HapticEffectTypeEXT::SawtoothDown: return SDL_HAPTIC_SAWTOOTHDOWN;
            case HapticEffectTypeEXT::Ramp:         return SDL_HAPTIC_RAMP;
            case HapticEffectTypeEXT::Spring:       return SDL_HAPTIC_SPRING;
            case HapticEffectTypeEXT::Damper:       return SDL_HAPTIC_DAMPER;
            case HapticEffectTypeEXT::Inertia:      return SDL_HAPTIC_INERTIA;
            case HapticEffectTypeEXT::Friction:     return SDL_HAPTIC_FRICTION;
            case HapticEffectTypeEXT::LeftRight:    return SDL_HAPTIC_LEFTRIGHT;
            case HapticEffectTypeEXT::Custom:       return SDL_HAPTIC_CUSTOM;
            }
            return SDL_HAPTIC_CONSTANT;
        }

        bool is_periodic(const HapticEffectTypeEXT type)
        {
            switch (type)
            {
            case HapticEffectTypeEXT::Sine:
            case HapticEffectTypeEXT::Square:
            case HapticEffectTypeEXT::Triangle:
            case HapticEffectTypeEXT::SawtoothUp:
            case HapticEffectTypeEXT::SawtoothDown:
                return true;
            default:
                return false;
            }
        }

        bool is_condition(const HapticEffectTypeEXT type)
        {
            switch (type)
            {
            case HapticEffectTypeEXT::Spring:
            case HapticEffectTypeEXT::Damper:
            case HapticEffectTypeEXT::Inertia:
            case HapticEffectTypeEXT::Friction:
                return true;
            default:
                return false;
            }
        }

        // Builds the SDL union from a HapticEffectEXT. `customDataStorage` receives a copy of
        // effect.customData when effect.type == Custom, and the returned struct's custom.data points
        // into it — customDataStorage must outlive the seam call the result is passed to.
        SDL_HapticEffect to_sdl_haptic_effect(const HapticEffectEXT& effect, std::vector<Uint16>& customDataStorage)
        {
            SDL_HapticEffect out{};
            const Uint16 sdlType = to_sdl_effect_type(effect.type);

            if (effect.type == HapticEffectTypeEXT::LeftRight)
            {
                out.leftright.type = sdlType;
                out.leftright.length = effect.length;
                out.leftright.large_magnitude = effect.largeMagnitude;
                out.leftright.small_magnitude = effect.smallMagnitude;
                return out;
            }

            if (is_condition(effect.type))
            {
                SDL_HapticCondition& c = out.condition;
                c.type = sdlType;
                c.direction = to_sdl_direction(effect.direction);
                c.length = effect.length;
                c.delay = effect.delay;
                c.button = effect.button;
                c.interval = effect.interval;
                for (int i = 0; i < 3; ++i)
                {
                    c.right_sat[i] = effect.rightSaturation[static_cast<std::size_t>(i)];
                    c.left_sat[i] = effect.leftSaturation[static_cast<std::size_t>(i)];
                    c.right_coeff[i] = effect.rightCoefficient[static_cast<std::size_t>(i)];
                    c.left_coeff[i] = effect.leftCoefficient[static_cast<std::size_t>(i)];
                    c.deadband[i] = effect.deadband[static_cast<std::size_t>(i)];
                    c.center[i] = effect.center[static_cast<std::size_t>(i)];
                }
                return out;
            }

            if (effect.type == HapticEffectTypeEXT::Custom)
            {
                SDL_HapticCustom& c = out.custom;
                c.type = sdlType;
                c.direction = to_sdl_direction(effect.direction);
                c.length = effect.length;
                c.delay = effect.delay;
                c.button = effect.button;
                c.interval = effect.interval;
                c.channels = effect.customChannels;
                c.period = effect.customPeriod;
                customDataStorage = effect.customData;
                c.samples = (effect.customChannels > 0)
                    ? static_cast<Uint16>(customDataStorage.size() / effect.customChannels)
                    : 0;
                c.data = customDataStorage.empty() ? nullptr : customDataStorage.data();
                c.attack_length = effect.attackLength;
                c.attack_level = effect.attackLevel;
                c.fade_length = effect.fadeLength;
                c.fade_level = effect.fadeLevel;
                return out;
            }

            if (effect.type == HapticEffectTypeEXT::Ramp)
            {
                SDL_HapticRamp& r = out.ramp;
                r.type = sdlType;
                r.direction = to_sdl_direction(effect.direction);
                r.length = effect.length;
                r.delay = effect.delay;
                r.button = effect.button;
                r.interval = effect.interval;
                r.start = effect.rampStart;
                r.end = effect.rampEnd;
                r.attack_length = effect.attackLength;
                r.attack_level = effect.attackLevel;
                r.fade_length = effect.fadeLength;
                r.fade_level = effect.fadeLevel;
                return out;
            }

            if (is_periodic(effect.type))
            {
                SDL_HapticPeriodic& p = out.periodic;
                p.type = sdlType;
                p.direction = to_sdl_direction(effect.direction);
                p.length = effect.length;
                p.delay = effect.delay;
                p.button = effect.button;
                p.interval = effect.interval;
                p.period = effect.period;
                p.magnitude = effect.magnitude;
                p.offset = effect.offset;
                p.phase = effect.phase;
                p.attack_length = effect.attackLength;
                p.attack_level = effect.attackLevel;
                p.fade_length = effect.fadeLength;
                p.fade_level = effect.fadeLevel;
                return out;
            }

            // Constant (the only remaining family).
            SDL_HapticConstant& k = out.constant;
            k.type = sdlType;
            k.direction = to_sdl_direction(effect.direction);
            k.length = effect.length;
            k.delay = effect.delay;
            k.button = effect.button;
            k.interval = effect.interval;
            k.level = effect.level;
            k.attack_length = effect.attackLength;
            k.attack_level = effect.attackLevel;
            k.fade_length = effect.fadeLength;
            k.fade_level = effect.fadeLevel;
            return out;
        }

        HapticFeatureEXT to_haptic_features_ext(const Uint32 sdlFeatures)
        {
            using U = std::underlying_type_t<HapticFeatureEXT>;
            // SDL_HAPTIC_* bit values are numerically identical to HapticFeatureEXT's — both mirror
            // the same SDL bitmask 1:1, so a direct mask-through is correct (unlike the effect-type
            // mapping above, there is no reordering here to keep explicit).
            return static_cast<HapticFeatureEXT>(static_cast<U>(sdlFeatures));
        }
    }

    HapticDevice::HapticDevice(void* effectHandle,
                               CNA::Platform::IPlatform* platform,
                               std::optional<std::uint64_t> standaloneId,
                               std::string standaloneName)
        : effectHandle_(effectHandle)
        , platform_(platform)
        , standaloneId_(standaloneId)
        , standaloneName_(std::move(standaloneName))
    {
    }

    HapticDevice::HapticDevice(HapticDevice&& other) noexcept
        : effectHandle_(std::exchange(other.effectHandle_, nullptr))
        , platform_(std::exchange(other.platform_, nullptr))
        , standaloneId_(std::exchange(other.standaloneId_, std::nullopt))
        , standaloneName_(std::move(other.standaloneName_))
        , isDisposed_(std::exchange(other.isDisposed_, true))
    {
    }

    HapticDevice& HapticDevice::operator=(HapticDevice&& other) noexcept
    {
        if (this != &other)
        {
            Dispose();
            effectHandle_ = std::exchange(other.effectHandle_, nullptr);
            platform_ = std::exchange(other.platform_, nullptr);
            standaloneId_ = std::exchange(other.standaloneId_, std::nullopt);
            standaloneName_ = std::move(other.standaloneName_);
            isDisposed_ = std::exchange(other.isDisposed_, true);
        }
        return *this;
    }

    HapticDevice::~HapticDevice()
    {
        Dispose();
    }

    void HapticDevice::Dispose()
    {
        if (isDisposed_)
            return;
        isDisposed_ = true;
        if (standaloneId_.has_value())
        {
            if (CNA::Platform::IPlatformHaptics* service = haptic_service(platform_))
            {
                (void)service->StopRumble(*standaloneId_);
            }
            standaloneId_.reset();
        }
        if (effectHandle_ != nullptr)
        {
            sdl_haptic_backend().CloseHaptic(native_handle(effectHandle_));
            effectHandle_ = nullptr;
        }
        if (platform_ != nullptr)
        {
            platform_->ReleaseSubsystem(CNA::Platform::PlatformSubsystem::Haptic);
            platform_ = nullptr;
        }
    }

    bool HapticDevice::IsOpenEXT() const
    {
        return effectHandle_ != nullptr || standaloneId_.has_value();
    }

    std::string HapticDevice::GetNameEXT() const
    {
        if (standaloneId_.has_value())
            return standaloneName_;
        return effectHandle_ != nullptr
            ? sdl_haptic_backend().GetHapticName(native_handle(effectHandle_)) : std::string();
    }

    HapticCapabilitiesEXT HapticDevice::GetCapabilitiesEXT() const
    {
        if (!IsOpenEXT())
            return HapticCapabilitiesEXT{};

        HapticCapabilitiesEXT caps;
        caps.isOpen = true;
        caps.name = GetNameEXT();
        if (standaloneId_.has_value())
        {
            CNA::Platform::IPlatformHaptics* service = haptic_service(platform_);
            caps.rumbleSupported = service != nullptr && service->SupportsRumble(*standaloneId_);
        }
        if (effectHandle_ != nullptr)
        {
            SDL_Haptic* handle = native_handle(effectHandle_);
            caps.features = to_haptic_features_ext(sdl_haptic_backend().GetHapticFeatures(handle));
            caps.axisCount = sdl_haptic_backend().GetNumHapticAxes(handle);
            caps.maxEffects = sdl_haptic_backend().GetMaxHapticEffects(handle);
            caps.maxEffectsPlaying = sdl_haptic_backend().GetMaxHapticEffectsPlaying(handle);
            if (!standaloneId_.has_value())
            {
                caps.rumbleSupported = sdl_haptic_backend().HapticRumbleSupported(handle);
            }
        }
        return caps;
    }

    bool HapticDevice::IsEffectSupportedEXT(const HapticEffectEXT& effect) const
    {
        if (effectHandle_ == nullptr)
            return false;
        std::vector<Uint16> customDataStorage;
        const SDL_HapticEffect sdlEffect = to_sdl_haptic_effect(effect, customDataStorage);
        return sdl_haptic_backend().HapticEffectSupported(native_handle(effectHandle_), &sdlEffect);
    }

    bool HapticDevice::InitRumbleEXT()
    {
        if (standaloneId_.has_value())
        {
            CNA::Platform::IPlatformHaptics* service = haptic_service(platform_);
            return service != nullptr && service->InitializeRumble(*standaloneId_);
        }
        return effectHandle_ != nullptr
            && sdl_haptic_backend().InitHapticRumble(native_handle(effectHandle_));
    }

    bool HapticDevice::PlayRumbleEXT(const float strength, const std::uint32_t lengthMs)
    {
        if (standaloneId_.has_value())
        {
            CNA::Platform::IPlatformHaptics* service = haptic_service(platform_);
            return service != nullptr
                && service->PlayRumble(*standaloneId_, strength, lengthMs);
        }
        return effectHandle_ != nullptr && sdl_haptic_backend().PlayHapticRumble(
            native_handle(effectHandle_), strength, lengthMs);
    }

    bool HapticDevice::StopRumbleEXT()
    {
        if (standaloneId_.has_value())
        {
            CNA::Platform::IPlatformHaptics* service = haptic_service(platform_);
            return service != nullptr && service->StopRumble(*standaloneId_);
        }
        return effectHandle_ != nullptr
            && sdl_haptic_backend().StopHapticRumble(native_handle(effectHandle_));
    }

    int HapticDevice::CreateEffectEXT(const HapticEffectEXT& effect)
    {
        if (effectHandle_ == nullptr)
            return -1;
        std::vector<Uint16> customDataStorage;
        const SDL_HapticEffect sdlEffect = to_sdl_haptic_effect(effect, customDataStorage);
        return sdl_haptic_backend().CreateHapticEffect(native_handle(effectHandle_), &sdlEffect);
    }

    bool HapticDevice::UpdateEffectEXT(const int effectId, const HapticEffectEXT& effect)
    {
        if (effectHandle_ == nullptr)
            return false;
        std::vector<Uint16> customDataStorage;
        const SDL_HapticEffect sdlEffect = to_sdl_haptic_effect(effect, customDataStorage);
        return sdl_haptic_backend().UpdateHapticEffect(
            native_handle(effectHandle_), effectId, &sdlEffect);
    }

    bool HapticDevice::RunEffectEXT(const int effectId, const std::uint32_t iterations)
    {
        return effectHandle_ != nullptr && sdl_haptic_backend().RunHapticEffect(
            native_handle(effectHandle_), effectId, iterations);
    }

    bool HapticDevice::StopEffectEXT(const int effectId)
    {
        return effectHandle_ != nullptr && sdl_haptic_backend().StopHapticEffect(
            native_handle(effectHandle_), effectId);
    }

    void HapticDevice::DestroyEffectEXT(const int effectId)
    {
        if (effectHandle_ != nullptr)
            sdl_haptic_backend().DestroyHapticEffect(native_handle(effectHandle_), effectId);
    }

    bool HapticDevice::GetEffectStatusEXT(const int effectId) const
    {
        return effectHandle_ != nullptr && sdl_haptic_backend().GetHapticEffectStatus(
            native_handle(effectHandle_), effectId);
    }

    bool HapticDevice::StopAllEffectsEXT()
    {
        return effectHandle_ != nullptr
            && sdl_haptic_backend().StopHapticEffects(native_handle(effectHandle_));
    }

    bool HapticDevice::SetGainEXT(const int gain)
    {
        return effectHandle_ != nullptr
            && sdl_haptic_backend().SetHapticGain(native_handle(effectHandle_), gain);
    }

    bool HapticDevice::SetAutocenterEXT(const int autocenter)
    {
        return effectHandle_ != nullptr
            && sdl_haptic_backend().SetHapticAutocenter(native_handle(effectHandle_), autocenter);
    }

    bool HapticDevice::PauseEXT()
    {
        return effectHandle_ != nullptr
            && sdl_haptic_backend().PauseHaptic(native_handle(effectHandle_));
    }

    bool HapticDevice::ResumeEXT()
    {
        return effectHandle_ != nullptr
            && sdl_haptic_backend().ResumeHaptic(native_handle(effectHandle_));
    }
}

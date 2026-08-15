// SPDX-License-Identifier: MS-PL
#include "CNA/Input/HapticDevice.hpp"

#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/Input/IPlatformHaptics.hpp"

#include <type_traits>
#include <utility>

namespace CNA::Input
{
    namespace
    {
        CNA::Platform::IPlatformHaptics* HapticService(CNA::Platform::IPlatform* platform)
        {
            return platform != nullptr ? platform->GetHaptics() : nullptr;
        }

        CNA::Platform::HapticEffect ToPlatformEffect(const HapticEffectEXT& effect)
        {
            CNA::Platform::HapticEffect result;
            result.type = static_cast<CNA::Platform::HapticEffectType>(effect.type);
            result.direction.type =
                static_cast<CNA::Platform::HapticDirectionType>(effect.direction.type);
            result.direction.values = effect.direction.values;
            result.length = effect.length;
            result.delay = effect.delay;
            result.button = effect.button;
            result.interval = effect.interval;
            result.level = effect.level;
            result.period = effect.period;
            result.magnitude = effect.magnitude;
            result.offset = effect.offset;
            result.phase = effect.phase;
            result.rampStart = effect.rampStart;
            result.rampEnd = effect.rampEnd;
            result.rightSaturation = effect.rightSaturation;
            result.leftSaturation = effect.leftSaturation;
            result.rightCoefficient = effect.rightCoefficient;
            result.leftCoefficient = effect.leftCoefficient;
            result.deadband = effect.deadband;
            result.center = effect.center;
            result.largeMagnitude = effect.largeMagnitude;
            result.smallMagnitude = effect.smallMagnitude;
            result.customChannels = effect.customChannels;
            result.customPeriod = effect.customPeriod;
            result.customData = effect.customData;
            result.attackLength = effect.attackLength;
            result.attackLevel = effect.attackLevel;
            result.fadeLength = effect.fadeLength;
            result.fadeLevel = effect.fadeLevel;
            return result;
        }
    }

    HapticDevice::HapticDevice(
        std::unique_ptr<CNA::Platform::IPlatformHapticDevice> effectDevice,
        CNA::Platform::IPlatform* platform, std::optional<std::uint64_t> standaloneId,
        std::string standaloneName)
        : effectDevice_(std::move(effectDevice)), platform_(platform),
          standaloneId_(standaloneId), standaloneName_(std::move(standaloneName))
    {
    }

    HapticDevice::HapticDevice(HapticDevice&& other) noexcept
        : effectDevice_(std::move(other.effectDevice_)),
          platform_(std::exchange(other.platform_, nullptr)),
          standaloneId_(std::exchange(other.standaloneId_, std::nullopt)),
          standaloneName_(std::move(other.standaloneName_)),
          isDisposed_(std::exchange(other.isDisposed_, true))
    {
    }

    HapticDevice& HapticDevice::operator=(HapticDevice&& other) noexcept
    {
        if (this != &other)
        {
            Dispose();
            effectDevice_ = std::move(other.effectDevice_);
            platform_ = std::exchange(other.platform_, nullptr);
            standaloneId_ = std::exchange(other.standaloneId_, std::nullopt);
            standaloneName_ = std::move(other.standaloneName_);
            isDisposed_ = std::exchange(other.isDisposed_, true);
        }
        return *this;
    }

    HapticDevice::~HapticDevice() { Dispose(); }

    void HapticDevice::Dispose()
    {
        if (isDisposed_)
        {
            return;
        }
        isDisposed_ = true;
        if (standaloneId_.has_value())
        {
            if (CNA::Platform::IPlatformHaptics* service = HapticService(platform_))
            {
                (void)service->StopRumble(*standaloneId_);
            }
            standaloneId_.reset();
        }
        // The native handle must close while its subsystem reference is still held.
        effectDevice_.reset();
        if (platform_ != nullptr)
        {
            platform_->ReleaseSubsystem(CNA::Platform::PlatformSubsystem::Haptic);
            platform_ = nullptr;
        }
    }

    bool HapticDevice::IsOpenEXT() const
    {
        return effectDevice_ != nullptr || standaloneId_.has_value();
    }

    std::string HapticDevice::GetNameEXT() const
    {
        if (standaloneId_.has_value())
        {
            return standaloneName_;
        }
        return effectDevice_ != nullptr
            ? effectDevice_->GetCapabilities().name
            : std::string();
    }

    HapticCapabilitiesEXT HapticDevice::GetCapabilitiesEXT() const
    {
        if (!IsOpenEXT())
        {
            return {};
        }
        HapticCapabilitiesEXT result;
        result.isOpen = true;
        result.name = GetNameEXT();
        if (standaloneId_.has_value())
        {
            CNA::Platform::IPlatformHaptics* service = HapticService(platform_);
            result.rumbleSupported = service != nullptr
                && service->SupportsRumble(*standaloneId_);
        }
        if (effectDevice_ != nullptr)
        {
            const CNA::Platform::HapticDeviceCapabilities capabilities =
                effectDevice_->GetCapabilities();
            result.features = static_cast<HapticFeatureEXT>(capabilities.features);
            result.axisCount = capabilities.axisCount;
            result.maxEffects = capabilities.maxEffects;
            result.maxEffectsPlaying = capabilities.maxEffectsPlaying;
            if (!standaloneId_.has_value())
            {
                result.rumbleSupported = capabilities.rumbleSupported;
            }
        }
        return result;
    }

    bool HapticDevice::IsEffectSupportedEXT(const HapticEffectEXT& effect) const
    {
        return effectDevice_ != nullptr
            && effectDevice_->IsEffectSupported(ToPlatformEffect(effect));
    }

    bool HapticDevice::InitRumbleEXT()
    {
        if (standaloneId_.has_value())
        {
            CNA::Platform::IPlatformHaptics* service = HapticService(platform_);
            return service != nullptr && service->InitializeRumble(*standaloneId_);
        }
        return effectDevice_ != nullptr && effectDevice_->InitializeRumble();
    }

    bool HapticDevice::PlayRumbleEXT(const float strength, const std::uint32_t lengthMs)
    {
        if (standaloneId_.has_value())
        {
            CNA::Platform::IPlatformHaptics* service = HapticService(platform_);
            return service != nullptr && service->PlayRumble(*standaloneId_, strength, lengthMs);
        }
        return effectDevice_ != nullptr && effectDevice_->PlayRumble(strength, lengthMs);
    }

    bool HapticDevice::StopRumbleEXT()
    {
        if (standaloneId_.has_value())
        {
            CNA::Platform::IPlatformHaptics* service = HapticService(platform_);
            return service != nullptr && service->StopRumble(*standaloneId_);
        }
        return effectDevice_ != nullptr && effectDevice_->StopRumble();
    }

    int HapticDevice::CreateEffectEXT(const HapticEffectEXT& effect)
    {
        return effectDevice_ != nullptr
            ? effectDevice_->CreateEffect(ToPlatformEffect(effect))
            : -1;
    }

    bool HapticDevice::UpdateEffectEXT(const int effectId, const HapticEffectEXT& effect)
    {
        return effectDevice_ != nullptr
            && effectDevice_->UpdateEffect(effectId, ToPlatformEffect(effect));
    }

    bool HapticDevice::RunEffectEXT(const int effectId, const std::uint32_t iterations)
    {
        return effectDevice_ != nullptr && effectDevice_->RunEffect(effectId, iterations);
    }

    bool HapticDevice::StopEffectEXT(const int effectId)
    {
        return effectDevice_ != nullptr && effectDevice_->StopEffect(effectId);
    }

    void HapticDevice::DestroyEffectEXT(const int effectId)
    {
        if (effectDevice_ != nullptr)
        {
            effectDevice_->DestroyEffect(effectId);
        }
    }

    bool HapticDevice::GetEffectStatusEXT(const int effectId) const
    {
        return effectDevice_ != nullptr && effectDevice_->GetEffectStatus(effectId);
    }

    bool HapticDevice::StopAllEffectsEXT()
    {
        return effectDevice_ != nullptr && effectDevice_->StopAllEffects();
    }

    bool HapticDevice::SetGainEXT(const int gain)
    {
        return effectDevice_ != nullptr && effectDevice_->SetGain(gain);
    }

    bool HapticDevice::SetAutocenterEXT(const int autocenter)
    {
        return effectDevice_ != nullptr && effectDevice_->SetAutocenter(autocenter);
    }

    bool HapticDevice::PauseEXT()
    {
        return effectDevice_ != nullptr && effectDevice_->Pause();
    }

    bool HapticDevice::ResumeEXT()
    {
        return effectDevice_ != nullptr && effectDevice_->Resume();
    }
}

// SPDX-License-Identifier: MS-PL

#include "CNA/C/audio.h"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Audio/AudioChannels.hpp"
#include "Microsoft/Xna/Framework/Audio/NoAudioHardwareException.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundState.hpp"
#include "System/InvalidOperationException.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace {

using CNA::C::Detail::AddOwnedAudioResource;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CheckedElementByteCount;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::RemoveOwnedAudioResource;
using CNA::C::Detail::ValidateActiveGameHandle;
using Microsoft::Xna::Framework::Audio::AudioChannels;
using Microsoft::Xna::Framework::Audio::NoAudioHardwareException;
using Microsoft::Xna::Framework::Audio::SoundEffect;
using Microsoft::Xna::Framework::Audio::SoundEffectInstance;
using Microsoft::Xna::Framework::Audio::SoundState;

constexpr uint32_t StructureVersion = UINT32_C(1);

struct SoundEffectResource final {
    std::shared_ptr<SoundEffect> value;
    CNA_Handle parentGame;
    uint64_t instanceCount;
};

struct SoundEffectInstanceResource final {
    std::shared_ptr<SoundEffectInstance> value;
    std::shared_ptr<SoundEffectResource> parent;
};

[[nodiscard]] CNA_Result GetSoundEffect(
    const CNA_Handle handle,
    std::shared_ptr<SoundEffectResource>* const outSoundEffect)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle,
        ObjectKind::SoundEffect,
        outSoundEffect);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned SoundEffect handle is invalid for this call.");
}

[[nodiscard]] CNA_Result GetSoundEffectInstance(
    const CNA_Handle handle,
    std::shared_ptr<SoundEffectInstanceResource>* const outInstance)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle,
        ObjectKind::SoundEffectInstance,
        outInstance);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned SoundEffectInstance handle is invalid for this call.");
}

[[nodiscard]] bool TryMapChannels(
    const CNA_AudioChannels channels,
    AudioChannels* const outChannels) noexcept
{
    if (outChannels == nullptr ||
        (channels != CNA_AUDIO_CHANNELS_MONO && channels != CNA_AUDIO_CHANNELS_STEREO)) {
        return false;
    }
    *outChannels = static_cast<AudioChannels>(channels);
    return true;
}

[[nodiscard]] CNA_SoundState MapSoundState(const SoundState state) noexcept
{
    return static_cast<CNA_SoundState>(state);
}

} // namespace

CNA_Result cna_audio_get_capabilities(
    const CNA_Handle gameHandle,
    CNA_AudioCapabilities* const outCapabilities)
{
    return CallWithExceptionBarrier([&]() {
        if (outCapabilities == nullptr ||
            outCapabilities->struct_size < sizeof(CNA_AudioCapabilities) ||
            outCapabilities->struct_version != StructureVersion) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The audio-capabilities output structure is invalid.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        CNA_Bool isPlaybackAvailable = CNA_TRUE;
        try {
            static_cast<void>(SoundEffect::getMasterVolumeProperty());
        } catch (const NoAudioHardwareException&) {
            isPlaybackAvailable = CNA_FALSE;
        }

        const CNA_AudioCapabilities capabilities = {
            sizeof(CNA_AudioCapabilities),
            StructureVersion,
            isPlaybackAvailable,
            {0U, 0U, 0U},
            0U
        };
        *outCapabilities = capabilities;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_create_pcm16(
    const CNA_Handle gameHandle,
    const CNA_SoundEffectCreateInfo* const createInfo,
    const uint8_t* const pcmBytes,
    const uint64_t byteCount,
    CNA_Handle* const outSoundEffect)
{
    return CallWithExceptionBarrier([&]() {
        if (outSoundEffect == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The SoundEffect output handle is null.");
        }
        *outSoundEffect = CNA_INVALID_HANDLE;

        AudioChannels nativeChannels = AudioChannels::Mono;
        if (createInfo == nullptr ||
            createInfo->struct_size < sizeof(CNA_SoundEffectCreateInfo) ||
            createInfo->struct_version != StructureVersion || createInfo->reserved != 0U ||
            createInfo->sample_rate == 0U ||
            createInfo->sample_rate > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
            !TryMapChannels(createInfo->channels, &nativeChannels)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The SoundEffect PCM creation configuration is invalid.");
        }

        std::size_t nativeByteCount = 0U;
        if (const CNA_Result result = CheckedElementByteCount(
                pcmBytes,
                byteCount,
                sizeof(uint8_t),
                &nativeByteCount);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The SoundEffect PCM byte range is invalid.");
        }
        const uint64_t frameByteCount = UINT64_C(2) * createInfo->channels;
        if (byteCount == 0U || byteCount > static_cast<uint64_t>(std::numeric_limits<int32_t>::max()) ||
            byteCount % frameByteCount != 0U) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The SoundEffect PCM data must contain complete nonempty channel frames.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        const std::vector<SharpRuntime::bytecs> pcm(
            pcmBytes,
            pcmBytes + nativeByteCount);
        std::shared_ptr<SoundEffect> nativeSoundEffect;
        try {
            nativeSoundEffect = std::make_shared<SoundEffect>(
                pcm,
                static_cast<int32_t>(createInfo->sample_rate),
                nativeChannels);
        } catch (const NoAudioHardwareException& exception) {
            return Fail(
                CNA_RESULT_NOT_SUPPORTED,
                CNA_ERROR_CATEGORY_NOT_SUPPORTED,
                exception.what());
        }

        const auto resource = std::make_shared<SoundEffectResource>(
            SoundEffectResource{nativeSoundEffect, gameHandle, 0U});
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::SoundEffect,
            resource,
            outSoundEffect);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned SoundEffect handle could not be created.");
        }
        AddOwnedAudioResource();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_get_duration_ticks(
    const CNA_Handle soundEffectHandle,
    int64_t* const outDurationTicks)
{
    return CallWithExceptionBarrier([&]() {
        if (outDurationTicks == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The SoundEffect duration output is null.");
        }
        std::shared_ptr<SoundEffectResource> soundEffect;
        if (const CNA_Result result = GetSoundEffect(soundEffectHandle, &soundEffect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outDurationTicks = soundEffect->value->getDurationProperty().getTicksProperty();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_create_instance(
    const CNA_Handle soundEffectHandle,
    CNA_Handle* const outInstance)
{
    return CallWithExceptionBarrier([&]() {
        if (outInstance == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The SoundEffectInstance output handle is null.");
        }
        *outInstance = CNA_INVALID_HANDLE;

        std::shared_ptr<SoundEffectResource> soundEffect;
        if (const CNA_Result result = GetSoundEffect(soundEffectHandle, &soundEffect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto nativeInstance = std::make_shared<SoundEffectInstance>(
            soundEffect->value->CreateInstance());
        const auto resource = std::make_shared<SoundEffectInstanceResource>(
            SoundEffectInstanceResource{nativeInstance, soundEffect});
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::SoundEffectInstance,
            resource,
            outInstance);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned SoundEffectInstance handle could not be created.");
        }
        ++soundEffect->instanceCount;
        AddOwnedAudioResource();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_destroy(const CNA_Handle soundEffectHandle)
{
    return CallWithExceptionBarrier([&]() {
        std::shared_ptr<SoundEffectResource> soundEffect;
        if (const CNA_Result result = GetSoundEffect(soundEffectHandle, &soundEffect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (soundEffect->instanceCount != 0U) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "All C SoundEffectInstance children must be destroyed before their SoundEffect.");
        }
        soundEffect->value->Dispose();
        const CNA_Result result = GetRuntimeHandles().Release(soundEffectHandle);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned SoundEffect handle could not be released.");
        }
        RemoveOwnedAudioResource();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_instance_play(const CNA_Handle instanceHandle)
{
    return CallWithExceptionBarrier([&]() {
        std::shared_ptr<SoundEffectInstanceResource> instance;
        if (const CNA_Result result = GetSoundEffectInstance(instanceHandle, &instance);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        instance->value->Play();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_instance_pause(const CNA_Handle instanceHandle)
{
    return CallWithExceptionBarrier([&]() {
        std::shared_ptr<SoundEffectInstanceResource> instance;
        if (const CNA_Result result = GetSoundEffectInstance(instanceHandle, &instance);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        instance->value->Pause();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_instance_resume(const CNA_Handle instanceHandle)
{
    return CallWithExceptionBarrier([&]() {
        std::shared_ptr<SoundEffectInstanceResource> instance;
        if (const CNA_Result result = GetSoundEffectInstance(instanceHandle, &instance);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        instance->value->Resume();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_instance_stop(
    const CNA_Handle instanceHandle,
    const CNA_Bool immediate)
{
    return CallWithExceptionBarrier([&]() {
        if (immediate != CNA_FALSE && immediate != CNA_TRUE) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The SoundEffectInstance immediate-stop flag is invalid.");
        }
        std::shared_ptr<SoundEffectInstanceResource> instance;
        if (const CNA_Result result = GetSoundEffectInstance(instanceHandle, &instance);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        instance->value->Stop(immediate == CNA_TRUE);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_instance_get_info(
    const CNA_Handle instanceHandle,
    CNA_SoundEffectInstanceInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() {
        if (outInfo == nullptr || outInfo->struct_size < sizeof(CNA_SoundEffectInstanceInfo) ||
            outInfo->struct_version != StructureVersion) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The SoundEffectInstance info output structure is invalid.");
        }
        std::shared_ptr<SoundEffectInstanceResource> instance;
        if (const CNA_Result result = GetSoundEffectInstance(instanceHandle, &instance);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_SoundEffectInstanceInfo info = {
            sizeof(CNA_SoundEffectInstanceInfo),
            StructureVersion,
            MapSoundState(instance->value->getStateProperty()),
            instance->value->getIsLoopedProperty() ? CNA_TRUE : CNA_FALSE,
            {0U, 0U, 0U},
            instance->value->getVolumeProperty(),
            instance->value->getPitchProperty(),
            instance->value->getPanProperty(),
            0U
        };
        *outInfo = info;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_instance_set_volume(
    const CNA_Handle instanceHandle,
    const float volume)
{
    return CallWithExceptionBarrier([&]() {
        if (!std::isfinite(volume)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The SoundEffectInstance volume must be finite.");
        }
        std::shared_ptr<SoundEffectInstanceResource> instance;
        if (const CNA_Result result = GetSoundEffectInstance(instanceHandle, &instance);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        instance->value->setVolumeProperty(volume);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_instance_set_pitch(
    const CNA_Handle instanceHandle,
    const float pitch)
{
    return CallWithExceptionBarrier([&]() {
        if (!std::isfinite(pitch)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The SoundEffectInstance pitch must be finite.");
        }
        std::shared_ptr<SoundEffectInstanceResource> instance;
        if (const CNA_Result result = GetSoundEffectInstance(instanceHandle, &instance);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        instance->value->setPitchProperty(pitch);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_instance_set_pan(
    const CNA_Handle instanceHandle,
    const float pan)
{
    return CallWithExceptionBarrier([&]() {
        if (!std::isfinite(pan) || pan < -1.0F || pan > 1.0F) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The SoundEffectInstance pan must be finite and within [-1, 1].");
        }
        std::shared_ptr<SoundEffectInstanceResource> instance;
        if (const CNA_Result result = GetSoundEffectInstance(instanceHandle, &instance);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        instance->value->setPanProperty(pan);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_instance_set_is_looped(
    const CNA_Handle instanceHandle,
    const CNA_Bool isLooped)
{
    return CallWithExceptionBarrier([&]() {
        if (isLooped != CNA_FALSE && isLooped != CNA_TRUE) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The SoundEffectInstance loop flag is invalid.");
        }
        std::shared_ptr<SoundEffectInstanceResource> instance;
        if (const CNA_Result result = GetSoundEffectInstance(instanceHandle, &instance);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        try {
            instance->value->setIsLoopedProperty(isLooped == CNA_TRUE);
        } catch (const System::InvalidOperationException& exception) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                exception.what());
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_instance_destroy(const CNA_Handle instanceHandle)
{
    return CallWithExceptionBarrier([&]() {
        std::shared_ptr<SoundEffectInstanceResource> instance;
        if (const CNA_Result result = GetSoundEffectInstance(instanceHandle, &instance);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        instance->value->Dispose();
        const CNA_Result result = GetRuntimeHandles().Release(instanceHandle);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned SoundEffectInstance handle could not be released.");
        }
        if (instance->parent->instanceCount != 0U) {
            --instance->parent->instanceCount;
        }
        RemoveOwnedAudioResource();
        return CNA_RESULT_SUCCESS;
    });
}

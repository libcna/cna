// SPDX-License-Identifier: MS-PL

#include "CNA/C/audio.h"
#include "CnaCApiAudioDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Audio/AudioChannels.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEmitter.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioListener.hpp"
#include "Microsoft/Xna/Framework/Audio/DynamicSoundEffectInstance.hpp"
#include "Microsoft/Xna/Framework/Audio/Microphone.hpp"
#include "Microsoft/Xna/Framework/Audio/MicrophoneState.hpp"
#include "Microsoft/Xna/Framework/Audio/NoAudioHardwareException.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundState.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/TimeSpan.hpp"

#include <cmath>
#include <cstring>
#include <cstdint>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

using CNA::C::Detail::AddOwnedAudioResource;
using CNA::C::Detail::AudioRegistration;
using CNA::C::Detail::AudioRegistrationBase;
using CNA::C::Detail::PublishAudioRegistration;
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

[[nodiscard]] CNA_Vector3 ToC(const Microsoft::Xna::Framework::Vector3& value) noexcept
{
    return CNA_Vector3{value.X, value.Y, value.Z};
}

[[nodiscard]] bool IsFinite(const CNA_Vector3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] Microsoft::Xna::Framework::Vector3 ToNativeVector(const CNA_Vector3 value) noexcept
{
    return Microsoft::Xna::Framework::Vector3{value.x, value.y, value.z};
}

[[nodiscard]] CNA_Result ToNativeListener(
    const CNA_AudioListener* const value,
    Microsoft::Xna::Framework::Audio::AudioListener* const outListener)
{
    if (value == nullptr || value->struct_size < sizeof(CNA_AudioListener) ||
        value->struct_version != StructureVersion) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The AudioListener structure is invalid.");
    }
    if (!IsFinite(value->forward) || !IsFinite(value->position) || !IsFinite(value->up) ||
        !IsFinite(value->velocity)) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The AudioListener vectors must be finite.");
    }
    outListener->setForwardProperty(ToNativeVector(value->forward));
    outListener->setPositionProperty(ToNativeVector(value->position));
    outListener->setUpProperty(ToNativeVector(value->up));
    outListener->setVelocityProperty(ToNativeVector(value->velocity));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ToNativeEmitter(
    const CNA_AudioEmitter* const value,
    Microsoft::Xna::Framework::Audio::AudioEmitter* const outEmitter)
{
    if (value == nullptr || value->struct_size < sizeof(CNA_AudioEmitter) ||
        value->struct_version != StructureVersion) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The AudioEmitter structure is invalid.");
    }
    if (!std::isfinite(value->doppler_scale)) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The AudioEmitter Doppler scale must be finite.");
    }
    if (!IsFinite(value->forward) || !IsFinite(value->position) || !IsFinite(value->up) ||
        !IsFinite(value->velocity)) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The AudioEmitter vectors must be finite.");
    }
    outEmitter->setDopplerScaleProperty(value->doppler_scale);
    outEmitter->setForwardProperty(ToNativeVector(value->forward));
    outEmitter->setPositionProperty(ToNativeVector(value->position));
    outEmitter->setUpProperty(ToNativeVector(value->up));
    outEmitter->setVelocityProperty(ToNativeVector(value->velocity));
    return CNA_RESULT_SUCCESS;
}

} // namespace

namespace CNA::C::Detail {

CNA_Result CreateOwnedSoundEffect(
    std::shared_ptr<SoundEffect> soundEffect,
    const CNA_Handle parentGame,
    CNA_Handle* const outSoundEffect)
{
    if (soundEffect == nullptr || outSoundEffect == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The owned SoundEffect factory arguments are invalid.");
    }
    const auto resource = std::make_shared<SoundEffectResource>(
        SoundEffectResource{std::move(soundEffect), parentGame, 0U});
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
}

} // namespace CNA::C::Detail

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
        // A streaming instance has no parent effect, so there is no instance count to decrement.
        if (instance->parent && instance->parent->instanceCount != 0U) {
            --instance->parent->instanceCount;
        }
        RemoveOwnedAudioResource();
        return CNA_RESULT_SUCCESS;
    });
}

namespace {

[[nodiscard]] CNA_Result AudioInvalidInput(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
}

[[nodiscard]] CNA_Result CopyAudioText(
    const std::string& value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    if (outBytes == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
        return AudioInvalidInput("The audio text output is invalid.");
    }
    *outBytes = value.size();
    if (capacity < value.size()) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination capacity is smaller than the audio text.");
    }
    if (!value.empty()) {
        std::memcpy(destination, value.data(), value.size());
    }
    return CNA_RESULT_SUCCESS;
}

// Every creation route publishes its handle the same way, so the ownership bookkeeping stays in one
// place: the effect is a child of the game and the game refuses to be destroyed while one is alive.
[[nodiscard]] CNA_Result PublishSoundEffect(
    std::shared_ptr<SoundEffect> nativeSoundEffect,
    const CNA_Handle gameHandle,
    CNA_Handle* const outSoundEffect)
{
    const auto resource = std::make_shared<SoundEffectResource>(
        SoundEffectResource{std::move(nativeSoundEffect), gameHandle, 0U});
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
}

} // namespace

CNA_Result cna_sound_effect_create_pcm16_range_ext(
    const CNA_Handle gameHandle,
    const CNA_SoundEffectCreateInfo* const createInfo,
    const uint8_t* const pcmBytes,
    const uint64_t byteCount,
    const int32_t offset,
    const int32_t count,
    const int32_t loopStart,
    const int32_t loopLength,
    CNA_Handle* const outSoundEffect)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSoundEffect == nullptr) {
            return AudioInvalidInput("The SoundEffect output handle is null.");
        }
        *outSoundEffect = CNA_INVALID_HANDLE;
        AudioChannels nativeChannels = AudioChannels::Mono;
        if (createInfo == nullptr ||
            createInfo->struct_size < sizeof(CNA_SoundEffectCreateInfo) ||
            createInfo->struct_version != StructureVersion || createInfo->reserved != 0U ||
            createInfo->sample_rate == 0U ||
            createInfo->sample_rate > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
            !TryMapChannels(createInfo->channels, &nativeChannels)) {
            return AudioInvalidInput("The SoundEffect PCM creation configuration is invalid.");
        }
        std::size_t nativeByteCount = 0U;
        if (const CNA_Result result =
                CheckedElementByteCount(pcmBytes, byteCount, sizeof(uint8_t), &nativeByteCount);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The SoundEffect PCM byte range is invalid.");
        }
        // The canonical constructor takes the range as signed offsets into the buffer, so the range
        // is checked here rather than left to it: a negative or overrunning range would otherwise
        // reach the decoder as a length nobody validated.
        if (offset < 0 || count <= 0 || loopStart < 0 || loopLength < 0 ||
            static_cast<uint64_t>(offset) + static_cast<uint64_t>(count) > byteCount) {
            return AudioInvalidInput("The SoundEffect PCM range lies outside the buffer.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::vector<SharpRuntime::bytecs> pcm(pcmBytes, pcmBytes + nativeByteCount);
        auto nativeSoundEffect = std::make_shared<SoundEffect>(
            pcm,
            static_cast<SharpRuntime::intcs>(offset),
            static_cast<SharpRuntime::intcs>(count),
            static_cast<SharpRuntime::intcs>(createInfo->sample_rate),
            nativeChannels,
            static_cast<SharpRuntime::intcs>(loopStart),
            static_cast<SharpRuntime::intcs>(loopLength));
        return PublishSoundEffect(std::move(nativeSoundEffect), gameHandle, outSoundEffect);
    });
}

CNA_Result cna_sound_effect_create_from_encoded_ext(
    const CNA_Handle gameHandle,
    const uint8_t* const bytes,
    const uint64_t byteCount,
    CNA_Handle* const outSoundEffect)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSoundEffect == nullptr) {
            return AudioInvalidInput("The SoundEffect output handle is null.");
        }
        *outSoundEffect = CNA_INVALID_HANDLE;
        std::size_t nativeByteCount = 0U;
        if (const CNA_Result result =
                CheckedElementByteCount(bytes, byteCount, sizeof(uint8_t), &nativeByteCount);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The encoded audio byte range is invalid.");
        }
        if (byteCount == UINT64_C(0)) {
            return AudioInvalidInput("The encoded audio data is empty.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The canonical factory reads a C++ stream to the end, so C hands it the bytes it would
        // have read. It answers a raw owning pointer, which this handle adopts immediately.
        std::string encoded(reinterpret_cast<const char*>(bytes), nativeByteCount);
        std::istringstream stream(std::move(encoded), std::ios::binary);
        const std::shared_ptr<SoundEffect> nativeSoundEffect(SoundEffect::FromStream(stream));
        if (!nativeSoundEffect) {
            return Fail(
                CNA_RESULT_NOT_SUPPORTED,
                CNA_ERROR_CATEGORY_NOT_SUPPORTED,
                "The encoded audio could not be decoded by this build.");
        }
        return PublishSoundEffect(nativeSoundEffect, gameHandle, outSoundEffect);
    });
}

CNA_Result cna_sound_effect_create_from_asset_ext(
    const CNA_Handle gameHandle,
    const CNA_StringView assetName,
    CNA_Handle* const outSoundEffect)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSoundEffect == nullptr) {
            return AudioInvalidInput("The SoundEffect output handle is null.");
        }
        *outSoundEffect = CNA_INVALID_HANDLE;
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string path;
        if (const CNA_Result result =
                CNA::C::Detail::CopyStringView(assetName, false, &path);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The audio asset name is not valid UTF-8.");
        }
        // An empty path is not an error: the canonical constructor answers an effect with no audio.
        auto nativeSoundEffect = std::make_shared<SoundEffect>(path);
        return PublishSoundEffect(std::move(nativeSoundEffect), gameHandle, outSoundEffect);
    });
}

CNA_Result cna_sound_effect_get_is_disposed(
    const CNA_Handle soundEffectHandle,
    CNA_Bool* const outDisposed)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outDisposed == nullptr) {
            return AudioInvalidInput("The disposal output is null.");
        }
        std::shared_ptr<SoundEffectResource> soundEffect;
        if (const CNA_Result result = GetSoundEffect(soundEffectHandle, &soundEffect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outDisposed = soundEffect->value->getIsDisposedProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_get_name_size(
    const CNA_Handle soundEffectHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return AudioInvalidInput("The name size output is null.");
        }
        std::shared_ptr<SoundEffectResource> soundEffect;
        if (const CNA_Result result = GetSoundEffect(soundEffectHandle, &soundEffect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = soundEffect->value->getNameProperty().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_copy_name(
    const CNA_Handle soundEffectHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SoundEffectResource> soundEffect;
        if (const CNA_Result result = GetSoundEffect(soundEffectHandle, &soundEffect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyAudioText(soundEffect->value->getNameProperty(), destination, capacity, outBytes);
    });
}

CNA_Result cna_sound_effect_set_name(
    const CNA_Handle soundEffectHandle,
    const CNA_StringView name)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SoundEffectResource> soundEffect;
        if (const CNA_Result result = GetSoundEffect(soundEffectHandle, &soundEffect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string value;
        if (const CNA_Result result = CNA::C::Detail::CopyStringView(name, false, &value);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The SoundEffect name is not valid UTF-8.");
        }
        soundEffect->value->setNameProperty(std::move(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_get_master_volume(const CNA_Handle gameHandle, float* const outVolume)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outVolume == nullptr) {
            return AudioInvalidInput("The master volume output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outVolume = SoundEffect::getMasterVolumeProperty();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_set_master_volume(const CNA_Handle gameHandle, const float volume)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        SoundEffect::setMasterVolumeProperty(volume);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_get_distance_scale(const CNA_Handle gameHandle, float* const outScale)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outScale == nullptr) {
            return AudioInvalidInput("The distance scale output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outScale = SoundEffect::getDistanceScaleProperty();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_set_distance_scale(const CNA_Handle gameHandle, const float scale)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        SoundEffect::setDistanceScaleProperty(scale);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_get_doppler_scale(const CNA_Handle gameHandle, float* const outScale)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outScale == nullptr) {
            return AudioInvalidInput("The Doppler scale output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outScale = SoundEffect::getDopplerScaleProperty();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_set_doppler_scale(const CNA_Handle gameHandle, const float scale)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        SoundEffect::setDopplerScaleProperty(scale);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_get_speed_of_sound(const CNA_Handle gameHandle, float* const outSpeed)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSpeed == nullptr) {
            return AudioInvalidInput("The speed of sound output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outSpeed = SoundEffect::getSpeedOfSoundProperty();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_set_speed_of_sound(const CNA_Handle gameHandle, const float speed)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        SoundEffect::setSpeedOfSoundProperty(speed);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_play(const CNA_Handle soundEffectHandle, CNA_Bool* const outPlayed)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPlayed == nullptr) {
            return AudioInvalidInput("The playback output is null.");
        }
        std::shared_ptr<SoundEffectResource> soundEffect;
        if (const CNA_Result result = GetSoundEffect(soundEffectHandle, &soundEffect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outPlayed = soundEffect->value->Play() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_play_with_settings(
    const CNA_Handle soundEffectHandle,
    const float volume,
    const float pitch,
    const float pan,
    CNA_Bool* const outPlayed)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPlayed == nullptr) {
            return AudioInvalidInput("The playback output is null.");
        }
        std::shared_ptr<SoundEffectResource> soundEffect;
        if (const CNA_Result result = GetSoundEffect(soundEffectHandle, &soundEffect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // Pan is range-checked by the canonical route and pitch is clamped; the asymmetry is
        // reported rather than evened out here.
        *outPlayed = soundEffect->value->Play(volume, pitch, pan) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_get_sample_duration_ticks(
    const int32_t sizeInBytes,
    const int32_t sampleRate,
    const CNA_AudioChannels channels,
    int64_t* const outTicks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTicks == nullptr) {
            return AudioInvalidInput("The sample duration output is null.");
        }
        AudioChannels nativeChannels = AudioChannels::Mono;
        if (!TryMapChannels(channels, &nativeChannels)) {
            return AudioInvalidInput("The audio channel count is not a defined identity.");
        }
        *outTicks = static_cast<int64_t>(
            SoundEffect::GetSampleDuration(
                static_cast<SharpRuntime::intcs>(sizeInBytes),
                static_cast<SharpRuntime::intcs>(sampleRate),
                nativeChannels)
                .getTicksProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_get_sample_size_in_bytes(
    const int64_t durationTicks,
    const int32_t sampleRate,
    const CNA_AudioChannels channels,
    int32_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return AudioInvalidInput("The sample size output is null.");
        }
        AudioChannels nativeChannels = AudioChannels::Mono;
        if (!TryMapChannels(channels, &nativeChannels)) {
            return AudioInvalidInput("The audio channel count is not a defined identity.");
        }
        *outBytes = static_cast<int32_t>(SoundEffect::GetSampleSizeInBytes(
            System::TimeSpan(static_cast<SharpRuntime::longcs>(durationTicks)),
            static_cast<SharpRuntime::intcs>(sampleRate),
            nativeChannels));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_get_type_name_size(
    const CNA_Handle soundEffectHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return AudioInvalidInput("The type-name size output is null.");
        }
        std::shared_ptr<SoundEffectResource> soundEffect;
        if (const CNA_Result result = GetSoundEffect(soundEffectHandle, &soundEffect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = soundEffect->value->GetTypeName().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_copy_type_name(
    const CNA_Handle soundEffectHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SoundEffectResource> soundEffect;
        if (const CNA_Result result = GetSoundEffect(soundEffectHandle, &soundEffect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyAudioText(soundEffect->value->GetTypeName(), destination, capacity, outBytes);
    });
}

CNA_Result cna_sound_effect_instance_get_is_disposed(
    const CNA_Handle instanceHandle,
    CNA_Bool* const outDisposed)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outDisposed == nullptr) {
            return AudioInvalidInput("The disposal output is null.");
        }
        std::shared_ptr<SoundEffectInstanceResource> instance;
        if (const CNA_Result result = GetSoundEffectInstance(instanceHandle, &instance);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outDisposed = instance->value->getIsDisposedProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_instance_get_type_name_size(
    const CNA_Handle instanceHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return AudioInvalidInput("The type-name size output is null.");
        }
        std::shared_ptr<SoundEffectInstanceResource> instance;
        if (const CNA_Result result = GetSoundEffectInstance(instanceHandle, &instance);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = instance->value->GetTypeName().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_instance_copy_type_name(
    const CNA_Handle instanceHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SoundEffectInstanceResource> instance;
        if (const CNA_Result result = GetSoundEffectInstance(instanceHandle, &instance);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyAudioText(instance->value->GetTypeName(), destination, capacity, outBytes);
    });
}

namespace {

using Microsoft::Xna::Framework::Audio::DynamicSoundEffectInstance;
using Microsoft::Xna::Framework::Audio::Microphone;
using Microsoft::Xna::Framework::Audio::MicrophoneState;

// A streaming instance is a sound-effect instance, so it lives under the same handle kind and every
// existing instance route accepts it. This side pointer is what the streaming-only routes need, and
// its absence is what tells them the handle is an ordinary instance.
[[nodiscard]] CNA_Result BorrowDynamicInstance(
    const CNA_Handle handle,
    std::shared_ptr<SoundEffectInstanceResource>* const outInstance,
    DynamicSoundEffectInstance** const outDynamic)
{
    if (const CNA_Result result = GetSoundEffectInstance(handle, outInstance);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outDynamic = dynamic_cast<DynamicSoundEffectInstance*>((*outInstance)->value.get());
    if (*outDynamic == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_STATE,
            CNA_ERROR_CATEGORY_STATE,
            "This SoundEffectInstance does not stream caller-supplied buffers.");
    }
    return CNA_RESULT_SUCCESS;
}

// The canonical microphone list is owned by the runtime and handed out as raw pointers, so C
// addresses it by index the way every other enumerated device in this ABI is addressed.
[[nodiscard]] CNA_Result BorrowMicrophone(
    const CNA_Handle gameHandle,
    const uint64_t index,
    Microphone** const outMicrophone)
{
    if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const auto& microphones = Microphone::getAllProperty();
    if (index >= static_cast<uint64_t>(microphones.size()) ||
        microphones[static_cast<std::size_t>(index)] == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_RANGE,
            "The microphone index is at or past the reported count.");
    }
    *outMicrophone = microphones[static_cast<std::size_t>(index)];
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_dynamic_sound_effect_instance_create(
    const CNA_Handle gameHandle,
    const int32_t sampleRate,
    const CNA_AudioChannels channels,
    CNA_Handle* const outInstance)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInstance == nullptr) {
            return AudioInvalidInput("The streaming instance output handle is null.");
        }
        *outInstance = CNA_INVALID_HANDLE;
        AudioChannels nativeChannels = AudioChannels::Mono;
        if (!TryMapChannels(channels, &nativeChannels)) {
            return AudioInvalidInput("The audio channel count is not a defined identity.");
        }
        if (sampleRate <= 0) {
            return AudioInvalidInput("The sample rate must be positive.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto nativeInstance = std::make_shared<DynamicSoundEffectInstance>(
            static_cast<SharpRuntime::intcs>(sampleRate),
            nativeChannels);
        // No parent sound effect: the caller is the source of every sample, so the instance-count
        // bookkeeping a SoundEffect child carries has nothing to point at here.
        const auto resource = std::make_shared<SoundEffectInstanceResource>(
            SoundEffectInstanceResource{std::move(nativeInstance), nullptr});
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::SoundEffectInstance,
            resource,
            outInstance);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned streaming instance handle could not be created.");
        }
        AddOwnedAudioResource();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_dynamic_sound_effect_instance_get_pending_buffer_count(
    const CNA_Handle instanceHandle,
    int32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return AudioInvalidInput("The pending buffer count output is null.");
        }
        std::shared_ptr<SoundEffectInstanceResource> instance;
        DynamicSoundEffectInstance* dynamicInstance = nullptr;
        if (const CNA_Result result =
                BorrowDynamicInstance(instanceHandle, &instance, &dynamicInstance);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<int32_t>(dynamicInstance->getPendingBufferCountProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_dynamic_sound_effect_instance_submit_buffer(
    const CNA_Handle instanceHandle,
    const uint8_t* const bytes,
    const uint64_t byteCount,
    const int32_t offset,
    const int32_t count)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::size_t nativeByteCount = 0U;
        if (const CNA_Result result =
                CheckedElementByteCount(bytes, byteCount, sizeof(uint8_t), &nativeByteCount);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The submitted audio byte range is invalid.");
        }
        if (offset < 0 || count <= 0 ||
            static_cast<uint64_t>(offset) + static_cast<uint64_t>(count) > byteCount) {
            return AudioInvalidInput("The submitted audio range lies outside the buffer.");
        }
        std::shared_ptr<SoundEffectInstanceResource> instance;
        DynamicSoundEffectInstance* dynamicInstance = nullptr;
        if (const CNA_Result result =
                BorrowDynamicInstance(instanceHandle, &instance, &dynamicInstance);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The bytes are copied here, which is what lets a producer thread reuse its buffer the
        // moment this returns.
        const std::vector<SharpRuntime::bytecs> pcm(bytes, bytes + nativeByteCount);
        dynamicInstance->SubmitBuffer(
            pcm,
            static_cast<SharpRuntime::intcs>(offset),
            static_cast<SharpRuntime::intcs>(count));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_dynamic_sound_effect_instance_submit_float_buffer_ext(
    const CNA_Handle instanceHandle,
    const float* const samples,
    const uint64_t sampleCount,
    const int32_t offset,
    const int32_t count)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::size_t nativeByteCount = 0U;
        if (const CNA_Result result =
                CheckedElementByteCount(samples, sampleCount, sizeof(float), &nativeByteCount);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The submitted float sample range is invalid.");
        }
        if (offset < 0 || count <= 0 ||
            static_cast<uint64_t>(offset) + static_cast<uint64_t>(count) > sampleCount) {
            return AudioInvalidInput("The submitted float range lies outside the buffer.");
        }
        std::shared_ptr<SoundEffectInstanceResource> instance;
        DynamicSoundEffectInstance* dynamicInstance = nullptr;
        if (const CNA_Result result =
                BorrowDynamicInstance(instanceHandle, &instance, &dynamicInstance);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::vector<float> buffer(samples, samples + sampleCount);
        dynamicInstance->SubmitFloatBufferEXT(
            buffer,
            static_cast<SharpRuntime::intcs>(offset),
            static_cast<SharpRuntime::intcs>(count));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_dynamic_sound_effect_instance_queue_initial_buffers_ext(
    const CNA_Handle instanceHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SoundEffectInstanceResource> instance;
        DynamicSoundEffectInstance* dynamicInstance = nullptr;
        if (const CNA_Result result =
                BorrowDynamicInstance(instanceHandle, &instance, &dynamicInstance);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        dynamicInstance->QueueInitialBuffers();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_dynamic_sound_effect_instance_clear_buffers_ext(const CNA_Handle instanceHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SoundEffectInstanceResource> instance;
        DynamicSoundEffectInstance* dynamicInstance = nullptr;
        if (const CNA_Result result =
                BorrowDynamicInstance(instanceHandle, &instance, &dynamicInstance);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        dynamicInstance->ClearBuffers();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_dynamic_sound_effect_instance_update_ext(const CNA_Handle instanceHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SoundEffectInstanceResource> instance;
        DynamicSoundEffectInstance* dynamicInstance = nullptr;
        if (const CNA_Result result =
                BorrowDynamicInstance(instanceHandle, &instance, &dynamicInstance);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        dynamicInstance->Update();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_dynamic_sound_effect_instance_get_sample_duration_ticks(
    const CNA_Handle instanceHandle,
    const int32_t sizeInBytes,
    int64_t* const outTicks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTicks == nullptr) {
            return AudioInvalidInput("The sample duration output is null.");
        }
        std::shared_ptr<SoundEffectInstanceResource> instance;
        DynamicSoundEffectInstance* dynamicInstance = nullptr;
        if (const CNA_Result result =
                BorrowDynamicInstance(instanceHandle, &instance, &dynamicInstance);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outTicks = static_cast<int64_t>(
            dynamicInstance->GetSampleDuration(static_cast<SharpRuntime::intcs>(sizeInBytes))
                .getTicksProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_dynamic_sound_effect_instance_get_sample_size_in_bytes(
    const CNA_Handle instanceHandle,
    const int64_t durationTicks,
    int32_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return AudioInvalidInput("The sample size output is null.");
        }
        std::shared_ptr<SoundEffectInstanceResource> instance;
        DynamicSoundEffectInstance* dynamicInstance = nullptr;
        if (const CNA_Result result =
                BorrowDynamicInstance(instanceHandle, &instance, &dynamicInstance);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = static_cast<int32_t>(dynamicInstance->GetSampleSizeInBytes(
            System::TimeSpan(static_cast<SharpRuntime::longcs>(durationTicks))));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_dynamic_sound_effect_instance_subscribe_buffer_needed(
    const CNA_Handle instanceHandle,
    const CNA_AudioEventCallback callback,
    void* const context,
    CNA_Handle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRegistration == nullptr) {
            return AudioInvalidInput("The audio registration output is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return AudioInvalidInput("The audio event callback is null.");
        }
        std::shared_ptr<SoundEffectInstanceResource> instance;
        DynamicSoundEffectInstance* dynamicInstance = nullptr;
        if (const CNA_Result result =
                BorrowDynamicInstance(instanceHandle, &instance, &dynamicInstance);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto* const source = &dynamicInstance->BufferNeeded;
        const auto token = source->Add(
            [callback, context](System::Object*, const System::EventArgs&) { callback(context); });
        return PublishAudioRegistration(
            std::make_shared<AudioRegistration>(instance, source, token),
            outRegistration);
    });
}

CNA_Result cna_dynamic_sound_effect_instance_get_type_name_size(
    const CNA_Handle instanceHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return AudioInvalidInput("The type-name size output is null.");
        }
        std::shared_ptr<SoundEffectInstanceResource> instance;
        DynamicSoundEffectInstance* dynamicInstance = nullptr;
        if (const CNA_Result result =
                BorrowDynamicInstance(instanceHandle, &instance, &dynamicInstance);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = dynamicInstance->GetTypeName().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_dynamic_sound_effect_instance_copy_type_name(
    const CNA_Handle instanceHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SoundEffectInstanceResource> instance;
        DynamicSoundEffectInstance* dynamicInstance = nullptr;
        if (const CNA_Result result =
                BorrowDynamicInstance(instanceHandle, &instance, &dynamicInstance);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyAudioText(dynamicInstance->GetTypeName(), destination, capacity, outBytes);
    });
}

CNA_Result cna_audio_unsubscribe_ext(const CNA_Handle registration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AudioRegistrationBase> value;
        if (const CNA_Result result = GetRuntimeHandles().Get(
                registration,
                ObjectKind::AudioEventRegistration,
                &value);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The audio registration handle is invalid for this call.");
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(registration);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The audio registration handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_microphone_get_count(const CNA_Handle gameHandle, uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return AudioInvalidInput("The microphone count output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<uint64_t>(Microphone::getAllProperty().size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_microphone_get_default_index_ext(
    const CNA_Handle gameHandle,
    uint64_t* const outIndex,
    CNA_Bool* const outAvailable)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIndex == nullptr || outAvailable == nullptr) {
            return AudioInvalidInput("The default microphone outputs are invalid.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outAvailable = CNA_FALSE;
        const Microphone* const preferred = Microphone::getDefaultProperty();
        if (preferred == nullptr) {
            return CNA_RESULT_SUCCESS;
        }
        const auto& microphones = Microphone::getAllProperty();
        for (std::size_t index = 0U; index < microphones.size(); ++index) {
            if (microphones[index] == preferred) {
                *outIndex = static_cast<uint64_t>(index);
                *outAvailable = CNA_TRUE;
                return CNA_RESULT_SUCCESS;
            }
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_microphone_get_name_size_at(
    const CNA_Handle gameHandle,
    const uint64_t index,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return AudioInvalidInput("The microphone name size output is null.");
        }
        Microphone* microphone = nullptr;
        if (const CNA_Result result = BorrowMicrophone(gameHandle, index, &microphone);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = microphone->Name.size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_microphone_copy_name_at(
    const CNA_Handle gameHandle,
    const uint64_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Microphone* microphone = nullptr;
        if (const CNA_Result result = BorrowMicrophone(gameHandle, index, &microphone);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyAudioText(microphone->Name, destination, capacity, outBytes);
    });
}

CNA_Result cna_microphone_get_buffer_duration_ticks_at(
    const CNA_Handle gameHandle,
    const uint64_t index,
    int64_t* const outTicks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTicks == nullptr) {
            return AudioInvalidInput("The buffer duration output is null.");
        }
        Microphone* microphone = nullptr;
        if (const CNA_Result result = BorrowMicrophone(gameHandle, index, &microphone);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outTicks = static_cast<int64_t>(
            microphone->getBufferDurationProperty().getTicksProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_microphone_set_buffer_duration_ticks_at(
    const CNA_Handle gameHandle,
    const uint64_t index,
    const int64_t ticks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Microphone* microphone = nullptr;
        if (const CNA_Result result = BorrowMicrophone(gameHandle, index, &microphone);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        microphone->setBufferDurationProperty(
            System::TimeSpan(static_cast<SharpRuntime::longcs>(ticks)));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_microphone_get_is_headset_at(
    const CNA_Handle gameHandle,
    const uint64_t index,
    CNA_Bool* const outHeadset)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHeadset == nullptr) {
            return AudioInvalidInput("The headset output is null.");
        }
        Microphone* microphone = nullptr;
        if (const CNA_Result result = BorrowMicrophone(gameHandle, index, &microphone);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outHeadset = microphone->getIsHeadsetProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_microphone_get_sample_rate_at(
    const CNA_Handle gameHandle,
    const uint64_t index,
    int32_t* const outSampleRate)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSampleRate == nullptr) {
            return AudioInvalidInput("The sample rate output is null.");
        }
        Microphone* microphone = nullptr;
        if (const CNA_Result result = BorrowMicrophone(gameHandle, index, &microphone);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outSampleRate = static_cast<int32_t>(microphone->getSampleRateProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_microphone_get_state_at(
    const CNA_Handle gameHandle,
    const uint64_t index,
    CNA_MicrophoneState* const outState)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outState == nullptr) {
            return AudioInvalidInput("The microphone state output is null.");
        }
        Microphone* microphone = nullptr;
        if (const CNA_Result result = BorrowMicrophone(gameHandle, index, &microphone);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outState = static_cast<CNA_MicrophoneState>(microphone->getStateProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_microphone_start_at(const CNA_Handle gameHandle, const uint64_t index)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Microphone* microphone = nullptr;
        if (const CNA_Result result = BorrowMicrophone(gameHandle, index, &microphone);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        microphone->Start();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_microphone_stop_at(const CNA_Handle gameHandle, const uint64_t index)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Microphone* microphone = nullptr;
        if (const CNA_Result result = BorrowMicrophone(gameHandle, index, &microphone);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        microphone->Stop();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_microphone_get_data_at(
    const CNA_Handle gameHandle,
    const uint64_t index,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
            return AudioInvalidInput("The capture read output is invalid.");
        }
        Microphone* microphone = nullptr;
        if (const CNA_Result result = BorrowMicrophone(gameHandle, index, &microphone);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = UINT64_C(0);
        if (capacity == UINT64_C(0)) {
            return CNA_RESULT_SUCCESS;
        }
        if (capacity > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
            return AudioInvalidInput("The capture destination is larger than the canonical limit.");
        }
        // A short read is the canonical answer rather than a failure: capture is a stream, so the
        // route reports how much arrived instead of refusing a buffer it could not fill.
        std::vector<SharpRuntime::bytecs> buffer(static_cast<std::size_t>(capacity), 0U);
        const auto read = static_cast<uint64_t>(microphone->GetData(buffer));
        const uint64_t copied = read < capacity ? read : capacity;
        if (copied != UINT64_C(0)) {
            std::memcpy(destination, buffer.data(), static_cast<std::size_t>(copied));
        }
        *outBytes = copied;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_microphone_get_sample_duration_ticks_at(
    const CNA_Handle gameHandle,
    const uint64_t index,
    const int32_t sizeInBytes,
    int64_t* const outTicks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTicks == nullptr) {
            return AudioInvalidInput("The sample duration output is null.");
        }
        Microphone* microphone = nullptr;
        if (const CNA_Result result = BorrowMicrophone(gameHandle, index, &microphone);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outTicks = static_cast<int64_t>(
            microphone->GetSampleDuration(static_cast<SharpRuntime::intcs>(sizeInBytes))
                .getTicksProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_microphone_get_sample_size_in_bytes_at(
    const CNA_Handle gameHandle,
    const uint64_t index,
    const int64_t durationTicks,
    int32_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return AudioInvalidInput("The sample size output is null.");
        }
        Microphone* microphone = nullptr;
        if (const CNA_Result result = BorrowMicrophone(gameHandle, index, &microphone);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = static_cast<int32_t>(microphone->GetSampleSizeInBytes(
            System::TimeSpan(static_cast<SharpRuntime::longcs>(durationTicks))));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_microphone_subscribe_buffer_ready_at(
    const CNA_Handle gameHandle,
    const uint64_t index,
    const CNA_AudioEventCallback callback,
    void* const context,
    CNA_Handle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRegistration == nullptr) {
            return AudioInvalidInput("The audio registration output is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return AudioInvalidInput("The audio event callback is null.");
        }
        Microphone* microphone = nullptr;
        if (const CNA_Result result = BorrowMicrophone(gameHandle, index, &microphone);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto* const source = &microphone->BufferReady;
        const auto token = source->Add(
            [callback, context](System::Object*, const System::EventArgs&) { callback(context); });
        // The runtime owns every microphone and outlives each registration, so this one needs no
        // owner reference of its own.
        return PublishAudioRegistration(
            std::make_shared<AudioRegistration>(nullptr, source, token),
            outRegistration);
    });
}

CNA_Result cna_microphone_check_all_buffers_ext(const CNA_Handle gameHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Microphone::CheckAllBuffers();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_microphone_get_type_name_size(
    const CNA_Handle gameHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return AudioInvalidInput("The type-name size output is null.");
        }
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The name belongs to the type, so a machine with no microphone still answers it.
        static const std::string typeName = "Microsoft.Xna.Framework.Audio.Microphone";
        *outBytes = typeName.size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_microphone_copy_type_name(
    const CNA_Handle gameHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        static const std::string typeName = "Microsoft.Xna.Framework.Audio.Microphone";
        return CopyAudioText(typeName, destination, capacity, outBytes);
    });
}

CNA_Result cna_audio_emitter_init(CNA_AudioEmitter* const outEmitter)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEmitter == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The AudioEmitter output is null.");
        }
        const Microsoft::Xna::Framework::Audio::AudioEmitter emitter;
        const CNA_AudioEmitter value = {
            sizeof(CNA_AudioEmitter),
            StructureVersion,
            emitter.getDopplerScaleProperty(),
            ToC(emitter.getForwardProperty()),
            ToC(emitter.getPositionProperty()),
            ToC(emitter.getUpProperty()),
            ToC(emitter.getVelocityProperty())
        };
        *outEmitter = value;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_audio_listener_init(CNA_AudioListener* const outListener)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outListener == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The AudioListener output is null.");
        }
        const Microsoft::Xna::Framework::Audio::AudioListener listener;
        const CNA_AudioListener value = {
            sizeof(CNA_AudioListener),
            StructureVersion,
            ToC(listener.getForwardProperty()),
            ToC(listener.getPositionProperty()),
            ToC(listener.getUpProperty()),
            ToC(listener.getVelocityProperty())
        };
        *outListener = value;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_instance_apply_3d(
    const CNA_Handle instanceHandle,
    const CNA_AudioListener* const listener,
    const CNA_AudioEmitter* const emitter)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Microsoft::Xna::Framework::Audio::AudioListener nativeListener;
        Microsoft::Xna::Framework::Audio::AudioEmitter nativeEmitter;
        if (const CNA_Result result = ToNativeListener(listener, &nativeListener);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ToNativeEmitter(emitter, &nativeEmitter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<SoundEffectInstanceResource> instance;
        if (const CNA_Result result = GetSoundEffectInstance(instanceHandle, &instance);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        instance->value->Apply3D(nativeListener, nativeEmitter);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_instance_apply_3d_multi_ext(
    const CNA_Handle instanceHandle,
    const CNA_AudioListener* const listeners,
    const uint64_t listenerCount,
    const CNA_AudioEmitter* const emitter)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (listeners == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The AudioListener array is null.");
        }
        if (listenerCount >
            static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The AudioListener count is too large.");
        }
        // At least one element, so the pointer handed to the canonical overload is never null even
        // for an empty array: a count of zero has to reach the canonical count check and be refused
        // as unsupported, not be mistaken for a null array.
        std::vector<Microsoft::Xna::Framework::Audio::AudioListener> nativeListeners(
            static_cast<std::size_t>(listenerCount == 0U ? 1U : listenerCount));
        for (uint64_t index = 0U; index < listenerCount; ++index) {
            if (const CNA_Result result =
                    ToNativeListener(&listeners[index], &nativeListeners[static_cast<std::size_t>(index)]);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
        }
        Microsoft::Xna::Framework::Audio::AudioEmitter nativeEmitter;
        if (const CNA_Result result = ToNativeEmitter(emitter, &nativeEmitter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<SoundEffectInstanceResource> instance;
        if (const CNA_Result result = GetSoundEffectInstance(instanceHandle, &instance);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The canonical overload accepts any count and then refuses everything but one; the empty
        // array reaches it as well, so the refusal is the canonical one rather than an early return
        // invented here.
        instance->value->Apply3D(
            nativeListeners.data(),
            static_cast<int>(listenerCount),
            nativeEmitter);
        return CNA_RESULT_SUCCESS;
    });
}

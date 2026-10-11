// SPDX-License-Identifier: MS-PL

#include "CNA/C/audio.h"
#include "CnaCApiAudioDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"
#include "CnaCApiOutput.hpp"
#include "CnaCApiMarshal.hpp"

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
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CheckedElementByteCount;
using CNA::C::Detail::CopyText;
using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::InvalidState;
using CNA::C::Detail::IsFinite;
using CNA::C::Detail::IsVersionedStructure;
using CNA::C::Detail::LookupHandle;
using CNA::C::Detail::NotSupported;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::PublishAudioRegistration;
using CNA::C::Detail::ReleaseHandle;
using CNA::C::Detail::RemoveOwnedAudioResource;
using CNA::C::Detail::StructureVersion;
using CNA::C::Detail::ToC;
using CNA::C::Detail::ToCBool;
using CNA::C::Detail::ToNative;
using CNA::C::Detail::ValidateActiveGameHandle;
using CNA::C::Detail::ValidateVersionedStructure;
using Microsoft::Xna::Framework::Audio::AudioChannels;
using Microsoft::Xna::Framework::Audio::NoAudioHardwareException;
using Microsoft::Xna::Framework::Audio::SoundEffect;
using Microsoft::Xna::Framework::Audio::SoundEffectInstance;
using Microsoft::Xna::Framework::Audio::SoundState;

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
    return LookupHandle(handle, ObjectKind::SoundEffect, outSoundEffect);
}

// CBIND-154: fire-and-forget playback from any thread. XNA's SoundEffect.Play answered on every
// thread, and games call it from their own (stpettersens/21's dealer plays its shuffle sound on the
// thread it shuffles on while the game thread waits for that thread). Playing reads only the
// effect's immutable audio and goes to the mixer, which both backends serialize; queueing it on
// the game thread instead left such a game waiting on itself.
[[nodiscard]] CNA_Result GetSoundEffectToPlay(
    const CNA_Handle handle,
    std::shared_ptr<SoundEffectResource>* const outSoundEffect)
{
    const CNA_Result result = GetRuntimeHandles().GetFromAnyThread(
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
    return LookupHandle(handle, ObjectKind::SoundEffectInstance, outInstance);
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

[[nodiscard]] CNA_Result ToNativeListener(
    const CNA_AudioListener* const value,
    Microsoft::Xna::Framework::Audio::AudioListener* const outListener)
{
    if (const CNA_Result result =
            ValidateVersionedStructure(value, "The AudioListener structure is invalid.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (!IsFinite(value->forward) || !IsFinite(value->position) || !IsFinite(value->up) ||
        !IsFinite(value->velocity)) {
        return InvalidArgument("The AudioListener vectors must be finite.");
    }
    outListener->setForwardProperty(ToNative(value->forward));
    outListener->setPositionProperty(ToNative(value->position));
    outListener->setUpProperty(ToNative(value->up));
    outListener->setVelocityProperty(ToNative(value->velocity));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ToNativeEmitter(
    const CNA_AudioEmitter* const value,
    Microsoft::Xna::Framework::Audio::AudioEmitter* const outEmitter)
{
    if (const CNA_Result result =
            ValidateVersionedStructure(value, "The AudioEmitter structure is invalid.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (!std::isfinite(value->doppler_scale)) {
        return InvalidArgument("The AudioEmitter Doppler scale must be finite.");
    }
    if (!IsFinite(value->forward) || !IsFinite(value->position) || !IsFinite(value->up) ||
        !IsFinite(value->velocity)) {
        return InvalidArgument("The AudioEmitter vectors must be finite.");
    }
    outEmitter->setDopplerScaleProperty(value->doppler_scale);
    outEmitter->setForwardProperty(ToNative(value->forward));
    outEmitter->setPositionProperty(ToNative(value->position));
    outEmitter->setUpProperty(ToNative(value->up));
    outEmitter->setVelocityProperty(ToNative(value->velocity));
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
        return InvalidArgument("The owned SoundEffect factory arguments are invalid.");
    }
    const auto resource = std::make_shared<SoundEffectResource>(
        SoundEffectResource{std::move(soundEffect), parentGame, 0U});
    if (const CNA_Result result = CreateHandle(ObjectKind::SoundEffect,
        resource,
        outSoundEffect);
        result != CNA_RESULT_SUCCESS) {
        return result;
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
        if (const CNA_Result result =
                ValidateVersionedStructure(outCapabilities, "The audio-capabilities output structure is invalid.");
            result != CNA_RESULT_SUCCESS) {
            return result;
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
            return InvalidArgument("The SoundEffect output handle is null.");
        }
        *outSoundEffect = CNA_INVALID_HANDLE;

        AudioChannels nativeChannels = AudioChannels::Mono;
        if (!IsVersionedStructure(createInfo) || createInfo->reserved != 0U ||
            createInfo->sample_rate == 0U ||
            createInfo->sample_rate > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
            !TryMapChannels(createInfo->channels, &nativeChannels)) {
            return InvalidArgument("The SoundEffect PCM creation configuration is invalid.");
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
            return InvalidArgument(
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
            return NotSupported(exception.what());
        }

        const auto resource = std::make_shared<SoundEffectResource>(
            SoundEffectResource{nativeSoundEffect, gameHandle, 0U});
        if (const CNA_Result result = CreateHandle(ObjectKind::SoundEffect,
            resource,
            outSoundEffect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        AddOwnedAudioResource();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_create_instance(
    const CNA_Handle soundEffectHandle,
    CNA_Handle* const outInstance)
{
    return CallWithExceptionBarrier([&]() {
        if (outInstance == nullptr) {
            return InvalidArgument("The SoundEffectInstance output handle is null.");
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
        if (const CNA_Result result = CreateHandle(ObjectKind::SoundEffectInstance,
            resource,
            outInstance);
            result != CNA_RESULT_SUCCESS) {
            return result;
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
            return InvalidState(
                "All C SoundEffectInstance children must be destroyed before their SoundEffect.");
        }
        soundEffect->value->Dispose();
        if (const CNA_Result result = ReleaseHandle(soundEffectHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        RemoveOwnedAudioResource();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_effect_instance_get_info(
    const CNA_Handle instanceHandle,
    CNA_SoundEffectInstanceInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() {
        if (const CNA_Result result =
                ValidateVersionedStructure(outInfo, "The SoundEffectInstance info output structure is invalid.");
            result != CNA_RESULT_SUCCESS) {
            return result;
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
            ToCBool(instance->value->getIsLoopedProperty()),
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

CNA_Result cna_sound_effect_instance_set_is_looped(
    const CNA_Handle instanceHandle,
    const CNA_Bool isLooped)
{
    return CallWithExceptionBarrier([&]() {
        if (isLooped != CNA_FALSE && isLooped != CNA_TRUE) {
            return InvalidArgument("The SoundEffectInstance loop flag is invalid.");
        }
        std::shared_ptr<SoundEffectInstanceResource> instance;
        if (const CNA_Result result = GetSoundEffectInstance(instanceHandle, &instance);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        try {
            instance->value->setIsLoopedProperty(isLooped == CNA_TRUE);
        } catch (const System::InvalidOperationException& exception) {
            return InvalidState(exception.what());
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
        if (const CNA_Result result = ReleaseHandle(instanceHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
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

// Every creation route publishes its handle the same way, so the ownership bookkeeping stays in one
// place: the effect is a child of the game and the game refuses to be destroyed while one is alive.
[[nodiscard]] CNA_Result PublishSoundEffect(
    std::shared_ptr<SoundEffect> nativeSoundEffect,
    const CNA_Handle gameHandle,
    CNA_Handle* const outSoundEffect)
{
    const auto resource = std::make_shared<SoundEffectResource>(
        SoundEffectResource{std::move(nativeSoundEffect), gameHandle, 0U});
    if (const CNA_Result result = CreateHandle(ObjectKind::SoundEffect,
        resource,
        outSoundEffect);
        result != CNA_RESULT_SUCCESS) {
        return result;
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
            return InvalidArgument("The SoundEffect output handle is null.");
        }
        *outSoundEffect = CNA_INVALID_HANDLE;
        AudioChannels nativeChannels = AudioChannels::Mono;
        if (!IsVersionedStructure(createInfo) || createInfo->reserved != 0U ||
            createInfo->sample_rate == 0U ||
            createInfo->sample_rate > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
            !TryMapChannels(createInfo->channels, &nativeChannels)) {
            return InvalidArgument("The SoundEffect PCM creation configuration is invalid.");
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
            return InvalidArgument("The SoundEffect PCM range lies outside the buffer.");
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
            return InvalidArgument("The SoundEffect output handle is null.");
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
            return InvalidArgument("The encoded audio data is empty.");
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
            return NotSupported("The encoded audio could not be decoded by this build.");
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
            return InvalidArgument("The SoundEffect output handle is null.");
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

CNA_Result cna_sound_effect_get_sample_duration_ticks(
    const int32_t sizeInBytes,
    const int32_t sampleRate,
    const CNA_AudioChannels channels,
    int64_t* const outTicks)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTicks == nullptr) {
            return InvalidArgument("The sample duration output is null.");
        }
        AudioChannels nativeChannels = AudioChannels::Mono;
        if (!TryMapChannels(channels, &nativeChannels)) {
            return InvalidArgument("The audio channel count is not a defined identity.");
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
            return InvalidArgument("The sample size output is null.");
        }
        AudioChannels nativeChannels = AudioChannels::Mono;
        if (!TryMapChannels(channels, &nativeChannels)) {
            return InvalidArgument("The audio channel count is not a defined identity.");
        }
        *outBytes = static_cast<int32_t>(SoundEffect::GetSampleSizeInBytes(
            System::TimeSpan(static_cast<SharpRuntime::longcs>(durationTicks)),
            static_cast<SharpRuntime::intcs>(sampleRate),
            nativeChannels));
        return CNA_RESULT_SUCCESS;
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
        return InvalidState("This SoundEffectInstance does not stream caller-supplied buffers.");
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
            return InvalidArgument("The streaming instance output handle is null.");
        }
        *outInstance = CNA_INVALID_HANDLE;
        AudioChannels nativeChannels = AudioChannels::Mono;
        if (!TryMapChannels(channels, &nativeChannels)) {
            return InvalidArgument("The audio channel count is not a defined identity.");
        }
        if (sampleRate <= 0) {
            return InvalidArgument("The sample rate must be positive.");
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
        if (const CNA_Result result = CreateHandle(ObjectKind::SoundEffectInstance,
            resource,
            outInstance);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        AddOwnedAudioResource();
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
            return InvalidArgument("The submitted audio range lies outside the buffer.");
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
            return InvalidArgument("The submitted float range lies outside the buffer.");
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

CNA_Result cna_dynamic_sound_effect_instance_subscribe_buffer_needed(
    const CNA_Handle instanceHandle,
    const CNA_AudioEventCallback callback,
    void* const context,
    CNA_Handle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRegistration == nullptr) {
            return InvalidArgument("The audio registration output is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return InvalidArgument("The audio event callback is null.");
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

CNA_Result cna_audio_unsubscribe_ext(const CNA_Handle registration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AudioRegistrationBase> value;
        if (const CNA_Result result = LookupHandle(registration, ObjectKind::AudioEventRegistration, &value);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return ReleaseHandle(registration);
    });
}

CNA_Result cna_microphone_get_default_index_ext(
    const CNA_Handle gameHandle,
    uint64_t* const outIndex,
    CNA_Bool* const outAvailable)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIndex == nullptr || outAvailable == nullptr) {
            return InvalidArgument("The default microphone outputs are invalid.");
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

CNA_Result cna_microphone_get_data_at(
    const CNA_Handle gameHandle,
    const uint64_t index,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
            return InvalidArgument("The capture read output is invalid.");
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
            return InvalidArgument("The capture destination is larger than the canonical limit.");
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

CNA_Result cna_microphone_subscribe_buffer_ready_at(
    const CNA_Handle gameHandle,
    const uint64_t index,
    const CNA_AudioEventCallback callback,
    void* const context,
    CNA_Handle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRegistration == nullptr) {
            return InvalidArgument("The audio registration output is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return InvalidArgument("The audio event callback is null.");
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

CNA_Result cna_microphone_get_type_name_size(
    const CNA_Handle gameHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidArgument("The type-name size output is null.");
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
        return CopyText(typeName, destination, capacity, outBytes);
    });
}

CNA_Result cna_audio_emitter_init(CNA_AudioEmitter* const outEmitter)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEmitter == nullptr) {
            return InvalidArgument("The AudioEmitter output is null.");
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
            return InvalidArgument("The AudioListener output is null.");
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
            return InvalidArgument("The AudioListener array is null.");
        }
        if (listenerCount >
            static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            return InvalidArgument("The AudioListener count is too large.");
        }
        // At least one element, so the pointer handed to the canonical overload is never null even
        // for an empty array: a count of zero reaches the canonical count check and is refused
        // there, not mistaken for a null array. CABI-6: the canonical overload now accepts any
        // count of one or more, matching XNA, which hands its whole listener array to XACT.
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
        // The canonical overload evaluates every listener and applies the dominant one, and the
        // empty array reaches it too, so a count of zero is refused by the canonical rule rather
        // than by an early return invented here.
        instance->value->Apply3D(
            nativeListeners.data(),
            static_cast<int>(listenerCount),
            nativeEmitter);
        return CNA_RESULT_SUCCESS;
    });
}

// The mechanically regular routes of this family, generated from modules/c-api/spec/Audio.toml.
#include "generated/Audio.inc"

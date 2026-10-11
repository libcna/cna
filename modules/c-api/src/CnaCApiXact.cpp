// SPDX-License-Identifier: MS-PL

#include "CNA/C/xact.h"
#include "CnaCApiAudioDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"
#include "CnaCApiOutput.hpp"
#include "CnaCApiMarshal.hpp"

#include "Microsoft/Xna/Framework/Audio/AudioCategory.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEmitter.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioListener.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioStopOptions.hpp"
#include "Microsoft/Xna/Framework/Audio/Cue.hpp"
#include "Microsoft/Xna/Framework/Audio/RendererDetail.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundBank.hpp"
#include "Microsoft/Xna/Framework/Audio/WaveBank.hpp"
#include "System/TimeSpan.hpp"

#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

namespace {

using CNA::C::Detail::AddOwnedAudioResource;
using CNA::C::Detail::AudioRegistration;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CopyText;
using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::InvalidState;
using CNA::C::Detail::IsFinite;
using CNA::C::Detail::IsVersionedStructure;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::PublishAudioRegistration;
using CNA::C::Detail::ReadTextBytes;
using CNA::C::Detail::ReleaseHandle;
using CNA::C::Detail::RemoveOwnedAudioResource;
using CNA::C::Detail::StructureVersion;
using CNA::C::Detail::ToCBool;
using CNA::C::Detail::ToNative;
using CNA::C::Detail::ValidateActiveGameHandle;
using CNA::C::Detail::ValidateVersionedStructure;

using Microsoft::Xna::Framework::Audio::AudioCategory;
using Microsoft::Xna::Framework::Audio::AudioEmitter;
using Microsoft::Xna::Framework::Audio::AudioEngine;
using Microsoft::Xna::Framework::Audio::AudioListener;
using Microsoft::Xna::Framework::Audio::AudioStopOptions;
using Microsoft::Xna::Framework::Audio::Cue;
using Microsoft::Xna::Framework::Audio::SoundBank;
using Microsoft::Xna::Framework::Audio::WaveBank;

// An engine owns banks and categories; a sound bank owns the cues it hands out. Each parent counts
// its live C children so a release cannot leave a handle pointing at a destroyed owner -- the same
// rule sound effects and their instances already follow.
struct EngineResource final {
    std::shared_ptr<AudioEngine> value;
    CNA_Handle parentGame;
    uint64_t childCount;
};

struct CategoryResource final {
    std::shared_ptr<AudioCategory> value;
    std::shared_ptr<EngineResource> parent;
};

struct WaveBankResource final {
    std::shared_ptr<WaveBank> value;
    std::shared_ptr<EngineResource> parent;
};

struct SoundBankResource final {
    std::shared_ptr<SoundBank> value;
    std::shared_ptr<EngineResource> parent;
    uint64_t cueCount;
};

struct CueResource final {
    std::shared_ptr<Cue> value;
    std::shared_ptr<SoundBankResource> parent;
};

template<typename TResource>
[[nodiscard]] CNA_Result GetResource(
    const CNA_Handle handle,
    const ObjectKind kind,
    const char* const message,
    std::shared_ptr<TResource>* const outResource)
{
    const CNA_Result result = GetRuntimeHandles().Get(handle, kind, outResource);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(result, ErrorCategoryForResult(result), message);
}

[[nodiscard]] CNA_Result GetEngine(
    const CNA_Handle handle,
    std::shared_ptr<EngineResource>* const outEngine)
{
    return GetResource(
        handle,
        ObjectKind::AudioEngine,
        "The owned AudioEngine handle is invalid for this call.",
        outEngine);
}

[[nodiscard]] CNA_Result GetCategory(
    const CNA_Handle handle,
    std::shared_ptr<CategoryResource>* const outCategory)
{
    return GetResource(
        handle,
        ObjectKind::AudioCategory,
        "The owned AudioCategory handle is invalid for this call.",
        outCategory);
}

[[nodiscard]] CNA_Result GetWaveBank(
    const CNA_Handle handle,
    std::shared_ptr<WaveBankResource>* const outWaveBank)
{
    return GetResource(
        handle,
        ObjectKind::WaveBank,
        "The owned WaveBank handle is invalid for this call.",
        outWaveBank);
}

[[nodiscard]] CNA_Result GetSoundBank(
    const CNA_Handle handle,
    std::shared_ptr<SoundBankResource>* const outSoundBank)
{
    return GetResource(
        handle,
        ObjectKind::SoundBank,
        "The owned SoundBank handle is invalid for this call.",
        outSoundBank);
}

[[nodiscard]] CNA_Result GetCue(
    const CNA_Handle handle,
    std::shared_ptr<CueResource>* const outCue)
{
    return GetResource(
        handle,
        ObjectKind::Cue,
        "The owned Cue handle is invalid for this call.",
        outCue);
}

[[nodiscard]] bool TryMapStopOptions(
    const CNA_AudioStopOptions options,
    AudioStopOptions* const outOptions) noexcept
{
    switch (options) {
        case CNA_AUDIO_STOP_OPTIONS_AS_AUTHORED:
            *outOptions = AudioStopOptions::AsAuthored;
            return true;
        case CNA_AUDIO_STOP_OPTIONS_IMMEDIATE:
            *outOptions = AudioStopOptions::Immediate;
            return true;
        default:
            return false;
    }
}

// The emitter and the listener are values the caller fills in, so every XACT route that positions
// something validates them exactly the way the sound-effect-instance routes do.
[[nodiscard]] CNA_Result ToNativePosition(
    const CNA_AudioListener* const listener,
    const CNA_AudioEmitter* const emitter,
    AudioListener* const outListener,
    AudioEmitter* const outEmitter)
{
    if (!IsVersionedStructure(listener) || !IsFinite(listener->forward) ||
        !IsFinite(listener->position) || !IsFinite(listener->up) || !IsFinite(listener->velocity)) {
        return InvalidArgument("The AudioListener structure is invalid.");
    }
    if (!IsVersionedStructure(emitter) || !std::isfinite(emitter->doppler_scale) ||
        !IsFinite(emitter->forward) || !IsFinite(emitter->position) || !IsFinite(emitter->up) ||
        !IsFinite(emitter->velocity)) {
        return InvalidArgument("The AudioEmitter structure is invalid.");
    }
    outListener->setForwardProperty(ToNative(listener->forward));
    outListener->setPositionProperty(ToNative(listener->position));
    outListener->setUpProperty(ToNative(listener->up));
    outListener->setVelocityProperty(ToNative(listener->velocity));
    outEmitter->setDopplerScaleProperty(emitter->doppler_scale);
    outEmitter->setForwardProperty(ToNative(emitter->forward));
    outEmitter->setPositionProperty(ToNative(emitter->position));
    outEmitter->setUpProperty(ToNative(emitter->up));
    outEmitter->setVelocityProperty(ToNative(emitter->velocity));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GetRendererDetail(
    const CNA_Handle engineHandle,
    const uint64_t rendererIndex,
    std::shared_ptr<EngineResource>* const outEngine,
    const Microsoft::Xna::Framework::Audio::RendererDetail** const outDetail)
{
    if (const CNA_Result result = GetEngine(engineHandle, outEngine);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const auto& details = (*outEngine)->value->getRendererDetailsProperty();
    if (rendererIndex >= static_cast<uint64_t>(details.size())) {
        return InvalidArgument("The renderer index is outside the engine's renderer list.");
    }
    *outDetail = &details[static_cast<std::size_t>(rendererIndex)];
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CreateEngine(
    const CNA_Handle gameHandle,
    const CNA_StringView settingsFile,
    const bool withRenderer,
    const int64_t lookAheadTicks,
    const CNA_StringView rendererId,
    CNA_Handle* const outEngine)
{
    if (outEngine == nullptr) {
        return InvalidArgument("The AudioEngine output handle is null.");
    }
    *outEngine = CNA_INVALID_HANDLE;
    std::string nativeSettingsFile;
    if (const CNA_Result result =
            ReadTextBytes(settingsFile, "The settings-file path is invalid.", &nativeSettingsFile);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::string nativeRendererId;
    if (withRenderer) {
        if (const CNA_Result result =
                ReadTextBytes(rendererId, "The renderer identifier is invalid.", &nativeRendererId);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
    }
    if (const CNA_Result result = ValidateActiveGameHandle(gameHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }

    std::shared_ptr<AudioEngine> nativeEngine;
    if (withRenderer) {
        nativeEngine = std::make_shared<AudioEngine>(
            nativeSettingsFile,
            System::TimeSpan(lookAheadTicks),
            nativeRendererId);
    } else {
        nativeEngine = std::make_shared<AudioEngine>(nativeSettingsFile);
    }

    const auto resource = std::make_shared<EngineResource>(
        EngineResource{std::move(nativeEngine), gameHandle, 0U});
    if (const CNA_Result result = CreateHandle(ObjectKind::AudioEngine,
        resource,
        outEngine);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    AddOwnedAudioResource();
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result SubscribeDisposing(
    System::EventHandler<System::EventArgs>* const source,
    std::shared_ptr<void> owner,
    const CNA_AudioEventCallback callback,
    void* const context,
    CNA_Handle* const outRegistration)
{
    const auto token = source->Add(
        [callback, context](System::Object*, const System::EventArgs&) { callback(context); });
    return PublishAudioRegistration(
        std::make_shared<AudioRegistration>(std::move(owner), source, token),
        outRegistration);
}

} // namespace

CNA_Result cna_audio_engine_create(
    const CNA_Handle gameHandle,
    const CNA_StringView settingsFile,
    CNA_Handle* const outEngine)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        const CNA_StringView empty = {nullptr, UINT64_C(0)};
        return CreateEngine(gameHandle, settingsFile, false, INT64_C(0), empty, outEngine);
    });
}

CNA_Result cna_audio_engine_destroy(const CNA_Handle engineHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<EngineResource> engine;
        if (const CNA_Result result = GetEngine(engineHandle, &engine);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (engine->childCount != 0U) {
            return InvalidState(
                "All C bank and category children must be destroyed before their AudioEngine.");
        }
        engine->value->Dispose();
        if (const CNA_Result result = ReleaseHandle(engineHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        RemoveOwnedAudioResource();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_audio_engine_renderers_equal(
    const CNA_Handle engineHandle,
    const uint64_t rendererIndex,
    const uint64_t otherRendererIndex,
    CNA_Bool* const outEquals)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEquals == nullptr) {
            return InvalidArgument("The renderer equality output is null.");
        }
        std::shared_ptr<EngineResource> engine;
        const Microsoft::Xna::Framework::Audio::RendererDetail* first = nullptr;
        const Microsoft::Xna::Framework::Audio::RendererDetail* second = nullptr;
        if (const CNA_Result result = GetRendererDetail(engineHandle, rendererIndex, &engine, &first);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result =
                GetRendererDetail(engineHandle, otherRendererIndex, &engine, &second);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The canonical inequality operator is the negation of this one answer, so both operators
        // and the equality method are the same route here.
        *outEquals = ToCBool(*first == *second);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_audio_engine_get_global_variable(
    const CNA_Handle engineHandle,
    const CNA_StringView name,
    float* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The global-variable output is null.");
        }
        std::string nativeName;
        if (const CNA_Result result =
                ReadTextBytes(name, "The global-variable name is invalid.", &nativeName);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<EngineResource> engine;
        if (const CNA_Result result = GetEngine(engineHandle, &engine);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = engine->value->GetGlobalVariable(nativeName);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_audio_engine_set_global_variable(
    const CNA_Handle engineHandle,
    const CNA_StringView name,
    const float value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!std::isfinite(value)) {
            return InvalidArgument("The global-variable value must be finite.");
        }
        std::string nativeName;
        if (const CNA_Result result =
                ReadTextBytes(name, "The global-variable name is invalid.", &nativeName);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<EngineResource> engine;
        if (const CNA_Result result = GetEngine(engineHandle, &engine);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        engine->value->SetGlobalVariable(nativeName, value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_audio_engine_subscribe_disposing_ext(
    const CNA_Handle engineHandle,
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
        std::shared_ptr<EngineResource> engine;
        if (const CNA_Result result = GetEngine(engineHandle, &engine);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return SubscribeDisposing(
            &engine->value->Disposing,
            engine,
            callback,
            context,
            outRegistration);
    });
}

CNA_Result cna_audio_engine_get_category(
    const CNA_Handle engineHandle,
    const CNA_StringView name,
    CNA_Handle* const outCategory)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCategory == nullptr) {
            return InvalidArgument("The AudioCategory output handle is null.");
        }
        *outCategory = CNA_INVALID_HANDLE;
        std::string nativeName;
        if (const CNA_Result result =
                ReadTextBytes(name, "The category name is invalid.", &nativeName);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<EngineResource> engine;
        if (const CNA_Result result = GetEngine(engineHandle, &engine);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto resource = std::make_shared<CategoryResource>(
            CategoryResource{
                std::make_shared<AudioCategory>(engine->value->GetCategory(nativeName)),
                engine});
        if (const CNA_Result result = CreateHandle(ObjectKind::AudioCategory,
            resource,
            outCategory);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        ++engine->childCount;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_audio_category_destroy(const CNA_Handle categoryHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CategoryResource> category;
        if (const CNA_Result result = GetCategory(categoryHandle, &category);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ReleaseHandle(categoryHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        --category->parent->childCount;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_audio_category_stop(
    const CNA_Handle categoryHandle,
    const CNA_AudioStopOptions options)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        AudioStopOptions nativeOptions = AudioStopOptions::AsAuthored;
        if (!TryMapStopOptions(options, &nativeOptions)) {
            return InvalidArgument("The audio stop options are not a defined identity.");
        }
        std::shared_ptr<CategoryResource> category;
        if (const CNA_Result result = GetCategory(categoryHandle, &category);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        category->value->Stop(nativeOptions);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_wave_bank_create(
    const CNA_Handle engineHandle,
    const CNA_StringView filename,
    CNA_Handle* const outWaveBank)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outWaveBank == nullptr) {
            return InvalidArgument("The WaveBank output handle is null.");
        }
        *outWaveBank = CNA_INVALID_HANDLE;
        std::string nativeFilename;
        if (const CNA_Result result =
                ReadTextBytes(filename, "The wave-bank path is invalid.", &nativeFilename);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<EngineResource> engine;
        if (const CNA_Result result = GetEngine(engineHandle, &engine);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto resource = std::make_shared<WaveBankResource>(
            WaveBankResource{
                std::make_shared<WaveBank>(engine->value.get(), nativeFilename),
                engine});
        if (const CNA_Result result = CreateHandle(ObjectKind::WaveBank,
            resource,
            outWaveBank);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        ++engine->childCount;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_wave_bank_create_streaming(
    const CNA_Handle engineHandle,
    const CNA_StringView filename,
    const int32_t offset,
    const int16_t packetSize,
    CNA_Handle* const outWaveBank)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outWaveBank == nullptr) {
            return InvalidArgument("The WaveBank output handle is null.");
        }
        *outWaveBank = CNA_INVALID_HANDLE;
        std::string nativeFilename;
        if (const CNA_Result result =
                ReadTextBytes(filename, "The wave-bank path is invalid.", &nativeFilename);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<EngineResource> engine;
        if (const CNA_Result result = GetEngine(engineHandle, &engine);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto resource = std::make_shared<WaveBankResource>(
            WaveBankResource{
                std::make_shared<WaveBank>(engine->value.get(), nativeFilename, offset, packetSize),
                engine});
        if (const CNA_Result result = CreateHandle(ObjectKind::WaveBank,
            resource,
            outWaveBank);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        ++engine->childCount;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_wave_bank_destroy(const CNA_Handle waveBankHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<WaveBankResource> waveBank;
        if (const CNA_Result result = GetWaveBank(waveBankHandle, &waveBank);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        waveBank->value->Dispose();
        if (const CNA_Result result = ReleaseHandle(waveBankHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        --waveBank->parent->childCount;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_wave_bank_subscribe_disposing_ext(
    const CNA_Handle waveBankHandle,
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
        std::shared_ptr<WaveBankResource> waveBank;
        if (const CNA_Result result = GetWaveBank(waveBankHandle, &waveBank);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return SubscribeDisposing(
            &waveBank->value->Disposing,
            waveBank,
            callback,
            context,
            outRegistration);
    });
}

CNA_Result cna_sound_bank_create(
    const CNA_Handle engineHandle,
    const CNA_StringView filename,
    CNA_Handle* const outSoundBank)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSoundBank == nullptr) {
            return InvalidArgument("The SoundBank output handle is null.");
        }
        *outSoundBank = CNA_INVALID_HANDLE;
        std::string nativeFilename;
        if (const CNA_Result result =
                ReadTextBytes(filename, "The sound-bank path is invalid.", &nativeFilename);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<EngineResource> engine;
        if (const CNA_Result result = GetEngine(engineHandle, &engine);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto resource = std::make_shared<SoundBankResource>(
            SoundBankResource{
                std::make_shared<SoundBank>(engine->value.get(), nativeFilename),
                engine,
                0U});
        if (const CNA_Result result = CreateHandle(ObjectKind::SoundBank,
            resource,
            outSoundBank);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        ++engine->childCount;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_bank_destroy(const CNA_Handle soundBankHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SoundBankResource> soundBank;
        if (const CNA_Result result = GetSoundBank(soundBankHandle, &soundBank);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (soundBank->cueCount != 0U) {
            return InvalidState("All C Cue children must be destroyed before their SoundBank.");
        }
        soundBank->value->Dispose();
        if (const CNA_Result result = ReleaseHandle(soundBankHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        --soundBank->parent->childCount;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_bank_get_cue(
    const CNA_Handle soundBankHandle,
    const CNA_StringView name,
    CNA_Handle* const outCue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCue == nullptr) {
            return InvalidArgument("The Cue output handle is null.");
        }
        *outCue = CNA_INVALID_HANDLE;
        std::string nativeName;
        if (const CNA_Result result = ReadTextBytes(name, "The cue name is invalid.", &nativeName);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<SoundBankResource> soundBank;
        if (const CNA_Result result = GetSoundBank(soundBankHandle, &soundBank);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The canonical lookup hands back a raw cue the caller owns, so the handle takes that
        // ownership straight away rather than borrowing something the bank keeps.
        const auto resource = std::make_shared<CueResource>(
            CueResource{
                std::shared_ptr<Cue>(soundBank->value->GetCue(nativeName)),
                soundBank});
        if (const CNA_Result result = CreateHandle(ObjectKind::Cue, resource, outCue);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        ++soundBank->cueCount;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_bank_play_cue(const CNA_Handle soundBankHandle, const CNA_StringView name)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::string nativeName;
        if (const CNA_Result result = ReadTextBytes(name, "The cue name is invalid.", &nativeName);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<SoundBankResource> soundBank;
        if (const CNA_Result result = GetSoundBank(soundBankHandle, &soundBank);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        soundBank->value->PlayCue(nativeName);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_bank_play_cue_3d(
    const CNA_Handle soundBankHandle,
    const CNA_StringView name,
    const CNA_AudioListener* const listener,
    const CNA_AudioEmitter* const emitter)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::string nativeName;
        if (const CNA_Result result = ReadTextBytes(name, "The cue name is invalid.", &nativeName);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        AudioListener nativeListener;
        AudioEmitter nativeEmitter;
        if (const CNA_Result result =
                ToNativePosition(listener, emitter, &nativeListener, &nativeEmitter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<SoundBankResource> soundBank;
        if (const CNA_Result result = GetSoundBank(soundBankHandle, &soundBank);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        soundBank->value->PlayCue(nativeName, nativeListener, nativeEmitter);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_bank_subscribe_disposing_ext(
    const CNA_Handle soundBankHandle,
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
        std::shared_ptr<SoundBankResource> soundBank;
        if (const CNA_Result result = GetSoundBank(soundBankHandle, &soundBank);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return SubscribeDisposing(
            &soundBank->value->Disposing,
            soundBank,
            callback,
            context,
            outRegistration);
    });
}

CNA_Result cna_cue_destroy(const CNA_Handle cueHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CueResource> cue;
        if (const CNA_Result result = GetCue(cueHandle, &cue);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        cue->value->Dispose();
        if (const CNA_Result result = ReleaseHandle(cueHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        --cue->parent->cueCount;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cue_apply_3d(
    const CNA_Handle cueHandle,
    const CNA_AudioListener* const listener,
    const CNA_AudioEmitter* const emitter)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        AudioListener nativeListener;
        AudioEmitter nativeEmitter;
        if (const CNA_Result result =
                ToNativePosition(listener, emitter, &nativeListener, &nativeEmitter);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<CueResource> cue;
        if (const CNA_Result result = GetCue(cueHandle, &cue);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        cue->value->Apply3D(nativeListener, nativeEmitter);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cue_get_variable(
    const CNA_Handle cueHandle,
    const CNA_StringView name,
    float* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The cue-variable output is null.");
        }
        std::string nativeName;
        if (const CNA_Result result =
                ReadTextBytes(name, "The cue-variable name is invalid.", &nativeName);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<CueResource> cue;
        if (const CNA_Result result = GetCue(cueHandle, &cue);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = cue->value->GetVariable(nativeName);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cue_set_variable(
    const CNA_Handle cueHandle,
    const CNA_StringView name,
    const float value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!std::isfinite(value)) {
            return InvalidArgument("The cue-variable value must be finite.");
        }
        std::string nativeName;
        if (const CNA_Result result =
                ReadTextBytes(name, "The cue-variable name is invalid.", &nativeName);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<CueResource> cue;
        if (const CNA_Result result = GetCue(cueHandle, &cue);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        cue->value->SetVariable(nativeName, value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cue_stop(const CNA_Handle cueHandle, const CNA_AudioStopOptions options)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        AudioStopOptions nativeOptions = AudioStopOptions::AsAuthored;
        if (!TryMapStopOptions(options, &nativeOptions)) {
            return InvalidArgument("The audio stop options are not a defined identity.");
        }
        std::shared_ptr<CueResource> cue;
        if (const CNA_Result result = GetCue(cueHandle, &cue);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        cue->value->Stop(nativeOptions);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cue_subscribe_disposing_ext(
    const CNA_Handle cueHandle,
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
        std::shared_ptr<CueResource> cue;
        if (const CNA_Result result = GetCue(cueHandle, &cue);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return SubscribeDisposing(
            &cue->value->Disposing,
            cue,
            callback,
            context,
            outRegistration);
    });
}

// The mechanically regular routes of this family, generated from modules/c-api/spec/Xact.toml.
#include "generated/Xact.inc"

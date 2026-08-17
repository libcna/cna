// SPDX-License-Identifier: MS-PL

#include "CNA/C/xact.h"
#include "CnaCApiAudioDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

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
using CNA::C::Detail::AudioRegistrationBase;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::PublishAudioRegistration;
using CNA::C::Detail::RemoveOwnedAudioResource;
using CNA::C::Detail::ValidateActiveGameHandle;

using Microsoft::Xna::Framework::Audio::AudioCategory;
using Microsoft::Xna::Framework::Audio::AudioEmitter;
using Microsoft::Xna::Framework::Audio::AudioEngine;
using Microsoft::Xna::Framework::Audio::AudioListener;
using Microsoft::Xna::Framework::Audio::AudioStopOptions;
using Microsoft::Xna::Framework::Audio::Cue;
using Microsoft::Xna::Framework::Audio::SoundBank;
using Microsoft::Xna::Framework::Audio::WaveBank;

constexpr uint32_t StructureVersion = UINT32_C(1);

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

[[nodiscard]] CNA_Result XactInvalidInput(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
}

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

[[nodiscard]] CNA_Result CopyXactText(
    const std::string& value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    if (outBytes == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
        return XactInvalidInput("The XACT text output is invalid.");
    }
    *outBytes = value.size();
    if (capacity < value.size()) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination capacity is smaller than the XACT text.");
    }
    if (!value.empty()) {
        std::memcpy(destination, value.data(), value.size());
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ToNativeText(
    const CNA_StringView view,
    const char* const message,
    std::string* const outText)
{
    if (view.data == nullptr && view.byte_length != UINT64_C(0)) {
        return XactInvalidInput(message);
    }
    outText->assign(view.data == nullptr ? "" : view.data, static_cast<std::size_t>(view.byte_length));
    return CNA_RESULT_SUCCESS;
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

[[nodiscard]] bool IsFinite(const CNA_Vector3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] Microsoft::Xna::Framework::Vector3 ToNativeVector(const CNA_Vector3 value) noexcept
{
    return Microsoft::Xna::Framework::Vector3{value.x, value.y, value.z};
}

// The emitter and the listener are values the caller fills in, so every XACT route that positions
// something validates them exactly the way the sound-effect-instance routes do.
[[nodiscard]] CNA_Result ToNativePosition(
    const CNA_AudioListener* const listener,
    const CNA_AudioEmitter* const emitter,
    AudioListener* const outListener,
    AudioEmitter* const outEmitter)
{
    if (listener == nullptr || listener->struct_size < sizeof(CNA_AudioListener) ||
        listener->struct_version != StructureVersion || !IsFinite(listener->forward) ||
        !IsFinite(listener->position) || !IsFinite(listener->up) || !IsFinite(listener->velocity)) {
        return XactInvalidInput("The AudioListener structure is invalid.");
    }
    if (emitter == nullptr || emitter->struct_size < sizeof(CNA_AudioEmitter) ||
        emitter->struct_version != StructureVersion || !std::isfinite(emitter->doppler_scale) ||
        !IsFinite(emitter->forward) || !IsFinite(emitter->position) || !IsFinite(emitter->up) ||
        !IsFinite(emitter->velocity)) {
        return XactInvalidInput("The AudioEmitter structure is invalid.");
    }
    outListener->setForwardProperty(ToNativeVector(listener->forward));
    outListener->setPositionProperty(ToNativeVector(listener->position));
    outListener->setUpProperty(ToNativeVector(listener->up));
    outListener->setVelocityProperty(ToNativeVector(listener->velocity));
    outEmitter->setDopplerScaleProperty(emitter->doppler_scale);
    outEmitter->setForwardProperty(ToNativeVector(emitter->forward));
    outEmitter->setPositionProperty(ToNativeVector(emitter->position));
    outEmitter->setUpProperty(ToNativeVector(emitter->up));
    outEmitter->setVelocityProperty(ToNativeVector(emitter->velocity));
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
        return XactInvalidInput("The renderer index is outside the engine's renderer list.");
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
        return XactInvalidInput("The AudioEngine output handle is null.");
    }
    *outEngine = CNA_INVALID_HANDLE;
    std::string nativeSettingsFile;
    if (const CNA_Result result =
            ToNativeText(settingsFile, "The settings-file path is invalid.", &nativeSettingsFile);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    std::string nativeRendererId;
    if (withRenderer) {
        if (const CNA_Result result =
                ToNativeText(rendererId, "The renderer identifier is invalid.", &nativeRendererId);
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
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::AudioEngine,
        resource,
        outEngine);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned AudioEngine handle could not be created.");
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

CNA_Result cna_audio_engine_create_with_renderer(
    const CNA_Handle gameHandle,
    const CNA_StringView settingsFile,
    const int64_t lookAheadTicks,
    const CNA_StringView rendererId,
    CNA_Handle* const outEngine)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CreateEngine(gameHandle, settingsFile, true, lookAheadTicks, rendererId, outEngine);
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
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "All C bank and category children must be destroyed before their AudioEngine.");
        }
        engine->value->Dispose();
        const CNA_Result result = GetRuntimeHandles().Release(engineHandle);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned AudioEngine handle could not be released.");
        }
        RemoveOwnedAudioResource();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_audio_engine_get_is_disposed(
    const CNA_Handle engineHandle,
    CNA_Bool* const outIsDisposed)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIsDisposed == nullptr) {
            return XactInvalidInput("The AudioEngine disposal output is null.");
        }
        std::shared_ptr<EngineResource> engine;
        if (const CNA_Result result = GetEngine(engineHandle, &engine);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIsDisposed = engine->value->getIsDisposedProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_audio_engine_get_renderer_count(
    const CNA_Handle engineHandle,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return XactInvalidInput("The renderer-count output is null.");
        }
        std::shared_ptr<EngineResource> engine;
        if (const CNA_Result result = GetEngine(engineHandle, &engine);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<uint64_t>(engine->value->getRendererDetailsProperty().size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_audio_engine_get_renderer_friendly_name_size(
    const CNA_Handle engineHandle,
    const uint64_t rendererIndex,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return XactInvalidInput("The renderer friendly-name size output is null.");
        }
        std::shared_ptr<EngineResource> engine;
        const Microsoft::Xna::Framework::Audio::RendererDetail* detail = nullptr;
        if (const CNA_Result result =
                GetRendererDetail(engineHandle, rendererIndex, &engine, &detail);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = detail->getFriendlyNameProperty().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_audio_engine_copy_renderer_friendly_name(
    const CNA_Handle engineHandle,
    const uint64_t rendererIndex,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<EngineResource> engine;
        const Microsoft::Xna::Framework::Audio::RendererDetail* detail = nullptr;
        if (const CNA_Result result =
                GetRendererDetail(engineHandle, rendererIndex, &engine, &detail);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyXactText(detail->getFriendlyNameProperty(), destination, capacity, outBytes);
    });
}

CNA_Result cna_audio_engine_get_renderer_id_size(
    const CNA_Handle engineHandle,
    const uint64_t rendererIndex,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return XactInvalidInput("The renderer identifier size output is null.");
        }
        std::shared_ptr<EngineResource> engine;
        const Microsoft::Xna::Framework::Audio::RendererDetail* detail = nullptr;
        if (const CNA_Result result =
                GetRendererDetail(engineHandle, rendererIndex, &engine, &detail);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = detail->getRendererIdProperty().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_audio_engine_copy_renderer_id(
    const CNA_Handle engineHandle,
    const uint64_t rendererIndex,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<EngineResource> engine;
        const Microsoft::Xna::Framework::Audio::RendererDetail* detail = nullptr;
        if (const CNA_Result result =
                GetRendererDetail(engineHandle, rendererIndex, &engine, &detail);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyXactText(detail->getRendererIdProperty(), destination, capacity, outBytes);
    });
}

CNA_Result cna_audio_engine_get_renderer_text_size(
    const CNA_Handle engineHandle,
    const uint64_t rendererIndex,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return XactInvalidInput("The renderer text size output is null.");
        }
        std::shared_ptr<EngineResource> engine;
        const Microsoft::Xna::Framework::Audio::RendererDetail* detail = nullptr;
        if (const CNA_Result result =
                GetRendererDetail(engineHandle, rendererIndex, &engine, &detail);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = detail->ToString().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_audio_engine_copy_renderer_text(
    const CNA_Handle engineHandle,
    const uint64_t rendererIndex,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<EngineResource> engine;
        const Microsoft::Xna::Framework::Audio::RendererDetail* detail = nullptr;
        if (const CNA_Result result =
                GetRendererDetail(engineHandle, rendererIndex, &engine, &detail);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyXactText(detail->ToString(), destination, capacity, outBytes);
    });
}

CNA_Result cna_audio_engine_get_renderer_hash_code(
    const CNA_Handle engineHandle,
    const uint64_t rendererIndex,
    int32_t* const outHashCode)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHashCode == nullptr) {
            return XactInvalidInput("The renderer hash-code output is null.");
        }
        std::shared_ptr<EngineResource> engine;
        const Microsoft::Xna::Framework::Audio::RendererDetail* detail = nullptr;
        if (const CNA_Result result =
                GetRendererDetail(engineHandle, rendererIndex, &engine, &detail);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outHashCode = static_cast<int32_t>(detail->GetHashCode());
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
            return XactInvalidInput("The renderer equality output is null.");
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
        *outEquals = (*first == *second) ? CNA_TRUE : CNA_FALSE;
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
            return XactInvalidInput("The global-variable output is null.");
        }
        std::string nativeName;
        if (const CNA_Result result =
                ToNativeText(name, "The global-variable name is invalid.", &nativeName);
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
            return XactInvalidInput("The global-variable value must be finite.");
        }
        std::string nativeName;
        if (const CNA_Result result =
                ToNativeText(name, "The global-variable name is invalid.", &nativeName);
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

CNA_Result cna_audio_engine_update(const CNA_Handle engineHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<EngineResource> engine;
        if (const CNA_Result result = GetEngine(engineHandle, &engine);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        engine->value->Update();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_audio_engine_get_type_name_size(
    const CNA_Handle engineHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return XactInvalidInput("The type-name size output is null.");
        }
        std::shared_ptr<EngineResource> engine;
        if (const CNA_Result result = GetEngine(engineHandle, &engine);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = engine->value->GetTypeName().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_audio_engine_copy_type_name(
    const CNA_Handle engineHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<EngineResource> engine;
        if (const CNA_Result result = GetEngine(engineHandle, &engine);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyXactText(engine->value->GetTypeName(), destination, capacity, outBytes);
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
            return XactInvalidInput("The audio registration output is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return XactInvalidInput("The audio event callback is null.");
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
            return XactInvalidInput("The AudioCategory output handle is null.");
        }
        *outCategory = CNA_INVALID_HANDLE;
        std::string nativeName;
        if (const CNA_Result result =
                ToNativeText(name, "The category name is invalid.", &nativeName);
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
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::AudioCategory,
            resource,
            outCategory);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned AudioCategory handle could not be created.");
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
        const CNA_Result result = GetRuntimeHandles().Release(categoryHandle);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned AudioCategory handle could not be released.");
        }
        --category->parent->childCount;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_audio_category_get_name_size(
    const CNA_Handle categoryHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return XactInvalidInput("The category-name size output is null.");
        }
        std::shared_ptr<CategoryResource> category;
        if (const CNA_Result result = GetCategory(categoryHandle, &category);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = category->value->getNameProperty().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_audio_category_copy_name(
    const CNA_Handle categoryHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CategoryResource> category;
        if (const CNA_Result result = GetCategory(categoryHandle, &category);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyXactText(category->value->getNameProperty(), destination, capacity, outBytes);
    });
}

CNA_Result cna_audio_category_pause(const CNA_Handle categoryHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CategoryResource> category;
        if (const CNA_Result result = GetCategory(categoryHandle, &category);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        category->value->Pause();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_audio_category_resume(const CNA_Handle categoryHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CategoryResource> category;
        if (const CNA_Result result = GetCategory(categoryHandle, &category);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        category->value->Resume();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_audio_category_set_volume(const CNA_Handle categoryHandle, const float volume)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!std::isfinite(volume)) {
            return XactInvalidInput("The category volume must be finite.");
        }
        std::shared_ptr<CategoryResource> category;
        if (const CNA_Result result = GetCategory(categoryHandle, &category);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        category->value->SetVolume(volume);
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
            return XactInvalidInput("The audio stop options are not a defined identity.");
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

CNA_Result cna_audio_category_equals(
    const CNA_Handle categoryHandle,
    const CNA_Handle otherHandle,
    CNA_Bool* const outEquals)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEquals == nullptr) {
            return XactInvalidInput("The category equality output is null.");
        }
        std::shared_ptr<CategoryResource> category;
        std::shared_ptr<CategoryResource> other;
        if (const CNA_Result result = GetCategory(categoryHandle, &category);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = GetCategory(otherHandle, &other);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // Categories compare by name alone, so two categories from two engines can be equal. Both
        // canonical equality operators are this one answer; inequality is its negation.
        *outEquals = (*category->value == *other->value) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_audio_category_get_hash_code(
    const CNA_Handle categoryHandle,
    int32_t* const outHashCode)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHashCode == nullptr) {
            return XactInvalidInput("The category hash-code output is null.");
        }
        std::shared_ptr<CategoryResource> category;
        if (const CNA_Result result = GetCategory(categoryHandle, &category);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outHashCode = static_cast<int32_t>(category->value->GetHashCode());
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
            return XactInvalidInput("The WaveBank output handle is null.");
        }
        *outWaveBank = CNA_INVALID_HANDLE;
        std::string nativeFilename;
        if (const CNA_Result result =
                ToNativeText(filename, "The wave-bank path is invalid.", &nativeFilename);
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
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::WaveBank,
            resource,
            outWaveBank);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned WaveBank handle could not be created.");
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
            return XactInvalidInput("The WaveBank output handle is null.");
        }
        *outWaveBank = CNA_INVALID_HANDLE;
        std::string nativeFilename;
        if (const CNA_Result result =
                ToNativeText(filename, "The wave-bank path is invalid.", &nativeFilename);
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
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::WaveBank,
            resource,
            outWaveBank);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned WaveBank handle could not be created.");
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
        const CNA_Result result = GetRuntimeHandles().Release(waveBankHandle);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned WaveBank handle could not be released.");
        }
        --waveBank->parent->childCount;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_wave_bank_get_is_disposed(
    const CNA_Handle waveBankHandle,
    CNA_Bool* const outIsDisposed)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIsDisposed == nullptr) {
            return XactInvalidInput("The WaveBank disposal output is null.");
        }
        std::shared_ptr<WaveBankResource> waveBank;
        if (const CNA_Result result = GetWaveBank(waveBankHandle, &waveBank);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIsDisposed = waveBank->value->getIsDisposedProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_wave_bank_get_is_prepared(
    const CNA_Handle waveBankHandle,
    CNA_Bool* const outIsPrepared)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIsPrepared == nullptr) {
            return XactInvalidInput("The WaveBank prepared output is null.");
        }
        std::shared_ptr<WaveBankResource> waveBank;
        if (const CNA_Result result = GetWaveBank(waveBankHandle, &waveBank);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIsPrepared = waveBank->value->getIsPreparedProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_wave_bank_get_is_in_use(
    const CNA_Handle waveBankHandle,
    CNA_Bool* const outIsInUse)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIsInUse == nullptr) {
            return XactInvalidInput("The WaveBank in-use output is null.");
        }
        std::shared_ptr<WaveBankResource> waveBank;
        if (const CNA_Result result = GetWaveBank(waveBankHandle, &waveBank);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIsInUse = waveBank->value->getIsInUseProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_wave_bank_get_type_name_size(
    const CNA_Handle waveBankHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return XactInvalidInput("The type-name size output is null.");
        }
        std::shared_ptr<WaveBankResource> waveBank;
        if (const CNA_Result result = GetWaveBank(waveBankHandle, &waveBank);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = waveBank->value->GetTypeName().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_wave_bank_copy_type_name(
    const CNA_Handle waveBankHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<WaveBankResource> waveBank;
        if (const CNA_Result result = GetWaveBank(waveBankHandle, &waveBank);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyXactText(waveBank->value->GetTypeName(), destination, capacity, outBytes);
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
            return XactInvalidInput("The audio registration output is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return XactInvalidInput("The audio event callback is null.");
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
            return XactInvalidInput("The SoundBank output handle is null.");
        }
        *outSoundBank = CNA_INVALID_HANDLE;
        std::string nativeFilename;
        if (const CNA_Result result =
                ToNativeText(filename, "The sound-bank path is invalid.", &nativeFilename);
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
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::SoundBank,
            resource,
            outSoundBank);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned SoundBank handle could not be created.");
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
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "All C Cue children must be destroyed before their SoundBank.");
        }
        soundBank->value->Dispose();
        const CNA_Result result = GetRuntimeHandles().Release(soundBankHandle);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned SoundBank handle could not be released.");
        }
        --soundBank->parent->childCount;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_bank_get_is_disposed(
    const CNA_Handle soundBankHandle,
    CNA_Bool* const outIsDisposed)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIsDisposed == nullptr) {
            return XactInvalidInput("The SoundBank disposal output is null.");
        }
        std::shared_ptr<SoundBankResource> soundBank;
        if (const CNA_Result result = GetSoundBank(soundBankHandle, &soundBank);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIsDisposed = soundBank->value->getIsDisposedProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_bank_get_is_in_use(
    const CNA_Handle soundBankHandle,
    CNA_Bool* const outIsInUse)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIsInUse == nullptr) {
            return XactInvalidInput("The SoundBank in-use output is null.");
        }
        std::shared_ptr<SoundBankResource> soundBank;
        if (const CNA_Result result = GetSoundBank(soundBankHandle, &soundBank);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIsInUse = soundBank->value->getIsInUseProperty() ? CNA_TRUE : CNA_FALSE;
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
            return XactInvalidInput("The Cue output handle is null.");
        }
        *outCue = CNA_INVALID_HANDLE;
        std::string nativeName;
        if (const CNA_Result result = ToNativeText(name, "The cue name is invalid.", &nativeName);
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
        const CNA_Result result = GetRuntimeHandles().Create(ObjectKind::Cue, resource, outCue);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned Cue handle could not be created.");
        }
        ++soundBank->cueCount;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_bank_play_cue(const CNA_Handle soundBankHandle, const CNA_StringView name)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::string nativeName;
        if (const CNA_Result result = ToNativeText(name, "The cue name is invalid.", &nativeName);
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
        if (const CNA_Result result = ToNativeText(name, "The cue name is invalid.", &nativeName);
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

CNA_Result cna_sound_bank_get_type_name_size(
    const CNA_Handle soundBankHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return XactInvalidInput("The type-name size output is null.");
        }
        std::shared_ptr<SoundBankResource> soundBank;
        if (const CNA_Result result = GetSoundBank(soundBankHandle, &soundBank);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = soundBank->value->GetTypeName().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sound_bank_copy_type_name(
    const CNA_Handle soundBankHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SoundBankResource> soundBank;
        if (const CNA_Result result = GetSoundBank(soundBankHandle, &soundBank);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyXactText(soundBank->value->GetTypeName(), destination, capacity, outBytes);
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
            return XactInvalidInput("The audio registration output is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return XactInvalidInput("The audio event callback is null.");
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
        const CNA_Result result = GetRuntimeHandles().Release(cueHandle);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned Cue handle could not be released.");
        }
        --cue->parent->cueCount;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cue_get_info(const CNA_Handle cueHandle, CNA_CueInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr || outInfo->struct_size < sizeof(CNA_CueInfo) ||
            outInfo->struct_version != StructureVersion) {
            return XactInvalidInput("The Cue info output structure is invalid.");
        }
        std::shared_ptr<CueResource> cue;
        if (const CNA_Result result = GetCue(cueHandle, &cue);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_CueInfo info = {
            sizeof(CNA_CueInfo),
            StructureVersion,
            cue->value->getIsCreatedProperty() ? CNA_TRUE : CNA_FALSE,
            cue->value->getIsDisposedProperty() ? CNA_TRUE : CNA_FALSE,
            cue->value->getIsPausedProperty() ? CNA_TRUE : CNA_FALSE,
            cue->value->getIsPlayingProperty() ? CNA_TRUE : CNA_FALSE,
            cue->value->getIsPreparedProperty() ? CNA_TRUE : CNA_FALSE,
            cue->value->getIsPreparingProperty() ? CNA_TRUE : CNA_FALSE,
            cue->value->getIsStoppedProperty() ? CNA_TRUE : CNA_FALSE,
            cue->value->getIsStoppingProperty() ? CNA_TRUE : CNA_FALSE
        };
        *outInfo = info;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cue_get_name_size(const CNA_Handle cueHandle, uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return XactInvalidInput("The cue-name size output is null.");
        }
        std::shared_ptr<CueResource> cue;
        if (const CNA_Result result = GetCue(cueHandle, &cue);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = cue->value->getNameProperty().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cue_copy_name(
    const CNA_Handle cueHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CueResource> cue;
        if (const CNA_Result result = GetCue(cueHandle, &cue);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyXactText(cue->value->getNameProperty(), destination, capacity, outBytes);
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
            return XactInvalidInput("The cue-variable output is null.");
        }
        std::string nativeName;
        if (const CNA_Result result =
                ToNativeText(name, "The cue-variable name is invalid.", &nativeName);
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
            return XactInvalidInput("The cue-variable value must be finite.");
        }
        std::string nativeName;
        if (const CNA_Result result =
                ToNativeText(name, "The cue-variable name is invalid.", &nativeName);
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

CNA_Result cna_cue_play(const CNA_Handle cueHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CueResource> cue;
        if (const CNA_Result result = GetCue(cueHandle, &cue);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        cue->value->Play();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cue_pause(const CNA_Handle cueHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CueResource> cue;
        if (const CNA_Result result = GetCue(cueHandle, &cue);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        cue->value->Pause();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cue_resume(const CNA_Handle cueHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CueResource> cue;
        if (const CNA_Result result = GetCue(cueHandle, &cue);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        cue->value->Resume();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cue_stop(const CNA_Handle cueHandle, const CNA_AudioStopOptions options)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        AudioStopOptions nativeOptions = AudioStopOptions::AsAuthored;
        if (!TryMapStopOptions(options, &nativeOptions)) {
            return XactInvalidInput("The audio stop options are not a defined identity.");
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

CNA_Result cna_cue_get_type_name_size(const CNA_Handle cueHandle, uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return XactInvalidInput("The type-name size output is null.");
        }
        std::shared_ptr<CueResource> cue;
        if (const CNA_Result result = GetCue(cueHandle, &cue);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = cue->value->GetTypeName().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cue_copy_type_name(
    const CNA_Handle cueHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CueResource> cue;
        if (const CNA_Result result = GetCue(cueHandle, &cue);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyXactText(cue->value->GetTypeName(), destination, capacity, outBytes);
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
            return XactInvalidInput("The audio registration output is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return XactInvalidInput("The audio event callback is null.");
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

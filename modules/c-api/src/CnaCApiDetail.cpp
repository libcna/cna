// SPDX-License-Identifier: MS-PL

#include "CnaCApiDetail.hpp"

#include "System/ArgumentException.hpp"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>
#include <typeinfo>
#include <vector>

#if defined(__GNUG__)
#include <cxxabi.h>
#include <cstdlib>
#include <memory>
#endif

namespace CNA::C::Detail {

namespace {


thread_local LastError lastError;

struct PendingException {
    bool noted = false;
    std::string type;
    std::string paramName;
};

thread_local PendingException pendingException;

thread_local bool foreignThreadRefusal = false;

struct ForeignCall final {
    CNA_Result (*run)(void*) = nullptr;
    void* context = nullptr;
    CNA_Result result = CNA_RESULT_INTERNAL;
    LastError error;
    bool done = false;
};

// One game exists at a time, so one queue serves it. Never destroyed: a worker may still be waiting
// in it while static destructors run at exit.
struct ForeignThreadCallQueue final {
    std::mutex mutex;
    std::condition_variable arrived;
    std::condition_variable finished;
    std::deque<ForeignCall*> calls;
    std::thread::id gameThread;
    bool enabled = false;
};

[[nodiscard]] ForeignThreadCallQueue& ForeignCalls() noexcept
{
    static auto* const queue = new ForeignThreadCallQueue();
    return *queue;
}

/**
 * The exception's canonical .NET type name. CNA's C++ namespaces are the .NET ones
 * (`System::IO::IOException`, `Microsoft::Xna::Framework::Content::ContentLoadException`), so the
 * dynamic type, demangled and with `::` read as `.`, is the name a managed consumer throws.
 */
[[nodiscard]] std::string CanonicalTypeName(const std::exception& exception)
{
    const char* raw = typeid(exception).name();
    std::string name;
#if defined(__GNUG__)
    int status = 0;
    const std::unique_ptr<char, void (*)(void*)> demangled(
        abi::__cxa_demangle(raw, nullptr, nullptr, &status), std::free);
    name = status == 0 && demangled ? demangled.get() : raw;
#else
    name = raw;
    for (const std::string_view prefix : {std::string_view("class "), std::string_view("struct ")}) {
        if (name.rfind(prefix, 0) == 0) {
            name.erase(0, prefix.size());
        }
    }
#endif
    for (std::size_t at = name.find("::"); at != std::string::npos; at = name.find("::", at)) {
        name.replace(at, 2, ".");
    }
    return name;
}

[[nodiscard]] uint32_t NextGeneration(const uint32_t current) noexcept
{
    return current == std::numeric_limits<uint32_t>::max() ? 1U : current + 1U;
}

[[nodiscard]] CNA_Handle MakeHandle(const uint32_t slotIndex, const uint32_t generation) noexcept
{
    return (static_cast<uint64_t>(generation) << 32U) |
        static_cast<uint64_t>(slotIndex + 1U);
}

} // namespace

std::string_view InvalidHandleMessage(const ObjectKind kind) noexcept
{
    switch (kind) {
        case ObjectKind::Unknown: return "The Unknown handle is invalid for this call.";
        case ObjectKind::Runtime: return "The Runtime handle is invalid for this call.";
        case ObjectKind::Game: return "The Game handle is invalid for this call.";
        case ObjectKind::GraphicsDevice: return "The GraphicsDevice handle is invalid for this call.";
        case ObjectKind::Texture2D: return "The Texture2D handle is invalid for this call.";
        case ObjectKind::SpriteBatch: return "The SpriteBatch handle is invalid for this call.";
        case ObjectKind::EventRegistration: return "The EventRegistration handle is invalid for this call.";
        case ObjectKind::ContentManager: return "The ContentManager handle is invalid for this call.";
        case ObjectKind::SoundEffect: return "The SoundEffect handle is invalid for this call.";
        case ObjectKind::SoundEffectInstance: return "The SoundEffectInstance handle is invalid for this call.";
        case ObjectKind::RenderTarget2D: return "The RenderTarget2D handle is invalid for this call.";
        case ObjectKind::RenderTargetCube: return "The RenderTargetCube handle is invalid for this call.";
        case ObjectKind::SpriteFont: return "The SpriteFont handle is invalid for this call.";
        case ObjectKind::CurveKeyCollection: return "The CurveKeyCollection handle is invalid for this call.";
        case ObjectKind::Curve: return "The Curve handle is invalid for this call.";
        case ObjectKind::VertexDeclaration: return "The VertexDeclaration handle is invalid for this call.";
        case ObjectKind::Texture3D: return "The Texture3D handle is invalid for this call.";
        case ObjectKind::TextureCube: return "The TextureCube handle is invalid for this call.";
        case ObjectKind::VertexBuffer: return "The VertexBuffer handle is invalid for this call.";
        case ObjectKind::VertexBufferEventRegistration: return "The VertexBufferEventRegistration handle is invalid for this call.";
        case ObjectKind::IndexBuffer: return "The IndexBuffer handle is invalid for this call.";
        case ObjectKind::IndexBufferEventRegistration: return "The IndexBufferEventRegistration handle is invalid for this call.";
        case ObjectKind::EffectAnnotation: return "The EffectAnnotation handle is invalid for this call.";
        case ObjectKind::EffectAnnotationCollection: return "The EffectAnnotationCollection handle is invalid for this call.";
        case ObjectKind::EffectParameter: return "The EffectParameter handle is invalid for this call.";
        case ObjectKind::EffectParameterCollection: return "The EffectParameterCollection handle is invalid for this call.";
        case ObjectKind::EffectPass: return "The EffectPass handle is invalid for this call.";
        case ObjectKind::EffectPassCollection: return "The EffectPassCollection handle is invalid for this call.";
        case ObjectKind::EffectTechnique: return "The EffectTechnique handle is invalid for this call.";
        case ObjectKind::EffectTechniqueCollection: return "The EffectTechniqueCollection handle is invalid for this call.";
        case ObjectKind::Effect: return "The Effect handle is invalid for this call.";
        case ObjectKind::DirectionalLight: return "The DirectionalLight handle is invalid for this call.";
        case ObjectKind::ModelBone: return "The ModelBone handle is invalid for this call.";
        case ObjectKind::ModelBoneCollection: return "The ModelBoneCollection handle is invalid for this call.";
        case ObjectKind::ModelMeshPart: return "The ModelMeshPart handle is invalid for this call.";
        case ObjectKind::ModelMeshPartCollection: return "The ModelMeshPartCollection handle is invalid for this call.";
        case ObjectKind::ModelMesh: return "The ModelMesh handle is invalid for this call.";
        case ObjectKind::ModelMeshCollection: return "The ModelMeshCollection handle is invalid for this call.";
        case ObjectKind::ModelEffectCollection: return "The ModelEffectCollection handle is invalid for this call.";
        case ObjectKind::Model: return "The Model handle is invalid for this call.";
        case ObjectKind::MorphTargetDataEXT: return "The MorphTargetDataEXT handle is invalid for this call.";
        case ObjectKind::SkinnedModelEXT: return "The SkinnedModelEXT handle is invalid for this call.";
        case ObjectKind::SkinningData: return "The SkinningData handle is invalid for this call.";
        case ObjectKind::AnimationPlayer: return "The AnimationPlayer handle is invalid for this call.";
        case ObjectKind::GraphicsDeviceEventRegistration: return "The GraphicsDeviceEventRegistration handle is invalid for this call.";
        case ObjectKind::OcclusionQuery: return "The OcclusionQuery handle is invalid for this call.";
        case ObjectKind::AsciiPostProcessEffect: return "The AsciiPostProcessEffect handle is invalid for this call.";
        case ObjectKind::StorageDevice: return "The StorageDevice handle is invalid for this call.";
        case ObjectKind::StorageContainer: return "The StorageContainer handle is invalid for this call.";
        case ObjectKind::StorageStream: return "The StorageStream handle is invalid for this call.";
        case ObjectKind::StorageDeviceEventRegistration: return "The StorageDeviceEventRegistration handle is invalid for this call.";
        case ObjectKind::StorageContainerEventRegistration: return "The StorageContainerEventRegistration handle is invalid for this call.";
        case ObjectKind::ContentReader: return "The ContentReader handle is invalid for this call.";
        case ObjectKind::ContentTypeReader: return "The ContentTypeReader handle is invalid for this call.";
        case ObjectKind::NetworkSessionProperties: return "The NetworkSessionProperties handle is invalid for this call.";
        case ObjectKind::NetworkSessionPropertyEnumerator: return "The NetworkSessionPropertyEnumerator handle is invalid for this call.";
        case ObjectKind::PacketWriter: return "The PacketWriter handle is invalid for this call.";
        case ObjectKind::PacketReader: return "The PacketReader handle is invalid for this call.";
        case ObjectKind::NetworkGamer: return "The NetworkGamer handle is invalid for this call.";
        case ObjectKind::NetworkMachine: return "The NetworkMachine handle is invalid for this call.";
        case ObjectKind::AvailableNetworkSession: return "The AvailableNetworkSession handle is invalid for this call.";
        case ObjectKind::AvailableNetworkSessionCollection: return "The AvailableNetworkSessionCollection handle is invalid for this call.";
        case ObjectKind::NetworkSession: return "The NetworkSession handle is invalid for this call.";
        case ObjectKind::SignedInGamer: return "The SignedInGamer handle is invalid for this call.";
        case ObjectKind::NetworkSessionEventRegistration: return "The NetworkSessionEventRegistration handle is invalid for this call.";
        case ObjectKind::MouseEventRegistration: return "The MouseEventRegistration handle is invalid for this call.";
        case ObjectKind::MouseCursor: return "The MouseCursor handle is invalid for this call.";
        case ObjectKind::TextInputEventRegistration: return "The TextInputEventRegistration handle is invalid for this call.";
        case ObjectKind::HapticDevice: return "The HapticDevice handle is invalid for this call.";
        case ObjectKind::JoystickState: return "The JoystickState handle is invalid for this call.";
        case ObjectKind::JoystickEventRegistration: return "The JoystickEventRegistration handle is invalid for this call.";
        case ObjectKind::InputDeviceEventRegistration: return "The InputDeviceEventRegistration handle is invalid for this call.";
        case ObjectKind::Song: return "The Song handle is invalid for this call.";
        case ObjectKind::SongCollection: return "The SongCollection handle is invalid for this call.";
        case ObjectKind::MediaLibrary: return "The MediaLibrary handle is invalid for this call.";
        case ObjectKind::Album: return "The Album handle is invalid for this call.";
        case ObjectKind::AlbumCollection: return "The AlbumCollection handle is invalid for this call.";
        case ObjectKind::Artist: return "The Artist handle is invalid for this call.";
        case ObjectKind::ArtistCollection: return "The ArtistCollection handle is invalid for this call.";
        case ObjectKind::Genre: return "The Genre handle is invalid for this call.";
        case ObjectKind::GenreCollection: return "The GenreCollection handle is invalid for this call.";
        case ObjectKind::Playlist: return "The Playlist handle is invalid for this call.";
        case ObjectKind::PlaylistCollection: return "The PlaylistCollection handle is invalid for this call.";
        case ObjectKind::Picture: return "The Picture handle is invalid for this call.";
        case ObjectKind::PictureCollection: return "The PictureCollection handle is invalid for this call.";
        case ObjectKind::PictureAlbum: return "The PictureAlbum handle is invalid for this call.";
        case ObjectKind::PictureAlbumCollection: return "The PictureAlbumCollection handle is invalid for this call.";
        case ObjectKind::MediaQueue: return "The MediaQueue handle is invalid for this call.";
        case ObjectKind::MediaPlayerEventRegistration: return "The MediaPlayerEventRegistration handle is invalid for this call.";
        case ObjectKind::Accelerometer: return "The Accelerometer handle is invalid for this call.";
        case ObjectKind::Gyroscope: return "The Gyroscope handle is invalid for this call.";
        case ObjectKind::SensorEventRegistration: return "The SensorEventRegistration handle is invalid for this call.";
        case ObjectKind::Compass: return "The Compass handle is invalid for this call.";
        case ObjectKind::Motion: return "The Motion handle is invalid for this call.";
        case ObjectKind::SystemTray: return "The SystemTray handle is invalid for this call.";
        case ObjectKind::Camera: return "The Camera handle is invalid for this call.";
        case ObjectKind::GameComponent: return "The GameComponent handle is invalid for this call.";
        case ObjectKind::GameComponentEventRegistration: return "The GameComponentEventRegistration handle is invalid for this call.";
        case ObjectKind::GameEventRegistration: return "The GameEventRegistration handle is invalid for this call.";
        case ObjectKind::GraphicsDeviceManager: return "The GraphicsDeviceManager handle is invalid for this call.";
        case ObjectKind::AudioEventRegistration: return "The AudioEventRegistration handle is invalid for this call.";
        case ObjectKind::Video: return "The Video handle is invalid for this call.";
        case ObjectKind::VideoPlayer: return "The VideoPlayer handle is invalid for this call.";
        case ObjectKind::AudioEngine: return "The AudioEngine handle is invalid for this call.";
        case ObjectKind::AudioCategory: return "The AudioCategory handle is invalid for this call.";
        case ObjectKind::WaveBank: return "The WaveBank handle is invalid for this call.";
        case ObjectKind::SoundBank: return "The SoundBank handle is invalid for this call.";
        case ObjectKind::Cue: return "The Cue handle is invalid for this call.";
        case ObjectKind::Gamer: return "The Gamer handle is invalid for this call.";
        case ObjectKind::GamerProfile: return "The GamerProfile handle is invalid for this call.";
        case ObjectKind::GamerCollection: return "The GamerCollection handle is invalid for this call.";
        case ObjectKind::GamerEnumerator: return "The GamerEnumerator handle is invalid for this call.";
        case ObjectKind::GamerEventRegistration: return "The GamerEventRegistration handle is invalid for this call.";
        case ObjectKind::Achievement: return "The Achievement handle is invalid for this call.";
        case ObjectKind::AchievementCollection: return "The AchievementCollection handle is invalid for this call.";
        case ObjectKind::PropertyDictionary: return "The PropertyDictionary handle is invalid for this call.";
        case ObjectKind::LeaderboardReader: return "The LeaderboardReader handle is invalid for this call.";
        case ObjectKind::LeaderboardEntry: return "The LeaderboardEntry handle is invalid for this call.";
        case ObjectKind::AvatarDescription: return "The AvatarDescription handle is invalid for this call.";
        case ObjectKind::AvatarAnimation: return "The AvatarAnimation handle is invalid for this call.";
        case ObjectKind::AvatarRenderer: return "The AvatarRenderer handle is invalid for this call.";
        case ObjectKind::ModelAnimationsEXT: return "The ModelAnimationsEXT handle is invalid for this call.";
        case ObjectKind::ContentTypeReaderRegistration: return "The ContentTypeReaderRegistration handle is invalid for this call.";
        case ObjectKind::OwnedGraphicsDevice: return "The OwnedGraphicsDevice handle is invalid for this call.";
        case ObjectKind::RenderTargetEventRegistration: return "The RenderTargetEventRegistration handle is invalid for this call.";
        case ObjectKind::DebugDraw: return "The DebugDraw handle is invalid for this call.";
        case ObjectKind::CnbDocument: return "The CnbDocument handle is invalid for this call.";
        case ObjectKind::CnbReader: return "The CnbReader handle is invalid for this call.";
        case ObjectKind::CnbByteWriter: return "The CnbByteWriter handle is invalid for this call.";
        case ObjectKind::CnbWriter: return "The CnbWriter handle is invalid for this call.";
        case ObjectKind::CnbTextureData: return "The CnbTextureData handle is invalid for this call.";
        case ObjectKind::CnbModelData: return "The CnbModelData handle is invalid for this call.";
        case ObjectKind::CnbModelFromCnj: return "The CnbModelFromCnj handle is invalid for this call.";
        case ObjectKind::CnbSpriteFontData: return "The CnbSpriteFontData handle is invalid for this call.";
        case ObjectKind::CnbSoundEffectData: return "The CnbSoundEffectData handle is invalid for this call.";
        case ObjectKind::CnbAnimationClip: return "The CnbAnimationClip handle is invalid for this call.";
        case ObjectKind::CnbLoader: return "The CnbLoader handle is invalid for this call.";
        case ObjectKind::CnjToCnbResult: return "The CnjToCnbResult handle is invalid for this call.";
        case ObjectKind::ReflectiveTypeReaderBuilder: return "The ReflectiveTypeReaderBuilder handle is invalid for this call.";
        case ObjectKind::ObjectDictionaryEXT: return "The ObjectDictionaryEXT handle is invalid for this call.";
        case ObjectKind::Test: return "The Test handle is invalid for this call.";
    }
    return "The handle is invalid for this call.";
}

CNA_ErrorCategory ErrorCategoryForResult(const CNA_Result result) noexcept
{
    switch (result) {
        case CNA_RESULT_SUCCESS:
            return CNA_ERROR_CATEGORY_NONE;
        case CNA_RESULT_INVALID_ARGUMENT:
            return CNA_ERROR_CATEGORY_ARGUMENT;
        case CNA_RESULT_INVALID_HANDLE:
            return CNA_ERROR_CATEGORY_HANDLE;
        case CNA_RESULT_INVALID_STATE:
            return CNA_ERROR_CATEGORY_STATE;
        case CNA_RESULT_OUT_OF_MEMORY:
            return CNA_ERROR_CATEGORY_MEMORY;
        case CNA_RESULT_IO:
            return CNA_ERROR_CATEGORY_IO;
        case CNA_RESULT_NOT_SUPPORTED:
            return CNA_ERROR_CATEGORY_NOT_SUPPORTED;
        case CNA_RESULT_PLATFORM:
            return CNA_ERROR_CATEGORY_PLATFORM;
        case CNA_RESULT_THREAD:
            return CNA_ERROR_CATEGORY_THREAD;
        case CNA_RESULT_CALLBACK:
            return CNA_ERROR_CATEGORY_CALLBACK;
        case CNA_RESULT_OVERFLOW:
        case CNA_RESULT_BUFFER_TOO_SMALL:
            return CNA_ERROR_CATEGORY_RANGE;
        case CNA_RESULT_ENCODING:
            return CNA_ERROR_CATEGORY_ENCODING;
        case CNA_RESULT_SHUTTING_DOWN:
            return CNA_ERROR_CATEGORY_SHUTTING_DOWN;
        default:
            return CNA_ERROR_CATEGORY_INTERNAL;
    }
}

const LastError& GetLastError() noexcept
{
    return lastError;
}

void SetLastJoinError(const uint32_t joinError) noexcept
{
    lastError.hasJoinError = true;
    lastError.joinError = joinError;
}

void SetLastSensorErrorId(const int32_t sensorErrorId) noexcept
{
    lastError.hasSensorErrorId = true;
    lastError.sensorErrorId = sensorErrorId;
}

void NoteTranslatedException(const std::exception& exception) noexcept
{
    try {
        pendingException.type = CanonicalTypeName(exception);
        const auto* const argument = dynamic_cast<const System::ArgumentException*>(&exception);
        pendingException.paramName = argument != nullptr ? argument->getParamNameProperty() : std::string();
        pendingException.noted = true;
    } catch (...) {
        pendingException = PendingException{};
    }
}

void NoteForeignThreadRefusal() noexcept
{
    foreignThreadRefusal = true;
}

void ClearForeignThreadRefusal() noexcept
{
    foreignThreadRefusal = false;
}

bool TakeForeignThreadRefusal() noexcept
{
    const bool refused = foreignThreadRefusal;
    foreignThreadRefusal = false;
    return refused;
}

bool ShouldServeOnGameThread() noexcept
{
    ForeignThreadCallQueue& queue = ForeignCalls();
    std::lock_guard lock(queue.mutex);
    return queue.enabled && queue.gameThread != std::this_thread::get_id();
}

CNA_Result ServeOnGameThread(CNA_Result (*const run)(void*), void* const context) noexcept
{
    ForeignThreadCallQueue& queue = ForeignCalls();
    ForeignCall call;
    call.run = run;
    call.context = context;
    {
        std::unique_lock lock(queue.mutex);
        if (!queue.enabled || queue.gameThread == std::this_thread::get_id()) {
            return CNA_RESULT_THREAD;
        }
        try {
            queue.calls.push_back(&call);
        } catch (...) {
            return Fail(CNA_RESULT_OUT_OF_MEMORY, CNA_ERROR_CATEGORY_MEMORY, "Native allocation failed.");
        }
        queue.arrived.notify_all();
        queue.finished.wait(lock, [&call] { return call.done; });
    }

    try {
        lastError = call.result == CNA_RESULT_SUCCESS ? LastError{} : std::move(call.error);
    } catch (...) {
        lastError = LastError{};
        lastError.result = call.result;
        lastError.category = ErrorCategoryForResult(call.result);
    }
    return call.result;
}

void SetForeignThreadCalls(const bool enabled) noexcept
{
    if (!enabled) {
        EndForeignThreadCalls();
        return;
    }
    ForeignThreadCallQueue& queue = ForeignCalls();
    std::lock_guard lock(queue.mutex);
    queue.gameThread = std::this_thread::get_id();
    queue.enabled = true;
}

void RunForeignThreadCalls() noexcept
{
    ForeignThreadCallQueue& queue = ForeignCalls();
    std::unique_lock lock(queue.mutex);
    if (!queue.enabled || queue.gameThread != std::this_thread::get_id()) {
        return;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(8);
    bool served = false;
    while (true) {
        if (queue.calls.empty()) {
            if (!served
                || !queue.arrived.wait_until(lock, deadline, [&queue] { return !queue.calls.empty(); })) {
                return;
            }
        }

        ForeignCall* const call = queue.calls.front();
        queue.calls.pop_front();
        lock.unlock();
        call->result = call->run(call->context);
        if (call->result != CNA_RESULT_SUCCESS) {
            try {
                call->error = GetLastError();
            } catch (...) {
                call->error.result = call->result;
            }
        }
        lock.lock();
        call->done = true;
        queue.finished.notify_all();
        served = true;
        if (std::chrono::steady_clock::now() >= deadline) {
            return;
        }
    }
}

void EndForeignThreadCalls() noexcept
{
    ForeignThreadCallQueue& queue = ForeignCalls();
    std::lock_guard lock(queue.mutex);
    queue.enabled = false;
    for (ForeignCall* const call : queue.calls) {
        call->result = CNA_RESULT_INVALID_STATE;
        call->error.result = CNA_RESULT_INVALID_STATE;
        call->error.category = CNA_ERROR_CATEGORY_STATE;
        try {
            call->error.message = "The game ended before its thread could run this call.";
        } catch (...) {
        }
        call->done = true;
    }
    queue.calls.clear();
    queue.finished.notify_all();
}

void SetLastError(
    const CNA_Result result,
    const CNA_ErrorCategory category,
    const std::string_view message) noexcept
{
    lastError.result = result;
    lastError.category = category;
    lastError.hasJoinError = false;
    lastError.joinError = 0U;
    lastError.hasSensorErrorId = false;
    lastError.sensorErrorId = 0;
    if (pendingException.noted) {
        lastError.exceptionType.swap(pendingException.type);
        lastError.exceptionParamName.swap(pendingException.paramName);
        pendingException = PendingException{};
    } else {
        lastError.exceptionType.clear();
        lastError.exceptionParamName.clear();
    }
    try {
        if (message.empty()) {
            lastError.message.clear();
        } else {
            lastError.message.assign(message.data(), message.size());
        }
    } catch (...) {
        lastError.message.clear();
    }
}

CNA_Result Fail(
    const CNA_Result result,
    const CNA_ErrorCategory category,
    const std::string_view message) noexcept
{
    SetLastError(result, category, message);
    return result;
}

CNA_Result ValidateCanonicalBool(const CNA_Bool value, const std::string_view name) noexcept
{
    if (IsCanonicalBool(value)) {
        return CNA_RESULT_SUCCESS;
    }
    // The name is in the message because a route may take more than one flag, and "a flag is not a
    // Boolean" is not something a caller can act on without knowing which.
    std::string message;
    message.reserve(name.size() + 48U);
    message.append("The '").append(name).append("' flag must be CNA_FALSE or CNA_TRUE.");
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
}

CNA_Result ValidateStringView(
    const CNA_StringView value,
    const bool rejectEmbeddedNul) noexcept
{
    if (value.data == nullptr) {
        return value.byte_length == 0U ? CNA_RESULT_SUCCESS : CNA_RESULT_INVALID_ARGUMENT;
    }

    const auto* bytes = reinterpret_cast<const unsigned char*>(value.data);
    for (uint64_t index = 0U; index < value.byte_length;) {
        const unsigned char first = bytes[index];
        if (first <= 0x7FU) {
            if (rejectEmbeddedNul && first == 0U) {
                return CNA_RESULT_ENCODING;
            }
            ++index;
            continue;
        }

        uint64_t continuationCount = 0U;
        if (first >= 0xC2U && first <= 0xDFU) {
            continuationCount = 1U;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            continuationCount = 2U;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            continuationCount = 3U;
        } else {
            return CNA_RESULT_ENCODING;
        }

        if (continuationCount > value.byte_length - index - 1U) {
            return CNA_RESULT_ENCODING;
        }

        const unsigned char second = bytes[index + 1U];
        if ((second & 0xC0U) != 0x80U ||
            (first == 0xE0U && second < 0xA0U) ||
            (first == 0xEDU && second > 0x9FU) ||
            (first == 0xF0U && second < 0x90U) ||
            (first == 0xF4U && second > 0x8FU)) {
            return CNA_RESULT_ENCODING;
        }

        for (uint64_t continuationIndex = 2U;
             continuationIndex <= continuationCount;
             ++continuationIndex) {
            if ((bytes[index + continuationIndex] & 0xC0U) != 0x80U) {
                return CNA_RESULT_ENCODING;
            }
        }
        index += continuationCount + 1U;
    }

    return CNA_RESULT_SUCCESS;
}

CNA_Result CopyStringView(
    const CNA_StringView value,
    const bool rejectEmbeddedNul,
    std::string* const outValue) noexcept
{
    if (outValue == nullptr) {
        return CNA_RESULT_INVALID_ARGUMENT;
    }

    const CNA_Result validationResult = ValidateStringView(value, rejectEmbeddedNul);
    if (validationResult != CNA_RESULT_SUCCESS) {
        return validationResult;
    }

    try {
        outValue->assign(value.data == nullptr ? "" : value.data, value.byte_length);
    } catch (const std::bad_alloc&) {
        return CNA_RESULT_OUT_OF_MEMORY;
    } catch (...) {
        return CNA_RESULT_INTERNAL;
    }
    return CNA_RESULT_SUCCESS;
}

CNA_Result ReadText(
    const CNA_StringView value,
    const std::string_view message,
    std::string* const outValue) noexcept
{
    if (const CNA_Result result = CopyStringView(value, false, outValue);
        result != CNA_RESULT_SUCCESS) {
        return Fail(result, ErrorCategoryForResult(result), message);
    }
    return CNA_RESULT_SUCCESS;
}

CNA_Result ReadTextBytes(
    const CNA_StringView value,
    const std::string_view message,
    std::string* const outValue)
{
    if (value.data == nullptr && value.byte_length != UINT64_C(0)) {
        return InvalidArgument(message);
    }
    outValue->assign(
        value.data == nullptr ? "" : value.data,
        static_cast<std::size_t>(value.byte_length));
    return CNA_RESULT_SUCCESS;
}

CNA_Result ValidateBuffer(const void* const data, const uint64_t count) noexcept
{
    return data == nullptr && count != 0U ? CNA_RESULT_INVALID_ARGUMENT : CNA_RESULT_SUCCESS;
}

CNA_Result CheckedElementByteCount(
    const void* const data,
    const uint64_t elementCount,
    const uint64_t elementByteSize,
    std::size_t* const outByteCount) noexcept
{
    if (outByteCount == nullptr || elementByteSize == 0U) {
        return CNA_RESULT_INVALID_ARGUMENT;
    }
    if (const CNA_Result pointerResult = ValidateBuffer(data, elementCount);
        pointerResult != CNA_RESULT_SUCCESS) {
        return pointerResult;
    }
    if (elementCount > std::numeric_limits<uint64_t>::max() / elementByteSize) {
        return CNA_RESULT_OVERFLOW;
    }

    const uint64_t byteCount = elementCount * elementByteSize;
    if (byteCount > std::numeric_limits<std::size_t>::max()) {
        return CNA_RESULT_OVERFLOW;
    }
    *outByteCount = static_cast<std::size_t>(byteCount);
    return CNA_RESULT_SUCCESS;
}

HandleRegistry::~HandleRegistry()
{
    // Held rather than dropped, so the objects stay reachable and a leak checker does not report
    // what is deliberate; the process is ending.
    static auto* const abandoned = new std::vector<std::shared_ptr<void>>();
    const std::thread::id current = std::this_thread::get_id();
    for (Slot& slot : slots_) {
        if (slot.object != nullptr && slot.creationThread != current) {
            abandoned->push_back(std::move(slot.object));
        }
    }
}

CNA_Result HandleRegistry::Create(
    const ObjectKind kind,
    std::shared_ptr<void> object,
    CNA_Handle* const outHandle)
{
    if (!object || outHandle == nullptr || kind == ObjectKind::Unknown) {
        return CNA_RESULT_INVALID_ARGUMENT;
    }

    std::lock_guard lock(mutex_);
    for (uint32_t index = 0; index < slots_.size(); ++index) {
        Slot& slot = slots_[index];
        if (slot.object) {
            continue;
        }

        slot.kind = kind;
        slot.object = std::move(object);
        slot.creationThread = std::this_thread::get_id();
        slot.userTag = 0U;
        *outHandle = MakeHandle(index, slot.generation);
        return CNA_RESULT_SUCCESS;
    }

    if (slots_.size() >= std::numeric_limits<uint32_t>::max()) {
        return CNA_RESULT_OVERFLOW;
    }

    slots_.push_back(Slot{
        .generation = 1,
        .kind = kind,
        .object = std::move(object),
        .creationThread = std::this_thread::get_id(),
        .userTag = 0U
    });
    *outHandle = MakeHandle(static_cast<uint32_t>(slots_.size() - 1U), 1U);
    return CNA_RESULT_SUCCESS;
}

CNA_Result HandleRegistry::GetKind(
    const CNA_Handle handle,
    ObjectKind* const outKind) const
{
    if (outKind == nullptr) {
        return CNA_RESULT_INVALID_ARGUMENT;
    }

    std::lock_guard lock(mutex_);
    Slot* slot = nullptr;
    const CNA_Result result = FindSlotLocked(handle, &slot);
    if (result != CNA_RESULT_SUCCESS) {
        return result;
    }

    *outKind = slot->kind;
    return CNA_RESULT_SUCCESS;
}

CNA_Result HandleRegistry::GetUserTag(
    const CNA_Handle handle,
    uint64_t* const outTag) const
{
    if (outTag == nullptr) {
        return CNA_RESULT_INVALID_ARGUMENT;
    }
    std::lock_guard lock(mutex_);
    Slot* slot = nullptr;
    const CNA_Result result = FindSlotLocked(handle, &slot);
    if (result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (slot->creationThread != std::this_thread::get_id()) {
        NoteForeignThreadRefusal();
        return CNA_RESULT_THREAD;
    }
    *outTag = slot->userTag;
    return CNA_RESULT_SUCCESS;
}

CNA_Result HandleRegistry::SetUserTag(
    const CNA_Handle handle,
    const uint64_t tag)
{
    std::lock_guard lock(mutex_);
    Slot* slot = nullptr;
    const CNA_Result result = FindSlotLocked(handle, &slot);
    if (result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (slot->creationThread != std::this_thread::get_id()) {
        NoteForeignThreadRefusal();
        return CNA_RESULT_THREAD;
    }
    slot->userTag = tag;
    return CNA_RESULT_SUCCESS;
}

CNA_Result HandleRegistry::Release(const CNA_Handle handle)
{
    std::shared_ptr<void> releasedObject;
    {
        std::lock_guard lock(mutex_);
        Slot* slot = nullptr;
        const CNA_Result result = FindSlotLocked(handle, &slot);
        if (result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (slot->creationThread != std::this_thread::get_id()) {
            NoteForeignThreadRefusal();
            return CNA_RESULT_THREAD;
        }

        releasedObject = std::move(slot->object);
        slot->kind = ObjectKind::Unknown;
        slot->creationThread = {};
        slot->userTag = 0U;
        slot->generation = NextGeneration(slot->generation);
    }
    return CNA_RESULT_SUCCESS;
}

CNA_Result HandleRegistry::FindSlotLocked(
    const CNA_Handle handle,
    Slot** const outSlot) const
{
    if (handle == CNA_INVALID_HANDLE || outSlot == nullptr) {
        return CNA_RESULT_INVALID_HANDLE;
    }

    const uint32_t encodedSlot = static_cast<uint32_t>(handle);
    const uint32_t generation = static_cast<uint32_t>(handle >> 32U);
    if (encodedSlot == 0U || generation == 0U) {
        return CNA_RESULT_INVALID_HANDLE;
    }

    const uint32_t slotIndex = encodedSlot - 1U;
    if (slotIndex >= slots_.size()) {
        return CNA_RESULT_INVALID_HANDLE;
    }

    Slot& slot = slots_[slotIndex];
    if (!slot.object || slot.generation != generation) {
        return CNA_RESULT_INVALID_HANDLE;
    }

    *outSlot = &slot;
    return CNA_RESULT_SUCCESS;
}

} // namespace CNA::C::Detail

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

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {
    // Set when Windows detaches this library from a terminating process; see DllMain below.
    bool processTerminating = false;

    // Keeps objects the process abandons reachable, so a leak checker does not report what is
    // deliberate.
    void Abandon(std::shared_ptr<void> object)
    {
        static auto* const abandoned = new std::vector<std::shared_ptr<void>>();
        abandoned->push_back(std::move(object));
    }
}

#if defined(_WIN32)
// Windows calls this before it runs the library's atexit handlers and static destructors. When the
// process is terminating (reserved is not null), every other thread is already gone: a device whose
// teardown waits for its own worker threads -- SDL_GPU's Direct3D 12 backend -- would wait forever,
// so the objects a game left alive are abandoned to the operating system, as Windows prescribes for
// that case. A library unloaded with FreeLibrary still destroys them.
extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_DETACH && reserved != nullptr) processTerminating = true;
    return TRUE;
}
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

[[nodiscard]] CNA_Handle MakeHandle(const uint32_t slotIndex, const uint32_t generation) noexcept
{
    return (static_cast<uint64_t>(generation) << 32U) |
        static_cast<uint64_t>(slotIndex + 1U);
}

} // namespace

std::string_view ObjectKindName(const ObjectKind kind) noexcept
{
    switch (kind) {
        case ObjectKind::Unknown: return "Unknown";
        case ObjectKind::Runtime: return "Runtime";
        case ObjectKind::Game: return "Game";
        case ObjectKind::GraphicsDevice: return "GraphicsDevice";
        case ObjectKind::Texture2D: return "Texture2D";
        case ObjectKind::SpriteBatch: return "SpriteBatch";
        case ObjectKind::EventRegistration: return "EventRegistration";
        case ObjectKind::ContentManager: return "ContentManager";
        case ObjectKind::SoundEffect: return "SoundEffect";
        case ObjectKind::SoundEffectInstance: return "SoundEffectInstance";
        case ObjectKind::RenderTarget2D: return "RenderTarget2D";
        case ObjectKind::RenderTargetCube: return "RenderTargetCube";
        case ObjectKind::SpriteFont: return "SpriteFont";
        case ObjectKind::CurveKeyCollection: return "CurveKeyCollection";
        case ObjectKind::Curve: return "Curve";
        case ObjectKind::VertexDeclaration: return "VertexDeclaration";
        case ObjectKind::Texture3D: return "Texture3D";
        case ObjectKind::TextureCube: return "TextureCube";
        case ObjectKind::VertexBuffer: return "VertexBuffer";
        case ObjectKind::VertexBufferEventRegistration: return "VertexBufferEventRegistration";
        case ObjectKind::IndexBuffer: return "IndexBuffer";
        case ObjectKind::IndexBufferEventRegistration: return "IndexBufferEventRegistration";
        case ObjectKind::EffectAnnotation: return "EffectAnnotation";
        case ObjectKind::EffectAnnotationCollection: return "EffectAnnotationCollection";
        case ObjectKind::EffectParameter: return "EffectParameter";
        case ObjectKind::EffectParameterCollection: return "EffectParameterCollection";
        case ObjectKind::EffectPass: return "EffectPass";
        case ObjectKind::EffectPassCollection: return "EffectPassCollection";
        case ObjectKind::EffectTechnique: return "EffectTechnique";
        case ObjectKind::EffectTechniqueCollection: return "EffectTechniqueCollection";
        case ObjectKind::Effect: return "Effect";
        case ObjectKind::DirectionalLight: return "DirectionalLight";
        case ObjectKind::ModelBone: return "ModelBone";
        case ObjectKind::ModelBoneCollection: return "ModelBoneCollection";
        case ObjectKind::ModelMeshPart: return "ModelMeshPart";
        case ObjectKind::ModelMeshPartCollection: return "ModelMeshPartCollection";
        case ObjectKind::ModelMesh: return "ModelMesh";
        case ObjectKind::ModelMeshCollection: return "ModelMeshCollection";
        case ObjectKind::ModelEffectCollection: return "ModelEffectCollection";
        case ObjectKind::Model: return "Model";
        case ObjectKind::MorphTargetDataEXT: return "MorphTargetDataEXT";
        case ObjectKind::SkinnedModelEXT: return "SkinnedModelEXT";
        case ObjectKind::SkinningData: return "SkinningData";
        case ObjectKind::AnimationPlayer: return "AnimationPlayer";
        case ObjectKind::GraphicsDeviceEventRegistration: return "GraphicsDeviceEventRegistration";
        case ObjectKind::OcclusionQuery: return "OcclusionQuery";
        case ObjectKind::AsciiPostProcessEffect: return "AsciiPostProcessEffect";
        case ObjectKind::StorageDevice: return "StorageDevice";
        case ObjectKind::StorageContainer: return "StorageContainer";
        case ObjectKind::StorageStream: return "StorageStream";
        case ObjectKind::StorageDeviceEventRegistration: return "StorageDeviceEventRegistration";
        case ObjectKind::StorageContainerEventRegistration:
            return "StorageContainerEventRegistration";
        case ObjectKind::ContentReader: return "ContentReader";
        case ObjectKind::ContentTypeReader: return "ContentTypeReader";
        case ObjectKind::NetworkSessionProperties: return "NetworkSessionProperties";
        case ObjectKind::NetworkSessionPropertyEnumerator:
            return "NetworkSessionPropertyEnumerator";
        case ObjectKind::PacketWriter: return "PacketWriter";
        case ObjectKind::PacketReader: return "PacketReader";
        case ObjectKind::NetworkGamer: return "NetworkGamer";
        case ObjectKind::NetworkMachine: return "NetworkMachine";
        case ObjectKind::AvailableNetworkSession: return "AvailableNetworkSession";
        case ObjectKind::AvailableNetworkSessionCollection:
            return "AvailableNetworkSessionCollection";
        case ObjectKind::NetworkSession: return "NetworkSession";
        case ObjectKind::SignedInGamer: return "SignedInGamer";
        case ObjectKind::NetworkSessionEventRegistration: return "NetworkSessionEventRegistration";
        case ObjectKind::MouseEventRegistration: return "MouseEventRegistration";
        case ObjectKind::MouseCursor: return "MouseCursor";
        case ObjectKind::TextInputEventRegistration: return "TextInputEventRegistration";
        case ObjectKind::HapticDevice: return "HapticDevice";
        case ObjectKind::JoystickState: return "JoystickState";
        case ObjectKind::JoystickEventRegistration: return "JoystickEventRegistration";
        case ObjectKind::InputDeviceEventRegistration: return "InputDeviceEventRegistration";
        case ObjectKind::Song: return "Song";
        case ObjectKind::SongCollection: return "SongCollection";
        case ObjectKind::MediaLibrary: return "MediaLibrary";
        case ObjectKind::Album: return "Album";
        case ObjectKind::AlbumCollection: return "AlbumCollection";
        case ObjectKind::Artist: return "Artist";
        case ObjectKind::ArtistCollection: return "ArtistCollection";
        case ObjectKind::Genre: return "Genre";
        case ObjectKind::GenreCollection: return "GenreCollection";
        case ObjectKind::Playlist: return "Playlist";
        case ObjectKind::PlaylistCollection: return "PlaylistCollection";
        case ObjectKind::Picture: return "Picture";
        case ObjectKind::PictureCollection: return "PictureCollection";
        case ObjectKind::PictureAlbum: return "PictureAlbum";
        case ObjectKind::PictureAlbumCollection: return "PictureAlbumCollection";
        case ObjectKind::MediaQueue: return "MediaQueue";
        case ObjectKind::MediaPlayerEventRegistration: return "MediaPlayerEventRegistration";
        case ObjectKind::Accelerometer: return "Accelerometer";
        case ObjectKind::Gyroscope: return "Gyroscope";
        case ObjectKind::SensorEventRegistration: return "SensorEventRegistration";
        case ObjectKind::Compass: return "Compass";
        case ObjectKind::Motion: return "Motion";
        case ObjectKind::SystemTray: return "SystemTray";
        case ObjectKind::Camera: return "Camera";
        case ObjectKind::GameComponent: return "GameComponent";
        case ObjectKind::GameComponentEventRegistration: return "GameComponentEventRegistration";
        case ObjectKind::GameEventRegistration: return "GameEventRegistration";
        case ObjectKind::GraphicsDeviceManager: return "GraphicsDeviceManager";
        case ObjectKind::AudioEventRegistration: return "AudioEventRegistration";
        case ObjectKind::Video: return "Video";
        case ObjectKind::VideoPlayer: return "VideoPlayer";
        case ObjectKind::AudioEngine: return "AudioEngine";
        case ObjectKind::AudioCategory: return "AudioCategory";
        case ObjectKind::WaveBank: return "WaveBank";
        case ObjectKind::SoundBank: return "SoundBank";
        case ObjectKind::Cue: return "Cue";
        case ObjectKind::Gamer: return "Gamer";
        case ObjectKind::GamerProfile: return "GamerProfile";
        case ObjectKind::GamerCollection: return "GamerCollection";
        case ObjectKind::GamerEnumerator: return "GamerEnumerator";
        case ObjectKind::GamerEventRegistration: return "GamerEventRegistration";
        case ObjectKind::Achievement: return "Achievement";
        case ObjectKind::AchievementCollection: return "AchievementCollection";
        case ObjectKind::PropertyDictionary: return "PropertyDictionary";
        case ObjectKind::LeaderboardReader: return "LeaderboardReader";
        case ObjectKind::LeaderboardEntry: return "LeaderboardEntry";
        case ObjectKind::AvatarDescription: return "AvatarDescription";
        case ObjectKind::AvatarAnimation: return "AvatarAnimation";
        case ObjectKind::AvatarRenderer: return "AvatarRenderer";
        case ObjectKind::ModelAnimationsEXT: return "ModelAnimationsEXT";
        case ObjectKind::ContentTypeReaderRegistration: return "ContentTypeReaderRegistration";
        case ObjectKind::OwnedGraphicsDevice: return "OwnedGraphicsDevice";
        case ObjectKind::RenderTargetEventRegistration: return "RenderTargetEventRegistration";
        case ObjectKind::DebugDraw: return "DebugDraw";
        case ObjectKind::CnbDocument: return "CnbDocument";
        case ObjectKind::CnbReader: return "CnbReader";
        case ObjectKind::CnbByteWriter: return "CnbByteWriter";
        case ObjectKind::CnbWriter: return "CnbWriter";
        case ObjectKind::CnbTextureData: return "CnbTextureData";
        case ObjectKind::CnbModelData: return "CnbModelData";
        case ObjectKind::CnbModelFromCnj: return "CnbModelFromCnj";
        case ObjectKind::CnbSpriteFontData: return "CnbSpriteFontData";
        case ObjectKind::CnbSoundEffectData: return "CnbSoundEffectData";
        case ObjectKind::CnbAnimationClip: return "CnbAnimationClip";
        case ObjectKind::CnbLoader: return "CnbLoader";
        case ObjectKind::CnjToCnbResult: return "CnjToCnbResult";
        case ObjectKind::ReflectiveTypeReaderBuilder: return "ReflectiveTypeReaderBuilder";
        case ObjectKind::ObjectDictionaryEXT: return "ObjectDictionaryEXT";
        case ObjectKind::Test: return "Test";
    }
    return "Unknown";
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
            return Fail(
                CNA_RESULT_OUT_OF_MEMORY, CNA_ERROR_CATEGORY_MEMORY, "Native allocation failed.");
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

CNA_Result FailHandle(
    const CNA_Result result,
    const ObjectKind kind,
    const std::string_view predicate) noexcept
{
    SetLastError(result, ErrorCategoryForResult(result), {});
    try {
        lastError.message.append("The ");
        if (kind != ObjectKind::Unknown) {
            lastError.message.append(ObjectKindName(kind)).append(" ");
        }
        lastError.message.append("handle ").append(predicate);
    } catch (...) {
        lastError.message.clear();
    }
    return result;
}

CNA_Result ValidateCanonicalBool(const CNA_Bool value, const std::string_view name) noexcept
{
    if (IsCanonicalBool(value)) {
        return CNA_RESULT_SUCCESS;
    }
    // The name is in the message because a route may take more than one flag, and "a flag is not a
    // Boolean" is not something a caller can act on without knowing which.
    SetLastError(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, {});
    try {
        lastError.message.append("The '").append(name).append(
            "' flag must be CNA_FALSE or CNA_TRUE.");
    } catch (...) {
        lastError.message.clear();
    }
    return CNA_RESULT_INVALID_ARGUMENT;
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
    std::string* const outValue,
    const EmbeddedNul nul) noexcept
{
    if (const CNA_Result result = CopyStringView(value, nul == EmbeddedNul::Refused, outValue);
        result != CNA_RESULT_SUCCESS) {
        return Fail(result, ErrorCategoryForResult(result), message);
    }
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

uint32_t NextHandleGeneration(const uint32_t current) noexcept
{
    return current == 0U || current == std::numeric_limits<uint32_t>::max() ? 0U : current + 1U;
}

HandleRegistry::~HandleRegistry()
{
    // Held rather than dropped; the process is ending.
    const std::thread::id current = std::this_thread::get_id();
    for (Slot& slot : slots_) {
        if (slot.object != nullptr && (processTerminating || slot.creationThread != current)) {
            Abandon(std::move(slot.object));
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
        if (slot.object || slot.generation == 0U) {
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

void HandleRegistry::DestroyAll() noexcept
{
    std::vector<Slot> slots;
    {
        std::lock_guard lock(mutex_);
        slots.swap(slots_);
    }
    if (processTerminating) {
        for (Slot& slot : slots) {
            if (slot.object != nullptr) Abandon(std::move(slot.object));
        }
    }
    // Destroyed outside the lock: an object's destructor may release handles of its own, which
    // the now empty registry answers as invalid.
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
        slot->generation = NextHandleGeneration(slot->generation);
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

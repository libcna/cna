// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_API_DETAIL_HPP
#define CNA_C_API_DETAIL_HPP

#include "CNA/C/core.h"

// Header-only canonical exception types. They add no link dependency, so every C entry point can
// report a lost, not-reset or unavailable graphics device as a specific result instead of letting
// it fall through to the generic internal-failure arm.
#include "Microsoft/Xna/Framework/Graphics/DeviceLostException.hpp"
#include "Microsoft/Xna/Framework/Graphics/DeviceNotResetException.hpp"
#include "Microsoft/Xna/Framework/Graphics/NoSuitableGraphicsDeviceException.hpp"

// A failed asset load and a disconnected storage device are I/O and state failures rather than
// generic internal ones. Catching either needs only the type's weakly emitted RTTI, not its
// out-of-line constructors, so both stay compile-time dependencies and add no link edge to any
// translation unit that never throws one.
#include "CNA/CNAException.hpp"
#ifdef CNA_CNAEXT
#include "CNA/Graphics/EngineException.hpp"
#endif
#include "CNA/Platform/PlatformException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Devices/Sensors/SensorFailedException.hpp"
#include "Microsoft/Xna/Framework/Audio/InstancePlayLimitException.hpp"
#include "Microsoft/Xna/Framework/Audio/NoAudioHardwareException.hpp"
#include "Microsoft/Xna/Framework/Audio/NoMicrophoneConnectedException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GameUpdateRequiredException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPrivilegeException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GuideAlreadyVisibleException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/NetworkException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/NetworkNotAvailableException.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionJoinException.hpp"
#include "Microsoft/Xna/Framework/Storage/StorageDeviceNotConnectedException.hpp"

#include "System/ArgumentException.hpp"
#include "System/IO/IOException.hpp"
#include "System/NotImplementedException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/InvalidOperationException.hpp"

#include <cstdint>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <ios>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace CNA::C::Detail {

enum class ObjectKind : uint32_t {
    Unknown = 0,
    Runtime = 1,
    Game = 2,
    GraphicsDevice = 3,
    Texture2D = 4,
    SpriteBatch = 5,
    EventRegistration = 6,
    ContentManager = 7,
    SoundEffect = 8,
    SoundEffectInstance = 9,
    RenderTarget2D = 10,
    RenderTargetCube = 11,
    SpriteFont = 12,
    CurveKeyCollection = 13,
    Curve = 14,
    VertexDeclaration = 15,
    Texture3D = 16,
    TextureCube = 17,
    VertexBuffer = 18,
    VertexBufferEventRegistration = 19,
    IndexBuffer = 20,
    IndexBufferEventRegistration = 21,
    EffectAnnotation = 22,
    EffectAnnotationCollection = 23,
    EffectParameter = 24,
    EffectParameterCollection = 25,
    EffectPass = 26,
    EffectPassCollection = 27,
    EffectTechnique = 28,
    EffectTechniqueCollection = 29,
    Effect = 30,
    DirectionalLight = 31,
    ModelBone = 32,
    ModelBoneCollection = 33,
    ModelMeshPart = 34,
    ModelMeshPartCollection = 35,
    ModelMesh = 36,
    ModelMeshCollection = 37,
    ModelEffectCollection = 38,
    Model = 39,
    MorphTargetDataEXT = 40,
    SkinnedModelEXT = 41,
    SkinningData = 42,
    AnimationPlayer = 43,
    GraphicsDeviceEventRegistration = 44,
    OcclusionQuery = 45,
    AsciiPostProcessEffect = 46,
    StorageDevice = 47,
    StorageContainer = 48,
    StorageStream = 49,
    StorageDeviceEventRegistration = 50,
    StorageContainerEventRegistration = 51,
    ContentReader = 52,
    ContentTypeReader = 53,
    NetworkSessionProperties = 54,
    NetworkSessionPropertyEnumerator = 55,
    PacketWriter = 56,
    PacketReader = 57,
    NetworkGamer = 58,
    NetworkMachine = 59,
    AvailableNetworkSession = 60,
    AvailableNetworkSessionCollection = 61,
    NetworkSession = 62,
    SignedInGamer = 63,
    NetworkSessionEventRegistration = 64,
    MouseEventRegistration = 65,
    MouseCursor = 66,
    TextInputEventRegistration = 67,
    HapticDevice = 68,
    JoystickState = 69,
    JoystickEventRegistration = 70,
    InputDeviceEventRegistration = 71,
    Song = 72,
    SongCollection = 73,
    MediaLibrary = 74,
    Album = 75,
    AlbumCollection = 76,
    Artist = 77,
    ArtistCollection = 78,
    Genre = 79,
    GenreCollection = 80,
    Playlist = 81,
    PlaylistCollection = 82,
    Picture = 83,
    PictureCollection = 84,
    PictureAlbum = 85,
    PictureAlbumCollection = 86,
    MediaQueue = 87,
    MediaPlayerEventRegistration = 88,
    Accelerometer = 91,
    Gyroscope = 92,
    SensorEventRegistration = 93,
    Compass = 94,
    Motion = 95,
    SystemTray = 96,
    Camera = 97,
    GameComponent = 98,
    GameComponentEventRegistration = 99,
    GameEventRegistration = 100,
    GraphicsDeviceManager = 101,
    AudioEventRegistration = 102,
    Video = 89,
    VideoPlayer = 90,
    AudioEngine = 103,
    AudioCategory = 104,
    WaveBank = 105,
    SoundBank = 106,
    Cue = 107,
    Gamer = 108,
    GamerProfile = 109,
    GamerCollection = 110,
    GamerEnumerator = 111,
    GamerEventRegistration = 112,
    Achievement = 113,
    AchievementCollection = 114,
    PropertyDictionary = 115,
    LeaderboardReader = 116,
    LeaderboardEntry = 117,
    AvatarDescription = 118,
    AvatarAnimation = 119,
    AvatarRenderer = 120,
    ModelAnimationsEXT = 121,
    ContentTypeReaderRegistration = 122,
    // plans/plan_cabi.md CABI-13. Distinct from GraphicsDevice, which is the game's own
    // device borrowed for a callback: this one the caller created and must destroy.
    OwnedGraphicsDevice = 123,
    /// plans/plan_cabi.md CABI-24: one render-target ContentLost subscription.
    RenderTargetEventRegistration = 124,
    // plans/plan_binding.md CBIND-084A: the engine layer's first two owned resources. Both exist
    // only when CNA_CNAEXT is on; the kinds are declared unconditionally so the registry's kind
    // space does not shift with a build option.
    StorageBuffer = 125,
    ComputeShader = 126,
    // plans/plan_binding.md CBIND-084B: timer, pool, factory and active scope handles follow
    // without reusing either resource kind above; every value stays fixed when CNA_CNAEXT is off.
    GpuTimer = 127,
    RenderTargetPool = 128,
    ShaderEffectFactory = 129,
    ScopedRenderTarget = 130,
    // plans/plan_binding.md CBIND-084C: the full-screen drawer, and one kind for every concrete
    // post-process pass. A pass is one kind rather than one per concrete type because what crosses
    // this ABI is the abstract contract's operations, not the class that implements them.
    FullscreenPass = 131,
    PostProcessPass = 132,
    // plans/plan_binding.md CBIND-085B1: the two 2D shadow maps.
    ShadowMap = 133,
    SpotShadowMap = 134,
    // plans/plan_binding.md CBIND-085B2: the cascaded atlas and the cube.
    CascadedShadowMap = 135,
    CubeShadowMap = 136,
    // plans/plan_binding.md CBIND-085C1: the clustered shadow budget, a pure CPU object.
    ClusteredShadowPolicy = 137,
    // plans/plan_binding.md CBIND-085C2. ContactShadowPass needs no kind of its own: it is a
    // PostProcessPass, so it uses that one.
    DepthNormalPrepass = 138,
    // plans/plan_binding.md CBIND-086A: a collection of values, so no borrow count.
    ClusteredLightSet = 139,
    // plans/plan_binding.md CBIND-086B. The grid and the assignment are pure CPU objects; the
    // buffer owns three textures and lends none of them, so none of the three counts a borrow.
    ClusteredLightGrid = 140,
    ClusteredLightAssignment = 141,
    ClusteredLightBuffer = 142,
    // plans/plan_binding.md CBIND-086C.
    ClusteredForwardEffect = 143,
    ClusteredLightCompute = 144,
    // plans/plan_binding.md CBIND-087A.
    PbrMaterialExtensions = 145,
    // plans/plan_binding.md CBIND-087D.
    TransparentDrawList = 146,
    WeightedBlendedTransparency = 147,
    // plans/plan_binding.md CBIND-088B.
    RenderPipeline = 148,
    // plans/plan_binding.md CBIND-089A.
    PostProcessChain = 149,
    // plans/plan_binding.md CBIND-089D: neither derives from PostProcessPass.
    DecalPass = 150,
    SpatialUpscalePass = 151,
    // plans/plan_binding.md CBIND-090: none of the three derives from PostProcessPass.
    HdrDisplayOutput = 152,
    AutoExposure = 153,
    CubeLut = 154,
    // plans/plan_binding.md CBIND-091A.
    LightProbe = 155,
    LightProbeVolume = 156,

    // CBIND-091B.
    LightProbeBaker = 157,
    EnvironmentProcessor = 158,
    Skybox = 159,
    AtmosphericSky = 160,

    // CBIND-091C.
    AreaLightBrdfTable = 161,

    // CBIND-092A.
    ParticleSystem = 162,

    // CBIND-092B.
    InstancedRenderer = 163,
    LodGroup = 164,

    // CBIND-092C.
    FrustumCuller = 165,
    GpuInstanceCuller = 166,

    // CBIND-092D.
    DebugDraw = 167,

    // CBIND-107: the .cnb container's four owning objects. The reader is the only one that can
    // hold a borrow -- of the document it was opened from -- which is why the document counts its
    // active borrows and refuses release while any stands.
    CnbDocument = 168,
    CnbReader = 169,
    CnbByteWriter = 170,
    CnbWriter = 171,

    // CBIND-108: the decoded texture description, which the canonical layer expresses as nested
    // vectors and C therefore reaches through a handle rather than a POD.
    CnbTextureData = 172,

    // CBIND-109: the model graph, one handle for the whole of it, and the compile result that
    // carries a model plus the two file lists a build system needs alongside it.
    CnbModelData = 173,
    CnbModelFromCnj = 174,

    // CBIND-110: the two schemas whose decoded form owns bulk data -- a font its atlas, a sound its
    // samples -- plus a standalone clip, whose keyframes cannot be lent back as a borrowed
    // descriptor. Curve decodes into ObjectKind::Curve, the handle the curve family already owns.
    CnbSpriteFontData = 175,
    CnbSoundEffectData = 176,
    CnbAnimationClip = 177,

    // CBIND-111: a resolved loader is a copy of the registered function, which is why it is an
    // object rather than a cursor into the table; a compile result holds bytes and two file lists.
    CnbLoader = 178,
    CnjToCnbResult = 179,

    // CBIND-105: a reflective reader's field list, held while it is being declared. The reader it
    // registers is owned by the canonical reader table, not by a handle.
    ReflectiveTypeReaderBuilder = 180,

    // CBIND-116: a Dictionary<string, object> read out of an .xnb. Borrowed rather than owned --
    // the loaded Model owns it, so the handle carries a weak view and no destroy route exists.
    ObjectDictionaryEXT = 181,
    Test = UINT32_MAX
};

struct LastError final {
    CNA_Result result = CNA_RESULT_SUCCESS;
    CNA_ErrorCategory category = CNA_ERROR_CATEGORY_NONE;
    std::string message;
    // A join failure is the one canonical exception whose payload is not expressible in the
    // message alone, so the firewall records it here rather than dropping it.
    bool hasJoinError = false;
    uint32_t joinError = 0U;
    // A sensor failure carries the same kind of payload: an error id the message does not spell
    // out, recorded here for the same reason.
    bool hasSensorErrorId = false;
    int32_t sensorErrorId = 0;
};

[[nodiscard]] const LastError& GetLastError() noexcept;

[[nodiscard]] CNA_ErrorCategory ErrorCategoryForResult(CNA_Result result) noexcept;

void SetLastError(
    CNA_Result result,
    CNA_ErrorCategory category,
    std::string_view message) noexcept;

[[nodiscard]] CNA_Result Fail(
    CNA_Result result,
    CNA_ErrorCategory category,
    std::string_view message) noexcept;

void SetLastJoinError(uint32_t joinError) noexcept;

void SetLastSensorErrorId(int32_t sensorErrorId) noexcept;

template<typename TCallable>
[[nodiscard]] CNA_Result CallWithExceptionBarrier(TCallable&& callable) noexcept
{
    try {
        return callable();
    } catch (const std::bad_alloc&) {
        return Fail(
            CNA_RESULT_OUT_OF_MEMORY,
            CNA_ERROR_CATEGORY_MEMORY,
            "Native allocation failed.");
    } catch (const std::overflow_error& exception) {
        return Fail(CNA_RESULT_OVERFLOW, CNA_ERROR_CATEGORY_RANGE, exception.what());
    } catch (const std::range_error& exception) {
        return Fail(CNA_RESULT_OVERFLOW, CNA_ERROR_CATEGORY_RANGE, exception.what());
    } catch (const std::out_of_range& exception) {
        return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_RANGE, exception.what());
    } catch (const std::invalid_argument& exception) {
        return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, exception.what());
    } catch (const std::ios_base::failure& exception) {
        return Fail(CNA_RESULT_IO, CNA_ERROR_CATEGORY_IO, exception.what());
    } catch (const std::filesystem::filesystem_error& exception) {
        return Fail(CNA_RESULT_IO, CNA_ERROR_CATEGORY_IO, exception.what());
    } catch (const System::IO::IOException& exception) {
        return Fail(CNA_RESULT_IO, CNA_ERROR_CATEGORY_IO, exception.what());
    } catch (const Microsoft::Xna::Framework::Content::ContentLoadException& exception) {
        return Fail(CNA_RESULT_IO, CNA_ERROR_CATEGORY_IO, exception.what());
    } catch (
        const Microsoft::Xna::Framework::Storage::StorageDeviceNotConnectedException& exception) {
        return Fail(CNA_RESULT_INVALID_STATE, CNA_ERROR_CATEGORY_STATE, exception.what());
    } catch (const Microsoft::Xna::Framework::Audio::NoAudioHardwareException& exception) {
        return Fail(CNA_RESULT_NOT_SUPPORTED, CNA_ERROR_CATEGORY_NOT_SUPPORTED, exception.what());
    } catch (const Microsoft::Xna::Framework::Audio::NoMicrophoneConnectedException& exception) {
        return Fail(CNA_RESULT_NOT_SUPPORTED, CNA_ERROR_CATEGORY_NOT_SUPPORTED, exception.what());
    } catch (const Microsoft::Xna::Framework::Audio::InstancePlayLimitException& exception) {
        return Fail(CNA_RESULT_INVALID_STATE, CNA_ERROR_CATEGORY_STATE, exception.what());
    } catch (const Microsoft::Xna::Framework::GamerServices::GamerServicesNotAvailableException&
                 exception) {
        // The platform has no gamer services at all. Nothing the caller supplies or retries changes
        // that, which is what separates it from every arm below.
        return Fail(CNA_RESULT_NOT_SUPPORTED, CNA_ERROR_CATEGORY_NOT_SUPPORTED, exception.what());
    } catch (const Microsoft::Xna::Framework::GamerServices::GameUpdateRequiredException&
                 exception) {
        // The service is there and refuses this build of the title. Also unchangeable by the caller
        // -- only shipping a new build fixes it -- so it is the same kind of answer as the one above
        // rather than a state the game can wait out.
        return Fail(CNA_RESULT_NOT_SUPPORTED, CNA_ERROR_CATEGORY_NOT_SUPPORTED, exception.what());
    } catch (const Microsoft::Xna::Framework::GamerServices::GamerPrivilegeException& exception) {
        // The operation is supported and well-formed; this gamer's privileges do not currently allow
        // it. Privileges are per-gamer state that can change, so this is a state failure rather than
        // an unsupported operation.
        return Fail(CNA_RESULT_INVALID_STATE, CNA_ERROR_CATEGORY_STATE, exception.what());
    } catch (const Microsoft::Xna::Framework::GamerServices::GuideAlreadyVisibleException&
                 exception) {
        return Fail(CNA_RESULT_INVALID_STATE, CNA_ERROR_CATEGORY_STATE, exception.what());
    } catch (const Microsoft::Devices::Sensors::SensorFailedException& exception) {
        const CNA_Result result =
            Fail(CNA_RESULT_INVALID_STATE, CNA_ERROR_CATEGORY_STATE, exception.what());
        SetLastSensorErrorId(static_cast<int32_t>(exception.getErrorIdProperty()));
        return result;
    } catch (const Microsoft::Xna::Framework::Net::NetworkSessionJoinException& exception) {
        const CNA_Result result =
            Fail(CNA_RESULT_INVALID_STATE, CNA_ERROR_CATEGORY_STATE, exception.what());
        SetLastJoinError(static_cast<uint32_t>(exception.getJoinErrorProperty()));
        return result;
    } catch (const Microsoft::Xna::Framework::GamerServices::NetworkNotAvailableException&
                 exception) {
        // Sits after the join arm and before its own base, because all three are one hierarchy: the
        // net module's join failure derives from this module's network exception. An absent network
        // is a resource that is simply not there right now, the same shape a disconnected storage
        // device already has.
        return Fail(CNA_RESULT_INVALID_STATE, CNA_ERROR_CATEGORY_STATE, exception.what());
    } catch (const Microsoft::Xna::Framework::GamerServices::NetworkException& exception) {
        // The base of both arms above: a network operation failed while the network was available.
        // That is a native service failure, and one a retry may well get past.
        return Fail(CNA_RESULT_PLATFORM, CNA_ERROR_CATEGORY_PLATFORM, exception.what());
    } catch (const CNA::Platform::PlatformException& exception) {
        // Merged from the platform-separation campaign: a platform operation that was expected to
        // succeed did not -- SDL could not create a system cursor under the dummy video driver, a
        // window could not be made, a subsystem could not be acquired. That is a native service
        // failure, which this ABI already has a result and a category for, and it must not fall
        // through to the std::exception arm and be reported as CNA_RESULT_INTERNAL: a caller can do
        // something about a platform refusal, and nothing about an internal error.
        //
        // It derives from std::runtime_error, so this arm must precede that one.
        return Fail(CNA_RESULT_PLATFORM, CNA_ERROR_CATEGORY_PLATFORM, exception.what());
#ifdef CNA_CNAEXT
    } catch (const CNA::Graphics::EngineException& exception) {
        // plans/plan_binding.md CBIND-084A. The engine layer throws this when a renderer cannot do
        // what a subsystem asked, and its message already names all three parts -- which subsystem,
        // what it needed, which renderer refused -- because EngineException::notSupported composes
        // them. So the three property accessors need no routes of their own: what a C caller can
        // act on crosses in the message, and the result says the refusal was a capability boundary
        // rather than a defect. It derives from System::Exception, so this arm must precede the
        // arms below that catch its bases.
        return Fail(CNA_RESULT_NOT_SUPPORTED, CNA_ERROR_CATEGORY_NOT_SUPPORTED, exception.what());
#endif
    } catch (const CNA::CNAException& exception) {
        return Fail(CNA_RESULT_INVALID_STATE, CNA_ERROR_CATEGORY_STATE, exception.what());
    } catch (const System::ArgumentException& exception) {
        return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, exception.what());
    } catch (const System::NotImplementedException& exception) {
        return Fail(CNA_RESULT_NOT_SUPPORTED, CNA_ERROR_CATEGORY_NOT_SUPPORTED, exception.what());
    } catch (const System::NotSupportedException& exception) {
        return Fail(CNA_RESULT_NOT_SUPPORTED, CNA_ERROR_CATEGORY_NOT_SUPPORTED, exception.what());
    } catch (const System::InvalidOperationException& exception) {
        return Fail(CNA_RESULT_INVALID_STATE, CNA_ERROR_CATEGORY_STATE, exception.what());
    } catch (const Microsoft::Xna::Framework::Graphics::DeviceLostException& exception) {
        return Fail(CNA_RESULT_INVALID_STATE, CNA_ERROR_CATEGORY_STATE, exception.what());
    } catch (const Microsoft::Xna::Framework::Graphics::DeviceNotResetException& exception) {
        return Fail(CNA_RESULT_INVALID_STATE, CNA_ERROR_CATEGORY_STATE, exception.what());
    } catch (
        const Microsoft::Xna::Framework::Graphics::NoSuitableGraphicsDeviceException& exception) {
        return Fail(CNA_RESULT_NOT_SUPPORTED, CNA_ERROR_CATEGORY_NOT_SUPPORTED, exception.what());
    } catch (const std::exception& exception) {
        return Fail(CNA_RESULT_INTERNAL, CNA_ERROR_CATEGORY_INTERNAL, exception.what());
    } catch (...) {
        return Fail(
            CNA_RESULT_INTERNAL,
            CNA_ERROR_CATEGORY_INTERNAL,
            "An unknown native failure occurred.");
    }
}

/**
 * @brief Reports whether a caller-supplied `CNA_Bool` is one of the two values the ABI defines.
 *
 * CBIND-067: `docs/c-api/ABI_VERSIONING.md` has always said only `CNA_FALSE` and `CNA_TRUE` are
 * valid, and the library enforced that in 24 routes out of 94. The other 66 accepted any byte and
 * then disagreed about what it meant -- read as `!= CNA_FALSE` in some places and `== CNA_TRUE` in
 * others, so a `9` was true here and false there. This is the single spelling they now share.
 */
[[nodiscard]] inline bool IsCanonicalBool(const CNA_Bool value) noexcept
{
    return value == CNA_FALSE || value == CNA_TRUE;
}

/**
 * @brief Refuses a `CNA_Bool` that is neither `CNA_FALSE` nor `CNA_TRUE`.
 *
 * @param value The caller-supplied flag.
 * @param name The parameter's name as the public header spells it, for the diagnostic.
 * @return `CNA_RESULT_SUCCESS` when the value is canonical, `CNA_RESULT_INVALID_ARGUMENT`
 *         otherwise.
 *
 * Call it **after** a route has cleared its `out_` handle, not before: this ABI promises a refused
 * creation leaves its output invalid, and a guard placed ahead of that clearing returns before the
 * promise is kept. `EffectSmoke.c` asserts exactly that pairing.
 */
[[nodiscard]] CNA_Result ValidateCanonicalBool(CNA_Bool value, std::string_view name) noexcept;

[[nodiscard]] CNA_Result ValidateStringView(
    CNA_StringView value,
    bool rejectEmbeddedNul) noexcept;

[[nodiscard]] CNA_Result CopyStringView(
    CNA_StringView value,
    bool rejectEmbeddedNul,
    std::string* outValue) noexcept;

[[nodiscard]] CNA_Result ValidateBuffer(
    const void* data,
    uint64_t count) noexcept;

[[nodiscard]] CNA_Result CheckedElementByteCount(
    const void* data,
    uint64_t elementCount,
    uint64_t elementByteSize,
    std::size_t* outByteCount) noexcept;

class HandleRegistry final {
public:
    CNA_Result Create(
        ObjectKind kind,
        std::shared_ptr<void> object,
        CNA_Handle* outHandle);

    CNA_Result GetKind(CNA_Handle handle, ObjectKind* outKind) const;

    CNA_Result GetUserTag(CNA_Handle handle, uint64_t* outTag) const;

    CNA_Result SetUserTag(CNA_Handle handle, uint64_t tag);

    template<typename TObject>
    CNA_Result Get(
        const CNA_Handle handle,
        const ObjectKind expectedKind,
        std::shared_ptr<TObject>* const outObject) const
    {
        if (outObject == nullptr || expectedKind == ObjectKind::Unknown) {
            return CNA_RESULT_INVALID_ARGUMENT;
        }

        std::lock_guard lock(mutex_);
        Slot* slot = nullptr;
        const CNA_Result result = FindSlotLocked(handle, &slot);
        if (result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (slot->kind != expectedKind) {
            return CNA_RESULT_INVALID_HANDLE;
        }
        if (slot->creationThread != std::this_thread::get_id()) {
            return CNA_RESULT_THREAD;
        }

        *outObject = std::static_pointer_cast<TObject>(slot->object);
        return CNA_RESULT_SUCCESS;
    }

    CNA_Result Release(CNA_Handle handle);

private:
    struct Slot final {
        uint32_t generation = 1;
        ObjectKind kind = ObjectKind::Unknown;
        std::shared_ptr<void> object;
        std::thread::id creationThread;
        uint64_t userTag = 0U;
    };

    [[nodiscard]] CNA_Result FindSlotLocked(CNA_Handle handle, Slot** outSlot) const;

    mutable std::mutex mutex_;
    mutable std::vector<Slot> slots_;
};

} // namespace CNA::C::Detail

#endif

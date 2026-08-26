// SPDX-License-Identifier: MS-PL

#include "CNA/C/engine_layer.h"
#include "CNA/C/matrix.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiGraphicsDetail.hpp"
#include "CnaCApiRenderTargetDetail.hpp"
#include "CnaCApiGraphicsStateDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/GraphicsImageAccess.hpp"
#include "Microsoft/Xna/Framework/Graphics/PunctualLightEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShadowCascadeStateEXT.hpp"
#include "CNA/GraphicsMemoryBarrier.hpp"

#include <cstring>
#include <memory>
#include <string>

#ifdef CNA_CNAEXT
#include "CNA/Graphics/BlitPass.hpp"
#include "CNA/Graphics/ComputeShader.hpp"
#include "CNA/Graphics/DepthEncoding.hpp"
#include "CNA/Graphics/DirectionalLightEXT.hpp"
#include "CNA/Graphics/PointLightEXT.hpp"
#include "CNA/Graphics/ShadowMap.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "CNA/Graphics/ShadowQuality.hpp"
#include "CNA/Graphics/SpotShadowMap.hpp"
#include "CNA/Graphics/SpotLightEXT.hpp"
#include "CNA/Graphics/EffectPass.hpp"
#include "CNA/Graphics/EngineLayerVersion.hpp"
#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/Graphics/GpuTimer.hpp"
#include "CNA/Graphics/MaterialBinding.hpp"
#include "CNA/Graphics/PbrMaterial.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/PostProcessPass.hpp"
#include "CNA/Graphics/RenderTargetPool.hpp"
#include "CNA/Graphics/ScopedRenderTarget.hpp"
#include "CNA/Graphics/ShaderEffectFactory.hpp"
#include "CNA/Graphics/StorageBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <limits>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#endif

namespace {

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::Fail;

// The engine layer is compiled out by default (CNA_CNAEXT is OFF at CMakeLists.txt:81), and this
// ABI keeps one shape regardless: every route below is declared and exported in both builds, and
// the ones that need a native engine-layer object answer this instead of vanishing. An export list
// that changed with a CMake option would make the recorded ABI baseline describe neither build.
[[nodiscard, maybe_unused]] CNA_Result ExtensionUnavailable()
{
    return Fail(
        CNA_RESULT_NOT_SUPPORTED,
        CNA_ERROR_CATEGORY_NOT_SUPPORTED,
        "This CNA build does not contain the extended graphics layer.");
}

template<typename TValue>
[[nodiscard]] CNA_Result StoreValue(TValue* const output, const TValue value) noexcept
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (output == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The output value is null.");
        }
        *output = value;
        return CNA_RESULT_SUCCESS;
    });
}

template<typename TCallable>
[[nodiscard, maybe_unused]] CNA_Result CopyFormattedString(
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes,
    TCallable&& callable) noexcept
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The string destination or required-byte output is invalid.");
        }
        const std::string text = std::forward<TCallable>(callable)();
        *outBytes = static_cast<uint64_t>(text.size());
        if (capacity < text.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination cannot hold the complete text.");
        }
        if (!text.empty()) {
            std::memcpy(destination, text.data(), text.size());
        }
        return CNA_RESULT_SUCCESS;
    });
}

template<typename TEnum>
[[nodiscard]] constexpr uint32_t NativeOrdinal(const TEnum value) noexcept
{
    return static_cast<uint32_t>(static_cast<std::underlying_type_t<TEnum>>(value));
}

// The C constants are the contract; these prove they still name what the canonical enumerations
// name. GraphicsImageAccess and GraphicsMemoryBarrier are not behind CNA_CNAEXT -- they live in
// modules/graphics and exist in every build -- so their agreement is checked in every build too.
static_assert(
    NativeOrdinal(CNA::GraphicsImageAccess::ReadOnly) == CNA_GRAPHICS_IMAGE_ACCESS_READ_ONLY &&
    NativeOrdinal(CNA::GraphicsImageAccess::WriteOnly) == CNA_GRAPHICS_IMAGE_ACCESS_WRITE_ONLY &&
    NativeOrdinal(CNA::GraphicsImageAccess::ReadWrite) == CNA_GRAPHICS_IMAGE_ACCESS_READ_WRITE);

static_assert(
    NativeOrdinal(CNA::GraphicsMemoryBarrier::None) == CNA_GRAPHICS_MEMORY_BARRIER_NONE &&
    NativeOrdinal(CNA::GraphicsMemoryBarrier::VertexAttribArray) ==
        CNA_GRAPHICS_MEMORY_BARRIER_VERTEX_ATTRIB_ARRAY &&
    NativeOrdinal(CNA::GraphicsMemoryBarrier::ElementArray) ==
        CNA_GRAPHICS_MEMORY_BARRIER_ELEMENT_ARRAY &&
    NativeOrdinal(CNA::GraphicsMemoryBarrier::Uniform) == CNA_GRAPHICS_MEMORY_BARRIER_UNIFORM &&
    NativeOrdinal(CNA::GraphicsMemoryBarrier::TextureFetch) ==
        CNA_GRAPHICS_MEMORY_BARRIER_TEXTURE_FETCH &&
    NativeOrdinal(CNA::GraphicsMemoryBarrier::ShaderImageAccess) ==
        CNA_GRAPHICS_MEMORY_BARRIER_SHADER_IMAGE_ACCESS &&
    NativeOrdinal(CNA::GraphicsMemoryBarrier::ShaderStorage) ==
        CNA_GRAPHICS_MEMORY_BARRIER_SHADER_STORAGE &&
    NativeOrdinal(CNA::GraphicsMemoryBarrier::BufferUpdate) ==
        CNA_GRAPHICS_MEMORY_BARRIER_BUFFER_UPDATE &&
    NativeOrdinal(CNA::GraphicsMemoryBarrier::Framebuffer) ==
        CNA_GRAPHICS_MEMORY_BARRIER_FRAMEBUFFER &&
    NativeOrdinal(CNA::GraphicsMemoryBarrier::IndirectCommand) ==
        CNA_GRAPHICS_MEMORY_BARRIER_INDIRECT_COMMAND &&
    NativeOrdinal(CNA::GraphicsMemoryBarrier::All) == CNA_GRAPHICS_MEMORY_BARRIER_ALL);

#ifdef CNA_CNAEXT

using CNA::C::Detail::AddOwnedGraphicsResourceFor;
using CNA::C::Detail::BorrowedGraphicsDevice;
using CNA::C::Detail::CopyStringView;
using CNA::C::Detail::EffectResource;
using CNA::C::Detail::ValidateCanonicalBool;
using CNA::C::Detail::ToNativeSamplerState;
using CNA::C::Detail::CreateBorrowedEffect;
using CNA::C::Detail::CreateBorrowedRenderTarget2D;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::GetBorrowedGraphicsDevice;
using CNA::C::Detail::GetOwnedTexture2D;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::GetTrackedRenderTargetBindings;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::RemoveOwnedGraphicsResourceFor;
using CNA::C::Detail::ResolveRenderTargetScopeReference;
using CNA::C::Detail::SetTrackedRenderTargetBindings;
using CNA::C::Detail::Texture2DResource;

namespace Ext = CNA::Graphics;

static_assert(
    NativeOrdinal(Ext::DepthEncoding::Automatic) == CNA_DEPTH_ENCODING_AUTOMATIC &&
    NativeOrdinal(Ext::DepthEncoding::Packed) == CNA_DEPTH_ENCODING_PACKED &&
    NativeOrdinal(Ext::DepthEncoding::HalfFloat) == CNA_DEPTH_ENCODING_HALF_FLOAT);

static_assert(
    CNA_ENGINE_LAYER_VERSION == CNA_CNAEXT_ENGINE_VERSION,
    "the C header's engine-layer revision must equal the canonical macro it mirrors");

// elementCount and elementByteSize are what StorageBufferT<T> carries in its type. C has no
// templates, so the buffer carries them as values instead -- which is what lets the element routes
// enforce the same "more elements than the buffer holds" refusal the template's setData enforces.
// Both are zero for a buffer created by byte size, and the element routes refuse such a buffer.
struct StorageBufferResource final {
    std::shared_ptr<Ext::StorageBuffer> value;
    CNA_Handle parentGame;
    uint64_t elementCount;
    uint64_t elementByteSize;
};

struct ComputeShaderResource final {
    std::shared_ptr<Ext::ComputeShader> value;
    CNA_Handle parentGame;
};

struct GpuTimerResource final {
    std::shared_ptr<Ext::GpuTimer> value;
    CNA_Handle parentGame;
};

struct RenderTargetPoolResource final {
    std::shared_ptr<Ext::RenderTargetPool> value;
    CNA_Handle parentGame;
    uint64_t activeBorrowCount = 0U;
};

struct ShaderEffectFactoryResource final {
    std::shared_ptr<Ext::ShaderEffectFactory> value;
    CNA_Handle parentGame;
    uint64_t activeBorrowCount = 0U;
};

template<typename TOwner>
class CountedBorrow final {
public:
    explicit CountedBorrow(std::shared_ptr<TOwner> owner)
        : owner_(std::move(owner))
    {
        if (owner_->activeBorrowCount == std::numeric_limits<uint64_t>::max()) {
            throw std::overflow_error("The engine-layer borrow count cannot be incremented.");
        }
        ++owner_->activeBorrowCount;
    }

    CountedBorrow(const CountedBorrow&) = delete;
    CountedBorrow& operator=(const CountedBorrow&) = delete;

    ~CountedBorrow()
    {
        if (owner_ != nullptr && owner_->activeBorrowCount != 0U) {
            --owner_->activeBorrowCount;
        }
    }

private:
    std::shared_ptr<TOwner> owner_;
};

class ScopeTargetReference final {
public:
    ScopeTargetReference(std::shared_ptr<void> owner, uint64_t* const referenceCount)
        : owner_(std::move(owner)), referenceCount_(referenceCount)
    {
        if (referenceCount_ == nullptr ||
            *referenceCount_ == std::numeric_limits<uint64_t>::max()) {
            throw std::overflow_error("The render-target scope reference count cannot be incremented.");
        }
        ++*referenceCount_;
    }

    ScopeTargetReference(const ScopeTargetReference&) = delete;
    ScopeTargetReference& operator=(const ScopeTargetReference&) = delete;

    ScopeTargetReference(ScopeTargetReference&& other) noexcept
        : owner_(std::move(other.owner_)), referenceCount_(other.referenceCount_)
    {
        other.referenceCount_ = nullptr;
    }

    ScopeTargetReference& operator=(ScopeTargetReference&&) = delete;

    ~ScopeTargetReference()
    {
        if (referenceCount_ != nullptr && *referenceCount_ != 0U) {
            --*referenceCount_;
        }
    }

private:
    std::shared_ptr<void> owner_;
    uint64_t* referenceCount_;
};

struct ScopedRenderTargetResource final {
    CNA_Handle parentGame;
    Microsoft::Xna::Framework::Graphics::GraphicsDevice* device;
    uint64_t stackToken;
    std::vector<CNA_RenderTargetBinding> previousBindings;
    std::vector<ScopeTargetReference> targetReferences;
    std::unique_ptr<Ext::ScopedRenderTarget> value;
};

std::unordered_map<Microsoft::Xna::Framework::Graphics::GraphicsDevice*, std::vector<uint64_t>>
    scopeStacks;
uint64_t nextScopeToken = 1U;

class ScopeStackReservation final {
public:
    ScopeStackReservation(
        Microsoft::Xna::Framework::Graphics::GraphicsDevice* const device,
        const uint64_t token)
        : device_(device), token_(token)
    {
        scopeStacks[device_].push_back(token_);
    }

    ScopeStackReservation(const ScopeStackReservation&) = delete;
    ScopeStackReservation& operator=(const ScopeStackReservation&) = delete;

    ~ScopeStackReservation()
    {
        if (!committed_) {
            Pop();
        }
    }

    void Commit() noexcept { committed_ = true; }

private:
    void Pop() noexcept
    {
        const auto found = scopeStacks.find(device_);
        if (found == scopeStacks.end()) {
            return;
        }
        if (!found->second.empty() && found->second.back() == token_) {
            found->second.pop_back();
        }
        if (found->second.empty()) {
            scopeStacks.erase(found);
        }
    }

    Microsoft::Xna::Framework::Graphics::GraphicsDevice* device_;
    uint64_t token_;
    bool committed_ = false;
};

[[nodiscard]] CNA_Result GetStorageBuffer(
    const CNA_Handle handle,
    std::shared_ptr<StorageBufferResource>* const outBuffer)
{
    const CNA_Result result =
        GetRuntimeHandles().Get(handle, ObjectKind::StorageBuffer, outBuffer);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The StorageBuffer handle is invalid for this call.");
}

[[nodiscard]] CNA_Result GetComputeShader(
    const CNA_Handle handle,
    std::shared_ptr<ComputeShaderResource>* const outShader)
{
    const CNA_Result result =
        GetRuntimeHandles().Get(handle, ObjectKind::ComputeShader, outShader);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The ComputeShader handle is invalid for this call.");
}

template<typename TResource>
[[nodiscard]] CNA_Result GetEngineResource(
    const CNA_Handle handle,
    const ObjectKind kind,
    const char* const name,
    std::shared_ptr<TResource>* const outResource)
{
    const CNA_Result result = GetRuntimeHandles().Get(handle, kind, outResource);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        std::string("The ") + name + " handle is invalid for this call.");
}

[[nodiscard]] CNA_Result RetainScopeTarget(
    const CNA_Handle handle,
    const CNA_Handle parentGame,
    std::vector<ScopeTargetReference>* const outReferences)
{
    std::shared_ptr<void> owner;
    uint64_t* referenceCount = nullptr;
    if (const CNA_Result result = ResolveRenderTargetScopeReference(
            handle, parentGame, &owner, &referenceCount);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    outReferences->emplace_back(std::move(owner), referenceCount);
    return CNA_RESULT_SUCCESS;
}

// The element routes exist to reproduce StorageBufferT<T>, so they refuse a buffer that was never
// given an element shape rather than guessing one. A guess here would silently reinterpret bytes.
[[nodiscard]] CNA_Result RequireElementShape(
    const StorageBufferResource& buffer,
    const uint64_t elementByteSize)
{
    if (buffer.elementByteSize == 0U) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "This storage buffer was created by byte size and has no element shape; use the byte "
            "routes.");
    }
    if (elementByteSize != buffer.elementByteSize) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The element size does not match the size this storage buffer was created with.");
    }
    return CNA_RESULT_SUCCESS;
}


[[nodiscard]] Microsoft::Xna::Framework::Matrix ToNativeMatrix(const CNA_Matrix& value) noexcept
{
    Microsoft::Xna::Framework::Matrix result;
    result.M11 = value.m11; result.M12 = value.m12; result.M13 = value.m13; result.M14 = value.m14;
    result.M21 = value.m21; result.M22 = value.m22; result.M23 = value.m23; result.M24 = value.m24;
    result.M31 = value.m31; result.M32 = value.m32; result.M33 = value.m33; result.M34 = value.m34;
    result.M41 = value.m41; result.M42 = value.m42; result.M43 = value.m43; result.M44 = value.m44;
    return result;
}

// --- CBIND-084C: passes and material binding -------------------------------------------------

struct FullscreenPassResource final {
    std::shared_ptr<Ext::FullscreenPass> value;
    CNA_Handle parentGame;
};

// One resource for every concrete pass, because what crosses this ABI is the abstract contract's
// operations rather than the class implementing them. `effectPass` is non-null exactly when the
// pass is an EffectPass, which is what lets the two effect-only routes refuse a BlitPass by
// argument rather than by handle kind.
//
// DEVIATION, deliberate: EffectPass's unique_ptr constructor transfers ownership of the effect.
// A C handle's object lives in the registry as a shared_ptr, and there is no way back to a
// unique_ptr, so `cna_post_process_effect_pass_create_owning` reproduces the *observable* contract instead of
// the C++ mechanism: it consumes the caller's handle, retains the effect resource here, and
// releases it when the pass is destroyed. Destroying the pass therefore destroys the effect, which
// is exactly what the canonical constructor promises.
struct PostProcessPassResource final {
    std::shared_ptr<Ext::PostProcessPass> value;
    Ext::EffectPass* effectPass;
    CNA_Handle parentGame;
    // Kept alive so a borrowed effect cannot dangle behind the pass that draws through it.
    std::shared_ptr<void> effectRetention;
    // Set only by the owning constructor: released with the pass.
    CNA_Handle ownedEffect;
    // CountedBorrow's contract: a borrowed effect handle keeps this non-zero, and destroy refuses
    // while it is, so a handed-out effect can never outlive the pass it was borrowed from.
    uint64_t activeBorrowCount = 0U;
};

[[nodiscard]] CNA_Result GetEffectForPass(
    const CNA_Handle handle,
    std::shared_ptr<EffectResource>* const outEffect)
{
    const CNA_Result result = GetRuntimeHandles().Get(handle, ObjectKind::Effect, outEffect);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The Effect handle is invalid for this call.");
}

[[nodiscard]] CNA_Result ResolveTexture2DArgument(
    const CNA_Handle handle,
    const char* const what,
    Microsoft::Xna::Framework::Graphics::Texture2D** const outTexture,
    std::shared_ptr<Texture2DResource>* const outRetention)
{
    *outTexture = nullptr;
    if (handle == CNA_INVALID_HANDLE) {
        return CNA_RESULT_SUCCESS;
    }
    if (const CNA_Result result = GetOwnedTexture2D(handle, outRetention);
        result != CNA_RESULT_SUCCESS) {
        (void)what;
        return result;
    }
    *outTexture = (*outRetention)->value.get();
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ResolveRenderTarget2DArgument(
    const CNA_Handle handle,
    Microsoft::Xna::Framework::Graphics::RenderTarget2D** const outTarget,
    std::shared_ptr<Texture2DResource>* const outRetention)
{
    *outTarget = nullptr;
    if (handle == CNA_INVALID_HANDLE) {
        return CNA_RESULT_SUCCESS;
    }
    if (const CNA_Result result = GetOwnedTexture2D(handle, outRetention);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    ObjectKind kind = ObjectKind::Unknown;
    if (GetRuntimeHandles().GetKind(handle, &kind) != CNA_RESULT_SUCCESS ||
        kind != ObjectKind::RenderTarget2D) {
        return Fail(
            CNA_RESULT_INVALID_HANDLE,
            CNA_ERROR_CATEGORY_HANDLE,
            "The destination handle is not a RenderTarget2D.");
    }
    *outTarget = static_cast<Microsoft::Xna::Framework::Graphics::RenderTarget2D*>(
        (*outRetention)->value.get());
    return CNA_RESULT_SUCCESS;
}

// Every handle the context names is resolved and retained for the duration of one apply, so a
// pass can never read a texture the caller released between filling the struct and applying it.
struct ResolvedPostProcessContext final {
    Ext::PostProcessContext value;
    std::shared_ptr<Texture2DResource> sourceRetention;
    std::shared_ptr<Texture2DResource> depthRetention;
    std::shared_ptr<Texture2DResource> normalsRetention;
    std::shared_ptr<Texture2DResource> velocityRetention;
    std::shared_ptr<Texture2DResource> destinationRetention;
};

[[nodiscard]] CNA_Result ResolvePostProcessContext(
    const CNA_PostProcessContext& context,
    ResolvedPostProcessContext* const out)
{
    if (context.struct_size != static_cast<uint32_t>(sizeof(CNA_PostProcessContext)) ||
        context.struct_version != UINT32_C(1)) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The post-process context was not initialized by cna_post_process_context_init.");
    }
    if (const CNA_Result result =
            ValidateCanonicalBool(context.has_previous_frame, "has_previous_frame");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ResolveTexture2DArgument(
            context.source, "source", &out->value.source, &out->sourceRetention);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ResolveTexture2DArgument(
            context.source_depth, "source_depth", &out->value.sourceDepth, &out->depthRetention);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ResolveTexture2DArgument(
            context.source_normals,
            "source_normals",
            &out->value.sourceNormals,
            &out->normalsRetention);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ResolveTexture2DArgument(
            context.source_velocity,
            "source_velocity",
            &out->value.sourceVelocity,
            &out->velocityRetention);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result result = ResolveRenderTarget2DArgument(
            context.destination, &out->value.destination, &out->destinationRetention);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    out->value.width = static_cast<int>(context.width);
    out->value.height = static_cast<int>(context.height);
    out->value.elapsedSeconds = context.elapsed_seconds;
    out->value.nearPlane = context.near_plane;
    out->value.farPlane = context.far_plane;
    out->value.projection = ToNativeMatrix(context.projection);
    out->value.inverseProjection = ToNativeMatrix(context.inverse_projection);
    out->value.inverseView = ToNativeMatrix(context.inverse_view);
    out->value.previousViewProjection = ToNativeMatrix(context.previous_view_projection);
    out->value.hasPreviousFrame = context.has_previous_frame == CNA_TRUE;
    // CBIND-088 owns RenderPipelineSettings; until its C form is the whole canonical type, a
    // pass applied from C gets no settings and uses its own defaults, which is what null means.
    out->value.settings = nullptr;
    return CNA_RESULT_SUCCESS;
}

#endif // CNA_CNAEXT

} // namespace

CNA_Result cna_engine_layer_get_version(int32_t* const outVersion)
{
#ifdef CNA_CNAEXT
    return StoreValue(outVersion, static_cast<int32_t>(CNA::Graphics::getEngineLayerVersion()));
#else
    return StoreValue(outVersion, INT32_C(0));
#endif
}

CNA_Result cna_engine_layer_copy_version_string(
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
#ifdef CNA_CNAEXT
    return CopyFormattedString(destination, capacity, outBytes, [] {
        return CNA::Graphics::getEngineLayerVersionString();
    });
#else
    (void)destination;
    (void)capacity;
    if (outBytes != nullptr) {
        *outBytes = UINT64_C(0);
    }
    return ExtensionUnavailable();
#endif
}

CNA_Result cna_graphics_memory_barrier_has(
    const CNA_GraphicsMemoryBarrier mask,
    const CNA_GraphicsMemoryBarrier bit,
    CNA_Bool* const outContains)
{
    return StoreValue(outContains, static_cast<CNA_Bool>((mask & bit) == bit ? CNA_TRUE : CNA_FALSE));
}

CNA_Result cna_post_process_context_init(CNA_PostProcessContext* const outContext)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outContext == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The post-process context output is null.");
        }
        CNA_PostProcessContext defaults;
        std::memset(&defaults, 0, sizeof(defaults));
        defaults.struct_size = static_cast<uint32_t>(sizeof(CNA_PostProcessContext));
        defaults.struct_version = UINT32_C(1);
        defaults.source = CNA_INVALID_HANDLE;
        defaults.source_depth = CNA_INVALID_HANDLE;
        defaults.source_normals = CNA_INVALID_HANDLE;
        defaults.source_velocity = CNA_INVALID_HANDLE;
        defaults.destination = CNA_INVALID_HANDLE;
        defaults.has_previous_frame = CNA_FALSE;
        // The canonical struct value-initializes its matrices, and `Matrix()` is **all zeros** --
        // not the identity. An earlier draft of this route wrote the identity because that reads
        // like the friendlier default; it is the wrong one, because a pass reading a defaulted
        // context would then see a different projection from the C++ caller's. The memset above
        // already leaves the zeros, so nothing more is needed here.
        *outContext = defaults;
        return CNA_RESULT_SUCCESS;
    });
}

namespace {

// The canonical structs are aggregates with member initializers, so a default-constructed value is
// the defaults themselves. Where the layer is present these are checked against it rather than
// transcribed; where it is not, the values below are the only copy and the assertions are absent
// -- the same arrangement cna_pbr_material_init already uses.
[[nodiscard]] CNA_Vector3 Vec3(const float x, const float y, const float z) noexcept
{
    CNA_Vector3 value;
    value.x = x;
    value.y = y;
    value.z = z;
    return value;
}

} // namespace

CNA_Result cna_directional_light_ext_init(CNA_DirectionalLightEXT* const outLight)
{
    CNA_DirectionalLightEXT defaults;
    std::memset(&defaults, 0, sizeof(defaults));
    defaults.struct_size = static_cast<uint32_t>(sizeof(CNA_DirectionalLightEXT));
    defaults.struct_version = UINT32_C(1);
    defaults.direction = Vec3(0.0F, -1.0F, 0.0F);
    defaults.color = Vec3(1.0F, 1.0F, 1.0F);
    defaults.intensity = 1.0F;
    defaults.casts_shadows = CNA_FALSE;
#ifdef CNA_CNAEXT
    {
        const CNA::Graphics::DirectionalLightEXT canonical;
        if (canonical.Direction.X != defaults.direction.x ||
            canonical.Direction.Y != defaults.direction.y ||
            canonical.Direction.Z != defaults.direction.z ||
            canonical.Color.X != defaults.color.x || canonical.Color.Y != defaults.color.y ||
            canonical.Color.Z != defaults.color.z || canonical.Intensity != defaults.intensity ||
            canonical.CastsShadows != (defaults.casts_shadows == CNA_TRUE)) {
            return Fail(
                CNA_RESULT_INTERNAL,
                CNA_ERROR_CATEGORY_INTERNAL,
                "The C directional-light defaults disagree with the canonical structure.");
        }
    }
#endif
    return StoreValue(outLight, defaults);
}

CNA_Result cna_point_light_ext_init(CNA_PointLightEXT* const outLight)
{
    CNA_PointLightEXT defaults;
    std::memset(&defaults, 0, sizeof(defaults));
    defaults.struct_size = static_cast<uint32_t>(sizeof(CNA_PointLightEXT));
    defaults.struct_version = UINT32_C(1);
    defaults.position = Vec3(0.0F, 0.0F, 0.0F);
    defaults.color = Vec3(1.0F, 1.0F, 1.0F);
    defaults.intensity = 1.0F;
    defaults.range = 20.0F;
    defaults.casts_shadows = CNA_FALSE;
#ifdef CNA_CNAEXT
    {
        const CNA::Graphics::PointLightEXT canonical;
        if (canonical.Position.X != defaults.position.x ||
            canonical.Color.X != defaults.color.x || canonical.Intensity != defaults.intensity ||
            canonical.Range != defaults.range ||
            canonical.CastsShadows != (defaults.casts_shadows == CNA_TRUE)) {
            return Fail(
                CNA_RESULT_INTERNAL,
                CNA_ERROR_CATEGORY_INTERNAL,
                "The C point-light defaults disagree with the canonical structure.");
        }
    }
#endif
    return StoreValue(outLight, defaults);
}

CNA_Result cna_spot_light_ext_init(CNA_SpotLightEXT* const outLight)
{
    CNA_SpotLightEXT defaults;
    std::memset(&defaults, 0, sizeof(defaults));
    defaults.struct_size = static_cast<uint32_t>(sizeof(CNA_SpotLightEXT));
    defaults.struct_version = UINT32_C(1);
    defaults.position = Vec3(0.0F, 0.0F, 0.0F);
    defaults.direction = Vec3(0.0F, -1.0F, 0.0F);
    defaults.color = Vec3(1.0F, 1.0F, 1.0F);
    defaults.intensity = 1.0F;
    defaults.range = 20.0F;
    defaults.inner_angle = 0.35F;
    defaults.outer_angle = 0.5F;
    defaults.casts_shadows = CNA_FALSE;
#ifdef CNA_CNAEXT
    {
        const CNA::Graphics::SpotLightEXT canonical;
        if (canonical.Direction.Y != defaults.direction.y ||
            canonical.Intensity != defaults.intensity || canonical.Range != defaults.range ||
            canonical.InnerAngle != defaults.inner_angle ||
            canonical.OuterAngle != defaults.outer_angle ||
            canonical.CastsShadows != (defaults.casts_shadows == CNA_TRUE)) {
            return Fail(
                CNA_RESULT_INTERNAL,
                CNA_ERROR_CATEGORY_INTERNAL,
                "The C spot-light defaults disagree with the canonical structure.");
        }
    }
#endif
    return StoreValue(outLight, defaults);
}

CNA_Result cna_punctual_light_ext_init(CNA_PunctualLightEXT* const outLight)
{
    using Microsoft::Xna::Framework::Graphics::PunctualLightEXT;
    using Microsoft::Xna::Framework::Graphics::PunctualLightKindEXT;
    static_assert(
        static_cast<uint32_t>(PunctualLightKindEXT::None) == CNA_PUNCTUAL_LIGHT_KIND_EXT_NONE &&
        static_cast<uint32_t>(PunctualLightKindEXT::Point) == CNA_PUNCTUAL_LIGHT_KIND_EXT_POINT &&
        static_cast<uint32_t>(PunctualLightKindEXT::Spot) == CNA_PUNCTUAL_LIGHT_KIND_EXT_SPOT);

    CNA_PunctualLightEXT defaults;
    std::memset(&defaults, 0, sizeof(defaults));
    defaults.struct_size = static_cast<uint32_t>(sizeof(CNA_PunctualLightEXT));
    defaults.struct_version = UINT32_C(1);
    const PunctualLightEXT canonical;
    defaults.kind = static_cast<CNA_PunctualLightKindEXT>(canonical.Kind);
    defaults.position = Vec3(canonical.Position.X, canonical.Position.Y, canonical.Position.Z);
    defaults.direction = Vec3(canonical.Direction.X, canonical.Direction.Y, canonical.Direction.Z);
    defaults.diffuse_color =
        Vec3(canonical.DiffuseColor.X, canonical.DiffuseColor.Y, canonical.DiffuseColor.Z);
    defaults.range = canonical.Range;
    defaults.inner_angle = canonical.InnerAngle;
    defaults.outer_angle = canonical.OuterAngle;
    defaults.shadow_depth_bias = canonical.ShadowDepthBias;
    // Both shadow textures default to null, and a handle's spelling of null is the invalid handle.
    defaults.shadow_cube = CNA_INVALID_HANDLE;
    defaults.shadow_map = CNA_INVALID_HANDLE;
    // `ShadowViewProjection{}` is a value-initialized Matrix, which is all zeros; the memset above
    // already left exactly that.
    return StoreValue(outLight, defaults);
}

CNA_Result cna_shadow_cascade_state_ext_init(CNA_ShadowCascadeStateEXT* const outState)
{
    using Microsoft::Xna::Framework::Graphics::ShadowCascadeStateEXT;
    static_assert(ShadowCascadeStateEXT::kMaxCascades == CNA_SHADOW_CASCADE_MAX_EXT);

    CNA_ShadowCascadeStateEXT defaults;
    std::memset(&defaults, 0, sizeof(defaults));
    defaults.struct_size = static_cast<uint32_t>(sizeof(CNA_ShadowCascadeStateEXT));
    defaults.struct_version = UINT32_C(1);
    const ShadowCascadeStateEXT canonical;
    defaults.count = static_cast<int32_t>(canonical.Count);
    defaults.blend_band = canonical.BlendBand;
    defaults.debug_tint = canonical.DebugTint ? CNA_TRUE : CNA_FALSE;
    // The canonical arrays are value-initialized, and `Matrix()` is all zeros -- so these stay
    // zero. That matters: with `count` at 0 no cascade transform is read at all, and inventing an
    // identity here would make a defaulted C state differ from a defaulted C++ one for no gain.
    for (int cascade = 0; cascade < CNA_SHADOW_CASCADE_MAX_EXT; ++cascade) {
        defaults.split_distance[cascade] = canonical.SplitDistance[cascade];
    }
    return StoreValue(outState, defaults);
}

CNA_Result cna_graphics_device_supports_shadow_sampling_ext(
    const CNA_Handle graphicsDeviceHandle,
    CNA_Bool* const outSupported)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSupported == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The shadow-sampling support output is null.");
        }
        std::shared_ptr<CNA::C::Detail::BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = CNA::C::Detail::GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outSupported =
            graphicsDevice->value->SupportsShadowSamplingEXT() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

#ifndef CNA_CNAEXT

CNA_Result cna_storage_buffer_create(
    const CNA_Handle graphicsDevice,
    const uint64_t byteSize,
    CNA_StorageBufferHandle* const outBuffer)
{
    (void)graphicsDevice;
    (void)byteSize;
    if (outBuffer != nullptr) {
        *outBuffer = CNA_INVALID_HANDLE;
    }
    return ExtensionUnavailable();
}

CNA_Result cna_storage_buffer_create_typed(
    const CNA_Handle graphicsDevice,
    const uint64_t elementCount,
    const uint64_t elementByteSize,
    CNA_StorageBufferHandle* const outBuffer)
{
    (void)graphicsDevice;
    (void)elementCount;
    (void)elementByteSize;
    if (outBuffer != nullptr) {
        *outBuffer = CNA_INVALID_HANDLE;
    }
    return ExtensionUnavailable();
}

CNA_Result cna_storage_buffer_set_bytes(
    const CNA_StorageBufferHandle buffer,
    const void* const data,
    const uint64_t byteSize)
{
    (void)buffer;
    (void)data;
    (void)byteSize;
    return ExtensionUnavailable();
}

CNA_Result cna_storage_buffer_get_bytes(
    const CNA_StorageBufferHandle buffer,
    void* const destination,
    const uint64_t byteSize)
{
    (void)buffer;
    (void)destination;
    (void)byteSize;
    return ExtensionUnavailable();
}

CNA_Result cna_storage_buffer_get_byte_size(
    const CNA_StorageBufferHandle buffer,
    uint64_t* const outByteSize)
{
    (void)buffer;
    (void)outByteSize;
    return ExtensionUnavailable();
}

CNA_Result cna_storage_buffer_set_elements(
    const CNA_StorageBufferHandle buffer,
    const void* const data,
    const uint64_t elementCount,
    const uint64_t elementByteSize)
{
    (void)buffer;
    (void)data;
    (void)elementCount;
    (void)elementByteSize;
    return ExtensionUnavailable();
}

CNA_Result cna_storage_buffer_get_elements(
    const CNA_StorageBufferHandle buffer,
    void* const destination,
    const uint64_t elementCount,
    const uint64_t elementByteSize)
{
    (void)buffer;
    (void)destination;
    (void)elementCount;
    (void)elementByteSize;
    return ExtensionUnavailable();
}

CNA_Result cna_storage_buffer_get_element_count(
    const CNA_StorageBufferHandle buffer,
    uint64_t* const outElementCount)
{
    (void)buffer;
    (void)outElementCount;
    return ExtensionUnavailable();
}

CNA_Result cna_storage_buffer_get_element_byte_size(
    const CNA_StorageBufferHandle buffer,
    uint64_t* const outElementByteSize)
{
    (void)buffer;
    (void)outElementByteSize;
    return ExtensionUnavailable();
}

CNA_Result cna_storage_buffer_destroy(const CNA_StorageBufferHandle buffer)
{
    (void)buffer;
    return ExtensionUnavailable();
}

CNA_Result cna_compute_shader_create(
    const CNA_Handle graphicsDevice,
    const CNA_StringView source,
    CNA_ComputeShaderHandle* const outShader)
{
    (void)graphicsDevice;
    (void)source;
    if (outShader != nullptr) {
        *outShader = CNA_INVALID_HANDLE;
    }
    return ExtensionUnavailable();
}

CNA_Result cna_compute_shader_set_uniform_int(
    const CNA_ComputeShaderHandle shader,
    const CNA_StringView name,
    const int32_t value)
{
    (void)shader;
    (void)name;
    (void)value;
    return ExtensionUnavailable();
}

CNA_Result cna_compute_shader_set_uniform_float(
    const CNA_ComputeShaderHandle shader,
    const CNA_StringView name,
    const float value)
{
    (void)shader;
    (void)name;
    (void)value;
    return ExtensionUnavailable();
}

CNA_Result cna_compute_shader_bind_storage_buffer(
    const CNA_ComputeShaderHandle shader,
    const int32_t binding,
    const CNA_StorageBufferHandle buffer)
{
    (void)shader;
    (void)binding;
    (void)buffer;
    return ExtensionUnavailable();
}

CNA_Result cna_compute_shader_bind_texture(
    const CNA_ComputeShaderHandle shader,
    const int32_t unit,
    const CNA_StringView samplerName,
    const CNA_Handle texture)
{
    (void)shader;
    (void)unit;
    (void)samplerName;
    (void)texture;
    return ExtensionUnavailable();
}

CNA_Result cna_compute_shader_is_image_binding_supported(
    const CNA_ComputeShaderHandle shader,
    CNA_Bool* const outSupported)
{
    (void)shader;
    (void)outSupported;
    return ExtensionUnavailable();
}

CNA_Result cna_compute_shader_bind_image(
    const CNA_ComputeShaderHandle shader,
    const int32_t unit,
    const CNA_Handle texture,
    const CNA_GraphicsImageAccess access)
{
    (void)shader;
    (void)unit;
    (void)texture;
    (void)access;
    return ExtensionUnavailable();
}

CNA_Result cna_compute_shader_dispatch(
    const CNA_ComputeShaderHandle shader,
    const int32_t groupsX,
    const int32_t groupsY,
    const int32_t groupsZ)
{
    (void)shader;
    (void)groupsX;
    (void)groupsY;
    (void)groupsZ;
    return ExtensionUnavailable();
}

CNA_Result cna_compute_shader_barrier(
    const CNA_ComputeShaderHandle shader,
    const CNA_GraphicsMemoryBarrier bits)
{
    (void)shader;
    (void)bits;
    return ExtensionUnavailable();
}

CNA_Result cna_compute_shader_is_valid(
    const CNA_ComputeShaderHandle shader,
    CNA_Bool* const outValid)
{
    (void)shader;
    (void)outValid;
    return ExtensionUnavailable();
}

CNA_Result cna_compute_shader_copy_compile_error(
    const CNA_ComputeShaderHandle shader,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    (void)shader;
    (void)destination;
    (void)capacity;
    if (outBytes != nullptr) {
        *outBytes = UINT64_C(0);
    }
    return ExtensionUnavailable();
}

CNA_Result cna_compute_shader_destroy(const CNA_ComputeShaderHandle shader)
{
    (void)shader;
    return ExtensionUnavailable();
}

CNA_Result cna_gpu_timer_create(
    const CNA_Handle graphicsDevice,
    CNA_GpuTimerHandle* const outTimer)
{
    (void)graphicsDevice;
    if (outTimer != nullptr) {
        *outTimer = CNA_INVALID_HANDLE;
    }
    return ExtensionUnavailable();
}

CNA_Result cna_gpu_timer_is_supported(
    const CNA_GpuTimerHandle timer,
    CNA_Bool* const outSupported)
{
    (void)timer;
    (void)outSupported;
    return ExtensionUnavailable();
}

CNA_Result cna_gpu_timer_copy_unsupported_reason(
    const CNA_GpuTimerHandle timer,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    (void)timer;
    (void)destination;
    (void)capacity;
    (void)outBytes;
    return ExtensionUnavailable();
}

CNA_Result cna_gpu_timer_begin(const CNA_GpuTimerHandle timer)
{
    (void)timer;
    return ExtensionUnavailable();
}

CNA_Result cna_gpu_timer_end(const CNA_GpuTimerHandle timer)
{
    (void)timer;
    return ExtensionUnavailable();
}

CNA_Result cna_gpu_timer_is_result_available(
    const CNA_GpuTimerHandle timer,
    CNA_Bool* const outAvailable)
{
    (void)timer;
    (void)outAvailable;
    return ExtensionUnavailable();
}

CNA_Result cna_gpu_timer_poll(
    const CNA_GpuTimerHandle timer,
    CNA_Bool* const outCollected)
{
    (void)timer;
    (void)outCollected;
    return ExtensionUnavailable();
}

CNA_Result cna_gpu_timer_get_last_milliseconds(
    const CNA_GpuTimerHandle timer,
    double* const outMilliseconds)
{
    (void)timer;
    (void)outMilliseconds;
    return ExtensionUnavailable();
}

CNA_Result cna_gpu_timer_get_sample_count(
    const CNA_GpuTimerHandle timer,
    int32_t* const outSampleCount)
{
    (void)timer;
    (void)outSampleCount;
    return ExtensionUnavailable();
}

CNA_Result cna_gpu_timer_is_open(
    const CNA_GpuTimerHandle timer,
    CNA_Bool* const outOpen)
{
    (void)timer;
    (void)outOpen;
    return ExtensionUnavailable();
}

CNA_Result cna_gpu_timer_destroy(const CNA_GpuTimerHandle timer)
{
    (void)timer;
    return ExtensionUnavailable();
}

CNA_Result cna_render_target_pool_create(
    const CNA_Handle graphicsDevice,
    CNA_RenderTargetPoolHandle* const outPool)
{
    (void)graphicsDevice;
    if (outPool != nullptr) {
        *outPool = CNA_INVALID_HANDLE;
    }
    return ExtensionUnavailable();
}

CNA_Result cna_render_target_pool_acquire(
    const CNA_RenderTargetPoolHandle pool,
    const int32_t width,
    const int32_t height,
    const CNA_SurfaceFormat format,
    const CNA_DepthFormat depthFormat,
    const int32_t slot,
    CNA_Handle* const outRenderTarget)
{
    (void)pool;
    (void)width;
    (void)height;
    (void)format;
    (void)depthFormat;
    (void)slot;
    if (outRenderTarget != nullptr) {
        *outRenderTarget = CNA_INVALID_HANDLE;
    }
    return ExtensionUnavailable();
}

CNA_Result cna_render_target_pool_reset(const CNA_RenderTargetPoolHandle pool)
{
    (void)pool;
    return ExtensionUnavailable();
}

CNA_Result cna_render_target_pool_get_target_count(
    const CNA_RenderTargetPoolHandle pool,
    uint64_t* const outTargetCount)
{
    (void)pool;
    (void)outTargetCount;
    return ExtensionUnavailable();
}

CNA_Result cna_render_target_pool_get_estimated_bytes(
    const CNA_RenderTargetPoolHandle pool,
    uint64_t* const outBytes)
{
    (void)pool;
    (void)outBytes;
    return ExtensionUnavailable();
}

CNA_Result cna_render_target_pool_destroy(const CNA_RenderTargetPoolHandle pool)
{
    (void)pool;
    return ExtensionUnavailable();
}

CNA_Result cna_shader_effect_factory_create(
    const CNA_Handle graphicsDevice,
    CNA_ShaderEffectFactoryHandle* const outFactory)
{
    (void)graphicsDevice;
    if (outFactory != nullptr) {
        *outFactory = CNA_INVALID_HANDLE;
    }
    return ExtensionUnavailable();
}

CNA_Result cna_shader_effect_factory_acquire(
    const CNA_ShaderEffectFactoryHandle factory,
    const CNA_StringView name,
    const CNA_StringView vertexSource,
    const CNA_StringView fragmentSource,
    CNA_EffectHandle* const outEffect)
{
    (void)factory;
    (void)name;
    (void)vertexSource;
    (void)fragmentSource;
    if (outEffect != nullptr) {
        *outEffect = CNA_INVALID_HANDLE;
    }
    return ExtensionUnavailable();
}

CNA_Result cna_shader_effect_factory_contains(
    const CNA_ShaderEffectFactoryHandle factory,
    const CNA_StringView name,
    CNA_Bool* const outContains)
{
    (void)factory;
    (void)name;
    (void)outContains;
    return ExtensionUnavailable();
}

CNA_Result cna_shader_effect_factory_get_compile_count(
    const CNA_ShaderEffectFactoryHandle factory,
    uint64_t* const outCompileCount)
{
    (void)factory;
    (void)outCompileCount;
    return ExtensionUnavailable();
}

CNA_Result cna_shader_effect_factory_clear(const CNA_ShaderEffectFactoryHandle factory)
{
    (void)factory;
    return ExtensionUnavailable();
}

CNA_Result cna_shader_effect_factory_destroy(const CNA_ShaderEffectFactoryHandle factory)
{
    (void)factory;
    return ExtensionUnavailable();
}

CNA_Result cna_scoped_render_target_begin(
    const CNA_Handle graphicsDevice,
    const CNA_Handle destination,
    CNA_ScopedRenderTargetHandle* const outScope)
{
    (void)graphicsDevice;
    (void)destination;
    if (outScope != nullptr) {
        *outScope = CNA_INVALID_HANDLE;
    }
    return ExtensionUnavailable();
}

CNA_Result cna_scoped_render_target_get_has_recorded_previous(
    const CNA_ScopedRenderTargetHandle scope,
    CNA_Bool* const outRecorded)
{
    (void)scope;
    (void)outRecorded;
    return ExtensionUnavailable();
}

CNA_Result cna_scoped_render_target_end(const CNA_ScopedRenderTargetHandle scope)
{
    (void)scope;
    return ExtensionUnavailable();
}

CNA_Result cna_fullscreen_pass_create(
    const CNA_Handle graphicsDevice, CNA_FullscreenPassHandle* const outPass)
{
    (void)graphicsDevice;
    if (outPass != nullptr) { *outPass = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_fullscreen_pass_draw(
    const CNA_FullscreenPassHandle pass, const CNA_Handle source, const CNA_Handle destination,
    const CNA_EffectHandle effect, const int32_t width, const int32_t height,
    const CNA_SamplerState* const sampler)
{
    (void)pass; (void)source; (void)destination; (void)effect; (void)width; (void)height;
    (void)sampler;
    return ExtensionUnavailable();
}

CNA_Result cna_fullscreen_pass_draw_over_current_target(
    const CNA_FullscreenPassHandle pass, const CNA_Handle source, const CNA_EffectHandle effect,
    const int32_t width, const int32_t height, const CNA_SamplerState* const sampler)
{
    (void)pass; (void)source; (void)effect; (void)width; (void)height; (void)sampler;
    return ExtensionUnavailable();
}

CNA_Result cna_fullscreen_pass_destroy(const CNA_FullscreenPassHandle pass)
{
    (void)pass;
    return ExtensionUnavailable();
}

CNA_Result cna_blit_pass_create(
    const CNA_Handle graphicsDevice, CNA_PostProcessPassHandle* const outPass)
{
    (void)graphicsDevice;
    if (outPass != nullptr) { *outPass = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_post_process_effect_pass_create(
    const CNA_Handle graphicsDevice, const CNA_EffectHandle effect, const CNA_StringView name,
    CNA_PostProcessPassHandle* const outPass)
{
    (void)graphicsDevice; (void)effect; (void)name;
    if (outPass != nullptr) { *outPass = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_post_process_effect_pass_create_owning(
    const CNA_Handle graphicsDevice, const CNA_EffectHandle effect, const CNA_StringView name,
    CNA_PostProcessPassHandle* const outPass)
{
    (void)graphicsDevice; (void)effect; (void)name;
    if (outPass != nullptr) { *outPass = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_post_process_effect_pass_get_effect(
    const CNA_PostProcessPassHandle pass, CNA_EffectHandle* const outEffect)
{
    (void)pass;
    if (outEffect != nullptr) { *outEffect = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_post_process_effect_pass_set_effect(
    const CNA_PostProcessPassHandle pass, const CNA_EffectHandle effect)
{
    (void)pass; (void)effect;
    return ExtensionUnavailable();
}

CNA_Result cna_post_process_pass_apply(
    const CNA_PostProcessPassHandle pass, const CNA_PostProcessContext* const context)
{
    (void)pass; (void)context;
    return ExtensionUnavailable();
}

CNA_Result cna_post_process_pass_copy_name(
    const CNA_PostProcessPassHandle pass, char* const destination, const uint64_t capacity,
    uint64_t* const outBytes)
{
    (void)pass; (void)destination; (void)capacity;
    if (outBytes != nullptr) { *outBytes = UINT64_C(0); }
    return ExtensionUnavailable();
}

CNA_Result cna_post_process_pass_is_supported(
    const CNA_PostProcessPassHandle pass, const CNA_Handle graphicsDevice,
    CNA_Bool* const outSupported)
{
    (void)pass; (void)graphicsDevice; (void)outSupported;
    return ExtensionUnavailable();
}

CNA_Result cna_post_process_pass_destroy(const CNA_PostProcessPassHandle pass)
{
    (void)pass;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_effect_apply_material(
    const CNA_EffectHandle effect, const CNA_PbrMaterialEXT* const material)
{
    (void)effect; (void)material;
    return ExtensionUnavailable();
}

CNA_Result cna_skinned_pbr_effect_apply_material(
    const CNA_EffectHandle effect, const CNA_PbrMaterialEXT* const material)
{
    (void)effect; (void)material;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_effect_extract_material(
    const CNA_EffectHandle effect, CNA_PbrMaterialEXT* const outMaterial)
{
    (void)effect; (void)outMaterial;
    return ExtensionUnavailable();
}

CNA_Result cna_skinned_pbr_effect_extract_material(
    const CNA_EffectHandle effect, CNA_PbrMaterialEXT* const outMaterial)
{
    (void)effect; (void)outMaterial;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_apply_state(
    const CNA_PbrMaterialEXT* const material, const CNA_Handle graphicsDevice)
{
    (void)material; (void)graphicsDevice;
    return ExtensionUnavailable();
}


CNA_Result cna_shadow_map_create(
    const CNA_Handle graphicsDevice, const CNA_ShadowQuality quality,
    CNA_ShadowMapHandle* const outShadowMap)
{
    (void)graphicsDevice; (void)quality;
    if (outShadowMap != nullptr) { *outShadowMap = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_shadow_map_is_supported(const CNA_ShadowMapHandle m, CNA_Bool* const o)
{ (void)m; (void)o; return ExtensionUnavailable(); }

CNA_Result cna_shadow_map_begin(
    const CNA_ShadowMapHandle m, const CNA_DirectionalLightEXT* const l,
    const CNA_BoundingBox* const b)
{ (void)m; (void)l; (void)b; return ExtensionUnavailable(); }

CNA_Result cna_shadow_map_end(const CNA_ShadowMapHandle m)
{ (void)m; return ExtensionUnavailable(); }

CNA_Result cna_shadow_map_get_caster_effect(const CNA_ShadowMapHandle m, CNA_EffectHandle* const o)
{ (void)m; if (o != nullptr) { *o = CNA_INVALID_HANDLE; } return ExtensionUnavailable(); }

CNA_Result cna_shadow_map_get_skinned_caster_effect(
    const CNA_ShadowMapHandle m, CNA_EffectHandle* const o)
{ (void)m; if (o != nullptr) { *o = CNA_INVALID_HANDLE; } return ExtensionUnavailable(); }

CNA_Result cna_shadow_map_apply_caster(const CNA_ShadowMapHandle m)
{ (void)m; return ExtensionUnavailable(); }

CNA_Result cna_shadow_map_apply_skinned_caster(
    const CNA_ShadowMapHandle m, const CNA_Matrix* const b, const uint64_t c, const int32_t w)
{ (void)m; (void)b; (void)c; (void)w; return ExtensionUnavailable(); }

CNA_Result cna_shadow_map_get_shadow_texture(const CNA_ShadowMapHandle m, CNA_Handle* const o)
{ (void)m; if (o != nullptr) { *o = CNA_INVALID_HANDLE; } return ExtensionUnavailable(); }

CNA_Result cna_shadow_map_get_light_view_projection(
    const CNA_ShadowMapHandle m, CNA_Matrix* const o)
{ (void)m; (void)o; return ExtensionUnavailable(); }

CNA_Result cna_shadow_map_get_size(const CNA_ShadowMapHandle m, int32_t* const o)
{ (void)m; (void)o; return ExtensionUnavailable(); }

CNA_Result cna_shadow_map_get_quality(const CNA_ShadowMapHandle m, CNA_ShadowQuality* const o)
{ (void)m; (void)o; return ExtensionUnavailable(); }

CNA_Result cna_shadow_map_get_depth_bias(const CNA_ShadowMapHandle m, float* const o)
{ (void)m; (void)o; return ExtensionUnavailable(); }

CNA_Result cna_shadow_map_set_depth_bias(const CNA_ShadowMapHandle m, const float b)
{ (void)m; (void)b; return ExtensionUnavailable(); }

CNA_Result cna_shadow_map_get_filter_radius(const CNA_ShadowMapHandle m, int32_t* const o)
{ (void)m; (void)o; return ExtensionUnavailable(); }

CNA_Result cna_shadow_map_compute_light_view(
    const CNA_DirectionalLightEXT* const l, const CNA_BoundingBox* const b, CNA_Matrix* const o)
{ (void)l; (void)b; (void)o; return ExtensionUnavailable(); }

CNA_Result cna_shadow_map_compute_light_projection(
    const CNA_Matrix* const v, const CNA_BoundingBox* const b, CNA_Matrix* const o)
{ (void)v; (void)b; (void)o; return ExtensionUnavailable(); }

CNA_Result cna_shadow_map_size_for_quality(const CNA_ShadowQuality q, int32_t* const o)
{ (void)q; (void)o; return ExtensionUnavailable(); }

CNA_Result cna_shadow_map_filter_radius_for_quality(const CNA_ShadowQuality q, int32_t* const o)
{ (void)q; (void)o; return ExtensionUnavailable(); }

CNA_Result cna_shadow_map_destroy(const CNA_ShadowMapHandle m)
{ (void)m; return ExtensionUnavailable(); }

CNA_Result cna_spot_shadow_map_create(
    const CNA_Handle d, const CNA_ShadowQuality q, CNA_SpotShadowMapHandle* const o)
{ (void)d; (void)q; if (o != nullptr) { *o = CNA_INVALID_HANDLE; } return ExtensionUnavailable(); }

CNA_Result cna_spot_shadow_map_is_supported(const CNA_SpotShadowMapHandle m, CNA_Bool* const o)
{ (void)m; (void)o; return ExtensionUnavailable(); }

CNA_Result cna_spot_shadow_map_begin(const CNA_SpotShadowMapHandle m, const CNA_SpotLightEXT* const l)
{ (void)m; (void)l; return ExtensionUnavailable(); }

CNA_Result cna_spot_shadow_map_end(const CNA_SpotShadowMapHandle m)
{ (void)m; return ExtensionUnavailable(); }

CNA_Result cna_spot_shadow_map_get_shadow_texture(
    const CNA_SpotShadowMapHandle m, CNA_Handle* const o)
{ (void)m; if (o != nullptr) { *o = CNA_INVALID_HANDLE; } return ExtensionUnavailable(); }

CNA_Result cna_spot_shadow_map_get_caster_effect(
    const CNA_SpotShadowMapHandle m, CNA_EffectHandle* const o)
{ (void)m; if (o != nullptr) { *o = CNA_INVALID_HANDLE; } return ExtensionUnavailable(); }

CNA_Result cna_spot_shadow_map_get_size(const CNA_SpotShadowMapHandle m, int32_t* const o)
{ (void)m; (void)o; return ExtensionUnavailable(); }

CNA_Result cna_spot_shadow_map_get_quality(
    const CNA_SpotShadowMapHandle m, CNA_ShadowQuality* const o)
{ (void)m; (void)o; return ExtensionUnavailable(); }

CNA_Result cna_spot_shadow_map_get_light_view_projection(
    const CNA_SpotShadowMapHandle m, CNA_Matrix* const o)
{ (void)m; (void)o; return ExtensionUnavailable(); }

CNA_Result cna_spot_shadow_map_get_light_position(
    const CNA_SpotShadowMapHandle m, CNA_Vector3* const o)
{ (void)m; (void)o; return ExtensionUnavailable(); }

CNA_Result cna_spot_shadow_map_get_light_range(const CNA_SpotShadowMapHandle m, float* const o)
{ (void)m; (void)o; return ExtensionUnavailable(); }

CNA_Result cna_spot_shadow_map_get_depth_bias(const CNA_SpotShadowMapHandle m, float* const o)
{ (void)m; (void)o; return ExtensionUnavailable(); }

CNA_Result cna_spot_shadow_map_set_depth_bias(const CNA_SpotShadowMapHandle m, const float b)
{ (void)m; (void)b; return ExtensionUnavailable(); }

CNA_Result cna_spot_shadow_map_compute_light_view(
    const CNA_SpotLightEXT* const l, CNA_Matrix* const o)
{ (void)l; (void)o; return ExtensionUnavailable(); }

CNA_Result cna_spot_shadow_map_compute_light_projection(
    const CNA_SpotLightEXT* const l, CNA_Matrix* const o)
{ (void)l; (void)o; return ExtensionUnavailable(); }

CNA_Result cna_spot_shadow_map_destroy(const CNA_SpotShadowMapHandle m)
{ (void)m; return ExtensionUnavailable(); }


#else // CNA_CNAEXT

namespace {

[[nodiscard]] CNA_Result CreateStorageBufferHandle(
    const CNA_Handle graphicsDeviceHandle,
    const uint64_t byteSize,
    const uint64_t elementCount,
    const uint64_t elementByteSize,
    CNA_StorageBufferHandle* const outBuffer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBuffer == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The storage-buffer output handle is null.");
        }
        *outBuffer = CNA_INVALID_HANDLE;
        if (byteSize > static_cast<uint64_t>(SIZE_MAX)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "The storage-buffer size does not fit this platform's size type.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result =
                GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        auto native = std::make_shared<Ext::StorageBuffer>(
            *graphicsDevice->value, static_cast<std::size_t>(byteSize));
        const auto resource = std::make_shared<StorageBufferResource>(StorageBufferResource{
            std::move(native), graphicsDevice->parentGame, elementCount, elementByteSize});
        const CNA_Result result =
            GetRuntimeHandles().Create(ObjectKind::StorageBuffer, resource, outBuffer);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned storage-buffer handle could not be created.");
        }
        AddOwnedGraphicsResourceFor(graphicsDevice->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

} // namespace

CNA_Result cna_storage_buffer_create(
    const CNA_Handle graphicsDeviceHandle,
    const uint64_t byteSize,
    CNA_StorageBufferHandle* const outBuffer)
{
    return CreateStorageBufferHandle(
        graphicsDeviceHandle, byteSize, UINT64_C(0), UINT64_C(0), outBuffer);
}

CNA_Result cna_storage_buffer_create_typed(
    const CNA_Handle graphicsDeviceHandle,
    const uint64_t elementCount,
    const uint64_t elementByteSize,
    CNA_StorageBufferHandle* const outBuffer)
{
    if (outBuffer != nullptr) {
        *outBuffer = CNA_INVALID_HANDLE;
    }
    if (elementByteSize == 0U) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "A typed storage buffer needs a non-zero element size.");
    }
    // The canonical template computes elementCount * sizeof(T); a C caller supplies both, so the
    // overflow the template cannot have is one this route must refuse rather than wrap.
    if (elementCount != 0U && elementByteSize > UINT64_MAX / elementCount) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_RANGE,
            "The element count times the element size overflows.");
    }
    return CreateStorageBufferHandle(
        graphicsDeviceHandle,
        elementCount * elementByteSize,
        elementCount,
        elementByteSize,
        outBuffer);
}

CNA_Result cna_storage_buffer_set_bytes(
    const CNA_StorageBufferHandle bufferHandle,
    const void* const data,
    const uint64_t byteSize)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (data == nullptr && byteSize != 0U) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The storage-buffer source is null.");
        }
        std::shared_ptr<StorageBufferResource> buffer;
        if (const CNA_Result result = GetStorageBuffer(bufferHandle, &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (byteSize > static_cast<uint64_t>(buffer->value->getByteSize())) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "More bytes than the storage buffer holds.");
        }
        buffer->value->setBytes(data, static_cast<std::size_t>(byteSize));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_buffer_get_bytes(
    const CNA_StorageBufferHandle bufferHandle,
    void* const destination,
    const uint64_t byteSize)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (destination == nullptr && byteSize != 0U) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The storage-buffer destination is null.");
        }
        std::shared_ptr<StorageBufferResource> buffer;
        if (const CNA_Result result = GetStorageBuffer(bufferHandle, &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (byteSize > static_cast<uint64_t>(buffer->value->getByteSize())) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "More bytes than the storage buffer holds.");
        }
        buffer->value->getBytes(destination, static_cast<std::size_t>(byteSize));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_buffer_get_byte_size(
    const CNA_StorageBufferHandle bufferHandle,
    uint64_t* const outByteSize)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteSize == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The storage-buffer size output is null.");
        }
        std::shared_ptr<StorageBufferResource> buffer;
        if (const CNA_Result result = GetStorageBuffer(bufferHandle, &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outByteSize = static_cast<uint64_t>(buffer->value->getByteSize());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_buffer_set_elements(
    const CNA_StorageBufferHandle bufferHandle,
    const void* const data,
    const uint64_t elementCount,
    const uint64_t elementByteSize)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (data == nullptr && elementCount != 0U) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The storage-buffer source is null.");
        }
        std::shared_ptr<StorageBufferResource> buffer;
        if (const CNA_Result result = GetStorageBuffer(bufferHandle, &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = RequireElementShape(*buffer, elementByteSize);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // StorageBufferT<T>::setData throws std::invalid_argument here; at this boundary the same
        // refusal is a result rather than an exception, which is what every other route does too.
        if (elementCount > buffer->elementCount) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "More elements than the storage buffer holds.");
        }
        buffer->value->setBytes(data, static_cast<std::size_t>(elementCount * elementByteSize));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_buffer_get_elements(
    const CNA_StorageBufferHandle bufferHandle,
    void* const destination,
    const uint64_t elementCount,
    const uint64_t elementByteSize)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (destination == nullptr && elementCount != 0U) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The storage-buffer destination is null.");
        }
        std::shared_ptr<StorageBufferResource> buffer;
        if (const CNA_Result result = GetStorageBuffer(bufferHandle, &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = RequireElementShape(*buffer, elementByteSize);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // getData() returns the buffer's whole element range, so the C form takes exactly that
        // count rather than an arbitrary prefix; a shorter read is the byte route's job.
        if (elementCount != buffer->elementCount) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "The element count must equal the storage buffer's own element count.");
        }
        buffer->value->getBytes(destination, static_cast<std::size_t>(elementCount * elementByteSize));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_buffer_get_element_count(
    const CNA_StorageBufferHandle bufferHandle,
    uint64_t* const outElementCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outElementCount == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The element-count output is null.");
        }
        std::shared_ptr<StorageBufferResource> buffer;
        if (const CNA_Result result = GetStorageBuffer(bufferHandle, &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outElementCount = buffer->elementCount;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_buffer_get_element_byte_size(
    const CNA_StorageBufferHandle bufferHandle,
    uint64_t* const outElementByteSize)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outElementByteSize == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The element-size output is null.");
        }
        std::shared_ptr<StorageBufferResource> buffer;
        if (const CNA_Result result = GetStorageBuffer(bufferHandle, &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outElementByteSize = buffer->elementByteSize;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_buffer_destroy(const CNA_StorageBufferHandle bufferHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<StorageBufferResource> buffer;
        if (const CNA_Result result = GetStorageBuffer(bufferHandle, &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(bufferHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The owned storage-buffer handle could not be released.");
        }
        RemoveOwnedGraphicsResourceFor(buffer->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compute_shader_create(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_StringView source,
    CNA_ComputeShaderHandle* const outShader)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outShader == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The compute-shader output handle is null.");
        }
        *outShader = CNA_INVALID_HANDLE;
        std::string nativeSource;
        if (const CNA_Result result = CopyStringView(source, false, &nativeSource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result =
                GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        auto native =
            std::make_shared<Ext::ComputeShader>(*graphicsDevice->value, nativeSource);
        const auto resource = std::make_shared<ComputeShaderResource>(
            ComputeShaderResource{std::move(native), graphicsDevice->parentGame});
        const CNA_Result result =
            GetRuntimeHandles().Create(ObjectKind::ComputeShader, resource, outShader);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned compute-shader handle could not be created.");
        }
        AddOwnedGraphicsResourceFor(graphicsDevice->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compute_shader_set_uniform_int(
    const CNA_ComputeShaderHandle shaderHandle,
    const CNA_StringView name,
    const int32_t value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::string nativeName;
        if (const CNA_Result result = CopyStringView(name, true, &nativeName);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<ComputeShaderResource> shader;
        if (const CNA_Result result = GetComputeShader(shaderHandle, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        shader->value->setUniform(nativeName, static_cast<int>(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compute_shader_set_uniform_float(
    const CNA_ComputeShaderHandle shaderHandle,
    const CNA_StringView name,
    const float value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::string nativeName;
        if (const CNA_Result result = CopyStringView(name, true, &nativeName);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<ComputeShaderResource> shader;
        if (const CNA_Result result = GetComputeShader(shaderHandle, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        shader->value->setUniform(nativeName, value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compute_shader_bind_storage_buffer(
    const CNA_ComputeShaderHandle shaderHandle,
    const int32_t binding,
    const CNA_StorageBufferHandle bufferHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ComputeShaderResource> shader;
        if (const CNA_Result result = GetComputeShader(shaderHandle, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<StorageBufferResource> buffer;
        if (const CNA_Result result = GetStorageBuffer(bufferHandle, &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        shader->value->bindStorageBuffer(static_cast<int>(binding), *buffer->value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compute_shader_bind_texture(
    const CNA_ComputeShaderHandle shaderHandle,
    const int32_t unit,
    const CNA_StringView samplerName,
    const CNA_Handle textureHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::string nativeName;
        if (const CNA_Result result = CopyStringView(samplerName, true, &nativeName);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<ComputeShaderResource> shader;
        if (const CNA_Result result = GetComputeShader(shaderHandle, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<Texture2DResource> texture;
        if (const CNA_Result result = GetOwnedTexture2D(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        shader->value->bindTexture(static_cast<int>(unit), nativeName, *texture->value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compute_shader_is_image_binding_supported(
    const CNA_ComputeShaderHandle shaderHandle,
    CNA_Bool* const outSupported)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSupported == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The image-binding support output is null.");
        }
        std::shared_ptr<ComputeShaderResource> shader;
        if (const CNA_Result result = GetComputeShader(shaderHandle, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outSupported = shader->value->isImageBindingSupported() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compute_shader_bind_image(
    const CNA_ComputeShaderHandle shaderHandle,
    const int32_t unit,
    const CNA_Handle textureHandle,
    const CNA_GraphicsImageAccess access)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (access > CNA_GRAPHICS_IMAGE_ACCESS_READ_WRITE) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The image access is not a defined CNA_GRAPHICS_IMAGE_ACCESS_* value.");
        }
        std::shared_ptr<ComputeShaderResource> shader;
        if (const CNA_Result result = GetComputeShader(shaderHandle, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<Texture2DResource> texture;
        if (const CNA_Result result = GetOwnedTexture2D(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        shader->value->bindImage(
            static_cast<int>(unit),
            *texture->value,
            static_cast<CNA::GraphicsImageAccess>(access));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compute_shader_dispatch(
    const CNA_ComputeShaderHandle shaderHandle,
    const int32_t groupsX,
    const int32_t groupsY,
    const int32_t groupsZ)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ComputeShaderResource> shader;
        if (const CNA_Result result = GetComputeShader(shaderHandle, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        shader->value->dispatch(
            static_cast<int>(groupsX), static_cast<int>(groupsY), static_cast<int>(groupsZ));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compute_shader_barrier(
    const CNA_ComputeShaderHandle shaderHandle,
    const CNA_GraphicsMemoryBarrier bits)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if ((bits & ~CNA_GRAPHICS_MEMORY_BARRIER_ALL) != 0U) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The barrier mask contains a bit no CNA_GRAPHICS_MEMORY_BARRIER_* value defines.");
        }
        std::shared_ptr<ComputeShaderResource> shader;
        if (const CNA_Result result = GetComputeShader(shaderHandle, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        shader->value->barrier(static_cast<CNA::GraphicsMemoryBarrier>(bits));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compute_shader_is_valid(
    const CNA_ComputeShaderHandle shaderHandle,
    CNA_Bool* const outValid)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValid == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The validity output is null.");
        }
        std::shared_ptr<ComputeShaderResource> shader;
        if (const CNA_Result result = GetComputeShader(shaderHandle, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValid = shader->value->isValid() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compute_shader_copy_compile_error(
    const CNA_ComputeShaderHandle shaderHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    std::shared_ptr<ComputeShaderResource> shader;
    if (const CNA_Result result = GetComputeShader(shaderHandle, &shader);
        result != CNA_RESULT_SUCCESS) {
        if (outBytes != nullptr) {
            *outBytes = UINT64_C(0);
        }
        return result;
    }
    return CopyFormattedString(destination, capacity, outBytes, [&shader] {
        return shader->value->getCompileError();
    });
}

CNA_Result cna_compute_shader_destroy(const CNA_ComputeShaderHandle shaderHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ComputeShaderResource> shader;
        if (const CNA_Result result = GetComputeShader(shaderHandle, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(shaderHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The owned compute-shader handle could not be released.");
        }
        RemoveOwnedGraphicsResourceFor(shader->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gpu_timer_create(
    const CNA_Handle graphicsDeviceHandle,
    CNA_GpuTimerHandle* const outTimer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTimer == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The GPU-timer output handle is null.");
        }
        *outTimer = CNA_INVALID_HANDLE;
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto resource = std::make_shared<GpuTimerResource>(GpuTimerResource{
            std::make_shared<Ext::GpuTimer>(*graphicsDevice->value),
            graphicsDevice->parentGame});
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::GpuTimer, resource, outTimer);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned GPU-timer handle could not be created.");
        }
        AddOwnedGraphicsResourceFor(graphicsDevice->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gpu_timer_is_supported(
    const CNA_GpuTimerHandle timerHandle,
    CNA_Bool* const outSupported)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSupported == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The GPU-timer support output is null.");
        }
        std::shared_ptr<GpuTimerResource> timer;
        if (const CNA_Result result = GetEngineResource(
                timerHandle, ObjectKind::GpuTimer, "GpuTimer", &timer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outSupported = timer->value->isSupported() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gpu_timer_copy_unsupported_reason(
    const CNA_GpuTimerHandle timerHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<GpuTimerResource> timer;
        if (const CNA_Result result = GetEngineResource(
                timerHandle, ObjectKind::GpuTimer, "GpuTimer", &timer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyFormattedString(destination, capacity, outBytes, [&timer] {
            return timer->value->getUnsupportedReason();
        });
    });
}

CNA_Result cna_gpu_timer_begin(const CNA_GpuTimerHandle timerHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<GpuTimerResource> timer;
        if (const CNA_Result result = GetEngineResource(
                timerHandle, ObjectKind::GpuTimer, "GpuTimer", &timer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        timer->value->begin();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gpu_timer_end(const CNA_GpuTimerHandle timerHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<GpuTimerResource> timer;
        if (const CNA_Result result = GetEngineResource(
                timerHandle, ObjectKind::GpuTimer, "GpuTimer", &timer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        timer->value->end();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gpu_timer_is_result_available(
    const CNA_GpuTimerHandle timerHandle,
    CNA_Bool* const outAvailable)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outAvailable == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The GPU-timer availability output is null.");
        }
        std::shared_ptr<GpuTimerResource> timer;
        if (const CNA_Result result = GetEngineResource(
                timerHandle, ObjectKind::GpuTimer, "GpuTimer", &timer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outAvailable = timer->value->isResultAvailable() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gpu_timer_poll(
    const CNA_GpuTimerHandle timerHandle,
    CNA_Bool* const outCollected)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCollected == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The GPU-timer poll output is null.");
        }
        std::shared_ptr<GpuTimerResource> timer;
        if (const CNA_Result result = GetEngineResource(
                timerHandle, ObjectKind::GpuTimer, "GpuTimer", &timer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCollected = timer->value->poll() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gpu_timer_get_last_milliseconds(
    const CNA_GpuTimerHandle timerHandle,
    double* const outMilliseconds)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outMilliseconds == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The GPU-timer millisecond output is null.");
        }
        std::shared_ptr<GpuTimerResource> timer;
        if (const CNA_Result result = GetEngineResource(
                timerHandle, ObjectKind::GpuTimer, "GpuTimer", &timer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outMilliseconds = timer->value->getLastMilliseconds();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gpu_timer_get_sample_count(
    const CNA_GpuTimerHandle timerHandle,
    int32_t* const outSampleCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSampleCount == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The GPU-timer sample-count output is null.");
        }
        std::shared_ptr<GpuTimerResource> timer;
        if (const CNA_Result result = GetEngineResource(
                timerHandle, ObjectKind::GpuTimer, "GpuTimer", &timer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outSampleCount = static_cast<int32_t>(timer->value->getSampleCount());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gpu_timer_is_open(
    const CNA_GpuTimerHandle timerHandle,
    CNA_Bool* const outOpen)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outOpen == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The GPU-timer open-state output is null.");
        }
        std::shared_ptr<GpuTimerResource> timer;
        if (const CNA_Result result = GetEngineResource(
                timerHandle, ObjectKind::GpuTimer, "GpuTimer", &timer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outOpen = timer->value->isOpen() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gpu_timer_destroy(const CNA_GpuTimerHandle timerHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<GpuTimerResource> timer;
        if (const CNA_Result result = GetEngineResource(
                timerHandle, ObjectKind::GpuTimer, "GpuTimer", &timer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(timerHandle);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned GPU-timer handle could not be released.");
        }
        RemoveOwnedGraphicsResourceFor(timer->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_render_target_pool_create(
    const CNA_Handle graphicsDeviceHandle,
    CNA_RenderTargetPoolHandle* const outPool)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPool == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The render-target-pool output handle is null.");
        }
        *outPool = CNA_INVALID_HANDLE;
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto resource = std::make_shared<RenderTargetPoolResource>(
            RenderTargetPoolResource{
                std::make_shared<Ext::RenderTargetPool>(*graphicsDevice->value),
                graphicsDevice->parentGame});
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::RenderTargetPool, resource, outPool);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned render-target-pool handle could not be created.");
        }
        AddOwnedGraphicsResourceFor(graphicsDevice->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_render_target_pool_acquire(
    const CNA_RenderTargetPoolHandle poolHandle,
    const int32_t width,
    const int32_t height,
    const CNA_SurfaceFormat format,
    const CNA_DepthFormat depthFormat,
    const int32_t slot,
    CNA_Handle* const outRenderTarget)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRenderTarget == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The pooled render-target output handle is null.");
        }
        *outRenderTarget = CNA_INVALID_HANDLE;
        if (format > CNA_SURFACE_FORMAT_USHORT_EXT ||
            depthFormat > CNA_DEPTH_FORMAT_DEPTH24_STENCIL8) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The pooled render-target format identity is invalid.");
        }
        std::shared_ptr<RenderTargetPoolResource> pool;
        if (const CNA_Result result = GetEngineResource(
                poolHandle, ObjectKind::RenderTargetPool, "RenderTargetPool", &pool);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto* const target = pool->value->acquire(
            static_cast<int>(width),
            static_cast<int>(height),
            static_cast<Microsoft::Xna::Framework::Graphics::SurfaceFormat>(format),
            static_cast<Microsoft::Xna::Framework::Graphics::DepthFormat>(depthFormat),
            static_cast<int>(slot));
        if (target == nullptr) {
            return Fail(
                CNA_RESULT_INTERNAL,
                CNA_ERROR_CATEGORY_INTERNAL,
                "The render-target pool returned a null target.");
        }
        const auto borrow = std::make_shared<CountedBorrow<RenderTargetPoolResource>>(pool);
        const std::shared_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> view(
            borrow, target);
        return CreateBorrowedRenderTarget2D(
            view, pool->parentGame, borrow, outRenderTarget);
    });
}

CNA_Result cna_render_target_pool_reset(const CNA_RenderTargetPoolHandle poolHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<RenderTargetPoolResource> pool;
        if (const CNA_Result result = GetEngineResource(
                poolHandle, ObjectKind::RenderTargetPool, "RenderTargetPool", &pool);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (pool->activeBorrowCount != 0U) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "Every borrowed pooled render-target handle must be released before reset.");
        }
        pool->value->reset();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_render_target_pool_get_target_count(
    const CNA_RenderTargetPoolHandle poolHandle,
    uint64_t* const outTargetCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTargetCount == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The render-target-pool count output is null.");
        }
        std::shared_ptr<RenderTargetPoolResource> pool;
        if (const CNA_Result result = GetEngineResource(
                poolHandle, ObjectKind::RenderTargetPool, "RenderTargetPool", &pool);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outTargetCount = static_cast<uint64_t>(pool->value->getTargetCount());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_render_target_pool_get_estimated_bytes(
    const CNA_RenderTargetPoolHandle poolHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The render-target-pool byte-count output is null.");
        }
        std::shared_ptr<RenderTargetPoolResource> pool;
        if (const CNA_Result result = GetEngineResource(
                poolHandle, ObjectKind::RenderTargetPool, "RenderTargetPool", &pool);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = static_cast<uint64_t>(pool->value->getEstimatedBytes());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_render_target_pool_destroy(const CNA_RenderTargetPoolHandle poolHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<RenderTargetPoolResource> pool;
        if (const CNA_Result result = GetEngineResource(
                poolHandle, ObjectKind::RenderTargetPool, "RenderTargetPool", &pool);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (pool->activeBorrowCount != 0U) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "Every borrowed pooled render-target handle must be released before the pool.");
        }
        const CNA_Result result = GetRuntimeHandles().Release(poolHandle);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned render-target-pool handle could not be released.");
        }
        RemoveOwnedGraphicsResourceFor(pool->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_shader_effect_factory_create(
    const CNA_Handle graphicsDeviceHandle,
    CNA_ShaderEffectFactoryHandle* const outFactory)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outFactory == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The shader-effect-factory output handle is null.");
        }
        *outFactory = CNA_INVALID_HANDLE;
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto resource = std::make_shared<ShaderEffectFactoryResource>(
            ShaderEffectFactoryResource{
                std::make_shared<Ext::ShaderEffectFactory>(*graphicsDevice->value),
                graphicsDevice->parentGame});
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::ShaderEffectFactory, resource, outFactory);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned shader-effect-factory handle could not be created.");
        }
        AddOwnedGraphicsResourceFor(graphicsDevice->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_shader_effect_factory_acquire(
    const CNA_ShaderEffectFactoryHandle factoryHandle,
    const CNA_StringView name,
    const CNA_StringView vertexSource,
    const CNA_StringView fragmentSource,
    CNA_EffectHandle* const outEffect)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEffect == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The cached shader-effect output handle is null.");
        }
        *outEffect = CNA_INVALID_HANDLE;
        std::string nativeName;
        std::string nativeVertexSource;
        std::string nativeFragmentSource;
        if (const CNA_Result result = CopyStringView(name, true, &nativeName);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The shader-effect cache key is not valid UTF-8 text.");
        }
        if (const CNA_Result result = CopyStringView(
                vertexSource, true, &nativeVertexSource);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The cached vertex source is not valid UTF-8 text.");
        }
        if (const CNA_Result result = CopyStringView(
                fragmentSource, true, &nativeFragmentSource);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The cached fragment source is not valid UTF-8 text.");
        }
        std::shared_ptr<ShaderEffectFactoryResource> factory;
        if (const CNA_Result result = GetEngineResource(
                factoryHandle,
                ObjectKind::ShaderEffectFactory,
                "ShaderEffectFactory",
                &factory);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto* const effect = factory->value->acquire(
            nativeName, nativeVertexSource, nativeFragmentSource);
        if (effect == nullptr) {
            return Fail(
                CNA_RESULT_INTERNAL,
                CNA_ERROR_CATEGORY_INTERNAL,
                "The shader-effect factory returned a null effect.");
        }
        const auto borrow =
            std::make_shared<CountedBorrow<ShaderEffectFactoryResource>>(factory);
        const std::shared_ptr<Microsoft::Xna::Framework::Graphics::Effect> view(
            borrow, effect);
        return CreateBorrowedEffect(view, factory->parentGame, outEffect);
    });
}

CNA_Result cna_shader_effect_factory_contains(
    const CNA_ShaderEffectFactoryHandle factoryHandle,
    const CNA_StringView name,
    CNA_Bool* const outContains)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outContains == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The shader-effect-factory contains output is null.");
        }
        std::string nativeName;
        if (const CNA_Result result = CopyStringView(name, true, &nativeName);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The shader-effect cache key is not valid UTF-8 text.");
        }
        std::shared_ptr<ShaderEffectFactoryResource> factory;
        if (const CNA_Result result = GetEngineResource(
                factoryHandle,
                ObjectKind::ShaderEffectFactory,
                "ShaderEffectFactory",
                &factory);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outContains = factory->value->contains(nativeName) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_shader_effect_factory_get_compile_count(
    const CNA_ShaderEffectFactoryHandle factoryHandle,
    uint64_t* const outCompileCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCompileCount == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The shader-effect-factory compile-count output is null.");
        }
        std::shared_ptr<ShaderEffectFactoryResource> factory;
        if (const CNA_Result result = GetEngineResource(
                factoryHandle,
                ObjectKind::ShaderEffectFactory,
                "ShaderEffectFactory",
                &factory);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCompileCount = static_cast<uint64_t>(factory->value->getCompileCount());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_shader_effect_factory_clear(
    const CNA_ShaderEffectFactoryHandle factoryHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ShaderEffectFactoryResource> factory;
        if (const CNA_Result result = GetEngineResource(
                factoryHandle,
                ObjectKind::ShaderEffectFactory,
                "ShaderEffectFactory",
                &factory);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (factory->activeBorrowCount != 0U) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "Every borrowed cached effect handle must be released before clear.");
        }
        factory->value->clear();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_shader_effect_factory_destroy(
    const CNA_ShaderEffectFactoryHandle factoryHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ShaderEffectFactoryResource> factory;
        if (const CNA_Result result = GetEngineResource(
                factoryHandle,
                ObjectKind::ShaderEffectFactory,
                "ShaderEffectFactory",
                &factory);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (factory->activeBorrowCount != 0U) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "Every borrowed cached effect handle must be released before the factory.");
        }
        const CNA_Result result = GetRuntimeHandles().Release(factoryHandle);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned shader-effect-factory handle could not be released.");
        }
        RemoveOwnedGraphicsResourceFor(factory->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_scoped_render_target_begin(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_Handle destinationHandle,
    CNA_ScopedRenderTargetHandle* const outScope)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outScope == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The scoped-render-target output handle is null.");
        }
        *outScope = CNA_INVALID_HANDLE;
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        std::vector<CNA_RenderTargetBinding> previousBindings;
        if (const CNA_Result result = GetTrackedRenderTargetBindings(
                graphicsDevice->value, &previousBindings);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<ScopeTargetReference> targetReferences;
        targetReferences.reserve(
            previousBindings.size() + (destinationHandle == CNA_INVALID_HANDLE ? 0U : 1U));
        for (const CNA_RenderTargetBinding& binding : previousBindings) {
            if (const CNA_Result result = RetainScopeTarget(
                    binding.render_target,
                    graphicsDevice->parentGame,
                    &targetReferences);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
        }

        Microsoft::Xna::Framework::Graphics::RenderTarget2D* destination = nullptr;
        if (destinationHandle != CNA_INVALID_HANDLE) {
            std::shared_ptr<Texture2DResource> target;
            if (const CNA_Result result = GetOwnedTexture2D(destinationHandle, &target);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            ObjectKind kind = ObjectKind::Unknown;
            if (GetRuntimeHandles().GetKind(destinationHandle, &kind) != CNA_RESULT_SUCCESS ||
                kind != ObjectKind::RenderTarget2D ||
                target->parentGame != graphicsDevice->parentGame) {
                return Fail(
                    CNA_RESULT_INVALID_HANDLE,
                    CNA_ERROR_CATEGORY_HANDLE,
                    "The scoped destination is not a RenderTarget2D owned by this game.");
            }
            if (const CNA_Result result = RetainScopeTarget(
                    destinationHandle,
                    graphicsDevice->parentGame,
                    &targetReferences);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            destination = static_cast<Microsoft::Xna::Framework::Graphics::RenderTarget2D*>(
                target->value.get());
        }

        if (nextScopeToken == std::numeric_limits<uint64_t>::max()) {
            return Fail(
                CNA_RESULT_OVERFLOW,
                CNA_ERROR_CATEGORY_RANGE,
                "The scoped-render-target token space is exhausted.");
        }
        const uint64_t token = nextScopeToken++;
        ScopeStackReservation stackReservation(graphicsDevice->value, token);
        auto native = std::make_unique<Ext::ScopedRenderTarget>(
            *graphicsDevice->value, destination);
        const auto resource = std::make_shared<ScopedRenderTargetResource>(
            ScopedRenderTargetResource{
                graphicsDevice->parentGame,
                graphicsDevice->value,
                token,
                previousBindings,
                std::move(targetReferences),
                std::move(native)});
        const CNA_Result createResult = GetRuntimeHandles().Create(
            ObjectKind::ScopedRenderTarget, resource, outScope);
        if (createResult != CNA_RESULT_SUCCESS) {
            return Fail(
                createResult,
                ErrorCategoryForResult(createResult),
                "The active scoped-render-target handle could not be created.");
        }

        std::vector<CNA_RenderTargetBinding> activeBindings;
        if (destinationHandle != CNA_INVALID_HANDLE) {
            activeBindings.push_back(CNA_RenderTargetBinding{
                .struct_size = sizeof(CNA_RenderTargetBinding),
                .struct_version = UINT32_C(1),
                .render_target = destinationHandle,
                .array_slice = 0,
                .cube_map_face = CNA_CUBE_MAP_FACE_POSITIVE_X});
        }
        try {
            SetTrackedRenderTargetBindings(
                graphicsDevice->value, std::move(activeBindings));
        } catch (...) {
            (void)GetRuntimeHandles().Release(*outScope);
            *outScope = CNA_INVALID_HANDLE;
            resource->value.reset();
            throw;
        }
        stackReservation.Commit();
        AddOwnedGraphicsResourceFor(graphicsDevice->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_scoped_render_target_get_has_recorded_previous(
    const CNA_ScopedRenderTargetHandle scopeHandle,
    CNA_Bool* const outRecorded)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRecorded == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The scoped-render-target recorded-state output is null.");
        }
        std::shared_ptr<ScopedRenderTargetResource> scope;
        if (const CNA_Result result = GetEngineResource(
                scopeHandle,
                ObjectKind::ScopedRenderTarget,
                "ScopedRenderTarget",
                &scope);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outRecorded = scope->value->hasRecordedPrevious() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_scoped_render_target_end(
    const CNA_ScopedRenderTargetHandle scopeHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ScopedRenderTargetResource> scope;
        if (const CNA_Result result = GetEngineResource(
                scopeHandle,
                ObjectKind::ScopedRenderTarget,
                "ScopedRenderTarget",
                &scope);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto found = scopeStacks.find(scope->device);
        if (found == scopeStacks.end() || found->second.empty() ||
            found->second.back() != scope->stackToken) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "Render-target scopes must be ended in reverse order on each device.");
        }

        std::vector<CNA_RenderTargetBinding> restored =
            scope->value->hasRecordedPrevious()
            ? scope->previousBindings
            : std::vector<CNA_RenderTargetBinding>{};
        SetTrackedRenderTargetBindings(scope->device, std::move(restored));
        scope->value.reset();
        const CNA_Result releaseResult = GetRuntimeHandles().Release(scopeHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The scoped-render-target handle could not be released.");
        }
        found->second.pop_back();
        if (found->second.empty()) {
            scopeStacks.erase(found);
        }
        RemoveOwnedGraphicsResourceFor(scope->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

namespace {

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::AlphaModeEXT;
using Microsoft::Xna::Framework::Graphics::PbrEffect;
using Microsoft::Xna::Framework::Graphics::SkinnedPbrEffect;
using Microsoft::Xna::Framework::Graphics::TextureTransformEXT;

constexpr int kPbrSlotCount = Ext::kPbrTextureSlotCount;

[[nodiscard]] CNA_Result GetPostProcessPass(
    const CNA_Handle handle, std::shared_ptr<PostProcessPassResource>* const outPass)
{
    return GetEngineResource(handle, ObjectKind::PostProcessPass, "PostProcessPass", outPass);
}

[[nodiscard]] CNA_Result GetEffectPass(
    const CNA_Handle handle, std::shared_ptr<PostProcessPassResource>* const outPass)
{
    if (const CNA_Result result = GetPostProcessPass(handle, outPass);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if ((*outPass)->effectPass == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "This post-process pass is not an effect pass.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CreatePassHandle(
    std::shared_ptr<Ext::PostProcessPass> native,
    Ext::EffectPass* const effectPass,
    const CNA_Handle parentGame,
    std::shared_ptr<void> effectRetention,
    const CNA_Handle ownedEffect,
    CNA_PostProcessPassHandle* const outPass)
{
    const auto resource = std::make_shared<PostProcessPassResource>(PostProcessPassResource{
        std::move(native), effectPass, parentGame, std::move(effectRetention), ownedEffect, 0U});
    const CNA_Result result =
        GetRuntimeHandles().Create(ObjectKind::PostProcessPass, resource, outPass);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned post-process pass handle could not be created.");
    }
    AddOwnedGraphicsResourceFor(parentGame);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ToNativePbrMaterial(
    const CNA_PbrMaterialEXT& value, Ext::PbrMaterial* const out)
{
    if (value.struct_size != static_cast<uint32_t>(sizeof(CNA_PbrMaterialEXT)) ||
        value.struct_version != UINT32_C(1)) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The material was not initialized by cna_pbr_material_ext_init.");
    }
    if (value.alpha_mode > UINT32_C(2)) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The alpha mode is not a defined CNA_ALPHA_MODE_EXT_* value.");
    }
    for (const CNA_Bool flag : {value.double_sided, value.base_color_texture_srgb,
                                value.emissive_texture_srgb, value.specular_color_texture_srgb,
                                value.output_encoded_to_srgb}) {
        if (const CNA_Result result = ValidateCanonicalBool(flag, "material flag");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
    }

    // Textures are borrowed: a material never owns the Texture2D a caller hands it, which is the
    // rule the effects family already settled. Each slot is resolved through the registry so a
    // stale handle is refused here rather than dereferenced inside the engine layer.
    struct SlotBinding {
        CNA_Handle handle;
        void (Ext::PbrMaterial::*setter)(Microsoft::Xna::Framework::Graphics::Texture2D*);
    };
    const SlotBinding slots[] = {
        {value.albedo_texture, &Ext::PbrMaterial::setAlbedoTexture},
        {value.normal_texture, &Ext::PbrMaterial::setNormalTexture},
        {value.metallic_roughness_texture, &Ext::PbrMaterial::setMetallicRoughnessTexture},
        {value.ambient_occlusion_texture, &Ext::PbrMaterial::setAmbientOcclusionTexture},
        {value.emissive_texture, &Ext::PbrMaterial::setEmissiveTexture},
        {value.specular_texture, &Ext::PbrMaterial::setSpecularTexture},
        {value.specular_color_texture, &Ext::PbrMaterial::setSpecularColorTexture},
    };
    for (const SlotBinding& slot : slots) {
        Microsoft::Xna::Framework::Graphics::Texture2D* texture = nullptr;
        std::shared_ptr<Texture2DResource> retention;
        if (const CNA_Result result =
                ResolveTexture2DArgument(slot.handle, "material texture", &texture, &retention);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        (out->*slot.setter)(texture);
    }

    out->setAlbedoColor(Color(
        value.albedo_color.r, value.albedo_color.g, value.albedo_color.b, value.albedo_color.a));
    out->setEmissiveFactor(
        Vector3(value.emissive_factor.x, value.emissive_factor.y, value.emissive_factor.z));
    out->setSpecularColorFactor(Vector3(
        value.specular_color_factor.x,
        value.specular_color_factor.y,
        value.specular_color_factor.z));
    out->setMetallicFactor(value.metallic_factor);
    out->setRoughnessFactor(value.roughness_factor);
    out->setNormalScale(value.normal_scale);
    out->setOcclusionStrength(value.occlusion_strength);
    out->setIor(value.ior);
    out->setSpecularFactor(value.specular_factor);
    out->setAlphaCutoff(value.alpha_cutoff);
    out->setAlphaMode(static_cast<AlphaModeEXT>(value.alpha_mode));
    out->setDoubleSided(value.double_sided == CNA_TRUE);
    out->setBaseColorTextureSrgb(value.base_color_texture_srgb == CNA_TRUE);
    out->setEmissiveTextureSrgb(value.emissive_texture_srgb == CNA_TRUE);
    out->setSpecularColorTextureSrgb(value.specular_color_texture_srgb == CNA_TRUE);
    out->setOutputEncodedToSrgb(value.output_encoded_to_srgb == CNA_TRUE);
    for (int slot = 0; slot < kPbrSlotCount; ++slot) {
        const auto which = static_cast<Ext::PbrTextureSlot>(slot);
        out->setTextureCoordinateSet(
            which, static_cast<int>(value.texture_coordinate_sets[slot]));
        TextureTransformEXT transform;
        transform.Offset = {value.texture_transforms[slot].offset.x,
                            value.texture_transforms[slot].offset.y};
        transform.Scale = {value.texture_transforms[slot].scale.x,
                           value.texture_transforms[slot].scale.y};
        transform.Rotation = value.texture_transforms[slot].rotation;
        out->setTextureTransform(which, transform);
    }
    return CNA_RESULT_SUCCESS;
}

// The textures deliberately do not come back. A handle is the registry's name for an object, and
// the engine layer stores a raw Texture2D*; inventing a handle for a pointer the caller may never
// have owned would hand back a name for something this ABI does not track. Every non-texture field
// round-trips, and the slots read as CNA_INVALID_HANDLE.
void FromNativePbrMaterial(const Ext::PbrMaterial& value, CNA_PbrMaterialEXT* const out)
{
    out->struct_size = static_cast<uint32_t>(sizeof(CNA_PbrMaterialEXT));
    out->struct_version = UINT32_C(1);
    out->albedo_texture = CNA_INVALID_HANDLE;
    out->normal_texture = CNA_INVALID_HANDLE;
    out->metallic_roughness_texture = CNA_INVALID_HANDLE;
    out->ambient_occlusion_texture = CNA_INVALID_HANDLE;
    out->emissive_texture = CNA_INVALID_HANDLE;
    out->specular_texture = CNA_INVALID_HANDLE;
    out->specular_color_texture = CNA_INVALID_HANDLE;

    const Color albedo = value.getAlbedoColor();
    out->albedo_color.r = albedo.getRProperty();
    out->albedo_color.g = albedo.getGProperty();
    out->albedo_color.b = albedo.getBProperty();
    out->albedo_color.a = albedo.getAProperty();
    const Vector3 emissive = value.getEmissiveFactor();
    out->emissive_factor.x = emissive.X;
    out->emissive_factor.y = emissive.Y;
    out->emissive_factor.z = emissive.Z;
    const Vector3 specularColor = value.getSpecularColorFactor();
    out->specular_color_factor.x = specularColor.X;
    out->specular_color_factor.y = specularColor.Y;
    out->specular_color_factor.z = specularColor.Z;
    out->metallic_factor = value.getMetallicFactor();
    out->roughness_factor = value.getRoughnessFactor();
    out->normal_scale = value.getNormalScale();
    out->occlusion_strength = value.getOcclusionStrength();
    out->ior = value.getIor();
    out->specular_factor = value.getSpecularFactor();
    out->alpha_cutoff = value.getAlphaCutoff();
    out->alpha_mode = static_cast<CNA_AlphaModeEXT>(value.getAlphaMode());
    out->double_sided = value.isDoubleSided() ? CNA_TRUE : CNA_FALSE;
    out->base_color_texture_srgb = value.isBaseColorTextureSrgb() ? CNA_TRUE : CNA_FALSE;
    out->emissive_texture_srgb = value.isEmissiveTextureSrgb() ? CNA_TRUE : CNA_FALSE;
    out->specular_color_texture_srgb = value.isSpecularColorTextureSrgb() ? CNA_TRUE : CNA_FALSE;
    out->output_encoded_to_srgb = value.isOutputEncodedToSrgb() ? CNA_TRUE : CNA_FALSE;
    out->reserved[0] = 0U; out->reserved[1] = 0U; out->reserved[2] = 0U;
    for (int slot = 0; slot < kPbrSlotCount; ++slot) {
        const auto which = static_cast<Ext::PbrTextureSlot>(slot);
        out->texture_coordinate_sets[slot] =
            static_cast<int32_t>(value.getTextureCoordinateSet(which));
        const TextureTransformEXT transform = value.getTextureTransform(which);
        out->texture_transforms[slot].struct_size =
            static_cast<uint32_t>(sizeof(CNA_TextureTransformEXT));
        out->texture_transforms[slot].struct_version = UINT32_C(1);
        out->texture_transforms[slot].offset.x = transform.Offset.X;
        out->texture_transforms[slot].offset.y = transform.Offset.Y;
        out->texture_transforms[slot].scale.x = transform.Scale.X;
        out->texture_transforms[slot].scale.y = transform.Scale.Y;
        out->texture_transforms[slot].rotation = transform.Rotation;
    }
}

template<typename TEffect>
[[nodiscard]] CNA_Result WithTypedEffect(
    const CNA_EffectHandle handle,
    const char* const typeName,
    const std::function<CNA_Result(TEffect&)>& body)
{
    std::shared_ptr<EffectResource> effect;
    if (const CNA_Result result = GetEffectForPass(handle, &effect);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    auto* const typed = dynamic_cast<TEffect*>(effect->value.get());
    if (typed == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            std::string("The effect is not a ") + typeName + ".");
    }
    return body(*typed);
}

} // namespace

CNA_Result cna_fullscreen_pass_create(
    const CNA_Handle graphicsDeviceHandle, CNA_FullscreenPassHandle* const outPass)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPass == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The full-screen pass output handle is null.");
        }
        *outPass = CNA_INVALID_HANDLE;
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result =
                GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto native = std::make_shared<Ext::FullscreenPass>(*graphicsDevice->value);
        const auto resource = std::make_shared<FullscreenPassResource>(
            FullscreenPassResource{std::move(native), graphicsDevice->parentGame});
        const CNA_Result result =
            GetRuntimeHandles().Create(ObjectKind::FullscreenPass, resource, outPass);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned full-screen pass handle could not be created.");
        }
        AddOwnedGraphicsResourceFor(graphicsDevice->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

namespace {

[[nodiscard]] CNA_Result FullscreenDraw(
    const CNA_FullscreenPassHandle passHandle,
    const CNA_Handle sourceHandle,
    const CNA_Handle destinationHandle,
    const CNA_EffectHandle effectHandle,
    const int32_t width,
    const int32_t height,
    const CNA_SamplerState* const sampler,
    const bool overCurrentTarget)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<FullscreenPassResource> pass;
        if (const CNA_Result result = GetEngineResource(
                passHandle, ObjectKind::FullscreenPass, "FullscreenPass", &pass);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Microsoft::Xna::Framework::Graphics::Texture2D* source = nullptr;
        std::shared_ptr<Texture2DResource> sourceRetention;
        if (const CNA_Result result =
                ResolveTexture2DArgument(sourceHandle, "source", &source, &sourceRetention);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Microsoft::Xna::Framework::Graphics::RenderTarget2D* destination = nullptr;
        std::shared_ptr<Texture2DResource> destinationRetention;
        if (!overCurrentTarget) {
            if (const CNA_Result result = ResolveRenderTarget2DArgument(
                    destinationHandle, &destination, &destinationRetention);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
        }
        Microsoft::Xna::Framework::Graphics::Effect* effect = nullptr;
        std::shared_ptr<EffectResource> effectRetention;
        if (effectHandle != CNA_INVALID_HANDLE) {
            if (const CNA_Result result = GetEffectForPass(effectHandle, &effectRetention);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            effect = effectRetention->value.get();
        }
        Microsoft::Xna::Framework::Graphics::SamplerState nativeSampler;
        Microsoft::Xna::Framework::Graphics::SamplerState* samplerArgument = nullptr;
        if (sampler != nullptr) {
            if (const CNA_Result result = ToNativeSamplerState(sampler, &nativeSampler);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            samplerArgument = &nativeSampler;
        }
        if (overCurrentTarget) {
            pass->value->drawOverCurrentTarget(
                source, effect, static_cast<int>(width), static_cast<int>(height), samplerArgument);
        } else {
            pass->value->draw(
                source,
                destination,
                effect,
                static_cast<int>(width),
                static_cast<int>(height),
                samplerArgument);
        }
        return CNA_RESULT_SUCCESS;
    });
}

} // namespace

CNA_Result cna_fullscreen_pass_draw(
    const CNA_FullscreenPassHandle pass,
    const CNA_Handle source,
    const CNA_Handle destination,
    const CNA_EffectHandle effect,
    const int32_t width,
    const int32_t height,
    const CNA_SamplerState* const sampler)
{
    return FullscreenDraw(pass, source, destination, effect, width, height, sampler, false);
}

CNA_Result cna_fullscreen_pass_draw_over_current_target(
    const CNA_FullscreenPassHandle pass,
    const CNA_Handle source,
    const CNA_EffectHandle effect,
    const int32_t width,
    const int32_t height,
    const CNA_SamplerState* const sampler)
{
    return FullscreenDraw(
        pass, source, CNA_INVALID_HANDLE, effect, width, height, sampler, true);
}

CNA_Result cna_fullscreen_pass_destroy(const CNA_FullscreenPassHandle passHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<FullscreenPassResource> pass;
        if (const CNA_Result result = GetEngineResource(
                passHandle, ObjectKind::FullscreenPass, "FullscreenPass", &pass);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(passHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The owned full-screen pass handle could not be released.");
        }
        RemoveOwnedGraphicsResourceFor(pass->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_blit_pass_create(
    const CNA_Handle graphicsDeviceHandle, CNA_PostProcessPassHandle* const outPass)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPass == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The blit-pass output handle is null.");
        }
        *outPass = CNA_INVALID_HANDLE;
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result =
                GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto native = std::make_shared<Ext::BlitPass>(*graphicsDevice->value);
        return CreatePassHandle(
            std::move(native),
            nullptr,
            graphicsDevice->parentGame,
            nullptr,
            CNA_INVALID_HANDLE,
            outPass);
    });
}

namespace {

[[nodiscard]] CNA_Result CreateEffectPass(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_EffectHandle effectHandle,
    const CNA_StringView name,
    const bool takeOwnership,
    CNA_PostProcessPassHandle* const outPass)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPass == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The effect-pass output handle is null.");
        }
        *outPass = CNA_INVALID_HANDLE;
        std::string nativeName;
        if (const CNA_Result result = CopyStringView(name, true, &nativeName);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result =
                GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<EffectResource> effect;
        if (effectHandle != CNA_INVALID_HANDLE) {
            if (const CNA_Result result = GetEffectForPass(effectHandle, &effect);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (effect->parentGame != graphicsDevice->parentGame) {
                return Fail(
                    CNA_RESULT_INVALID_HANDLE,
                    CNA_ERROR_CATEGORY_HANDLE,
                    "The effect is not owned by the game that owns this device.");
            }
        }
        if (takeOwnership && effectHandle == CNA_INVALID_HANDLE) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "An owning effect pass needs an effect to take over.");
        }

        auto native = std::make_shared<Ext::EffectPass>(
            *graphicsDevice->value,
            effect == nullptr ? nullptr : effect->value.get(),
            nativeName);
        auto* const asEffectPass = native.get();
        // Everything that can fail has failed by now, so consuming the caller's handle here cannot
        // strand the effect: on any earlier refusal the caller still owns it.
        if (takeOwnership) {
            const CNA_Result releaseResult = GetRuntimeHandles().Release(effectHandle);
            if (releaseResult != CNA_RESULT_SUCCESS) {
                return Fail(
                    releaseResult,
                    ErrorCategoryForResult(releaseResult),
                    "The effect handle could not be consumed by the pass.");
            }
            RemoveOwnedGraphicsResourceFor(effect->parentGame);
        }
        return CreatePassHandle(
            std::move(native),
            asEffectPass,
            graphicsDevice->parentGame,
            effect,
            takeOwnership ? effectHandle : CNA_INVALID_HANDLE,
            outPass);
    });
}

} // namespace

CNA_Result cna_post_process_effect_pass_create(
    const CNA_Handle graphicsDevice,
    const CNA_EffectHandle effect,
    const CNA_StringView name,
    CNA_PostProcessPassHandle* const outPass)
{
    return CreateEffectPass(graphicsDevice, effect, name, false, outPass);
}

CNA_Result cna_post_process_effect_pass_create_owning(
    const CNA_Handle graphicsDevice,
    const CNA_EffectHandle effect,
    const CNA_StringView name,
    CNA_PostProcessPassHandle* const outPass)
{
    return CreateEffectPass(graphicsDevice, effect, name, true, outPass);
}

CNA_Result cna_post_process_effect_pass_get_effect(
    const CNA_PostProcessPassHandle passHandle, CNA_EffectHandle* const outEffect)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEffect == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The effect output handle is null.");
        }
        *outEffect = CNA_INVALID_HANDLE;
        std::shared_ptr<PostProcessPassResource> pass;
        if (const CNA_Result result = GetEffectPass(passHandle, &pass);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto* const effect = pass->effectPass->getEffect();
        if (effect == nullptr) {
            return CNA_RESULT_SUCCESS;
        }
        const auto borrow = std::make_shared<CountedBorrow<PostProcessPassResource>>(pass);
        const std::shared_ptr<Microsoft::Xna::Framework::Graphics::Effect> view(borrow, effect);
        return CreateBorrowedEffect(view, pass->parentGame, outEffect);
    });
}

CNA_Result cna_post_process_effect_pass_set_effect(
    const CNA_PostProcessPassHandle passHandle, const CNA_EffectHandle effectHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<PostProcessPassResource> pass;
        if (const CNA_Result result = GetEffectPass(passHandle, &pass);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<EffectResource> effect;
        if (effectHandle != CNA_INVALID_HANDLE) {
            if (const CNA_Result result = GetEffectForPass(effectHandle, &effect);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (effect->parentGame != pass->parentGame) {
                return Fail(
                    CNA_RESULT_INVALID_HANDLE,
                    CNA_ERROR_CATEGORY_HANDLE,
                    "The effect is not owned by the game that owns this pass.");
            }
        }
        pass->effectPass->setEffect(effect == nullptr ? nullptr : effect->value.get());
        // Retain the new borrow, and stop retaining the old one, so the pass can never draw
        // through an effect the caller has since released.
        pass->effectRetention = effect;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_post_process_pass_apply(
    const CNA_PostProcessPassHandle passHandle, const CNA_PostProcessContext* const context)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (context == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The post-process context is null.");
        }
        std::shared_ptr<PostProcessPassResource> pass;
        if (const CNA_Result result = GetPostProcessPass(passHandle, &pass);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        ResolvedPostProcessContext resolved;
        if (const CNA_Result result = ResolvePostProcessContext(*context, &resolved);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        pass->value->apply(resolved.value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_post_process_pass_copy_name(
    const CNA_PostProcessPassHandle passHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    std::shared_ptr<PostProcessPassResource> pass;
    if (const CNA_Result result = GetPostProcessPass(passHandle, &pass);
        result != CNA_RESULT_SUCCESS) {
        if (outBytes != nullptr) {
            *outBytes = UINT64_C(0);
        }
        return result;
    }
    return CopyFormattedString(destination, capacity, outBytes, [&pass] {
        return pass->value->getName();
    });
}

CNA_Result cna_post_process_pass_is_supported(
    const CNA_PostProcessPassHandle passHandle,
    const CNA_Handle graphicsDeviceHandle,
    CNA_Bool* const outSupported)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSupported == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The support output is null.");
        }
        std::shared_ptr<PostProcessPassResource> pass;
        if (const CNA_Result result = GetPostProcessPass(passHandle, &pass);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result =
                GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outSupported = pass->value->isSupported(*graphicsDevice->value) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_post_process_pass_destroy(const CNA_PostProcessPassHandle passHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<PostProcessPassResource> pass;
        if (const CNA_Result result = GetPostProcessPass(passHandle, &pass);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Handle ownedEffect = pass->ownedEffect;
        const CNA_Handle parentGame = pass->parentGame;
        const CNA_Result releaseResult = GetRuntimeHandles().Release(passHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The owned post-process pass handle could not be released.");
        }
        RemoveOwnedGraphicsResourceFor(parentGame);
        // An owning pass destroys its effect with itself, which is what the canonical unique_ptr
        // constructor promises. The handle was already consumed at creation, so only the resource
        // accounting is left to undo here.
        (void)ownedEffect;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_effect_apply_material(
    const CNA_EffectHandle effect, const CNA_PbrMaterialEXT* const material)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (material == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, "The material is null.");
        }
        return WithTypedEffect<PbrEffect>(effect, "PbrEffect", [&](PbrEffect& typed) {
            Ext::PbrMaterial native;
            if (const CNA_Result result = ToNativePbrMaterial(*material, &native);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            Ext::applyMaterial(native, typed);
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_skinned_pbr_effect_apply_material(
    const CNA_EffectHandle effect, const CNA_PbrMaterialEXT* const material)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (material == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, "The material is null.");
        }
        return WithTypedEffect<SkinnedPbrEffect>(
            effect, "SkinnedPbrEffect", [&](SkinnedPbrEffect& typed) {
                Ext::PbrMaterial native;
                if (const CNA_Result result = ToNativePbrMaterial(*material, &native);
                    result != CNA_RESULT_SUCCESS) {
                    return result;
                }
                Ext::applyMaterial(native, typed);
                return CNA_RESULT_SUCCESS;
            });
    });
}

CNA_Result cna_pbr_effect_extract_material(
    const CNA_EffectHandle effect, CNA_PbrMaterialEXT* const outMaterial)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outMaterial == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The material output is null.");
        }
        return WithTypedEffect<PbrEffect>(effect, "PbrEffect", [&](PbrEffect& typed) {
            FromNativePbrMaterial(Ext::extractMaterial(typed), outMaterial);
            return CNA_RESULT_SUCCESS;
        });
    });
}

CNA_Result cna_skinned_pbr_effect_extract_material(
    const CNA_EffectHandle effect, CNA_PbrMaterialEXT* const outMaterial)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outMaterial == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The material output is null.");
        }
        return WithTypedEffect<SkinnedPbrEffect>(
            effect, "SkinnedPbrEffect", [&](SkinnedPbrEffect& typed) {
                FromNativePbrMaterial(Ext::extractMaterial(typed), outMaterial);
                return CNA_RESULT_SUCCESS;
            });
    });
}

CNA_Result cna_pbr_material_apply_state(
    const CNA_PbrMaterialEXT* const material, const CNA_Handle graphicsDeviceHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (material == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, "The material is null.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result =
                GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Ext::PbrMaterial native;
        if (const CNA_Result result = ToNativePbrMaterial(*material, &native);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Ext::applyMaterialState(native, *graphicsDevice->value);
        return CNA_RESULT_SUCCESS;
    });
}

namespace {

using Microsoft::Xna::Framework::BoundingBox;

struct ShadowMapResource final {
    std::shared_ptr<Ext::ShadowMap> value;
    CNA_Handle parentGame;
    uint64_t activeBorrowCount = 0U;
};

struct SpotShadowMapResource final {
    std::shared_ptr<Ext::SpotShadowMap> value;
    CNA_Handle parentGame;
    uint64_t activeBorrowCount = 0U;
};

[[nodiscard]] CNA_Matrix ToCMatrix(const Microsoft::Xna::Framework::Matrix& value) noexcept
{
    CNA_Matrix result;
    result.m11 = value.M11; result.m12 = value.M12; result.m13 = value.M13; result.m14 = value.M14;
    result.m21 = value.M21; result.m22 = value.M22; result.m23 = value.M23; result.m24 = value.M24;
    result.m31 = value.M31; result.m32 = value.M32; result.m33 = value.M33; result.m34 = value.M34;
    result.m41 = value.M41; result.m42 = value.M42; result.m43 = value.M43; result.m44 = value.M44;
    return result;
}

[[nodiscard]] CNA_Result ToNativeShadowQuality(
    const CNA_ShadowQuality value, Ext::ShadowQuality* const out)
{
    if (value > CNA_SHADOW_QUALITY_ULTRA) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The shadow quality is not a defined CNA_SHADOW_QUALITY_* value.");
    }
    *out = static_cast<Ext::ShadowQuality>(value);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ToNativeDirectionalLight(
    const CNA_DirectionalLightEXT* const value, Ext::DirectionalLightEXT* const out)
{
    if (value == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, "The light is null.");
    }
    if (value->struct_size != static_cast<uint32_t>(sizeof(CNA_DirectionalLightEXT)) ||
        value->struct_version != UINT32_C(1)) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The light was not initialized by cna_directional_light_ext_init.");
    }
    if (const CNA_Result result =
            ValidateCanonicalBool(value->casts_shadows, "casts_shadows");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    out->Direction = {value->direction.x, value->direction.y, value->direction.z};
    out->Color = {value->color.x, value->color.y, value->color.z};
    out->Intensity = value->intensity;
    out->CastsShadows = value->casts_shadows == CNA_TRUE;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ToNativeSpotLight(
    const CNA_SpotLightEXT* const value, Ext::SpotLightEXT* const out)
{
    if (value == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, "The light is null.");
    }
    if (value->struct_size != static_cast<uint32_t>(sizeof(CNA_SpotLightEXT)) ||
        value->struct_version != UINT32_C(1)) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The light was not initialized by cna_spot_light_ext_init.");
    }
    if (const CNA_Result result =
            ValidateCanonicalBool(value->casts_shadows, "casts_shadows");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    out->Position = {value->position.x, value->position.y, value->position.z};
    out->Direction = {value->direction.x, value->direction.y, value->direction.z};
    out->Color = {value->color.x, value->color.y, value->color.z};
    out->Intensity = value->intensity;
    out->Range = value->range;
    out->InnerAngle = value->inner_angle;
    out->OuterAngle = value->outer_angle;
    out->CastsShadows = value->casts_shadows == CNA_TRUE;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ToNativeBounds(
    const CNA_BoundingBox* const value, BoundingBox* const out)
{
    if (value == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, "The scene bounds are null.");
    }
    out->Min = {value->min.x, value->min.y, value->min.z};
    out->Max = {value->max.x, value->max.y, value->max.z};
    return CNA_RESULT_SUCCESS;
}

// A caster effect and a shadow texture both belong to the map. Handing either out as a borrow
// keeps the map alive behind it, which is what stops a caller destroying the map and then drawing
// through an effect that no longer exists.
template<typename TResource>
[[nodiscard]] CNA_Result BorrowEffectFrom(
    const std::shared_ptr<TResource>& owner,
    Microsoft::Xna::Framework::Graphics::Effect* const effect,
    CNA_EffectHandle* const outEffect)
{
    if (effect == nullptr) {
        *outEffect = CNA_INVALID_HANDLE;
        return CNA_RESULT_SUCCESS;
    }
    const auto borrow = std::make_shared<CountedBorrow<TResource>>(owner);
    const std::shared_ptr<Microsoft::Xna::Framework::Graphics::Effect> view(borrow, effect);
    return CreateBorrowedEffect(view, owner->parentGame, outEffect);
}

template<typename TResource>
[[nodiscard]] CNA_Result BorrowShadowTextureFrom(
    const std::shared_ptr<TResource>& owner,
    Microsoft::Xna::Framework::Graphics::Texture2D* const texture,
    CNA_Handle* const outTexture)
{
    if (texture == nullptr) {
        *outTexture = CNA_INVALID_HANDLE;
        return CNA_RESULT_SUCCESS;
    }
    const auto borrow = std::make_shared<CountedBorrow<TResource>>(owner);
    const std::shared_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> view(borrow, texture);
    return CreateBorrowedRenderTarget2D(view, owner->parentGame, borrow, outTexture);
}

} // namespace

CNA_Result cna_shadow_map_create(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_ShadowQuality quality,
    CNA_ShadowMapHandle* const outShadowMap)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outShadowMap == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The shadow-map output handle is null.");
        }
        *outShadowMap = CNA_INVALID_HANDLE;
        Ext::ShadowQuality nativeQuality{};
        if (const CNA_Result result = ToNativeShadowQuality(quality, &nativeQuality);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result =
                GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto native = std::make_shared<Ext::ShadowMap>(*graphicsDevice->value, nativeQuality);
        const auto resource = std::make_shared<ShadowMapResource>(
            ShadowMapResource{std::move(native), graphicsDevice->parentGame, 0U});
        const CNA_Result result =
            GetRuntimeHandles().Create(ObjectKind::ShadowMap, resource, outShadowMap);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned shadow-map handle could not be created.");
        }
        AddOwnedGraphicsResourceFor(graphicsDevice->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

namespace {

[[nodiscard]] CNA_Result GetShadowMap(
    const CNA_Handle handle, std::shared_ptr<ShadowMapResource>* const out)
{
    return GetEngineResource(handle, ObjectKind::ShadowMap, "ShadowMap", out);
}

[[nodiscard]] CNA_Result GetSpotShadowMap(
    const CNA_Handle handle, std::shared_ptr<SpotShadowMapResource>* const out)
{
    return GetEngineResource(handle, ObjectKind::SpotShadowMap, "SpotShadowMap", out);
}

template<typename TResource, typename TCallable>
[[nodiscard]] CNA_Result WithMap(
    const CNA_Handle handle,
    const ObjectKind kind,
    const char* const name,
    TCallable&& body)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<TResource> resource;
        if (const CNA_Result result = GetEngineResource(handle, kind, name, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return std::forward<TCallable>(body)(resource);
    });
}

} // namespace

CNA_Result cna_shadow_map_is_supported(
    const CNA_ShadowMapHandle shadowMap, CNA_Bool* const outSupported)
{
    return WithMap<ShadowMapResource>(
        shadowMap, ObjectKind::ShadowMap, "ShadowMap",
        [&](const std::shared_ptr<ShadowMapResource>& map) -> CNA_Result {
            if (outSupported == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The support output is null.");
            }
            *outSupported = map->value->isSupported() ? CNA_TRUE : CNA_FALSE;
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_shadow_map_begin(
    const CNA_ShadowMapHandle shadowMap,
    const CNA_DirectionalLightEXT* const light,
    const CNA_BoundingBox* const sceneBounds)
{
    return WithMap<ShadowMapResource>(
        shadowMap, ObjectKind::ShadowMap, "ShadowMap",
        [&](const std::shared_ptr<ShadowMapResource>& map) -> CNA_Result {
            Ext::DirectionalLightEXT nativeLight;
            if (const CNA_Result result = ToNativeDirectionalLight(light, &nativeLight);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            BoundingBox nativeBounds;
            if (const CNA_Result result = ToNativeBounds(sceneBounds, &nativeBounds);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            map->value->begin(nativeLight, nativeBounds);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_shadow_map_end(const CNA_ShadowMapHandle shadowMap)
{
    return WithMap<ShadowMapResource>(
        shadowMap, ObjectKind::ShadowMap, "ShadowMap",
        [](const std::shared_ptr<ShadowMapResource>& map) -> CNA_Result {
            map->value->end();
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_shadow_map_get_caster_effect(
    const CNA_ShadowMapHandle shadowMap, CNA_EffectHandle* const outEffect)
{
    return WithMap<ShadowMapResource>(
        shadowMap, ObjectKind::ShadowMap, "ShadowMap",
        [&](const std::shared_ptr<ShadowMapResource>& map) -> CNA_Result {
            if (outEffect == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The effect output handle is null.");
            }
            *outEffect = CNA_INVALID_HANDLE;
            return BorrowEffectFrom(map, map->value->getCasterEffect(), outEffect);
        });
}

CNA_Result cna_shadow_map_get_skinned_caster_effect(
    const CNA_ShadowMapHandle shadowMap, CNA_EffectHandle* const outEffect)
{
    return WithMap<ShadowMapResource>(
        shadowMap, ObjectKind::ShadowMap, "ShadowMap",
        [&](const std::shared_ptr<ShadowMapResource>& map) -> CNA_Result {
            if (outEffect == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The effect output handle is null.");
            }
            *outEffect = CNA_INVALID_HANDLE;
            return BorrowEffectFrom(map, map->value->getSkinnedCasterEffect(), outEffect);
        });
}

CNA_Result cna_shadow_map_apply_caster(const CNA_ShadowMapHandle shadowMap)
{
    return WithMap<ShadowMapResource>(
        shadowMap, ObjectKind::ShadowMap, "ShadowMap",
        [](const std::shared_ptr<ShadowMapResource>& map) -> CNA_Result {
            map->value->applyCaster();
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_shadow_map_apply_skinned_caster(
    const CNA_ShadowMapHandle shadowMap,
    const CNA_Matrix* const boneTransforms,
    const uint64_t boneCount,
    const int32_t weightsPerVertex)
{
    return WithMap<ShadowMapResource>(
        shadowMap, ObjectKind::ShadowMap, "ShadowMap",
        [&](const std::shared_ptr<ShadowMapResource>& map) -> CNA_Result {
            if (boneTransforms == nullptr && boneCount != 0U) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The bone-transform array is null.");
            }
            if (boneCount > static_cast<uint64_t>(SIZE_MAX / sizeof(CNA_Matrix))) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_RANGE,
                    "The bone count does not fit this platform's size type.");
            }
            std::vector<Microsoft::Xna::Framework::Matrix> bones;
            bones.reserve(static_cast<std::size_t>(boneCount));
            for (uint64_t bone = 0U; bone < boneCount; ++bone) {
                bones.push_back(ToNativeMatrix(boneTransforms[bone]));
            }
            map->value->applySkinnedCaster(bones, static_cast<int>(weightsPerVertex));
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_shadow_map_get_shadow_texture(
    const CNA_ShadowMapHandle shadowMap, CNA_Handle* const outTexture)
{
    return WithMap<ShadowMapResource>(
        shadowMap, ObjectKind::ShadowMap, "ShadowMap",
        [&](const std::shared_ptr<ShadowMapResource>& map) -> CNA_Result {
            if (outTexture == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The texture output handle is null.");
            }
            *outTexture = CNA_INVALID_HANDLE;
            return BorrowShadowTextureFrom(map, map->value->getShadowTexture(), outTexture);
        });
}

CNA_Result cna_shadow_map_get_light_view_projection(
    const CNA_ShadowMapHandle shadowMap, CNA_Matrix* const outMatrix)
{
    return WithMap<ShadowMapResource>(
        shadowMap, ObjectKind::ShadowMap, "ShadowMap",
        [&](const std::shared_ptr<ShadowMapResource>& map) -> CNA_Result {
            return StoreValue(outMatrix, ToCMatrix(map->value->getLightViewProjection()));
        });
}

CNA_Result cna_shadow_map_get_size(const CNA_ShadowMapHandle shadowMap, int32_t* const outSize)
{
    return WithMap<ShadowMapResource>(
        shadowMap, ObjectKind::ShadowMap, "ShadowMap",
        [&](const std::shared_ptr<ShadowMapResource>& map) -> CNA_Result {
            return StoreValue(outSize, static_cast<int32_t>(map->value->getSize()));
        });
}

CNA_Result cna_shadow_map_get_quality(
    const CNA_ShadowMapHandle shadowMap, CNA_ShadowQuality* const outQuality)
{
    return WithMap<ShadowMapResource>(
        shadowMap, ObjectKind::ShadowMap, "ShadowMap",
        [&](const std::shared_ptr<ShadowMapResource>& map) -> CNA_Result {
            return StoreValue(outQuality, NativeOrdinal(map->value->getQuality()));
        });
}

CNA_Result cna_shadow_map_get_depth_bias(
    const CNA_ShadowMapHandle shadowMap, float* const outBias)
{
    return WithMap<ShadowMapResource>(
        shadowMap, ObjectKind::ShadowMap, "ShadowMap",
        [&](const std::shared_ptr<ShadowMapResource>& map) -> CNA_Result {
            return StoreValue(outBias, map->value->getDepthBias());
        });
}

CNA_Result cna_shadow_map_set_depth_bias(const CNA_ShadowMapHandle shadowMap, const float bias)
{
    return WithMap<ShadowMapResource>(
        shadowMap, ObjectKind::ShadowMap, "ShadowMap",
        [&](const std::shared_ptr<ShadowMapResource>& map) -> CNA_Result {
            map->value->setDepthBias(bias);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_shadow_map_get_filter_radius(
    const CNA_ShadowMapHandle shadowMap, int32_t* const outRadius)
{
    return WithMap<ShadowMapResource>(
        shadowMap, ObjectKind::ShadowMap, "ShadowMap",
        [&](const std::shared_ptr<ShadowMapResource>& map) -> CNA_Result {
            return StoreValue(outRadius, static_cast<int32_t>(map->value->getFilterRadius()));
        });
}

CNA_Result cna_shadow_map_compute_light_view(
    const CNA_DirectionalLightEXT* const light,
    const CNA_BoundingBox* const sceneBounds,
    CNA_Matrix* const outMatrix)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Ext::DirectionalLightEXT nativeLight;
        if (const CNA_Result result = ToNativeDirectionalLight(light, &nativeLight);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        BoundingBox nativeBounds;
        if (const CNA_Result result = ToNativeBounds(sceneBounds, &nativeBounds);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return StoreValue(
            outMatrix, ToCMatrix(Ext::ShadowMap::computeLightView(nativeLight, nativeBounds)));
    });
}

CNA_Result cna_shadow_map_compute_light_projection(
    const CNA_Matrix* const lightView,
    const CNA_BoundingBox* const sceneBounds,
    CNA_Matrix* const outMatrix)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (lightView == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The light view is null.");
        }
        BoundingBox nativeBounds;
        if (const CNA_Result result = ToNativeBounds(sceneBounds, &nativeBounds);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return StoreValue(
            outMatrix,
            ToCMatrix(Ext::ShadowMap::computeLightProjection(
                ToNativeMatrix(*lightView), nativeBounds)));
    });
}

CNA_Result cna_shadow_map_size_for_quality(
    const CNA_ShadowQuality quality, int32_t* const outSize)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Ext::ShadowQuality nativeQuality{};
        if (const CNA_Result result = ToNativeShadowQuality(quality, &nativeQuality);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return StoreValue(
            outSize, static_cast<int32_t>(Ext::ShadowMap::sizeForQuality(nativeQuality)));
    });
}

CNA_Result cna_shadow_map_filter_radius_for_quality(
    const CNA_ShadowQuality quality, int32_t* const outRadius)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Ext::ShadowQuality nativeQuality{};
        if (const CNA_Result result = ToNativeShadowQuality(quality, &nativeQuality);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return StoreValue(
            outRadius, static_cast<int32_t>(Ext::ShadowMap::filterRadiusForQuality(nativeQuality)));
    });
}

CNA_Result cna_shadow_map_destroy(const CNA_ShadowMapHandle shadowMapHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ShadowMapResource> map;
        if (const CNA_Result result = GetShadowMap(shadowMapHandle, &map);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The effects and the texture this map hands out are borrows that keep it alive.
        // Destroying it underneath one would leave a handle naming an object that is gone,
        // so the borrow is what has to be released first.
        if (map->activeBorrowCount != 0U) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The shadow map is still lending an effect or its shadow texture.");
        }
        // The effects and the texture this map hands out are borrows that keep it alive.
        // Destroying it underneath one would leave a handle naming an object that is gone,
        // so the borrow is what has to be released first.
        if (map->activeBorrowCount != 0U) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The spot shadow map is still lending an effect or its shadow texture.");
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(shadowMapHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The owned shadow-map handle could not be released.");
        }
        RemoveOwnedGraphicsResourceFor(map->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_spot_shadow_map_create(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_ShadowQuality quality,
    CNA_SpotShadowMapHandle* const outShadowMap)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outShadowMap == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The spot-shadow-map output handle is null.");
        }
        *outShadowMap = CNA_INVALID_HANDLE;
        Ext::ShadowQuality nativeQuality{};
        if (const CNA_Result result = ToNativeShadowQuality(quality, &nativeQuality);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result =
                GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto native = std::make_shared<Ext::SpotShadowMap>(*graphicsDevice->value, nativeQuality);
        const auto resource = std::make_shared<SpotShadowMapResource>(
            SpotShadowMapResource{std::move(native), graphicsDevice->parentGame, 0U});
        const CNA_Result result =
            GetRuntimeHandles().Create(ObjectKind::SpotShadowMap, resource, outShadowMap);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned spot-shadow-map handle could not be created.");
        }
        AddOwnedGraphicsResourceFor(graphicsDevice->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_spot_shadow_map_is_supported(
    const CNA_SpotShadowMapHandle shadowMap, CNA_Bool* const outSupported)
{
    return WithMap<SpotShadowMapResource>(
        shadowMap, ObjectKind::SpotShadowMap, "SpotShadowMap",
        [&](const std::shared_ptr<SpotShadowMapResource>& map) -> CNA_Result {
            return StoreValue(
                outSupported, static_cast<CNA_Bool>(
                    map->value->isSupported() ? CNA_TRUE : CNA_FALSE));
        });
}

CNA_Result cna_spot_shadow_map_begin(
    const CNA_SpotShadowMapHandle shadowMap, const CNA_SpotLightEXT* const light)
{
    return WithMap<SpotShadowMapResource>(
        shadowMap, ObjectKind::SpotShadowMap, "SpotShadowMap",
        [&](const std::shared_ptr<SpotShadowMapResource>& map) -> CNA_Result {
            Ext::SpotLightEXT nativeLight;
            if (const CNA_Result result = ToNativeSpotLight(light, &nativeLight);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            map->value->begin(nativeLight);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_spot_shadow_map_end(const CNA_SpotShadowMapHandle shadowMap)
{
    return WithMap<SpotShadowMapResource>(
        shadowMap, ObjectKind::SpotShadowMap, "SpotShadowMap",
        [](const std::shared_ptr<SpotShadowMapResource>& map) -> CNA_Result {
            map->value->end();
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_spot_shadow_map_get_shadow_texture(
    const CNA_SpotShadowMapHandle shadowMap, CNA_Handle* const outTexture)
{
    return WithMap<SpotShadowMapResource>(
        shadowMap, ObjectKind::SpotShadowMap, "SpotShadowMap",
        [&](const std::shared_ptr<SpotShadowMapResource>& map) -> CNA_Result {
            if (outTexture == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The texture output handle is null.");
            }
            *outTexture = CNA_INVALID_HANDLE;
            return BorrowShadowTextureFrom(map, map->value->getShadowTexture(), outTexture);
        });
}

CNA_Result cna_spot_shadow_map_get_caster_effect(
    const CNA_SpotShadowMapHandle shadowMap, CNA_EffectHandle* const outEffect)
{
    return WithMap<SpotShadowMapResource>(
        shadowMap, ObjectKind::SpotShadowMap, "SpotShadowMap",
        [&](const std::shared_ptr<SpotShadowMapResource>& map) -> CNA_Result {
            if (outEffect == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The effect output handle is null.");
            }
            *outEffect = CNA_INVALID_HANDLE;
            return BorrowEffectFrom(map, map->value->getCasterEffect(), outEffect);
        });
}

CNA_Result cna_spot_shadow_map_get_size(
    const CNA_SpotShadowMapHandle shadowMap, int32_t* const outSize)
{
    return WithMap<SpotShadowMapResource>(
        shadowMap, ObjectKind::SpotShadowMap, "SpotShadowMap",
        [&](const std::shared_ptr<SpotShadowMapResource>& map) -> CNA_Result {
            return StoreValue(outSize, static_cast<int32_t>(map->value->getSize()));
        });
}

CNA_Result cna_spot_shadow_map_get_quality(
    const CNA_SpotShadowMapHandle shadowMap, CNA_ShadowQuality* const outQuality)
{
    return WithMap<SpotShadowMapResource>(
        shadowMap, ObjectKind::SpotShadowMap, "SpotShadowMap",
        [&](const std::shared_ptr<SpotShadowMapResource>& map) -> CNA_Result {
            return StoreValue(outQuality, NativeOrdinal(map->value->getQuality()));
        });
}

CNA_Result cna_spot_shadow_map_get_light_view_projection(
    const CNA_SpotShadowMapHandle shadowMap, CNA_Matrix* const outMatrix)
{
    return WithMap<SpotShadowMapResource>(
        shadowMap, ObjectKind::SpotShadowMap, "SpotShadowMap",
        [&](const std::shared_ptr<SpotShadowMapResource>& map) -> CNA_Result {
            return StoreValue(outMatrix, ToCMatrix(map->value->getLightViewProjection()));
        });
}

CNA_Result cna_spot_shadow_map_get_light_position(
    const CNA_SpotShadowMapHandle shadowMap, CNA_Vector3* const outPosition)
{
    return WithMap<SpotShadowMapResource>(
        shadowMap, ObjectKind::SpotShadowMap, "SpotShadowMap",
        [&](const std::shared_ptr<SpotShadowMapResource>& map) -> CNA_Result {
            const auto position = map->value->getLightPosition();
            return StoreValue(outPosition, Vec3(position.X, position.Y, position.Z));
        });
}

CNA_Result cna_spot_shadow_map_get_light_range(
    const CNA_SpotShadowMapHandle shadowMap, float* const outRange)
{
    return WithMap<SpotShadowMapResource>(
        shadowMap, ObjectKind::SpotShadowMap, "SpotShadowMap",
        [&](const std::shared_ptr<SpotShadowMapResource>& map) -> CNA_Result {
            return StoreValue(outRange, map->value->getLightRange());
        });
}

CNA_Result cna_spot_shadow_map_get_depth_bias(
    const CNA_SpotShadowMapHandle shadowMap, float* const outBias)
{
    return WithMap<SpotShadowMapResource>(
        shadowMap, ObjectKind::SpotShadowMap, "SpotShadowMap",
        [&](const std::shared_ptr<SpotShadowMapResource>& map) -> CNA_Result {
            return StoreValue(outBias, map->value->getDepthBias());
        });
}

CNA_Result cna_spot_shadow_map_set_depth_bias(
    const CNA_SpotShadowMapHandle shadowMap, const float bias)
{
    return WithMap<SpotShadowMapResource>(
        shadowMap, ObjectKind::SpotShadowMap, "SpotShadowMap",
        [&](const std::shared_ptr<SpotShadowMapResource>& map) -> CNA_Result {
            map->value->setDepthBias(bias);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_spot_shadow_map_compute_light_view(
    const CNA_SpotLightEXT* const light, CNA_Matrix* const outMatrix)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Ext::SpotLightEXT nativeLight;
        if (const CNA_Result result = ToNativeSpotLight(light, &nativeLight);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return StoreValue(
            outMatrix, ToCMatrix(Ext::SpotShadowMap::computeLightView(nativeLight)));
    });
}

CNA_Result cna_spot_shadow_map_compute_light_projection(
    const CNA_SpotLightEXT* const light, CNA_Matrix* const outMatrix)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Ext::SpotLightEXT nativeLight;
        if (const CNA_Result result = ToNativeSpotLight(light, &nativeLight);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return StoreValue(
            outMatrix, ToCMatrix(Ext::SpotShadowMap::computeLightProjection(nativeLight)));
    });
}

CNA_Result cna_spot_shadow_map_destroy(const CNA_SpotShadowMapHandle shadowMapHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<SpotShadowMapResource> map;
        if (const CNA_Result result = GetSpotShadowMap(shadowMapHandle, &map);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(shadowMapHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The owned spot-shadow-map handle could not be released.");
        }
        RemoveOwnedGraphicsResourceFor(map->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

#endif // CNA_CNAEXT

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
#include "Microsoft/Xna/Framework/Graphics/IShadowReceiverEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/PunctualLightEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShadowCascadeStateEXT.hpp"
#include "CNA/GraphicsMemoryBarrier.hpp"

#include <cstring>
#include <memory>
#include <string>

#ifdef CNA_CNAEXT
#include "CNA/Graphics/BlitPass.hpp"
#include "CNA/Graphics/CascadedShadowMap.hpp"
#include "CNA/Graphics/ClusteredLightAssignment.hpp"
#include "CNA/Graphics/ClusteredLightBuffer.hpp"
#include "CNA/Graphics/ClusteredLightEXT.hpp"
#include "CNA/Graphics/ClusteredForwardEffect.hpp"
#include "CNA/Graphics/PbrMaterialExtensions.hpp"
#include "CNA/Graphics/AerialPerspectivePass.hpp"
#include "CNA/Graphics/DepthOfFieldPass.hpp"
#include "CNA/Graphics/HeightFogPass.hpp"
#include "CNA/Graphics/LightShaftPass.hpp"
#include "CNA/Graphics/VolumetricFogPass.hpp"
#include "CNA/Graphics/PostProcessChain.hpp"
#include "CNA/Graphics/SsaoPass.hpp"
#include "CNA/Graphics/SsrPass.hpp"
#include "CNA/Graphics/RenderPipeline.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/GltfMaterialBridge.hpp"
#include "CNA/Graphics/TransparencyMode.hpp"
#include "CNA/Graphics/TransparentDrawList.hpp"
#include "CNA/Graphics/WeightedBlendedTransparency.hpp"
#include "CNA/Graphics/ThinFilmIridescence.hpp"
#include "CNA/Graphics/ClusteredLightCompute.hpp"
#include "CNA/Graphics/ClusteredLightGrid.hpp"
#include "CNA/Graphics/ClusteredLightSetEXT.hpp"
#include "CNA/Graphics/ClusteredLightType.hpp"
#include "CNA/Graphics/ClusteredShadowPolicyEXT.hpp"
#include "CNA/Graphics/ComputeShader.hpp"
#include "CNA/Graphics/ContactShadowPass.hpp"
#include "CNA/Graphics/CubeShadowMap.hpp"
#include "CNA/Graphics/DepthEncoding.hpp"
#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/Graphics/DirectionalLightEXT.hpp"
#include "CNA/Graphics/PointLightEXT.hpp"
#include "CNA/Graphics/ShadowMap.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
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

#include <array>
#include <limits>
#include <tuple>
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
using CNA::C::Detail::CreateOwnedTextureCube;
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

CNA_Result cna_clustered_light_ext_init(CNA_ClusteredLightEXT* const outLight)
{
    CNA_ClusteredLightEXT defaults;
    std::memset(&defaults, 0, sizeof(defaults));
    defaults.struct_size = static_cast<uint32_t>(sizeof(CNA_ClusteredLightEXT));
    defaults.struct_version = UINT32_C(1);
    defaults.type = CNA_CLUSTERED_LIGHT_TYPE_POINT;
    defaults.casts_shadows = CNA_FALSE;
    defaults.position = Vec3(0.0F, 0.0F, 0.0F);
    defaults.direction = Vec3(0.0F, -1.0F, 0.0F);
    defaults.color = Vec3(1.0F, 1.0F, 1.0F);
    defaults.intensity = 1.0F;
    defaults.range = 20.0F;
    defaults.inner_angle = 0.35F;
    defaults.outer_angle = 0.5F;
#ifdef CNA_CNAEXT
    {
        const CNA::Graphics::ClusteredLightEXT canonical;
        if (static_cast<uint32_t>(canonical.Type) != defaults.type ||
            canonical.Direction.Y != defaults.direction.y ||
            canonical.Intensity != defaults.intensity || canonical.Range != defaults.range ||
            canonical.InnerAngle != defaults.inner_angle ||
            canonical.OuterAngle != defaults.outer_angle ||
            canonical.CastsShadows != (defaults.casts_shadows == CNA_TRUE)) {
            return Fail(
                CNA_RESULT_INTERNAL,
                CNA_ERROR_CATEGORY_INTERNAL,
                "The C clustered-light defaults disagree with the canonical structure.");
        }
    }
#endif
    return StoreValue(outLight, defaults);
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


CNA_Result cna_cascaded_shadow_map_create(CNA_Handle p0, CNA_ShadowQuality p1, int32_t p2, CNA_CascadedShadowMapHandle* p3)
{
    (void)p0; (void)p1; (void)p2; (void)p3;
    if (p3 != nullptr) { *p3 = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_cascaded_shadow_map_is_supported(CNA_CascadedShadowMapHandle p0, CNA_Bool* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_cascaded_shadow_map_update(CNA_CascadedShadowMapHandle p0, const CNA_DirectionalLightEXT* p1, const CNA_Matrix* p2, const CNA_Matrix* p3)
{
    (void)p0; (void)p1; (void)p2; (void)p3;
    return ExtensionUnavailable();
}

CNA_Result cna_cascaded_shadow_map_begin(CNA_CascadedShadowMapHandle p0, int32_t p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_cascaded_shadow_map_end(CNA_CascadedShadowMapHandle p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_cascaded_shadow_map_get_cascade_count(CNA_CascadedShadowMapHandle p0, int32_t* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_cascaded_shadow_map_get_cascade_size(CNA_CascadedShadowMapHandle p0, int32_t* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_cascaded_shadow_map_get_shadow_texture(CNA_CascadedShadowMapHandle p0, CNA_Handle* p1)
{
    (void)p0; (void)p1;
    if (p1 != nullptr) { *p1 = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_cascaded_shadow_map_get_caster_effect(CNA_CascadedShadowMapHandle p0, CNA_EffectHandle* p1)
{
    (void)p0; (void)p1;
    if (p1 != nullptr) { *p1 = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_cascaded_shadow_map_get_cascade_matrix(CNA_CascadedShadowMapHandle p0, int32_t p1, CNA_Matrix* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_cascaded_shadow_map_get_split_distance(CNA_CascadedShadowMapHandle p0, int32_t p1, float* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_cascaded_shadow_map_get_blend_band(CNA_CascadedShadowMapHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_cascaded_shadow_map_set_blend_band(CNA_CascadedShadowMapHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_cascaded_shadow_map_is_debug_tint_enabled(CNA_CascadedShadowMapHandle p0, CNA_Bool* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_cascaded_shadow_map_set_debug_tint_enabled(CNA_CascadedShadowMapHandle p0, CNA_Bool p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_cascaded_shadow_map_select_cascade(CNA_CascadedShadowMapHandle p0, float p1, int32_t* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_cascaded_shadow_map_get_split_lambda(CNA_CascadedShadowMapHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_cascaded_shadow_map_set_split_lambda(CNA_CascadedShadowMapHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_cascaded_shadow_map_compute_split_distances(float p0, float p1, int32_t p2, float p3, float* p4, uint64_t p5, uint64_t* p6)
{
    (void)p0; (void)p1; (void)p2; (void)p3; (void)p4; (void)p5; (void)p6;
    return ExtensionUnavailable();
}

CNA_Result cna_cascaded_shadow_map_compute_frustum_corners(const CNA_Matrix* p0, const CNA_Matrix* p1, CNA_Vector3* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_cascaded_shadow_map_compute_bounding_sphere(const CNA_Vector3* p0, CNA_Vector3* p1, float* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_cascaded_shadow_map_snap_to_texel_grid(const CNA_Vector3* p0, float p1, int32_t p2, CNA_Vector3* p3)
{
    (void)p0; (void)p1; (void)p2; (void)p3;
    return ExtensionUnavailable();
}

CNA_Result cna_cascaded_shadow_map_destroy(CNA_CascadedShadowMapHandle p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_cube_shadow_map_create(CNA_Handle p0, CNA_ShadowQuality p1, CNA_CubeShadowMapHandle* p2)
{
    (void)p0; (void)p1; (void)p2;
    if (p2 != nullptr) { *p2 = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_cube_shadow_map_is_supported(CNA_CubeShadowMapHandle p0, CNA_Bool* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_cube_shadow_map_update(CNA_CubeShadowMapHandle p0, const CNA_PointLightEXT* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_cube_shadow_map_begin(CNA_CubeShadowMapHandle p0, int32_t p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_cube_shadow_map_end(CNA_CubeShadowMapHandle p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_cube_shadow_map_get_shadow_texture(CNA_CubeShadowMapHandle p0, CNA_Handle* p1)
{
    (void)p0; (void)p1;
    if (p1 != nullptr) { *p1 = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_cube_shadow_map_get_caster_effect(CNA_CubeShadowMapHandle p0, CNA_EffectHandle* p1)
{
    (void)p0; (void)p1;
    if (p1 != nullptr) { *p1 = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_cube_shadow_map_get_size(CNA_CubeShadowMapHandle p0, int32_t* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_cube_shadow_map_get_quality(CNA_CubeShadowMapHandle p0, CNA_ShadowQuality* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_cube_shadow_map_get_light_position(CNA_CubeShadowMapHandle p0, CNA_Vector3* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_cube_shadow_map_get_light_range(CNA_CubeShadowMapHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_cube_shadow_map_get_depth_bias(CNA_CubeShadowMapHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_cube_shadow_map_set_depth_bias(CNA_CubeShadowMapHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_cube_shadow_map_compute_face_view(CNA_CubeMapFace p0, const CNA_Vector3* p1, CNA_Matrix* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_cube_shadow_map_compute_face_projection(float p0, CNA_Matrix* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_cube_shadow_map_size_for_quality(CNA_ShadowQuality p0, int32_t* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_cube_shadow_map_destroy(CNA_CubeShadowMapHandle p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_effect_set_shadow_map_ext(CNA_EffectHandle p0, CNA_Handle p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_effect_get_shadow_map_ext(CNA_EffectHandle p0, CNA_Handle* p1)
{
    (void)p0; (void)p1;
    if (p1 != nullptr) { *p1 = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_effect_set_light_view_projection_ext(CNA_EffectHandle p0, const CNA_Matrix* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_effect_get_light_view_projection_ext(CNA_EffectHandle p0, CNA_Matrix* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_effect_set_shadows_enabled_ext(CNA_EffectHandle p0, CNA_Bool p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_effect_is_shadows_enabled_ext(CNA_EffectHandle p0, CNA_Bool* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_effect_set_shadow_depth_bias_ext(CNA_EffectHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_effect_get_shadow_depth_bias_ext(CNA_EffectHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_effect_set_shadow_filter_radius_ext(CNA_EffectHandle p0, int32_t p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_effect_get_shadow_filter_radius_ext(CNA_EffectHandle p0, int32_t* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_effect_set_shadow_cascades_ext(CNA_EffectHandle p0, const CNA_ShadowCascadeStateEXT* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_effect_get_shadow_cascades_ext(CNA_EffectHandle p0, CNA_ShadowCascadeStateEXT* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_effect_set_punctual_light_ext(CNA_EffectHandle p0, const CNA_PunctualLightEXT* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_effect_get_punctual_light_ext(CNA_EffectHandle p0, CNA_PunctualLightEXT* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_cascaded_shadow_map_apply_to_receiver(CNA_CascadedShadowMapHandle p0, CNA_EffectHandle p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_shadow_policy_create(CNA_Handle p0, int32_t p1, CNA_ClusteredShadowPolicyHandle* p2)
{
    (void)p0; (void)p1; (void)p2;
    if (p2 != nullptr) { *p2 = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_shadow_policy_get_budget(CNA_ClusteredShadowPolicyHandle p0, int32_t* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_shadow_policy_set_budget(CNA_ClusteredShadowPolicyHandle p0, int32_t p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_shadow_policy_get_hysteresis(CNA_ClusteredShadowPolicyHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_shadow_policy_set_hysteresis(CNA_ClusteredShadowPolicyHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_shadow_policy_copy_selected(CNA_ClusteredShadowPolicyHandle p0, int32_t* p1, uint64_t p2, uint64_t* p3)
{
    (void)p0; (void)p1; (void)p2; (void)p3;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_shadow_policy_is_selected(CNA_ClusteredShadowPolicyHandle p0, int32_t p1, CNA_Bool* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_shadow_policy_get_score(CNA_ClusteredShadowPolicyHandle p0, int32_t p1, float* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_shadow_policy_get_request_count(CNA_ClusteredShadowPolicyHandle p0, int32_t* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_shadow_policy_get_refused_count(CNA_ClusteredShadowPolicyHandle p0, int32_t* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_shadow_policy_reset(CNA_ClusteredShadowPolicyHandle p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_shadow_policy_destroy(CNA_ClusteredShadowPolicyHandle p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_normal_prepass_create(CNA_Handle p0, int32_t p1, int32_t p2, CNA_DepthEncoding p3, CNA_DepthNormalPrepassHandle* p4)
{
    (void)p0; (void)p1; (void)p2; (void)p3; (void)p4;
    if (p4 != nullptr) { *p4 = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_depth_normal_prepass_resize(CNA_DepthNormalPrepassHandle p0, int32_t p1, int32_t p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_normal_prepass_get_pass_count(CNA_DepthNormalPrepassHandle p0, int32_t* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_normal_prepass_begin(CNA_DepthNormalPrepassHandle p0, int32_t p1, const CNA_Matrix* p2, const CNA_Matrix* p3, float p4, float p5)
{
    (void)p0; (void)p1; (void)p2; (void)p3; (void)p4; (void)p5;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_normal_prepass_end(CNA_DepthNormalPrepassHandle p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_normal_prepass_get_prepass_effect(CNA_DepthNormalPrepassHandle p0, CNA_EffectHandle* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_normal_prepass_get_skinned_prepass_effect(CNA_DepthNormalPrepassHandle p0, CNA_EffectHandle* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_normal_prepass_get_depth_texture(CNA_DepthNormalPrepassHandle p0, CNA_Handle* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_normal_prepass_get_normal_texture(CNA_DepthNormalPrepassHandle p0, CNA_Handle* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_normal_prepass_get_velocity_texture_ext(CNA_DepthNormalPrepassHandle p0, CNA_Handle* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_normal_prepass_is_supported(CNA_DepthNormalPrepassHandle p0, CNA_Handle p1, CNA_Bool* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_normal_prepass_is_using_multiple_render_targets(CNA_DepthNormalPrepassHandle p0, CNA_Bool* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_normal_prepass_is_depth_packed(CNA_DepthNormalPrepassHandle p0, CNA_Bool* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_normal_prepass_uses_packed_depth_ext(CNA_Handle p0, CNA_Bool* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_normal_prepass_get_roughness(CNA_DepthNormalPrepassHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_normal_prepass_set_roughness(CNA_DepthNormalPrepassHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_normal_prepass_is_velocity_enabled_ext(CNA_DepthNormalPrepassHandle p0, CNA_Bool* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_normal_prepass_set_velocity_enabled_ext(CNA_DepthNormalPrepassHandle p0, CNA_Bool p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_normal_prepass_set_previous_world_ext(CNA_DepthNormalPrepassHandle p0, const CNA_Matrix* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_normal_prepass_set_previous_camera_ext(CNA_DepthNormalPrepassHandle p0, const CNA_Matrix* p1, const CNA_Matrix* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_normal_prepass_copy_depth_decode_glsl(CNA_Bool p0, char* p1, uint64_t p2, uint64_t* p3)
{
    (void)p0; (void)p1; (void)p2; (void)p3;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_normal_prepass_copy_velocity_decode_glsl(char* p0, uint64_t p1, uint64_t* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_normal_prepass_has_velocity_ext(CNA_Color p0, CNA_Bool* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_normal_prepass_decode_velocity_ext(CNA_Color p0, CNA_Vector2* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_normal_prepass_pack_depth(float p0, float* p1, float* p2, float* p3, float* p4)
{
    (void)p0; (void)p1; (void)p2; (void)p3; (void)p4;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_normal_prepass_unpack_depth(float p0, float p1, float p2, float p3, float* p4)
{
    (void)p0; (void)p1; (void)p2; (void)p3; (void)p4;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_normal_prepass_destroy(CNA_DepthNormalPrepassHandle p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_contact_shadow_pass_create(CNA_Handle p0, CNA_PostProcessPassHandle* p1)
{
    (void)p0; (void)p1;
    if (p1 != nullptr) { *p1 = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_contact_shadow_pass_get_light_direction(CNA_PostProcessPassHandle p0, CNA_Vector3* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_contact_shadow_pass_set_light_direction(CNA_PostProcessPassHandle p0, const CNA_Vector3* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_contact_shadow_pass_get_max_distance(CNA_PostProcessPassHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_contact_shadow_pass_set_max_distance(CNA_PostProcessPassHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_contact_shadow_pass_get_step_count(CNA_PostProcessPassHandle p0, int32_t* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_contact_shadow_pass_set_step_count(CNA_PostProcessPassHandle p0, int32_t p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_contact_shadow_pass_get_thickness(CNA_PostProcessPassHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_contact_shadow_pass_set_thickness(CNA_PostProcessPassHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_contact_shadow_pass_get_intensity(CNA_PostProcessPassHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_contact_shadow_pass_set_intensity(CNA_PostProcessPassHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_contact_shadow_pass_get_bias(CNA_PostProcessPassHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_contact_shadow_pass_set_bias(CNA_PostProcessPassHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_contact_shadow_pass_copy_fallback_reason(CNA_PostProcessPassHandle p0, char* p1, uint64_t p2, uint64_t* p3)
{
    (void)p0; (void)p1; (void)p2; (void)p3;
    return ExtensionUnavailable();
}

CNA_Result cna_contact_shadow_pass_is_occluded(float p0, float p1, float p2, float p3, CNA_Bool* p4)
{
    (void)p0; (void)p1; (void)p2; (void)p3; (void)p4;
    return ExtensionUnavailable();
}

CNA_Result cna_contact_shadow_pass_copy_occlusion_test_glsl(char* p0, uint64_t p1, uint64_t* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_contact_shadow_pass_combine_visibility(float p0, float p1, float* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_set_is_usable(const CNA_ClusteredLightEXT* p0, CNA_Bool* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_set_create(CNA_Handle p0, CNA_ClusteredLightSetHandle* p1)
{
    (void)p0; (void)p1;
    if (p1 != nullptr) { *p1 = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_set_add(CNA_ClusteredLightSetHandle p0, const CNA_ClusteredLightEXT* p1, int32_t* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_set_add_point(CNA_ClusteredLightSetHandle p0, const CNA_PointLightEXT* p1, int32_t* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_set_add_spot(CNA_ClusteredLightSetHandle p0, const CNA_SpotLightEXT* p1, int32_t* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_set_replace_at(CNA_ClusteredLightSetHandle p0, int32_t p1, const CNA_ClusteredLightEXT* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_set_remove_at(CNA_ClusteredLightSetHandle p0, int32_t p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_set_clear(CNA_ClusteredLightSetHandle p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_set_get_count(CNA_ClusteredLightSetHandle p0, int32_t* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_set_is_empty(CNA_ClusteredLightSetHandle p0, CNA_Bool* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_set_get_at(CNA_ClusteredLightSetHandle p0, int32_t p1, CNA_ClusteredLightEXT* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_set_copy_lights(CNA_ClusteredLightSetHandle p0, CNA_ClusteredLightEXT* p1, uint64_t p2, uint64_t* p3)
{
    (void)p0; (void)p1; (void)p2; (void)p3;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_set_get_bounds_at(CNA_ClusteredLightSetHandle p0, int32_t p1, CNA_BoundingSphere* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_set_copy_bounds(CNA_ClusteredLightSetHandle p0, CNA_BoundingSphere* p1, uint64_t p2, uint64_t* p3)
{
    (void)p0; (void)p1; (void)p2; (void)p3;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_set_destroy(CNA_ClusteredLightSetHandle p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_grid_create(CNA_Handle p0, int32_t p1, int32_t p2, int32_t p3, CNA_ClusteredLightGridHandle* p4)
{
    (void)p0; (void)p1; (void)p2; (void)p3; (void)p4;
    if (p4 != nullptr) { *p4 = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_grid_get_tiles_x(CNA_ClusteredLightGridHandle p0, int32_t* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_grid_get_tiles_y(CNA_ClusteredLightGridHandle p0, int32_t* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_grid_get_slice_count(CNA_ClusteredLightGridHandle p0, int32_t* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_grid_get_cluster_count(CNA_ClusteredLightGridHandle p0, int32_t* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_grid_cluster_index(CNA_ClusteredLightGridHandle p0, int32_t p1, int32_t p2, int32_t p3, int32_t* p4)
{
    (void)p0; (void)p1; (void)p2; (void)p3; (void)p4;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_grid_set_projection(CNA_ClusteredLightGridHandle p0, const CNA_Matrix* p1, float p2, float p3)
{
    (void)p0; (void)p1; (void)p2; (void)p3;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_grid_has_projection(CNA_ClusteredLightGridHandle p0, CNA_Bool* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_grid_get_near_plane(CNA_ClusteredLightGridHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_grid_get_far_plane(CNA_ClusteredLightGridHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_grid_get_inverse_projection(CNA_ClusteredLightGridHandle p0, CNA_Matrix* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_grid_slice_distance(CNA_ClusteredLightGridHandle p0, int32_t p1, float* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_grid_slice_for_view_distance(CNA_ClusteredLightGridHandle p0, float p1, int32_t* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_grid_cluster_bounds(CNA_ClusteredLightGridHandle p0, int32_t p1, int32_t p2, int32_t p3, CNA_BoundingBox* p4)
{
    (void)p0; (void)p1; (void)p2; (void)p3; (void)p4;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_grid_destroy(CNA_ClusteredLightGridHandle p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_assignment_create(CNA_Handle p0, CNA_ClusteredLightAssignmentHandle* p1)
{
    (void)p0; (void)p1;
    if (p1 != nullptr) { *p1 = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_assignment_assign(CNA_ClusteredLightAssignmentHandle p0, CNA_ClusteredLightGridHandle p1, const CNA_Matrix* p2, const CNA_BoundingSphere* p3, uint64_t p4)
{
    (void)p0; (void)p1; (void)p2; (void)p3; (void)p4;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_assignment_clear(CNA_ClusteredLightAssignmentHandle p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_assignment_adopt(CNA_ClusteredLightAssignmentHandle p0, int32_t p1, const int32_t* p2, uint64_t p3, const int32_t* p4, uint64_t p5)
{
    (void)p0; (void)p1; (void)p2; (void)p3; (void)p4; (void)p5;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_assignment_get_light_count(CNA_ClusteredLightAssignmentHandle p0, int32_t* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_assignment_get_cluster_count(CNA_ClusteredLightAssignmentHandle p0, int32_t* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_assignment_copy_lights_in_cluster(CNA_ClusteredLightAssignmentHandle p0, int32_t p1, int32_t* p2, uint64_t p3, uint64_t* p4)
{
    (void)p0; (void)p1; (void)p2; (void)p3; (void)p4;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_assignment_copy_indices(CNA_ClusteredLightAssignmentHandle p0, int32_t* p1, uint64_t p2, uint64_t* p3)
{
    (void)p0; (void)p1; (void)p2; (void)p3;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_assignment_copy_offsets(CNA_ClusteredLightAssignmentHandle p0, int32_t* p1, uint64_t p2, uint64_t* p3)
{
    (void)p0; (void)p1; (void)p2; (void)p3;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_assignment_get_total_reference_count(CNA_ClusteredLightAssignmentHandle p0, int32_t* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_assignment_get_max_lights_per_cluster(CNA_ClusteredLightAssignmentHandle p0, int32_t* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_assignment_destroy(CNA_ClusteredLightAssignmentHandle p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_buffer_create(CNA_Handle p0, CNA_ClusteredLightBufferHandle* p1)
{
    (void)p0; (void)p1;
    if (p1 != nullptr) { *p1 = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_buffer_upload(CNA_ClusteredLightBufferHandle p0, CNA_ClusteredLightSetHandle p1, CNA_ClusteredLightGridHandle p2, CNA_ClusteredLightAssignmentHandle p3)
{
    (void)p0; (void)p1; (void)p2; (void)p3;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_buffer_bind(CNA_ClusteredLightBufferHandle p0, CNA_EffectHandle p1, int32_t p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_buffer_is_uploaded(CNA_ClusteredLightBufferHandle p0, CNA_Bool* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_buffer_get_light_count(CNA_ClusteredLightBufferHandle p0, int32_t* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_buffer_get_cluster_count(CNA_ClusteredLightBufferHandle p0, int32_t* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_buffer_get_reference_count(CNA_ClusteredLightBufferHandle p0, int32_t* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_buffer_copy_light_lookup_glsl(char* p0, uint64_t p1, uint64_t* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_buffer_destroy(CNA_ClusteredLightBufferHandle p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_forward_effect_create(CNA_Handle p0, CNA_ClusteredForwardEffectHandle* p1)
{
    (void)p0; (void)p1;
    if (p1 != nullptr) { *p1 = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_forward_effect_is_supported(CNA_ClusteredForwardEffectHandle p0, CNA_Bool* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_forward_effect_begin(CNA_ClusteredForwardEffectHandle p0, const CNA_Matrix* p1, const CNA_Matrix* p2, const CNA_Matrix* p3, const CNA_Vector3* p4, CNA_ClusteredLightBufferHandle p5)
{
    (void)p0; (void)p1; (void)p2; (void)p3; (void)p4; (void)p5;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_forward_effect_get_effect(CNA_ClusteredForwardEffectHandle p0, CNA_EffectHandle* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_forward_effect_has_area_light(CNA_ClusteredForwardEffectHandle p0, CNA_Bool* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_forward_effect_clear_area_light(CNA_ClusteredForwardEffectHandle p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_forward_effect_has_light_probe(CNA_ClusteredForwardEffectHandle p0, CNA_Bool* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_forward_effect_clear_light_probe(CNA_ClusteredForwardEffectHandle p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_forward_effect_get_base_color(CNA_ClusteredForwardEffectHandle p0, CNA_Vector3* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_forward_effect_set_base_color(CNA_ClusteredForwardEffectHandle p0, const CNA_Vector3* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_forward_effect_get_metallic(CNA_ClusteredForwardEffectHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_forward_effect_set_metallic(CNA_ClusteredForwardEffectHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_forward_effect_get_roughness(CNA_ClusteredForwardEffectHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_forward_effect_set_roughness(CNA_ClusteredForwardEffectHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_forward_effect_get_ior(CNA_ClusteredForwardEffectHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_forward_effect_set_ior(CNA_ClusteredForwardEffectHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_forward_effect_get_ambient(CNA_ClusteredForwardEffectHandle p0, CNA_Vector3* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_forward_effect_set_ambient(CNA_ClusteredForwardEffectHandle p0, const CNA_Vector3* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_forward_effect_get_opaque_frame(CNA_ClusteredForwardEffectHandle p0, CNA_Handle* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_forward_effect_set_opaque_frame(CNA_ClusteredForwardEffectHandle p0, CNA_Handle p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_forward_effect_volume_attenuation(const CNA_Vector3* p0, float p1, float p2, CNA_Vector3* p3)
{
    (void)p0; (void)p1; (void)p2; (void)p3;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_forward_effect_contribution(const CNA_ClusteredLightEXT* p0, const CNA_Vector3* p1, const CNA_Vector3* p2, const CNA_Vector3* p3, const CNA_Vector3* p4, float p5, float p6, float p7, float p8, const CNA_Vector3* p9, float p10, float p11, float p12, float p13, const CNA_Vector3* p14, float p15, CNA_Vector3* p16)
{
    (void)p0; (void)p1; (void)p2; (void)p3; (void)p4; (void)p5; (void)p6; (void)p7; (void)p8; (void)p9; (void)p10; (void)p11; (void)p12; (void)p13; (void)p14; (void)p15; (void)p16;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_forward_effect_destroy(CNA_ClusteredForwardEffectHandle p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_compute_create(CNA_Handle p0, int32_t p1, CNA_ClusteredLightComputeHandle* p2)
{
    (void)p0; (void)p1; (void)p2;
    if (p2 != nullptr) { *p2 = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_compute_is_supported(CNA_ClusteredLightComputeHandle p0, CNA_Bool* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_compute_copy_unsupported_reason(CNA_ClusteredLightComputeHandle p0, char* p1, uint64_t p2, uint64_t* p3)
{
    (void)p0; (void)p1; (void)p2; (void)p3;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_compute_get_stride(CNA_ClusteredLightComputeHandle p0, int32_t* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_compute_assign(CNA_ClusteredLightComputeHandle p0, CNA_ClusteredLightGridHandle p1, const CNA_Matrix* p2, const CNA_BoundingSphere* p3, uint64_t p4, CNA_ClusteredLightAssignmentHandle p5)
{
    (void)p0; (void)p1; (void)p2; (void)p3; (void)p4; (void)p5;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_compute_used_compute(CNA_ClusteredLightComputeHandle p0, CNA_Bool* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_compute_has_overflowed(CNA_ClusteredLightComputeHandle p0, CNA_Bool* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_light_compute_destroy(CNA_ClusteredLightComputeHandle p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_shadow_policy_select(CNA_ClusteredShadowPolicyHandle p0, CNA_ClusteredLightSetHandle p1, const CNA_Matrix* p2, const CNA_Matrix* p3, const CNA_Vector3* p4)
{
    (void)p0; (void)p1; (void)p2; (void)p3; (void)p4;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_create(CNA_PbrMaterialExtensionsHandle* p0)
{
    (void)p0;
    if (p0 != nullptr) { *p0 = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_destroy(CNA_PbrMaterialExtensionsHandle p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_copy_from(CNA_PbrMaterialExtensionsHandle p0, CNA_PbrMaterialExtensionsHandle p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_get_clearcoat_factor(CNA_PbrMaterialExtensionsHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_set_clearcoat_factor(CNA_PbrMaterialExtensionsHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_get_clearcoat_roughness(CNA_PbrMaterialExtensionsHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_set_clearcoat_roughness(CNA_PbrMaterialExtensionsHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_get_clearcoat_normal_scale(CNA_PbrMaterialExtensionsHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_set_clearcoat_normal_scale(CNA_PbrMaterialExtensionsHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_get_sheen_roughness(CNA_PbrMaterialExtensionsHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_set_sheen_roughness(CNA_PbrMaterialExtensionsHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_get_transmission_factor(CNA_PbrMaterialExtensionsHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_set_transmission_factor(CNA_PbrMaterialExtensionsHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_get_thickness_factor(CNA_PbrMaterialExtensionsHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_set_thickness_factor(CNA_PbrMaterialExtensionsHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_get_attenuation_distance(CNA_PbrMaterialExtensionsHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_set_attenuation_distance(CNA_PbrMaterialExtensionsHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_get_iridescence_factor(CNA_PbrMaterialExtensionsHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_set_iridescence_factor(CNA_PbrMaterialExtensionsHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_get_iridescence_ior(CNA_PbrMaterialExtensionsHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_set_iridescence_ior(CNA_PbrMaterialExtensionsHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_get_iridescence_thickness_minimum(CNA_PbrMaterialExtensionsHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_set_iridescence_thickness_minimum(CNA_PbrMaterialExtensionsHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_get_iridescence_thickness_maximum(CNA_PbrMaterialExtensionsHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_set_iridescence_thickness_maximum(CNA_PbrMaterialExtensionsHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_get_subsurface_wrap(CNA_PbrMaterialExtensionsHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_set_subsurface_wrap(CNA_PbrMaterialExtensionsHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_get_sheen_color_factor(CNA_PbrMaterialExtensionsHandle p0, CNA_Vector3* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_set_sheen_color_factor(CNA_PbrMaterialExtensionsHandle p0, const CNA_Vector3* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_get_attenuation_color(CNA_PbrMaterialExtensionsHandle p0, CNA_Vector3* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_set_attenuation_color(CNA_PbrMaterialExtensionsHandle p0, const CNA_Vector3* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_get_subsurface_color(CNA_PbrMaterialExtensionsHandle p0, CNA_Vector3* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_set_subsurface_color(CNA_PbrMaterialExtensionsHandle p0, const CNA_Vector3* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_get_clearcoat_texture(CNA_PbrMaterialExtensionsHandle p0, CNA_Handle* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_set_clearcoat_texture(CNA_PbrMaterialExtensionsHandle p0, CNA_Handle p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_get_clearcoat_roughness_texture(CNA_PbrMaterialExtensionsHandle p0, CNA_Handle* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_set_clearcoat_roughness_texture(CNA_PbrMaterialExtensionsHandle p0, CNA_Handle p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_get_clearcoat_normal_texture(CNA_PbrMaterialExtensionsHandle p0, CNA_Handle* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_set_clearcoat_normal_texture(CNA_PbrMaterialExtensionsHandle p0, CNA_Handle p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_get_sheen_color_texture(CNA_PbrMaterialExtensionsHandle p0, CNA_Handle* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_set_sheen_color_texture(CNA_PbrMaterialExtensionsHandle p0, CNA_Handle p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_get_sheen_roughness_texture(CNA_PbrMaterialExtensionsHandle p0, CNA_Handle* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_set_sheen_roughness_texture(CNA_PbrMaterialExtensionsHandle p0, CNA_Handle p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_get_transmission_texture(CNA_PbrMaterialExtensionsHandle p0, CNA_Handle* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_set_transmission_texture(CNA_PbrMaterialExtensionsHandle p0, CNA_Handle p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_get_thickness_texture(CNA_PbrMaterialExtensionsHandle p0, CNA_Handle* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_set_thickness_texture(CNA_PbrMaterialExtensionsHandle p0, CNA_Handle p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_get_iridescence_texture(CNA_PbrMaterialExtensionsHandle p0, CNA_Handle* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_set_iridescence_texture(CNA_PbrMaterialExtensionsHandle p0, CNA_Handle p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_get_iridescence_thickness_texture(CNA_PbrMaterialExtensionsHandle p0, CNA_Handle* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_set_iridescence_thickness_texture(CNA_PbrMaterialExtensionsHandle p0, CNA_Handle p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_is_subsurface_enabled(CNA_PbrMaterialExtensionsHandle p0, CNA_Bool* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_is_iridescence_enabled(CNA_PbrMaterialExtensionsHandle p0, CNA_Bool* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_is_transmission_enabled(CNA_PbrMaterialExtensionsHandle p0, CNA_Bool* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_is_sheen_enabled(CNA_PbrMaterialExtensionsHandle p0, CNA_Bool* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_is_neutral(CNA_PbrMaterialExtensionsHandle p0, CNA_Bool* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_equals(CNA_PbrMaterialExtensionsHandle p0, CNA_PbrMaterialExtensionsHandle p1, CNA_Bool* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_get_hash_code(CNA_PbrMaterialExtensionsHandle p0, uint64_t* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_extensions_copy_to_string(CNA_PbrMaterialExtensionsHandle p0, char* p1, uint64_t p2, uint64_t* p3)
{
    (void)p0; (void)p1; (void)p2; (void)p3;
    return ExtensionUnavailable();
}

CNA_Result cna_thin_film_iridescence_evaluate(float p0, float p1, float p2, float p3, const CNA_Vector3* p4, CNA_Vector3* p5)
{
    (void)p0; (void)p1; (void)p2; (void)p3; (void)p4; (void)p5;
    return ExtensionUnavailable();
}

CNA_Result cna_thin_film_iridescence_copy_glsl(char* p0, uint64_t p1, uint64_t* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_forward_effect_get_material_extensions(CNA_ClusteredForwardEffectHandle p0, CNA_PbrMaterialExtensionsHandle* p1)
{
    (void)p0; (void)p1;
    if (p1 != nullptr) { *p1 = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_forward_effect_set_material_extensions(CNA_ClusteredForwardEffectHandle p0, CNA_PbrMaterialExtensionsHandle p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_clustered_forward_effect_contribution_with_extensions(const CNA_ClusteredLightEXT* p0, const CNA_Vector3* p1, const CNA_Vector3* p2, const CNA_Vector3* p3, const CNA_Vector3* p4, float p5, float p6, CNA_PbrMaterialExtensionsHandle p7, CNA_Vector3* p8)
{
    (void)p0; (void)p1; (void)p2; (void)p3; (void)p4; (void)p5; (void)p6; (void)p7; (void)p8;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_ext_equals(const CNA_PbrMaterialEXT* p0, const CNA_PbrMaterialEXT* p1, CNA_Bool* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_ext_get_hash_code(const CNA_PbrMaterialEXT* p0, uint64_t* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_pbr_material_ext_copy_to_string(const CNA_PbrMaterialEXT* p0, char* p1, uint64_t p2, uint64_t* p3)
{
    (void)p0; (void)p1; (void)p2; (void)p3;
    return ExtensionUnavailable();
}

CNA_Result cna_gltf_material_source_ext_init(CNA_GltfMaterialSourceEXT* p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_gltf_material_extension_source_ext_init(CNA_GltfMaterialExtensionSourceEXT* p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_gltf_material_textures_ext_init(CNA_GltfMaterialTexturesEXT* p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_gltf_material_extension_textures_ext_init(CNA_GltfMaterialExtensionTexturesEXT* p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_gltf_material_bridge_build_material(const CNA_GltfMaterialSourceEXT* p0, const CNA_GltfMaterialTexturesEXT* p1, CNA_PbrMaterialEXT* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_gltf_material_bridge_build_extensions(const CNA_GltfMaterialExtensionSourceEXT* p0, const CNA_GltfMaterialExtensionTexturesEXT* p1, CNA_PbrMaterialExtensionsHandle p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_transparent_draw_list_create(CNA_TransparentDrawListHandle* p0)
{
    (void)p0;
    if (p0 != nullptr) { *p0 = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_transparent_draw_list_destroy(CNA_TransparentDrawListHandle p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_transparent_draw_list_clear(CNA_TransparentDrawListHandle p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_transparent_draw_list_submit(CNA_TransparentDrawListHandle p0, const CNA_BoundingBox* p1, CNA_TransparentDrawCallback p2, void* p3)
{
    (void)p0; (void)p1; (void)p2; (void)p3;
    return ExtensionUnavailable();
}

CNA_Result cna_transparent_draw_list_get_count(CNA_TransparentDrawListHandle p0, uint64_t* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_transparent_draw_list_draw_sorted(CNA_TransparentDrawListHandle p0, const CNA_Matrix* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_transparent_draw_list_copy_sorted_order_ext(CNA_TransparentDrawListHandle p0, const CNA_Matrix* p1, int32_t* p2, uint64_t p3, uint64_t* p4)
{
    (void)p0; (void)p1; (void)p2; (void)p3; (void)p4;
    return ExtensionUnavailable();
}

CNA_Result cna_transparent_draw_list_sort_key(const CNA_BoundingBox* p0, const CNA_Vector3* p1, float* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_transparent_draw_list_camera_position_of(const CNA_Matrix* p0, CNA_Vector3* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_weighted_blended_transparency_create(CNA_Handle p0, int32_t p1, int32_t p2, CNA_WeightedBlendedTransparencyHandle* p3)
{
    (void)p0; (void)p1; (void)p2; (void)p3;
    if (p3 != nullptr) { *p3 = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_weighted_blended_transparency_destroy(CNA_WeightedBlendedTransparencyHandle p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_weighted_blended_transparency_is_supported(CNA_WeightedBlendedTransparencyHandle p0, CNA_Bool* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_weighted_blended_transparency_copy_unsupported_reason(CNA_WeightedBlendedTransparencyHandle p0, char* p1, uint64_t p2, uint64_t* p3)
{
    (void)p0; (void)p1; (void)p2; (void)p3;
    return ExtensionUnavailable();
}

CNA_Result cna_weighted_blended_transparency_resize(CNA_WeightedBlendedTransparencyHandle p0, int32_t p1, int32_t p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_weighted_blended_transparency_begin(CNA_WeightedBlendedTransparencyHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_weighted_blended_transparency_end(CNA_WeightedBlendedTransparencyHandle p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_weighted_blended_transparency_resolve(CNA_WeightedBlendedTransparencyHandle p0, int32_t p1, int32_t p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_weighted_blended_transparency_is_accumulating(CNA_WeightedBlendedTransparencyHandle p0, CNA_Bool* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_weighted_blended_transparency_get_accumulation_texture_ext(CNA_WeightedBlendedTransparencyHandle p0, CNA_Handle* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_weighted_blended_transparency_get_revealage_texture_ext(CNA_WeightedBlendedTransparencyHandle p0, CNA_Handle* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_weighted_blended_transparency_copy_accumulation_glsl(char* p0, uint64_t p1, uint64_t* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_weighted_blended_transparency_weight(float p0, float p1, float p2, float* p3)
{
    (void)p0; (void)p1; (void)p2; (void)p3;
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_settings_ext_init(CNA_RenderPipelineSettingsEXT* p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_settings_ext_normalize(CNA_RenderPipelineSettingsEXT* p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_settings_ext_apply_render_quality_preset(CNA_RenderPipelineSettingsEXT* p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_settings_ext_apply_from_string(CNA_RenderPipelineSettingsEXT* p0, CNA_StringView p1, int32_t* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_create(CNA_Handle p0, CNA_RenderPipelineHandle* p1)
{
    (void)p0; (void)p1;
    if (p1 != nullptr) { *p1 = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_destroy(CNA_RenderPipelineHandle p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_get_settings(CNA_RenderPipelineHandle p0, CNA_RenderPipelineSettingsEXT* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_set_settings(CNA_RenderPipelineHandle p0, const CNA_RenderPipelineSettingsEXT* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_resize(CNA_RenderPipelineHandle p0, int32_t p1, int32_t p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_begin(CNA_RenderPipelineHandle p0, const CNA_Color* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_end(CNA_RenderPipelineHandle p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_add_user_pass(CNA_RenderPipelineHandle p0, CNA_PostProcessPassHandle p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_clear_user_passes(CNA_RenderPipelineHandle p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_set_depth_normal_inputs(CNA_RenderPipelineHandle p0, CNA_Handle p1, CNA_Handle p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_set_velocity_input_ext(CNA_RenderPipelineHandle p0, CNA_Handle p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_set_transparent_scene(CNA_RenderPipelineHandle p0, CNA_RenderPipelineDrawCallback p1, void* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_set_shadow_scene(CNA_RenderPipelineHandle p0, CNA_ShadowMapHandle p1, const CNA_DirectionalLightEXT* p2, const CNA_BoundingBox* p3, CNA_RenderPipelineDrawCallback p4, void* p5)
{
    (void)p0; (void)p1; (void)p2; (void)p3; (void)p4; (void)p5;
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_set_camera(CNA_RenderPipelineHandle p0, const CNA_Matrix* p1, const CNA_Matrix* p2, float p3, float p4)
{
    (void)p0; (void)p1; (void)p2; (void)p3; (void)p4;
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_set_skybox_camera(CNA_RenderPipelineHandle p0, const CNA_Matrix* p1, const CNA_Matrix* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_copy_transparency_fallback_reason_ext(CNA_RenderPipelineHandle p0, char* p1, uint64_t p2, uint64_t* p3)
{
    (void)p0; (void)p1; (void)p2; (void)p3;
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_set_gpu_timing_enabled_ext(CNA_RenderPipelineHandle p0, CNA_Bool p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_is_gpu_timing_enabled_ext(CNA_RenderPipelineHandle p0, CNA_Bool* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_did_skybox_draw(CNA_RenderPipelineHandle p0, CNA_Bool* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_did_shadow_pass_run(CNA_RenderPipelineHandle p0, CNA_Bool* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_get_shadow_map(CNA_RenderPipelineHandle p0, CNA_ShadowMapHandle* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_get_scene_target(CNA_RenderPipelineHandle p0, CNA_Handle* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_get_scene_target_format(CNA_RenderPipelineHandle p0, CNA_SurfaceFormat* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_is_using_scene_target(CNA_RenderPipelineHandle p0, CNA_Bool* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_get_last_frame_pass_count(CNA_RenderPipelineHandle p0, int32_t* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_get_gpu_memory_estimate_bytes(CNA_RenderPipelineHandle p0, uint64_t* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_get_statistics(CNA_RenderPipelineHandle p0, CNA_RenderPipelineFrameStatisticsEXT* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_release_device_resources_ext(CNA_RenderPipelineHandle p0)
{
    (void)p0;
    return ExtensionUnavailable();
}



CNA_Result cna_post_process_chain_create(CNA_Handle p0, CNA_PostProcessChainHandle* p1)
{
    (void)p0; (void)p1;
    if (p1 != nullptr) { *p1 = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_post_process_chain_destroy(CNA_PostProcessChainHandle p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_post_process_chain_add_pass(CNA_PostProcessChainHandle p0, CNA_PostProcessPassHandle p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_post_process_chain_add_owned_pass(CNA_PostProcessChainHandle p0, CNA_PostProcessPassHandle p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_post_process_chain_clear(CNA_PostProcessChainHandle p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_post_process_chain_get_pass_count(CNA_PostProcessChainHandle p0, int32_t* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_post_process_chain_apply(CNA_PostProcessChainHandle p0, const CNA_PostProcessContext* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_post_process_chain_reset_targets(CNA_PostProcessChainHandle p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_post_process_chain_get_target_pool(CNA_PostProcessChainHandle p0, CNA_RenderTargetPoolHandle* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_post_process_chain_is_gpu_timing_enabled(CNA_PostProcessChainHandle p0, CNA_Bool* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_post_process_chain_set_gpu_timing_enabled(CNA_PostProcessChainHandle p0, CNA_Bool p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_post_process_chain_get_pass_timing_count(CNA_PostProcessChainHandle p0, uint64_t* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_post_process_chain_get_pass_timing(CNA_PostProcessChainHandle p0, uint64_t p1, CNA_PassTimingEXT* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_post_process_chain_copy_pass_timing_name(CNA_PostProcessChainHandle p0, uint64_t p1, char* p2, uint64_t p3, uint64_t* p4)
{
    (void)p0; (void)p1; (void)p2; (void)p3; (void)p4;
    if (p4 != nullptr) { *p4 = UINT64_C(0); }
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_get_pass_timing_count_ext(CNA_RenderPipelineHandle p0, uint64_t* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_get_pass_timing_ext(CNA_RenderPipelineHandle p0, uint64_t p1, CNA_PassTimingEXT* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_render_pipeline_copy_pass_timing_name_ext(CNA_RenderPipelineHandle p0, uint64_t p1, char* p2, uint64_t p3, uint64_t* p4)
{
    (void)p0; (void)p1; (void)p2; (void)p3; (void)p4;
    if (p4 != nullptr) { *p4 = UINT64_C(0); }
    return ExtensionUnavailable();
}

CNA_Result cna_ssr_pass_create(CNA_Handle p0, CNA_PostProcessPassHandle* p1)
{
    (void)p0; (void)p1;
    if (p1 != nullptr) { *p1 = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_ssr_pass_get_max_distance(CNA_PostProcessPassHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_ssr_pass_set_max_distance(CNA_PostProcessPassHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_ssr_pass_get_step_count(CNA_PostProcessPassHandle p0, int32_t* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_ssr_pass_set_step_count(CNA_PostProcessPassHandle p0, int32_t p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_ssr_pass_get_thickness(CNA_PostProcessPassHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_ssr_pass_set_thickness(CNA_PostProcessPassHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_ssr_pass_get_depth_bias(CNA_PostProcessPassHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_ssr_pass_set_depth_bias(CNA_PostProcessPassHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_ssr_pass_get_roughness_blur(CNA_PostProcessPassHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_ssr_pass_set_roughness_blur(CNA_PostProcessPassHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_ssr_pass_get_edge_fade(CNA_PostProcessPassHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_ssr_pass_set_edge_fade(CNA_PostProcessPassHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_ssr_pass_get_intensity(CNA_PostProcessPassHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_ssr_pass_set_intensity(CNA_PostProcessPassHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_ssao_pass_create(CNA_Handle p0, CNA_PostProcessPassHandle* p1)
{
    (void)p0; (void)p1;
    if (p1 != nullptr) { *p1 = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_ssao_pass_get_radius(CNA_PostProcessPassHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_ssao_pass_set_radius(CNA_PostProcessPassHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_ssao_pass_get_intensity(CNA_PostProcessPassHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_ssao_pass_set_intensity(CNA_PostProcessPassHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_ssao_pass_get_sample_count(CNA_PostProcessPassHandle p0, int32_t* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_ssao_pass_set_sample_count(CNA_PostProcessPassHandle p0, int32_t p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_ssao_pass_get_half_resolution(CNA_PostProcessPassHandle p0, CNA_Bool* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_ssao_pass_set_half_resolution(CNA_PostProcessPassHandle p0, CNA_Bool p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_of_field_pass_create(CNA_Handle p0, CNA_PostProcessPassHandle* p1)
{
    (void)p0; (void)p1;
    if (p1 != nullptr) { *p1 = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_depth_of_field_pass_get_focus_distance(CNA_PostProcessPassHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_of_field_pass_set_focus_distance(CNA_PostProcessPassHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_of_field_pass_get_focal_length(CNA_PostProcessPassHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_of_field_pass_set_focal_length(CNA_PostProcessPassHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_of_field_pass_get_f_number(CNA_PostProcessPassHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_of_field_pass_set_f_number(CNA_PostProcessPassHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_of_field_pass_get_max_radius(CNA_PostProcessPassHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_depth_of_field_pass_set_max_radius(CNA_PostProcessPassHandle p0, float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_ssao_pass_reset_targets(CNA_PostProcessPassHandle p0)
{
    (void)p0;
    return ExtensionUnavailable();
}

CNA_Result cna_ssao_pass_copy_kernel(CNA_PostProcessPassHandle p0, CNA_Vector3* p1, uint64_t p2, uint64_t* p3)
{
    (void)p0; (void)p1; (void)p2; (void)p3;
    if (p3 != nullptr) { *p3 = UINT64_C(0); }
    return ExtensionUnavailable();
}

CNA_Result cna_ssao_pass_copy_occlusion_glsl(CNA_Bool p0, char* p1, uint64_t p2, uint64_t* p3)
{
    (void)p0; (void)p1; (void)p2; (void)p3;
    if (p3 != nullptr) { *p3 = UINT64_C(0); }
    return ExtensionUnavailable();
}

CNA_Result cna_ssao_pass_sample_count_for_quality(CNA_RenderQuality p0, int32_t* p1)
{
    (void)p0; (void)p1;
    if (p1 != nullptr) { *p1 = UINT64_C(0); }
    return ExtensionUnavailable();
}

CNA_Result cna_depth_of_field_pass_circle_of_confusion_millimetres(float p0, float p1, float p2, float p3, float* p4)
{
    (void)p0; (void)p1; (void)p2; (void)p3; (void)p4;
    return ExtensionUnavailable();
}

CNA_Result cna_aerial_perspective_pass_create(CNA_Handle p0, CNA_PostProcessPassHandle* p1)
{
    (void)p0; (void)p1;
    if (p1 != nullptr) { *p1 = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_aerial_perspective_pass_get_sun_direction(CNA_PostProcessPassHandle p0, CNA_Vector3* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_aerial_perspective_pass_set_sun_direction(CNA_PostProcessPassHandle p0, const CNA_Vector3* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_aerial_perspective_pass_get_turbidity(CNA_PostProcessPassHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_aerial_perspective_pass_set_turbidity(CNA_PostProcessPassHandle p0, const float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_aerial_perspective_pass_get_intensity(CNA_PostProcessPassHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_aerial_perspective_pass_set_intensity(CNA_PostProcessPassHandle p0, const float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_aerial_perspective_pass_get_scale_height(CNA_PostProcessPassHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_aerial_perspective_pass_set_scale_height(CNA_PostProcessPassHandle p0, const float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_volumetric_fog_pass_create(CNA_Handle p0, CNA_PostProcessPassHandle* p1)
{
    (void)p0; (void)p1;
    if (p1 != nullptr) { *p1 = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_volumetric_fog_pass_get_density(CNA_PostProcessPassHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_volumetric_fog_pass_set_density(CNA_PostProcessPassHandle p0, const float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_volumetric_fog_pass_get_anisotropy(CNA_PostProcessPassHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_volumetric_fog_pass_set_anisotropy(CNA_PostProcessPassHandle p0, const float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_volumetric_fog_pass_get_range(CNA_PostProcessPassHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_volumetric_fog_pass_set_range(CNA_PostProcessPassHandle p0, const float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_height_fog_pass_create(CNA_Handle p0, CNA_PostProcessPassHandle* p1)
{
    (void)p0; (void)p1;
    if (p1 != nullptr) { *p1 = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_height_fog_pass_get_color(CNA_PostProcessPassHandle p0, CNA_Vector3* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_height_fog_pass_set_color(CNA_PostProcessPassHandle p0, const CNA_Vector3* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_height_fog_pass_get_density(CNA_PostProcessPassHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_height_fog_pass_set_density(CNA_PostProcessPassHandle p0, const float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_height_fog_pass_get_falloff(CNA_PostProcessPassHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_height_fog_pass_set_falloff(CNA_PostProcessPassHandle p0, const float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_height_fog_pass_get_base_height(CNA_PostProcessPassHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_height_fog_pass_set_base_height(CNA_PostProcessPassHandle p0, const float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_light_shaft_pass_create(CNA_Handle p0, CNA_PostProcessPassHandle* p1)
{
    (void)p0; (void)p1;
    if (p1 != nullptr) { *p1 = CNA_INVALID_HANDLE; }
    return ExtensionUnavailable();
}

CNA_Result cna_light_shaft_pass_get_light_screen_position(CNA_PostProcessPassHandle p0, CNA_Vector2* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_light_shaft_pass_set_light_screen_position(CNA_PostProcessPassHandle p0, const CNA_Vector2* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_light_shaft_pass_get_threshold(CNA_PostProcessPassHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_light_shaft_pass_set_threshold(CNA_PostProcessPassHandle p0, const float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_light_shaft_pass_get_intensity(CNA_PostProcessPassHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_light_shaft_pass_set_intensity(CNA_PostProcessPassHandle p0, const float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_light_shaft_pass_get_decay(CNA_PostProcessPassHandle p0, float* p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_light_shaft_pass_set_decay(CNA_PostProcessPassHandle p0, const float p1)
{
    (void)p0; (void)p1;
    return ExtensionUnavailable();
}

CNA_Result cna_aerial_perspective_pass_copy_fallback_reason(CNA_PostProcessPassHandle p0, char* p1, uint64_t p2, uint64_t* p3)
{
    (void)p0; (void)p1; (void)p2; (void)p3;
    if (p3 != nullptr) { *p3 = UINT64_C(0); }
    return ExtensionUnavailable();
}

CNA_Result cna_aerial_perspective_pass_air_mass_for_distance(const CNA_Vector3* p0, float p1, float p2, float* p3)
{
    (void)p0; (void)p1; (void)p2; (void)p3;
    return ExtensionUnavailable();
}

CNA_Result cna_aerial_perspective_pass_transmittance(float p0, float p1, CNA_Vector3* p2)
{
    (void)p0; (void)p1; (void)p2;
    return ExtensionUnavailable();
}

CNA_Result cna_height_fog_pass_optical_depth(float p0, float p1, float p2, float p3, float p4, float p5, float* p6)
{
    (void)p0; (void)p1; (void)p2; (void)p3; (void)p4; (void)p5; (void)p6;
    return ExtensionUnavailable();
}

CNA_Result cna_volumetric_fog_pass_set_light(CNA_PostProcessPassHandle p0, CNA_ShadowMapHandle p1, const CNA_Vector3* p2, const CNA_Vector3* p3)
{
    (void)p0; (void)p1; (void)p2; (void)p3;
    return ExtensionUnavailable();
}

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

namespace {

struct CascadedShadowMapResource final {
    std::shared_ptr<Ext::CascadedShadowMap> value;
    CNA_Handle parentGame;
    uint64_t activeBorrowCount = 0U;
};

struct CubeShadowMapResource final {
    std::shared_ptr<Ext::CubeShadowMap> value;
    CNA_Handle parentGame;
    uint64_t activeBorrowCount = 0U;
};

[[nodiscard]] CNA_Result RequireMatrixArgument(const CNA_Matrix* const value, const char* const what)
{
    if (value == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, what);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ToNativePointLight(
    const CNA_PointLightEXT* const value, Ext::PointLightEXT* const out)
{
    if (value == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, "The light is null.");
    }
    if (value->struct_size != static_cast<uint32_t>(sizeof(CNA_PointLightEXT)) ||
        value->struct_version != UINT32_C(1)) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The light was not initialized by cna_point_light_ext_init.");
    }
    if (const CNA_Result result = ValidateCanonicalBool(value->casts_shadows, "casts_shadows");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    out->Position = {value->position.x, value->position.y, value->position.z};
    out->Color = {value->color.x, value->color.y, value->color.z};
    out->Intensity = value->intensity;
    out->Range = value->range;
    out->CastsShadows = value->casts_shadows == CNA_TRUE;
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_cascaded_shadow_map_create(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_ShadowQuality quality,
    const int32_t cascadeCount,
    CNA_CascadedShadowMapHandle* const outShadowMap)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outShadowMap == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The cascaded-shadow-map output handle is null.");
        }
        *outShadowMap = CNA_INVALID_HANDLE;
        Ext::ShadowQuality nativeQuality{};
        if (const CNA_Result result = ToNativeShadowQuality(quality, &nativeQuality);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The canonical constructor clamps rather than refuses, but a C caller asking for a count
        // the atlas cannot hold has made a mistake worth naming rather than silently correcting.
        if (cascadeCount < 1 || cascadeCount > CNA_SHADOW_CASCADE_MAX_EXT) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "The cascade count must be between 1 and CNA_SHADOW_CASCADE_MAX_EXT.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result =
                GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto native = std::make_shared<Ext::CascadedShadowMap>(
            *graphicsDevice->value, nativeQuality, static_cast<int>(cascadeCount));
        const auto resource = std::make_shared<CascadedShadowMapResource>(
            CascadedShadowMapResource{std::move(native), graphicsDevice->parentGame, 0U});
        const CNA_Result result =
            GetRuntimeHandles().Create(ObjectKind::CascadedShadowMap, resource, outShadowMap);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned cascaded-shadow-map handle could not be created.");
        }
        AddOwnedGraphicsResourceFor(graphicsDevice->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

#define CNA_WITH_CASCADED(handle, body)                                                            \
    WithMap<CascadedShadowMapResource>(                                                            \
        (handle), ObjectKind::CascadedShadowMap, "CascadedShadowMap", body)

CNA_Result cna_cascaded_shadow_map_is_supported(
    const CNA_CascadedShadowMapHandle shadowMap, CNA_Bool* const outSupported)
{
    return CNA_WITH_CASCADED(shadowMap,
        [&](const std::shared_ptr<CascadedShadowMapResource>& map) -> CNA_Result {
            return StoreValue(
                outSupported,
                static_cast<CNA_Bool>(map->value->isSupported() ? CNA_TRUE : CNA_FALSE));
        });
}

CNA_Result cna_cascaded_shadow_map_update(
    const CNA_CascadedShadowMapHandle shadowMap,
    const CNA_DirectionalLightEXT* const light,
    const CNA_Matrix* const cameraView,
    const CNA_Matrix* const cameraProjection)
{
    return CNA_WITH_CASCADED(shadowMap,
        [&](const std::shared_ptr<CascadedShadowMapResource>& map) -> CNA_Result {
            Ext::DirectionalLightEXT nativeLight;
            if (const CNA_Result result = ToNativeDirectionalLight(light, &nativeLight);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (const CNA_Result result =
                    RequireMatrixArgument(cameraView, "The camera view is null.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (const CNA_Result result =
                    RequireMatrixArgument(cameraProjection, "The camera projection is null.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            map->value->update(
                nativeLight, ToNativeMatrix(*cameraView), ToNativeMatrix(*cameraProjection));
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_cascaded_shadow_map_begin(
    const CNA_CascadedShadowMapHandle shadowMap, const int32_t cascadeIndex)
{
    return CNA_WITH_CASCADED(shadowMap,
        [&](const std::shared_ptr<CascadedShadowMapResource>& map) -> CNA_Result {
            map->value->begin(static_cast<int>(cascadeIndex));
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_cascaded_shadow_map_end(const CNA_CascadedShadowMapHandle shadowMap)
{
    return CNA_WITH_CASCADED(shadowMap,
        [](const std::shared_ptr<CascadedShadowMapResource>& map) -> CNA_Result {
            map->value->end();
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_cascaded_shadow_map_get_cascade_count(
    const CNA_CascadedShadowMapHandle shadowMap, int32_t* const outCount)
{
    return CNA_WITH_CASCADED(shadowMap,
        [&](const std::shared_ptr<CascadedShadowMapResource>& map) -> CNA_Result {
            return StoreValue(outCount, static_cast<int32_t>(map->value->getCascadeCount()));
        });
}

CNA_Result cna_cascaded_shadow_map_get_cascade_size(
    const CNA_CascadedShadowMapHandle shadowMap, int32_t* const outSize)
{
    return CNA_WITH_CASCADED(shadowMap,
        [&](const std::shared_ptr<CascadedShadowMapResource>& map) -> CNA_Result {
            return StoreValue(outSize, static_cast<int32_t>(map->value->getCascadeSize()));
        });
}

CNA_Result cna_cascaded_shadow_map_get_shadow_texture(
    const CNA_CascadedShadowMapHandle shadowMap, CNA_Handle* const outTexture)
{
    return CNA_WITH_CASCADED(shadowMap,
        [&](const std::shared_ptr<CascadedShadowMapResource>& map) -> CNA_Result {
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

CNA_Result cna_cascaded_shadow_map_get_caster_effect(
    const CNA_CascadedShadowMapHandle shadowMap, CNA_EffectHandle* const outEffect)
{
    return CNA_WITH_CASCADED(shadowMap,
        [&](const std::shared_ptr<CascadedShadowMapResource>& map) -> CNA_Result {
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

CNA_Result cna_cascaded_shadow_map_get_cascade_matrix(
    const CNA_CascadedShadowMapHandle shadowMap,
    const int32_t cascadeIndex,
    CNA_Matrix* const outMatrix)
{
    return CNA_WITH_CASCADED(shadowMap,
        [&](const std::shared_ptr<CascadedShadowMapResource>& map) -> CNA_Result {
            return StoreValue(
                outMatrix,
                ToCMatrix(map->value->getCascadeMatrix(static_cast<int>(cascadeIndex))));
        });
}

CNA_Result cna_cascaded_shadow_map_get_split_distance(
    const CNA_CascadedShadowMapHandle shadowMap,
    const int32_t cascadeIndex,
    float* const outDistance)
{
    return CNA_WITH_CASCADED(shadowMap,
        [&](const std::shared_ptr<CascadedShadowMapResource>& map) -> CNA_Result {
            return StoreValue(
                outDistance, map->value->getSplitDistance(static_cast<int>(cascadeIndex)));
        });
}

CNA_Result cna_cascaded_shadow_map_get_blend_band(
    const CNA_CascadedShadowMapHandle shadowMap, float* const outBand)
{
    return CNA_WITH_CASCADED(shadowMap,
        [&](const std::shared_ptr<CascadedShadowMapResource>& map) -> CNA_Result {
            return StoreValue(outBand, map->value->getBlendBand());
        });
}

CNA_Result cna_cascaded_shadow_map_set_blend_band(
    const CNA_CascadedShadowMapHandle shadowMap, const float band)
{
    return CNA_WITH_CASCADED(shadowMap,
        [&](const std::shared_ptr<CascadedShadowMapResource>& map) -> CNA_Result {
            map->value->setBlendBand(band);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_cascaded_shadow_map_is_debug_tint_enabled(
    const CNA_CascadedShadowMapHandle shadowMap, CNA_Bool* const outEnabled)
{
    return CNA_WITH_CASCADED(shadowMap,
        [&](const std::shared_ptr<CascadedShadowMapResource>& map) -> CNA_Result {
            return StoreValue(
                outEnabled,
                static_cast<CNA_Bool>(map->value->isDebugTintEnabled() ? CNA_TRUE : CNA_FALSE));
        });
}

CNA_Result cna_cascaded_shadow_map_set_debug_tint_enabled(
    const CNA_CascadedShadowMapHandle shadowMap, const CNA_Bool enabled)
{
    // CBIND-067's discipline: a non-canonical CNA_Bool is refused **before** the handle is
    // resolved, so the answer is the same whatever handle came with it. Validating it inside the
    // resource lookup instead is what CApiBoolContractSmoke caught -- with an invalid handle the
    // route answered a handle error, which reads as "accepted the byte".
    if (const CNA_Result result = CNA::C::Detail::ValidateCanonicalBool(enabled, "enabled");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return CNA_WITH_CASCADED(shadowMap,
        [&](const std::shared_ptr<CascadedShadowMapResource>& map) -> CNA_Result {
            map->value->setDebugTintEnabled(enabled == CNA_TRUE);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_cascaded_shadow_map_select_cascade(
    const CNA_CascadedShadowMapHandle shadowMap, const float viewDepth, int32_t* const outIndex)
{
    return CNA_WITH_CASCADED(shadowMap,
        [&](const std::shared_ptr<CascadedShadowMapResource>& map) -> CNA_Result {
            return StoreValue(
                outIndex, static_cast<int32_t>(map->value->selectCascade(viewDepth)));
        });
}

CNA_Result cna_cascaded_shadow_map_get_split_lambda(
    const CNA_CascadedShadowMapHandle shadowMap, float* const outLambda)
{
    return CNA_WITH_CASCADED(shadowMap,
        [&](const std::shared_ptr<CascadedShadowMapResource>& map) -> CNA_Result {
            return StoreValue(outLambda, map->value->getSplitLambda());
        });
}

CNA_Result cna_cascaded_shadow_map_set_split_lambda(
    const CNA_CascadedShadowMapHandle shadowMap, const float lambda)
{
    return CNA_WITH_CASCADED(shadowMap,
        [&](const std::shared_ptr<CascadedShadowMapResource>& map) -> CNA_Result {
            map->value->setSplitLambda(lambda);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_cascaded_shadow_map_compute_split_distances(
    const float nearPlane,
    const float farPlane,
    const int32_t cascadeCount,
    const float lambda,
    float* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr || (destination == nullptr && capacity != 0U)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The split destination or required-count output is invalid.");
        }
        const std::vector<float> distances = Ext::CascadedShadowMap::computeSplitDistances(
            nearPlane, farPlane, static_cast<int>(cascadeCount), lambda);
        *outCount = static_cast<uint64_t>(distances.size());
        if (capacity < distances.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination cannot hold every split distance.");
        }
        for (std::size_t split = 0; split < distances.size(); ++split) {
            destination[split] = distances[split];
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cascaded_shadow_map_compute_frustum_corners(
    const CNA_Matrix* const view,
    const CNA_Matrix* const projection,
    CNA_Vector3* const outCorners)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCorners == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The corner destination is null.");
        }
        if (const CNA_Result result = RequireMatrixArgument(view, "The view is null.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result =
                RequireMatrixArgument(projection, "The projection is null.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto corners = Ext::CascadedShadowMap::computeFrustumCorners(
            ToNativeMatrix(*view), ToNativeMatrix(*projection));
        static_assert(
            std::tuple_size<decltype(corners)>::value == CNA_FRUSTUM_CORNER_COUNT_EXT,
            "the C corner count must equal the canonical array's size");
        for (int corner = 0; corner < CNA_FRUSTUM_CORNER_COUNT_EXT; ++corner) {
            outCorners[corner] = Vec3(
                corners[static_cast<std::size_t>(corner)].X,
                corners[static_cast<std::size_t>(corner)].Y,
                corners[static_cast<std::size_t>(corner)].Z);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cascaded_shadow_map_compute_bounding_sphere(
    const CNA_Vector3* const corners,
    CNA_Vector3* const outCentre,
    float* const outRadius)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (corners == nullptr || outCentre == nullptr || outRadius == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "A bounding-sphere argument is null.");
        }
        std::array<Microsoft::Xna::Framework::Vector3, CNA_FRUSTUM_CORNER_COUNT_EXT> nativeCorners{};
        for (int corner = 0; corner < CNA_FRUSTUM_CORNER_COUNT_EXT; ++corner) {
            nativeCorners[static_cast<std::size_t>(corner)] = {
                corners[corner].x, corners[corner].y, corners[corner].z};
        }
        Microsoft::Xna::Framework::Vector3 centre{};
        const float radius =
            Ext::CascadedShadowMap::computeBoundingSphere(nativeCorners, centre);
        *outCentre = Vec3(centre.X, centre.Y, centre.Z);
        *outRadius = radius;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cascaded_shadow_map_snap_to_texel_grid(
    const CNA_Vector3* const centre,
    const float radius,
    const int32_t cascadeSize,
    CNA_Vector3* const outCentre)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (centre == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, "The centre is null.");
        }
        const Microsoft::Xna::Framework::Vector3 nativeCentre{centre->x, centre->y, centre->z};
        const auto snapped = Ext::CascadedShadowMap::snapToTexelGrid(
            nativeCentre, radius, static_cast<int>(cascadeSize));
        return StoreValue(outCentre, Vec3(snapped.X, snapped.Y, snapped.Z));
    });
}

CNA_Result cna_cascaded_shadow_map_destroy(const CNA_CascadedShadowMapHandle shadowMapHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CascadedShadowMapResource> map;
        if (const CNA_Result result = GetEngineResource(
                shadowMapHandle, ObjectKind::CascadedShadowMap, "CascadedShadowMap", &map);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (map->activeBorrowCount != 0U) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The cascaded shadow map is still lending its effect or its atlas.");
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(shadowMapHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The owned cascaded-shadow-map handle could not be released.");
        }
        RemoveOwnedGraphicsResourceFor(map->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

#define CNA_WITH_CUBE(handle, body)                                                                \
    WithMap<CubeShadowMapResource>((handle), ObjectKind::CubeShadowMap, "CubeShadowMap", body)

CNA_Result cna_cube_shadow_map_create(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_ShadowQuality quality,
    CNA_CubeShadowMapHandle* const outShadowMap)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outShadowMap == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The cube-shadow-map output handle is null.");
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
        auto native = std::make_shared<Ext::CubeShadowMap>(*graphicsDevice->value, nativeQuality);
        const auto resource = std::make_shared<CubeShadowMapResource>(
            CubeShadowMapResource{std::move(native), graphicsDevice->parentGame, 0U});
        const CNA_Result result =
            GetRuntimeHandles().Create(ObjectKind::CubeShadowMap, resource, outShadowMap);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned cube-shadow-map handle could not be created.");
        }
        AddOwnedGraphicsResourceFor(graphicsDevice->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_cube_shadow_map_is_supported(
    const CNA_CubeShadowMapHandle shadowMap, CNA_Bool* const outSupported)
{
    return CNA_WITH_CUBE(shadowMap,
        [&](const std::shared_ptr<CubeShadowMapResource>& map) -> CNA_Result {
            return StoreValue(
                outSupported,
                static_cast<CNA_Bool>(map->value->isSupported() ? CNA_TRUE : CNA_FALSE));
        });
}

CNA_Result cna_cube_shadow_map_update(
    const CNA_CubeShadowMapHandle shadowMap, const CNA_PointLightEXT* const light)
{
    return CNA_WITH_CUBE(shadowMap,
        [&](const std::shared_ptr<CubeShadowMapResource>& map) -> CNA_Result {
            Ext::PointLightEXT nativeLight;
            if (const CNA_Result result = ToNativePointLight(light, &nativeLight);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            map->value->update(nativeLight);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_cube_shadow_map_begin(
    const CNA_CubeShadowMapHandle shadowMap, const int32_t faceIndex)
{
    return CNA_WITH_CUBE(shadowMap,
        [&](const std::shared_ptr<CubeShadowMapResource>& map) -> CNA_Result {
            map->value->begin(static_cast<int>(faceIndex));
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_cube_shadow_map_end(const CNA_CubeShadowMapHandle shadowMap)
{
    return CNA_WITH_CUBE(shadowMap,
        [](const std::shared_ptr<CubeShadowMapResource>& map) -> CNA_Result {
            map->value->end();
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_cube_shadow_map_get_shadow_texture(
    const CNA_CubeShadowMapHandle shadowMap, CNA_Handle* const outTexture)
{
    return CNA_WITH_CUBE(shadowMap,
        [&](const std::shared_ptr<CubeShadowMapResource>& map) -> CNA_Result {
            if (outTexture == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The texture output handle is null.");
            }
            *outTexture = CNA_INVALID_HANDLE;
            auto* const cube = map->value->getShadowTexture();
            if (cube == nullptr) {
                return CNA_RESULT_SUCCESS;
            }
            // cna_texturecube_destroy releases the handle without disposing the object, so the
            // aliasing view below is safe to hand out: the map keeps owning its cube, and the
            // borrow keeps the map alive until the caller releases the handle.
            const auto borrow = std::make_shared<CountedBorrow<CubeShadowMapResource>>(map);
            const std::shared_ptr<Microsoft::Xna::Framework::Graphics::TextureCube> view(
                borrow, cube);
            return CreateOwnedTextureCube(view, map->parentGame, outTexture);
        });
}

CNA_Result cna_cube_shadow_map_get_caster_effect(
    const CNA_CubeShadowMapHandle shadowMap, CNA_EffectHandle* const outEffect)
{
    return CNA_WITH_CUBE(shadowMap,
        [&](const std::shared_ptr<CubeShadowMapResource>& map) -> CNA_Result {
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

CNA_Result cna_cube_shadow_map_get_size(
    const CNA_CubeShadowMapHandle shadowMap, int32_t* const outSize)
{
    return CNA_WITH_CUBE(shadowMap,
        [&](const std::shared_ptr<CubeShadowMapResource>& map) -> CNA_Result {
            return StoreValue(outSize, static_cast<int32_t>(map->value->getSize()));
        });
}

CNA_Result cna_cube_shadow_map_get_quality(
    const CNA_CubeShadowMapHandle shadowMap, CNA_ShadowQuality* const outQuality)
{
    return CNA_WITH_CUBE(shadowMap,
        [&](const std::shared_ptr<CubeShadowMapResource>& map) -> CNA_Result {
            return StoreValue(outQuality, NativeOrdinal(map->value->getQuality()));
        });
}

CNA_Result cna_cube_shadow_map_get_light_position(
    const CNA_CubeShadowMapHandle shadowMap, CNA_Vector3* const outPosition)
{
    return CNA_WITH_CUBE(shadowMap,
        [&](const std::shared_ptr<CubeShadowMapResource>& map) -> CNA_Result {
            const auto position = map->value->getLightPosition();
            return StoreValue(outPosition, Vec3(position.X, position.Y, position.Z));
        });
}

CNA_Result cna_cube_shadow_map_get_light_range(
    const CNA_CubeShadowMapHandle shadowMap, float* const outRange)
{
    return CNA_WITH_CUBE(shadowMap,
        [&](const std::shared_ptr<CubeShadowMapResource>& map) -> CNA_Result {
            return StoreValue(outRange, map->value->getLightRange());
        });
}

CNA_Result cna_cube_shadow_map_get_depth_bias(
    const CNA_CubeShadowMapHandle shadowMap, float* const outBias)
{
    return CNA_WITH_CUBE(shadowMap,
        [&](const std::shared_ptr<CubeShadowMapResource>& map) -> CNA_Result {
            return StoreValue(outBias, map->value->getDepthBias());
        });
}

CNA_Result cna_cube_shadow_map_set_depth_bias(
    const CNA_CubeShadowMapHandle shadowMap, const float bias)
{
    return CNA_WITH_CUBE(shadowMap,
        [&](const std::shared_ptr<CubeShadowMapResource>& map) -> CNA_Result {
            map->value->setDepthBias(bias);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_cube_shadow_map_compute_face_view(
    const CNA_CubeMapFace face, const CNA_Vector3* const position, CNA_Matrix* const outMatrix)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (position == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, "The position is null.");
        }
        if (face > CNA_CUBE_MAP_FACE_NEGATIVE_Z) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The cube face is not a defined CNA_CUBE_MAP_FACE_* value.");
        }
        const Microsoft::Xna::Framework::Vector3 nativePosition{
            position->x, position->y, position->z};
        return StoreValue(
            outMatrix,
            ToCMatrix(Ext::CubeShadowMap::computeFaceView(
                static_cast<Microsoft::Xna::Framework::Graphics::CubeMapFace>(face),
                nativePosition)));
    });
}

CNA_Result cna_cube_shadow_map_compute_face_projection(
    const float range, CNA_Matrix* const outMatrix)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return StoreValue(outMatrix, ToCMatrix(Ext::CubeShadowMap::computeFaceProjection(range)));
    });
}

CNA_Result cna_cube_shadow_map_size_for_quality(
    const CNA_ShadowQuality quality, int32_t* const outSize)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Ext::ShadowQuality nativeQuality{};
        if (const CNA_Result result = ToNativeShadowQuality(quality, &nativeQuality);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return StoreValue(
            outSize, static_cast<int32_t>(Ext::CubeShadowMap::sizeForQuality(nativeQuality)));
    });
}

CNA_Result cna_cube_shadow_map_destroy(const CNA_CubeShadowMapHandle shadowMapHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CubeShadowMapResource> map;
        if (const CNA_Result result = GetEngineResource(
                shadowMapHandle, ObjectKind::CubeShadowMap, "CubeShadowMap", &map);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (map->activeBorrowCount != 0U) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The cube shadow map is still lending its effect or its cube.");
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(shadowMapHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The owned cube-shadow-map handle could not be released.");
        }
        RemoveOwnedGraphicsResourceFor(map->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

namespace {

using Microsoft::Xna::Framework::Graphics::IShadowReceiverEXT;
using Microsoft::Xna::Framework::Graphics::PunctualLightEXT;
using Microsoft::Xna::Framework::Graphics::ShadowCascadeStateEXT;

struct ClusteredShadowPolicyResource final {
    std::shared_ptr<Ext::ClusteredShadowPolicyEXT> value;
    CNA_Handle parentGame;
};

// The interface is what an effect implements, so the C form resolves the effect and asks whether
// this concrete one is a receiver. A dynamic_cast is the honest test: BasicEffect and SkinnedEffect
// implement it, a SpriteEffect does not, and the answer belongs to the object rather than to a
// flag this ABI could get out of step with.
template<typename TCallable>
[[nodiscard]] CNA_Result WithShadowReceiver(const CNA_EffectHandle handle, TCallable&& body)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<EffectResource> effect;
        if (const CNA_Result result = GetEffectForPass(handle, &effect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto* const receiver = dynamic_cast<IShadowReceiverEXT*>(effect->value.get());
        if (receiver == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "This effect does not receive shadows.");
        }
        return std::forward<TCallable>(body)(*receiver, effect);
    });
}

[[nodiscard]] CNA_Result ToNativeCascadeState(
    const CNA_ShadowCascadeStateEXT* const value, ShadowCascadeStateEXT* const out)
{
    if (value == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, "The cascade state is null.");
    }
    if (value->struct_size != static_cast<uint32_t>(sizeof(CNA_ShadowCascadeStateEXT)) ||
        value->struct_version != UINT32_C(1)) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The cascade state was not initialized by cna_shadow_cascade_state_ext_init.");
    }
    if (const CNA_Result result = ValidateCanonicalBool(value->debug_tint, "debug_tint");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (value->count < 0 || value->count > CNA_SHADOW_CASCADE_MAX_EXT) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_RANGE,
            "The cascade count is outside the fixed array.");
    }
    out->Count = static_cast<int>(value->count);
    out->BlendBand = value->blend_band;
    out->DebugTint = value->debug_tint == CNA_TRUE;
    out->CameraView = ToNativeMatrix(value->camera_view);
    for (int cascade = 0; cascade < CNA_SHADOW_CASCADE_MAX_EXT; ++cascade) {
        out->WorldToAtlas[cascade] = ToNativeMatrix(value->world_to_atlas[cascade]);
        out->SplitDistance[cascade] = value->split_distance[cascade];
    }
    return CNA_RESULT_SUCCESS;
}

void FromNativeCascadeState(
    const ShadowCascadeStateEXT& value, CNA_ShadowCascadeStateEXT* const out)
{
    out->struct_size = static_cast<uint32_t>(sizeof(CNA_ShadowCascadeStateEXT));
    out->struct_version = UINT32_C(1);
    out->count = static_cast<int32_t>(value.Count);
    out->blend_band = value.BlendBand;
    out->debug_tint = value.DebugTint ? CNA_TRUE : CNA_FALSE;
    out->reserved[0] = 0U; out->reserved[1] = 0U; out->reserved[2] = 0U;
    out->camera_view = ToCMatrix(value.CameraView);
    for (int cascade = 0; cascade < CNA_SHADOW_CASCADE_MAX_EXT; ++cascade) {
        out->world_to_atlas[cascade] = ToCMatrix(value.WorldToAtlas[cascade]);
        out->split_distance[cascade] = value.SplitDistance[cascade];
    }
}

[[nodiscard]] CNA_Result ToNativePunctualLight(
    const CNA_PunctualLightEXT* const value, PunctualLightEXT* const out)
{
    if (value == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, "The light is null.");
    }
    if (value->struct_size != static_cast<uint32_t>(sizeof(CNA_PunctualLightEXT)) ||
        value->struct_version != UINT32_C(1)) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The light was not initialized by cna_punctual_light_ext_init.");
    }
    if (value->kind > CNA_PUNCTUAL_LIGHT_KIND_EXT_SPOT) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The light kind is not a defined CNA_PUNCTUAL_LIGHT_KIND_EXT_* value.");
    }
    out->Kind = static_cast<Microsoft::Xna::Framework::Graphics::PunctualLightKindEXT>(value->kind);
    out->Position = {value->position.x, value->position.y, value->position.z};
    out->Direction = {value->direction.x, value->direction.y, value->direction.z};
    out->DiffuseColor = {value->diffuse_color.x, value->diffuse_color.y, value->diffuse_color.z};
    out->Range = value->range;
    out->InnerAngle = value->inner_angle;
    out->OuterAngle = value->outer_angle;
    out->ShadowDepthBias = value->shadow_depth_bias;
    out->ShadowViewProjection = ToNativeMatrix(value->shadow_view_projection);
    // The two shadow textures are resolved from their handles; an invalid handle means none, which
    // is the canonical null.
    out->ShadowCube = nullptr;
    out->ShadowMap = nullptr;
    if (value->shadow_map != CNA_INVALID_HANDLE) {
        std::shared_ptr<Texture2DResource> texture;
        if (const CNA_Result result = GetOwnedTexture2D(value->shadow_map, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        out->ShadowMap = texture->value.get();
    }
    if (value->shadow_cube != CNA_INVALID_HANDLE) {
        std::shared_ptr<CNA::C::Detail::TextureCubeResource> cube;
        if (const CNA_Result result = GetRuntimeHandles().Get(
                value->shadow_cube, ObjectKind::TextureCube, &cube);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The shadow-cube handle is invalid for this call.");
        }
        out->ShadowCube = cube->value.get();
    }
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_effect_set_shadow_map_ext(
    const CNA_EffectHandle effect, const CNA_Handle shadowMap)
{
    return WithShadowReceiver(effect,
        [&](IShadowReceiverEXT& receiver, const std::shared_ptr<EffectResource>&) -> CNA_Result {
            Microsoft::Xna::Framework::Graphics::Texture2D* texture = nullptr;
            std::shared_ptr<Texture2DResource> retention;
            if (const CNA_Result result =
                    ResolveTexture2DArgument(shadowMap, "shadow map", &texture, &retention);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            receiver.setShadowMapEXT(texture);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_effect_get_shadow_map_ext(
    const CNA_EffectHandle effect, CNA_Handle* const outShadowMap)
{
    return WithShadowReceiver(effect,
        [&](IShadowReceiverEXT& receiver,
            const std::shared_ptr<EffectResource>& resource) -> CNA_Result {
            if (outShadowMap == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The shadow-map output handle is null.");
            }
            *outShadowMap = CNA_INVALID_HANDLE;
            auto* const texture = receiver.getShadowMapEXT();
            if (texture == nullptr) {
                return CNA_RESULT_SUCCESS;
            }
            // A fresh handle naming the same texture: the canonical interface stores a raw
            // pointer and has no handle to give back, so identity with the handle originally set
            // cannot be preserved.
            //
            // It is NOT a counted borrow, and deliberately so. What could dangle here is the
            // texture, and the effect does not own it -- the canonical contract is that the caller
            // owns the shadow map and merely lends it. Counting a borrow on the effect would
            // suggest this ABI was keeping the texture alive when it is not, and would refuse to
            // destroy the effect for a reason that protects nothing. The view aliases the effect
            // resource so the handle cannot outlive the effect that answered for it; the texture's
            // lifetime stays where the canonical API puts it, with the caller.
            const std::shared_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> view(
                resource, texture);
            return CreateBorrowedRenderTarget2D(
                view, resource->parentGame, resource, outShadowMap);
        });
}

CNA_Result cna_effect_set_light_view_projection_ext(
    const CNA_EffectHandle effect, const CNA_Matrix* const lightViewProjection)
{
    return WithShadowReceiver(effect,
        [&](IShadowReceiverEXT& receiver, const std::shared_ptr<EffectResource>&) -> CNA_Result {
            if (const CNA_Result result = RequireMatrixArgument(
                    lightViewProjection, "The light view-projection is null.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            receiver.setLightViewProjectionEXT(ToNativeMatrix(*lightViewProjection));
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_effect_get_light_view_projection_ext(
    const CNA_EffectHandle effect, CNA_Matrix* const outMatrix)
{
    return WithShadowReceiver(effect,
        [&](IShadowReceiverEXT& receiver, const std::shared_ptr<EffectResource>&) -> CNA_Result {
            return StoreValue(outMatrix, ToCMatrix(receiver.getLightViewProjectionEXT()));
        });
}

CNA_Result cna_effect_set_shadows_enabled_ext(
    const CNA_EffectHandle effect, const CNA_Bool enabled)
{
    // CBIND-067: the boolean is refused before the handle is resolved.
    if (const CNA_Result result = ValidateCanonicalBool(enabled, "enabled");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return WithShadowReceiver(effect,
        [&](IShadowReceiverEXT& receiver, const std::shared_ptr<EffectResource>&) -> CNA_Result {
            receiver.setShadowsEnabledEXT(enabled == CNA_TRUE);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_effect_is_shadows_enabled_ext(
    const CNA_EffectHandle effect, CNA_Bool* const outEnabled)
{
    return WithShadowReceiver(effect,
        [&](IShadowReceiverEXT& receiver, const std::shared_ptr<EffectResource>&) -> CNA_Result {
            return StoreValue(
                outEnabled,
                static_cast<CNA_Bool>(receiver.isShadowsEnabledEXT() ? CNA_TRUE : CNA_FALSE));
        });
}

CNA_Result cna_effect_set_shadow_depth_bias_ext(const CNA_EffectHandle effect, const float bias)
{
    return WithShadowReceiver(effect,
        [&](IShadowReceiverEXT& receiver, const std::shared_ptr<EffectResource>&) -> CNA_Result {
            receiver.setShadowDepthBiasEXT(bias);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_effect_get_shadow_depth_bias_ext(
    const CNA_EffectHandle effect, float* const outBias)
{
    return WithShadowReceiver(effect,
        [&](IShadowReceiverEXT& receiver, const std::shared_ptr<EffectResource>&) -> CNA_Result {
            return StoreValue(outBias, receiver.getShadowDepthBiasEXT());
        });
}

CNA_Result cna_effect_set_shadow_filter_radius_ext(
    const CNA_EffectHandle effect, const int32_t radius)
{
    return WithShadowReceiver(effect,
        [&](IShadowReceiverEXT& receiver, const std::shared_ptr<EffectResource>&) -> CNA_Result {
            receiver.setShadowFilterRadiusEXT(static_cast<int>(radius));
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_effect_get_shadow_filter_radius_ext(
    const CNA_EffectHandle effect, int32_t* const outRadius)
{
    return WithShadowReceiver(effect,
        [&](IShadowReceiverEXT& receiver, const std::shared_ptr<EffectResource>&) -> CNA_Result {
            return StoreValue(
                outRadius, static_cast<int32_t>(receiver.getShadowFilterRadiusEXT()));
        });
}

CNA_Result cna_effect_set_shadow_cascades_ext(
    const CNA_EffectHandle effect, const CNA_ShadowCascadeStateEXT* const state)
{
    return WithShadowReceiver(effect,
        [&](IShadowReceiverEXT& receiver, const std::shared_ptr<EffectResource>&) -> CNA_Result {
            ShadowCascadeStateEXT native;
            if (const CNA_Result result = ToNativeCascadeState(state, &native);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            receiver.setShadowCascadesEXT(native);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_effect_get_shadow_cascades_ext(
    const CNA_EffectHandle effect, CNA_ShadowCascadeStateEXT* const outState)
{
    return WithShadowReceiver(effect,
        [&](IShadowReceiverEXT& receiver, const std::shared_ptr<EffectResource>&) -> CNA_Result {
            if (outState == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The cascade-state output is null.");
            }
            FromNativeCascadeState(receiver.getShadowCascadesEXT(), outState);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_effect_set_punctual_light_ext(
    const CNA_EffectHandle effect, const CNA_PunctualLightEXT* const light)
{
    return WithShadowReceiver(effect,
        [&](IShadowReceiverEXT& receiver, const std::shared_ptr<EffectResource>&) -> CNA_Result {
            PunctualLightEXT native;
            if (const CNA_Result result = ToNativePunctualLight(light, &native);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            receiver.setPunctualLightEXT(native);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_effect_get_punctual_light_ext(
    const CNA_EffectHandle effect, CNA_PunctualLightEXT* const outLight)
{
    return WithShadowReceiver(effect,
        [&](IShadowReceiverEXT& receiver, const std::shared_ptr<EffectResource>&) -> CNA_Result {
            if (outLight == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The light output is null.");
            }
            const PunctualLightEXT& native = receiver.getPunctualLightEXT();
            CNA_PunctualLightEXT value;
            std::memset(&value, 0, sizeof(value));
            value.struct_size = static_cast<uint32_t>(sizeof(CNA_PunctualLightEXT));
            value.struct_version = UINT32_C(1);
            value.kind = static_cast<CNA_PunctualLightKindEXT>(native.Kind);
            value.position = Vec3(native.Position.X, native.Position.Y, native.Position.Z);
            value.direction = Vec3(native.Direction.X, native.Direction.Y, native.Direction.Z);
            value.diffuse_color =
                Vec3(native.DiffuseColor.X, native.DiffuseColor.Y, native.DiffuseColor.Z);
            value.range = native.Range;
            value.inner_angle = native.InnerAngle;
            value.outer_angle = native.OuterAngle;
            value.shadow_depth_bias = native.ShadowDepthBias;
            value.shadow_view_projection = ToCMatrix(native.ShadowViewProjection);
            // The canonical structure holds raw texture pointers; this ABI does not invent a name
            // for a texture it does not track, so the two slots come back invalid.
            value.shadow_cube = CNA_INVALID_HANDLE;
            value.shadow_map = CNA_INVALID_HANDLE;
            *outLight = value;
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_cascaded_shadow_map_apply_to_receiver(
    const CNA_CascadedShadowMapHandle shadowMap, const CNA_EffectHandle effect)
{
    return CNA_WITH_CASCADED(shadowMap,
        [&](const std::shared_ptr<CascadedShadowMapResource>& map) -> CNA_Result {
            return WithShadowReceiver(effect,
                [&](IShadowReceiverEXT& receiver,
                    const std::shared_ptr<EffectResource>&) -> CNA_Result {
                    map->value->applyToReceiver(receiver);
                    return CNA_RESULT_SUCCESS;
                });
        });
}

CNA_Result cna_clustered_shadow_policy_create(
    const CNA_Handle gameHandle,
    const int32_t budget,
    CNA_ClusteredShadowPolicyHandle* const outPolicy)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPolicy == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The shadow-policy output handle is null.");
        }
        *outPolicy = CNA_INVALID_HANDLE;
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(gameHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto native =
            std::make_shared<Ext::ClusteredShadowPolicyEXT>(static_cast<int>(budget));
        const auto resource = std::make_shared<ClusteredShadowPolicyResource>(
            ClusteredShadowPolicyResource{std::move(native), graphicsDevice->parentGame});
        const CNA_Result result =
            GetRuntimeHandles().Create(ObjectKind::ClusteredShadowPolicy, resource, outPolicy);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned shadow-policy handle could not be created.");
        }
        AddOwnedGraphicsResourceFor(graphicsDevice->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

#define CNA_WITH_POLICY(handle, body)                                                              \
    WithMap<ClusteredShadowPolicyResource>(                                                        \
        (handle), ObjectKind::ClusteredShadowPolicy, "ClusteredShadowPolicyEXT", body)

CNA_Result cna_clustered_shadow_policy_get_budget(
    const CNA_ClusteredShadowPolicyHandle policy, int32_t* const outBudget)
{
    return CNA_WITH_POLICY(policy,
        [&](const std::shared_ptr<ClusteredShadowPolicyResource>& p) -> CNA_Result {
            return StoreValue(outBudget, static_cast<int32_t>(p->value->getBudget()));
        });
}

CNA_Result cna_clustered_shadow_policy_set_budget(
    const CNA_ClusteredShadowPolicyHandle policy, const int32_t budget)
{
    return CNA_WITH_POLICY(policy,
        [&](const std::shared_ptr<ClusteredShadowPolicyResource>& p) -> CNA_Result {
            p->value->setBudget(static_cast<int>(budget));
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_clustered_shadow_policy_get_hysteresis(
    const CNA_ClusteredShadowPolicyHandle policy, float* const outHysteresis)
{
    return CNA_WITH_POLICY(policy,
        [&](const std::shared_ptr<ClusteredShadowPolicyResource>& p) -> CNA_Result {
            return StoreValue(outHysteresis, p->value->getHysteresis());
        });
}

CNA_Result cna_clustered_shadow_policy_set_hysteresis(
    const CNA_ClusteredShadowPolicyHandle policy, const float hysteresis)
{
    return CNA_WITH_POLICY(policy,
        [&](const std::shared_ptr<ClusteredShadowPolicyResource>& p) -> CNA_Result {
            p->value->setHysteresis(hysteresis);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_clustered_shadow_policy_copy_selected(
    const CNA_ClusteredShadowPolicyHandle policy,
    int32_t* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CNA_WITH_POLICY(policy,
        [&](const std::shared_ptr<ClusteredShadowPolicyResource>& p) -> CNA_Result {
            if (outCount == nullptr || (destination == nullptr && capacity != 0U)) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The selection destination or required-count output is invalid.");
            }
            const std::vector<int>& selected = p->value->getSelected();
            *outCount = static_cast<uint64_t>(selected.size());
            if (capacity < selected.size()) {
                return Fail(
                    CNA_RESULT_BUFFER_TOO_SMALL,
                    CNA_ERROR_CATEGORY_RANGE,
                    "The destination cannot hold every selected light.");
            }
            for (std::size_t index = 0; index < selected.size(); ++index) {
                destination[index] = static_cast<int32_t>(selected[index]);
            }
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_clustered_shadow_policy_is_selected(
    const CNA_ClusteredShadowPolicyHandle policy,
    const int32_t lightIndex,
    CNA_Bool* const outSelected)
{
    return CNA_WITH_POLICY(policy,
        [&](const std::shared_ptr<ClusteredShadowPolicyResource>& p) -> CNA_Result {
            return StoreValue(
                outSelected,
                static_cast<CNA_Bool>(
                    p->value->isSelected(static_cast<int>(lightIndex)) ? CNA_TRUE : CNA_FALSE));
        });
}

CNA_Result cna_clustered_shadow_policy_get_score(
    const CNA_ClusteredShadowPolicyHandle policy,
    const int32_t lightIndex,
    float* const outScore)
{
    return CNA_WITH_POLICY(policy,
        [&](const std::shared_ptr<ClusteredShadowPolicyResource>& p) -> CNA_Result {
            return StoreValue(outScore, p->value->getScore(static_cast<int>(lightIndex)));
        });
}

CNA_Result cna_clustered_shadow_policy_get_request_count(
    const CNA_ClusteredShadowPolicyHandle policy, int32_t* const outCount)
{
    return CNA_WITH_POLICY(policy,
        [&](const std::shared_ptr<ClusteredShadowPolicyResource>& p) -> CNA_Result {
            return StoreValue(outCount, static_cast<int32_t>(p->value->getRequestCount()));
        });
}

CNA_Result cna_clustered_shadow_policy_get_refused_count(
    const CNA_ClusteredShadowPolicyHandle policy, int32_t* const outCount)
{
    return CNA_WITH_POLICY(policy,
        [&](const std::shared_ptr<ClusteredShadowPolicyResource>& p) -> CNA_Result {
            return StoreValue(outCount, static_cast<int32_t>(p->value->getRefusedCount()));
        });
}

CNA_Result cna_clustered_shadow_policy_reset(const CNA_ClusteredShadowPolicyHandle policy)
{
    return CNA_WITH_POLICY(policy,
        [](const std::shared_ptr<ClusteredShadowPolicyResource>& p) -> CNA_Result {
            p->value->reset();
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_clustered_shadow_policy_destroy(const CNA_ClusteredShadowPolicyHandle policyHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ClusteredShadowPolicyResource> policy;
        if (const CNA_Result result = GetEngineResource(
                policyHandle, ObjectKind::ClusteredShadowPolicy, "ClusteredShadowPolicyEXT",
                &policy);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(policyHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The owned shadow-policy handle could not be released.");
        }
        RemoveOwnedGraphicsResourceFor(policy->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

namespace {

struct DepthNormalPrepassResource final {
    std::shared_ptr<Ext::DepthNormalPrepass> value;
    CNA_Handle parentGame;
    uint64_t activeBorrowCount = 0U;
};

[[nodiscard]] CNA_Result ToNativeDepthEncoding(
    const CNA_DepthEncoding value, Ext::DepthEncoding* const out)
{
    if (value > CNA_DEPTH_ENCODING_HALF_FLOAT) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The depth encoding is not a defined CNA_DEPTH_ENCODING_* value.");
    }
    *out = static_cast<Ext::DepthEncoding>(value);
    return CNA_RESULT_SUCCESS;
}

// A contact-shadow pass shares PostProcessPass's handle kind, so the discriminator is the concrete
// type rather than the kind -- the same test the effect-pass routes use, for the same reason.
template<typename TCallable>
[[nodiscard]] CNA_Result WithContactShadowPass(const CNA_Handle handle, TCallable&& body)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<PostProcessPassResource> pass;
        if (const CNA_Result result = GetPostProcessPass(handle, &pass);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto* const contact = dynamic_cast<Ext::ContactShadowPass*>(pass->value.get());
        if (contact == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "This post-process pass is not a contact-shadow pass.");
        }
        return std::forward<TCallable>(body)(*contact);
    });
}

} // namespace

CNA_Result cna_depth_normal_prepass_create(
    const CNA_Handle graphicsDeviceHandle,
    const int32_t width,
    const int32_t height,
    const CNA_DepthEncoding encoding,
    CNA_DepthNormalPrepassHandle* const outPrepass)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPrepass == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The prepass output handle is null.");
        }
        *outPrepass = CNA_INVALID_HANDLE;
        Ext::DepthEncoding nativeEncoding{};
        if (const CNA_Result result = ToNativeDepthEncoding(encoding, &nativeEncoding);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (width <= 0 || height <= 0) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "The prepass target size must be positive.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result =
                GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto native = std::make_shared<Ext::DepthNormalPrepass>(
            *graphicsDevice->value, static_cast<int>(width), static_cast<int>(height),
            nativeEncoding);
        const auto resource = std::make_shared<DepthNormalPrepassResource>(
            DepthNormalPrepassResource{std::move(native), graphicsDevice->parentGame, 0U});
        const CNA_Result result =
            GetRuntimeHandles().Create(ObjectKind::DepthNormalPrepass, resource, outPrepass);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned prepass handle could not be created.");
        }
        AddOwnedGraphicsResourceFor(graphicsDevice->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

#define CNA_WITH_PREPASS(handle, body)                                                             \
    WithMap<DepthNormalPrepassResource>(                                                           \
        (handle), ObjectKind::DepthNormalPrepass, "DepthNormalPrepass", body)

CNA_Result cna_depth_normal_prepass_resize(
    const CNA_DepthNormalPrepassHandle prepass, const int32_t width, const int32_t height)
{
    return CNA_WITH_PREPASS(prepass,
        [&](const std::shared_ptr<DepthNormalPrepassResource>& p) -> CNA_Result {
            p->value->resize(static_cast<int>(width), static_cast<int>(height));
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_depth_normal_prepass_get_pass_count(
    const CNA_DepthNormalPrepassHandle prepass, int32_t* const outCount)
{
    return CNA_WITH_PREPASS(prepass,
        [&](const std::shared_ptr<DepthNormalPrepassResource>& p) -> CNA_Result {
            return StoreValue(outCount, static_cast<int32_t>(p->value->getPassCount()));
        });
}

CNA_Result cna_depth_normal_prepass_begin(
    const CNA_DepthNormalPrepassHandle prepass,
    const int32_t passIndex,
    const CNA_Matrix* const view,
    const CNA_Matrix* const projection,
    const float nearPlane,
    const float farPlane)
{
    return CNA_WITH_PREPASS(prepass,
        [&](const std::shared_ptr<DepthNormalPrepassResource>& p) -> CNA_Result {
            if (const CNA_Result result = RequireMatrixArgument(view, "The view is null.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (const CNA_Result result =
                    RequireMatrixArgument(projection, "The projection is null.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            p->value->begin(
                static_cast<int>(passIndex), ToNativeMatrix(*view), ToNativeMatrix(*projection),
                nearPlane, farPlane);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_depth_normal_prepass_end(const CNA_DepthNormalPrepassHandle prepass)
{
    return CNA_WITH_PREPASS(prepass,
        [](const std::shared_ptr<DepthNormalPrepassResource>& p) -> CNA_Result {
            p->value->end();
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_depth_normal_prepass_get_prepass_effect(
    const CNA_DepthNormalPrepassHandle prepass, CNA_EffectHandle* const outEffect)
{
    return CNA_WITH_PREPASS(prepass,
        [&](const std::shared_ptr<DepthNormalPrepassResource>& p) -> CNA_Result {
            if (outEffect == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The effect output handle is null.");
            }
            *outEffect = CNA_INVALID_HANDLE;
            return BorrowEffectFrom(p, p->value->getPrepassEffect(), outEffect);
        });
}

CNA_Result cna_depth_normal_prepass_get_skinned_prepass_effect(
    const CNA_DepthNormalPrepassHandle prepass, CNA_EffectHandle* const outEffect)
{
    return CNA_WITH_PREPASS(prepass,
        [&](const std::shared_ptr<DepthNormalPrepassResource>& p) -> CNA_Result {
            if (outEffect == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The effect output handle is null.");
            }
            *outEffect = CNA_INVALID_HANDLE;
            return BorrowEffectFrom(p, p->value->getSkinnedPrepassEffect(), outEffect);
        });
}

CNA_Result cna_depth_normal_prepass_get_depth_texture(
    const CNA_DepthNormalPrepassHandle prepass, CNA_Handle* const outTexture)
{
    return CNA_WITH_PREPASS(prepass,
        [&](const std::shared_ptr<DepthNormalPrepassResource>& p) -> CNA_Result {
            if (outTexture == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The texture output handle is null.");
            }
            *outTexture = CNA_INVALID_HANDLE;
            return BorrowShadowTextureFrom(p, p->value->getDepthTexture(), outTexture);
        });
}

CNA_Result cna_depth_normal_prepass_get_normal_texture(
    const CNA_DepthNormalPrepassHandle prepass, CNA_Handle* const outTexture)
{
    return CNA_WITH_PREPASS(prepass,
        [&](const std::shared_ptr<DepthNormalPrepassResource>& p) -> CNA_Result {
            if (outTexture == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The texture output handle is null.");
            }
            *outTexture = CNA_INVALID_HANDLE;
            return BorrowShadowTextureFrom(p, p->value->getNormalTexture(), outTexture);
        });
}

CNA_Result cna_depth_normal_prepass_get_velocity_texture_ext(
    const CNA_DepthNormalPrepassHandle prepass, CNA_Handle* const outTexture)
{
    return CNA_WITH_PREPASS(prepass,
        [&](const std::shared_ptr<DepthNormalPrepassResource>& p) -> CNA_Result {
            if (outTexture == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The texture output handle is null.");
            }
            *outTexture = CNA_INVALID_HANDLE;
            return BorrowShadowTextureFrom(p, p->value->getVelocityTextureEXT(), outTexture);
        });
}

CNA_Result cna_depth_normal_prepass_is_supported(
    const CNA_DepthNormalPrepassHandle prepass,
    const CNA_Handle graphicsDeviceHandle,
    CNA_Bool* const outSupported)
{
    return CNA_WITH_PREPASS(prepass,
        [&](const std::shared_ptr<DepthNormalPrepassResource>& p) -> CNA_Result {
            std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
            if (const CNA_Result result =
                    GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            return StoreValue(
                outSupported,
                static_cast<CNA_Bool>(
                    p->value->isSupported(*graphicsDevice->value) ? CNA_TRUE : CNA_FALSE));
        });
}

CNA_Result cna_depth_normal_prepass_is_using_multiple_render_targets(
    const CNA_DepthNormalPrepassHandle prepass, CNA_Bool* const outUsing)
{
    return CNA_WITH_PREPASS(prepass,
        [&](const std::shared_ptr<DepthNormalPrepassResource>& p) -> CNA_Result {
            return StoreValue(
                outUsing,
                static_cast<CNA_Bool>(
                    p->value->isUsingMultipleRenderTargets() ? CNA_TRUE : CNA_FALSE));
        });
}

CNA_Result cna_depth_normal_prepass_is_depth_packed(
    const CNA_DepthNormalPrepassHandle prepass, CNA_Bool* const outPacked)
{
    return CNA_WITH_PREPASS(prepass,
        [&](const std::shared_ptr<DepthNormalPrepassResource>& p) -> CNA_Result {
            return StoreValue(
                outPacked,
                static_cast<CNA_Bool>(p->value->isDepthPacked() ? CNA_TRUE : CNA_FALSE));
        });
}

CNA_Result cna_depth_normal_prepass_uses_packed_depth_ext(
    const CNA_Handle graphicsDeviceHandle, CNA_Bool* const outPacked)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPacked == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The packed-depth output is null.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result =
                GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outPacked =
            Ext::DepthNormalPrepass::usesPackedDepthEXT(*graphicsDevice->value)
                ? CNA_TRUE
                : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_depth_normal_prepass_get_roughness(
    const CNA_DepthNormalPrepassHandle prepass, float* const outRoughness)
{
    return CNA_WITH_PREPASS(prepass,
        [&](const std::shared_ptr<DepthNormalPrepassResource>& p) -> CNA_Result {
            return StoreValue(outRoughness, p->value->getRoughness());
        });
}

CNA_Result cna_depth_normal_prepass_set_roughness(
    const CNA_DepthNormalPrepassHandle prepass, const float roughness)
{
    return CNA_WITH_PREPASS(prepass,
        [&](const std::shared_ptr<DepthNormalPrepassResource>& p) -> CNA_Result {
            // The canonical setter clamps rather than refuses, and this route preserves that: a
            // roughness outside zero-to-one is a value to correct, not a caller error.
            p->value->setRoughness(roughness);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_depth_normal_prepass_is_velocity_enabled_ext(
    const CNA_DepthNormalPrepassHandle prepass, CNA_Bool* const outEnabled)
{
    return CNA_WITH_PREPASS(prepass,
        [&](const std::shared_ptr<DepthNormalPrepassResource>& p) -> CNA_Result {
            return StoreValue(
                outEnabled,
                static_cast<CNA_Bool>(p->value->isVelocityEnabledEXT() ? CNA_TRUE : CNA_FALSE));
        });
}

CNA_Result cna_depth_normal_prepass_set_velocity_enabled_ext(
    const CNA_DepthNormalPrepassHandle prepass, const CNA_Bool enabled)
{
    if (const CNA_Result result = ValidateCanonicalBool(enabled, "enabled");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return CNA_WITH_PREPASS(prepass,
        [&](const std::shared_ptr<DepthNormalPrepassResource>& p) -> CNA_Result {
            p->value->setVelocityEnabledEXT(enabled == CNA_TRUE);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_depth_normal_prepass_set_previous_world_ext(
    const CNA_DepthNormalPrepassHandle prepass, const CNA_Matrix* const previousWorld)
{
    return CNA_WITH_PREPASS(prepass,
        [&](const std::shared_ptr<DepthNormalPrepassResource>& p) -> CNA_Result {
            if (const CNA_Result result =
                    RequireMatrixArgument(previousWorld, "The previous world is null.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            p->value->setPreviousWorldEXT(ToNativeMatrix(*previousWorld));
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_depth_normal_prepass_set_previous_camera_ext(
    const CNA_DepthNormalPrepassHandle prepass,
    const CNA_Matrix* const previousView,
    const CNA_Matrix* const previousProjection)
{
    return CNA_WITH_PREPASS(prepass,
        [&](const std::shared_ptr<DepthNormalPrepassResource>& p) -> CNA_Result {
            if (const CNA_Result result =
                    RequireMatrixArgument(previousView, "The previous view is null.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (const CNA_Result result = RequireMatrixArgument(
                    previousProjection, "The previous projection is null.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            p->value->setPreviousCameraEXT(
                ToNativeMatrix(*previousView), ToNativeMatrix(*previousProjection));
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_depth_normal_prepass_copy_depth_decode_glsl(
    const CNA_Bool packed, char* const destination, const uint64_t capacity,
    uint64_t* const outBytes)
{
    if (const CNA_Result result = ValidateCanonicalBool(packed, "packed");
        result != CNA_RESULT_SUCCESS) {
        if (outBytes != nullptr) {
            *outBytes = UINT64_C(0);
        }
        return result;
    }
    return CopyFormattedString(destination, capacity, outBytes, [packed] {
        return Ext::DepthNormalPrepass::getDepthDecodeGlsl(packed == CNA_TRUE);
    });
}

CNA_Result cna_depth_normal_prepass_copy_velocity_decode_glsl(
    char* const destination, const uint64_t capacity, uint64_t* const outBytes)
{
    return CopyFormattedString(destination, capacity, outBytes, [] {
        return Ext::DepthNormalPrepass::getVelocityDecodeGlsl();
    });
}

CNA_Result cna_depth_normal_prepass_has_velocity_ext(
    const CNA_Color texel, CNA_Bool* const outHas)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        const Microsoft::Xna::Framework::Color native(texel.r, texel.g, texel.b, texel.a);
        return StoreValue(
            outHas,
            static_cast<CNA_Bool>(
                Ext::DepthNormalPrepass::hasVelocityEXT(native) ? CNA_TRUE : CNA_FALSE));
    });
}

CNA_Result cna_depth_normal_prepass_decode_velocity_ext(
    const CNA_Color texel, CNA_Vector2* const outVelocity)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outVelocity == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The velocity output is null.");
        }
        const Microsoft::Xna::Framework::Color native(texel.r, texel.g, texel.b, texel.a);
        const auto velocity = Ext::DepthNormalPrepass::decodeVelocityEXT(native);
        outVelocity->x = velocity.X;
        outVelocity->y = velocity.Y;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_depth_normal_prepass_pack_depth(
    const float value, float* const outR, float* const outG, float* const outB,
    float* const outA)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outR == nullptr || outG == nullptr || outB == nullptr || outA == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "A packed-depth channel output is null.");
        }
        Ext::DepthNormalPrepass::packDepth(value, *outR, *outG, *outB, *outA);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_depth_normal_prepass_unpack_depth(
    const float r, const float g, const float b, const float a, float* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return StoreValue(outValue, Ext::DepthNormalPrepass::unpackDepth(r, g, b, a));
    });
}

CNA_Result cna_depth_normal_prepass_destroy(const CNA_DepthNormalPrepassHandle prepassHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<DepthNormalPrepassResource> prepass;
        if (const CNA_Result result = GetEngineResource(
                prepassHandle, ObjectKind::DepthNormalPrepass, "DepthNormalPrepass", &prepass);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (prepass->activeBorrowCount != 0U) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The prepass is still lending an effect or one of its textures.");
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(prepassHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The owned prepass handle could not be released.");
        }
        RemoveOwnedGraphicsResourceFor(prepass->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_contact_shadow_pass_create(
    const CNA_Handle graphicsDeviceHandle, CNA_PostProcessPassHandle* const outPass)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPass == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The contact-shadow pass output handle is null.");
        }
        *outPass = CNA_INVALID_HANDLE;
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result =
                GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto native = std::make_shared<Ext::ContactShadowPass>(*graphicsDevice->value);
        return CreatePassHandle(
            std::move(native), nullptr, graphicsDevice->parentGame, nullptr, CNA_INVALID_HANDLE,
            outPass);
    });
}

CNA_Result cna_contact_shadow_pass_get_light_direction(
    const CNA_PostProcessPassHandle pass, CNA_Vector3* const outDirection)
{
    return WithContactShadowPass(pass, [&](Ext::ContactShadowPass& contact) -> CNA_Result {
        const auto direction = contact.getLightDirection();
        return StoreValue(outDirection, Vec3(direction.X, direction.Y, direction.Z));
    });
}

CNA_Result cna_contact_shadow_pass_set_light_direction(
    const CNA_PostProcessPassHandle pass, const CNA_Vector3* const direction)
{
    return WithContactShadowPass(pass, [&](Ext::ContactShadowPass& contact) -> CNA_Result {
        if (direction == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, "The direction is null.");
        }
        contact.setLightDirection({direction->x, direction->y, direction->z});
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_contact_shadow_pass_get_max_distance(
    const CNA_PostProcessPassHandle pass, float* const outDistance)
{
    return WithContactShadowPass(pass, [&](Ext::ContactShadowPass& contact) -> CNA_Result {
        return StoreValue(outDistance, contact.getMaxDistance());
    });
}

CNA_Result cna_contact_shadow_pass_set_max_distance(
    const CNA_PostProcessPassHandle pass, const float distance)
{
    return WithContactShadowPass(pass, [&](Ext::ContactShadowPass& contact) -> CNA_Result {
        contact.setMaxDistance(distance);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_contact_shadow_pass_get_step_count(
    const CNA_PostProcessPassHandle pass, int32_t* const outCount)
{
    return WithContactShadowPass(pass, [&](Ext::ContactShadowPass& contact) -> CNA_Result {
        return StoreValue(outCount, static_cast<int32_t>(contact.getStepCount()));
    });
}

CNA_Result cna_contact_shadow_pass_set_step_count(
    const CNA_PostProcessPassHandle pass, const int32_t count)
{
    return WithContactShadowPass(pass, [&](Ext::ContactShadowPass& contact) -> CNA_Result {
        contact.setStepCount(static_cast<int>(count));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_contact_shadow_pass_get_thickness(
    const CNA_PostProcessPassHandle pass, float* const outThickness)
{
    return WithContactShadowPass(pass, [&](Ext::ContactShadowPass& contact) -> CNA_Result {
        return StoreValue(outThickness, contact.getThickness());
    });
}

CNA_Result cna_contact_shadow_pass_set_thickness(
    const CNA_PostProcessPassHandle pass, const float thickness)
{
    return WithContactShadowPass(pass, [&](Ext::ContactShadowPass& contact) -> CNA_Result {
        contact.setThickness(thickness);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_contact_shadow_pass_get_intensity(
    const CNA_PostProcessPassHandle pass, float* const outIntensity)
{
    return WithContactShadowPass(pass, [&](Ext::ContactShadowPass& contact) -> CNA_Result {
        return StoreValue(outIntensity, contact.getIntensity());
    });
}

CNA_Result cna_contact_shadow_pass_set_intensity(
    const CNA_PostProcessPassHandle pass, const float intensity)
{
    return WithContactShadowPass(pass, [&](Ext::ContactShadowPass& contact) -> CNA_Result {
        contact.setIntensity(intensity);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_contact_shadow_pass_get_bias(
    const CNA_PostProcessPassHandle pass, float* const outBias)
{
    return WithContactShadowPass(pass, [&](Ext::ContactShadowPass& contact) -> CNA_Result {
        return StoreValue(outBias, contact.getBias());
    });
}

CNA_Result cna_contact_shadow_pass_set_bias(
    const CNA_PostProcessPassHandle pass, const float bias)
{
    return WithContactShadowPass(pass, [&](Ext::ContactShadowPass& contact) -> CNA_Result {
        contact.setBias(bias);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_contact_shadow_pass_copy_fallback_reason(
    const CNA_PostProcessPassHandle pass, char* const destination, const uint64_t capacity,
    uint64_t* const outBytes)
{
    std::shared_ptr<PostProcessPassResource> resource;
    if (const CNA_Result result = GetPostProcessPass(pass, &resource);
        result != CNA_RESULT_SUCCESS) {
        if (outBytes != nullptr) {
            *outBytes = UINT64_C(0);
        }
        return result;
    }
    auto* const contact = dynamic_cast<Ext::ContactShadowPass*>(resource->value.get());
    if (contact == nullptr) {
        if (outBytes != nullptr) {
            *outBytes = UINT64_C(0);
        }
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "This post-process pass is not a contact-shadow pass.");
    }
    return CopyFormattedString(destination, capacity, outBytes, [contact] {
        return contact->getFallbackReason();
    });
}

CNA_Result cna_contact_shadow_pass_is_occluded(
    const float rayViewDepth, const float sceneViewDepth, const float bias, const float thickness,
    CNA_Bool* const outOccluded)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return StoreValue(
            outOccluded,
            static_cast<CNA_Bool>(
                Ext::ContactShadowPass::isOccluded(rayViewDepth, sceneViewDepth, bias, thickness)
                    ? CNA_TRUE
                    : CNA_FALSE));
    });
}

CNA_Result cna_contact_shadow_pass_copy_occlusion_test_glsl(
    char* const destination, const uint64_t capacity, uint64_t* const outBytes)
{
    return CopyFormattedString(destination, capacity, outBytes, [] {
        return Ext::ContactShadowPass::getOcclusionTestGlsl();
    });
}

CNA_Result cna_contact_shadow_pass_combine_visibility(
    const float shadowMapVisibility, const float contactVisibility, float* const outVisibility)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return StoreValue(
            outVisibility,
            Ext::ContactShadowPass::combineVisibility(shadowMapVisibility, contactVisibility));
    });
}

namespace {

using Microsoft::Xna::Framework::BoundingSphere;

struct ClusteredLightSetResource final {
    std::shared_ptr<Ext::ClusteredLightSetEXT> value;
    CNA_Handle parentGame;
};

static_assert(
    static_cast<uint32_t>(Ext::ClusteredLightType::Point) == CNA_CLUSTERED_LIGHT_TYPE_POINT &&
    static_cast<uint32_t>(Ext::ClusteredLightType::Spot) == CNA_CLUSTERED_LIGHT_TYPE_SPOT);
static_assert(
    Ext::ClusteredLightSetEXT::kMaxLights == CNA_CLUSTERED_LIGHT_SET_MAX_EXT,
    "the C maximum must equal the canonical bound the shader's index width is sized from");

[[nodiscard]] CNA_Result ToNativeClusteredLight(
    const CNA_ClusteredLightEXT* const value, Ext::ClusteredLightEXT* const out)
{
    if (value == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, "The light is null.");
    }
    if (value->struct_size != static_cast<uint32_t>(sizeof(CNA_ClusteredLightEXT)) ||
        value->struct_version != UINT32_C(1)) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The light was not initialized by cna_clustered_light_ext_init.");
    }
    if (value->type > CNA_CLUSTERED_LIGHT_TYPE_SPOT) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The light type is not a defined CNA_CLUSTERED_LIGHT_TYPE_* value.");
    }
    if (const CNA_Result result = ValidateCanonicalBool(value->casts_shadows, "casts_shadows");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    out->Type = static_cast<Ext::ClusteredLightType>(value->type);
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

void FromNativeClusteredLight(
    const Ext::ClusteredLightEXT& value, CNA_ClusteredLightEXT* const out)
{
    std::memset(out, 0, sizeof(*out));
    out->struct_size = static_cast<uint32_t>(sizeof(CNA_ClusteredLightEXT));
    out->struct_version = UINT32_C(1);
    out->type = static_cast<CNA_ClusteredLightType>(value.Type);
    out->casts_shadows = value.CastsShadows ? CNA_TRUE : CNA_FALSE;
    out->position = Vec3(value.Position.X, value.Position.Y, value.Position.Z);
    out->direction = Vec3(value.Direction.X, value.Direction.Y, value.Direction.Z);
    out->color = Vec3(value.Color.X, value.Color.Y, value.Color.Z);
    out->intensity = value.Intensity;
    out->range = value.Range;
    out->inner_angle = value.InnerAngle;
    out->outer_angle = value.OuterAngle;
}

[[nodiscard]] CNA_BoundingSphere ToCBoundingSphere(const BoundingSphere& value) noexcept
{
    CNA_BoundingSphere result;
    result.center = Vec3(value.Center.X, value.Center.Y, value.Center.Z);
    result.radius = value.Radius;
    return result;
}

// The canonical set refuses two different ways -- a full set is std::length_error, an unusable
// light std::invalid_argument -- and the firewall would flatten both to one result. They are
// separated here because a caller can act on them differently: a full set means drop a light, an
// unusable one means fix the light it just built.
[[nodiscard]] CNA_Result RequireRoomAndUsable(
    const Ext::ClusteredLightSetEXT& set, const Ext::ClusteredLightEXT& light)
{
    if (set.getCount() >= Ext::ClusteredLightSetEXT::kMaxLights) {
        return Fail(
            CNA_RESULT_INVALID_STATE,
            CNA_ERROR_CATEGORY_STATE,
            "The clustered light set already holds its maximum of 256 lights; the uploaded buffer "
            "and the shader's index width are sized from that bound, so it is refused rather than "
            "grown.");
    }
    if (!Ext::ClusteredLightSetEXT::isUsable(light)) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The light is not usable: a range must be positive, an intensity finite and "
            "non-negative, every vector finite, and a spot's direction non-degenerate with its "
            "inner angle no wider than its outer.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result RequireLightIndex(
    const Ext::ClusteredLightSetEXT& set, const int32_t index)
{
    if (index < 0 || index >= static_cast<int32_t>(set.getCount())) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_RANGE,
            "No light in the set has that index.");
    }
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_clustered_light_set_is_usable(
    const CNA_ClusteredLightEXT* const light, CNA_Bool* const outUsable)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Ext::ClusteredLightEXT native;
        if (const CNA_Result result = ToNativeClusteredLight(light, &native);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return StoreValue(
            outUsable,
            static_cast<CNA_Bool>(
                Ext::ClusteredLightSetEXT::isUsable(native) ? CNA_TRUE : CNA_FALSE));
    });
}

CNA_Result cna_clustered_light_set_create(
    const CNA_Handle gameHandle, CNA_ClusteredLightSetHandle* const outSet)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSet == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The light-set output handle is null.");
        }
        *outSet = CNA_INVALID_HANDLE;
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(gameHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto native = std::make_shared<Ext::ClusteredLightSetEXT>();
        const auto resource = std::make_shared<ClusteredLightSetResource>(
            ClusteredLightSetResource{std::move(native), graphicsDevice->parentGame});
        const CNA_Result result =
            GetRuntimeHandles().Create(ObjectKind::ClusteredLightSet, resource, outSet);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned light-set handle could not be created.");
        }
        AddOwnedGraphicsResourceFor(graphicsDevice->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

#define CNA_WITH_LIGHT_SET(handle, body)                                                           \
    WithMap<ClusteredLightSetResource>(                                                            \
        (handle), ObjectKind::ClusteredLightSet, "ClusteredLightSetEXT", body)

CNA_Result cna_clustered_light_set_add(
    const CNA_ClusteredLightSetHandle set,
    const CNA_ClusteredLightEXT* const light,
    int32_t* const outIndex)
{
    return CNA_WITH_LIGHT_SET(set,
        [&](const std::shared_ptr<ClusteredLightSetResource>& s) -> CNA_Result {
            if (outIndex == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The index output is null.");
            }
            Ext::ClusteredLightEXT native;
            if (const CNA_Result result = ToNativeClusteredLight(light, &native);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (const CNA_Result result = RequireRoomAndUsable(*s->value, native);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            *outIndex = static_cast<int32_t>(s->value->add(native));
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_clustered_light_set_add_point(
    const CNA_ClusteredLightSetHandle set,
    const CNA_PointLightEXT* const light,
    int32_t* const outIndex)
{
    return CNA_WITH_LIGHT_SET(set,
        [&](const std::shared_ptr<ClusteredLightSetResource>& s) -> CNA_Result {
            if (outIndex == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The index output is null.");
            }
            Ext::PointLightEXT native;
            if (const CNA_Result result = ToNativePointLight(light, &native);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            // The canonical overload converts and then applies the same two refusals, so the C
            // form checks the converted light rather than duplicating the conversion rules.
            Ext::ClusteredLightEXT converted;
            converted.Type = Ext::ClusteredLightType::Point;
            converted.Position = native.Position;
            converted.Color = native.Color;
            converted.Intensity = native.Intensity;
            converted.Range = native.Range;
            converted.CastsShadows = native.CastsShadows;
            if (const CNA_Result result = RequireRoomAndUsable(*s->value, converted);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            *outIndex = static_cast<int32_t>(s->value->add(native));
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_clustered_light_set_add_spot(
    const CNA_ClusteredLightSetHandle set,
    const CNA_SpotLightEXT* const light,
    int32_t* const outIndex)
{
    return CNA_WITH_LIGHT_SET(set,
        [&](const std::shared_ptr<ClusteredLightSetResource>& s) -> CNA_Result {
            if (outIndex == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The index output is null.");
            }
            Ext::SpotLightEXT native;
            if (const CNA_Result result = ToNativeSpotLight(light, &native);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            Ext::ClusteredLightEXT converted;
            converted.Type = Ext::ClusteredLightType::Spot;
            converted.Position = native.Position;
            converted.Direction = native.Direction;
            converted.Color = native.Color;
            converted.Intensity = native.Intensity;
            converted.Range = native.Range;
            converted.InnerAngle = native.InnerAngle;
            converted.OuterAngle = native.OuterAngle;
            converted.CastsShadows = native.CastsShadows;
            if (const CNA_Result result = RequireRoomAndUsable(*s->value, converted);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            *outIndex = static_cast<int32_t>(s->value->add(native));
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_clustered_light_set_replace_at(
    const CNA_ClusteredLightSetHandle set,
    const int32_t index,
    const CNA_ClusteredLightEXT* const light)
{
    return CNA_WITH_LIGHT_SET(set,
        [&](const std::shared_ptr<ClusteredLightSetResource>& s) -> CNA_Result {
            Ext::ClusteredLightEXT native;
            if (const CNA_Result result = ToNativeClusteredLight(light, &native);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (const CNA_Result result = RequireLightIndex(*s->value, index);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (!Ext::ClusteredLightSetEXT::isUsable(native)) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The replacement light is not usable.");
            }
            s->value->replaceAt(static_cast<int>(index), native);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_clustered_light_set_remove_at(
    const CNA_ClusteredLightSetHandle set, const int32_t index)
{
    return CNA_WITH_LIGHT_SET(set,
        [&](const std::shared_ptr<ClusteredLightSetResource>& s) -> CNA_Result {
            if (const CNA_Result result = RequireLightIndex(*s->value, index);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            s->value->removeAt(static_cast<int>(index));
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_clustered_light_set_clear(const CNA_ClusteredLightSetHandle set)
{
    return CNA_WITH_LIGHT_SET(set,
        [](const std::shared_ptr<ClusteredLightSetResource>& s) -> CNA_Result {
            s->value->clear();
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_clustered_light_set_get_count(
    const CNA_ClusteredLightSetHandle set, int32_t* const outCount)
{
    return CNA_WITH_LIGHT_SET(set,
        [&](const std::shared_ptr<ClusteredLightSetResource>& s) -> CNA_Result {
            return StoreValue(outCount, static_cast<int32_t>(s->value->getCount()));
        });
}

CNA_Result cna_clustered_light_set_is_empty(
    const CNA_ClusteredLightSetHandle set, CNA_Bool* const outEmpty)
{
    return CNA_WITH_LIGHT_SET(set,
        [&](const std::shared_ptr<ClusteredLightSetResource>& s) -> CNA_Result {
            return StoreValue(
                outEmpty, static_cast<CNA_Bool>(s->value->isEmpty() ? CNA_TRUE : CNA_FALSE));
        });
}

CNA_Result cna_clustered_light_set_get_at(
    const CNA_ClusteredLightSetHandle set,
    const int32_t index,
    CNA_ClusteredLightEXT* const outLight)
{
    return CNA_WITH_LIGHT_SET(set,
        [&](const std::shared_ptr<ClusteredLightSetResource>& s) -> CNA_Result {
            if (outLight == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The light output is null.");
            }
            if (const CNA_Result result = RequireLightIndex(*s->value, index);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            FromNativeClusteredLight(s->value->getAt(static_cast<int>(index)), outLight);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_clustered_light_set_copy_lights(
    const CNA_ClusteredLightSetHandle set,
    CNA_ClusteredLightEXT* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CNA_WITH_LIGHT_SET(set,
        [&](const std::shared_ptr<ClusteredLightSetResource>& s) -> CNA_Result {
            if (outCount == nullptr || (destination == nullptr && capacity != 0U)) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The light destination or required-count output is invalid.");
            }
            const std::vector<Ext::ClusteredLightEXT>& lights = s->value->getLights();
            *outCount = static_cast<uint64_t>(lights.size());
            if (capacity < lights.size()) {
                return Fail(
                    CNA_RESULT_BUFFER_TOO_SMALL,
                    CNA_ERROR_CATEGORY_RANGE,
                    "The destination cannot hold every light.");
            }
            for (std::size_t light = 0; light < lights.size(); ++light) {
                FromNativeClusteredLight(lights[light], &destination[light]);
            }
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_clustered_light_set_get_bounds_at(
    const CNA_ClusteredLightSetHandle set,
    const int32_t index,
    CNA_BoundingSphere* const outBounds)
{
    return CNA_WITH_LIGHT_SET(set,
        [&](const std::shared_ptr<ClusteredLightSetResource>& s) -> CNA_Result {
            if (const CNA_Result result = RequireLightIndex(*s->value, index);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            return StoreValue(
                outBounds, ToCBoundingSphere(s->value->getBoundsAt(static_cast<int>(index))));
        });
}

CNA_Result cna_clustered_light_set_copy_bounds(
    const CNA_ClusteredLightSetHandle set,
    CNA_BoundingSphere* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CNA_WITH_LIGHT_SET(set,
        [&](const std::shared_ptr<ClusteredLightSetResource>& s) -> CNA_Result {
            if (outCount == nullptr || (destination == nullptr && capacity != 0U)) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The bounds destination or required-count output is invalid.");
            }
            const std::vector<BoundingSphere> bounds = s->value->collectBounds();
            *outCount = static_cast<uint64_t>(bounds.size());
            if (capacity < bounds.size()) {
                return Fail(
                    CNA_RESULT_BUFFER_TOO_SMALL,
                    CNA_ERROR_CATEGORY_RANGE,
                    "The destination cannot hold every bounding sphere.");
            }
            for (std::size_t sphere = 0; sphere < bounds.size(); ++sphere) {
                destination[sphere] = ToCBoundingSphere(bounds[sphere]);
            }
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_clustered_light_set_destroy(const CNA_ClusteredLightSetHandle setHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ClusteredLightSetResource> set;
        if (const CNA_Result result = GetEngineResource(
                setHandle, ObjectKind::ClusteredLightSet, "ClusteredLightSetEXT", &set);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // No borrow count: the set holds values, so nothing it returned is still pointing at it.
        const CNA_Result releaseResult = GetRuntimeHandles().Release(setHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The owned light-set handle could not be released.");
        }
        RemoveOwnedGraphicsResourceFor(set->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

namespace {

struct ClusteredLightGridResource final {
    std::shared_ptr<Ext::ClusteredLightGrid> value;
    CNA_Handle parentGame;
};

struct ClusteredLightAssignmentResource final {
    std::shared_ptr<Ext::ClusteredLightAssignment> value;
    CNA_Handle parentGame;
};

struct ClusteredLightBufferResource final {
    std::shared_ptr<Ext::ClusteredLightBuffer> value;
    CNA_Handle parentGame;
};

static_assert(
    Ext::ClusteredLightGrid::kMaxTilesPerAxis == CNA_CLUSTER_GRID_MAX_TILES_PER_AXIS_EXT &&
    Ext::ClusteredLightGrid::kMaxSliceCount == CNA_CLUSTER_GRID_MAX_SLICE_COUNT_EXT &&
    Ext::ClusteredLightGrid::kDefaultTilesX == CNA_CLUSTER_GRID_DEFAULT_TILES_X_EXT &&
    Ext::ClusteredLightGrid::kDefaultTilesY == CNA_CLUSTER_GRID_DEFAULT_TILES_Y_EXT &&
    Ext::ClusteredLightGrid::kDefaultSliceCount == CNA_CLUSTER_GRID_DEFAULT_SLICE_COUNT_EXT);
static_assert(
    Ext::ClusteredLightAssignment::kMaxLights == CNA_CLUSTERED_ASSIGNMENT_MAX_LIGHTS_EXT);

[[nodiscard]] CNA_Result RequireClusterCoordinate(
    const Ext::ClusteredLightGrid& grid, const int32_t x, const int32_t y, const int32_t slice)
{
    if (x < 0 || x >= grid.getTilesX() || y < 0 || y >= grid.getTilesY() ||
        slice < 0 || slice >= grid.getSliceCount()) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_RANGE,
            "The cluster coordinate is outside the grid.");
    }
    return CNA_RESULT_SUCCESS;
}

// A count/copy body shared by the four int32 array read-backs on the assignment.
template<typename TSource>
[[nodiscard]] CNA_Result CopyInt32Range(
    const TSource& source, int32_t* const destination, const uint64_t capacity,
    uint64_t* const outCount)
{
    if (outCount == nullptr || (destination == nullptr && capacity != 0U)) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The destination or required-count output is invalid.");
    }
    const auto count = static_cast<uint64_t>(source.size());
    *outCount = count;
    if (capacity < count) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination cannot hold every element.");
    }
    uint64_t written = 0U;
    for (const int value : source) {
        destination[written++] = static_cast<int32_t>(value);
    }
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_clustered_light_grid_create(
    const CNA_Handle gameHandle,
    const int32_t tilesX,
    const int32_t tilesY,
    const int32_t sliceCount,
    CNA_ClusteredLightGridHandle* const outGrid)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outGrid == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The grid output handle is null.");
        }
        *outGrid = CNA_INVALID_HANDLE;
        // Refused rather than clamped: the cluster count is what the light-index list is sized
        // from, so a grid quietly smaller than asked for would size a list the caller did not mean.
        if (tilesX < 1 || tilesX > CNA_CLUSTER_GRID_MAX_TILES_PER_AXIS_EXT ||
            tilesY < 1 || tilesY > CNA_CLUSTER_GRID_MAX_TILES_PER_AXIS_EXT ||
            sliceCount < 1 || sliceCount > CNA_CLUSTER_GRID_MAX_SLICE_COUNT_EXT) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "A grid dimension is outside its range: the screen axes take 1 to 128 tiles and "
                "the depth axis 1 to 256 slices.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(gameHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto native = std::make_shared<Ext::ClusteredLightGrid>(
            static_cast<int>(tilesX), static_cast<int>(tilesY), static_cast<int>(sliceCount));
        const auto resource = std::make_shared<ClusteredLightGridResource>(
            ClusteredLightGridResource{std::move(native), graphicsDevice->parentGame});
        const CNA_Result result =
            GetRuntimeHandles().Create(ObjectKind::ClusteredLightGrid, resource, outGrid);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned cluster-grid handle could not be created.");
        }
        AddOwnedGraphicsResourceFor(graphicsDevice->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

#define CNA_WITH_GRID(handle, body)                                                                \
    WithMap<ClusteredLightGridResource>(                                                           \
        (handle), ObjectKind::ClusteredLightGrid, "ClusteredLightGrid", body)
#define CNA_WITH_ASSIGNMENT(handle, body)                                                          \
    WithMap<ClusteredLightAssignmentResource>(                                                     \
        (handle), ObjectKind::ClusteredLightAssignment, "ClusteredLightAssignment", body)
#define CNA_WITH_LIGHT_BUFFER(handle, body)                                                        \
    WithMap<ClusteredLightBufferResource>(                                                         \
        (handle), ObjectKind::ClusteredLightBuffer, "ClusteredLightBuffer", body)

CNA_Result cna_clustered_light_grid_get_tiles_x(
    const CNA_ClusteredLightGridHandle grid, int32_t* const outTiles)
{
    return CNA_WITH_GRID(grid, [&](const std::shared_ptr<ClusteredLightGridResource>& g)
        -> CNA_Result { return StoreValue(outTiles, static_cast<int32_t>(g->value->getTilesX())); });
}

CNA_Result cna_clustered_light_grid_get_tiles_y(
    const CNA_ClusteredLightGridHandle grid, int32_t* const outTiles)
{
    return CNA_WITH_GRID(grid, [&](const std::shared_ptr<ClusteredLightGridResource>& g)
        -> CNA_Result { return StoreValue(outTiles, static_cast<int32_t>(g->value->getTilesY())); });
}

CNA_Result cna_clustered_light_grid_get_slice_count(
    const CNA_ClusteredLightGridHandle grid, int32_t* const outSlices)
{
    return CNA_WITH_GRID(grid, [&](const std::shared_ptr<ClusteredLightGridResource>& g)
        -> CNA_Result {
            return StoreValue(outSlices, static_cast<int32_t>(g->value->getSliceCount()));
        });
}

CNA_Result cna_clustered_light_grid_get_cluster_count(
    const CNA_ClusteredLightGridHandle grid, int32_t* const outCount)
{
    return CNA_WITH_GRID(grid, [&](const std::shared_ptr<ClusteredLightGridResource>& g)
        -> CNA_Result {
            return StoreValue(outCount, static_cast<int32_t>(g->value->getClusterCount()));
        });
}

CNA_Result cna_clustered_light_grid_cluster_index(
    const CNA_ClusteredLightGridHandle grid,
    const int32_t x, const int32_t y, const int32_t slice, int32_t* const outIndex)
{
    return CNA_WITH_GRID(grid, [&](const std::shared_ptr<ClusteredLightGridResource>& g)
        -> CNA_Result {
            if (const CNA_Result result = RequireClusterCoordinate(*g->value, x, y, slice);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            return StoreValue(
                outIndex,
                static_cast<int32_t>(g->value->clusterIndex(
                    static_cast<int>(x), static_cast<int>(y), static_cast<int>(slice))));
        });
}

CNA_Result cna_clustered_light_grid_set_projection(
    const CNA_ClusteredLightGridHandle grid,
    const CNA_Matrix* const projection, const float nearPlane, const float farPlane)
{
    return CNA_WITH_GRID(grid, [&](const std::shared_ptr<ClusteredLightGridResource>& g)
        -> CNA_Result {
            if (const CNA_Result result =
                    RequireMatrixArgument(projection, "The projection is null.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (!(nearPlane > 0.0F) || !(farPlane > nearPlane)) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_RANGE,
                    "The near distance must be positive and the far distance must exceed it: the "
                    "slice spacing is a ratio of the two.");
            }
            g->value->setProjection(ToNativeMatrix(*projection), nearPlane, farPlane);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_clustered_light_grid_has_projection(
    const CNA_ClusteredLightGridHandle grid, CNA_Bool* const outHas)
{
    return CNA_WITH_GRID(grid, [&](const std::shared_ptr<ClusteredLightGridResource>& g)
        -> CNA_Result {
            return StoreValue(
                outHas, static_cast<CNA_Bool>(g->value->hasProjection() ? CNA_TRUE : CNA_FALSE));
        });
}

CNA_Result cna_clustered_light_grid_get_near_plane(
    const CNA_ClusteredLightGridHandle grid, float* const outNear)
{
    return CNA_WITH_GRID(grid, [&](const std::shared_ptr<ClusteredLightGridResource>& g)
        -> CNA_Result { return StoreValue(outNear, g->value->getNearPlane()); });
}

CNA_Result cna_clustered_light_grid_get_far_plane(
    const CNA_ClusteredLightGridHandle grid, float* const outFar)
{
    return CNA_WITH_GRID(grid, [&](const std::shared_ptr<ClusteredLightGridResource>& g)
        -> CNA_Result { return StoreValue(outFar, g->value->getFarPlane()); });
}

CNA_Result cna_clustered_light_grid_get_inverse_projection(
    const CNA_ClusteredLightGridHandle grid, CNA_Matrix* const outMatrix)
{
    return CNA_WITH_GRID(grid, [&](const std::shared_ptr<ClusteredLightGridResource>& g)
        -> CNA_Result { return StoreValue(outMatrix, ToCMatrix(g->value->getInverseProjection())); });
}

CNA_Result cna_clustered_light_grid_slice_distance(
    const CNA_ClusteredLightGridHandle grid, const int32_t slice, float* const outDistance)
{
    return CNA_WITH_GRID(grid, [&](const std::shared_ptr<ClusteredLightGridResource>& g)
        -> CNA_Result {
            // Inclusive of the slice count: there is one more boundary than slice, and the last
            // names the far edge. A `>=` here would lose the far plane.
            if (slice < 0 || slice > static_cast<int32_t>(g->value->getSliceCount())) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_RANGE,
                    "The slice boundary is outside the grid.");
            }
            return StoreValue(outDistance, g->value->sliceDistance(static_cast<int>(slice)));
        });
}

CNA_Result cna_clustered_light_grid_slice_for_view_distance(
    const CNA_ClusteredLightGridHandle grid, const float viewDistance, int32_t* const outSlice)
{
    return CNA_WITH_GRID(grid, [&](const std::shared_ptr<ClusteredLightGridResource>& g)
        -> CNA_Result {
            // Clamped, not refused: a point outside the frustum belongs to the nearest slice,
            // which is what a renderer wants when a light straddles the edge.
            return StoreValue(
                outSlice, static_cast<int32_t>(g->value->sliceForViewDistance(viewDistance)));
        });
}

CNA_Result cna_clustered_light_grid_cluster_bounds(
    const CNA_ClusteredLightGridHandle grid,
    const int32_t x, const int32_t y, const int32_t slice, CNA_BoundingBox* const outBounds)
{
    return CNA_WITH_GRID(grid, [&](const std::shared_ptr<ClusteredLightGridResource>& g)
        -> CNA_Result {
            if (const CNA_Result result = RequireClusterCoordinate(*g->value, x, y, slice);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (!g->value->hasProjection()) {
                return Fail(
                    CNA_RESULT_INVALID_STATE,
                    CNA_ERROR_CATEGORY_STATE,
                    "The grid has no projection, so it has no shape yet.");
            }
            if (outBounds == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The bounds output is null.");
            }
            const auto bounds = g->value->clusterBounds(
                static_cast<int>(x), static_cast<int>(y), static_cast<int>(slice));
            outBounds->min = Vec3(bounds.Min.X, bounds.Min.Y, bounds.Min.Z);
            outBounds->max = Vec3(bounds.Max.X, bounds.Max.Y, bounds.Max.Z);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_clustered_light_grid_destroy(const CNA_ClusteredLightGridHandle gridHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ClusteredLightGridResource> grid;
        if (const CNA_Result result = GetEngineResource(
                gridHandle, ObjectKind::ClusteredLightGrid, "ClusteredLightGrid", &grid);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(gridHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The owned cluster-grid handle could not be released.");
        }
        RemoveOwnedGraphicsResourceFor(grid->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_clustered_light_assignment_create(
    const CNA_Handle gameHandle, CNA_ClusteredLightAssignmentHandle* const outAssignment)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outAssignment == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The assignment output handle is null.");
        }
        *outAssignment = CNA_INVALID_HANDLE;
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(gameHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto native = std::make_shared<Ext::ClusteredLightAssignment>();
        const auto resource = std::make_shared<ClusteredLightAssignmentResource>(
            ClusteredLightAssignmentResource{std::move(native), graphicsDevice->parentGame});
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::ClusteredLightAssignment, resource, outAssignment);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned assignment handle could not be created.");
        }
        AddOwnedGraphicsResourceFor(graphicsDevice->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_clustered_light_assignment_assign(
    const CNA_ClusteredLightAssignmentHandle assignment,
    const CNA_ClusteredLightGridHandle gridHandle,
    const CNA_Matrix* const view,
    const CNA_BoundingSphere* const bounds,
    const uint64_t boundsCount)
{
    return CNA_WITH_ASSIGNMENT(assignment,
        [&](const std::shared_ptr<ClusteredLightAssignmentResource>& a) -> CNA_Result {
            if (bounds == nullptr && boundsCount != 0U) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The bounds array is null.");
            }
            if (const CNA_Result result = RequireMatrixArgument(view, "The view is null.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (boundsCount > static_cast<uint64_t>(CNA_CLUSTERED_ASSIGNMENT_MAX_LIGHTS_EXT)) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_RANGE,
                    "More lights than the assignment accepts: the index list is sized from that "
                    "bound, and a scene needing more wants a second grid.");
            }
            std::shared_ptr<ClusteredLightGridResource> grid;
            if (const CNA_Result result = GetEngineResource(
                    gridHandle, ObjectKind::ClusteredLightGrid, "ClusteredLightGrid", &grid);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (!grid->value->hasProjection()) {
                return Fail(
                    CNA_RESULT_INVALID_STATE,
                    CNA_ERROR_CATEGORY_STATE,
                    "The grid has no projection, so it has no clusters to sort into yet.");
            }
            std::vector<BoundingSphere> nativeBounds;
            nativeBounds.reserve(static_cast<std::size_t>(boundsCount));
            for (uint64_t sphere = 0U; sphere < boundsCount; ++sphere) {
                nativeBounds.emplace_back(
                    Microsoft::Xna::Framework::Vector3(
                        bounds[sphere].center.x, bounds[sphere].center.y, bounds[sphere].center.z),
                    bounds[sphere].radius);
            }
            a->value->assign(*grid->value, ToNativeMatrix(*view), nativeBounds);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_clustered_light_assignment_clear(
    const CNA_ClusteredLightAssignmentHandle assignment)
{
    return CNA_WITH_ASSIGNMENT(assignment,
        [](const std::shared_ptr<ClusteredLightAssignmentResource>& a) -> CNA_Result {
            a->value->clear();
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_clustered_light_assignment_adopt(
    const CNA_ClusteredLightAssignmentHandle assignment,
    const int32_t lightCount,
    const int32_t* const offsets,
    const uint64_t offsetCount,
    const int32_t* const indices,
    const uint64_t indexCount)
{
    return CNA_WITH_ASSIGNMENT(assignment,
        [&](const std::shared_ptr<ClusteredLightAssignmentResource>& a) -> CNA_Result {
            if ((offsets == nullptr && offsetCount != 0U) ||
                (indices == nullptr && indexCount != 0U)) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "An adopted array is null.");
            }
            // The canonical checks, made here so each answers with its own message rather than
            // arriving as one flattened invalid_argument from the firewall.
            if (lightCount < 0 || offsetCount == 0U || offsets[0] != INT32_C(0)) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The offsets must begin at zero and describe at least one cluster.");
            }
            if (static_cast<uint64_t>(offsets[offsetCount - 1U]) != indexCount) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The last offset must be the length of the index array.");
            }
            for (uint64_t offset = 1U; offset < offsetCount; ++offset) {
                if (offsets[offset] < offsets[offset - 1U]) {
                    return Fail(
                        CNA_RESULT_INVALID_ARGUMENT,
                        CNA_ERROR_CATEGORY_ARGUMENT,
                        "The offsets go backwards.");
                }
            }
            for (uint64_t index = 0U; index < indexCount; ++index) {
                if (indices[index] < 0 || indices[index] >= lightCount) {
                    return Fail(
                        CNA_RESULT_INVALID_ARGUMENT,
                        CNA_ERROR_CATEGORY_ARGUMENT,
                        "An index names a light that is not in the set.");
                }
            }
            std::vector<int> nativeOffsets;
            nativeOffsets.reserve(static_cast<std::size_t>(offsetCount));
            for (uint64_t offset = 0U; offset < offsetCount; ++offset) {
                nativeOffsets.push_back(static_cast<int>(offsets[offset]));
            }
            std::vector<int> nativeIndices;
            nativeIndices.reserve(static_cast<std::size_t>(indexCount));
            for (uint64_t index = 0U; index < indexCount; ++index) {
                nativeIndices.push_back(static_cast<int>(indices[index]));
            }
            a->value->adopt(
                static_cast<int>(lightCount), std::move(nativeOffsets), std::move(nativeIndices));
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_clustered_light_assignment_get_light_count(
    const CNA_ClusteredLightAssignmentHandle assignment, int32_t* const outCount)
{
    return CNA_WITH_ASSIGNMENT(assignment,
        [&](const std::shared_ptr<ClusteredLightAssignmentResource>& a) -> CNA_Result {
            return StoreValue(outCount, static_cast<int32_t>(a->value->getLightCount()));
        });
}

CNA_Result cna_clustered_light_assignment_get_cluster_count(
    const CNA_ClusteredLightAssignmentHandle assignment, int32_t* const outCount)
{
    return CNA_WITH_ASSIGNMENT(assignment,
        [&](const std::shared_ptr<ClusteredLightAssignmentResource>& a) -> CNA_Result {
            return StoreValue(outCount, static_cast<int32_t>(a->value->getClusterCount()));
        });
}

CNA_Result cna_clustered_light_assignment_copy_lights_in_cluster(
    const CNA_ClusteredLightAssignmentHandle assignment,
    const int32_t clusterIndex,
    int32_t* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CNA_WITH_ASSIGNMENT(assignment,
        [&](const std::shared_ptr<ClusteredLightAssignmentResource>& a) -> CNA_Result {
            if (clusterIndex < 0 ||
                clusterIndex >= static_cast<int32_t>(a->value->getClusterCount())) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_RANGE,
                    "The cluster index is outside the assigned range.");
            }
            const auto span = a->value->lightsInCluster(static_cast<int>(clusterIndex));
            return CopyInt32Range(span, destination, capacity, outCount);
        });
}

CNA_Result cna_clustered_light_assignment_copy_indices(
    const CNA_ClusteredLightAssignmentHandle assignment,
    int32_t* const destination, const uint64_t capacity, uint64_t* const outCount)
{
    return CNA_WITH_ASSIGNMENT(assignment,
        [&](const std::shared_ptr<ClusteredLightAssignmentResource>& a) -> CNA_Result {
            return CopyInt32Range(a->value->getIndices(), destination, capacity, outCount);
        });
}

CNA_Result cna_clustered_light_assignment_copy_offsets(
    const CNA_ClusteredLightAssignmentHandle assignment,
    int32_t* const destination, const uint64_t capacity, uint64_t* const outCount)
{
    return CNA_WITH_ASSIGNMENT(assignment,
        [&](const std::shared_ptr<ClusteredLightAssignmentResource>& a) -> CNA_Result {
            return CopyInt32Range(a->value->getOffsets(), destination, capacity, outCount);
        });
}

CNA_Result cna_clustered_light_assignment_get_total_reference_count(
    const CNA_ClusteredLightAssignmentHandle assignment, int32_t* const outCount)
{
    return CNA_WITH_ASSIGNMENT(assignment,
        [&](const std::shared_ptr<ClusteredLightAssignmentResource>& a) -> CNA_Result {
            return StoreValue(outCount, static_cast<int32_t>(a->value->getTotalReferenceCount()));
        });
}

CNA_Result cna_clustered_light_assignment_get_max_lights_per_cluster(
    const CNA_ClusteredLightAssignmentHandle assignment, int32_t* const outCount)
{
    return CNA_WITH_ASSIGNMENT(assignment,
        [&](const std::shared_ptr<ClusteredLightAssignmentResource>& a) -> CNA_Result {
            return StoreValue(outCount, static_cast<int32_t>(a->value->getMaxLightsPerCluster()));
        });
}

CNA_Result cna_clustered_light_assignment_destroy(
    const CNA_ClusteredLightAssignmentHandle assignmentHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ClusteredLightAssignmentResource> assignment;
        if (const CNA_Result result = GetEngineResource(
                assignmentHandle, ObjectKind::ClusteredLightAssignment, "ClusteredLightAssignment",
                &assignment);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(assignmentHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The owned assignment handle could not be released.");
        }
        RemoveOwnedGraphicsResourceFor(assignment->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_clustered_light_buffer_create(
    const CNA_Handle graphicsDeviceHandle, CNA_ClusteredLightBufferHandle* const outBuffer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBuffer == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The light-buffer output handle is null.");
        }
        *outBuffer = CNA_INVALID_HANDLE;
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result =
                GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto native = std::make_shared<Ext::ClusteredLightBuffer>(*graphicsDevice->value);
        const auto resource = std::make_shared<ClusteredLightBufferResource>(
            ClusteredLightBufferResource{std::move(native), graphicsDevice->parentGame});
        const CNA_Result result =
            GetRuntimeHandles().Create(ObjectKind::ClusteredLightBuffer, resource, outBuffer);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned light-buffer handle could not be created.");
        }
        AddOwnedGraphicsResourceFor(graphicsDevice->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_clustered_light_buffer_upload(
    const CNA_ClusteredLightBufferHandle buffer,
    const CNA_ClusteredLightSetHandle lights,
    const CNA_ClusteredLightGridHandle gridHandle,
    const CNA_ClusteredLightAssignmentHandle assignment)
{
    return CNA_WITH_LIGHT_BUFFER(buffer,
        [&](const std::shared_ptr<ClusteredLightBufferResource>& b) -> CNA_Result {
            std::shared_ptr<ClusteredLightSetResource> set;
            if (const CNA_Result result = GetEngineResource(
                    lights, ObjectKind::ClusteredLightSet, "ClusteredLightSetEXT", &set);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            std::shared_ptr<ClusteredLightGridResource> grid;
            if (const CNA_Result result = GetEngineResource(
                    gridHandle, ObjectKind::ClusteredLightGrid, "ClusteredLightGrid", &grid);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            std::shared_ptr<ClusteredLightAssignmentResource> assigned;
            if (const CNA_Result result = GetEngineResource(
                    assignment, ObjectKind::ClusteredLightAssignment, "ClusteredLightAssignment",
                    &assigned);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            // The trio must describe one another. Uploading a mismatched set would light the wrong
            // objects with the wrong lamps rather than fail, which is why this is a refusal and
            // not a best effort.
            if (assigned->value->getLightCount() != set->value->getCount() ||
                assigned->value->getClusterCount() != grid->value->getClusterCount()) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The assignment describes a different set of lights or a different grid.");
            }
            b->value->upload(*set->value, *grid->value, *assigned->value);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_clustered_light_buffer_bind(
    const CNA_ClusteredLightBufferHandle buffer,
    const CNA_EffectHandle effect,
    const int32_t firstUnit)
{
    return CNA_WITH_LIGHT_BUFFER(buffer,
        [&](const std::shared_ptr<ClusteredLightBufferResource>& b) -> CNA_Result {
            if (!b->value->isUploaded()) {
                return Fail(
                    CNA_RESULT_INVALID_STATE,
                    CNA_ERROR_CATEGORY_STATE,
                    "Nothing has been uploaded, so there is no light list to bind.");
            }
            std::shared_ptr<EffectResource> effectResource;
            if (const CNA_Result result = GetEffectForPass(effect, &effectResource);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            auto* const shader = dynamic_cast<Microsoft::Xna::Framework::Graphics::ShaderEffect*>(
                effectResource->value.get());
            if (shader == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The clustered light buffer binds into a ShaderEffect, and this effect is not "
                    "one.");
            }
            b->value->bind(*shader, static_cast<int>(firstUnit));
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_clustered_light_buffer_is_uploaded(
    const CNA_ClusteredLightBufferHandle buffer, CNA_Bool* const outUploaded)
{
    return CNA_WITH_LIGHT_BUFFER(buffer,
        [&](const std::shared_ptr<ClusteredLightBufferResource>& b) -> CNA_Result {
            return StoreValue(
                outUploaded, static_cast<CNA_Bool>(b->value->isUploaded() ? CNA_TRUE : CNA_FALSE));
        });
}

CNA_Result cna_clustered_light_buffer_get_light_count(
    const CNA_ClusteredLightBufferHandle buffer, int32_t* const outCount)
{
    return CNA_WITH_LIGHT_BUFFER(buffer,
        [&](const std::shared_ptr<ClusteredLightBufferResource>& b) -> CNA_Result {
            return StoreValue(outCount, static_cast<int32_t>(b->value->getLightCount()));
        });
}

CNA_Result cna_clustered_light_buffer_get_cluster_count(
    const CNA_ClusteredLightBufferHandle buffer, int32_t* const outCount)
{
    return CNA_WITH_LIGHT_BUFFER(buffer,
        [&](const std::shared_ptr<ClusteredLightBufferResource>& b) -> CNA_Result {
            return StoreValue(outCount, static_cast<int32_t>(b->value->getClusterCount()));
        });
}

CNA_Result cna_clustered_light_buffer_get_reference_count(
    const CNA_ClusteredLightBufferHandle buffer, int32_t* const outCount)
{
    return CNA_WITH_LIGHT_BUFFER(buffer,
        [&](const std::shared_ptr<ClusteredLightBufferResource>& b) -> CNA_Result {
            return StoreValue(outCount, static_cast<int32_t>(b->value->getReferenceCount()));
        });
}

CNA_Result cna_clustered_light_buffer_copy_light_lookup_glsl(
    char* const destination, const uint64_t capacity, uint64_t* const outBytes)
{
    return CopyFormattedString(destination, capacity, outBytes, [] {
        return Ext::ClusteredLightBuffer::getLightLookupGlsl();
    });
}

CNA_Result cna_clustered_light_buffer_destroy(const CNA_ClusteredLightBufferHandle bufferHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ClusteredLightBufferResource> buffer;
        if (const CNA_Result result = GetEngineResource(
                bufferHandle, ObjectKind::ClusteredLightBuffer, "ClusteredLightBuffer", &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // No borrow count: the buffer's three textures are private and it lends none of them.
        const CNA_Result releaseResult = GetRuntimeHandles().Release(bufferHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The owned light-buffer handle could not be released.");
        }
        RemoveOwnedGraphicsResourceFor(buffer->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

namespace {

struct ClusteredForwardEffectResource final {
    std::shared_ptr<Ext::ClusteredForwardEffect> value;
    CNA_Handle parentGame;
    uint64_t activeBorrowCount = 0U;
};

struct ClusteredLightComputeResource final {
    std::shared_ptr<Ext::ClusteredLightCompute> value;
    CNA_Handle parentGame;
};

static_assert(
    Ext::ClusteredForwardEffect::kMaxLightsPerFragment ==
        CNA_CLUSTERED_FORWARD_MAX_LIGHTS_PER_FRAGMENT_EXT &&
    Ext::ClusteredLightCompute::kDefaultStride == CNA_CLUSTERED_COMPUTE_DEFAULT_STRIDE_EXT);

[[nodiscard]] CNA_Result RequireVector3Argument(
    const CNA_Vector3* const value, const char* const what)
{
    if (value == nullptr) {
        return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, what);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] Microsoft::Xna::Framework::Vector3 ToNativeVector3(const CNA_Vector3& v) noexcept
{
    return {v.x, v.y, v.z};
}

[[nodiscard]] CNA_Result ToNativeSphereArray(
    const CNA_BoundingSphere* const bounds,
    const uint64_t boundsCount,
    std::vector<BoundingSphere>* const out)
{
    if (bounds == nullptr && boundsCount != 0U) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, "The bounds array is null.");
    }
    if (boundsCount > static_cast<uint64_t>(CNA_CLUSTERED_ASSIGNMENT_MAX_LIGHTS_EXT)) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_RANGE,
            "More lights than the assignment accepts.");
    }
    out->reserve(static_cast<std::size_t>(boundsCount));
    for (uint64_t sphere = 0U; sphere < boundsCount; ++sphere) {
        out->emplace_back(
            ToNativeVector3(bounds[sphere].center), bounds[sphere].radius);
    }
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_clustered_forward_effect_create(
    const CNA_Handle graphicsDeviceHandle, CNA_ClusteredForwardEffectHandle* const outEffect)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEffect == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The clustered-forward-effect output handle is null.");
        }
        *outEffect = CNA_INVALID_HANDLE;
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result =
                GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto native = std::make_shared<Ext::ClusteredForwardEffect>(*graphicsDevice->value);
        const auto resource = std::make_shared<ClusteredForwardEffectResource>(
            ClusteredForwardEffectResource{std::move(native), graphicsDevice->parentGame, 0U});
        const CNA_Result result =
            GetRuntimeHandles().Create(ObjectKind::ClusteredForwardEffect, resource, outEffect);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned clustered-forward-effect handle could not be created.");
        }
        AddOwnedGraphicsResourceFor(graphicsDevice->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

#define CNA_WITH_FORWARD(handle, body)                                                             \
    WithMap<ClusteredForwardEffectResource>(                                                       \
        (handle), ObjectKind::ClusteredForwardEffect, "ClusteredForwardEffect", body)
#define CNA_WITH_COMPUTE(handle, body)                                                             \
    WithMap<ClusteredLightComputeResource>(                                                        \
        (handle), ObjectKind::ClusteredLightCompute, "ClusteredLightCompute", body)

CNA_Result cna_clustered_forward_effect_is_supported(
    const CNA_ClusteredForwardEffectHandle effect, CNA_Bool* const outSupported)
{
    return CNA_WITH_FORWARD(effect, [&](const std::shared_ptr<ClusteredForwardEffectResource>& e)
        -> CNA_Result {
            return StoreValue(
                outSupported,
                static_cast<CNA_Bool>(e->value->isSupported() ? CNA_TRUE : CNA_FALSE));
        });
}

CNA_Result cna_clustered_forward_effect_begin(
    const CNA_ClusteredForwardEffectHandle effect,
    const CNA_Matrix* const world,
    const CNA_Matrix* const view,
    const CNA_Matrix* const projection,
    const CNA_Vector3* const cameraPosition,
    const CNA_ClusteredLightBufferHandle lights)
{
    return CNA_WITH_FORWARD(effect, [&](const std::shared_ptr<ClusteredForwardEffectResource>& e)
        -> CNA_Result {
            if (const CNA_Result result = RequireMatrixArgument(world, "The world is null.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (const CNA_Result result = RequireMatrixArgument(view, "The view is null.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (const CNA_Result result =
                    RequireMatrixArgument(projection, "The projection is null.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (const CNA_Result result =
                    RequireVector3Argument(cameraPosition, "The camera position is null.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            std::shared_ptr<ClusteredLightBufferResource> buffer;
            if (const CNA_Result result = GetEngineResource(
                    lights, ObjectKind::ClusteredLightBuffer, "ClusteredLightBuffer", &buffer);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            // Both canonical refusals are answered here so each keeps its own message: an empty
            // buffer has no cluster table to walk, and a transmissive material with no opaque
            // frame is an opaque object where a glass one was asked for.
            if (!buffer->value->isUploaded()) {
                return Fail(
                    CNA_RESULT_INVALID_STATE,
                    CNA_ERROR_CATEGORY_STATE,
                    "The light buffer holds nothing, so there is no cluster table for the shader "
                    "to walk.");
            }
            e->value->begin(
                ToNativeMatrix(*world), ToNativeMatrix(*view), ToNativeMatrix(*projection),
                ToNativeVector3(*cameraPosition), *buffer->value);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_clustered_forward_effect_get_effect(
    const CNA_ClusteredForwardEffectHandle effect, CNA_EffectHandle* const outShader)
{
    return CNA_WITH_FORWARD(effect, [&](const std::shared_ptr<ClusteredForwardEffectResource>& e)
        -> CNA_Result {
            if (outShader == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The shader output handle is null.");
            }
            *outShader = CNA_INVALID_HANDLE;
            return BorrowEffectFrom(e, e->value->getEffect(), outShader);
        });
}

CNA_Result cna_clustered_forward_effect_has_area_light(
    const CNA_ClusteredForwardEffectHandle effect, CNA_Bool* const outHas)
{
    return CNA_WITH_FORWARD(effect, [&](const std::shared_ptr<ClusteredForwardEffectResource>& e)
        -> CNA_Result {
            return StoreValue(
                outHas, static_cast<CNA_Bool>(e->value->hasAreaLight() ? CNA_TRUE : CNA_FALSE));
        });
}

CNA_Result cna_clustered_forward_effect_clear_area_light(
    const CNA_ClusteredForwardEffectHandle effect)
{
    return CNA_WITH_FORWARD(effect, [](const std::shared_ptr<ClusteredForwardEffectResource>& e)
        -> CNA_Result {
            e->value->clearAreaLight();
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_clustered_forward_effect_has_light_probe(
    const CNA_ClusteredForwardEffectHandle effect, CNA_Bool* const outHas)
{
    return CNA_WITH_FORWARD(effect, [&](const std::shared_ptr<ClusteredForwardEffectResource>& e)
        -> CNA_Result {
            return StoreValue(
                outHas, static_cast<CNA_Bool>(e->value->hasLightProbe() ? CNA_TRUE : CNA_FALSE));
        });
}

CNA_Result cna_clustered_forward_effect_clear_light_probe(
    const CNA_ClusteredForwardEffectHandle effect)
{
    return CNA_WITH_FORWARD(effect, [](const std::shared_ptr<ClusteredForwardEffectResource>& e)
        -> CNA_Result {
            e->value->clearLightProbe();
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_clustered_forward_effect_get_base_color(
    const CNA_ClusteredForwardEffectHandle effect, CNA_Vector3* const outColor)
{
    return CNA_WITH_FORWARD(effect, [&](const std::shared_ptr<ClusteredForwardEffectResource>& e)
        -> CNA_Result {
            const auto c = e->value->getBaseColor();
            return StoreValue(outColor, Vec3(c.X, c.Y, c.Z));
        });
}

CNA_Result cna_clustered_forward_effect_set_base_color(
    const CNA_ClusteredForwardEffectHandle effect, const CNA_Vector3* const color)
{
    return CNA_WITH_FORWARD(effect, [&](const std::shared_ptr<ClusteredForwardEffectResource>& e)
        -> CNA_Result {
            if (const CNA_Result result = RequireVector3Argument(color, "The colour is null.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            // Clamped per channel by the canonical setter; preserved rather than refused.
            e->value->setBaseColor(ToNativeVector3(*color));
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_clustered_forward_effect_get_metallic(
    const CNA_ClusteredForwardEffectHandle effect, float* const outMetallic)
{
    return CNA_WITH_FORWARD(effect, [&](const std::shared_ptr<ClusteredForwardEffectResource>& e)
        -> CNA_Result { return StoreValue(outMetallic, e->value->getMetallic()); });
}

CNA_Result cna_clustered_forward_effect_set_metallic(
    const CNA_ClusteredForwardEffectHandle effect, const float metallic)
{
    return CNA_WITH_FORWARD(effect, [&](const std::shared_ptr<ClusteredForwardEffectResource>& e)
        -> CNA_Result {
            e->value->setMetallic(metallic);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_clustered_forward_effect_get_roughness(
    const CNA_ClusteredForwardEffectHandle effect, float* const outRoughness)
{
    return CNA_WITH_FORWARD(effect, [&](const std::shared_ptr<ClusteredForwardEffectResource>& e)
        -> CNA_Result { return StoreValue(outRoughness, e->value->getRoughness()); });
}

CNA_Result cna_clustered_forward_effect_set_roughness(
    const CNA_ClusteredForwardEffectHandle effect, const float roughness)
{
    return CNA_WITH_FORWARD(effect, [&](const std::shared_ptr<ClusteredForwardEffectResource>& e)
        -> CNA_Result {
            // Clamped to 0.04 at the low end, not 0: a perfectly smooth surface collapses the
            // specular lobe to a point the shader cannot integrate.
            e->value->setRoughness(roughness);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_clustered_forward_effect_get_ior(
    const CNA_ClusteredForwardEffectHandle effect, float* const outIor)
{
    return CNA_WITH_FORWARD(effect, [&](const std::shared_ptr<ClusteredForwardEffectResource>& e)
        -> CNA_Result { return StoreValue(outIor, e->value->getIor()); });
}

CNA_Result cna_clustered_forward_effect_set_ior(
    const CNA_ClusteredForwardEffectHandle effect, const float ior)
{
    return CNA_WITH_FORWARD(effect, [&](const std::shared_ptr<ClusteredForwardEffectResource>& e)
        -> CNA_Result {
            e->value->setIor(ior);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_clustered_forward_effect_get_ambient(
    const CNA_ClusteredForwardEffectHandle effect, CNA_Vector3* const outAmbient)
{
    return CNA_WITH_FORWARD(effect, [&](const std::shared_ptr<ClusteredForwardEffectResource>& e)
        -> CNA_Result {
            const auto a = e->value->getAmbient();
            return StoreValue(outAmbient, Vec3(a.X, a.Y, a.Z));
        });
}

CNA_Result cna_clustered_forward_effect_set_ambient(
    const CNA_ClusteredForwardEffectHandle effect, const CNA_Vector3* const ambient)
{
    return CNA_WITH_FORWARD(effect, [&](const std::shared_ptr<ClusteredForwardEffectResource>& e)
        -> CNA_Result {
            if (const CNA_Result result = RequireVector3Argument(ambient, "The ambient is null.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            // Floored at zero per channel: a negative ambient would subtract light never added.
            e->value->setAmbient(ToNativeVector3(*ambient));
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_clustered_forward_effect_get_opaque_frame(
    const CNA_ClusteredForwardEffectHandle effect, CNA_Handle* const outFrame)
{
    return CNA_WITH_FORWARD(effect, [&](const std::shared_ptr<ClusteredForwardEffectResource>& e)
        -> CNA_Result {
            if (outFrame == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The frame output handle is null.");
            }
            *outFrame = CNA_INVALID_HANDLE;
            auto* const frame = e->value->getOpaqueFrame();
            if (frame == nullptr) {
                return CNA_RESULT_SUCCESS;
            }
            // Not a counted borrow, for the reason cna_effect_get_shadow_map_ext gives: the effect
            // borrows the frame rather than owning it, so counting would protect nothing.
            const std::shared_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> view(e, frame);
            return CreateBorrowedRenderTarget2D(view, e->parentGame, e, outFrame);
        });
}

CNA_Result cna_clustered_forward_effect_set_opaque_frame(
    const CNA_ClusteredForwardEffectHandle effect, const CNA_Handle frame)
{
    return CNA_WITH_FORWARD(effect, [&](const std::shared_ptr<ClusteredForwardEffectResource>& e)
        -> CNA_Result {
            Microsoft::Xna::Framework::Graphics::Texture2D* texture = nullptr;
            std::shared_ptr<Texture2DResource> retention;
            if (const CNA_Result result =
                    ResolveTexture2DArgument(frame, "opaque frame", &texture, &retention);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            e->value->setOpaqueFrame(texture);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_clustered_forward_effect_volume_attenuation(
    const CNA_Vector3* const attenuationColor,
    const float attenuationDistance,
    const float thickness,
    CNA_Vector3* const outAttenuation)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result =
                RequireVector3Argument(attenuationColor, "The attenuation colour is null.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto value = Ext::ClusteredForwardEffect::volumeAttenuation(
            ToNativeVector3(*attenuationColor), attenuationDistance, thickness);
        return StoreValue(outAttenuation, Vec3(value.X, value.Y, value.Z));
    });
}

CNA_Result cna_clustered_forward_effect_contribution(
    const CNA_ClusteredLightEXT* const light,
    const CNA_Vector3* const surface,
    const CNA_Vector3* const normal,
    const CNA_Vector3* const cameraPosition,
    const CNA_Vector3* const baseColor,
    const float metallic,
    const float roughness,
    const float clearcoat,
    const float clearcoatRoughness,
    const CNA_Vector3* const sheenColor,
    const float sheenRoughness,
    const float iridescence,
    const float iridescenceIor,
    const float iridescenceThickness,
    const CNA_Vector3* const subsurfaceColor,
    const float subsurfaceWrap,
    CNA_Vector3* const outContribution)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Ext::ClusteredLightEXT nativeLight;
        if (const CNA_Result result = ToNativeClusteredLight(light, &nativeLight);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Vector3* const required[] = {
            surface, normal, cameraPosition, baseColor, sheenColor, subsurfaceColor};
        for (const CNA_Vector3* const argument : required) {
            if (const CNA_Result result =
                    RequireVector3Argument(argument, "A contribution vector is null.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
        }
        const auto value = Ext::ClusteredForwardEffect::contribution(
            nativeLight, ToNativeVector3(*surface), ToNativeVector3(*normal),
            ToNativeVector3(*cameraPosition), ToNativeVector3(*baseColor), metallic, roughness,
            clearcoat, clearcoatRoughness, ToNativeVector3(*sheenColor), sheenRoughness,
            iridescence, iridescenceIor, iridescenceThickness, ToNativeVector3(*subsurfaceColor),
            subsurfaceWrap);
        return StoreValue(outContribution, Vec3(value.X, value.Y, value.Z));
    });
}

CNA_Result cna_clustered_forward_effect_destroy(
    const CNA_ClusteredForwardEffectHandle effectHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ClusteredForwardEffectResource> effect;
        if (const CNA_Result result = GetEngineResource(
                effectHandle, ObjectKind::ClusteredForwardEffect, "ClusteredForwardEffect",
                &effect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (effect->activeBorrowCount != 0U) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The clustered forward effect is still lending its shader effect.");
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(effectHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The owned clustered-forward-effect handle could not be released.");
        }
        RemoveOwnedGraphicsResourceFor(effect->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_clustered_light_compute_create(
    const CNA_Handle graphicsDeviceHandle,
    const int32_t stride,
    CNA_ClusteredLightComputeHandle* const outCompute)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCompute == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The compute output handle is null.");
        }
        *outCompute = CNA_INVALID_HANDLE;
        if (stride <= 0) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "The per-cluster capacity must be positive.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result =
                GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto native = std::make_shared<Ext::ClusteredLightCompute>(
            *graphicsDevice->value, static_cast<int>(stride));
        const auto resource = std::make_shared<ClusteredLightComputeResource>(
            ClusteredLightComputeResource{std::move(native), graphicsDevice->parentGame});
        const CNA_Result result =
            GetRuntimeHandles().Create(ObjectKind::ClusteredLightCompute, resource, outCompute);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned compute handle could not be created.");
        }
        AddOwnedGraphicsResourceFor(graphicsDevice->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_clustered_light_compute_is_supported(
    const CNA_ClusteredLightComputeHandle compute, CNA_Bool* const outSupported)
{
    return CNA_WITH_COMPUTE(compute, [&](const std::shared_ptr<ClusteredLightComputeResource>& c)
        -> CNA_Result {
            return StoreValue(
                outSupported,
                static_cast<CNA_Bool>(c->value->isSupported() ? CNA_TRUE : CNA_FALSE));
        });
}

CNA_Result cna_clustered_light_compute_copy_unsupported_reason(
    const CNA_ClusteredLightComputeHandle compute,
    char* const destination, const uint64_t capacity, uint64_t* const outBytes)
{
    std::shared_ptr<ClusteredLightComputeResource> resource;
    if (const CNA_Result result = GetEngineResource(
            compute, ObjectKind::ClusteredLightCompute, "ClusteredLightCompute", &resource);
        result != CNA_RESULT_SUCCESS) {
        if (outBytes != nullptr) {
            *outBytes = UINT64_C(0);
        }
        return result;
    }
    return CopyFormattedString(destination, capacity, outBytes, [&resource] {
        return resource->value->getUnsupportedReason();
    });
}

CNA_Result cna_clustered_light_compute_get_stride(
    const CNA_ClusteredLightComputeHandle compute, int32_t* const outStride)
{
    return CNA_WITH_COMPUTE(compute, [&](const std::shared_ptr<ClusteredLightComputeResource>& c)
        -> CNA_Result { return StoreValue(outStride, static_cast<int32_t>(c->value->getStride())); });
}

CNA_Result cna_clustered_light_compute_assign(
    const CNA_ClusteredLightComputeHandle compute,
    const CNA_ClusteredLightGridHandle gridHandle,
    const CNA_Matrix* const view,
    const CNA_BoundingSphere* const bounds,
    const uint64_t boundsCount,
    const CNA_ClusteredLightAssignmentHandle outAssignment)
{
    return CNA_WITH_COMPUTE(compute, [&](const std::shared_ptr<ClusteredLightComputeResource>& c)
        -> CNA_Result {
            if (const CNA_Result result = RequireMatrixArgument(view, "The view is null.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            std::vector<BoundingSphere> nativeBounds;
            if (const CNA_Result result =
                    ToNativeSphereArray(bounds, boundsCount, &nativeBounds);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            std::shared_ptr<ClusteredLightGridResource> grid;
            if (const CNA_Result result = GetEngineResource(
                    gridHandle, ObjectKind::ClusteredLightGrid, "ClusteredLightGrid", &grid);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            std::shared_ptr<ClusteredLightAssignmentResource> assignment;
            if (const CNA_Result result = GetEngineResource(
                    outAssignment, ObjectKind::ClusteredLightAssignment,
                    "ClusteredLightAssignment", &assignment);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            // The projection check is made here rather than left to the canonical code, because
            // the CPU fallback path reaches ClusteredLightAssignment::assign, which throws its own
            // differently-worded version of the same refusal.
            if (!grid->value->hasProjection()) {
                return Fail(
                    CNA_RESULT_INVALID_STATE,
                    CNA_ERROR_CATEGORY_STATE,
                    "The grid has no projection, so it has no clusters to sort into yet.");
            }
            c->value->assign(
                *grid->value, ToNativeMatrix(*view), nativeBounds, *assignment->value);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_clustered_light_compute_used_compute(
    const CNA_ClusteredLightComputeHandle compute, CNA_Bool* const outUsed)
{
    return CNA_WITH_COMPUTE(compute, [&](const std::shared_ptr<ClusteredLightComputeResource>& c)
        -> CNA_Result {
            return StoreValue(
                outUsed, static_cast<CNA_Bool>(c->value->usedCompute() ? CNA_TRUE : CNA_FALSE));
        });
}

CNA_Result cna_clustered_light_compute_has_overflowed(
    const CNA_ClusteredLightComputeHandle compute, CNA_Bool* const outOverflowed)
{
    return CNA_WITH_COMPUTE(compute, [&](const std::shared_ptr<ClusteredLightComputeResource>& c)
        -> CNA_Result {
            return StoreValue(
                outOverflowed,
                static_cast<CNA_Bool>(c->value->hasOverflowed() ? CNA_TRUE : CNA_FALSE));
        });
}

CNA_Result cna_clustered_light_compute_destroy(
    const CNA_ClusteredLightComputeHandle computeHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ClusteredLightComputeResource> compute;
        if (const CNA_Result result = GetEngineResource(
                computeHandle, ObjectKind::ClusteredLightCompute, "ClusteredLightCompute",
                &compute);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(computeHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The owned compute handle could not be released.");
        }
        RemoveOwnedGraphicsResourceFor(compute->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_clustered_shadow_policy_select(
    const CNA_ClusteredShadowPolicyHandle policy,
    const CNA_ClusteredLightSetHandle lights,
    const CNA_Matrix* const view,
    const CNA_Matrix* const projection,
    const CNA_Vector3* const cameraPosition)
{
    return CNA_WITH_POLICY(policy,
        [&](const std::shared_ptr<ClusteredShadowPolicyResource>& p) -> CNA_Result {
            if (const CNA_Result result = RequireMatrixArgument(view, "The view is null.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (const CNA_Result result =
                    RequireMatrixArgument(projection, "The projection is null.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (const CNA_Result result =
                    RequireVector3Argument(cameraPosition, "The camera position is null.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            std::shared_ptr<ClusteredLightSetResource> set;
            if (const CNA_Result result = GetEngineResource(
                    lights, ObjectKind::ClusteredLightSet, "ClusteredLightSetEXT", &set);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            p->value->select(
                *set->value, ToNativeMatrix(*view), ToNativeMatrix(*projection),
                ToNativeVector3(*cameraPosition));
            return CNA_RESULT_SUCCESS;
        });
}

namespace {

struct PbrMaterialExtensionsResource final {
    std::shared_ptr<Ext::PbrMaterialExtensions> value;
    // The game a bound texture belongs to, remembered so a read-back handle can be parented the
    // way every other borrowed texture in this ABI is. It is a plain handle, not a retention: the
    // extensions borrow their textures and must not keep one alive.
    CNA_Handle textureParentGame = CNA_INVALID_HANDLE;
};

// A texture the extensions point at but do not own. The aliasing constructor keeps the extensions
// alive for as long as the borrowed handle exists, so the pointer cannot outlive its holder; the
// texture's own lifetime stays entirely the caller's, which is the borrow rule this type inherits
// from effects.
[[nodiscard]] CNA_Result BorrowTextureFrom(
    const std::shared_ptr<PbrMaterialExtensionsResource>& owner,
    Microsoft::Xna::Framework::Graphics::Texture2D* const texture,
    CNA_Handle* const outTexture)
{
    if (outTexture == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The texture output handle is null.");
    }
    *outTexture = CNA_INVALID_HANDLE;
    if (texture == nullptr) {
        return CNA_RESULT_SUCCESS;
    }
    const std::shared_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> view(owner, texture);
    return CreateBorrowedRenderTarget2D(view, owner->textureParentGame, owner, outTexture);
}

[[nodiscard]] CNA_Result GetExtensions(
    const CNA_Handle handle, std::shared_ptr<PbrMaterialExtensionsResource>* const out)
{
    return GetEngineResource(
        handle, ObjectKind::PbrMaterialExtensions, "PbrMaterialExtensions", out);
}

} // namespace

#define CNA_WITH_EXTENSIONS(handle, body)                                                          \
    WithMap<PbrMaterialExtensionsResource>(                                                        \
        (handle), ObjectKind::PbrMaterialExtensions, "PbrMaterialExtensions", body)

CNA_Result cna_pbr_material_extensions_create(CNA_PbrMaterialExtensionsHandle* const outExtensions)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outExtensions == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The extensions output handle is null.");
        }
        *outExtensions = CNA_INVALID_HANDLE;
        const auto resource = std::make_shared<PbrMaterialExtensionsResource>(
            PbrMaterialExtensionsResource{std::make_shared<Ext::PbrMaterialExtensions>(),
                                          CNA_INVALID_HANDLE});
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::PbrMaterialExtensions, resource, outExtensions);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned material-extensions handle could not be created.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_material_extensions_destroy(
    const CNA_PbrMaterialExtensionsHandle extensionsHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<PbrMaterialExtensionsResource> extensions;
        if (const CNA_Result result = GetExtensions(extensionsHandle, &extensions);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The nine textures are borrowed, so nothing here releases one.
        const CNA_Result releaseResult = GetRuntimeHandles().Release(extensionsHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The owned material-extensions handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_pbr_material_extensions_copy_from(
    const CNA_PbrMaterialExtensionsHandle destination,
    const CNA_PbrMaterialExtensionsHandle source)
{
    return CNA_WITH_EXTENSIONS(destination,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& d) -> CNA_Result {
            std::shared_ptr<PbrMaterialExtensionsResource> s;
            if (const CNA_Result result = GetExtensions(source, &s);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            *d->value = *s->value;
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_pbr_material_extensions_get_clearcoat_factor(
    const CNA_PbrMaterialExtensionsHandle extensions, float* const outValue)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            return StoreValue(outValue, e->value->getClearcoatFactor());
        });
}

CNA_Result cna_pbr_material_extensions_set_clearcoat_factor(
    const CNA_PbrMaterialExtensionsHandle extensions, const float value)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            e->value->setClearcoatFactor(value);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_pbr_material_extensions_get_clearcoat_roughness(
    const CNA_PbrMaterialExtensionsHandle extensions, float* const outValue)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            return StoreValue(outValue, e->value->getClearcoatRoughness());
        });
}

CNA_Result cna_pbr_material_extensions_set_clearcoat_roughness(
    const CNA_PbrMaterialExtensionsHandle extensions, const float value)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            e->value->setClearcoatRoughness(value);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_pbr_material_extensions_get_clearcoat_normal_scale(
    const CNA_PbrMaterialExtensionsHandle extensions, float* const outValue)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            return StoreValue(outValue, e->value->getClearcoatNormalScale());
        });
}

CNA_Result cna_pbr_material_extensions_set_clearcoat_normal_scale(
    const CNA_PbrMaterialExtensionsHandle extensions, const float value)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            e->value->setClearcoatNormalScale(value);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_pbr_material_extensions_get_sheen_roughness(
    const CNA_PbrMaterialExtensionsHandle extensions, float* const outValue)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            return StoreValue(outValue, e->value->getSheenRoughness());
        });
}

CNA_Result cna_pbr_material_extensions_set_sheen_roughness(
    const CNA_PbrMaterialExtensionsHandle extensions, const float value)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            e->value->setSheenRoughness(value);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_pbr_material_extensions_get_transmission_factor(
    const CNA_PbrMaterialExtensionsHandle extensions, float* const outValue)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            return StoreValue(outValue, e->value->getTransmissionFactor());
        });
}

CNA_Result cna_pbr_material_extensions_set_transmission_factor(
    const CNA_PbrMaterialExtensionsHandle extensions, const float value)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            e->value->setTransmissionFactor(value);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_pbr_material_extensions_get_thickness_factor(
    const CNA_PbrMaterialExtensionsHandle extensions, float* const outValue)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            return StoreValue(outValue, e->value->getThicknessFactor());
        });
}

CNA_Result cna_pbr_material_extensions_set_thickness_factor(
    const CNA_PbrMaterialExtensionsHandle extensions, const float value)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            e->value->setThicknessFactor(value);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_pbr_material_extensions_get_attenuation_distance(
    const CNA_PbrMaterialExtensionsHandle extensions, float* const outValue)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            return StoreValue(outValue, e->value->getAttenuationDistance());
        });
}

CNA_Result cna_pbr_material_extensions_set_attenuation_distance(
    const CNA_PbrMaterialExtensionsHandle extensions, const float value)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            e->value->setAttenuationDistance(value);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_pbr_material_extensions_get_iridescence_factor(
    const CNA_PbrMaterialExtensionsHandle extensions, float* const outValue)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            return StoreValue(outValue, e->value->getIridescenceFactor());
        });
}

CNA_Result cna_pbr_material_extensions_set_iridescence_factor(
    const CNA_PbrMaterialExtensionsHandle extensions, const float value)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            e->value->setIridescenceFactor(value);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_pbr_material_extensions_get_iridescence_ior(
    const CNA_PbrMaterialExtensionsHandle extensions, float* const outValue)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            return StoreValue(outValue, e->value->getIridescenceIor());
        });
}

CNA_Result cna_pbr_material_extensions_set_iridescence_ior(
    const CNA_PbrMaterialExtensionsHandle extensions, const float value)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            e->value->setIridescenceIor(value);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_pbr_material_extensions_get_iridescence_thickness_minimum(
    const CNA_PbrMaterialExtensionsHandle extensions, float* const outValue)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            return StoreValue(outValue, e->value->getIridescenceThicknessMinimum());
        });
}

CNA_Result cna_pbr_material_extensions_set_iridescence_thickness_minimum(
    const CNA_PbrMaterialExtensionsHandle extensions, const float value)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            e->value->setIridescenceThicknessMinimum(value);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_pbr_material_extensions_get_iridescence_thickness_maximum(
    const CNA_PbrMaterialExtensionsHandle extensions, float* const outValue)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            return StoreValue(outValue, e->value->getIridescenceThicknessMaximum());
        });
}

CNA_Result cna_pbr_material_extensions_set_iridescence_thickness_maximum(
    const CNA_PbrMaterialExtensionsHandle extensions, const float value)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            e->value->setIridescenceThicknessMaximum(value);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_pbr_material_extensions_get_subsurface_wrap(
    const CNA_PbrMaterialExtensionsHandle extensions, float* const outValue)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            return StoreValue(outValue, e->value->getSubsurfaceWrap());
        });
}

CNA_Result cna_pbr_material_extensions_set_subsurface_wrap(
    const CNA_PbrMaterialExtensionsHandle extensions, const float value)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            e->value->setSubsurfaceWrap(value);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_pbr_material_extensions_get_sheen_color_factor(
    const CNA_PbrMaterialExtensionsHandle extensions, CNA_Vector3* const outValue)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            const auto v = e->value->getSheenColorFactor();
            return StoreValue(outValue, Vec3(v.X, v.Y, v.Z));
        });
}

CNA_Result cna_pbr_material_extensions_set_sheen_color_factor(
    const CNA_PbrMaterialExtensionsHandle extensions, const CNA_Vector3* const value)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            if (const CNA_Result result = RequireVector3Argument(value, "The value is null.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            e->value->setSheenColorFactor(ToNativeVector3(*value));
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_pbr_material_extensions_get_attenuation_color(
    const CNA_PbrMaterialExtensionsHandle extensions, CNA_Vector3* const outValue)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            const auto v = e->value->getAttenuationColor();
            return StoreValue(outValue, Vec3(v.X, v.Y, v.Z));
        });
}

CNA_Result cna_pbr_material_extensions_set_attenuation_color(
    const CNA_PbrMaterialExtensionsHandle extensions, const CNA_Vector3* const value)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            if (const CNA_Result result = RequireVector3Argument(value, "The value is null.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            e->value->setAttenuationColor(ToNativeVector3(*value));
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_pbr_material_extensions_get_subsurface_color(
    const CNA_PbrMaterialExtensionsHandle extensions, CNA_Vector3* const outValue)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            const auto v = e->value->getSubsurfaceColor();
            return StoreValue(outValue, Vec3(v.X, v.Y, v.Z));
        });
}

CNA_Result cna_pbr_material_extensions_set_subsurface_color(
    const CNA_PbrMaterialExtensionsHandle extensions, const CNA_Vector3* const value)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            if (const CNA_Result result = RequireVector3Argument(value, "The value is null.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            e->value->setSubsurfaceColor(ToNativeVector3(*value));
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_pbr_material_extensions_get_clearcoat_texture(
    const CNA_PbrMaterialExtensionsHandle extensions, CNA_Handle* const outTexture)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            return BorrowTextureFrom(e, e->value->getClearcoatTexture(), outTexture);
        });
}

CNA_Result cna_pbr_material_extensions_set_clearcoat_texture(
    const CNA_PbrMaterialExtensionsHandle extensions, const CNA_Handle texture)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            Microsoft::Xna::Framework::Graphics::Texture2D* native = nullptr;
            std::shared_ptr<Texture2DResource> retention;
            if (const CNA_Result result =
                    ResolveTexture2DArgument(texture, "clearcoat_texture", &native, &retention);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (retention != nullptr) {
                e->textureParentGame = retention->parentGame;
            }
            e->value->setClearcoatTexture(native);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_pbr_material_extensions_get_clearcoat_roughness_texture(
    const CNA_PbrMaterialExtensionsHandle extensions, CNA_Handle* const outTexture)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            return BorrowTextureFrom(e, e->value->getClearcoatRoughnessTexture(), outTexture);
        });
}

CNA_Result cna_pbr_material_extensions_set_clearcoat_roughness_texture(
    const CNA_PbrMaterialExtensionsHandle extensions, const CNA_Handle texture)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            Microsoft::Xna::Framework::Graphics::Texture2D* native = nullptr;
            std::shared_ptr<Texture2DResource> retention;
            if (const CNA_Result result =
                    ResolveTexture2DArgument(texture, "clearcoat_roughness_texture", &native, &retention);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (retention != nullptr) {
                e->textureParentGame = retention->parentGame;
            }
            e->value->setClearcoatRoughnessTexture(native);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_pbr_material_extensions_get_clearcoat_normal_texture(
    const CNA_PbrMaterialExtensionsHandle extensions, CNA_Handle* const outTexture)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            return BorrowTextureFrom(e, e->value->getClearcoatNormalTexture(), outTexture);
        });
}

CNA_Result cna_pbr_material_extensions_set_clearcoat_normal_texture(
    const CNA_PbrMaterialExtensionsHandle extensions, const CNA_Handle texture)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            Microsoft::Xna::Framework::Graphics::Texture2D* native = nullptr;
            std::shared_ptr<Texture2DResource> retention;
            if (const CNA_Result result =
                    ResolveTexture2DArgument(texture, "clearcoat_normal_texture", &native, &retention);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (retention != nullptr) {
                e->textureParentGame = retention->parentGame;
            }
            e->value->setClearcoatNormalTexture(native);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_pbr_material_extensions_get_sheen_color_texture(
    const CNA_PbrMaterialExtensionsHandle extensions, CNA_Handle* const outTexture)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            return BorrowTextureFrom(e, e->value->getSheenColorTexture(), outTexture);
        });
}

CNA_Result cna_pbr_material_extensions_set_sheen_color_texture(
    const CNA_PbrMaterialExtensionsHandle extensions, const CNA_Handle texture)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            Microsoft::Xna::Framework::Graphics::Texture2D* native = nullptr;
            std::shared_ptr<Texture2DResource> retention;
            if (const CNA_Result result =
                    ResolveTexture2DArgument(texture, "sheen_color_texture", &native, &retention);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (retention != nullptr) {
                e->textureParentGame = retention->parentGame;
            }
            e->value->setSheenColorTexture(native);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_pbr_material_extensions_get_sheen_roughness_texture(
    const CNA_PbrMaterialExtensionsHandle extensions, CNA_Handle* const outTexture)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            return BorrowTextureFrom(e, e->value->getSheenRoughnessTexture(), outTexture);
        });
}

CNA_Result cna_pbr_material_extensions_set_sheen_roughness_texture(
    const CNA_PbrMaterialExtensionsHandle extensions, const CNA_Handle texture)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            Microsoft::Xna::Framework::Graphics::Texture2D* native = nullptr;
            std::shared_ptr<Texture2DResource> retention;
            if (const CNA_Result result =
                    ResolveTexture2DArgument(texture, "sheen_roughness_texture", &native, &retention);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (retention != nullptr) {
                e->textureParentGame = retention->parentGame;
            }
            e->value->setSheenRoughnessTexture(native);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_pbr_material_extensions_get_transmission_texture(
    const CNA_PbrMaterialExtensionsHandle extensions, CNA_Handle* const outTexture)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            return BorrowTextureFrom(e, e->value->getTransmissionTexture(), outTexture);
        });
}

CNA_Result cna_pbr_material_extensions_set_transmission_texture(
    const CNA_PbrMaterialExtensionsHandle extensions, const CNA_Handle texture)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            Microsoft::Xna::Framework::Graphics::Texture2D* native = nullptr;
            std::shared_ptr<Texture2DResource> retention;
            if (const CNA_Result result =
                    ResolveTexture2DArgument(texture, "transmission_texture", &native, &retention);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (retention != nullptr) {
                e->textureParentGame = retention->parentGame;
            }
            e->value->setTransmissionTexture(native);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_pbr_material_extensions_get_thickness_texture(
    const CNA_PbrMaterialExtensionsHandle extensions, CNA_Handle* const outTexture)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            return BorrowTextureFrom(e, e->value->getThicknessTexture(), outTexture);
        });
}

CNA_Result cna_pbr_material_extensions_set_thickness_texture(
    const CNA_PbrMaterialExtensionsHandle extensions, const CNA_Handle texture)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            Microsoft::Xna::Framework::Graphics::Texture2D* native = nullptr;
            std::shared_ptr<Texture2DResource> retention;
            if (const CNA_Result result =
                    ResolveTexture2DArgument(texture, "thickness_texture", &native, &retention);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (retention != nullptr) {
                e->textureParentGame = retention->parentGame;
            }
            e->value->setThicknessTexture(native);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_pbr_material_extensions_get_iridescence_texture(
    const CNA_PbrMaterialExtensionsHandle extensions, CNA_Handle* const outTexture)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            return BorrowTextureFrom(e, e->value->getIridescenceTexture(), outTexture);
        });
}

CNA_Result cna_pbr_material_extensions_set_iridescence_texture(
    const CNA_PbrMaterialExtensionsHandle extensions, const CNA_Handle texture)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            Microsoft::Xna::Framework::Graphics::Texture2D* native = nullptr;
            std::shared_ptr<Texture2DResource> retention;
            if (const CNA_Result result =
                    ResolveTexture2DArgument(texture, "iridescence_texture", &native, &retention);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (retention != nullptr) {
                e->textureParentGame = retention->parentGame;
            }
            e->value->setIridescenceTexture(native);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_pbr_material_extensions_get_iridescence_thickness_texture(
    const CNA_PbrMaterialExtensionsHandle extensions, CNA_Handle* const outTexture)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            return BorrowTextureFrom(e, e->value->getIridescenceThicknessTexture(), outTexture);
        });
}

CNA_Result cna_pbr_material_extensions_set_iridescence_thickness_texture(
    const CNA_PbrMaterialExtensionsHandle extensions, const CNA_Handle texture)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            Microsoft::Xna::Framework::Graphics::Texture2D* native = nullptr;
            std::shared_ptr<Texture2DResource> retention;
            if (const CNA_Result result =
                    ResolveTexture2DArgument(texture, "iridescence_thickness_texture", &native, &retention);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (retention != nullptr) {
                e->textureParentGame = retention->parentGame;
            }
            e->value->setIridescenceThicknessTexture(native);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_pbr_material_extensions_is_subsurface_enabled(
    const CNA_PbrMaterialExtensionsHandle extensions, CNA_Bool* const outValue)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            return StoreValue(
                outValue, static_cast<CNA_Bool>(e->value->isSubsurfaceEnabled() ? CNA_TRUE : CNA_FALSE));
        });
}

CNA_Result cna_pbr_material_extensions_is_iridescence_enabled(
    const CNA_PbrMaterialExtensionsHandle extensions, CNA_Bool* const outValue)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            return StoreValue(
                outValue, static_cast<CNA_Bool>(e->value->isIridescenceEnabled() ? CNA_TRUE : CNA_FALSE));
        });
}

CNA_Result cna_pbr_material_extensions_is_transmission_enabled(
    const CNA_PbrMaterialExtensionsHandle extensions, CNA_Bool* const outValue)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            return StoreValue(
                outValue, static_cast<CNA_Bool>(e->value->isTransmissionEnabled() ? CNA_TRUE : CNA_FALSE));
        });
}

CNA_Result cna_pbr_material_extensions_is_sheen_enabled(
    const CNA_PbrMaterialExtensionsHandle extensions, CNA_Bool* const outValue)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            return StoreValue(
                outValue, static_cast<CNA_Bool>(e->value->isSheenEnabled() ? CNA_TRUE : CNA_FALSE));
        });
}

CNA_Result cna_pbr_material_extensions_is_neutral(
    const CNA_PbrMaterialExtensionsHandle extensions, CNA_Bool* const outValue)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            return StoreValue(
                outValue, static_cast<CNA_Bool>(e->value->isNeutral() ? CNA_TRUE : CNA_FALSE));
        });
}

CNA_Result cna_pbr_material_extensions_equals(
    const CNA_PbrMaterialExtensionsHandle first,
    const CNA_PbrMaterialExtensionsHandle second,
    CNA_Bool* const outEqual)
{
    return CNA_WITH_EXTENSIONS(first,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& a) -> CNA_Result {
            std::shared_ptr<PbrMaterialExtensionsResource> b;
            if (const CNA_Result result = GetExtensions(second, &b);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            return StoreValue(
                outEqual, static_cast<CNA_Bool>(*a->value == *b->value ? CNA_TRUE : CNA_FALSE));
        });
}

CNA_Result cna_pbr_material_extensions_get_hash_code(
    const CNA_PbrMaterialExtensionsHandle extensions, uint64_t* const outHash)
{
    return CNA_WITH_EXTENSIONS(extensions,
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            return StoreValue(outHash, static_cast<uint64_t>(e->value->GetHashCode()));
        });
}

CNA_Result cna_pbr_material_extensions_copy_to_string(
    const CNA_PbrMaterialExtensionsHandle extensions,
    char* const destination, const uint64_t capacity, uint64_t* const outBytes)
{
    std::shared_ptr<PbrMaterialExtensionsResource> resource;
    if (const CNA_Result result = GetExtensions(extensions, &resource);
        result != CNA_RESULT_SUCCESS) {
        if (outBytes != nullptr) {
            *outBytes = UINT64_C(0);
        }
        return result;
    }
    return CopyFormattedString(
        destination, capacity, outBytes, [&resource] { return resource->value->ToString(); });
}

CNA_Result cna_thin_film_iridescence_evaluate(
    const float outsideIor,
    const float filmIor,
    const float cosTheta,
    const float thicknessNm,
    const CNA_Vector3* const baseF0,
    CNA_Vector3* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result =
                RequireVector3Argument(baseF0, "The base reflectance is null.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto v = Ext::ThinFilmIridescence::evaluate(
            outsideIor, filmIor, cosTheta, thicknessNm, ToNativeVector3(*baseF0));
        return StoreValue(outValue, Vec3(v.X, v.Y, v.Z));
    });
}

CNA_Result cna_thin_film_iridescence_copy_glsl(
    char* const destination, const uint64_t capacity, uint64_t* const outBytes)
{
    return CopyFormattedString(
        destination, capacity, outBytes, [] { return Ext::ThinFilmIridescence::getGlsl(); });
}

CNA_Result cna_clustered_forward_effect_get_material_extensions(
    const CNA_ClusteredForwardEffectHandle effect,
    CNA_PbrMaterialExtensionsHandle* const outExtensions)
{
    return CNA_WITH_FORWARD(effect, [&](const std::shared_ptr<ClusteredForwardEffectResource>& e)
        -> CNA_Result {
            if (outExtensions == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The extensions output handle is null.");
            }
            *outExtensions = CNA_INVALID_HANDLE;
            // A borrow onto the effect's own extensions: the aliasing constructor keeps the
            // effect alive for as long as the borrowed handle exists, so the reference the
            // canonical getter returns cannot outlive what it points into.
            const std::shared_ptr<Ext::PbrMaterialExtensions> view(
                e, const_cast<Ext::PbrMaterialExtensions*>(&e->value->getMaterialExtensions()));
            const auto resource = std::make_shared<PbrMaterialExtensionsResource>(
                PbrMaterialExtensionsResource{view, e->parentGame});
            const CNA_Result result = GetRuntimeHandles().Create(
                ObjectKind::PbrMaterialExtensions, resource, outExtensions);
            if (result != CNA_RESULT_SUCCESS) {
                return Fail(
                    result,
                    ErrorCategoryForResult(result),
                    "The borrowed material-extensions handle could not be created.");
            }
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_clustered_forward_effect_set_material_extensions(
    const CNA_ClusteredForwardEffectHandle effect,
    const CNA_PbrMaterialExtensionsHandle extensions)
{
    return CNA_WITH_FORWARD(effect, [&](const std::shared_ptr<ClusteredForwardEffectResource>& e)
        -> CNA_Result {
            std::shared_ptr<PbrMaterialExtensionsResource> source;
            if (const CNA_Result result = GetExtensions(extensions, &source);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            e->value->setMaterialExtensions(*source->value);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_clustered_forward_effect_contribution_with_extensions(
    const CNA_ClusteredLightEXT* const light,
    const CNA_Vector3* const surface,
    const CNA_Vector3* const normal,
    const CNA_Vector3* const cameraPosition,
    const CNA_Vector3* const baseColor,
    const float metallic,
    const float roughness,
    const CNA_PbrMaterialExtensionsHandle extensions,
    CNA_Vector3* const outContribution)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Ext::ClusteredLightEXT nativeLight;
        if (const CNA_Result result = ToNativeClusteredLight(light, &nativeLight);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Vector3* const required[] = {surface, normal, cameraPosition, baseColor};
        for (const CNA_Vector3* const argument : required) {
            if (const CNA_Result result =
                    RequireVector3Argument(argument, "A contribution vector is null.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
        }
        std::shared_ptr<PbrMaterialExtensionsResource> source;
        if (const CNA_Result result = GetExtensions(extensions, &source);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto v = Ext::ClusteredForwardEffect::contribution(
            nativeLight, ToNativeVector3(*surface), ToNativeVector3(*normal),
            ToNativeVector3(*cameraPosition), ToNativeVector3(*baseColor), metallic, roughness,
            *source->value);
        return StoreValue(outContribution, Vec3(v.X, v.Y, v.Z));
    });
}

CNA_Result cna_pbr_material_ext_equals(
    const CNA_PbrMaterialEXT* const first,
    const CNA_PbrMaterialEXT* const second,
    CNA_Bool* const outEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Ext::PbrMaterial a;
        Ext::PbrMaterial b;
        if (first == nullptr || second == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "A material structure is null.");
        }
        if (const CNA_Result result = ToNativePbrMaterial(*first, &a);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ToNativePbrMaterial(*second, &b);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return StoreValue(outEqual, static_cast<CNA_Bool>(a == b ? CNA_TRUE : CNA_FALSE));
    });
}

CNA_Result cna_pbr_material_ext_get_hash_code(
    const CNA_PbrMaterialEXT* const material, uint64_t* const outHash)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Ext::PbrMaterial native;
        if (material == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The material structure is null.");
        }
        if (const CNA_Result result = ToNativePbrMaterial(*material, &native);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return StoreValue(outHash, static_cast<uint64_t>(native.GetHashCode()));
    });
}

CNA_Result cna_pbr_material_ext_copy_to_string(
    const CNA_PbrMaterialEXT* const material,
    char* const destination, const uint64_t capacity, uint64_t* const outBytes)
{
    Ext::PbrMaterial native;
    if (material == nullptr) {
        if (outBytes != nullptr) {
            *outBytes = UINT64_C(0);
        }
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The material structure is null.");
    }
    if (const CNA_Result result = ToNativePbrMaterial(*material, &native);
        result != CNA_RESULT_SUCCESS) {
        if (outBytes != nullptr) {
            *outBytes = UINT64_C(0);
        }
        return result;
    }
    return CopyFormattedString(
        destination, capacity, outBytes, [&native] { return native.ToString(); });
}

namespace {
// CBIND-087B. The C identities must name the canonical ordinals, not merely resemble them.
static_assert(CNA_PBR_TEXTURE_SLOT_COUNT == Ext::kPbrTextureSlotCount);
static_assert(
    static_cast<uint32_t>(Ext::TransparencyMode::None) == CNA_TRANSPARENCY_MODE_NONE &&
    static_cast<uint32_t>(Ext::TransparencyMode::Sorted) == CNA_TRANSPARENCY_MODE_SORTED &&
    static_cast<uint32_t>(Ext::TransparencyMode::OrderIndependent) ==
        CNA_TRANSPARENCY_MODE_ORDER_INDEPENDENT);
static_assert(
    static_cast<uint32_t>(Ext::PbrTextureSlot::BaseColor) == CNA_PBR_TEXTURE_BASE_COLOR &&
    static_cast<uint32_t>(Ext::PbrTextureSlot::SpecularColor) ==
        CNA_PBR_TEXTURE_SPECULAR_COLOR_EXT);
} // namespace

namespace {

struct TransparentDrawListResource final {
    std::shared_ptr<Ext::TransparentDrawList> value;
};

struct WeightedBlendedTransparencyResource final {
    std::shared_ptr<Ext::WeightedBlendedTransparency> value;
    CNA_Handle parentGame;
};

// CBIND-087D. A callback that fails must stop the draw and reach the caller unchanged, so the
// failure travels as an exception across the canonical std::function and is unwrapped here rather
// than being flattened into one generic result by the firewall.
struct TransparentDrawFailure final {
    CNA_Result result;
};

[[nodiscard]] CNA_Result RequireStruct(
    const uint32_t size, const uint32_t version, const uint32_t expectedSize, const char* what)
{
    if (size != expectedSize || version == 0U) {
        return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, what);
    }
    return CNA_RESULT_SUCCESS;
}

} // namespace

#define CNA_WITH_DRAWLIST(handle, body)                                                            \
    WithMap<TransparentDrawListResource>(                                                          \
        (handle), ObjectKind::TransparentDrawList, "TransparentDrawList", body)
#define CNA_WITH_WBT(handle, body)                                                                 \
    WithMap<WeightedBlendedTransparencyResource>(                                                  \
        (handle), ObjectKind::WeightedBlendedTransparency, "WeightedBlendedTransparency", body)

CNA_Result cna_gltf_material_source_ext_init(CNA_GltfMaterialSourceEXT* const outSource)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSource == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, "The source is null.");
        }
        *outSource = CNA_GltfMaterialSourceEXT{};
        outSource->struct_size = static_cast<uint32_t>(sizeof(CNA_GltfMaterialSourceEXT));
        outSource->struct_version = UINT32_C(1);
        // The glTF specification's defaults, not this layer's: an omitted factor means these.
        outSource->base_color_factor = CNA_Vector4{1.0F, 1.0F, 1.0F, 1.0F};
        outSource->metallic_factor = 1.0F;
        outSource->roughness_factor = 1.0F;
        outSource->normal_scale = 1.0F;
        outSource->occlusion_strength = 1.0F;
        outSource->ior_ext = 1.5F;
        outSource->specular_factor_ext = 1.0F;
        outSource->specular_color_factor_ext = Vec3(1.0F, 1.0F, 1.0F);
        outSource->alpha_mode = CNA_ALPHA_MODE_OPAQUE_EXT;
        outSource->alpha_cutoff = 0.5F;
        for (int slot = 0; slot < CNA_PBR_TEXTURE_SLOT_COUNT; ++slot) {
            if (const CNA_Result result =
                    cna_texture_transform_ext_init(&outSource->texture_transforms_ext[slot]);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gltf_material_extension_source_ext_init(
    CNA_GltfMaterialExtensionSourceEXT* const outSource)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSource == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, "The source is null.");
        }
        *outSource = CNA_GltfMaterialExtensionSourceEXT{};
        outSource->struct_size =
            static_cast<uint32_t>(sizeof(CNA_GltfMaterialExtensionSourceEXT));
        outSource->struct_version = UINT32_C(1);
        outSource->attenuation_color_ext = Vec3(1.0F, 1.0F, 1.0F);
        outSource->iridescence_ior_ext = 1.3F;
        outSource->iridescence_thickness_minimum_ext = 100.0F;
        outSource->iridescence_thickness_maximum_ext = 400.0F;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gltf_material_textures_ext_init(CNA_GltfMaterialTexturesEXT* const outTextures)
{
    if (outTextures == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, "The textures are null.");
    }
    *outTextures = CNA_GltfMaterialTexturesEXT{};
    outTextures->struct_size = static_cast<uint32_t>(sizeof(CNA_GltfMaterialTexturesEXT));
    outTextures->struct_version = UINT32_C(1);
    for (int slot = 0; slot < CNA_PBR_TEXTURE_SLOT_COUNT; ++slot) {
        outTextures->slots[slot] = CNA_INVALID_HANDLE;
    }
    return CNA_RESULT_SUCCESS;
}

CNA_Result cna_gltf_material_extension_textures_ext_init(
    CNA_GltfMaterialExtensionTexturesEXT* const outTextures)
{
    if (outTextures == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, "The textures are null.");
    }
    *outTextures = CNA_GltfMaterialExtensionTexturesEXT{};
    outTextures->struct_size =
        static_cast<uint32_t>(sizeof(CNA_GltfMaterialExtensionTexturesEXT));
    outTextures->struct_version = UINT32_C(1);
    return CNA_RESULT_SUCCESS;
}

CNA_Result cna_gltf_material_bridge_build_material(
    const CNA_GltfMaterialSourceEXT* const source,
    const CNA_GltfMaterialTexturesEXT* const textures,
    CNA_PbrMaterialEXT* const outMaterial)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (source == nullptr || textures == nullptr || outMaterial == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "A bridge argument is null.");
        }
        if (const CNA_Result result = RequireStruct(
                source->struct_size, source->struct_version,
                static_cast<uint32_t>(sizeof(CNA_GltfMaterialSourceEXT)),
                "The glTF material source is malformed.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = RequireStruct(
                textures->struct_size, textures->struct_version,
                static_cast<uint32_t>(sizeof(CNA_GltfMaterialTexturesEXT)),
                "The glTF texture set is malformed.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        // The canonical bridge is written against a concept, so the C source structure is turned
        // into a local type that satisfies it. The concept is the contract; this is how C meets it.
        struct Source final {
            Microsoft::Xna::Framework::Vector4 baseColorFactor;
            float metallicFactor;
            float roughnessFactor;
            Microsoft::Xna::Framework::Vector3 emissiveFactor;
            float normalScale;
            float occlusionStrength;
            float iorEXT;
            float specularFactorEXT;
            Microsoft::Xna::Framework::Vector3 specularColorFactorEXT;
            Microsoft::Xna::Framework::Graphics::AlphaModeEXT alphaMode;
            float alphaCutoff;
            bool doubleSided;
            std::array<int, Ext::kPbrTextureSlotCount> textureCoordinateSetsEXT;
            std::array<Microsoft::Xna::Framework::Graphics::TextureTransformEXT,
                       Ext::kPbrTextureSlotCount> textureTransformsEXT;
        } native{};

        native.baseColorFactor = {
            source->base_color_factor.x, source->base_color_factor.y,
            source->base_color_factor.z, source->base_color_factor.w};
        native.metallicFactor = source->metallic_factor;
        native.roughnessFactor = source->roughness_factor;
        native.emissiveFactor = ToNativeVector3(source->emissive_factor);
        native.normalScale = source->normal_scale;
        native.occlusionStrength = source->occlusion_strength;
        native.iorEXT = source->ior_ext;
        native.specularFactorEXT = source->specular_factor_ext;
        native.specularColorFactorEXT = ToNativeVector3(source->specular_color_factor_ext);
        if (source->alpha_mode > UINT32_C(2)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The alpha mode is not a defined CNA_ALPHA_MODE_EXT_* value.");
        }
        native.alphaMode =
            static_cast<Microsoft::Xna::Framework::Graphics::AlphaModeEXT>(source->alpha_mode);
        native.alphaCutoff = source->alpha_cutoff;
        if (source->double_sided != CNA_TRUE && source->double_sided != CNA_FALSE) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "double_sided must be CNA_TRUE or CNA_FALSE.");
        }
        native.doubleSided = source->double_sided == CNA_TRUE;

        Ext::GltfMaterialTexturesEXT nativeTextures{};
        for (int slot = 0; slot < Ext::kPbrTextureSlotCount; ++slot) {
            native.textureCoordinateSetsEXT[static_cast<std::size_t>(slot)] =
                source->texture_coordinate_sets_ext[slot];
            const CNA_TextureTransformEXT& t = source->texture_transforms_ext[slot];
            auto& nativeTransform = native.textureTransformsEXT[static_cast<std::size_t>(slot)];
            nativeTransform.Offset = {t.offset.x, t.offset.y};
            nativeTransform.Scale = {t.scale.x, t.scale.y};
            nativeTransform.Rotation = t.rotation;
            Microsoft::Xna::Framework::Graphics::Texture2D* texture = nullptr;
            std::shared_ptr<Texture2DResource> retention;
            if (const CNA_Result result = ResolveTexture2DArgument(
                    textures->slots[slot], "glTF texture slot", &texture, &retention);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            nativeTextures.Slots[static_cast<std::size_t>(slot)] = texture;
        }

        const Ext::PbrMaterial built = Ext::materialFromGltfEXT(native, nativeTextures);
        FromNativePbrMaterial(built, outMaterial);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gltf_material_bridge_build_extensions(
    const CNA_GltfMaterialExtensionSourceEXT* const source,
    const CNA_GltfMaterialExtensionTexturesEXT* const textures,
    const CNA_PbrMaterialExtensionsHandle outExtensions)
{
    return WithMap<PbrMaterialExtensionsResource>(
        outExtensions, ObjectKind::PbrMaterialExtensions, "PbrMaterialExtensions",
        [&](const std::shared_ptr<PbrMaterialExtensionsResource>& e) -> CNA_Result {
            if (source == nullptr || textures == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "A bridge argument is null.");
            }
            if (const CNA_Result result = RequireStruct(
                    source->struct_size, source->struct_version,
                    static_cast<uint32_t>(sizeof(CNA_GltfMaterialExtensionSourceEXT)),
                    "The glTF extension source is malformed.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (const CNA_Result result = RequireStruct(
                    textures->struct_size, textures->struct_version,
                    static_cast<uint32_t>(sizeof(CNA_GltfMaterialExtensionTexturesEXT)),
                    "The glTF extension texture set is malformed.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            struct Source final {
                float clearcoatFactorEXT;
                float clearcoatRoughnessFactorEXT;
                Microsoft::Xna::Framework::Vector3 sheenColorFactorEXT;
                float sheenRoughnessFactorEXT;
                float transmissionFactorEXT;
                float thicknessFactorEXT;
                float attenuationDistanceEXT;
                Microsoft::Xna::Framework::Vector3 attenuationColorEXT;
                float iridescenceFactorEXT;
                float iridescenceIorEXT;
                float iridescenceThicknessMinimumEXT;
                float iridescenceThicknessMaximumEXT;
            } native{};
            native.clearcoatFactorEXT = source->clearcoat_factor_ext;
            native.clearcoatRoughnessFactorEXT = source->clearcoat_roughness_factor_ext;
            native.sheenColorFactorEXT = ToNativeVector3(source->sheen_color_factor_ext);
            native.sheenRoughnessFactorEXT = source->sheen_roughness_factor_ext;
            native.transmissionFactorEXT = source->transmission_factor_ext;
            native.thicknessFactorEXT = source->thickness_factor_ext;
            native.attenuationDistanceEXT = source->attenuation_distance_ext;
            native.attenuationColorEXT = ToNativeVector3(source->attenuation_color_ext);
            native.iridescenceFactorEXT = source->iridescence_factor_ext;
            native.iridescenceIorEXT = source->iridescence_ior_ext;
            native.iridescenceThicknessMinimumEXT = source->iridescence_thickness_minimum_ext;
            native.iridescenceThicknessMaximumEXT = source->iridescence_thickness_maximum_ext;

            Ext::GltfMaterialExtensionTexturesEXT nativeTextures{};
            const CNA_Handle handles[] = {
                textures->clearcoat, textures->clearcoat_roughness, textures->clearcoat_normal,
                textures->sheen_color, textures->sheen_roughness, textures->transmission,
                textures->thickness, textures->iridescence, textures->iridescence_thickness};
            Microsoft::Xna::Framework::Graphics::Texture2D** const slots[] = {
                &nativeTextures.Clearcoat, &nativeTextures.ClearcoatRoughness,
                &nativeTextures.ClearcoatNormal, &nativeTextures.SheenColor,
                &nativeTextures.SheenRoughness, &nativeTextures.Transmission,
                &nativeTextures.Thickness, &nativeTextures.Iridescence,
                &nativeTextures.IridescenceThickness};
            for (std::size_t slot = 0; slot < std::size(handles); ++slot) {
                Microsoft::Xna::Framework::Graphics::Texture2D* texture = nullptr;
                std::shared_ptr<Texture2DResource> retention;
                if (const CNA_Result result = ResolveTexture2DArgument(
                        handles[slot], "glTF extension texture", &texture, &retention);
                    result != CNA_RESULT_SUCCESS) {
                    return result;
                }
                *slots[slot] = texture;
                if (texture != nullptr && retention != nullptr) {
                    e->textureParentGame = retention->parentGame;
                }
            }
            *e->value = Ext::materialExtensionsFromGltfEXT(native, nativeTextures);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_transparent_draw_list_create(CNA_TransparentDrawListHandle* const outList)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outList == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The draw-list output handle is null.");
        }
        *outList = CNA_INVALID_HANDLE;
        const auto resource = std::make_shared<TransparentDrawListResource>(
            TransparentDrawListResource{std::make_shared<Ext::TransparentDrawList>()});
        const CNA_Result result =
            GetRuntimeHandles().Create(ObjectKind::TransparentDrawList, resource, outList);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned draw-list handle could not be created.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_transparent_draw_list_destroy(const CNA_TransparentDrawListHandle listHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<TransparentDrawListResource> list;
        if (const CNA_Result result = GetEngineResource(
                listHandle, ObjectKind::TransparentDrawList, "TransparentDrawList", &list);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(listHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The owned draw-list handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_transparent_draw_list_clear(const CNA_TransparentDrawListHandle list)
{
    return CNA_WITH_DRAWLIST(list,
        [](const std::shared_ptr<TransparentDrawListResource>& l) -> CNA_Result {
            l->value->clear();
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_transparent_draw_list_submit(
    const CNA_TransparentDrawListHandle list,
    const CNA_BoundingBox* const bounds,
    const CNA_TransparentDrawCallback draw,
    void* const context)
{
    return CNA_WITH_DRAWLIST(list,
        [&](const std::shared_ptr<TransparentDrawListResource>& l) -> CNA_Result {
            if (bounds == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The bounds are null.");
            }
            // The canonical submit() refuses an empty callable; in C a null function pointer is
            // that same mistake, and it is refused here rather than stored and discovered later.
            if (draw == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "There is nothing to draw.");
            }
            BoundingBox nativeBounds;
            if (const CNA_Result result = ToNativeBounds(bounds, &nativeBounds);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            l->value->submit(nativeBounds, [draw, context]() {
                const CNA_Result result = draw(context);
                if (result != CNA_RESULT_SUCCESS) {
                    throw TransparentDrawFailure{result};
                }
            });
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_transparent_draw_list_get_count(
    const CNA_TransparentDrawListHandle list, uint64_t* const outCount)
{
    return CNA_WITH_DRAWLIST(list,
        [&](const std::shared_ptr<TransparentDrawListResource>& l) -> CNA_Result {
            return StoreValue(outCount, static_cast<uint64_t>(l->value->getCount()));
        });
}

CNA_Result cna_transparent_draw_list_draw_sorted(
    const CNA_TransparentDrawListHandle list, const CNA_Matrix* const view)
{
    return CNA_WITH_DRAWLIST(list,
        [&](const std::shared_ptr<TransparentDrawListResource>& l) -> CNA_Result {
            if (const CNA_Result result = RequireMatrixArgument(view, "The view is null.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            // A failing callback stops the draw and its own result reaches the caller unchanged,
            // so a caller learns which draw failed instead of finding a partly drawn frame.
            try {
                l->value->drawSorted(ToNativeMatrix(*view));
            } catch (const TransparentDrawFailure& failure) {
                return failure.result;
            }
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_transparent_draw_list_copy_sorted_order_ext(
    const CNA_TransparentDrawListHandle list,
    const CNA_Matrix* const view,
    int32_t* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    std::shared_ptr<TransparentDrawListResource> resource;
    if (const CNA_Result result = GetEngineResource(
            list, ObjectKind::TransparentDrawList, "TransparentDrawList", &resource);
        result != CNA_RESULT_SUCCESS) {
        if (outCount != nullptr) {
            *outCount = UINT64_C(0);
        }
        return result;
    }
    if (view == nullptr) {
        if (outCount != nullptr) {
            *outCount = UINT64_C(0);
        }
        return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, "The view is null.");
    }
    const std::vector<int> order = resource->value->getSortedOrderEXT(ToNativeMatrix(*view));
    return CopyInt32Range(order, destination, capacity, outCount);
}

CNA_Result cna_transparent_draw_list_sort_key(
    const CNA_BoundingBox* const bounds,
    const CNA_Vector3* const cameraPosition,
    float* const outKey)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (bounds == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, "The bounds are null.");
        }
        if (const CNA_Result result =
                RequireVector3Argument(cameraPosition, "The camera position is null.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        BoundingBox nativeBounds;
        if (const CNA_Result result = ToNativeBounds(bounds, &nativeBounds);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return StoreValue(
            outKey,
            Ext::TransparentDrawList::sortKey(nativeBounds, ToNativeVector3(*cameraPosition)));
    });
}

CNA_Result cna_transparent_draw_list_camera_position_of(
    const CNA_Matrix* const view, CNA_Vector3* const outPosition)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = RequireMatrixArgument(view, "The view is null.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto v = Ext::TransparentDrawList::cameraPositionOf(ToNativeMatrix(*view));
        return StoreValue(outPosition, Vec3(v.X, v.Y, v.Z));
    });
}

CNA_Result cna_weighted_blended_transparency_create(
    const CNA_Handle graphicsDeviceHandle,
    const int32_t width,
    const int32_t height,
    CNA_WeightedBlendedTransparencyHandle* const outTransparency)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTransparency == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The transparency output handle is null.");
        }
        *outTransparency = CNA_INVALID_HANDLE;
        if (width <= 0 || height <= 0) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "The target size must be positive.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result =
                GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto native = std::make_shared<Ext::WeightedBlendedTransparency>(
            *graphicsDevice->value, static_cast<int>(width), static_cast<int>(height));
        const auto resource = std::make_shared<WeightedBlendedTransparencyResource>(
            WeightedBlendedTransparencyResource{std::move(native), graphicsDevice->parentGame});
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::WeightedBlendedTransparency, resource, outTransparency);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned transparency handle could not be created.");
        }
        AddOwnedGraphicsResourceFor(graphicsDevice->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_weighted_blended_transparency_destroy(
    const CNA_WeightedBlendedTransparencyHandle transparencyHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<WeightedBlendedTransparencyResource> transparency;
        if (const CNA_Result result = GetEngineResource(
                transparencyHandle, ObjectKind::WeightedBlendedTransparency,
                "WeightedBlendedTransparency", &transparency);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(transparencyHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The owned transparency handle could not be released.");
        }
        RemoveOwnedGraphicsResourceFor(transparency->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_weighted_blended_transparency_is_supported(
    const CNA_WeightedBlendedTransparencyHandle transparency, CNA_Bool* const outSupported)
{
    return CNA_WITH_WBT(transparency,
        [&](const std::shared_ptr<WeightedBlendedTransparencyResource>& t) -> CNA_Result {
            return StoreValue(
                outSupported,
                static_cast<CNA_Bool>(t->value->isSupported() ? CNA_TRUE : CNA_FALSE));
        });
}

CNA_Result cna_weighted_blended_transparency_copy_unsupported_reason(
    const CNA_WeightedBlendedTransparencyHandle transparency,
    char* const destination, const uint64_t capacity, uint64_t* const outBytes)
{
    std::shared_ptr<WeightedBlendedTransparencyResource> resource;
    if (const CNA_Result result = GetEngineResource(
            transparency, ObjectKind::WeightedBlendedTransparency, "WeightedBlendedTransparency",
            &resource);
        result != CNA_RESULT_SUCCESS) {
        if (outBytes != nullptr) {
            *outBytes = UINT64_C(0);
        }
        return result;
    }
    return CopyFormattedString(destination, capacity, outBytes, [&resource] {
        return resource->value->getUnsupportedReason();
    });
}

CNA_Result cna_weighted_blended_transparency_resize(
    const CNA_WeightedBlendedTransparencyHandle transparency,
    const int32_t width,
    const int32_t height)
{
    return CNA_WITH_WBT(transparency,
        [&](const std::shared_ptr<WeightedBlendedTransparencyResource>& t) -> CNA_Result {
            // A bad size is an argument mistake and an open bracket is a sequencing mistake; the
            // canonical code throws two different exception types and they stay apart here.
            if (width <= 0 || height <= 0) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_RANGE,
                    "The size must be positive.");
            }
            if (t->value->isAccumulating()) {
                return Fail(
                    CNA_RESULT_INVALID_STATE,
                    CNA_ERROR_CATEGORY_STATE,
                    "Accumulation is open.");
            }
            t->value->resize(static_cast<int>(width), static_cast<int>(height));
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_weighted_blended_transparency_begin(
    const CNA_WeightedBlendedTransparencyHandle transparency, const float farPlane)
{
    return CNA_WITH_WBT(transparency,
        [&](const std::shared_ptr<WeightedBlendedTransparencyResource>& t) -> CNA_Result {
            if (!(farPlane > 0.0F)) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_RANGE,
                    "The far plane must be positive.");
            }
            if (t->value->isAccumulating()) {
                return Fail(
                    CNA_RESULT_INVALID_STATE,
                    CNA_ERROR_CATEGORY_STATE,
                    "Accumulation is already open.");
            }
            // On a renderer that cannot run the resolve this opens nothing, so isAccumulating()
            // stays false and a matching end() refuses. Reproduced rather than corrected; see
            // plans/plan_binding.md CBIND-098.
            t->value->begin(farPlane);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_weighted_blended_transparency_end(
    const CNA_WeightedBlendedTransparencyHandle transparency)
{
    return CNA_WITH_WBT(transparency,
        [&](const std::shared_ptr<WeightedBlendedTransparencyResource>& t) -> CNA_Result {
            if (!t->value->isAccumulating()) {
                return Fail(
                    CNA_RESULT_INVALID_STATE,
                    CNA_ERROR_CATEGORY_STATE,
                    "Accumulation is not open.");
            }
            t->value->end();
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_weighted_blended_transparency_resolve(
    const CNA_WeightedBlendedTransparencyHandle transparency,
    const int32_t width,
    const int32_t height)
{
    return CNA_WITH_WBT(transparency,
        [&](const std::shared_ptr<WeightedBlendedTransparencyResource>& t) -> CNA_Result {
            if (width <= 0 || height <= 0) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_RANGE,
                    "The size must be positive.");
            }
            if (t->value->isAccumulating()) {
                return Fail(
                    CNA_RESULT_INVALID_STATE,
                    CNA_ERROR_CATEGORY_STATE,
                    "Accumulation is still open.");
            }
            t->value->resolve(static_cast<int>(width), static_cast<int>(height));
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_weighted_blended_transparency_is_accumulating(
    const CNA_WeightedBlendedTransparencyHandle transparency, CNA_Bool* const outAccumulating)
{
    return CNA_WITH_WBT(transparency,
        [&](const std::shared_ptr<WeightedBlendedTransparencyResource>& t) -> CNA_Result {
            return StoreValue(
                outAccumulating,
                static_cast<CNA_Bool>(t->value->isAccumulating() ? CNA_TRUE : CNA_FALSE));
        });
}

CNA_Result cna_weighted_blended_transparency_get_accumulation_texture_ext(
    const CNA_WeightedBlendedTransparencyHandle transparency, CNA_Handle* const outTexture)
{
    return CNA_WITH_WBT(transparency,
        [&](const std::shared_ptr<WeightedBlendedTransparencyResource>& t) -> CNA_Result {
            if (outTexture == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The texture output handle is null.");
            }
            *outTexture = CNA_INVALID_HANDLE;
            auto* const texture = t->value->getAccumulationTextureEXT();
            if (texture == nullptr) {
                return CNA_RESULT_SUCCESS;
            }
            const std::shared_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> view(t, texture);
            return CreateBorrowedRenderTarget2D(view, t->parentGame, t, outTexture);
        });
}

CNA_Result cna_weighted_blended_transparency_get_revealage_texture_ext(
    const CNA_WeightedBlendedTransparencyHandle transparency, CNA_Handle* const outTexture)
{
    return CNA_WITH_WBT(transparency,
        [&](const std::shared_ptr<WeightedBlendedTransparencyResource>& t) -> CNA_Result {
            if (outTexture == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The texture output handle is null.");
            }
            *outTexture = CNA_INVALID_HANDLE;
            auto* const texture = t->value->getRevealageTextureEXT();
            if (texture == nullptr) {
                return CNA_RESULT_SUCCESS;
            }
            const std::shared_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> view(t, texture);
            return CreateBorrowedRenderTarget2D(view, t->parentGame, t, outTexture);
        });
}

CNA_Result cna_weighted_blended_transparency_copy_accumulation_glsl(
    char* const destination, const uint64_t capacity, uint64_t* const outBytes)
{
    return CopyFormattedString(destination, capacity, outBytes, [] {
        return Ext::WeightedBlendedTransparency::getAccumulationGlsl();
    });
}

CNA_Result cna_weighted_blended_transparency_weight(
    const float viewDepth, const float alpha, const float farPlane, float* const outWeight)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        // Both clamps are corrections: the curve is unbounded near zero depth, and a weight that
        // overflows poisons the whole accumulation buffer rather than one fragment.
        return StoreValue(
            outWeight, Ext::WeightedBlendedTransparency::weight(viewDepth, alpha, farPlane));
    });
}

namespace {

// CBIND-088A. Every field goes through its canonical setter, which is what preserves the
// thirty-one corrections a plain structure copy would lose.
[[nodiscard]] CNA_Result ToNativeRenderPipelineSettings(
    const CNA_RenderPipelineSettingsEXT& value, Ext::RenderPipelineSettings* const out)
{
    if (value.struct_size != sizeof(CNA_RenderPipelineSettingsEXT) ||
        value.struct_version == UINT32_C(0)) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The render-pipeline settings structure is malformed.");
    }
    if (value.hdr_enabled != CNA_TRUE && value.hdr_enabled != CNA_FALSE) {
        return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT,
                    "hdr_enabled must be CNA_TRUE or CNA_FALSE.");
    }
    out->setHDREnabled(value.hdr_enabled == CNA_TRUE);
    out->setExposure(value.exposure);
    out->setGamma(value.gamma);
    if (value.tonemapping_mode > UINT32_C(3)) {
        return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT,
                    "tonemapping_mode is not a defined identity.");
    }
    out->setTonemappingMode(static_cast<Ext::TonemappingMode>(value.tonemapping_mode));
    if (value.bloom_enabled != CNA_TRUE && value.bloom_enabled != CNA_FALSE) {
        return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT,
                    "bloom_enabled must be CNA_TRUE or CNA_FALSE.");
    }
    out->setBloomEnabled(value.bloom_enabled == CNA_TRUE);
    out->setBloomIntensity(value.bloom_intensity);
    out->setBloomThreshold(value.bloom_threshold);
    out->setBloomIterations(static_cast<int>(value.bloom_iterations));
    if (value.ssao_enabled != CNA_TRUE && value.ssao_enabled != CNA_FALSE) {
        return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT,
                    "ssao_enabled must be CNA_TRUE or CNA_FALSE.");
    }
    out->setSSAOEnabled(value.ssao_enabled == CNA_TRUE);
    if (value.transparency_mode > UINT32_C(2)) {
        return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT,
                    "transparency_mode is not a defined identity.");
    }
    out->setTransparencyMode(static_cast<Ext::TransparencyMode>(value.transparency_mode));
    out->setSSAORadius(value.ssao_radius);
    out->setSSAOIntensity(value.ssao_intensity);
    out->setSSAOSampleCount(static_cast<int>(value.ssao_sample_count));
    if (value.ssr_enabled != CNA_TRUE && value.ssr_enabled != CNA_FALSE) {
        return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT,
                    "ssr_enabled must be CNA_TRUE or CNA_FALSE.");
    }
    out->setSSREnabled(value.ssr_enabled == CNA_TRUE);
    out->setSSRMaxDistance(value.ssr_max_distance);
    out->setSSRStepCount(static_cast<int>(value.ssr_step_count));
    out->setSSRThickness(value.ssr_thickness);
    out->setSSRDepthBias(value.ssr_depth_bias);
    out->setSSREdgeFade(value.ssr_edge_fade);
    out->setVolumetricFogDensity(value.volumetric_fog_density);
    out->setLightShaftThreshold(value.light_shaft_threshold);
    out->setLightShaftIntensity(value.light_shaft_intensity);
    out->setLightShaftDecay(value.light_shaft_decay);
    out->setHeightFogDensity(value.height_fog_density);
    out->setHeightFogFalloff(value.height_fog_falloff);
    out->setHeightFogBaseHeight(value.height_fog_base_height);
    out->setMotionBlurStrength(value.motion_blur_strength);
    out->setMotionBlurMaxDistance(value.motion_blur_max_distance);
    out->setChromaticAberrationStrength(value.chromatic_aberration_strength);
    out->setFilmGrainIntensity(value.film_grain_intensity);
    out->setLensFlareThreshold(value.lens_flare_threshold);
    out->setLensFlareIntensity(value.lens_flare_intensity);
    out->setLensFlareDispersal(value.lens_flare_dispersal);
    if (value.color_grade_enabled != CNA_TRUE && value.color_grade_enabled != CNA_FALSE) {
        return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT,
                    "color_grade_enabled must be CNA_TRUE or CNA_FALSE.");
    }
    out->setColorGradeEnabled(value.color_grade_enabled == CNA_TRUE);
    out->setColorGradeStrength(value.color_grade_strength);
    if (value.dof_enabled != CNA_TRUE && value.dof_enabled != CNA_FALSE) {
        return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT,
                    "dof_enabled must be CNA_TRUE or CNA_FALSE.");
    }
    out->setDOFEnabled(value.dof_enabled == CNA_TRUE);
    out->setDOFFocusDistance(value.dof_focus_distance);
    out->setDOFFocalLength(value.dof_focal_length);
    out->setDOFFNumber(value.doff_number);
    out->setDOFMaxRadius(value.dof_max_radius);
    out->setSSRRoughnessBlur(value.ssr_roughness_blur);
    out->setSSRIntensity(value.ssr_intensity);
    if (value.fxaa_enabled != CNA_TRUE && value.fxaa_enabled != CNA_FALSE) {
        return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT,
                    "fxaa_enabled must be CNA_TRUE or CNA_FALSE.");
    }
    out->setFXAAEnabled(value.fxaa_enabled == CNA_TRUE);
    out->setFXAAEdgeThresholdEXT(value.fxaa_edge_threshold_ext);
    if (value.render_quality > UINT32_C(3)) {
        return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT,
                    "render_quality is not a defined identity.");
    }
    out->setRenderQuality(static_cast<Ext::RenderQuality>(value.render_quality));
    if (value.shadow_quality > UINT32_C(3)) {
        return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT,
                    "shadow_quality is not a defined identity.");
    }
    out->setShadowQuality(static_cast<Ext::ShadowQuality>(value.shadow_quality));
    if (value.shadows_enabled != CNA_TRUE && value.shadows_enabled != CNA_FALSE) {
        return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT,
                    "shadows_enabled must be CNA_TRUE or CNA_FALSE.");
    }
    out->setShadowsEnabled(value.shadows_enabled == CNA_TRUE);
    return CNA_RESULT_SUCCESS;
}

void FromNativeRenderPipelineSettings(
    const Ext::RenderPipelineSettings& value, CNA_RenderPipelineSettingsEXT* const out)
{
    out->hdr_enabled = static_cast<CNA_Bool>(value.isHDREnabled() ? CNA_TRUE : CNA_FALSE);
    out->exposure = value.getExposure();
    out->gamma = value.getGamma();
    out->tonemapping_mode = static_cast<CNA_TonemappingMode>(value.getTonemappingMode());
    out->bloom_enabled = static_cast<CNA_Bool>(value.isBloomEnabled() ? CNA_TRUE : CNA_FALSE);
    out->bloom_intensity = value.getBloomIntensity();
    out->bloom_threshold = value.getBloomThreshold();
    out->bloom_iterations = static_cast<int32_t>(value.getBloomIterations());
    out->ssao_enabled = static_cast<CNA_Bool>(value.isSSAOEnabled() ? CNA_TRUE : CNA_FALSE);
    out->transparency_mode = static_cast<CNA_TransparencyMode>(value.getTransparencyMode());
    out->ssao_radius = value.getSSAORadius();
    out->ssao_intensity = value.getSSAOIntensity();
    out->ssao_sample_count = static_cast<int32_t>(value.getSSAOSampleCount());
    out->ssr_enabled = static_cast<CNA_Bool>(value.isSSREnabled() ? CNA_TRUE : CNA_FALSE);
    out->ssr_max_distance = value.getSSRMaxDistance();
    out->ssr_step_count = static_cast<int32_t>(value.getSSRStepCount());
    out->ssr_thickness = value.getSSRThickness();
    out->ssr_depth_bias = value.getSSRDepthBias();
    out->ssr_edge_fade = value.getSSREdgeFade();
    out->volumetric_fog_density = value.getVolumetricFogDensity();
    out->light_shaft_threshold = value.getLightShaftThreshold();
    out->light_shaft_intensity = value.getLightShaftIntensity();
    out->light_shaft_decay = value.getLightShaftDecay();
    out->height_fog_density = value.getHeightFogDensity();
    out->height_fog_falloff = value.getHeightFogFalloff();
    out->height_fog_base_height = value.getHeightFogBaseHeight();
    out->motion_blur_strength = value.getMotionBlurStrength();
    out->motion_blur_max_distance = value.getMotionBlurMaxDistance();
    out->chromatic_aberration_strength = value.getChromaticAberrationStrength();
    out->film_grain_intensity = value.getFilmGrainIntensity();
    out->lens_flare_threshold = value.getLensFlareThreshold();
    out->lens_flare_intensity = value.getLensFlareIntensity();
    out->lens_flare_dispersal = value.getLensFlareDispersal();
    out->color_grade_enabled = static_cast<CNA_Bool>(value.isColorGradeEnabled() ? CNA_TRUE : CNA_FALSE);
    out->color_grade_strength = value.getColorGradeStrength();
    out->dof_enabled = static_cast<CNA_Bool>(value.isDOFEnabled() ? CNA_TRUE : CNA_FALSE);
    out->dof_focus_distance = value.getDOFFocusDistance();
    out->dof_focal_length = value.getDOFFocalLength();
    out->doff_number = value.getDOFFNumber();
    out->dof_max_radius = value.getDOFMaxRadius();
    out->ssr_roughness_blur = value.getSSRRoughnessBlur();
    out->ssr_intensity = value.getSSRIntensity();
    out->fxaa_enabled = static_cast<CNA_Bool>(value.isFXAAEnabled() ? CNA_TRUE : CNA_FALSE);
    out->fxaa_edge_threshold_ext = value.getFXAAEdgeThresholdEXT();
    out->render_quality = static_cast<CNA_RenderQuality>(value.getRenderQuality());
    out->shadow_quality = static_cast<CNA_ShadowQuality>(value.getShadowQuality());
    out->shadows_enabled = static_cast<CNA_Bool>(value.isShadowsEnabled() ? CNA_TRUE : CNA_FALSE);
}

// The C identities must name the canonical ordinals and the canonical minima.
static_assert(
    CNA_RENDER_PIPELINE_MINIMUM_GAMMA_EXT == Ext::RenderPipelineSettings::kMinimumGamma &&
    CNA_RENDER_PIPELINE_MINIMUM_FXAA_EDGE_THRESHOLD_EXT ==
        Ext::RenderPipelineSettings::kMinimumFxaaEdgeThreshold);
// CBIND-090 owns TonemappingMode::Uncharted2 and will add the fifth identity with it. Until then
// ToNativeRenderPipelineSettings refuses an ordinal C cannot name, and this assertion is what
// stops the bound from being widened without the macro that gives the value a name.
static_assert(static_cast<uint32_t>(Ext::TonemappingMode::Aces) == CNA_TONEMAPPING_MODE_ACES);

} // namespace

CNA_Result cna_render_pipeline_settings_ext_init(
    CNA_RenderPipelineSettingsEXT* const outSettings)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSettings == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The settings output is null.");
        }
        *outSettings = CNA_RenderPipelineSettingsEXT{};
        outSettings->struct_size = static_cast<uint32_t>(sizeof(CNA_RenderPipelineSettingsEXT));
        outSettings->struct_version = UINT32_C(1);
        const Ext::RenderPipelineSettings defaults;
        FromNativeRenderPipelineSettings(defaults, outSettings);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_render_pipeline_settings_ext_normalize(
    CNA_RenderPipelineSettingsEXT* const settings)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (settings == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, "The settings are null.");
        }
        Ext::RenderPipelineSettings native;
        if (const CNA_Result result = ToNativeRenderPipelineSettings(*settings, &native);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        FromNativeRenderPipelineSettings(native, settings);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_render_pipeline_settings_ext_apply_render_quality_preset(
    CNA_RenderPipelineSettingsEXT* const settings)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (settings == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, "The settings are null.");
        }
        Ext::RenderPipelineSettings native;
        if (const CNA_Result result = ToNativeRenderPipelineSettings(*settings, &native);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        native.applyRenderQualityPresetEXT();
        FromNativeRenderPipelineSettings(native, settings);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_render_pipeline_settings_ext_apply_from_string(
    CNA_RenderPipelineSettingsEXT* const settings,
    const CNA_StringView text,
    int32_t* const outApplied)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (settings == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, "The settings are null.");
        }
        // Embedded NULs are rejected: the canonical parser reads the whole text, so a NUL would
        // silently truncate what a caller believes it applied.
        if (const CNA_Result result = CNA::C::Detail::ValidateStringView(text, true);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Ext::RenderPipelineSettings native;
        if (const CNA_Result result = ToNativeRenderPipelineSettings(*settings, &native);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::string parsed(
            text.data == nullptr ? "" : text.data, static_cast<std::size_t>(text.byte_length));
        const int applied = native.applyFromStringEXT(parsed);
        FromNativeRenderPipelineSettings(native, settings);
        return StoreValue(outApplied, static_cast<int32_t>(applied));
    });
}

namespace {

struct RenderPipelineResource final {
    std::shared_ptr<Ext::RenderPipeline> value;
    CNA_Handle parentGame;
    // CBIND-088B. The canonical begin() throws logic_error for two different states -- a frame
    // already open, and a pipeline that has never been sized -- and the exception type alone
    // cannot separate them. The pipeline exposes no width accessor, so the C layer remembers
    // whether resize() ever succeeded and answers the two with their own messages.
    bool hasBeenSized = false;
};

// CBIND-088B. A draw callback that fails must stop the frame and reach the caller unchanged, the
// same shape CBIND-087D's draw list uses.
struct RenderPipelineDrawFailure final {
    CNA_Result result;
};

} // namespace

#define CNA_WITH_PIPELINE(handle, body)                                                            \
    WithMap<RenderPipelineResource>(                                                               \
        (handle), ObjectKind::RenderPipeline, "RenderPipeline", body)

CNA_Result cna_render_pipeline_create(
    const CNA_Handle graphicsDeviceHandle, CNA_RenderPipelineHandle* const outPipeline)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPipeline == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The pipeline output handle is null.");
        }
        *outPipeline = CNA_INVALID_HANDLE;
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result =
                GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto native = std::make_shared<Ext::RenderPipeline>(*graphicsDevice->value);
        const auto resource = std::make_shared<RenderPipelineResource>(
            RenderPipelineResource{std::move(native), graphicsDevice->parentGame, false});
        const CNA_Result result =
            GetRuntimeHandles().Create(ObjectKind::RenderPipeline, resource, outPipeline);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned pipeline handle could not be created.");
        }
        AddOwnedGraphicsResourceFor(graphicsDevice->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_render_pipeline_destroy(const CNA_RenderPipelineHandle pipelineHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<RenderPipelineResource> pipeline;
        if (const CNA_Result result = GetEngineResource(
                pipelineHandle, ObjectKind::RenderPipeline, "RenderPipeline", &pipeline);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(pipelineHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The owned pipeline handle could not be released.");
        }
        RemoveOwnedGraphicsResourceFor(pipeline->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_render_pipeline_get_settings(
    const CNA_RenderPipelineHandle pipeline, CNA_RenderPipelineSettingsEXT* const outSettings)
{
    return CNA_WITH_PIPELINE(pipeline,
        [&](const std::shared_ptr<RenderPipelineResource>& p) -> CNA_Result {
            if (outSettings == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The settings output is null.");
            }
            // The canonical getter returns a reference into the pipeline; this copies, so the
            // result stays correct after the pipeline changes and there is no view to dangle.
            *outSettings = CNA_RenderPipelineSettingsEXT{};
            outSettings->struct_size =
                static_cast<uint32_t>(sizeof(CNA_RenderPipelineSettingsEXT));
            outSettings->struct_version = UINT32_C(1);
            FromNativeRenderPipelineSettings(p->value->getSettings(), outSettings);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_render_pipeline_set_settings(
    const CNA_RenderPipelineHandle pipeline,
    const CNA_RenderPipelineSettingsEXT* const settings)
{
    return CNA_WITH_PIPELINE(pipeline,
        [&](const std::shared_ptr<RenderPipelineResource>& p) -> CNA_Result {
            if (settings == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT,
                    "The settings are null.");
            }
            // Through the canonical setters, so CBIND-088A's thirty-one corrections apply here
            // exactly as they do to _normalize.
            return ToNativeRenderPipelineSettings(*settings, &p->value->getSettings());
        });
}

CNA_Result cna_render_pipeline_resize(
    const CNA_RenderPipelineHandle pipeline, const int32_t width, const int32_t height)
{
    return CNA_WITH_PIPELINE(pipeline,
        [&](const std::shared_ptr<RenderPipelineResource>& p) -> CNA_Result {
            if (width <= 0 || height <= 0) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_RANGE,
                    "The size must be positive.");
            }
            p->value->resize(static_cast<int>(width), static_cast<int>(height));
            p->hasBeenSized = true;
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_render_pipeline_begin(
    const CNA_RenderPipelineHandle pipeline, const CNA_Color* const clearColor)
{
    return CNA_WITH_PIPELINE(pipeline,
        [&](const std::shared_ptr<RenderPipelineResource>& p) -> CNA_Result {
            if (clearColor == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT,
                    "The clear colour is null.");
            }
            if (!p->hasBeenSized) {
                return Fail(
                    CNA_RESULT_INVALID_STATE,
                    CNA_ERROR_CATEGORY_STATE,
                    "The pipeline has never been sized; call resize first.");
            }
            try {
                p->value->begin(
                    Color(clearColor->r, clearColor->g, clearColor->b, clearColor->a));
            } catch (const std::logic_error&) {
                return Fail(
                    CNA_RESULT_INVALID_STATE,
                    CNA_ERROR_CATEGORY_STATE,
                    "A frame is already open.");
            }
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_render_pipeline_end(const CNA_RenderPipelineHandle pipeline)
{
    return CNA_WITH_PIPELINE(pipeline,
        [&](const std::shared_ptr<RenderPipelineResource>& p) -> CNA_Result {
            try {
                p->value->end();
            } catch (const RenderPipelineDrawFailure& failure) {
                return failure.result;
            } catch (const std::logic_error&) {
                return Fail(
                    CNA_RESULT_INVALID_STATE, CNA_ERROR_CATEGORY_STATE, "No frame is open.");
            }
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_render_pipeline_add_user_pass(
    const CNA_RenderPipelineHandle pipeline, const CNA_PostProcessPassHandle pass)
{
    return CNA_WITH_PIPELINE(pipeline,
        [&](const std::shared_ptr<RenderPipelineResource>& p) -> CNA_Result {
            std::shared_ptr<PostProcessPassResource> passResource;
            if (const CNA_Result result = GetEngineResource(
                    pass, ObjectKind::PostProcessPass, "PostProcessPass", &passResource);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            // Borrowed: the pipeline records the pass and never owns it.
            p->value->addUserPass(passResource->value.get());
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_render_pipeline_clear_user_passes(const CNA_RenderPipelineHandle pipeline)
{
    return CNA_WITH_PIPELINE(pipeline,
        [](const std::shared_ptr<RenderPipelineResource>& p) -> CNA_Result {
            p->value->clearUserPasses();
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_render_pipeline_set_depth_normal_inputs(
    const CNA_RenderPipelineHandle pipeline, const CNA_Handle depth, const CNA_Handle normals)
{
    return CNA_WITH_PIPELINE(pipeline,
        [&](const std::shared_ptr<RenderPipelineResource>& p) -> CNA_Result {
            Microsoft::Xna::Framework::Graphics::Texture2D* depthTexture = nullptr;
            Microsoft::Xna::Framework::Graphics::Texture2D* normalTexture = nullptr;
            std::shared_ptr<Texture2DResource> depthRetention;
            std::shared_ptr<Texture2DResource> normalRetention;
            if (const CNA_Result result = ResolveTexture2DArgument(
                    depth, "depth", &depthTexture, &depthRetention);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (const CNA_Result result = ResolveTexture2DArgument(
                    normals, "normals", &normalTexture, &normalRetention);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            p->value->setDepthNormalInputs(depthTexture, normalTexture);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_render_pipeline_set_velocity_input_ext(
    const CNA_RenderPipelineHandle pipeline, const CNA_Handle velocity)
{
    return CNA_WITH_PIPELINE(pipeline,
        [&](const std::shared_ptr<RenderPipelineResource>& p) -> CNA_Result {
            Microsoft::Xna::Framework::Graphics::Texture2D* texture = nullptr;
            std::shared_ptr<Texture2DResource> retention;
            if (const CNA_Result result =
                    ResolveTexture2DArgument(velocity, "velocity", &texture, &retention);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            p->value->setVelocityInputEXT(texture);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_render_pipeline_set_transparent_scene(
    const CNA_RenderPipelineHandle pipeline,
    const CNA_RenderPipelineDrawCallback draw,
    void* const context)
{
    return CNA_WITH_PIPELINE(pipeline,
        [&](const std::shared_ptr<RenderPipelineResource>& p) -> CNA_Result {
            if (draw == nullptr) {
                p->value->setTransparentScene(nullptr);
                return CNA_RESULT_SUCCESS;
            }
            p->value->setTransparentScene([draw, context]() {
                const CNA_Result result = draw(context);
                if (result != CNA_RESULT_SUCCESS) {
                    throw RenderPipelineDrawFailure{result};
                }
            });
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_render_pipeline_set_shadow_scene(
    const CNA_RenderPipelineHandle pipeline,
    const CNA_ShadowMapHandle shadowMap,
    const CNA_DirectionalLightEXT* const light,
    const CNA_BoundingBox* const sceneBounds,
    const CNA_RenderPipelineDrawCallback drawCasters,
    void* const context)
{
    return WithMap<RenderPipelineResource>(
        pipeline, ObjectKind::RenderPipeline, "RenderPipeline",
        [&](const std::shared_ptr<RenderPipelineResource>& p) -> CNA_Result {
            Ext::DirectionalLightEXT nativeLight;
            BoundingBox nativeBounds;
            if (const CNA_Result result = ToNativeDirectionalLight(light, &nativeLight);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (const CNA_Result result = ToNativeBounds(sceneBounds, &nativeBounds);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            Ext::ShadowMap* nativeMap = nullptr;
            if (shadowMap != CNA_INVALID_HANDLE) {
                std::shared_ptr<ShadowMapResource> mapResource;
                if (const CNA_Result result = GetEngineResource(
                        shadowMap, ObjectKind::ShadowMap, "ShadowMap", &mapResource);
                    result != CNA_RESULT_SUCCESS) {
                    return result;
                }
                nativeMap = mapResource->value.get();
            }
            std::function<void()> casters;
            if (drawCasters != nullptr) {
                casters = [drawCasters, context]() {
                    const CNA_Result result = drawCasters(context);
                    if (result != CNA_RESULT_SUCCESS) {
                        throw RenderPipelineDrawFailure{result};
                    }
                };
            }
            p->value->setShadowScene(nativeMap, nativeLight, nativeBounds, std::move(casters));
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_render_pipeline_set_camera(
    const CNA_RenderPipelineHandle pipeline,
    const CNA_Matrix* const view,
    const CNA_Matrix* const projection,
    const float nearPlane,
    const float farPlane)
{
    return CNA_WITH_PIPELINE(pipeline,
        [&](const std::shared_ptr<RenderPipelineResource>& p) -> CNA_Result {
            if (const CNA_Result result = RequireMatrixArgument(view, "The view is null.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (const CNA_Result result =
                    RequireMatrixArgument(projection, "The projection is null.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (!(nearPlane > 0.0F) || !(farPlane > nearPlane)) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_RANGE,
                    "The near plane must be positive and the far plane beyond it.");
            }
            p->value->setCamera(
                ToNativeMatrix(*view), ToNativeMatrix(*projection), nearPlane, farPlane);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_render_pipeline_set_skybox_camera(
    const CNA_RenderPipelineHandle pipeline,
    const CNA_Matrix* const view,
    const CNA_Matrix* const projection)
{
    return CNA_WITH_PIPELINE(pipeline,
        [&](const std::shared_ptr<RenderPipelineResource>& p) -> CNA_Result {
            if (const CNA_Result result = RequireMatrixArgument(view, "The view is null.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (const CNA_Result result =
                    RequireMatrixArgument(projection, "The projection is null.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            p->value->setSkyboxCamera(ToNativeMatrix(*view), ToNativeMatrix(*projection));
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_render_pipeline_copy_transparency_fallback_reason_ext(
    const CNA_RenderPipelineHandle pipeline,
    char* const destination, const uint64_t capacity, uint64_t* const outBytes)
{
    std::shared_ptr<RenderPipelineResource> resource;
    if (const CNA_Result result = GetEngineResource(
            pipeline, ObjectKind::RenderPipeline, "RenderPipeline", &resource);
        result != CNA_RESULT_SUCCESS) {
        if (outBytes != nullptr) {
            *outBytes = UINT64_C(0);
        }
        return result;
    }
    return CopyFormattedString(destination, capacity, outBytes, [&resource] {
        return resource->value->getTransparencyFallbackReasonEXT();
    });
}

CNA_Result cna_render_pipeline_set_gpu_timing_enabled_ext(
    const CNA_RenderPipelineHandle pipeline, const CNA_Bool value)
{
    // The bool contract is validated ahead of the handle, the CBIND-067 discipline.
    if (value != CNA_TRUE && value != CNA_FALSE) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "value must be CNA_TRUE or CNA_FALSE.");
    }
    return CNA_WITH_PIPELINE(pipeline,
        [&](const std::shared_ptr<RenderPipelineResource>& p) -> CNA_Result {
            p->value->setGpuTimingEnabledEXT(value == CNA_TRUE);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_render_pipeline_is_gpu_timing_enabled_ext(
    const CNA_RenderPipelineHandle pipeline, CNA_Bool* const outEnabled)
{
    return CNA_WITH_PIPELINE(pipeline,
        [&](const std::shared_ptr<RenderPipelineResource>& p) -> CNA_Result {
            return StoreValue(
                outEnabled,
                static_cast<CNA_Bool>(p->value->isGpuTimingEnabledEXT() ? CNA_TRUE : CNA_FALSE));
        });
}

CNA_Result cna_render_pipeline_did_skybox_draw(
    const CNA_RenderPipelineHandle pipeline, CNA_Bool* const outDrew)
{
    return CNA_WITH_PIPELINE(pipeline,
        [&](const std::shared_ptr<RenderPipelineResource>& p) -> CNA_Result {
            return StoreValue(
                outDrew, static_cast<CNA_Bool>(p->value->didSkyboxDraw() ? CNA_TRUE : CNA_FALSE));
        });
}

CNA_Result cna_render_pipeline_did_shadow_pass_run(
    const CNA_RenderPipelineHandle pipeline, CNA_Bool* const outRan)
{
    return CNA_WITH_PIPELINE(pipeline,
        [&](const std::shared_ptr<RenderPipelineResource>& p) -> CNA_Result {
            return StoreValue(
                outRan, static_cast<CNA_Bool>(p->value->didShadowPassRun() ? CNA_TRUE : CNA_FALSE));
        });
}

CNA_Result cna_render_pipeline_get_shadow_map(
    const CNA_RenderPipelineHandle pipeline, CNA_ShadowMapHandle* const outShadowMap)
{
    return WithMap<RenderPipelineResource>(
        pipeline, ObjectKind::RenderPipeline, "RenderPipeline",
        [&](const std::shared_ptr<RenderPipelineResource>& p) -> CNA_Result {
            if (outShadowMap == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The shadow-map output handle is null.");
            }
            *outShadowMap = CNA_INVALID_HANDLE;
            auto* const map = p->value->getShadowMap();
            if (map == nullptr) {
                return CNA_RESULT_SUCCESS;
            }
            // Borrowed: the pipeline never owned the map a caller gave it.
            const auto resource = std::make_shared<ShadowMapResource>(
                ShadowMapResource{std::shared_ptr<Ext::ShadowMap>(p, map), p->parentGame, 0U});
            const CNA_Result result =
                GetRuntimeHandles().Create(ObjectKind::ShadowMap, resource, outShadowMap);
            if (result != CNA_RESULT_SUCCESS) {
                return Fail(
                    result,
                    ErrorCategoryForResult(result),
                    "The borrowed shadow-map handle could not be created.");
            }
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_render_pipeline_get_scene_target(
    const CNA_RenderPipelineHandle pipeline, CNA_Handle* const outTexture)
{
    return CNA_WITH_PIPELINE(pipeline,
        [&](const std::shared_ptr<RenderPipelineResource>& p) -> CNA_Result {
            if (outTexture == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The texture output handle is null.");
            }
            *outTexture = CNA_INVALID_HANDLE;
            auto* const target = p->value->getSceneTarget();
            if (target == nullptr) {
                return CNA_RESULT_SUCCESS;
            }
            const std::shared_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> view(p, target);
            return CreateBorrowedRenderTarget2D(view, p->parentGame, p, outTexture);
        });
}

CNA_Result cna_render_pipeline_get_scene_target_format(
    const CNA_RenderPipelineHandle pipeline, CNA_SurfaceFormat* const outFormat)
{
    return CNA_WITH_PIPELINE(pipeline,
        [&](const std::shared_ptr<RenderPipelineResource>& p) -> CNA_Result {
            return StoreValue(
                outFormat, static_cast<CNA_SurfaceFormat>(p->value->getSceneTargetFormat()));
        });
}

CNA_Result cna_render_pipeline_is_using_scene_target(
    const CNA_RenderPipelineHandle pipeline, CNA_Bool* const outUsing)
{
    return CNA_WITH_PIPELINE(pipeline,
        [&](const std::shared_ptr<RenderPipelineResource>& p) -> CNA_Result {
            return StoreValue(
                outUsing,
                static_cast<CNA_Bool>(p->value->isUsingSceneTarget() ? CNA_TRUE : CNA_FALSE));
        });
}

CNA_Result cna_render_pipeline_get_last_frame_pass_count(
    const CNA_RenderPipelineHandle pipeline, int32_t* const outCount)
{
    return CNA_WITH_PIPELINE(pipeline,
        [&](const std::shared_ptr<RenderPipelineResource>& p) -> CNA_Result {
            return StoreValue(outCount, static_cast<int32_t>(p->value->getLastFramePassCount()));
        });
}

CNA_Result cna_render_pipeline_get_gpu_memory_estimate_bytes(
    const CNA_RenderPipelineHandle pipeline, uint64_t* const outBytes)
{
    return CNA_WITH_PIPELINE(pipeline,
        [&](const std::shared_ptr<RenderPipelineResource>& p) -> CNA_Result {
            return StoreValue(
                outBytes, static_cast<uint64_t>(p->value->getGpuMemoryEstimateBytes()));
        });
}

CNA_Result cna_render_pipeline_get_statistics(
    const CNA_RenderPipelineHandle pipeline,
    CNA_RenderPipelineFrameStatisticsEXT* const outStatistics)
{
    return CNA_WITH_PIPELINE(pipeline,
        [&](const std::shared_ptr<RenderPipelineResource>& p) -> CNA_Result {
            if (outStatistics == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The statistics output is null.");
            }
            const auto stats = p->value->getStatistics();
            *outStatistics = CNA_RenderPipelineFrameStatisticsEXT{};
            outStatistics->struct_size =
                static_cast<uint32_t>(sizeof(CNA_RenderPipelineFrameStatisticsEXT));
            outStatistics->struct_version = UINT32_C(1);
            outStatistics->passes_run = static_cast<int32_t>(stats.passesRun);
            outStatistics->target_switches = static_cast<int32_t>(stats.targetSwitches);
            outStatistics->used_scene_target =
                static_cast<CNA_Bool>(stats.usedSceneTarget ? CNA_TRUE : CNA_FALSE);
            outStatistics->drew_skybox =
                static_cast<CNA_Bool>(stats.drewSkybox ? CNA_TRUE : CNA_FALSE);
            outStatistics->gpu_memory_estimate_bytes =
                static_cast<uint64_t>(stats.gpuMemoryEstimateBytes);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_render_pipeline_release_device_resources_ext(
    const CNA_RenderPipelineHandle pipeline)
{
    return CNA_WITH_PIPELINE(pipeline,
        [&](const std::shared_ptr<RenderPipelineResource>& p) -> CNA_Result {
            try {
                p->value->releaseDeviceResourcesEXT();
            } catch (const std::logic_error&) {
                return Fail(
                    CNA_RESULT_INVALID_STATE,
                    CNA_ERROR_CATEGORY_STATE,
                    "A frame is open; releasing the targets now would leave it drawing into "
                    "freed memory.");
            }
            return CNA_RESULT_SUCCESS;
        });
}

namespace {

struct PostProcessChainResource final {
    std::shared_ptr<Ext::PostProcessChain> value;
    CNA_Handle parentGame;
    uint64_t activeBorrowCount = 0U;
    // CBIND-089A. The canonical addOwnedPass takes a unique_ptr; this ABI holds its objects in
    // shared_ptr and cannot release one. So the chain resource keeps the handed-over pass alive
    // itself and registers it with the non-owning addPass. The observable lifetime is identical --
    // the pass lives exactly as long as the chain, and the caller's handle is consumed either way
    // -- and it avoids the one thing a forced transfer would risk, which is two owners.
    std::vector<std::shared_ptr<Ext::PostProcessPass>> ownedPasses;
};

[[nodiscard]] CNA_Result GetChainTiming(
    const std::shared_ptr<PostProcessChainResource>& chain,
    const uint64_t index,
    const Ext::PostProcessChain::PassTiming** const out)
{
    const auto& timings = chain->value->getPassTimings();
    if (index >= static_cast<uint64_t>(timings.size())) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_RANGE,
            "There is no timing at that index.");
    }
    *out = &timings[static_cast<std::size_t>(index)];
    return CNA_RESULT_SUCCESS;
}

void FillPassTiming(
    const Ext::PostProcessChain::PassTiming& value, CNA_PassTimingEXT* const out)
{
    *out = CNA_PassTimingEXT{};
    out->struct_size = static_cast<uint32_t>(sizeof(CNA_PassTimingEXT));
    out->struct_version = UINT32_C(1);
    out->sample_count = static_cast<int32_t>(value.SampleCount);
    out->milliseconds = static_cast<double>(value.Milliseconds);
}

} // namespace

#define CNA_WITH_CHAIN(handle, body)                                                               \
    WithMap<PostProcessChainResource>(                                                             \
        (handle), ObjectKind::PostProcessChain, "PostProcessChain", body)

CNA_Result cna_post_process_chain_create(
    const CNA_Handle graphicsDeviceHandle, CNA_PostProcessChainHandle* const outChain)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outChain == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The chain output handle is null.");
        }
        *outChain = CNA_INVALID_HANDLE;
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result =
                GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto native = std::make_shared<Ext::PostProcessChain>(*graphicsDevice->value);
        const auto resource = std::make_shared<PostProcessChainResource>(
            PostProcessChainResource{std::move(native), graphicsDevice->parentGame, 0U, {}});
        const CNA_Result result =
            GetRuntimeHandles().Create(ObjectKind::PostProcessChain, resource, outChain);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned chain handle could not be created.");
        }
        AddOwnedGraphicsResourceFor(graphicsDevice->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_post_process_chain_destroy(const CNA_PostProcessChainHandle chainHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<PostProcessChainResource> chain;
        if (const CNA_Result result = GetEngineResource(
                chainHandle, ObjectKind::PostProcessChain, "PostProcessChain", &chain);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (chain->activeBorrowCount != 0U) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The chain is still lending its target pool.");
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(chainHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The owned chain handle could not be released.");
        }
        RemoveOwnedGraphicsResourceFor(chain->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_post_process_chain_add_pass(
    const CNA_PostProcessChainHandle chain, const CNA_PostProcessPassHandle pass)
{
    return CNA_WITH_CHAIN(chain,
        [&](const std::shared_ptr<PostProcessChainResource>& c) -> CNA_Result {
            std::shared_ptr<PostProcessPassResource> passResource;
            if (const CNA_Result result = GetEngineResource(
                    pass, ObjectKind::PostProcessPass, "PostProcessPass", &passResource);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            // Borrowed: the caller keeps owning the pass and must outlive the chain's use of it.
            c->value->addPass(passResource->value.get());
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_post_process_chain_add_owned_pass(
    const CNA_PostProcessChainHandle chain, const CNA_PostProcessPassHandle pass)
{
    return CNA_WITH_CHAIN(chain,
        [&](const std::shared_ptr<PostProcessChainResource>& c) -> CNA_Result {
            std::shared_ptr<PostProcessPassResource> passResource;
            if (const CNA_Result result = GetEngineResource(
                    pass, ObjectKind::PostProcessPass, "PostProcessPass", &passResource);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            // A pass lending its effect cannot be handed over: the borrower would outlive the
            // handle that guaranteed it, which is the one thing CountedBorrow exists to prevent.
            if (passResource->activeBorrowCount != 0U) {
                return Fail(
                    CNA_RESULT_INVALID_STATE,
                    CNA_ERROR_CATEGORY_STATE,
                    "The pass is still lending its effect.");
            }
            // The chain resource takes ownership and registers the pass non-owningly, which
            // gives the canonical lifetime without needing a transfer this ABI cannot express.
            c->ownedPasses.push_back(passResource->value);
            c->value->addPass(passResource->value.get());
            const CNA_Result releaseResult = GetRuntimeHandles().Release(pass);
            if (releaseResult != CNA_RESULT_SUCCESS) {
                c->ownedPasses.pop_back();
                c->value->clear();
                for (const auto& owned : c->ownedPasses) {
                    c->value->addPass(owned.get());
                }
                return Fail(
                    releaseResult,
                    ErrorCategoryForResult(releaseResult),
                    "The pass handle could not be consumed.");
            }
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_post_process_chain_clear(const CNA_PostProcessChainHandle chain)
{
    return CNA_WITH_CHAIN(chain,
        [](const std::shared_ptr<PostProcessChainResource>& c) -> CNA_Result {
            c->value->clear();
            // Releasing what the chain owned, which is what the canonical clear() does too.
            c->ownedPasses.clear();
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_post_process_chain_get_pass_count(
    const CNA_PostProcessChainHandle chain, int32_t* const outCount)
{
    return CNA_WITH_CHAIN(chain,
        [&](const std::shared_ptr<PostProcessChainResource>& c) -> CNA_Result {
            return StoreValue(outCount, static_cast<int32_t>(c->value->getPassCount()));
        });
}

CNA_Result cna_post_process_chain_apply(
    const CNA_PostProcessChainHandle chain, const CNA_PostProcessContext* const context)
{
    return CNA_WITH_CHAIN(chain,
        [&](const std::shared_ptr<PostProcessChainResource>& c) -> CNA_Result {
            if (context == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT,
                    "The context is null.");
            }
            ResolvedPostProcessContext resolved;
            if (const CNA_Result result = ResolvePostProcessContext(*context, &resolved);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            // Both canonical throws are argument mistakes and are answered before the call, so the
            // chain never sees a context it would reject.
            if (resolved.value.source == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The chain has nothing to read from: the context's source is null.");
            }
            if (resolved.value.width <= 0 || resolved.value.height <= 0) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_RANGE,
                    "The context size must be positive.");
            }
            c->value->apply(resolved.value);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_post_process_chain_reset_targets(const CNA_PostProcessChainHandle chain)
{
    return CNA_WITH_CHAIN(chain,
        [](const std::shared_ptr<PostProcessChainResource>& c) -> CNA_Result {
            c->value->resetTargets();
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_post_process_chain_get_target_pool(
    const CNA_PostProcessChainHandle chain, CNA_RenderTargetPoolHandle* const outPool)
{
    return CNA_WITH_CHAIN(chain,
        [&](const std::shared_ptr<PostProcessChainResource>& c) -> CNA_Result {
            if (outPool == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The pool output handle is null.");
            }
            *outPool = CNA_INVALID_HANDLE;
            const auto borrow = std::make_shared<CountedBorrow<PostProcessChainResource>>(c);
            const std::shared_ptr<Ext::RenderTargetPool> view(borrow, &c->value->getTargetPool());
            const auto resource = std::make_shared<RenderTargetPoolResource>(
                RenderTargetPoolResource{view, c->parentGame, 0U});
            const CNA_Result result =
                GetRuntimeHandles().Create(ObjectKind::RenderTargetPool, resource, outPool);
            if (result != CNA_RESULT_SUCCESS) {
                return Fail(
                    result,
                    ErrorCategoryForResult(result),
                    "The borrowed pool handle could not be created.");
            }
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_post_process_chain_is_gpu_timing_enabled(
    const CNA_PostProcessChainHandle chain, CNA_Bool* const outEnabled)
{
    return CNA_WITH_CHAIN(chain,
        [&](const std::shared_ptr<PostProcessChainResource>& c) -> CNA_Result {
            return StoreValue(
                outEnabled,
                static_cast<CNA_Bool>(c->value->isGpuTimingEnabled() ? CNA_TRUE : CNA_FALSE));
        });
}

CNA_Result cna_post_process_chain_set_gpu_timing_enabled(
    const CNA_PostProcessChainHandle chain, const CNA_Bool value)
{
    if (value != CNA_TRUE && value != CNA_FALSE) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "value must be CNA_TRUE or CNA_FALSE.");
    }
    return CNA_WITH_CHAIN(chain,
        [&](const std::shared_ptr<PostProcessChainResource>& c) -> CNA_Result {
            c->value->setGpuTimingEnabled(value == CNA_TRUE);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_post_process_chain_get_pass_timing_count(
    const CNA_PostProcessChainHandle chain, uint64_t* const outCount)
{
    return CNA_WITH_CHAIN(chain,
        [&](const std::shared_ptr<PostProcessChainResource>& c) -> CNA_Result {
            return StoreValue(
                outCount, static_cast<uint64_t>(c->value->getPassTimings().size()));
        });
}

CNA_Result cna_post_process_chain_get_pass_timing(
    const CNA_PostProcessChainHandle chain,
    const uint64_t index,
    CNA_PassTimingEXT* const outTiming)
{
    return CNA_WITH_CHAIN(chain,
        [&](const std::shared_ptr<PostProcessChainResource>& c) -> CNA_Result {
            if (outTiming == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT,
                    "The timing output is null.");
            }
            const Ext::PostProcessChain::PassTiming* timing = nullptr;
            if (const CNA_Result result = GetChainTiming(c, index, &timing);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            FillPassTiming(*timing, outTiming);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_post_process_chain_copy_pass_timing_name(
    const CNA_PostProcessChainHandle chain,
    const uint64_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    std::shared_ptr<PostProcessChainResource> resource;
    if (const CNA_Result result = GetEngineResource(
            chain, ObjectKind::PostProcessChain, "PostProcessChain", &resource);
        result != CNA_RESULT_SUCCESS) {
        if (outBytes != nullptr) {
            *outBytes = UINT64_C(0);
        }
        return result;
    }
    const Ext::PostProcessChain::PassTiming* timing = nullptr;
    if (const CNA_Result result = GetChainTiming(resource, index, &timing);
        result != CNA_RESULT_SUCCESS) {
        if (outBytes != nullptr) {
            *outBytes = UINT64_C(0);
        }
        return result;
    }
    return CopyFormattedString(
        destination, capacity, outBytes, [timing] { return timing->Name; });
}

namespace {

[[nodiscard]] CNA_Result GetPipelineTiming(
    const std::shared_ptr<RenderPipelineResource>& pipeline,
    const uint64_t index,
    const Ext::PostProcessChain::PassTiming** const out)
{
    const auto& timings = pipeline->value->getPassTimingsEXT();
    if (index >= static_cast<uint64_t>(timings.size())) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_RANGE,
            "There is no timing at that index.");
    }
    *out = &timings[static_cast<std::size_t>(index)];
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_render_pipeline_get_pass_timing_count_ext(
    const CNA_RenderPipelineHandle pipeline, uint64_t* const outCount)
{
    return CNA_WITH_PIPELINE(pipeline,
        [&](const std::shared_ptr<RenderPipelineResource>& p) -> CNA_Result {
            return StoreValue(
                outCount, static_cast<uint64_t>(p->value->getPassTimingsEXT().size()));
        });
}

CNA_Result cna_render_pipeline_get_pass_timing_ext(
    const CNA_RenderPipelineHandle pipeline,
    const uint64_t index,
    CNA_PassTimingEXT* const outTiming)
{
    return CNA_WITH_PIPELINE(pipeline,
        [&](const std::shared_ptr<RenderPipelineResource>& p) -> CNA_Result {
            if (outTiming == nullptr) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT,
                    "The timing output is null.");
            }
            const Ext::PostProcessChain::PassTiming* timing = nullptr;
            if (const CNA_Result result = GetPipelineTiming(p, index, &timing);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            FillPassTiming(*timing, outTiming);
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_render_pipeline_copy_pass_timing_name_ext(
    const CNA_RenderPipelineHandle pipeline,
    const uint64_t index,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    std::shared_ptr<RenderPipelineResource> resource;
    if (const CNA_Result result = GetEngineResource(
            pipeline, ObjectKind::RenderPipeline, "RenderPipeline", &resource);
        result != CNA_RESULT_SUCCESS) {
        if (outBytes != nullptr) {
            *outBytes = UINT64_C(0);
        }
        return result;
    }
    const Ext::PostProcessChain::PassTiming* timing = nullptr;
    if (const CNA_Result result = GetPipelineTiming(resource, index, &timing);
        result != CNA_RESULT_SUCCESS) {
        if (outBytes != nullptr) {
            *outBytes = UINT64_C(0);
        }
        return result;
    }
    return CopyFormattedString(
        destination, capacity, outBytes, [timing] { return timing->Name; });
}

namespace {

// CBIND-089B. The count/copy body CopyInt32Range already provides for int32 arrays, generalised
// over the element type so a Vector3 array reads the same way. Same contract: nothing is written
// unless the whole result fits, so a caller never sees a partial array it might mistake for a
// short one.
template <typename TElement, typename TSource>
[[nodiscard]] CNA_Result CopyValueRange(
    const TSource& source, TElement* const destination, const uint64_t capacity,
    uint64_t* const outCount)
{
    if (outCount == nullptr || (destination == nullptr && capacity != 0U)) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The count/copy arguments are inconsistent.");
    }
    const auto required = static_cast<uint64_t>(source.size());
    *outCount = required;
    if (capacity < required) {
        return CNA_RESULT_BUFFER_TOO_SMALL;
    }
    uint64_t index = 0U;
    for (const auto& value : source) {
        destination[index] = value;
        ++index;
    }
    return CNA_RESULT_SUCCESS;
}

// CBIND-089B. The seventeen post-process passes share one shape: create on a device, then a
// handful of accessors the pass alone knows. CBIND-084 already bound apply, name, is_supported and
// destroy on the shared handle, so these two helpers are the whole of what a pass slice adds, and
// CBIND-089C and CBIND-089D reuse them unchanged.
template <typename TPass>
[[nodiscard]] CNA_Result CreateEnginePass(
    const CNA_Handle graphicsDeviceHandle, CNA_PostProcessPassHandle* const outPass)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPass == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The pass output handle is null.");
        }
        *outPass = CNA_INVALID_HANDLE;
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result =
                GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto native = std::make_shared<TPass>(*graphicsDevice->value);
        return CreatePassHandle(
            std::move(native), nullptr, graphicsDevice->parentGame, nullptr, CNA_INVALID_HANDLE,
            outPass);
    });
}

// A pass-specific accessor is refused **by argument**, not by handle, when the handle names a
// different pass: the handle is perfectly valid, it is the concrete type that cannot answer. That
// is the rule CBIND-085B1 settled for interfaces and it applies unchanged here.
template <typename TPass, typename TBody>
[[nodiscard]] CNA_Result WithEnginePass(
    const CNA_PostProcessPassHandle pass, const char* const what, TBody&& body)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<PostProcessPassResource> resource;
        if (const CNA_Result result = GetEngineResource(
                pass, ObjectKind::PostProcessPass, "PostProcessPass", &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto* const typed = dynamic_cast<TPass*>(resource->value.get());
        if (typed == nullptr) {
            (void)what;
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "That pass is not of the type this route reads.");
        }
        return body(typed);
    });
}

} // namespace

CNA_Result cna_ssr_pass_create(
    const CNA_Handle graphicsDeviceHandle, CNA_PostProcessPassHandle* const outPass)
{
    return CreateEnginePass<Ext::SsrPass>(graphicsDeviceHandle, outPass);
}
CNA_Result cna_ssr_pass_get_max_distance(
    const CNA_PostProcessPassHandle pass, float* const outValue)
{
    return WithEnginePass<Ext::SsrPass>(pass, "SsrPass", [&](Ext::SsrPass* const p) -> CNA_Result {
        return StoreValue(outValue, p->getMaxDistance());
    });
}
CNA_Result cna_ssr_pass_set_max_distance(
    const CNA_PostProcessPassHandle pass, const float value)
{
    return WithEnginePass<Ext::SsrPass>(pass, "SsrPass", [&](Ext::SsrPass* const p) -> CNA_Result {
        p->setMaxDistance(value);
        return CNA_RESULT_SUCCESS;
    });
}
CNA_Result cna_ssr_pass_get_step_count(
    const CNA_PostProcessPassHandle pass, int32_t* const outValue)
{
    return WithEnginePass<Ext::SsrPass>(pass, "SsrPass", [&](Ext::SsrPass* const p) -> CNA_Result {
        return StoreValue(outValue, static_cast<int32_t>(p->getStepCount()));
    });
}
CNA_Result cna_ssr_pass_set_step_count(
    const CNA_PostProcessPassHandle pass, const int32_t value)
{
    return WithEnginePass<Ext::SsrPass>(pass, "SsrPass", [&](Ext::SsrPass* const p) -> CNA_Result {
        p->setStepCount(static_cast<int>(value));
        return CNA_RESULT_SUCCESS;
    });
}
CNA_Result cna_ssr_pass_get_thickness(
    const CNA_PostProcessPassHandle pass, float* const outValue)
{
    return WithEnginePass<Ext::SsrPass>(pass, "SsrPass", [&](Ext::SsrPass* const p) -> CNA_Result {
        return StoreValue(outValue, p->getThickness());
    });
}
CNA_Result cna_ssr_pass_set_thickness(
    const CNA_PostProcessPassHandle pass, const float value)
{
    return WithEnginePass<Ext::SsrPass>(pass, "SsrPass", [&](Ext::SsrPass* const p) -> CNA_Result {
        p->setThickness(value);
        return CNA_RESULT_SUCCESS;
    });
}
CNA_Result cna_ssr_pass_get_depth_bias(
    const CNA_PostProcessPassHandle pass, float* const outValue)
{
    return WithEnginePass<Ext::SsrPass>(pass, "SsrPass", [&](Ext::SsrPass* const p) -> CNA_Result {
        return StoreValue(outValue, p->getDepthBias());
    });
}
CNA_Result cna_ssr_pass_set_depth_bias(
    const CNA_PostProcessPassHandle pass, const float value)
{
    return WithEnginePass<Ext::SsrPass>(pass, "SsrPass", [&](Ext::SsrPass* const p) -> CNA_Result {
        p->setDepthBias(value);
        return CNA_RESULT_SUCCESS;
    });
}
CNA_Result cna_ssr_pass_get_roughness_blur(
    const CNA_PostProcessPassHandle pass, float* const outValue)
{
    return WithEnginePass<Ext::SsrPass>(pass, "SsrPass", [&](Ext::SsrPass* const p) -> CNA_Result {
        return StoreValue(outValue, p->getRoughnessBlur());
    });
}
CNA_Result cna_ssr_pass_set_roughness_blur(
    const CNA_PostProcessPassHandle pass, const float value)
{
    return WithEnginePass<Ext::SsrPass>(pass, "SsrPass", [&](Ext::SsrPass* const p) -> CNA_Result {
        p->setRoughnessBlur(value);
        return CNA_RESULT_SUCCESS;
    });
}
CNA_Result cna_ssr_pass_get_edge_fade(
    const CNA_PostProcessPassHandle pass, float* const outValue)
{
    return WithEnginePass<Ext::SsrPass>(pass, "SsrPass", [&](Ext::SsrPass* const p) -> CNA_Result {
        return StoreValue(outValue, p->getEdgeFade());
    });
}
CNA_Result cna_ssr_pass_set_edge_fade(
    const CNA_PostProcessPassHandle pass, const float value)
{
    return WithEnginePass<Ext::SsrPass>(pass, "SsrPass", [&](Ext::SsrPass* const p) -> CNA_Result {
        p->setEdgeFade(value);
        return CNA_RESULT_SUCCESS;
    });
}
CNA_Result cna_ssr_pass_get_intensity(
    const CNA_PostProcessPassHandle pass, float* const outValue)
{
    return WithEnginePass<Ext::SsrPass>(pass, "SsrPass", [&](Ext::SsrPass* const p) -> CNA_Result {
        return StoreValue(outValue, p->getIntensity());
    });
}
CNA_Result cna_ssr_pass_set_intensity(
    const CNA_PostProcessPassHandle pass, const float value)
{
    return WithEnginePass<Ext::SsrPass>(pass, "SsrPass", [&](Ext::SsrPass* const p) -> CNA_Result {
        p->setIntensity(value);
        return CNA_RESULT_SUCCESS;
    });
}
CNA_Result cna_ssao_pass_create(
    const CNA_Handle graphicsDeviceHandle, CNA_PostProcessPassHandle* const outPass)
{
    return CreateEnginePass<Ext::SsaoPass>(graphicsDeviceHandle, outPass);
}
CNA_Result cna_ssao_pass_get_radius(
    const CNA_PostProcessPassHandle pass, float* const outValue)
{
    return WithEnginePass<Ext::SsaoPass>(pass, "SsaoPass", [&](Ext::SsaoPass* const p) -> CNA_Result {
        return StoreValue(outValue, p->getRadius());
    });
}
CNA_Result cna_ssao_pass_set_radius(
    const CNA_PostProcessPassHandle pass, const float value)
{
    return WithEnginePass<Ext::SsaoPass>(pass, "SsaoPass", [&](Ext::SsaoPass* const p) -> CNA_Result {
        p->setRadius(value);
        return CNA_RESULT_SUCCESS;
    });
}
CNA_Result cna_ssao_pass_get_intensity(
    const CNA_PostProcessPassHandle pass, float* const outValue)
{
    return WithEnginePass<Ext::SsaoPass>(pass, "SsaoPass", [&](Ext::SsaoPass* const p) -> CNA_Result {
        return StoreValue(outValue, p->getIntensity());
    });
}
CNA_Result cna_ssao_pass_set_intensity(
    const CNA_PostProcessPassHandle pass, const float value)
{
    return WithEnginePass<Ext::SsaoPass>(pass, "SsaoPass", [&](Ext::SsaoPass* const p) -> CNA_Result {
        p->setIntensity(value);
        return CNA_RESULT_SUCCESS;
    });
}
CNA_Result cna_ssao_pass_get_sample_count(
    const CNA_PostProcessPassHandle pass, int32_t* const outValue)
{
    return WithEnginePass<Ext::SsaoPass>(pass, "SsaoPass", [&](Ext::SsaoPass* const p) -> CNA_Result {
        return StoreValue(outValue, static_cast<int32_t>(p->getSampleCount()));
    });
}
CNA_Result cna_ssao_pass_set_sample_count(
    const CNA_PostProcessPassHandle pass, const int32_t value)
{
    return WithEnginePass<Ext::SsaoPass>(pass, "SsaoPass", [&](Ext::SsaoPass* const p) -> CNA_Result {
        p->setSampleCount(static_cast<int>(value));
        return CNA_RESULT_SUCCESS;
    });
}
CNA_Result cna_ssao_pass_get_half_resolution(
    const CNA_PostProcessPassHandle pass, CNA_Bool* const outValue)
{
    return WithEnginePass<Ext::SsaoPass>(pass, "SsaoPass", [&](Ext::SsaoPass* const p) -> CNA_Result {
        return StoreValue(outValue, static_cast<CNA_Bool>(p->isHalfResolution() ? CNA_TRUE : CNA_FALSE));
    });
}
CNA_Result cna_ssao_pass_set_half_resolution(
    const CNA_PostProcessPassHandle pass, const CNA_Bool value)
{
    if (value != CNA_TRUE && value != CNA_FALSE) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "value must be CNA_TRUE or CNA_FALSE.");
    }
    return WithEnginePass<Ext::SsaoPass>(pass, "SsaoPass", [&](Ext::SsaoPass* const p) -> CNA_Result {
        p->setHalfResolution(value == CNA_TRUE);
        return CNA_RESULT_SUCCESS;
    });
}
CNA_Result cna_depth_of_field_pass_create(
    const CNA_Handle graphicsDeviceHandle, CNA_PostProcessPassHandle* const outPass)
{
    return CreateEnginePass<Ext::DepthOfFieldPass>(graphicsDeviceHandle, outPass);
}
CNA_Result cna_depth_of_field_pass_get_focus_distance(
    const CNA_PostProcessPassHandle pass, float* const outValue)
{
    return WithEnginePass<Ext::DepthOfFieldPass>(pass, "DepthOfFieldPass", [&](Ext::DepthOfFieldPass* const p) -> CNA_Result {
        return StoreValue(outValue, p->getFocusDistance());
    });
}
CNA_Result cna_depth_of_field_pass_set_focus_distance(
    const CNA_PostProcessPassHandle pass, const float value)
{
    return WithEnginePass<Ext::DepthOfFieldPass>(pass, "DepthOfFieldPass", [&](Ext::DepthOfFieldPass* const p) -> CNA_Result {
        p->setFocusDistance(value);
        return CNA_RESULT_SUCCESS;
    });
}
CNA_Result cna_depth_of_field_pass_get_focal_length(
    const CNA_PostProcessPassHandle pass, float* const outValue)
{
    return WithEnginePass<Ext::DepthOfFieldPass>(pass, "DepthOfFieldPass", [&](Ext::DepthOfFieldPass* const p) -> CNA_Result {
        return StoreValue(outValue, p->getFocalLength());
    });
}
CNA_Result cna_depth_of_field_pass_set_focal_length(
    const CNA_PostProcessPassHandle pass, const float value)
{
    return WithEnginePass<Ext::DepthOfFieldPass>(pass, "DepthOfFieldPass", [&](Ext::DepthOfFieldPass* const p) -> CNA_Result {
        p->setFocalLength(value);
        return CNA_RESULT_SUCCESS;
    });
}
CNA_Result cna_depth_of_field_pass_get_f_number(
    const CNA_PostProcessPassHandle pass, float* const outValue)
{
    return WithEnginePass<Ext::DepthOfFieldPass>(pass, "DepthOfFieldPass", [&](Ext::DepthOfFieldPass* const p) -> CNA_Result {
        return StoreValue(outValue, p->getFNumber());
    });
}
CNA_Result cna_depth_of_field_pass_set_f_number(
    const CNA_PostProcessPassHandle pass, const float value)
{
    return WithEnginePass<Ext::DepthOfFieldPass>(pass, "DepthOfFieldPass", [&](Ext::DepthOfFieldPass* const p) -> CNA_Result {
        p->setFNumber(value);
        return CNA_RESULT_SUCCESS;
    });
}
CNA_Result cna_depth_of_field_pass_get_max_radius(
    const CNA_PostProcessPassHandle pass, float* const outValue)
{
    return WithEnginePass<Ext::DepthOfFieldPass>(pass, "DepthOfFieldPass", [&](Ext::DepthOfFieldPass* const p) -> CNA_Result {
        return StoreValue(outValue, p->getMaxRadius());
    });
}
CNA_Result cna_depth_of_field_pass_set_max_radius(
    const CNA_PostProcessPassHandle pass, const float value)
{
    return WithEnginePass<Ext::DepthOfFieldPass>(pass, "DepthOfFieldPass", [&](Ext::DepthOfFieldPass* const p) -> CNA_Result {
        p->setMaxRadius(value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_ssao_pass_reset_targets(const CNA_PostProcessPassHandle pass)
{
    return WithEnginePass<Ext::SsaoPass>(pass, "SsaoPass", [](Ext::SsaoPass* const p)
        -> CNA_Result {
            p->resetTargets();
            return CNA_RESULT_SUCCESS;
        });
}

CNA_Result cna_ssao_pass_copy_kernel(
    const CNA_PostProcessPassHandle pass,
    CNA_Vector3* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return WithEnginePass<Ext::SsaoPass>(pass, "SsaoPass", [&](Ext::SsaoPass* const p)
        -> CNA_Result {
            const auto& kernel = p->getKernel();
            std::vector<CNA_Vector3> values;
            values.reserve(kernel.size());
            for (const auto& sample : kernel) {
                values.push_back(Vec3(sample.X, sample.Y, sample.Z));
            }
            return CopyValueRange(values, destination, capacity, outCount);
        });
}

CNA_Result cna_ssao_pass_copy_occlusion_glsl(
    const CNA_Bool packed, char* const destination, const uint64_t capacity,
    uint64_t* const outBytes)
{
    if (packed != CNA_TRUE && packed != CNA_FALSE) {
        if (outBytes != nullptr) {
            *outBytes = UINT64_C(0);
        }
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "packed must be CNA_TRUE or CNA_FALSE.");
    }
    return CopyFormattedString(destination, capacity, outBytes, [packed] {
        return Ext::SsaoPass::getOcclusionGlsl(packed == CNA_TRUE);
    });
}

CNA_Result cna_ssao_pass_sample_count_for_quality(
    const CNA_RenderQuality quality, int32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (quality > UINT32_C(3)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "quality is not a defined CNA_RENDER_QUALITY_* value.");
        }
        return StoreValue(
            outCount,
            static_cast<int32_t>(
                Ext::SsaoPass::sampleCountForQuality(static_cast<Ext::RenderQuality>(quality))));
    });
}

CNA_Result cna_depth_of_field_pass_circle_of_confusion_millimetres(
    const float depth,
    const float focusDistance,
    const float focalLength,
    const float fNumber,
    float* const outMillimetres)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return StoreValue(
            outMillimetres,
            Ext::DepthOfFieldPass::circleOfConfusionMillimetres(
                depth, focusDistance, focalLength, fNumber));
    });
}

namespace {
// The C constants must name the canonical ones.
static_assert(
    CNA_SSR_PASS_MIN_STEP_COUNT_EXT == Ext::SsrPass::kMinStepCount &&
    CNA_SSR_PASS_MAX_STEP_COUNT_EXT == Ext::SsrPass::kMaxStepCount &&
    CNA_DEPTH_OF_FIELD_SENSOR_HEIGHT_MILLIMETRES_EXT ==
        Ext::DepthOfFieldPass::kSensorHeightMillimetres);
} // namespace
CNA_Result cna_aerial_perspective_pass_create(
    const CNA_Handle graphicsDeviceHandle, CNA_PostProcessPassHandle* const outPass)
{
    return CreateEnginePass<Ext::AerialPerspectivePass>(graphicsDeviceHandle, outPass);
}
CNA_Result cna_aerial_perspective_pass_get_sun_direction(
    const CNA_PostProcessPassHandle pass, CNA_Vector3* const outValue)
{
    return WithEnginePass<Ext::AerialPerspectivePass>(pass, "AerialPerspectivePass", [&](Ext::AerialPerspectivePass* const p) -> CNA_Result {
        const auto v = p->getSunDirection();
        return StoreValue(outValue, Vec3(v.X, v.Y, v.Z));
    });
}

CNA_Result cna_aerial_perspective_pass_set_sun_direction(
    const CNA_PostProcessPassHandle pass, const CNA_Vector3* const value)
{
    return WithEnginePass<Ext::AerialPerspectivePass>(pass, "AerialPerspectivePass", [&](Ext::AerialPerspectivePass* const p) -> CNA_Result {
        if (const CNA_Result result = RequireVector3Argument(value, "The value is null.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        p->setSunDirection(ToNativeVector3(*value));
        return CNA_RESULT_SUCCESS;
    });
}
CNA_Result cna_aerial_perspective_pass_get_turbidity(
    const CNA_PostProcessPassHandle pass, float* const outValue)
{
    return WithEnginePass<Ext::AerialPerspectivePass>(pass, "AerialPerspectivePass", [&](Ext::AerialPerspectivePass* const p) -> CNA_Result {
        return StoreValue(outValue, p->getTurbidity());
    });
}

CNA_Result cna_aerial_perspective_pass_set_turbidity(
    const CNA_PostProcessPassHandle pass, const float value)
{
    return WithEnginePass<Ext::AerialPerspectivePass>(pass, "AerialPerspectivePass", [&](Ext::AerialPerspectivePass* const p) -> CNA_Result {
        p->setTurbidity(value);
        return CNA_RESULT_SUCCESS;
    });
}
CNA_Result cna_aerial_perspective_pass_get_intensity(
    const CNA_PostProcessPassHandle pass, float* const outValue)
{
    return WithEnginePass<Ext::AerialPerspectivePass>(pass, "AerialPerspectivePass", [&](Ext::AerialPerspectivePass* const p) -> CNA_Result {
        return StoreValue(outValue, p->getIntensity());
    });
}

CNA_Result cna_aerial_perspective_pass_set_intensity(
    const CNA_PostProcessPassHandle pass, const float value)
{
    return WithEnginePass<Ext::AerialPerspectivePass>(pass, "AerialPerspectivePass", [&](Ext::AerialPerspectivePass* const p) -> CNA_Result {
        p->setIntensity(value);
        return CNA_RESULT_SUCCESS;
    });
}
CNA_Result cna_aerial_perspective_pass_get_scale_height(
    const CNA_PostProcessPassHandle pass, float* const outValue)
{
    return WithEnginePass<Ext::AerialPerspectivePass>(pass, "AerialPerspectivePass", [&](Ext::AerialPerspectivePass* const p) -> CNA_Result {
        return StoreValue(outValue, p->getScaleHeight());
    });
}

CNA_Result cna_aerial_perspective_pass_set_scale_height(
    const CNA_PostProcessPassHandle pass, const float value)
{
    return WithEnginePass<Ext::AerialPerspectivePass>(pass, "AerialPerspectivePass", [&](Ext::AerialPerspectivePass* const p) -> CNA_Result {
        p->setScaleHeight(value);
        return CNA_RESULT_SUCCESS;
    });
}
CNA_Result cna_volumetric_fog_pass_create(
    const CNA_Handle graphicsDeviceHandle, CNA_PostProcessPassHandle* const outPass)
{
    return CreateEnginePass<Ext::VolumetricFogPass>(graphicsDeviceHandle, outPass);
}
CNA_Result cna_volumetric_fog_pass_get_density(
    const CNA_PostProcessPassHandle pass, float* const outValue)
{
    return WithEnginePass<Ext::VolumetricFogPass>(pass, "VolumetricFogPass", [&](Ext::VolumetricFogPass* const p) -> CNA_Result {
        return StoreValue(outValue, p->getDensity());
    });
}

CNA_Result cna_volumetric_fog_pass_set_density(
    const CNA_PostProcessPassHandle pass, const float value)
{
    return WithEnginePass<Ext::VolumetricFogPass>(pass, "VolumetricFogPass", [&](Ext::VolumetricFogPass* const p) -> CNA_Result {
        p->setDensity(value);
        return CNA_RESULT_SUCCESS;
    });
}
CNA_Result cna_volumetric_fog_pass_get_anisotropy(
    const CNA_PostProcessPassHandle pass, float* const outValue)
{
    return WithEnginePass<Ext::VolumetricFogPass>(pass, "VolumetricFogPass", [&](Ext::VolumetricFogPass* const p) -> CNA_Result {
        return StoreValue(outValue, p->getAnisotropy());
    });
}

CNA_Result cna_volumetric_fog_pass_set_anisotropy(
    const CNA_PostProcessPassHandle pass, const float value)
{
    return WithEnginePass<Ext::VolumetricFogPass>(pass, "VolumetricFogPass", [&](Ext::VolumetricFogPass* const p) -> CNA_Result {
        p->setAnisotropy(value);
        return CNA_RESULT_SUCCESS;
    });
}
CNA_Result cna_volumetric_fog_pass_get_range(
    const CNA_PostProcessPassHandle pass, float* const outValue)
{
    return WithEnginePass<Ext::VolumetricFogPass>(pass, "VolumetricFogPass", [&](Ext::VolumetricFogPass* const p) -> CNA_Result {
        return StoreValue(outValue, p->getRange());
    });
}

CNA_Result cna_volumetric_fog_pass_set_range(
    const CNA_PostProcessPassHandle pass, const float value)
{
    return WithEnginePass<Ext::VolumetricFogPass>(pass, "VolumetricFogPass", [&](Ext::VolumetricFogPass* const p) -> CNA_Result {
        p->setRange(value);
        return CNA_RESULT_SUCCESS;
    });
}
CNA_Result cna_height_fog_pass_create(
    const CNA_Handle graphicsDeviceHandle, CNA_PostProcessPassHandle* const outPass)
{
    return CreateEnginePass<Ext::HeightFogPass>(graphicsDeviceHandle, outPass);
}
CNA_Result cna_height_fog_pass_get_color(
    const CNA_PostProcessPassHandle pass, CNA_Vector3* const outValue)
{
    return WithEnginePass<Ext::HeightFogPass>(pass, "HeightFogPass", [&](Ext::HeightFogPass* const p) -> CNA_Result {
        const auto v = p->getColor();
        return StoreValue(outValue, Vec3(v.X, v.Y, v.Z));
    });
}

CNA_Result cna_height_fog_pass_set_color(
    const CNA_PostProcessPassHandle pass, const CNA_Vector3* const value)
{
    return WithEnginePass<Ext::HeightFogPass>(pass, "HeightFogPass", [&](Ext::HeightFogPass* const p) -> CNA_Result {
        if (const CNA_Result result = RequireVector3Argument(value, "The value is null.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        p->setColor(ToNativeVector3(*value));
        return CNA_RESULT_SUCCESS;
    });
}
CNA_Result cna_height_fog_pass_get_density(
    const CNA_PostProcessPassHandle pass, float* const outValue)
{
    return WithEnginePass<Ext::HeightFogPass>(pass, "HeightFogPass", [&](Ext::HeightFogPass* const p) -> CNA_Result {
        return StoreValue(outValue, p->getDensity());
    });
}

CNA_Result cna_height_fog_pass_set_density(
    const CNA_PostProcessPassHandle pass, const float value)
{
    return WithEnginePass<Ext::HeightFogPass>(pass, "HeightFogPass", [&](Ext::HeightFogPass* const p) -> CNA_Result {
        p->setDensity(value);
        return CNA_RESULT_SUCCESS;
    });
}
CNA_Result cna_height_fog_pass_get_falloff(
    const CNA_PostProcessPassHandle pass, float* const outValue)
{
    return WithEnginePass<Ext::HeightFogPass>(pass, "HeightFogPass", [&](Ext::HeightFogPass* const p) -> CNA_Result {
        return StoreValue(outValue, p->getFalloff());
    });
}

CNA_Result cna_height_fog_pass_set_falloff(
    const CNA_PostProcessPassHandle pass, const float value)
{
    return WithEnginePass<Ext::HeightFogPass>(pass, "HeightFogPass", [&](Ext::HeightFogPass* const p) -> CNA_Result {
        p->setFalloff(value);
        return CNA_RESULT_SUCCESS;
    });
}
CNA_Result cna_height_fog_pass_get_base_height(
    const CNA_PostProcessPassHandle pass, float* const outValue)
{
    return WithEnginePass<Ext::HeightFogPass>(pass, "HeightFogPass", [&](Ext::HeightFogPass* const p) -> CNA_Result {
        return StoreValue(outValue, p->getBaseHeight());
    });
}

CNA_Result cna_height_fog_pass_set_base_height(
    const CNA_PostProcessPassHandle pass, const float value)
{
    return WithEnginePass<Ext::HeightFogPass>(pass, "HeightFogPass", [&](Ext::HeightFogPass* const p) -> CNA_Result {
        p->setBaseHeight(value);
        return CNA_RESULT_SUCCESS;
    });
}
CNA_Result cna_light_shaft_pass_create(
    const CNA_Handle graphicsDeviceHandle, CNA_PostProcessPassHandle* const outPass)
{
    return CreateEnginePass<Ext::LightShaftPass>(graphicsDeviceHandle, outPass);
}
CNA_Result cna_light_shaft_pass_get_light_screen_position(
    const CNA_PostProcessPassHandle pass, CNA_Vector2* const outValue)
{
    return WithEnginePass<Ext::LightShaftPass>(pass, "LightShaftPass", [&](Ext::LightShaftPass* const p) -> CNA_Result {
        const auto v = p->getLightScreenPosition();
        CNA_Vector2 out;
        out.x = v.X;
        out.y = v.Y;
        return StoreValue(outValue, out);
    });
}

CNA_Result cna_light_shaft_pass_set_light_screen_position(
    const CNA_PostProcessPassHandle pass, const CNA_Vector2* const value)
{
    return WithEnginePass<Ext::LightShaftPass>(pass, "LightShaftPass", [&](Ext::LightShaftPass* const p) -> CNA_Result {
        if (value == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, "The value is null.");
        }
        p->setLightScreenPosition(Microsoft::Xna::Framework::Vector2(value->x, value->y));
        return CNA_RESULT_SUCCESS;
    });
}
CNA_Result cna_light_shaft_pass_get_threshold(
    const CNA_PostProcessPassHandle pass, float* const outValue)
{
    return WithEnginePass<Ext::LightShaftPass>(pass, "LightShaftPass", [&](Ext::LightShaftPass* const p) -> CNA_Result {
        return StoreValue(outValue, p->getThreshold());
    });
}

CNA_Result cna_light_shaft_pass_set_threshold(
    const CNA_PostProcessPassHandle pass, const float value)
{
    return WithEnginePass<Ext::LightShaftPass>(pass, "LightShaftPass", [&](Ext::LightShaftPass* const p) -> CNA_Result {
        p->setThreshold(value);
        return CNA_RESULT_SUCCESS;
    });
}
CNA_Result cna_light_shaft_pass_get_intensity(
    const CNA_PostProcessPassHandle pass, float* const outValue)
{
    return WithEnginePass<Ext::LightShaftPass>(pass, "LightShaftPass", [&](Ext::LightShaftPass* const p) -> CNA_Result {
        return StoreValue(outValue, p->getIntensity());
    });
}

CNA_Result cna_light_shaft_pass_set_intensity(
    const CNA_PostProcessPassHandle pass, const float value)
{
    return WithEnginePass<Ext::LightShaftPass>(pass, "LightShaftPass", [&](Ext::LightShaftPass* const p) -> CNA_Result {
        p->setIntensity(value);
        return CNA_RESULT_SUCCESS;
    });
}
CNA_Result cna_light_shaft_pass_get_decay(
    const CNA_PostProcessPassHandle pass, float* const outValue)
{
    return WithEnginePass<Ext::LightShaftPass>(pass, "LightShaftPass", [&](Ext::LightShaftPass* const p) -> CNA_Result {
        return StoreValue(outValue, p->getDecay());
    });
}

CNA_Result cna_light_shaft_pass_set_decay(
    const CNA_PostProcessPassHandle pass, const float value)
{
    return WithEnginePass<Ext::LightShaftPass>(pass, "LightShaftPass", [&](Ext::LightShaftPass* const p) -> CNA_Result {
        p->setDecay(value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_aerial_perspective_pass_copy_fallback_reason(
    const CNA_PostProcessPassHandle pass, char* const destination, const uint64_t capacity,
    uint64_t* const outBytes)
{
    return WithEnginePass<Ext::AerialPerspectivePass>(
        pass, "AerialPerspectivePass", [&](Ext::AerialPerspectivePass* const p) -> CNA_Result {
            return CopyFormattedString(
                destination, capacity, outBytes, [p] { return p->getFallbackReason(); });
        });
}

CNA_Result cna_aerial_perspective_pass_air_mass_for_distance(
    const CNA_Vector3* const viewDirection,
    const float distance,
    const float scaleHeight,
    float* const outAirMass)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result =
                RequireVector3Argument(viewDirection, "The view direction is null.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return StoreValue(
            outAirMass,
            Ext::AerialPerspectivePass::airMassForDistance(
                ToNativeVector3(*viewDirection), distance, scaleHeight));
    });
}

CNA_Result cna_aerial_perspective_pass_transmittance(
    const float turbidity, const float airMass, CNA_Vector3* const outTransmittance)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        const auto v = Ext::AerialPerspectivePass::transmittance(turbidity, airMass);
        return StoreValue(outTransmittance, Vec3(v.X, v.Y, v.Z));
    });
}

CNA_Result cna_height_fog_pass_optical_depth(
    const float cameraHeight,
    const float rayHeightStep,
    const float distance,
    const float density,
    const float falloff,
    const float baseHeight,
    float* const outDepth)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return StoreValue(
            outDepth,
            Ext::HeightFogPass::opticalDepth(
                cameraHeight, rayHeightStep, distance, density, falloff, baseHeight));
    });
}

CNA_Result cna_volumetric_fog_pass_set_light(
    const CNA_PostProcessPassHandle pass,
    const CNA_ShadowMapHandle shadowMap,
    const CNA_Vector3* const lightDirection,
    const CNA_Vector3* const lightColor)
{
    return WithEnginePass<Ext::VolumetricFogPass>(
        pass, "VolumetricFogPass", [&](Ext::VolumetricFogPass* const p) -> CNA_Result {
            if (const CNA_Result result =
                    RequireVector3Argument(lightDirection, "The light direction is null.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (const CNA_Result result =
                    RequireVector3Argument(lightColor, "The light colour is null.");
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            Ext::ShadowMap* nativeMap = nullptr;
            if (shadowMap != CNA_INVALID_HANDLE) {
                std::shared_ptr<ShadowMapResource> mapResource;
                if (const CNA_Result result = GetEngineResource(
                        shadowMap, ObjectKind::ShadowMap, "ShadowMap", &mapResource);
                    result != CNA_RESULT_SUCCESS) {
                    return result;
                }
                nativeMap = mapResource->value.get();
            }
            // Borrowed: the pass records the map and never owns it.
            p->setLight(
                nativeMap, ToNativeVector3(*lightDirection), ToNativeVector3(*lightColor));
            return CNA_RESULT_SUCCESS;
        });
}

namespace {
static_assert(
    CNA_VOLUMETRIC_FOG_SLICE_COUNT_EXT == Ext::VolumetricFogPass::kSliceCount &&
    CNA_VOLUMETRIC_FOG_SLICE_RESOLUTION_EXT == Ext::VolumetricFogPass::kSliceResolution &&
    CNA_LIGHT_SHAFT_STEP_COUNT_EXT == Ext::LightShaftPass::kStepCount);
} // namespace

#endif // CNA_CNAEXT

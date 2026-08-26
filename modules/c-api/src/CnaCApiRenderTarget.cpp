// SPDX-License-Identifier: MS-PL

#include "CNA/C/render_target.h"
#include "CnaCApiGraphicsDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using CNA::C::Detail::AddOwnedGraphicsResource;
using CNA::C::Detail::AddOwnedGraphicsResourceFor;
using CNA::C::Detail::RemoveOwnedGraphicsResourceFor;
using CNA::C::Detail::BorrowedGraphicsDevice;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CheckedElementByteCount;
using CNA::C::Detail::CreateOwnedRenderTarget2D;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetBorrowedGraphicsDevice;
using CNA::C::Detail::GetOwnedTexture2D;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::RemoveOwnedGraphicsResource;
using CNA::C::Detail::RenderTargetCubeResource;
using CNA::C::Detail::Texture2DResource;
using Microsoft::Xna::Framework::Graphics::CubeMapFace;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::RenderTargetBinding;
using Microsoft::Xna::Framework::Graphics::RenderTargetCube;
using Microsoft::Xna::Framework::Graphics::RenderTargetUsage;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

constexpr uint32_t StructureVersion = UINT32_C(1);

std::unordered_map<GraphicsDevice*, std::vector<CNA_RenderTargetBinding>> activeBindings;

[[nodiscard]] bool IsBool(const CNA_Bool value) noexcept
{
    return value == CNA_FALSE || value == CNA_TRUE;
}

[[nodiscard]] bool IsSurfaceFormat(const CNA_SurfaceFormat value) noexcept
{
    return value <= CNA_SURFACE_FORMAT_USHORT_EXT;
}

[[nodiscard]] bool IsDepthFormat(const CNA_DepthFormat value) noexcept
{
    return value <= CNA_DEPTH_FORMAT_DEPTH24_STENCIL8;
}

[[nodiscard]] bool IsUsage(const CNA_RenderTargetUsage value) noexcept
{
    return value <= CNA_RENDER_TARGET_USAGE_PLATFORM_CONTENTS;
}

[[nodiscard]] bool IsCubeFace(const CNA_CubeMapFace value) noexcept
{
    return value <= CNA_CUBE_MAP_FACE_NEGATIVE_Z;
}

[[nodiscard]] CNA_Result InvalidArgument(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
}

[[nodiscard]] CNA_Result GetRenderTarget2D(
    const CNA_Handle handle,
    std::shared_ptr<Texture2DResource>* const outTarget)
{
    ObjectKind kind = ObjectKind::Unknown;
    CNA_Result result = GetRuntimeHandles().GetKind(handle, &kind);
    if (result != CNA_RESULT_SUCCESS || kind != ObjectKind::RenderTarget2D) {
        if (result == CNA_RESULT_SUCCESS) {
            result = CNA_RESULT_INVALID_HANDLE;
        }
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned RenderTarget2D handle is invalid for this call.");
    }
    return GetOwnedTexture2D(handle, outTarget);
}

[[nodiscard]] CNA_Result GetRenderTargetCube(
    const CNA_Handle handle,
    std::shared_ptr<RenderTargetCubeResource>* const outTarget)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::RenderTargetCube, outTarget);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned RenderTargetCube handle is invalid for this call.");
}

[[nodiscard]] CNA_Result CreateRenderTargetCubeHandle(
    std::shared_ptr<RenderTargetCube> target,
    const CNA_Handle parentGame,
    CNA_Handle* const outTarget)
{
    const auto resource = std::make_shared<RenderTargetCubeResource>(
        RenderTargetCubeResource{std::move(target), parentGame, 0U});
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::RenderTargetCube, resource, outTarget);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned RenderTargetCube handle could not be created.");
    }
    AddOwnedGraphicsResourceFor(parentGame);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ResolveBindings(
    const CNA_Handle parentGame,
    const CNA_RenderTargetBinding* const bindings,
    const uint64_t bindingCount,
    std::vector<RenderTargetBinding>* const outNative,
    std::vector<CNA_RenderTargetBinding>* const outNormalized)
{
    std::size_t ignoredBytes = 0U;
    if (const CNA_Result result = CheckedElementByteCount(
            bindings, bindingCount, sizeof(CNA_RenderTargetBinding), &ignoredBytes);
        result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The render-target binding array is invalid.");
    }
    if (bindingCount > outNative->max_size()) {
        return Fail(
            CNA_RESULT_OVERFLOW,
            CNA_ERROR_CATEGORY_RANGE,
            "The render-target binding count exceeds the native collection range.");
    }
    outNative->reserve(static_cast<std::size_t>(bindingCount));
    outNormalized->reserve(static_cast<std::size_t>(bindingCount));

    for (uint64_t index = 0U; index < bindingCount; ++index) {
        const CNA_RenderTargetBinding& binding = bindings[index];
        if (binding.struct_size != sizeof(CNA_RenderTargetBinding) ||
            binding.struct_version != StructureVersion) {
            return InvalidArgument("A render-target binding has an invalid structure version.");
        }

        ObjectKind kind = ObjectKind::Unknown;
        if (const CNA_Result result = GetRuntimeHandles().GetKind(binding.render_target, &kind);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "A render-target binding handle is invalid.");
        }

        if (kind == ObjectKind::RenderTarget2D) {
            // CBIND-070: the two used to share one refusal and one result code, and they are not
            // the same kind of failure. A nonzero slice is what the canonical SetRenderTargets
            // itself refuses, with NotSupportedException -- so it answers NOT_SUPPORTED here, the
            // code that exception maps to everywhere else in this ABI. A face on a 2D binding is
            // an ordinary bad argument: the field means nothing for this target kind.
            if (binding.array_slice != 0) {
                return Fail(
                    CNA_RESULT_NOT_SUPPORTED,
                    CNA_ERROR_CATEGORY_NOT_SUPPORTED,
                    "RenderTarget2D array slices are not supported; the array slice must be 0.");
            }
            if (binding.cube_map_face != CNA_CUBE_MAP_FACE_POSITIVE_X) {
                return InvalidArgument(
                    "A RenderTarget2D binding must name the positive-X face, which is the value "
                    "the field carries when it has no meaning.");
            }
            std::shared_ptr<Texture2DResource> resource;
            if (const CNA_Result result = GetRenderTarget2D(binding.render_target, &resource);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (resource->parentGame != parentGame) {
                return Fail(
                    CNA_RESULT_INVALID_HANDLE,
                    CNA_ERROR_CATEGORY_HANDLE,
                    "A render target belongs to a different game.");
            }
            const auto target = std::static_pointer_cast<RenderTarget2D>(resource->value);
            outNative->emplace_back(target.get(), binding.array_slice);
        } else if (kind == ObjectKind::RenderTargetCube) {
            // The canonical cube path reads the face and never the slice, so a nonzero one is
            // meaningless rather than unsupported. Refused anyway, and separately: a caller who
            // set it believed it meant something, and silently dropping it would leave that
            // belief intact.
            if (binding.array_slice != 0) {
                return InvalidArgument(
                    "A RenderTargetCube binding has no array slice; the face selects the "
                    "subresource, so the array slice must be 0.");
            }
            if (!IsCubeFace(binding.cube_map_face)) {
                return InvalidArgument("A RenderTargetCube binding has an invalid face.");
            }
            std::shared_ptr<RenderTargetCubeResource> resource;
            if (const CNA_Result result = GetRenderTargetCube(binding.render_target, &resource);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (resource->parentGame != parentGame) {
                return Fail(
                    CNA_RESULT_INVALID_HANDLE,
                    CNA_ERROR_CATEGORY_HANDLE,
                    "A render target belongs to a different game.");
            }
            outNative->emplace_back(
                resource->value.get(), static_cast<CubeMapFace>(binding.cube_map_face));
        } else {
            return Fail(
                CNA_RESULT_INVALID_HANDLE,
                CNA_ERROR_CATEGORY_HANDLE,
                "A binding does not refer to a render-target handle.");
        }
        outNormalized->push_back(binding);
    }
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_render_target_usage_preserves_contents(
    const CNA_RenderTargetUsage usage,
    CNA_Bool* const outPreserves)
{
    return CallWithExceptionBarrier([&]() {
        if (outPreserves == nullptr || !IsUsage(usage)) {
            return InvalidArgument("The render-target usage query is invalid.");
        }
        *outPreserves = RenderTargetUsagePreservesContentsEXT(
            static_cast<RenderTargetUsage>(usage)) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_render_target2d_create(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_RenderTarget2DCreateInfo* const createInfo,
    CNA_Handle* const outRenderTarget)
{
    return CallWithExceptionBarrier([&]() {
        if (outRenderTarget == nullptr) {
            return InvalidArgument("The RenderTarget2D output handle is null.");
        }
        *outRenderTarget = CNA_INVALID_HANDLE;
        if (createInfo == nullptr ||
            createInfo->struct_size < sizeof(CNA_RenderTarget2DCreateInfo) ||
            createInfo->struct_version != StructureVersion || createInfo->width == 0U ||
            createInfo->height == 0U ||
            createInfo->width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
            createInfo->height > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
            !IsBool(createInfo->mip_map) || createInfo->reserved0[0] != 0U ||
            createInfo->reserved0[1] != 0U || createInfo->reserved0[2] != 0U ||
            !IsSurfaceFormat(createInfo->format) || !IsDepthFormat(createInfo->depth_format) ||
            createInfo->multi_sample_count < 0 || !IsUsage(createInfo->usage) ||
            createInfo->reserved1 != 0U) {
            return InvalidArgument("The RenderTarget2D creation configuration is invalid.");
        }

        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto target = std::make_shared<RenderTarget2D>(
            *graphicsDevice->value,
            static_cast<int>(createInfo->width),
            static_cast<int>(createInfo->height),
            createInfo->mip_map == CNA_TRUE,
            static_cast<SurfaceFormat>(createInfo->format),
            static_cast<DepthFormat>(createInfo->depth_format),
            createInfo->multi_sample_count,
            static_cast<RenderTargetUsage>(createInfo->usage));
        return CreateOwnedRenderTarget2D(
            std::static_pointer_cast<Microsoft::Xna::Framework::Graphics::Texture2D>(target),
            graphicsDevice->parentGame,
            outRenderTarget);
    });
}

CNA_Result cna_render_target_cube_create(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_RenderTargetCubeCreateInfo* const createInfo,
    CNA_Handle* const outRenderTarget)
{
    return CallWithExceptionBarrier([&]() {
        if (outRenderTarget == nullptr) {
            return InvalidArgument("The RenderTargetCube output handle is null.");
        }
        *outRenderTarget = CNA_INVALID_HANDLE;
        if (createInfo == nullptr ||
            createInfo->struct_size < sizeof(CNA_RenderTargetCubeCreateInfo) ||
            createInfo->struct_version != StructureVersion || createInfo->size == 0U ||
            createInfo->size > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
            !IsBool(createInfo->mip_map) || createInfo->reserved[0] != 0U ||
            createInfo->reserved[1] != 0U || createInfo->reserved[2] != 0U ||
            !IsSurfaceFormat(createInfo->format) || !IsDepthFormat(createInfo->depth_format) ||
            createInfo->multi_sample_count < 0 || !IsUsage(createInfo->usage)) {
            return InvalidArgument("The RenderTargetCube creation configuration is invalid.");
        }

        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto target = std::make_shared<RenderTargetCube>(
            *graphicsDevice->value,
            static_cast<int>(createInfo->size),
            createInfo->mip_map == CNA_TRUE,
            static_cast<SurfaceFormat>(createInfo->format),
            static_cast<DepthFormat>(createInfo->depth_format),
            createInfo->multi_sample_count,
            static_cast<RenderTargetUsage>(createInfo->usage));
        return CreateRenderTargetCubeHandle(
            target, graphicsDevice->parentGame, outRenderTarget);
    });
}

CNA_Result cna_render_target_get_info(
    const CNA_Handle renderTargetHandle,
    CNA_RenderTargetInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() {
        if (outInfo == nullptr || outInfo->struct_size < sizeof(CNA_RenderTargetInfo) ||
            outInfo->struct_version != StructureVersion) {
            return InvalidArgument("The render-target output structure is invalid.");
        }
        ObjectKind kind = ObjectKind::Unknown;
        if (const CNA_Result result = GetRuntimeHandles().GetKind(renderTargetHandle, &kind);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The render-target handle is invalid for this call.");
        }

        if (kind == ObjectKind::RenderTarget2D) {
            std::shared_ptr<Texture2DResource> resource;
            if (const CNA_Result result = GetRenderTarget2D(renderTargetHandle, &resource);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            const auto target = std::static_pointer_cast<RenderTarget2D>(resource->value);
            *outInfo = CNA_RenderTargetInfo{
                .struct_size = sizeof(CNA_RenderTargetInfo),
                .struct_version = StructureVersion,
                .kind = CNA_RENDER_TARGET_KIND_2D,
                .width = static_cast<uint32_t>(target->getWidthProperty()),
                .height = static_cast<uint32_t>(target->getHeightProperty()),
                .level_count = static_cast<uint32_t>(target->getLevelCountProperty()),
                .format = static_cast<CNA_SurfaceFormat>(target->getFormatProperty()),
                .depth_format = static_cast<CNA_DepthFormat>(
                    target->getDepthStencilFormatProperty()),
                .multi_sample_count = target->getMultiSampleCountProperty(),
                .usage = static_cast<CNA_RenderTargetUsage>(
                    target->getRenderTargetUsageProperty()),
                .is_content_lost = target->getIsContentLostProperty() ? CNA_TRUE : CNA_FALSE,
                .renderer_available = target->GetRenderTargetRenderer() != nullptr
                    ? CNA_TRUE : CNA_FALSE,
                .reserved = {0U, 0U}};
            return CNA_RESULT_SUCCESS;
        }
        if (kind == ObjectKind::RenderTargetCube) {
            std::shared_ptr<RenderTargetCubeResource> resource;
            if (const CNA_Result result = GetRenderTargetCube(renderTargetHandle, &resource);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            const RenderTargetCube& target = *resource->value;
            *outInfo = CNA_RenderTargetInfo{
                .struct_size = sizeof(CNA_RenderTargetInfo),
                .struct_version = StructureVersion,
                .kind = CNA_RENDER_TARGET_KIND_CUBE,
                .width = static_cast<uint32_t>(target.getWidthProperty()),
                .height = static_cast<uint32_t>(target.getHeightProperty()),
                .level_count = static_cast<uint32_t>(target.getLevelCountProperty()),
                .format = static_cast<CNA_SurfaceFormat>(target.getFormatProperty()),
                .depth_format = static_cast<CNA_DepthFormat>(
                    target.getDepthStencilFormatProperty()),
                .multi_sample_count = target.getMultiSampleCountProperty(),
                .usage = static_cast<CNA_RenderTargetUsage>(
                    target.getRenderTargetUsageProperty()),
                .is_content_lost = target.getIsContentLostProperty() ? CNA_TRUE : CNA_FALSE,
                .renderer_available = target.GetRenderTargetCubeRenderer() != nullptr
                    ? CNA_TRUE : CNA_FALSE,
                .reserved = {0U, 0U}};
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            CNA_RESULT_INVALID_HANDLE,
            CNA_ERROR_CATEGORY_HANDLE,
            "The handle does not refer to a render target.");
    });
}

CNA_Result cna_graphics_device_set_render_targets(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_RenderTargetBinding* const bindings,
    const uint64_t bindingCount)
{
    return CallWithExceptionBarrier([&]() {
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<RenderTargetBinding> nativeBindings;
        std::vector<CNA_RenderTargetBinding> normalizedBindings;
        if (const CNA_Result result = ResolveBindings(
                graphicsDevice->parentGame,
                bindings,
                bindingCount,
                &nativeBindings,
                &normalizedBindings);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        graphicsDevice->value->SetRenderTargets(nativeBindings);
        if (normalizedBindings.empty()) {
            activeBindings.erase(graphicsDevice->value);
        } else {
            activeBindings[graphicsDevice->value] = std::move(normalizedBindings);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_set_render_target2d(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_Handle renderTargetHandle)
{
    if (renderTargetHandle == CNA_INVALID_HANDLE) {
        return cna_graphics_device_set_render_targets(graphicsDeviceHandle, nullptr, 0U);
    }
    const CNA_RenderTargetBinding binding = {
        .struct_size = sizeof(CNA_RenderTargetBinding),
        .struct_version = StructureVersion,
        .render_target = renderTargetHandle,
        .array_slice = 0,
        .cube_map_face = CNA_CUBE_MAP_FACE_POSITIVE_X};
    return cna_graphics_device_set_render_targets(graphicsDeviceHandle, &binding, 1U);
}

CNA_Result cna_graphics_device_set_render_target_cube(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_Handle renderTargetHandle,
    const CNA_CubeMapFace cubeMapFace)
{
    if (renderTargetHandle == CNA_INVALID_HANDLE) {
        return cna_graphics_device_set_render_targets(graphicsDeviceHandle, nullptr, 0U);
    }
    if (!IsCubeFace(cubeMapFace)) {
        return InvalidArgument("The cube-map face is invalid.");
    }
    const CNA_RenderTargetBinding binding = {
        .struct_size = sizeof(CNA_RenderTargetBinding),
        .struct_version = StructureVersion,
        .render_target = renderTargetHandle,
        .array_slice = 0,
        .cube_map_face = cubeMapFace};
    return cna_graphics_device_set_render_targets(graphicsDeviceHandle, &binding, 1U);
}

CNA_Result cna_graphics_device_get_render_target_count(
    const CNA_Handle graphicsDeviceHandle,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() {
        if (outCount == nullptr) {
            return InvalidArgument("The render-target count output is null.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto found = activeBindings.find(graphicsDevice->value);
        *outCount = found == activeBindings.end() ? 0U : found->second.size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_copy_render_targets(
    const CNA_Handle graphicsDeviceHandle,
    CNA_RenderTargetBinding* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() {
        if (outCount == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The render-target binding output buffer is invalid.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto found = activeBindings.find(graphicsDevice->value);
        const uint64_t count = found == activeBindings.end() ? 0U : found->second.size();
        *outCount = count;
        if (capacity < count) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The render-target binding output buffer is too small.");
        }
        if (count != 0U) {
            std::memcpy(
                destination,
                found->second.data(),
                static_cast<std::size_t>(count) * sizeof(CNA_RenderTargetBinding));
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_render_target_destroy(const CNA_Handle renderTargetHandle)
{
    return CallWithExceptionBarrier([&]() {
        ObjectKind kind = ObjectKind::Unknown;
        if (const CNA_Result result = GetRuntimeHandles().GetKind(renderTargetHandle, &kind);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The render-target handle is invalid for destruction.");
        }
        if (kind == ObjectKind::RenderTarget2D) {
            return cna_texture2d_destroy(renderTargetHandle);
        }
        if (kind != ObjectKind::RenderTargetCube) {
            return Fail(
                CNA_RESULT_INVALID_HANDLE,
                CNA_ERROR_CATEGORY_HANDLE,
                "The handle does not refer to a render target.");
        }

        std::shared_ptr<RenderTargetCubeResource> resource;
        if (const CNA_Result result = GetRenderTargetCube(renderTargetHandle, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (resource->activeEffectReferenceCount != 0U) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The RenderTargetCube is retained by an EffectParameter.");
        }
        resource->value->Dispose();
        const CNA_Result releaseResult = GetRuntimeHandles().Release(renderTargetHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The owned RenderTargetCube handle could not be released.");
        }
        RemoveOwnedGraphicsResourceFor(resource->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

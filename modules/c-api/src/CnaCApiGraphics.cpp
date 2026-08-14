// SPDX-License-Identifier: MS-PL

#include "CNA/C/graphics.h"
#include "CnaCApiRuntimeDetail.hpp"

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstring>
#include <limits>
#include <memory>
#include <string_view>
#include <vector>

namespace {

using CNA::C::Detail::AddOwnedGraphicsResource;
using CNA::C::Detail::BorrowedGraphicsDevice;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CheckedElementByteCount;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetBorrowedGraphicsDevice;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::RemoveOwnedGraphicsResource;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;

constexpr uint32_t StructureVersion = UINT32_C(1);

struct Texture2DResource final {
    std::shared_ptr<Texture2D> value;
    CNA_Handle parentGame;
};

[[nodiscard]] bool TryMapGraphicsCapability(
    const CNA_GraphicsCapability capability,
    CNA::GraphicsCapability* const outCapability) noexcept
{
    if (outCapability == nullptr) {
        return false;
    }
    switch (capability) {
        case CNA_GRAPHICS_CAPABILITY_THREE_D:
            *outCapability = CNA::GraphicsCapability::ThreeD;
            return true;
        case CNA_GRAPHICS_CAPABILITY_DEPTH_STENCIL_BUFFER:
            *outCapability = CNA::GraphicsCapability::DepthStencilBuffer;
            return true;
        case CNA_GRAPHICS_CAPABILITY_MULTI_SAMPLE_ANTI_ALIASING:
            *outCapability = CNA::GraphicsCapability::MultiSampleAntiAliasing;
            return true;
        case CNA_GRAPHICS_CAPABILITY_MULTIPLE_RENDER_TARGETS:
            *outCapability = CNA::GraphicsCapability::MultipleRenderTargets;
            return true;
        case CNA_GRAPHICS_CAPABILITY_ANISOTROPIC_FILTERING:
            *outCapability = CNA::GraphicsCapability::AnisotropicFiltering;
            return true;
        case CNA_GRAPHICS_CAPABILITY_WIRE_FRAME:
            *outCapability = CNA::GraphicsCapability::WireFrame;
            return true;
        case CNA_GRAPHICS_CAPABILITY_OCCLUSION_QUERY:
            *outCapability = CNA::GraphicsCapability::OcclusionQuery;
            return true;
        case CNA_GRAPHICS_CAPABILITY_CUSTOM_EFFECTS:
            *outCapability = CNA::GraphicsCapability::CustomEffects;
            return true;
        case CNA_GRAPHICS_CAPABILITY_TEXTURE_3D:
            *outCapability = CNA::GraphicsCapability::Texture3D;
            return true;
        case CNA_GRAPHICS_CAPABILITY_MULTI_STREAM_VERTEX_INPUT:
            *outCapability = CNA::GraphicsCapability::MultiStreamVertexInput;
            return true;
        case CNA_GRAPHICS_CAPABILITY_INSTANCING:
            *outCapability = CNA::GraphicsCapability::Instancing;
            return true;
        case CNA_GRAPHICS_CAPABILITY_STENCIL_BUFFER:
            *outCapability = CNA::GraphicsCapability::StencilBuffer;
            return true;
        case CNA_GRAPHICS_CAPABILITY_ADDITIVE_BLENDING:
            *outCapability = CNA::GraphicsCapability::AdditiveBlending;
            return true;
        default:
            return false;
    }
}

[[nodiscard]] CNA_GraphicsRendererType MapGraphicsRendererType(
    const CNA::GraphicsRendererType rendererType) noexcept
{
    switch (rendererType) {
        case CNA::GraphicsRendererType::SdlRenderer: return CNA_GRAPHICS_RENDERER_SDL_RENDERER;
        case CNA::GraphicsRendererType::OpenGLES2: return CNA_GRAPHICS_RENDERER_OPENGLES2;
        case CNA::GraphicsRendererType::OpenGLES3: return CNA_GRAPHICS_RENDERER_OPENGLES3;
        case CNA::GraphicsRendererType::OpenGL33: return CNA_GRAPHICS_RENDERER_OPENGL33;
        case CNA::GraphicsRendererType::WebGL1: return CNA_GRAPHICS_RENDERER_WEBGL1;
        case CNA::GraphicsRendererType::WebGL2: return CNA_GRAPHICS_RENDERER_WEBGL2;
        case CNA::GraphicsRendererType::Bgfx: return CNA_GRAPHICS_RENDERER_BGFX;
        case CNA::GraphicsRendererType::Vulkan: return CNA_GRAPHICS_RENDERER_VULKAN;
        case CNA::GraphicsRendererType::WebGPU: return CNA_GRAPHICS_RENDERER_WEBGPU;
        case CNA::GraphicsRendererType::Magnum: return CNA_GRAPHICS_RENDERER_MAGNUM;
        case CNA::GraphicsRendererType::Headless: return CNA_GRAPHICS_RENDERER_HEADLESS;
        case CNA::GraphicsRendererType::Software: return CNA_GRAPHICS_RENDERER_SOFTWARE;
        case CNA::GraphicsRendererType::Stub: return CNA_GRAPHICS_RENDERER_STUB;
        case CNA::GraphicsRendererType::DirectX11: return CNA_GRAPHICS_RENDERER_DIRECTX11;
        case CNA::GraphicsRendererType::DirectX12: return CNA_GRAPHICS_RENDERER_DIRECTX12;
        case CNA::GraphicsRendererType::Direct2D: return CNA_GRAPHICS_RENDERER_DIRECT2D;
        case CNA::GraphicsRendererType::Canvas: return CNA_GRAPHICS_RENDERER_CANVAS;
        case CNA::GraphicsRendererType::HtmlDom: return CNA_GRAPHICS_RENDERER_HTML_DOM;
        case CNA::GraphicsRendererType::Skia: return CNA_GRAPHICS_RENDERER_SKIA;
        case CNA::GraphicsRendererType::Blend2D: return CNA_GRAPHICS_RENDERER_BLEND2D;
        case CNA::GraphicsRendererType::FreeDirect: return CNA_GRAPHICS_RENDERER_FREEDIRECT;
        case CNA::GraphicsRendererType::DirectX9: return CNA_GRAPHICS_RENDERER_DIRECTX9;
        case CNA::GraphicsRendererType::DirectX1: return CNA_GRAPHICS_RENDERER_DIRECTX1;
        case CNA::GraphicsRendererType::DirectX2: return CNA_GRAPHICS_RENDERER_DIRECTX2;
        case CNA::GraphicsRendererType::DirectX3: return CNA_GRAPHICS_RENDERER_DIRECTX3;
        case CNA::GraphicsRendererType::DirectX5: return CNA_GRAPHICS_RENDERER_DIRECTX5;
        case CNA::GraphicsRendererType::DirectX6: return CNA_GRAPHICS_RENDERER_DIRECTX6;
        case CNA::GraphicsRendererType::DirectX7: return CNA_GRAPHICS_RENDERER_DIRECTX7;
        case CNA::GraphicsRendererType::DirectX8: return CNA_GRAPHICS_RENDERER_DIRECTX8;
        case CNA::GraphicsRendererType::DirectX10: return CNA_GRAPHICS_RENDERER_DIRECTX10;
        case CNA::GraphicsRendererType::SdlGpu: return CNA_GRAPHICS_RENDERER_SDL_GPU;
        case CNA::GraphicsRendererType::OpenGLES1: return CNA_GRAPHICS_RENDERER_OPENGLES1;
        case CNA::GraphicsRendererType::OpenGL4: return CNA_GRAPHICS_RENDERER_OPENGL4;
        case CNA::GraphicsRendererType::OpenGL1: return CNA_GRAPHICS_RENDERER_OPENGL1;
        case CNA::GraphicsRendererType::OpenGL2: return CNA_GRAPHICS_RENDERER_OPENGL2;
        case CNA::GraphicsRendererType::Wicked: return CNA_GRAPHICS_RENDERER_WICKED;
        case CNA::GraphicsRendererType::Sokol: return CNA_GRAPHICS_RENDERER_SOKOL;
        case CNA::GraphicsRendererType::Diligent: return CNA_GRAPHICS_RENDERER_DILIGENT;
        case CNA::GraphicsRendererType::Glide: return CNA_GRAPHICS_RENDERER_GLIDE;
        case CNA::GraphicsRendererType::Gdi: return CNA_GRAPHICS_RENDERER_GDI;
        case CNA::GraphicsRendererType::Llgl: return CNA_GRAPHICS_RENDERER_LLGL;
        case CNA::GraphicsRendererType::Metal: return CNA_GRAPHICS_RENDERER_METAL;
        case CNA::GraphicsRendererType::Fna3d: return CNA_GRAPHICS_RENDERER_FNA3D;
        case CNA::GraphicsRendererType::SvgDom: return CNA_GRAPHICS_RENDERER_SVG_DOM;
        case CNA::GraphicsRendererType::OpenVg: return CNA_GRAPHICS_RENDERER_OPENVG;
        case CNA::GraphicsRendererType::PortableGL: return CNA_GRAPHICS_RENDERER_PORTABLEGL;
    }
    return CNA_GRAPHICS_RENDERER_UNKNOWN;
}

[[nodiscard]] CNA_SurfaceFormat MapSurfaceFormat(const SurfaceFormat format) noexcept
{
    switch (format) {
        case SurfaceFormat::Color: return CNA_SURFACE_FORMAT_COLOR;
        case SurfaceFormat::Bgr565: return CNA_SURFACE_FORMAT_BGR565;
        case SurfaceFormat::Bgra5551: return CNA_SURFACE_FORMAT_BGRA5551;
        case SurfaceFormat::Bgra4444: return CNA_SURFACE_FORMAT_BGRA4444;
        case SurfaceFormat::Dxt1: return CNA_SURFACE_FORMAT_DXT1;
        case SurfaceFormat::Dxt3: return CNA_SURFACE_FORMAT_DXT3;
        case SurfaceFormat::Dxt5: return CNA_SURFACE_FORMAT_DXT5;
        case SurfaceFormat::NormalizedByte2: return CNA_SURFACE_FORMAT_NORMALIZED_BYTE2;
        case SurfaceFormat::NormalizedByte4: return CNA_SURFACE_FORMAT_NORMALIZED_BYTE4;
        case SurfaceFormat::Rgba1010102: return CNA_SURFACE_FORMAT_RGBA1010102;
        case SurfaceFormat::Rg32: return CNA_SURFACE_FORMAT_RG32;
        case SurfaceFormat::Rgba64: return CNA_SURFACE_FORMAT_RGBA64;
        case SurfaceFormat::Alpha8: return CNA_SURFACE_FORMAT_ALPHA8;
        case SurfaceFormat::Single: return CNA_SURFACE_FORMAT_SINGLE;
        case SurfaceFormat::Vector2: return CNA_SURFACE_FORMAT_VECTOR2;
        case SurfaceFormat::Vector4: return CNA_SURFACE_FORMAT_VECTOR4;
        case SurfaceFormat::HalfSingle: return CNA_SURFACE_FORMAT_HALF_SINGLE;
        case SurfaceFormat::HalfVector2: return CNA_SURFACE_FORMAT_HALF_VECTOR2;
        case SurfaceFormat::HalfVector4: return CNA_SURFACE_FORMAT_HALF_VECTOR4;
        case SurfaceFormat::HdrBlendable: return CNA_SURFACE_FORMAT_HDR_BLENDABLE;
        case SurfaceFormat::ColorBgraEXT: return CNA_SURFACE_FORMAT_COLOR_BGRA_EXT;
        case SurfaceFormat::ColorSrgbEXT: return CNA_SURFACE_FORMAT_COLOR_SRGB_EXT;
        case SurfaceFormat::Dxt5SrgbEXT: return CNA_SURFACE_FORMAT_DXT5_SRGB_EXT;
        case SurfaceFormat::Bc7EXT: return CNA_SURFACE_FORMAT_BC7_EXT;
        case SurfaceFormat::Bc7SrgbEXT: return CNA_SURFACE_FORMAT_BC7_SRGB_EXT;
        case SurfaceFormat::ByteEXT: return CNA_SURFACE_FORMAT_BYTE_EXT;
        case SurfaceFormat::UShortEXT: return CNA_SURFACE_FORMAT_USHORT_EXT;
    }
    return CNA_SURFACE_FORMAT_COLOR;
}

[[nodiscard]] CNA_GraphicsCapabilityFlags GetGraphicsCapabilityFlags(
    GraphicsDevice& graphicsDevice)
{
    CNA_GraphicsCapabilityFlags flags = UINT64_C(0);
    for (CNA_GraphicsCapability capability = CNA_GRAPHICS_CAPABILITY_THREE_D;
         capability <= CNA_GRAPHICS_CAPABILITY_ADDITIVE_BLENDING;
         ++capability) {
        CNA::GraphicsCapability nativeCapability{};
        if (TryMapGraphicsCapability(capability, &nativeCapability) &&
            graphicsDevice.SupportsCapability(nativeCapability)) {
            flags |= UINT64_C(1) << capability;
        }
    }
    return flags;
}

[[nodiscard]] CNA_Result GetTexture2D(
    const CNA_Handle handle,
    std::shared_ptr<Texture2DResource>* const outTexture)
{
    const CNA_Result result = GetRuntimeHandles().Get(handle, ObjectKind::Texture2D, outTexture);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned Texture2D handle is invalid for this call.");
}

[[nodiscard]] uint64_t GetTexturePixelCount(const Texture2D& texture) noexcept
{
    return static_cast<uint64_t>(texture.getWidthProperty()) *
        static_cast<uint64_t>(texture.getHeightProperty());
}

} // namespace

CNA_Result cna_graphics_device_get_renderer_info(
    const CNA_Handle graphicsDeviceHandle,
    CNA_RendererInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() {
        if (outInfo == nullptr || outInfo->struct_size < sizeof(CNA_RendererInfo) ||
            outInfo->struct_version != StructureVersion) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The renderer-info output structure is invalid.");
        }

        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle,
                &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        GraphicsDevice& nativeDevice = *graphicsDevice->value;
        const std::string_view rendererName = nativeDevice.GetGraphicsRendererName();
        const int maxTextureDimension = nativeDevice.GetMaxTextureDimension();
        if (maxTextureDimension < 0) {
            return Fail(
                CNA_RESULT_INTERNAL,
                CNA_ERROR_CATEGORY_INTERNAL,
                "The native renderer reported a negative texture-dimension limit.");
        }
        const CNA_RendererInfo info = {
            .struct_size = sizeof(CNA_RendererInfo),
            .struct_version = StructureVersion,
            .renderer_name_byte_length = rendererName.size(),
            .capability_flags = GetGraphicsCapabilityFlags(nativeDevice),
            .renderer_type = MapGraphicsRendererType(nativeDevice.GetGraphicsRendererType()),
            .max_texture_dimension = static_cast<uint32_t>(maxTextureDimension)
        };
        *outInfo = info;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_get_renderer_name_size(
    const CNA_Handle graphicsDeviceHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() {
        if (outBytes == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The renderer-name size output is null.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle,
                &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = graphicsDevice->value->GetGraphicsRendererName().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_copy_renderer_name(
    const CNA_Handle graphicsDeviceHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() {
        if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The renderer-name output buffer is invalid.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle,
                &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        const std::string_view rendererName = graphicsDevice->value->GetGraphicsRendererName();
        *outBytes = rendererName.size();
        if (capacity < rendererName.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The renderer-name output buffer is too small.");
        }
        if (!rendererName.empty()) {
            std::memcpy(destination, rendererName.data(), rendererName.size());
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_supports_capability(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_GraphicsCapability capability,
    CNA_Bool* const outSupported)
{
    return CallWithExceptionBarrier([&]() {
        CNA::GraphicsCapability nativeCapability{};
        if (outSupported == nullptr || !TryMapGraphicsCapability(capability, &nativeCapability)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The graphics-capability query arguments are invalid.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle,
                &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outSupported = graphicsDevice->value->SupportsCapability(nativeCapability)
            ? CNA_TRUE
            : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_texture2d_create(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_Texture2DCreateInfo* const createInfo,
    CNA_Handle* const outTexture)
{
    return CallWithExceptionBarrier([&]() {
        if (outTexture == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The Texture2D output handle is null.");
        }
        *outTexture = CNA_INVALID_HANDLE;
        if (createInfo == nullptr || createInfo->struct_size < sizeof(CNA_Texture2DCreateInfo) ||
            createInfo->struct_version != StructureVersion || createInfo->width == 0U ||
            createInfo->height == 0U ||
            (createInfo->mip_map != CNA_FALSE && createInfo->mip_map != CNA_TRUE) ||
            createInfo->reserved[0] != 0U || createInfo->reserved[1] != 0U ||
            createInfo->reserved[2] != 0U) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The Texture2D creation configuration is invalid.");
        }
        if (createInfo->format > CNA_SURFACE_FORMAT_USHORT_EXT) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The Texture2D surface-format identity is invalid.");
        }
        if (createInfo->format != CNA_SURFACE_FORMAT_COLOR) {
            return Fail(
                CNA_RESULT_NOT_SUPPORTED,
                CNA_ERROR_CATEGORY_NOT_SUPPORTED,
                "The initial C Texture2D slice supports only CNA_SURFACE_FORMAT_COLOR.");
        }
        if (createInfo->width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
            createInfo->height > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
            static_cast<uint64_t>(createInfo->width) * createInfo->height >
                static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            return Fail(
                CNA_RESULT_OVERFLOW,
                CNA_ERROR_CATEGORY_RANGE,
                "The Texture2D dimensions exceed the native transfer range.");
        }

        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle,
                &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const int maxTextureDimension = graphicsDevice->value->GetMaxTextureDimension();
        if (maxTextureDimension < 0) {
            return Fail(
                CNA_RESULT_INTERNAL,
                CNA_ERROR_CATEGORY_INTERNAL,
                "The native renderer reported a negative texture-dimension limit.");
        }
        if (createInfo->width > static_cast<uint32_t>(maxTextureDimension) ||
            createInfo->height > static_cast<uint32_t>(maxTextureDimension)) {
            return Fail(
                CNA_RESULT_NOT_SUPPORTED,
                CNA_ERROR_CATEGORY_NOT_SUPPORTED,
                "The Texture2D dimensions exceed the active renderer's maximum.");
        }

        const auto texture = std::make_shared<Texture2D>(
            *graphicsDevice->value,
            static_cast<int>(createInfo->width),
            static_cast<int>(createInfo->height),
            createInfo->mip_map == CNA_TRUE,
            SurfaceFormat::Color);
        const auto resource = std::make_shared<Texture2DResource>(
            Texture2DResource{texture, graphicsDevice->parentGame});
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::Texture2D,
            resource,
            outTexture);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned Texture2D handle could not be created.");
        }
        AddOwnedGraphicsResource();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_texture2d_get_info(
    const CNA_Handle textureHandle,
    CNA_Texture2DInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() {
        if (outInfo == nullptr || outInfo->struct_size < sizeof(CNA_Texture2DInfo) ||
            outInfo->struct_version != StructureVersion) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The Texture2D info output structure is invalid.");
        }
        std::shared_ptr<Texture2DResource> texture;
        if (const CNA_Result result = GetTexture2D(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Texture2DInfo info = {
            .struct_size = sizeof(CNA_Texture2DInfo),
            .struct_version = StructureVersion,
            .width = static_cast<uint32_t>(texture->value->getWidthProperty()),
            .height = static_cast<uint32_t>(texture->value->getHeightProperty()),
            .level_count = static_cast<uint32_t>(texture->value->getLevelCountProperty()),
            .format = MapSurfaceFormat(texture->value->getFormatProperty())
        };
        *outInfo = info;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_texture2d_set_data_rgba8(
    const CNA_Handle textureHandle,
    const CNA_Color* const pixels,
    const uint64_t pixelCount)
{
    return CallWithExceptionBarrier([&]() {
        std::size_t ignoredByteCount = 0U;
        if (const CNA_Result validationResult = CheckedElementByteCount(
                pixels,
                pixelCount,
                sizeof(CNA_Color),
                &ignoredByteCount);
            validationResult != CNA_RESULT_SUCCESS) {
            return Fail(
                validationResult,
                ErrorCategoryForResult(validationResult),
                "The Texture2D upload buffer is invalid.");
        }
        std::shared_ptr<Texture2DResource> texture;
        if (const CNA_Result result = GetTexture2D(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const uint64_t requiredPixels = GetTexturePixelCount(*texture->value);
        if (pixelCount != requiredPixels) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The Texture2D upload pixel count must equal width multiplied by height.");
        }

        std::vector<Color> nativePixels;
        nativePixels.reserve(static_cast<std::size_t>(requiredPixels));
        for (uint64_t index = 0U; index < requiredPixels; ++index) {
            nativePixels.emplace_back(
                pixels[index].r,
                pixels[index].g,
                pixels[index].b,
                pixels[index].a);
        }
        texture->value->SetData(nativePixels.data(), static_cast<int>(nativePixels.size()));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_texture2d_get_data_rgba8(
    const CNA_Handle textureHandle,
    CNA_Color* const destination,
    const uint64_t capacity,
    uint64_t* const outPixels)
{
    return CallWithExceptionBarrier([&]() {
        if (outPixels == nullptr || (destination == nullptr && capacity != 0U)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The Texture2D readback buffer is invalid.");
        }
        std::shared_ptr<Texture2DResource> texture;
        if (const CNA_Result result = GetTexture2D(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const uint64_t requiredPixels = GetTexturePixelCount(*texture->value);
        *outPixels = requiredPixels;
        if (capacity < requiredPixels) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The Texture2D readback buffer is too small.");
        }

        std::vector<Color> nativePixels;
        nativePixels.reserve(static_cast<std::size_t>(requiredPixels));
        for (uint64_t index = 0U; index < requiredPixels; ++index) {
            nativePixels.emplace_back(
                UINT8_C(0), UINT8_C(0), UINT8_C(0), UINT8_C(0));
        }
        texture->value->GetData(nativePixels.data(), static_cast<int>(nativePixels.size()));
        for (uint64_t index = 0U; index < requiredPixels; ++index) {
            destination[index] = CNA_Color{
                .r = nativePixels[index].getRProperty(),
                .g = nativePixels[index].getGProperty(),
                .b = nativePixels[index].getBProperty(),
                .a = nativePixels[index].getAProperty()
            };
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_texture2d_destroy(const CNA_Handle textureHandle)
{
    return CallWithExceptionBarrier([&]() {
        std::shared_ptr<Texture2DResource> texture;
        if (const CNA_Result result = GetTexture2D(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        texture->value->Dispose();
        const CNA_Result releaseResult = GetRuntimeHandles().Release(textureHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The owned Texture2D handle could not be released.");
        }
        RemoveOwnedGraphicsResource();
        return CNA_RESULT_SUCCESS;
    });
}

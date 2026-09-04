// SPDX-License-Identifier: MS-PL
#pragma once

// Family-private translation between XNA's raw state ordinals and IGL's own enums. Kept beside the
// implementation rather than in include/ because nothing outside this renderer may depend on it:
// the ordinals are the shared IGraphicsRenderer contract, and the IGL enums are an implementation
// detail of this one family.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Igl/IglShaderLibrary.hpp"
#include "CNA/Internal/Renderers/Igl/IglSurfaceFormats.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include <igl/DepthStencilState.h>
#include <igl/RenderPipelineState.h>
#include <igl/SamplerState.h>
#include <igl/Texture.h>
#include <igl/VertexInputState.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Renderers::Igl
{
    using VertexElementUsage = Microsoft::Xna::Framework::Graphics::VertexElementUsage;

    /** @brief Maps an XNA `Blend` ordinal to IGL's blend factor. */
    [[nodiscard]] inline igl::BlendFactor ToIglBlendFactor(const int blend)
    {
        switch (blend)
        {
            case 0:  return igl::BlendFactor::One;
            case 1:  return igl::BlendFactor::Zero;
            case 2:  return igl::BlendFactor::SrcColor;
            case 3:  return igl::BlendFactor::OneMinusSrcColor;
            case 4:  return igl::BlendFactor::SrcAlpha;
            case 5:  return igl::BlendFactor::OneMinusSrcAlpha;
            case 6:  return igl::BlendFactor::DstColor;
            case 7:  return igl::BlendFactor::OneMinusDstColor;
            case 8:  return igl::BlendFactor::DstAlpha;
            case 9:  return igl::BlendFactor::OneMinusDstAlpha;
            case 10: return igl::BlendFactor::BlendColor;
            case 11: return igl::BlendFactor::OneMinusBlendColor;
            case 12: return igl::BlendFactor::SrcAlphaSaturated;
            default: return igl::BlendFactor::One;
        }
    }

    /** @brief Maps an XNA `BlendFunction` ordinal to IGL's blend operation. */
    [[nodiscard]] inline igl::BlendOp ToIglBlendOp(const int blendFunction)
    {
        switch (blendFunction)
        {
            case 0:  return igl::BlendOp::Add;
            case 1:  return igl::BlendOp::Subtract;
            case 2:  return igl::BlendOp::ReverseSubtract;
            case 3:  return igl::BlendOp::Max;
            case 4:  return igl::BlendOp::Min;
            default: return igl::BlendOp::Add;
        }
    }

    /** @brief Maps an XNA `CompareFunction` ordinal to IGL's comparison. */
    [[nodiscard]] inline igl::CompareFunction ToIglCompareFunction(const int compareFunction)
    {
        switch (compareFunction)
        {
            case 0:  return igl::CompareFunction::AlwaysPass;
            case 1:  return igl::CompareFunction::Never;
            case 2:  return igl::CompareFunction::Less;
            case 3:  return igl::CompareFunction::LessEqual;
            case 4:  return igl::CompareFunction::Equal;
            case 5:  return igl::CompareFunction::GreaterEqual;
            case 6:  return igl::CompareFunction::Greater;
            case 7:  return igl::CompareFunction::NotEqual;
            default: return igl::CompareFunction::AlwaysPass;
        }
    }

    /**
     * @brief Maps an XNA `StencilOperation` ordinal to IGL's stencil operation.
     *
     * XNA's `Increment`/`Decrement` wrap and its `*Saturation` variants clamp, which is the reverse
     * of the naming most APIs use -- IGL's `IncrementWrap` is XNA's `Increment`, and its
     * `IncrementClamp` is XNA's `IncrementSaturation`.
     */
    [[nodiscard]] inline igl::StencilOperation ToIglStencilOperation(const int stencilOperation)
    {
        switch (stencilOperation)
        {
            case 0:  return igl::StencilOperation::Keep;
            case 1:  return igl::StencilOperation::Zero;
            case 2:  return igl::StencilOperation::Replace;
            case 3:  return igl::StencilOperation::IncrementWrap;
            case 4:  return igl::StencilOperation::DecrementWrap;
            case 5:  return igl::StencilOperation::IncrementClamp;
            case 6:  return igl::StencilOperation::DecrementClamp;
            case 7:  return igl::StencilOperation::Invert;
            default: return igl::StencilOperation::Keep;
        }
    }

    /** @brief Maps an XNA `CullMode` ordinal to IGL's cull mode. */
    [[nodiscard]] inline igl::CullMode ToIglCullMode(const int cullMode)
    {
        switch (cullMode)
        {
            case 1:  return igl::CullMode::Front;
            case 2:  return igl::CullMode::Back;
            default: return igl::CullMode::Disabled;
        }
    }

    /** @brief Maps an XNA `FillMode` ordinal to IGL's polygon fill mode. */
    [[nodiscard]] inline igl::PolygonFillMode ToIglFillMode(const int fillMode)
    {
        return fillMode == 1 ? igl::PolygonFillMode::Line : igl::PolygonFillMode::Fill;
    }

    /** @brief Maps an XNA `TextureFilter` ordinal to IGL's minification filter. */
    [[nodiscard]] inline igl::SamplerMinMagFilter ToIglMinFilter(const int textureFilter)
    {
        switch (textureFilter)
        {
            case 1:  // Point
            case 7:  // MinPointMagLinearMipLinear -- minification is Point
            case 8:  // MinPointMagLinearMipPoint
                return igl::SamplerMinMagFilter::Nearest;
            default:
                return igl::SamplerMinMagFilter::Linear;
        }
    }

    /** @brief Maps an XNA `TextureFilter` ordinal to IGL's magnification filter. */
    [[nodiscard]] inline igl::SamplerMinMagFilter ToIglMagFilter(const int textureFilter)
    {
        switch (textureFilter)
        {
            case 1:  // Point
            case 5:  // MinLinearMagPointMipLinear
            case 6:  // MinLinearMagPointMipPoint
                return igl::SamplerMinMagFilter::Nearest;
            default:
                return igl::SamplerMinMagFilter::Linear;
        }
    }

    /** @brief Maps an XNA `TextureFilter` ordinal to IGL's mip filter. */
    [[nodiscard]] inline igl::SamplerMipFilter ToIglMipFilter(const int textureFilter)
    {
        switch (textureFilter)
        {
            case 1:  // Point
            case 3:  // LinearMipPoint
            case 6:  // MinLinearMagPointMipPoint
            case 8:  // MinPointMagLinearMipPoint
                return igl::SamplerMipFilter::Nearest;
            default:
                return igl::SamplerMipFilter::Linear;
        }
    }

    /** @brief Maps an XNA `TextureAddressMode` ordinal to IGL's address mode. */
    [[nodiscard]] inline igl::SamplerAddressMode ToIglAddressMode(const int addressMode)
    {
        switch (addressMode)
        {
            case 1:  return igl::SamplerAddressMode::Clamp;
            case 2:  return igl::SamplerAddressMode::MirrorRepeat;
            default: return igl::SamplerAddressMode::Repeat;
        }
    }

    /** @brief Maps an XNA `PrimitiveType` to IGL's topology. */
    [[nodiscard]] inline igl::PrimitiveType ToIglPrimitiveType(
        const Microsoft::Xna::Framework::Graphics::PrimitiveType primitive)
    {
        using Xna = Microsoft::Xna::Framework::Graphics::PrimitiveType;
        switch (primitive)
        {
            case Xna::TriangleList:  return igl::PrimitiveType::Triangle;
            case Xna::TriangleStrip: return igl::PrimitiveType::TriangleStrip;
            case Xna::LineList:      return igl::PrimitiveType::Line;
            case Xna::LineStrip:     return igl::PrimitiveType::LineStrip;
            case Xna::PointListEXT:  return igl::PrimitiveType::Point;
        }
        return igl::PrimitiveType::Triangle;
    }

    /**
     * @brief Returns how many vertices @p primitiveCount primitives of @p primitive consume.
     *
     * @param primitive      Topology.
     * @param primitiveCount Number of primitives.
     * @return The vertex or index count the draw call needs.
     */
    [[nodiscard]] inline int VertexCountForPrimitives(
        const Microsoft::Xna::Framework::Graphics::PrimitiveType primitive,
        const int primitiveCount)
    {
        using Xna = Microsoft::Xna::Framework::Graphics::PrimitiveType;
        switch (primitive)
        {
            case Xna::TriangleList:  return primitiveCount * 3;
            case Xna::TriangleStrip: return primitiveCount + 2;
            case Xna::LineList:      return primitiveCount * 2;
            case Xna::LineStrip:     return primitiveCount + 1;
            case Xna::PointListEXT:  return primitiveCount;
        }
        return primitiveCount * 3;
    }

    // The XNA-SurfaceFormat-to-IGL mapping and the transfer byte arithmetic live in
    // IglSurfaceFormats.{hpp,cpp}, included above: they are a real table with real refusals rather
    // than a translation, they are shared with the renderer's own format-capability answers, and
    // they are the part of this family a host-portable unit test can measure without a device.

    /**
     * @brief Maps an XNA `DepthFormat` ordinal to an IGL depth/stencil format.
     *
     * @param depthFormat Raw ordinal (None=0, Depth16=1, Depth24=2, Depth24Stencil8=3).
     * @return The matching format, or `Invalid` when no depth surface was requested.
     */
    [[nodiscard]] inline igl::TextureFormat ToIglDepthFormat(const int depthFormat)
    {
        switch (depthFormat)
        {
            case 1:  return igl::TextureFormat::Z_UNorm16;
            case 2:  return igl::TextureFormat::Z_UNorm24;
            case 3:  return igl::TextureFormat::S8_UInt_Z24_UNorm;
            default: return igl::TextureFormat::Invalid;
        }
    }

    /** @brief Returns whether an XNA `DepthFormat` ordinal carries a stencil plane. */
    [[nodiscard]] inline bool DepthFormatHasStencil(const int depthFormat)
    {
        return depthFormat == 3;
    }

    /** @brief Maps an XNA `VertexElementFormat` to IGL's vertex attribute format. */
    [[nodiscard]] inline igl::VertexAttributeFormat ToIglVertexFormat(
        const Microsoft::Xna::Framework::Graphics::VertexElementFormat format)
    {
        using Xna = Microsoft::Xna::Framework::Graphics::VertexElementFormat;
        switch (format)
        {
            case Xna::Single:           return igl::VertexAttributeFormat::Float1;
            case Xna::Vector2:          return igl::VertexAttributeFormat::Float2;
            case Xna::Vector3:          return igl::VertexAttributeFormat::Float3;
            case Xna::Vector4:          return igl::VertexAttributeFormat::Float4;
            case Xna::Color:            return igl::VertexAttributeFormat::UByte4Norm;
            case Xna::Byte4:            return igl::VertexAttributeFormat::UByte4;
            case Xna::Short2:           return igl::VertexAttributeFormat::Short2;
            case Xna::Short4:           return igl::VertexAttributeFormat::Short4;
            case Xna::NormalizedShort2: return igl::VertexAttributeFormat::Short2Norm;
            case Xna::NormalizedShort4: return igl::VertexAttributeFormat::Short4Norm;
            case Xna::HalfVector2:      return igl::VertexAttributeFormat::HalfFloat2;
            case Xna::HalfVector4:      return igl::VertexAttributeFormat::HalfFloat4;
        }
        return igl::VertexAttributeFormat::Float4;
    }

    /**
     * @brief Maps an XNA vertex element usage to one of this renderer's shader attribute slots.
     *
     * @param usage      Element usage.
     * @param usageIndex Usage index; only texture coordinates 0 and 1 have distinct slots.
     * @return The slot, or `VertexAttributeSlot::Count` when the element has no shader input.
     */
    [[nodiscard]] inline std::uint32_t ToVertexAttributeSlot(const VertexElementUsage usage,
                                                             const int usageIndex)
    {
        switch (usage)
        {
            case VertexElementUsage::Position:
                return VertexAttributeSlot::Position;
            case VertexElementUsage::Color:
                return VertexAttributeSlot::Color;
            case VertexElementUsage::Normal:
                return VertexAttributeSlot::Normal;
            case VertexElementUsage::Tangent:
                return VertexAttributeSlot::Tangent;
            case VertexElementUsage::BlendIndices:
                return VertexAttributeSlot::BlendIndices;
            case VertexElementUsage::BlendWeight:
                return VertexAttributeSlot::BlendWeights;
            case VertexElementUsage::TextureCoordinate:
                if (usageIndex <= 0) return VertexAttributeSlot::TexCoord0;
                if (usageIndex == 1) return VertexAttributeSlot::TexCoord1;
                return VertexAttributeSlot::Count;
            default:
                // Binormal, Depth, Fog, PointSize, Sample and TessellateFactor have no input in
                // this renderer's generated shaders. They stay in the vertex buffer untouched
                // rather than being silently re-slotted onto an attribute that means something
                // else.
                return VertexAttributeSlot::Count;
        }
    }

    /**
     * @brief Copies an XNA matrix into the 16 floats a GLSL `mat4` uniform expects.
     *
     * XNA stores its matrices row-major and multiplies row vectors on the left; GLSL reads a `mat4`
     * column-major and multiplies column vectors on the right. Reading XNA's own storage order as
     * column-major is precisely the transpose those two conventions differ by, so this is a straight
     * copy rather than a transpose.
     *
     * @param matrix Source matrix.
     * @param out    Destination, 16 floats.
     */
    inline void CopyMatrixToGlsl(const Microsoft::Xna::Framework::Matrix& matrix, float* out)
    {
        std::memcpy(out, &matrix.M11, sizeof(float) * 16);
    }

    /**
     * @brief Rewrites a combined matrix's Z row for an API whose clip space is [-1, 1].
     *
     * XNA's projection matrices target Direct3D's [0, w] clip depth. IGL reports which convention a
     * device uses through `getNormalizedZRange()`; on the OpenGL backend the depth has to be
     * remapped with `z' = 2z - w`, which -- in XNA's row-vector storage -- is one operation per row.
     *
     * @param matrix Matrix to adjust in place, in XNA storage order.
     */
    inline void ApplyNegativeOneToOneDepth(Microsoft::Xna::Framework::Matrix& matrix)
    {
        float* m = &matrix.M11;
        for (int row = 0; row < 4; ++row)
        {
            float* r = m + row * 4;
            r[2] = 2.0f * r[2] - r[3];
        }
    }

    /**
     * @brief Swaps the red and blue bytes of every RGBA8-sized pixel in `data`, if `format` is
     * one IGL reports as a B-first byte order.
     *
     * `igl::IFramebuffer::copyBytesColorAttachment()` (both backends) copies the attachment's raw
     * physical bytes with no channel reordering -- its own `format` parameter is accepted but
     * unused on the Vulkan backend. Every texture CNA itself creates (`RenderTarget2D`,
     * `RenderTargetCube`, `Texture2D`, ...) requests an R-first format explicitly and needs no
     * correction, but the Vulkan back buffer's physical format is whatever the platform's window
     * system surface natively offers -- on this renderer's Linux/X11/Mesa target that is
     * `BGRA_UNorm8`, not `RGBA_UNorm8`, so every read through XNA's RGBA-ordered `Color` without
     * this correction silently swaps red and blue (verified: a pure-red clear (255,0,0) reads back
     * as pure blue (0,0,255); a pure-green clear is unaffected since G is unaffected by an R/B swap
     * and both channels happen to already be zero, which is what made this bug easy to miss when a
     * single pixel check happened to be reused across colours). Checking the ACTUAL texture format (not
     * hardcoding "swap when Vulkan and it's the back buffer") keeps this correct if a future
     * platform's WSI surface ever natively offers RGBA instead.
     *
     * @param data        RGBA8 pixel bytes to correct in place, `pixelCount * 4` bytes long.
     * @param pixelCount  Number of 4-byte pixels in `data`.
     * @param format      The texture's real IGL format, as reported by `ITexture::getFormat()`.
     */
    inline void SwapRedBlueIfBgrOrdered(std::uint8_t* data, const std::size_t pixelCount,
                                        const igl::TextureFormat format)
    {
        // Only the plain byte-order variant is corrected here -- it is the one this renderer's
        // own Vulkan back buffer actually reports (see this function's own doc comment); the
        // packed `_Rev` variant's byte layout has not been verified against a real surface this
        // renderer has ever chosen, so it is deliberately left alone rather than guessed at.
        if (format != igl::TextureFormat::BGRA_UNorm8)
            return;
        for (std::size_t i = 0; i < pixelCount; ++i)
        {
            std::uint8_t* pixel = data + i * 4;
            std::swap(pixel[0], pixel[2]);
        }
    }
}

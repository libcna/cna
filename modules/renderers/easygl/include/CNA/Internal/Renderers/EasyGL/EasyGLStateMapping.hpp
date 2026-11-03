// SPDX-License-Identifier: MS-PL
#pragma once

#include <algorithm>
#include <cstddef>
#include <easygl/easygl.hpp>
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "System/NotSupportedException.hpp"

namespace CNA::Internal::Renderers::EasyGL
{
    using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
    // XNA Blend enum → easygl BlendFactor
    // Blend: One=0, Zero=1, SourceColor=2, InverseSourceColor=3, SourceAlpha=4,
    //        InverseSourceAlpha=5, DestinationColor=6, InverseDestinationColor=7,
    //        DestinationAlpha=8, InverseDestinationAlpha=9, BlendFactor=10,
    //        InverseBlendFactor=11, SourceAlphaSaturation=12
    /**
     * @brief Converts an XNA blend operand to its GL factor.
     *
     * @param xnaBlend XNA Blend ordinal.
     * @return The GL blend factor.
     */
    inline ::easygl::BlendFactor ToEasyGLBlendFactor(int xnaBlend)
    {
        switch (xnaBlend)
        {
        case  1: return ::easygl::BlendFactor::Zero;
        case  2: return ::easygl::BlendFactor::SrcColor;
        case  3: return ::easygl::BlendFactor::OneMinusSrcColor;
        case  4: return ::easygl::BlendFactor::SrcAlpha;
        case  5: return ::easygl::BlendFactor::OneMinusSrcAlpha;
        case  6: return ::easygl::BlendFactor::DstColor;
        case  7: return ::easygl::BlendFactor::OneMinusDstColor;
        case  8: return ::easygl::BlendFactor::DstAlpha;
        case  9: return ::easygl::BlendFactor::OneMinusDstAlpha;
        case 10: return ::easygl::BlendFactor::ConstantColor;
        case 11: return ::easygl::BlendFactor::OneMinusConstantColor;
        case 12: return ::easygl::BlendFactor::SrcAlphaSaturate;
        default: return ::easygl::BlendFactor::One;  // Blend::One = 0
        }
    }

    // XNA BlendFunction enum → easygl BlendEquation
    // BlendFunction: Add=0, Subtract=1, ReverseSubtract=2, Min=3, Max=4
    /**
     * @brief Converts an XNA blend equation to GL.
     *
     * @param xnaBlendFunc XNA BlendFunction ordinal.
     * @return The GL blend equation.
     */
    inline ::easygl::BlendEquation ToEasyGLBlendEquation(int xnaBlendFunc)
    {
        switch (xnaBlendFunc)
        {
        case 1: return ::easygl::BlendEquation::FuncSubtract;
        case 2: return ::easygl::BlendEquation::FuncReverseSubtract;
        case 3: return ::easygl::BlendEquation::Min;
        case 4: return ::easygl::BlendEquation::Max;
        default: return ::easygl::BlendEquation::FuncAdd;  // BlendFunction::Add = 0
        }
    }

    // XNA CompareFunction enum → easygl CompareFunc
    // CompareFunction: Always=0, Never=1, Less=2, LessEqual=3, Equal=4,
    //                  GreaterEqual=5, Greater=6, NotEqual=7
    /**
     * @brief Converts an XNA comparison to GL.
     *
     * @param xnaCompare XNA CompareFunction ordinal.
     * @return The GL comparison function.
     */
    inline ::easygl::CompareFunc ToEasyGLCompareFunc(int xnaCompare)
    {
        switch (xnaCompare)
        {
        case 1: return ::easygl::CompareFunc::Never;
        case 2: return ::easygl::CompareFunc::Less;
        case 3: return ::easygl::CompareFunc::Lequal;
        case 4: return ::easygl::CompareFunc::Equal;
        case 5: return ::easygl::CompareFunc::Gequal;
        case 6: return ::easygl::CompareFunc::Greater;
        case 7: return ::easygl::CompareFunc::Notequal;
        default: return ::easygl::CompareFunc::Always;  // CompareFunction::Always = 0
        }
    }

    // XNA StencilOperation ordinals: Keep=0, Zero=1, Replace=2, Increment=3,
    // Decrement=4, IncrementSaturation=5, DecrementSaturation=6, Invert=7
    /**
     * @brief Converts an XNA stencil operation to GL.
     *
     * @param xnaOp XNA StencilOperation ordinal.
     * @return The GL stencil operation.
     */
    inline ::easygl::StencilOp ToEasyGLStencilOp(int xnaOp)
    {
        switch (xnaOp)
        {
        case 1: return ::easygl::StencilOp::Zero;
        case 2: return ::easygl::StencilOp::Replace;
        case 3: return ::easygl::StencilOp::IncrWrap;
        case 4: return ::easygl::StencilOp::DecrWrap;
        case 5: return ::easygl::StencilOp::Incr;
        case 6: return ::easygl::StencilOp::Decr;
        case 7: return ::easygl::StencilOp::Invert;
        default: return ::easygl::StencilOp::Keep;  // StencilOperation::Keep = 0
        }
    }

    /** @brief GL attribute shape for one XNA vertex element. */
    struct VertexAttribFormat
    {
        /** @brief Number of scalar components. */
        int componentCount;
        /** @brief GL scalar type. */
        ::easygl::DataType type;
        /** @brief Whether integer components are normalized. */
        bool normalized;
        /** @brief Whether the shader input uses integer registers. */
        bool isInteger;
    };

    // Task 1080 / SOFTWARE-130: maps XNA's VertexElementFormat to the GL attribute shape needed
    // to bind it -- component count, GL scalar type, and whether values are normalized to
    // [0,1]/[-1,1]. Stock inputs are floating-point shader registers, including Byte4-backed
    // blend indices, so all XNA formats use GL's converting float attribute path.
    /**
     * @brief Describes a vertex element for GL attribute binding.
     *
     * @param format XNA vertex element format.
     * @return The component count, scalar type and conversion flags.
     */
    inline VertexAttribFormat DescribeVertexElementFormat(VertexElementFormat format)
    {
        switch (format)
        {
        case VertexElementFormat::Single:          return { 1, ::easygl::DataType::Float,        false, false };
        case VertexElementFormat::Vector2:         return { 2, ::easygl::DataType::Float,        false, false };
        case VertexElementFormat::Vector3:         return { 3, ::easygl::DataType::Float,        false, false };
        case VertexElementFormat::Vector4:         return { 4, ::easygl::DataType::Float,        false, false };
        case VertexElementFormat::Color:           return { 4, ::easygl::DataType::UnsignedByte, true,  false };
        case VertexElementFormat::Byte4:
            // FX-127: read as floats on every profile, not as true integers. Byte4 is used
            // exclusively for BLENDINDICES-style bone-index attributes, and the same semantic
            // is equally legal as Vector4 -- XNA's vertex element format describes the bytes,
            // while the shader register is a float4 either way, so a processor is free to
            // write either (CustomModelAnimation's own SkinnedModelProcessor converts
            // BlendIndices0 to Vector4 and real XNA renders it). One shader attribute cannot
            // be both an integer and a float, so the skinned programs declare
            // "in vec4 aBoneIndices" and cast to int() when indexing uBones[], and both
            // formats bind to it as floats. The range is 0-255 at most, far more than the
            // <= 72 bone count needs, and exactly float-representable.
            return { 4, ::easygl::DataType::UnsignedByte, false, false };
        case VertexElementFormat::Short2:          return { 2, ::easygl::DataType::Short,        false, false };
        case VertexElementFormat::Short4:          return { 4, ::easygl::DataType::Short,        false, false };
        case VertexElementFormat::NormalizedShort2:return { 2, ::easygl::DataType::Short,        true,  false };
        case VertexElementFormat::NormalizedShort4:return { 4, ::easygl::DataType::Short,        true,  false };
        case VertexElementFormat::HalfVector2:     return { 2, ::easygl::DataType::HalfFloat,    false, false };
        case VertexElementFormat::HalfVector4:     return { 4, ::easygl::DataType::HalfFloat,    false, false };
        }
        throw System::NotSupportedException(
            "EasyGL: the VertexDeclaration contains an unknown VertexElementFormat.");
    }

    // XNA requests D3D9's complete volume chain, so the largest of all three dimensions controls
    // the level count. FNA's width/height-only calculation truncates depth-dominant volumes.
    /**
     * @brief Counts a complete volume mip chain.
     *
     * @param w Base width.
     * @param h Base height.
     * @param d Base depth.
     * @return Number of levels, including the base level.
     */
    inline int CalculateTexture3DMipLevels(int w, int h, int d)
    {
        int levels = 1;
        while (w > 1 || h > 1 || d > 1)
        {
            w = std::max(1, w / 2);
            h = std::max(1, h / 2);
            d = std::max(1, d / 2);
            ++levels;
        }
        return levels;
    }

    // Mirrors TextureCube.cpp's CalculateMipLevels(size,size) — cube faces are square.
    /**
     * @brief Counts a complete cube-face mip chain.
     *
     * @param size Base face edge length.
     * @return Number of levels, including the base level.
     */
    inline int CalculateCubeMipLevels(int size)
    {
        int levels = 1;
        int s = size;
        while (s > 1) { s = std::max(1, s / 2); ++levels; }
        return levels;
    }

    /// Bytes per 4x4 block: DXT1 carries colour only, DXT3 and DXT5 add an alpha block.
    /**
     * @brief Reports the byte size of a compressed 4 by 4 block.
     *
     * @param format DXT1, DXT3 or DXT5 format.
     * @return Eight bytes for DXT1, sixteen for DXT3 and DXT5.
     */
    inline constexpr std::size_t DxtBlockBytesEXT(
        Microsoft::Xna::Framework::Graphics::SurfaceFormat format)
    {
        return format == Microsoft::Xna::Framework::Graphics::SurfaceFormat::Dxt1 ? 8u : 16u;
    }

    /**
     * @brief Selects the compressed GL storage format.
     *
     * @param format DXT1, DXT3 or DXT5 format.
     * @return The matching S3TC internal format.
     */
    inline ::metagl::CompressedInternalFormat ToDxtInternalFormatEXT(
        Microsoft::Xna::Framework::Graphics::SurfaceFormat format)
    {
            using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        return format == SurfaceFormat::Dxt1
            ? ::metagl::CompressedInternalFormat::RgbaS3tcDxt1
            : (format == SurfaceFormat::Dxt3
                   ? ::metagl::CompressedInternalFormat::RgbaS3tcDxt3
                   : ::metagl::CompressedInternalFormat::RgbaS3tcDxt5);
    }

    /**
     * @brief Counts a complete render-target mip chain.
     *
     * @param w Base width.
     * @param h Base height.
     * @return Number of levels, including the base level.
     */
    inline int CalculateRenderTargetMipLevels(int w, int h)
    {
        int levels = 1;
        while (w > 1 || h > 1) { w = std::max(1, w / 2); h = std::max(1, h / 2); ++levels; }
        return levels;
    }

    // Task 877: maps a Microsoft::Xna::Framework::Graphics::DepthFormat ordinal to the GL
    // renderbuffer internal format and framebuffer attachment point a render target's
    // depth/stencil buffer should use. Returns false for DepthFormat::None, meaning no
    // depth/stencil attachment should be created at all.
    /**
     * @brief Selects depth storage and its framebuffer attachment.
     *
     * @param depthFormat XNA DepthFormat ordinal.
     * @param outFormat Receives the storage format on success.
     * @param outAttachment Receives the attachment point on success.
     * @return True if a depth attachment is required.
     */
    inline bool MapDepthFormat(int depthFormat, ::metagl::InternalFormat& outFormat,
                                ::metagl::FramebufferAttachment& outAttachment)
    {
        using ::Microsoft::Xna::Framework::Graphics::DepthFormat;
        switch (static_cast<DepthFormat>(depthFormat))
        {
        case DepthFormat::Depth16:
            outFormat = ::metagl::InternalFormat::DepthComponent16;
            outAttachment = ::metagl::FramebufferAttachment::Depth;
            return true;
        case DepthFormat::Depth24:
            outFormat = ::metagl::InternalFormat::DepthComponent24;
            outAttachment = ::metagl::FramebufferAttachment::Depth;
            return true;
        case DepthFormat::Depth24Stencil8:
            outFormat = ::metagl::InternalFormat::Depth24Stencil8;
            outAttachment = ::metagl::FramebufferAttachment::DepthStencil;
            return true;
        case DepthFormat::None:
        default:
            return false;
        }
    }

    // MOD-116: maps a Microsoft::Xna::Framework::Graphics::SurfaceFormat ordinal to
    // the GL colour storage a render target of that format needs. Data, not a switch chain buried in
    // the allocation code, because the identical triple is needed in three places: the texture's
    // per-level storage, the multisample colour renderbuffer, and the probe that decides whether
    // this GL context can render to the format at all.
    //
    // The formats CNA actually allocates are listed. Everything else is deliberately absent and
    // refused rather than silently substituted. The two 16-bit normalized layouts are selected
    // only after the runtime texture-norm16/completeness checks below.
    /** @brief GL storage, transfer representation and logical texel size. */
    struct RenderTargetColorStorage
    {
        /** @brief Sized GL internal storage format. */
        ::metagl::InternalFormat internalFormat;
        /** @brief Transfer channel layout. */
        ::metagl::PixelFormat    pixelFormat;
        /** @brief Transfer scalar or packed type. */
        ::metagl::PixelType      pixelType;
        /** @brief Whether floating-point renderability is required. */
        bool                     isFloat;
        /** @brief Whether floating-point channels have 32 bits. */
        bool                     isFullFloat;
        /** @brief Logical readback bytes per texel. */
        int                      bytesPerPixel;
    };

    /// GL_RG16 is supplied by desktop GL and GL_EXT_texture_norm16, but the GLES-generated
    /// metagl enum intentionally names only core formats. Keep the extension token local to the
    /// capability-gated path that uses it.
    /**
     * @brief Names the extension-gated normalized two-channel 16-bit format.
     *
     * @return The GL_RG16 token.
     */
    [[nodiscard]] inline ::metagl::InternalFormat Rg16InternalFormatEXT()
    {
        return static_cast<::metagl::InternalFormat>(0x822C);
    }

    /**
     * @brief Selects render-target color storage and transfer layout.
     *
     * @param surfaceFormat XNA SurfaceFormat ordinal.
     * @param out Receives the layout on success.
     * @return True if the format has a renderer storage mapping; capabilities are checked separately.
     */
    inline bool MapRenderTargetColorFormat(int surfaceFormat, RenderTargetColorStorage& out)
    {
        using ::Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        switch (static_cast<SurfaceFormat>(surfaceFormat))
        {
        case SurfaceFormat::Color:
            out = {::metagl::InternalFormat::Rgba8, ::metagl::PixelFormat::Rgba,
                   ::metagl::PixelType::UnsignedByte, false, false, 4};
            return true;
        case SurfaceFormat::Rgba1010102:
            out = {::metagl::InternalFormat::Rgb10A2, ::metagl::PixelFormat::Rgba,
                   ::metagl::PixelType::UnsignedInt2101010Rev, false, false, 4};
            return true;
        case SurfaceFormat::Rg32:
            out = {Rg16InternalFormatEXT(), ::metagl::PixelFormat::Rg,
                   ::metagl::PixelType::UnsignedShort, false, false, 4};
            return true;
        case SurfaceFormat::Rgba64:
            out = {::metagl::InternalFormat::Rgba16, ::metagl::PixelFormat::Rgba,
                   ::metagl::PixelType::UnsignedShort, false, false, 8};
            return true;
        case SurfaceFormat::Single:
            out = {::metagl::InternalFormat::R32F, ::metagl::PixelFormat::Red,
                   ::metagl::PixelType::Float, true, true, 4};
            return true;
        case SurfaceFormat::Vector2:
            out = {::metagl::InternalFormat::Rg32F, ::metagl::PixelFormat::Rg,
                   ::metagl::PixelType::Float, true, true, 8};
            return true;
        case SurfaceFormat::Vector4:
            out = {::metagl::InternalFormat::Rgba32F, ::metagl::PixelFormat::Rgba,
                   ::metagl::PixelType::Float, true, true, 16};
            return true;
        case SurfaceFormat::HalfSingle:
            out = {::metagl::InternalFormat::R16F, ::metagl::PixelFormat::Red,
                   ::metagl::PixelType::HalfFloat, true, false, 2};
            return true;
        case SurfaceFormat::HalfVector2:
            out = {::metagl::InternalFormat::Rg16F, ::metagl::PixelFormat::Rg,
                   ::metagl::PixelType::HalfFloat, true, false, 4};
            return true;
        case SurfaceFormat::HalfVector4:
        case SurfaceFormat::HdrBlendable:
            // HdrBlendable is XNA's "float format for HDR data"; on Windows it was RGBA16F, and CNA
            // makes that equivalence explicit rather than inventing a third meaning for it.
            out = {::metagl::InternalFormat::Rgba16F, ::metagl::PixelFormat::Rgba,
                   ::metagl::PixelType::HalfFloat, true, false, 8};
            return true;
        default:
            return false;
        }
    }

}

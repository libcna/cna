// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Metal/MetalTextureTransfer.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <cstddef>

namespace CNA::Internal::Renderers::Metal
{
    /** @brief Result of validating the stream shape of a Metal draw. */
    enum class MetalDrawStreamPolicy
    {
        /**
         * @brief Zero streams (legacy/internal), or a consistent list of per-vertex and per-instance
         *        streams (plans/plan_apple_m4.md AM4-143).
         */
        Supported,
        /** @brief The binding count or one binding's metadata is internally inconsistent. */
        InvalidBinding,
    };

    /** @brief Result of validating a Texture2D ImageData allocation/upload request. */
    enum class MetalTexture2DImagePolicy
    {
        /** @brief Color format, dimensions, mip count, and level-zero bytes are exact. */
        Supported,
        /** @brief The requested SurfaceFormat is not Color. */
        UnsupportedFormat,
        /** @brief One or both dimensions, or the mip count, is invalid. */
        InvalidDimensionsOrMipCount,
        /** @brief Level-zero RGBA bytes are undersized, oversized, or overflowed. */
        InvalidBaseByteCount
    };

    /** @brief Synchronous action when native backbuffer attachment acquisition is attempted. */
    enum class MetalFrameEntryPolicy
    {
        /** @brief A render target or drawable is available and submission may proceed. */
        Proceed,
        /** @brief No drawable is currently available; skip this backbuffer frame without error. */
        SkipUnavailableBackbuffer
    };

    /**
     * @brief Classifies transient native attachment availability for a draw entry.
     *
     * @param attachmentAvailable True when the active target or CAMetalDrawable resolved.
     * @return Proceed or a non-error backbuffer-frame skip.
     */
    [[nodiscard]] constexpr MetalFrameEntryPolicy DescribeMetalFrameEntryPolicy(
        bool attachmentAvailable) noexcept
    {
        return attachmentAvailable ? MetalFrameEntryPolicy::Proceed
                                   : MetalFrameEntryPolicy::SkipUnavailableBackbuffer;
    }

    /** @brief Tracks one transient drawable-acquisition failure for a logical frame. */
    class MetalFrameAvailabilityState
    {
    public:
        /**
         * @brief Reports whether this logical frame may attempt CAMetalLayer acquisition.
         * @return False after the first failed attempt and until Present resets the frame.
         */
        [[nodiscard]] bool ShouldAttemptBackbufferAcquisition() const noexcept
        {
            return !unavailableThisFrame_;
        }

        /**
         * @brief Records the result of the frame's one permitted acquisition attempt.
         * @param available True when nextDrawable returned a drawable.
         */
        void RecordBackbufferAcquisition(bool available) noexcept
        {
            if(!available) unavailableThisFrame_=true;
        }

        /** @brief Starts a new logical frame after Present. */
        void EndLogicalFrame() noexcept
        {
            unavailableThisFrame_=false;
        }

        /** @brief Clears cached frame availability after abandoning failed native work. */
        void ResetAfterCommandFailure() noexcept
        {
            unavailableThisFrame_=false;
        }

    private:
        bool unavailableThisFrame_=false;
    };

    /**
     * @brief Reports whether the current Metal contract accepts a draw's stream shape.
     *
     * @param params Renderer draw parameters to inspect without native submission.
     * @return The deterministic supported/rejection classification.
     */
    [[nodiscard]] inline MetalDrawStreamPolicy DescribeMetalDrawStreamPolicy(
        const GpuDrawParams& params)
    {
        if (params.vertexStreamCount < 0 || params.vertexStreamCount > kMaxVertexStreams)
            return MetalDrawStreamPolicy::InvalidBinding;
        if (params.instanceCount < 1 || params.firstInstance < 0)
            return MetalDrawStreamPolicy::InvalidBinding;

        // plans/plan_apple_m4.md AM4-143: every stream is accepted; what is checked is that the
        // per-vertex ones tile the combined vertex in slot order, which the input builder relies on.
        int perVertexBytes=0;
        int perVertexCount=0;
        for (int i=0; i<params.vertexStreamCount; ++i)
        {
            const auto& stream=params.vertexStreams[i];
            // A zero stride is a buffer without a declaration, whose stride the renderer resolves
            // from its upload (GpuVertexStreamBinding::strideInBytes).
            if (!stream.buffer || stream.slot!=i || stream.strideInBytes<0 ||
                stream.vertexOffset<0 || stream.vertexCount<0 || stream.instanceFrequency<0)
                return MetalDrawStreamPolicy::InvalidBinding;
            if (stream.instanceFrequency>0)
                continue;
            if (stream.combinedByteBase!=perVertexBytes)
                return MetalDrawStreamPolicy::InvalidBinding;
            perVertexBytes+=stream.strideInBytes;
            ++perVertexCount;
        }
        if (params.combinedVertexStride!=(perVertexCount>0 ? perVertexBytes : 0))
            return MetalDrawStreamPolicy::InvalidBinding;
        return MetalDrawStreamPolicy::Supported;
    }

    /**
     * @brief Checks that a draw's stream 0 names and advances the uploaded Metal buffer.
     *
     * @param params Renderer draw parameters whose stream metadata was captured for this draw.
     * @param uploadedBuffer Vertex buffer passed to the draw route.
     * @param uploadedStride Byte stride used when that buffer's native storage was uploaded.
     * @return True for the legacy zero-stream shape, or when the one stream names the same buffer
     *         and uses exactly its uploaded stride.
     */
    [[nodiscard]] inline bool MetalStreamZeroMatchesUploadedBuffer(
        const GpuDrawParams& params,
        const IVertexBufferRenderer& uploadedBuffer,
        std::size_t uploadedStride) noexcept
    {
        if (params.vertexStreamCount == 0)
            return true;
        const auto& stream = params.vertexStreams[0];
        return stream.buffer == &uploadedBuffer &&
               (stream.strideInBytes == 0 ||
                static_cast<std::size_t>(stream.strideInBytes) == uploadedStride);
    }

    /**
     * @brief Reports the conservative supported Metal capability contract.
     *
     * @param capability Capability to query.
     * @return True only for a specifically enumerated implemented feature.
     */
    [[nodiscard]] constexpr bool MetalSupportsCapability(CNA::GraphicsCapability capability)
    {
        switch (capability)
        {
            case CNA::GraphicsCapability::ThreeD:                    return true;
            case CNA::GraphicsCapability::DepthStencilBuffer:       return true;
            case CNA::GraphicsCapability::MultiSampleAntiAliasing:  return false;  // per device: MetalRenderer::SupportsCapability (AM4-141)
            case CNA::GraphicsCapability::MultipleRenderTargets:    return true;   // AM4-097
            case CNA::GraphicsCapability::AnisotropicFiltering:     return true;
            case CNA::GraphicsCapability::WireFrame:                return true;
            case CNA::GraphicsCapability::OcclusionQuery:           return true;  // AM4-038
            case CNA::GraphicsCapability::CustomEffects:            return true;   // AM4-077, SpriteBatch-scoped MSL
            case CNA::GraphicsCapability::Texture3D:                return true;
            case CNA::GraphicsCapability::MultiStreamVertexInput:   return true;   // AM4-143
            case CNA::GraphicsCapability::Instancing:               return true;   // AM4-143
            case CNA::GraphicsCapability::StencilBuffer:            return true;
            case CNA::GraphicsCapability::AdditiveBlending:         return true;
        }
        return false;
    }

    /**
     * @brief Reports whether a raw SurfaceFormat ordinal is Metal's supported Color format.
     * @param surfaceFormat Raw Microsoft.Xna.Framework.Graphics.SurfaceFormat ordinal.
     * @return True only for SurfaceFormat::Color.
     */
    [[nodiscard]] constexpr bool MetalSupportsSurfaceFormat(int surfaceFormat)
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        return surfaceFormat==static_cast<int>(SurfaceFormat::Color);
    }

    /**
     * @brief Reports whether a RenderTargetCube accepts a CPU upload (TextureCube.SetData).
     *
     * plans/plan_apple_m4.md AM4-147: a single-sampled cube target stores the face it is given. A
     * multisampled one renders each face into its own multisample texture and only resolves into
     * the cube, so an upload into the cube would be overwritten by the next resolve of stale
     * samples; it is refused rather than accepted and lost.
     *
     * @param appliedSampleCount The target's applied multisample count (0 when single-sampled).
     * @return True when the upload is stored.
     */
    [[nodiscard]] constexpr bool MetalRenderTargetCubeUploadSupported(int appliedSampleCount) noexcept
    {
        return appliedSampleCount <= 0;
    }

    /**
     * @brief Validates the complete portable contract for a Metal Texture2D ImageData request.
     *
     * @param surfaceFormat Raw SurfaceFormat ordinal.
     * @param width Level-zero width.
     * @param height Level-zero height.
     * @param mipLevels Requested mip count; either one or the complete chain.
     * @param baseByteCount Number of level-zero bytes supplied by ImageData.
     * @return Deterministic allocation/upload policy result.
     */
    [[nodiscard]] inline MetalTexture2DImagePolicy DescribeMetalTexture2DShapePolicy(
        int width,
        int height,
        int mipLevels,
        std::size_t baseByteCount,
        std::size_t bytesPerTexel = 4u) noexcept;

    [[nodiscard]] inline MetalTexture2DImagePolicy DescribeMetalTexture2DImagePolicy(
        int surfaceFormat,
        int width,
        int height,
        int mipLevels,
        std::size_t baseByteCount) noexcept
    {
        if (!MetalSupportsSurfaceFormat(surfaceFormat))
            return MetalTexture2DImagePolicy::UnsupportedFormat;
        return DescribeMetalTexture2DShapePolicy(width, height, mipLevels, baseByteCount);
    }

    /**
     * @brief Validates a Texture2D's dimensions, mip count and level-zero bytes at a texel size.
     *
     * plans/plan_apple_m4.md AM4-142: the shape rules of DescribeMetalTexture2DImagePolicy for any
     * natively stored format.
     *
     * @param width Requested width.
     * @param height Requested height.
     * @param mipLevels Requested mip count; either one or the complete chain.
     * @param baseByteCount Number of level-zero bytes supplied.
     * @param bytesPerTexel Bytes in one texel of the format.
     * @return Supported, or the first rule the request breaks.
     */
    [[nodiscard]] inline MetalTexture2DImagePolicy DescribeMetalTexture2DShapePolicy(
        int width,
        int height,
        int mipLevels,
        std::size_t baseByteCount,
        std::size_t bytesPerTexel) noexcept
    {
        if (width <= 0 || height <= 0 || mipLevels <= 0)
            return MetalTexture2DImagePolicy::InvalidDimensionsOrMipCount;
        int fullMipCount = 1;
        for (int w = width, h = height; w > 1 || h > 1; ++fullMipCount)
        {
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
        }
        if (mipLevels != 1 && mipLevels != fullMipCount)
            return MetalTexture2DImagePolicy::InvalidDimensionsOrMipCount;
        MetalTextureTransferLayout layout{};
        if (!TryBuildMetalTextureTransferLayout(width, height, 1, 1, layout, bytesPerTexel) ||
            baseByteCount != layout.tightTotalBytes)
        {
            return MetalTexture2DImagePolicy::InvalidBaseByteCount;
        }
        return MetalTexture2DImagePolicy::Supported;
    }

    /** @brief Native colour storage of a Metal render target (plans/plan_apple_m4.md AM4-142). */
    enum class MetalColorStorage
    {
        /** @brief BGRA8Unorm -- SurfaceFormat::Color, the backbuffer's format. */
        Bgra8,
        /** @brief RGB10A2Unorm -- Rgba1010102 (red in the low bits, as XNA's). */
        Rgb10A2,
        /** @brief RG16Unorm -- Rg32. */
        Rg16Unorm,
        /** @brief RGBA16Unorm -- Rgba64. */
        Rgba16Unorm,
        /** @brief R32Float -- Single. */
        R32Float,
        /** @brief RG32Float -- Vector2. */
        Rg32Float,
        /** @brief RGBA32Float -- Vector4. */
        Rgba32Float,
        /** @brief R16Float -- HalfSingle. */
        R16Float,
        /** @brief RG16Float -- HalfVector2. */
        Rg16Float,
        /** @brief RGBA16Float -- HalfVector4 and HdrBlendable. */
        Rgba16Float
    };

    /** @brief How one render-target SurfaceFormat is stored and transferred on Metal. */
    struct MetalColorStorageInfo
    {
        /** @brief Native storage. */
        MetalColorStorage storage = MetalColorStorage::Bgra8;
        /** @brief Bytes in one texel (the GetData/SetData element size). */
        int bytesPerTexel = 4;
        /**
         * @brief Channels the format stores: 1, 2 or 4. Sampling fills the rest as Direct3D 9
         *        does -- one-channel formats read (r, 1, 1, 1) and two-channel ones (r, g, 1, 1).
         */
        int channels = 4;
        /** @brief The bytes are blue-green-red-alpha and swap to XNA's red-first order. */
        bool bgraOrder = true;
        /** @brief The SurfaceFormat ordinal asked for (HalfVector4 and HdrBlendable share a storage). */
        int surfaceFormat = 0;
    };

    /**
     * @brief The native storage of a RenderTarget2D/RenderTargetCube surface format, if Metal renders it.
     *
     * plans/plan_apple_m4.md AM4-142: every target used to be BGRA8 whatever was asked, so HDR, bloom
     * and float shadow maps clamped to 8-bit [0, 1]. These are the XNA HiDef render-target formats
     * whose Metal byte layout is XNA's own; the packed 16-bit ones and Alpha8 still fall back to
     * Color, as XNA lets a device do.
     *
     * @param surfaceFormat SurfaceFormat ordinal.
     * @param info Receives the storage when the format is renderable.
     * @return True when Metal renders the format natively.
     */
    [[nodiscard]] constexpr bool MetalRenderTargetStorageFor(int surfaceFormat, MetalColorStorageInfo& info) noexcept
    {
        using SF = Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        switch (static_cast<SF>(surfaceFormat))
        {
            case SF::Color:        info = {MetalColorStorage::Bgra8, 4, 4, true}; break;
            case SF::Rgba1010102:  info = {MetalColorStorage::Rgb10A2, 4, 4, false}; break;
            case SF::Rg32:         info = {MetalColorStorage::Rg16Unorm, 4, 2, false}; break;
            case SF::Rgba64:       info = {MetalColorStorage::Rgba16Unorm, 8, 4, false}; break;
            case SF::Single:       info = {MetalColorStorage::R32Float, 4, 1, false}; break;
            case SF::Vector2:      info = {MetalColorStorage::Rg32Float, 8, 2, false}; break;
            case SF::Vector4:      info = {MetalColorStorage::Rgba32Float, 16, 4, false}; break;
            case SF::HalfSingle:   info = {MetalColorStorage::R16Float, 2, 1, false}; break;
            case SF::HalfVector2:  info = {MetalColorStorage::Rg16Float, 4, 2, false}; break;
            case SF::HalfVector4:
            case SF::HdrBlendable: info = {MetalColorStorage::Rgba16Float, 8, 4, false}; break;
            default: return false;
        }
        info.surfaceFormat = surfaceFormat;
        return true;
    }

    /** @brief Native storage of a Metal Texture2D (plans/plan_apple_m4.md AM4-142). */
    enum class MetalTextureStorage
    {
        /** @brief RGBA8Unorm -- Color. */
        Rgba8,
        /** @brief A8Unorm -- Alpha8; samples (0, 0, 0, a) as Direct3D 9 does. */
        A8,
        /** @brief RG8Snorm -- NormalizedByte2. */
        Rg8Snorm,
        /** @brief RGBA8Snorm -- NormalizedByte4. */
        Rgba8Snorm,
        /** @brief B5G6R5Unorm -- Bgr565 (Apple GPUs). */
        B5G6R5,
        /** @brief BGR5A1Unorm -- Bgra5551 (Apple GPUs). */
        Bgr5A1,
        /** @brief ABGR4Unorm -- Bgra4444 (Apple GPUs); each texel's nibbles are rotated on upload. */
        Abgr4,
        /** @brief RGB10A2Unorm -- Rgba1010102. */
        Rgb10A2,
        /** @brief RG16Unorm -- Rg32. */
        Rg16Unorm,
        /** @brief RGBA16Unorm -- Rgba64. */
        Rgba16Unorm,
        /** @brief R32Float -- Single. */
        R32Float,
        /** @brief RG32Float -- Vector2. */
        Rg32Float,
        /** @brief RGBA32Float -- Vector4. */
        Rgba32Float,
        /** @brief R16Float -- HalfSingle. */
        R16Float,
        /** @brief RG16Float -- HalfVector2. */
        Rg16Float,
        /** @brief RGBA16Float -- HalfVector4 and HdrBlendable. */
        Rgba16Float
    };

    /** @brief How one Texture2D SurfaceFormat is stored and uploaded on Metal. */
    struct MetalTextureStorageInfo
    {
        /** @brief Native storage. */
        MetalTextureStorage storage = MetalTextureStorage::Rgba8;
        /** @brief Bytes in one texel as XNA lays it out. */
        int bytesPerTexel = 4;
        /** @brief Stored channels: 1 or 2 sample the rest as 1, as Direct3D 9 does; 4 as stored. */
        int channels = 4;
        /** @brief XNA's A4R4G4B4 texel becomes Metal's R4G4B4A4 by rotating its nibbles. */
        bool rotate4444 = false;
    };

    /**
     * @brief The native storage of a Texture2D surface format on Metal, if any.
     *
     * plans/plan_apple_m4.md AM4-142: Texture2D was RGBA8-only; every XNA HiDef uncompressed format
     * now has a native Metal format whose byte layout is XNA's (the 4444 one after a nibble
     * rotation). The packed 16-bit formats exist on Apple GPUs only.
     *
     * @param surfaceFormat SurfaceFormat ordinal.
     * @param packed16Available Whether the device has B5G6R5/BGR5A1/ABGR4 (an Apple GPU).
     * @param info Receives the storage when the format is stored natively.
     * @return True when Metal stores the format natively.
     */
    [[nodiscard]] constexpr bool MetalTextureStorageFor(int surfaceFormat, bool packed16Available,
                                                        MetalTextureStorageInfo& info) noexcept
    {
        using SF = Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        using TS = MetalTextureStorage;
        switch (static_cast<SF>(surfaceFormat))
        {
            case SF::Color:           info = {TS::Rgba8, 4, 4, false}; return true;
            case SF::Bgr565:          info = {TS::B5G6R5, 2, 4, false}; return packed16Available;
            case SF::Bgra5551:        info = {TS::Bgr5A1, 2, 4, false}; return packed16Available;
            case SF::Bgra4444:        info = {TS::Abgr4, 2, 4, true}; return packed16Available;
            case SF::NormalizedByte2: info = {TS::Rg8Snorm, 2, 2, false}; return true;
            case SF::NormalizedByte4: info = {TS::Rgba8Snorm, 4, 4, false}; return true;
            case SF::Rgba1010102:     info = {TS::Rgb10A2, 4, 4, false}; return true;
            case SF::Rg32:            info = {TS::Rg16Unorm, 4, 2, false}; return true;
            case SF::Rgba64:          info = {TS::Rgba16Unorm, 8, 4, false}; return true;
            case SF::Alpha8:          info = {TS::A8, 1, 4, false}; return true;
            case SF::Single:          info = {TS::R32Float, 4, 1, false}; return true;
            case SF::Vector2:         info = {TS::Rg32Float, 8, 2, false}; return true;
            case SF::Vector4:         info = {TS::Rgba32Float, 16, 4, false}; return true;
            case SF::HalfSingle:      info = {TS::R16Float, 2, 1, false}; return true;
            case SF::HalfVector2:     info = {TS::Rg16Float, 4, 2, false}; return true;
            case SF::HalfVector4:
            case SF::HdrBlendable:    info = {TS::Rgba16Float, 8, 4, false}; return true;
            default: return false;
        }
    }

    /**
     * @brief Converts one XNA Bgra4444 texel (A in the high nibble) to Metal's ABGR4Unorm layout
     *        (R in the high nibble, A in the low one).
     *
     * @param xna The XNA A4R4G4B4 texel.
     * @return The Metal R4G4B4A4 texel.
     */
    [[nodiscard]] constexpr uint16_t MetalBgra4444ToAbgr4(uint16_t xna) noexcept
    {
        return static_cast<uint16_t>(((xna << 4) | (xna >> 12)) & 0xFFFFu);
    }

    /**
     * @brief Converts one Metal ABGR4Unorm texel back to XNA's Bgra4444 layout (A in the high
     *        nibble) -- the inverse of MetalBgra4444ToAbgr4, for readback.
     *
     * @param metal The Metal R4G4B4A4 texel.
     * @return The XNA A4R4G4B4 texel.
     */
    [[nodiscard]] constexpr uint16_t MetalAbgr4ToBgra4444(uint16_t metal) noexcept
    {
        return static_cast<uint16_t>(((metal >> 4) | (metal << 12)) & 0xFFFFu);
    }

    /**
     * @brief Whether XNA keeps a format out of a TextureCube or a Texture3D although a Texture2D
     *        stores it.
     *
     * plans/plan_apple_m4.md AM4-147: NormalizedByte2 and NormalizedByte4 are Texture2D-only for
     * both kinds (Texture::IsCubeFormatAllowedByProfileEXT), and a volume carries no DXT format
     * (Texture::IsVolumeFormatAllowedByProfileEXT).
     *
     * @param surfaceFormat SurfaceFormat ordinal.
     * @param volume True for a Texture3D, false for a TextureCube.
     * @return True when the format is refused for that kind.
     */
    [[nodiscard]] constexpr bool MetalIsTexture2DOnlyFormat(int surfaceFormat, bool volume) noexcept
    {
        using SF = Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        switch (static_cast<SF>(surfaceFormat))
        {
            case SF::NormalizedByte2:
            case SF::NormalizedByte4:
                return true;
            case SF::Dxt1:
            case SF::Dxt3:
            case SF::Dxt5:
                return volume;
            default:
                return false;
        }
    }

    /**
     * @brief Whether Metal can issue a buffered draw's vertex or index window as XNA forwards it.
     *
     * plans/plan_apple_m4.md AM4-150: XNA validates only the counts and hands offsets to Direct3D 9
     * unchecked. Metal's draw takes unsigned offsets and its buffers are not bounds-checked, so a
     * window that starts below zero or runs past the buffer is not issued at all -- the draw
     * completes and changes nothing, one of the outcomes of the undefined native draw.
     *
     * @param first First vertex (non-indexed) or index (indexed) the draw reads.
     * @param count Vertices or indices the draw reads.
     * @param available Vertices in the vertex buffer, or indices in the index buffer.
     * @return True when [first, first + count) lies inside [0, available).
     */
    [[nodiscard]] constexpr bool MetalBufferedDrawRangeIsServable(long long first, long long count,
                                                                  long long available) noexcept
    {
        return first >= 0 && count >= 0 && available >= 0 && first <= available &&
               count <= available - first;
    }

    /** @brief Bit for a sample count in a supported-count mask (2, 4 and 8 are the counts asked about). */
    [[nodiscard]] constexpr unsigned MetalSampleCountBit(int samples) noexcept
    {
        return (samples == 2 || samples == 4 || samples == 8) ? (1u << samples) : 0u;
    }

    /**
     * @brief The multisample count a request gets on a device.
     *
     * plans/plan_apple_m4.md AM4-141: a request is rounded down to the nearest count the device
     * supports (`supportsTextureSampleCount:`), as XNA's own validation lowers an unsupported
     * PresentationParameters.MultiSampleCount; 0 and 1 mean single-sampled, reported as 0.
     *
     * @param requestedMultiSampleCount Requested public sample count.
     * @param supportedMask MetalSampleCountBit of every count the device supports.
     * @return The applied public count: 0, 2, 4 or 8.
     */
    [[nodiscard]] constexpr int MetalAppliedMultiSampleCount(int requestedMultiSampleCount,
                                                             unsigned supportedMask) noexcept
    {
        for (int samples = 8; samples >= 2; samples /= 2)
            if (requestedMultiSampleCount >= samples && (supportedMask & MetalSampleCountBit(samples)) != 0)
                return samples;
        return 0;
    }

    /** @brief A viewport's depth range. */
    struct MetalDepthRange
    {
        /** @brief Depth the near clip plane maps to. */
        double minDepth = 0.0;
        /** @brief Depth the far clip plane maps to. */
        double maxDepth = 1.0;
    };

    /**
     * @brief The viewport depth range that carries XNA's constant RasterizerState.DepthBias.
     *
     * plans/plan_apple_m4.md AM4-151 (refining AM4-134): Direct3D 9 adds DepthBias to a fragment's
     * depth after the viewport transform -- a plain offset in normalized depth, whatever the depth
     * format. setDepthBias: cannot say that for a float attachment: it counts the minimum
     * resolvable difference at the primitive's own exponent, so AM4-134's 2^-24 scale was exact
     * only for depth in [0.5, 1) and half as large below 0.5. Moving the viewport's depth range by
     * the bias adds exactly that offset in every format; the slope-scaled term stays native.
     *
     * @param minDepth Viewport.MinDepth.
     * @param maxDepth Viewport.MaxDepth.
     * @param xnaDepthBias RasterizerState.DepthBias.
     * @return The range to hand to setViewport:.
     */
    [[nodiscard]] constexpr MetalDepthRange MetalBiasedDepthRange(double minDepth, double maxDepth,
                                                                  float xnaDepthBias) noexcept
    {
        return MetalDepthRange{minDepth + static_cast<double>(xnaDepthBias),
                               maxDepth + static_cast<double>(xnaDepthBias)};
    }

    /** @brief What a draw does under BlendState.MultiSampleMask on its target. */
    enum class MetalSampleMaskAdmission
    {
        /** @brief Draw normally: a single-sample target, or every sample kept. */
        Draw,
        /** @brief Skip the draw: no sample of the target is kept, so nothing would be written. */
        Skip,
        /** @brief Draw writing [[sample_mask]]: some samples kept and some dropped. */
        Masked
    };

    /**
     * @brief Judges BlendState.MultiSampleMask against the sample count of the target drawn to.
     *
     * plans/plan_apple_m4.md AM4-141: XNA's mask "has no effect when rendering to a single sample
     * buffer" (D3D9). On a multisampled target, a mask that keeps no sample writes nothing -- exact
     * by not drawing -- and a partial one is applied by the fragment's [[sample_mask]] output,
     * since Metal has no pipeline sample mask.
     *
     * @param mask BlendState.MultiSampleMask.
     * @param sampleCount Native sample count of the bound target (1 when single-sampled).
     * @return Draw, Skip or Masked.
     */
    [[nodiscard]] constexpr MetalSampleMaskAdmission DescribeMetalSampleMaskAdmission(
        unsigned mask, int sampleCount) noexcept
    {
        if (sampleCount <= 1) return MetalSampleMaskAdmission::Draw;
        const unsigned every = sampleCount >= 32 ? 0xFFFFFFFFu : ((1u << sampleCount) - 1u);
        const unsigned kept = mask & every;
        if (kept == every) return MetalSampleMaskAdmission::Draw;
        if (kept == 0) return MetalSampleMaskAdmission::Skip;
        return MetalSampleMaskAdmission::Masked;
    }

    /**
     * @brief Reports whether output-write state can be applied without silent degradation.
     *
     * plans/plan_apple_m4.md AM4-079: render target 0's ColorWriteChannels is baked into the
     * pipeline (`MetalBlendKey::writeMask`); AM4-146: ColorWriteChannels1..3 likewise
     * (`writeMask1..3`), for the attachments a compiled pixel shader writes -- the built-in
     * functions write COLOR0 only and mask the other attachments out.
     *
     * The coverage mask is not judged here (AM4-135): XNA's MultiSampleMask has no effect on a
     * single-sample target (D3D9: "no effect when rendering to a single sample buffer"), so it can
     * only be accepted or refused against the target a draw actually goes to.
     *
     * @param state Renderer-neutral color-write masks and multisample coverage mask.
     * @return True when all four write masks are valid ColorWriteChannels values.
     */
    [[nodiscard]] constexpr bool MetalSupportsBlendWriteState(const BlendWriteState& state)
    {
        for (const int channels : state.colorWriteChannels)
            if (channels<0 || channels>15) return false;
        return true;
    }
}

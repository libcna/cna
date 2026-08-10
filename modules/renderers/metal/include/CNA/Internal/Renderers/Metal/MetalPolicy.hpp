// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Metal/MetalTextureTransfer.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <cstddef>

namespace CNA::Internal::Renderers::Metal
{
    /** @brief Result of validating the stream shape accepted by Metal's ordinary draw path. */
    enum class MetalDrawStreamPolicy
    {
        /** @brief Zero streams (legacy/internal) or one valid per-vertex stream. */
        Supported,
        /** @brief The binding count or one binding's metadata is internally inconsistent. */
        InvalidBinding,
        /** @brief More than one per-vertex stream was supplied. */
        MultiStreamUnsupported,
        /** @brief An instance stream or instance count above one was supplied. */
        InstancingUnsupported,
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
        if (params.instanceCount != 1)
            return MetalDrawStreamPolicy::InstancingUnsupported;

        int perVertexCount=0;
        for (int i=0; i<params.vertexStreamCount; ++i)
        {
            const auto& stream=params.vertexStreams[i];
            if (!stream.buffer || stream.slot!=i || stream.strideInBytes<=0 ||
                stream.vertexOffset<0 || stream.vertexCount<0)
                return MetalDrawStreamPolicy::InvalidBinding;
            if (stream.instanceFrequency>0)
                return MetalDrawStreamPolicy::InstancingUnsupported;
            if (stream.instanceFrequency<0)
                return MetalDrawStreamPolicy::InvalidBinding;
            ++perVertexCount;
        }

        if (perVertexCount>1)
            return MetalDrawStreamPolicy::MultiStreamUnsupported;
        if (perVertexCount==1)
        {
            const auto& stream=params.vertexStreams[0];
            if (stream.combinedByteBase!=0 || params.combinedVertexStride!=stream.strideInBytes)
                return MetalDrawStreamPolicy::InvalidBinding;
        }
        else if (params.combinedVertexStride!=0)
        {
            return MetalDrawStreamPolicy::InvalidBinding;
        }
        return MetalDrawStreamPolicy::Supported;
    }

    /**
     * @brief Checks that a captured single stream names and advances the uploaded Metal buffer.
     *
     * @param params Renderer draw parameters whose stream metadata was captured for this draw.
     * @param uploadedBuffer Vertex buffer passed to the draw route.
     * @param uploadedStride Byte stride used when that buffer's native storage was uploaded.
     * @return True for the legacy zero-stream shape, or when the one stream names the same buffer
     *         and uses exactly its uploaded stride.
     */
    [[nodiscard]] inline bool MetalSingleStreamMatchesUploadedBuffer(
        const GpuDrawParams& params,
        const IVertexBufferRenderer& uploadedBuffer,
        std::size_t uploadedStride) noexcept
    {
        if (params.vertexStreamCount == 0)
            return true;
        if (params.vertexStreamCount != 1)
            return false;
        const auto& stream = params.vertexStreams[0];
        return stream.buffer == &uploadedBuffer && stream.strideInBytes > 0 &&
               static_cast<std::size_t>(stream.strideInBytes) == uploadedStride;
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
            case CNA::GraphicsCapability::MultiSampleAntiAliasing:  return false;
            case CNA::GraphicsCapability::MultipleRenderTargets:    return false;
            case CNA::GraphicsCapability::AnisotropicFiltering:     return true;
            case CNA::GraphicsCapability::WireFrame:                return true;
            case CNA::GraphicsCapability::OcclusionQuery:           return false;
            case CNA::GraphicsCapability::CustomEffects:            return false;
            case CNA::GraphicsCapability::Texture3D:                return true;
            case CNA::GraphicsCapability::MultiStreamVertexInput:   return false;
            case CNA::GraphicsCapability::Instancing:               return false;
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
     * @brief Reports whether RenderTargetCube accepts CPU uploads in the current contract.
     * @return False; uploads are rejected instead of silently discarded.
     */
    [[nodiscard]] constexpr bool MetalRenderTargetCubeUploadSupported() noexcept
    {
        return false;
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
    [[nodiscard]] inline MetalTexture2DImagePolicy DescribeMetalTexture2DImagePolicy(
        int surfaceFormat,
        int width,
        int height,
        int mipLevels,
        std::size_t baseByteCount) noexcept
    {
        if (!MetalSupportsSurfaceFormat(surfaceFormat))
            return MetalTexture2DImagePolicy::UnsupportedFormat;
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
        if (!TryBuildMetalTextureTransferLayout(width, height, 1, 1, layout) ||
            baseByteCount != layout.tightTotalBytes)
        {
            return MetalTexture2DImagePolicy::InvalidBaseByteCount;
        }
        return MetalTexture2DImagePolicy::Supported;
    }

    /**
     * @brief Clamps every requested sample count to the currently supported value zero.
     * @param requestedMultiSampleCount Requested public sample count.
     * @return Zero; Metal MSAA is deliberately outside the supported contract.
     */
    [[nodiscard]] constexpr int MetalAppliedMultiSampleCount(int requestedMultiSampleCount)
    {
        (void)requestedMultiSampleCount;
        return 0;
    }

    /**
     * @brief Reports whether output-write state can be applied without silent degradation.
     * @param state Renderer-neutral color-write masks and multisample coverage mask.
     * @return True only for all-channel writes on every slot and the default coverage mask.
     */
    [[nodiscard]] constexpr bool MetalSupportsBlendWriteState(const BlendWriteState& state)
    {
        return state.colorWriteChannels[0]==15 && state.colorWriteChannels[1]==15 &&
               state.colorWriteChannels[2]==15 && state.colorWriteChannels[3]==15 &&
               state.multiSampleMask==0xFFFFFFFFu;
    }
}

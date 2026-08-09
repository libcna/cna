// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

namespace CNA::Internal::Backends::Metal
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

    /**
     * @brief Reports whether the current Metal contract accepts a draw's stream shape.
     *
     * @param params Backend draw parameters to inspect without native submission.
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
            case CNA::GraphicsCapability::OcclusionQuery:           return true;
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
     * @param state Backend-neutral color-write masks and multisample coverage mask.
     * @return True only for all-channel writes on every slot and the default coverage mask.
     */
    [[nodiscard]] constexpr bool MetalSupportsBlendWriteState(const BlendWriteState& state)
    {
        return state.colorWriteChannels[0]==15 && state.colorWriteChannels[1]==15 &&
               state.colorWriteChannels[2]==15 && state.colorWriteChannels[3]==15 &&
               state.multiSampleMask==0xFFFFFFFFu;
    }
}

#pragma once

#include <cmath>
#include <stdexcept>

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

namespace CNA::Internal::Backends::Glide
{
    /** Validates the current stream-array contract for Glide's single ordinary input stream. */
    inline void ValidateGlideVertexStreams(const GpuDrawParams& params,
                                           const IVertexBufferBackend* primaryVertexBuffer,
                                           int primaryStrideInBytes,
                                           int primaryVertexCount)
    {
        // A zero-count array is the internal DrawUser* shape: the primary buffer argument still
        // carries the transient packed vertices. Every public buffer-bound route supplies one.
        if (params.vertexStreamCount < 0 || params.vertexStreamCount > 1)
        {
            throw std::runtime_error(
                "GLIDE 3D supports exactly one per-vertex stream; multi-stream vertex input "
                "and per-instance streams are unavailable");
        }
        if (params.vertexStreamCount == 0)
        {
            return;
        }

        const GpuVertexStreamBinding& stream = params.vertexStreams[0];
        if (stream.slot != 0 || stream.buffer != primaryVertexBuffer || stream.instanceFrequency != 0 ||
            stream.vertexOffset != 0 || stream.combinedByteBase != 0)
        {
            throw std::runtime_error(
                "GLIDE 3D requires one ordinary stream matching the draw's primary vertex "
                "buffer; VertexOffset is carried only by vertexStart/baseVertex and instance "
                "streams are unavailable");
        }
        if (stream.strideInBytes != primaryStrideInBytes ||
            params.combinedVertexStride != stream.strideInBytes ||
            stream.vertexCount != primaryVertexCount)
        {
            throw std::runtime_error(
                "GLIDE 3D received vertex-stream metadata that does not match its primary "
                "vertex buffer");
        }
    }

    /**
     * Glide's two TMUs can filter independently, but this implementation deliberately shares one
     * native s/t vertex channel. Address modes therefore must agree: CPU wrap/mirror segmentation
     * rewrites that shared coordinate before either TMU sees it.
     */
    inline void ValidateGlideDualSamplerAddressModes(int addressU0, int addressV0,
                                                     int addressU1, int addressV1)
    {
        if (addressU0 != addressU1 || addressV0 != addressV1)
        {
            throw std::runtime_error(
                "GLIDE DualTextureEffect requires SamplerStates[0] and SamplerStates[1] to use "
                "the same address modes because both TMUs share one texture-coordinate channel");
        }
    }

    /** Validates the sampler mip controls the Glide 3.x API can represent exactly. */
    inline void ValidateGlideSamplerMipState(int maxMipLevel, float lodBias)
    {
        if (maxMipLevel != 0)
        {
            throw std::runtime_error("GLIDE backend cannot represent SamplerState::MaxMipLevel");
        }
        if (!std::isfinite(lodBias) || lodBias < -8.0f || lodBias > 7.75f)
        {
            throw std::runtime_error(
                "GLIDE SamplerState LOD bias must be finite and within [-8, 7.75]");
        }
    }

    /**
     * Indexed Glide draws expand validated indices into one transient ordinary stream. Preserve
     * every effect/state field while making that internal stream shape explicit to the common
     * non-indexed submitter.
     */
    [[nodiscard]] inline GpuDrawParams MakeGlideExpandedIndexedDrawParams(
        const GpuDrawParams& source)
    {
        GpuDrawParams expanded = source;
        expanded.vertexStreams = {};
        expanded.vertexStreamCount = 0;
        expanded.combinedVertexStride = 0;
        expanded.vertexStart = 0;
        expanded.startIndex = 0;
        expanded.baseVertex = 0;
        return expanded;
    }
}

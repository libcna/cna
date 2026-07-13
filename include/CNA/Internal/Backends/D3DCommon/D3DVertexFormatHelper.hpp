#pragma once

// plan_dx.md Phase DX3 (DX-16-vtx): stride-keyed vertex layout inference, mirroring the exact
// convention this project already established for WebGPU/Software (16/20/24/32/52-byte strides
// map to VertexPositionColor/VertexPositionTexture/VertexPositionColorTexture/
// VertexPositionNormalTexture/VertexPositionNormalTextureSkinned) -- not a new convention.
// HLSL semantic names (POSITION/COLOR/TEXCOORD/NORMAL/BLENDWEIGHT/BLENDINDICES) match this
// project's own stock-effect shader semantics (Phase DX8).

#include <d3d11.h>
#include <cstddef>

namespace CNA::Internal::Backends::D3DCommon
{
    /// Returns the D3D11_INPUT_ELEMENT_DESC array (and its element count via @p count) for the
    /// given vertex stride in bytes, or nullptr / count=0 if the stride is not one of this
    /// project's 5 established stride-keyed layouts (16/20/24/32/52).
    ///
    /// Layouts (byte offsets match VertexPositionColor/VertexPositionTexture/
    /// VertexPositionColorTexture/VertexPositionNormalTexture/VertexPositionNormalTextureSkinned's
    /// own getVertexDeclarationStatic() layouts exactly):
    ///   16: POSITION0 (R32G32B32_FLOAT, 0), COLOR0 (R8G8B8A8_UNORM, 12)
    ///   20: POSITION0 (R32G32B32_FLOAT, 0), TEXCOORD0 (R32G32_FLOAT, 12)
    ///   24: POSITION0 (R32G32B32_FLOAT, 0), COLOR0 (R8G8B8A8_UNORM, 12), TEXCOORD0 (R32G32_FLOAT, 16)
    ///   32: POSITION0 (R32G32B32_FLOAT, 0), NORMAL0 (R32G32B32_FLOAT, 12), TEXCOORD0 (R32G32_FLOAT, 24)
    ///   52: POSITION0 (0), NORMAL0 (12), TEXCOORD0 (24), BLENDWEIGHT0 (R32G32B32A32_FLOAT, 32),
    ///       BLENDINDICES0 (R8G8B8A8_UINT, 48)
    const D3D11_INPUT_ELEMENT_DESC* InputElementsForStride(std::size_t strideInBytes, UINT& count);
}

#pragma once

// plans/plan_dx.md Phase DIRECTX3 (DX-16-vtx): stride-keyed vertex layout inference, mirroring the exact
// convention this project already established for WebGPU/Software (16/20/24/32/52-byte strides
// map to VertexPositionColor/VertexPositionTexture/VertexPositionColorTexture/
// VertexPositionNormalTexture/VertexPositionNormalTextureSkinned) -- not a new convention.
// HLSL semantic names (POSITION/COLOR/TEXCOORD/NORMAL/BLENDWEIGHT/BLENDINDICES) match this
// project's own stock-effect shader semantics (Phase DIRECTX8).

#include <d3d11.h>
#include <d3d12.h>
#include <cstddef>

namespace CNA::Internal::Renderers::D3DCommon
{
    /// Returns the D3D11_INPUT_ELEMENT_DESC array (and its element count via @p count) for the
    /// given vertex stride in bytes, or nullptr / count=0 if the stride is not one of this
    /// project's established stride-keyed layouts (16/20/24/32/48/52/56/68).
    ///
    /// Layouts (byte offsets match VertexPositionColor/VertexPositionTexture/
    /// VertexPositionColorTexture/VertexPositionNormalTexture/VertexPositionNormalTextureSkinned/
    /// VertexPositionNormalTangentTexture/VertexPositionNormalTangentTextureSkinned's own
    /// getVertexDeclarationStatic() layouts exactly, plus the stride-56 skinned+Color layout that
    /// -- like the stride-24 AlphaTestColored3d precedent -- has no dedicated named XNA-layer
    /// struct, only a raw VertexDeclaration; see EasyGLRenderer::ApplyLayout's own
    /// case 56 comment for the byte-for-byte convention this mirrors):
    ///   16: POSITION0 (R32G32B32_FLOAT, 0), COLOR0 (R8G8B8A8_UNORM, 12)
    ///   20: POSITION0 (R32G32B32_FLOAT, 0), TEXCOORD0 (R32G32_FLOAT, 12)
    ///   24: POSITION0 (R32G32B32_FLOAT, 0), COLOR0 (R8G8B8A8_UNORM, 12), TEXCOORD0 (R32G32_FLOAT, 16)
    ///   32: POSITION0 (R32G32B32_FLOAT, 0), NORMAL0 (R32G32B32_FLOAT, 12), TEXCOORD0 (R32G32_FLOAT, 24)
    ///   48: POSITION0 (0), NORMAL0 (12), TANGENT0 (R32G32B32A32_FLOAT, 24), TEXCOORD0 (R32G32_FLOAT, 40)
    ///   52: POSITION0 (0), NORMAL0 (12), TEXCOORD0 (24), BLENDWEIGHT0 (R32G32B32A32_FLOAT, 32),
    ///       BLENDINDICES0 (R8G8B8A8_UINT, 48)
    ///   56: stride-52 layout above + COLOR0 (R8G8B8A8_UNORM, 52)
    ///   68: stride-48 layout above + BLENDWEIGHT0 (R32G32B32A32_FLOAT, 48), BLENDINDICES0
    ///       (R8G8B8A8_UINT, 64)
    const D3D11_INPUT_ELEMENT_DESC* InputElementsForStride(std::size_t strideInBytes, UINT& count);

    /// plans/plan_dx.md Phase DX12 (DX-107): D3D12 counterpart of InputElementsForStride() above, same
    /// stride-keyed layouts (16/20/24/32/48/52/56/68 -- the last three added by the D3D12 PBR/
    /// skinned-vertex-color reconciliation follow-up, additive-only: D3D11's own InputElementsForStride()
    /// already covered them), same byte offsets/semantic names -- D3D11_INPUT_ELEMENT_DESC and
    /// D3D12_INPUT_ELEMENT_DESC are identical in field shape (verified: same field order/types,
    /// only the struct/enum names carry D3D11_/D3D12_ prefixes -- D3D11_INPUT_PER_VERTEX_DATA == 0
    /// == D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, checked against both SDK headers on this
    /// machine, same "verify, don't assume" discipline design decision 4/DX-12-state already used).
    /// Unlike D3D11 (DX-32's own ID3D11InputLayout cache), D3D12 has no separate input-layout COM
    /// object at all -- this array is baked directly into a D3D12_GRAPHICS_PIPELINE_STATE_DESC's
    /// InputLayout field at PSO-creation time (DX-107), so this function is a pure data lookup, not
    /// a cache.
    const D3D12_INPUT_ELEMENT_DESC* InputElementsForStrideD3D12(std::size_t strideInBytes, UINT& count);
}

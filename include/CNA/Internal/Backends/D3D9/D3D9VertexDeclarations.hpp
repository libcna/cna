#pragma once

// plan_dx9.md Phase D9-2 (D9-22): stride-keyed D3DVERTEXELEMENT9 arrays, mirroring the same 5
// stride-keyed layouts (16/20/24/32/52 bytes) D3DCommon::InputElementsForStride() already
// establishes for D3D11/D3D12 -- same byte offsets, same semantic meaning, D3D9's own element/type
// enums (design decision 12: this is D3D9's own table, not a D3DCommon consumer).
//
// XNA's own VertexDeclaration maps 1:1 onto D3DVERTEXELEMENT9 -- this project's D3D9 backend is
// not inventing a layout, just transcribing the one every other backend already uses.

#include <d3d9.h>
#include <cstddef>

namespace CNA::Internal::Backends::D3D9
{
    /// Returns the D3DVERTEXELEMENT9 array (already terminated with the D3DDECL_END() sentinel --
    /// safe to pass directly to IDirect3DDevice9::CreateVertexDeclaration()) for the given vertex
    /// stride in bytes, or nullptr / count=0 if the stride is not one of this project's 5
    /// established stride-keyed layouts. @p count receives the number of REAL (non-sentinel)
    /// elements -- for iterating the layout's own fields; it does not need to be passed to
    /// CreateVertexDeclaration() separately, since the sentinel already marks the array's end.
    ///
    /// Layouts (byte offsets/semantics match D3DCommon::InputElementsForStride()'s own doc
    /// exactly, just in D3D9's Usage/UsageIndex/Type vocabulary instead of DXGI_FORMAT + semantic
    /// name strings):
    ///   16: POSITION0 (FLOAT3, 0), COLOR0 (D3DCOLOR, 12)
    ///   20: POSITION0 (FLOAT3, 0), TEXCOORD0 (FLOAT2, 12)
    ///   24: POSITION0 (FLOAT3, 0), COLOR0 (D3DCOLOR, 12), TEXCOORD0 (FLOAT2, 16)
    ///   32: POSITION0 (FLOAT3, 0), NORMAL0 (FLOAT3, 12), TEXCOORD0 (FLOAT2, 24)
    ///   52: POSITION0 (0), NORMAL0 (12), TEXCOORD0 (24), BLENDWEIGHT0 (FLOAT4, 32),
    ///       BLENDINDICES0 (UBYTE4, 48)
    ///
    /// Note (D9-90's own future concern, not this function's): stride 32 is ambiguous between
    /// VertexPositionNormalTexture and a hypothetical differently-shaped 32-byte SpriteBatch
    /// vertex -- D3D11's own DX-70 already found and solved this exact collision; reuse that
    /// resolution when D3D9's SpriteBatch backend lands rather than rediscovering it here.
    const D3DVERTEXELEMENT9* VertexElementsForStrideD3D9(std::size_t strideInBytes, UINT& count);
}

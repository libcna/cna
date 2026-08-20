#pragma once

// plans/plan_dx9.md Phase D9-2 (D9-22): stride-keyed D3DVERTEXELEMENT9 arrays, mirroring the same 5
// stride-keyed layouts (16/20/24/32/52 bytes) D3DCommon::InputElementsForStride() already
// establishes for D3D11/D3D12 -- same byte offsets, same semantic meaning, D3D9's own element/type
// enums (design decision 12: this is D3D9's own table, not a D3DCommon consumer).
//
// XNA's own VertexDeclaration maps 1:1 onto D3DVERTEXELEMENT9 -- this project's D3D9 renderer is
// not inventing a layout, just transcribing the one every other renderer already uses.

#include <d3d9.h>
#include <cstddef>

namespace CNA::Internal::Renderers::DirectX9
{
    /// Returns the D3DVERTEXELEMENT9 array (already terminated with the D3DDECL_END() sentinel --
    /// safe to pass directly to IDirect3DDevice9::CreateVertexDeclaration()) for the given vertex
    /// stride in bytes, or nullptr / count=0 if the stride is not one of this project's established
    /// stride-keyed layouts. @p count receives the number of REAL (non-sentinel) elements -- for
    /// iterating the layout's own fields; it does not need to be passed to
    /// CreateVertexDeclaration() separately, since the sentinel already marks the array's end.
    ///
    /// Layouts (byte offsets/semantics match D3DCommon::InputElementsForStride()'s own doc
    /// exactly for the 5 layouts D3D11/D3D12 also share, just in D3D9's Usage/UsageIndex/Type
    /// vocabulary instead of DXGI_FORMAT + semantic name strings; stride 28 is D3D9-only, see its
    /// own note below):
    ///   16: POSITION0 (FLOAT3, 0), COLOR0 (UBYTE4N, 12)
    ///   20: POSITION0 (FLOAT3, 0), TEXCOORD0 (FLOAT2, 12)
    ///   24: POSITION0 (FLOAT3, 0), COLOR0 (UBYTE4N, 12), TEXCOORD0 (FLOAT2, 16)
    ///   28: POSITION0 (FLOAT3, 0), TEXCOORD0 (FLOAT2, 12), TEXCOORD1 (FLOAT2, 20)
    ///   32: POSITION0 (FLOAT3, 0), NORMAL0 (FLOAT3, 12), TEXCOORD0 (FLOAT2, 24)
    ///   48: POSITION0 (FLOAT3, 0), NORMAL0 (FLOAT3, 12), TANGENT0 (FLOAT4, 24), TEXCOORD0
    ///       (FLOAT2, 40) -- VertexPositionNormalTangentTexture, CNA's own CNAEXT "Pbr3D" shader
    ///       (PbrEffect, unskinned)
    ///   52: POSITION0 (0), NORMAL0 (12), TEXCOORD0 (24), BLENDWEIGHT0 (FLOAT4, 32),
    ///       BLENDINDICES0 (UBYTE4, 48)
    ///   56: the stride-52 layout with COLOR0 (UBYTE4N, 52) appended -- CNA's own CNAEXT
    ///       "SkinnedVertexColor3D" shader (real XNA SkinnedEffect has no vertex-color input)
    ///   68: POSITION0 (FLOAT3, 0), NORMAL0 (FLOAT3, 12), TANGENT0 (FLOAT4, 24), TEXCOORD0
    ///       (FLOAT2, 40), BLENDWEIGHT0 (FLOAT4, 48), BLENDINDICES0 (UBYTE4, 64) --
    ///       VertexPositionNormalTangentTextureSkinned, CNA's own CNAEXT "PbrSkinned3D" shader
    ///       (SkinnedPbrEffect)
    ///
    /// D9-82d found live that stride 28 (Position+TexCoord0+TexCoord1) has no equivalent in the 5
    /// layouts D3D11/D3D12 share: `DualTextureEffect.fx`'s real, byte-identical `VSInputTx2`
    /// genuinely needs TWO distinct texture-coordinate sets (unlike D3D11's own
    /// `dual_texture3d.vert.hlsl`, which simplifies to a single shared UV set -- not an option here
    /// since this renderer must draw Microsoft's unmodified compiled shader, not a CNA
    /// reimplementation). Added as a new, D3D9-local stride (design decision 12: this table is not
    /// a `D3DCommon` consumer, so a D3D9-only addition here does not touch any other renderer).
    /// `DualTextureEffect`'s vertex-color variants (`VSInputTx2Vc`, 32 bytes) still collide with the
    /// existing Position+Normal+TexCoord 32-byte layout and remain undrawable, same category as
    /// `BasicEffect`'s own `D9-82b` gaps.
    ///
    /// COLOR0 is UBYTE4N (four bytes normalized in their EXISTING memory order), not
    /// D3DDECLTYPE_D3DCOLOR -- D9-82 found live that D3DDECLTYPE_D3DCOLOR's real contract expects
    /// ARGB-packed (B,G,R,A ascending) memory bytes and byte-swizzles them into RGBA, which
    /// silently swaps R and B against XNA's own native R,G,B,A ascending Color.PackedValue layout
    /// (confirmed empirically: opaque red read back as opaque blue before this fix).
    ///
    /// Note (D9-90's own future concern, not this function's): stride 32 is ambiguous between
    /// VertexPositionNormalTexture and a hypothetical differently-shaped 32-byte SpriteBatch
    /// vertex -- D3D11's own DX-70 already found and solved this exact collision; reuse that
    /// resolution when D3D9's SpriteBatch renderer lands rather than rediscovering it here.
    const D3DVERTEXELEMENT9* VertexElementsForStrideD3D9(std::size_t strideInBytes, UINT& count);
}

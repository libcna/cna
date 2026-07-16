// plan_dx9.md Phase D9-2 (D9-22). COLOR0 element type corrected in D9-82 (see below).
#include "CNA/Internal/Backends/D3D9/D3D9VertexDeclarations.hpp"

namespace CNA::Internal::Backends::D3D9
{
    namespace
    {
        // D9-82 real finding (plan_dx9.md's D3D9GraphicsBackend's own first real draw, empirically
        // verified with cna_test_d3d9_draw before AND after this fix -- not assumed): COLOR0 was
        // originally declared D3DDECLTYPE_D3DCOLOR (D9-22, "same semantic meaning" as D3D11's
        // DXGI_FORMAT_R8G8B8A8_UNORM). That is wrong -- MSDN's own D3DDECLTYPE reference says
        // D3DDECLTYPE_D3DCOLOR's "Input is a D3DCOLOR and is expanded to RGBA order", i.e. it
        // expects ARGB-packed memory bytes (B,G,R,A ascending) and byte-swizzles them into the
        // shader's RGBA register order. XNA's own Color.PackedValue is R,G,B,A ascending (matches
        // D3D9FormatMapping.cpp's identical finding for render-target formats) -- feeding that
        // native layout through D3DDECLTYPE_D3DCOLOR silently swaps R and B. Confirmed live: a
        // vertex fed XNA-native opaque red (0xFF0000FF) read back as opaque BLUE (R=0,G=0,B=255)
        // with the old declaration. D3DDECLTYPE_UBYTE4N (four bytes normalized in their EXISTING
        // order, no ARGB reorder) is the correct type for CNA's own native byte order -- switching
        // to it made the same test read back exact, correct red.
        //
        // VertexPositionColor (stride 16): POSITION0 (FLOAT3, 0), COLOR0 (UBYTE4N, 12).
        constexpr D3DVERTEXELEMENT9 kStride16[] = {
            {0, 0,  D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
            {0, 12, D3DDECLTYPE_UBYTE4N,  D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,    0},
            D3DDECL_END()
        };

        // VertexPositionTexture (stride 20): POSITION0 (FLOAT3, 0), TEXCOORD0 (FLOAT2, 12).
        constexpr D3DVERTEXELEMENT9 kStride20[] = {
            {0, 0,  D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
            {0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
            D3DDECL_END()
        };

        // VertexPositionColorTexture (stride 24): POSITION0 (FLOAT3, 0), COLOR0 (UBYTE4N, 12),
        // TEXCOORD0 (FLOAT2, 16).
        constexpr D3DVERTEXELEMENT9 kStride24[] = {
            {0, 0,  D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
            {0, 12, D3DDECLTYPE_UBYTE4N,  D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,    0},
            {0, 16, D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
            D3DDECL_END()
        };

        // D9-82d: D3D9-only, not shared with D3D11/D3D12 -- see this file's own header comment for
        // why (DualTextureEffect.fx's real VSInputTx2 genuinely needs two distinct UV sets).
        // Position+TexCoord0+TexCoord1 (stride 28): POSITION0 (FLOAT3, 0), TEXCOORD0 (FLOAT2, 12),
        // TEXCOORD1 (FLOAT2, 20).
        constexpr D3DVERTEXELEMENT9 kStride28[] = {
            {0, 0,  D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
            {0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
            {0, 20, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
            D3DDECL_END()
        };

        // VertexPositionNormalTexture (stride 32): POSITION0 (FLOAT3, 0), NORMAL0 (FLOAT3, 12),
        // TEXCOORD0 (FLOAT2, 24).
        constexpr D3DVERTEXELEMENT9 kStride32[] = {
            {0, 0,  D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
            {0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,   0},
            {0, 24, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
            D3DDECL_END()
        };

        // VertexPositionNormalTextureSkinned (stride 52): POSITION0 (0), NORMAL0 (12),
        // TEXCOORD0 (24), BLENDWEIGHT0 (FLOAT4, 32), BLENDINDICES0 (UBYTE4, 48).
        constexpr D3DVERTEXELEMENT9 kStride52[] = {
            {0, 0,  D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,     0},
            {0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,       0},
            {0, 24, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,     0},
            {0, 32, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT,  0},
            {0, 48, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0},
            D3DDECL_END()
        };
    }

    const D3DVERTEXELEMENT9* VertexElementsForStrideD3D9(std::size_t strideInBytes, UINT& count)
    {
        switch (strideInBytes)
        {
            case 16: count = 2; return kStride16;
            case 20: count = 2; return kStride20;
            case 24: count = 3; return kStride24;
            case 28: count = 3; return kStride28;
            case 32: count = 3; return kStride32;
            case 52: count = 5; return kStride52;
            default: count = 0; return nullptr;
        }
    }
}

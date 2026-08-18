// plan_dx9.md Phase D9-2 (D9-22). COLOR0 element type corrected in D9-82 (see below).
#include "CNA/Internal/Renderers/DirectX9/D3D9VertexDeclarations.hpp"

namespace CNA::Internal::Renderers::DirectX9
{
    namespace
    {
        // D9-82 real finding (plan_dx9.md's DirectX9Renderer's own first real draw, empirically
        // verified with cna_test_directx9_draw before AND after this fix -- not assumed): COLOR0 was
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

        // plan_cnj.md CNB-58/CNB-67-equivalent D3D9 porting task: VertexPositionNormalTangentTexture
        // (stride 48), used by the CNA CNAEXT "Pbr3D" custom shader (PbrEffect, unskinned). Byte
        // offsets match EasyGLRenderer.cpp's own ApplyLayout() `case 48:` exactly: POSITION0
        // (FLOAT3, 0), NORMAL0 (FLOAT3, 12), TANGENT0 (FLOAT4, 24 -- xyz=tangent, w=bitangent sign,
        // glTF convention), TEXCOORD0 (FLOAT2, 40).
        constexpr D3DVERTEXELEMENT9 kStride48[] = {
            {0, 0,  D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
            {0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,   0},
            {0, 24, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT,  0},
            {0, 40, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
            D3DDECL_END()
        };

        // D3D9 porting task (CNB-67-equivalent): the stride-52 SkinnedVertex layout above with a
        // per-vertex Color (normalized ubyte4) appended at the end (offset 52), matching
        // EasyGLRenderer.cpp's own ApplyLayout() `case 56:` byte-for-byte -- used by CNA's own
        // CNAEXT "SkinnedVertexColor3D" custom shader (real XNA SkinnedEffect has no vertex-color
        // input at all; Microsoft's own compiled SkinnedEffect.fx bytecode is never modified to add
        // one, see D3D9EffectDraw.cpp's own header comment on staying byte-identical to Microsoft's
        // shipped shaders).
        constexpr D3DVERTEXELEMENT9 kStride56[] = {
            {0, 0,  D3DDECLTYPE_FLOAT3,  D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,     0},
            {0, 12, D3DDECLTYPE_FLOAT3,  D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,       0},
            {0, 24, D3DDECLTYPE_FLOAT2,  D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,     0},
            {0, 32, D3DDECLTYPE_FLOAT4,  D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT,  0},
            {0, 48, D3DDECLTYPE_UBYTE4,  D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0},
            {0, 52, D3DDECLTYPE_UBYTE4N, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,        0},
            D3DDECL_END()
        };

        // plan_cnj.md CNB-75..79-equivalent D3D9 porting task: VertexPositionNormalTangentTextureSkinned
        // (stride 68), used by the CNA CNAEXT "PbrSkinned3D" custom shader (SkinnedPbrEffect). The
        // stride-48 PBR layout above with the stride-52/56 skinning suffix (BlendWeight, BlendIndices)
        // appended, matching EasyGLRenderer.cpp's own ApplyLayout() `case 68:` exactly.
        // plan_gltf.md GLTF-465: the two colour-carrying PBR records. Stride 60 is the stride-48
        // rigid record with TEXCOORD_1 at 48 and a packed COLOR_0 at 56; stride 80 is the skinned
        // record with TEXCOORD_1 at 68 and its own COLOR_0 at 76. D3DDECLTYPE_D3DCOLOR is the
        // right type for both -- it is the BGRA-ordered, normalized four-byte element D3D9 reads
        // into a float4 COLOR register, which is exactly what the importer packs. The second UV set
        // stays undeclared: this renderer's PBR shaders sample one set, which is a separate
        // capability gap (GLTF-344) and unchanged here.
        constexpr D3DVERTEXELEMENT9 kStride60Color[] = {
            {0, 0,  D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,     0},
            {0, 12, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,       0},
            {0, 24, D3DDECLTYPE_FLOAT4,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT,      0},
            {0, 40, D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,     0},
            {0, 56, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,        0},
            D3DDECL_END()
        };

        constexpr D3DVERTEXELEMENT9 kStride80Color[] = {
            {0, 0,  D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,     0},
            {0, 12, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,       0},
            {0, 24, D3DDECLTYPE_FLOAT4,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT,      0},
            {0, 40, D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,     0},
            {0, 48, D3DDECLTYPE_FLOAT4,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT,  0},
            {0, 64, D3DDECLTYPE_UBYTE4,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0},
            {0, 76, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,        0},
            D3DDECL_END()
        };

        constexpr D3DVERTEXELEMENT9 kStride68[] = {
            {0, 0,  D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,     0},
            {0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,       0},
            {0, 24, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT,      0},
            {0, 40, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,     0},
            {0, 48, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT,  0},
            {0, 64, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0},
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
            case 48: count = 4; return kStride48;
            // plan_gltf.md GLTF-462: stride 60's first four fields are byte-identical to stride 48,
            // and a D3D9 declaration carries element OFFSETS while the stride travels with
            // SetStreamSource -- so the stride-48 element list describes a stride-60 record exactly,
            // minus the second UV set and the packed colour this renderer's PBR shader does not
            // read. Without this row the declaration was null and the draw was refused outright.
            // GLTF-465 now consumes the colour slot: stride 60 declares it, so the PBR shader's
            // stride-60 variant receives the authored COLOR_0 instead of the opaque-white identity.
            case 60: count = 5; return kStride60Color;
            case 52: count = 5; return kStride52;
            case 56: count = 6; return kStride56;
            case 68: count = 6; return kStride68;
            case 80: count = 7; return kStride80Color;
            default: count = 0; return nullptr;
        }
    }
}

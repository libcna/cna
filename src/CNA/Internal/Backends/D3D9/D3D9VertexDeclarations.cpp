// plan_dx9.md Phase D9-2 (D9-22).
#include "CNA/Internal/Backends/D3D9/D3D9VertexDeclarations.hpp"

namespace CNA::Internal::Backends::D3D9
{
    namespace
    {
        // VertexPositionColor (stride 16): POSITION0 (FLOAT3, 0), COLOR0 (D3DCOLOR, 12).
        constexpr D3DVERTEXELEMENT9 kStride16[] = {
            {0, 0,  D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
            {0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,    0},
            D3DDECL_END()
        };

        // VertexPositionTexture (stride 20): POSITION0 (FLOAT3, 0), TEXCOORD0 (FLOAT2, 12).
        constexpr D3DVERTEXELEMENT9 kStride20[] = {
            {0, 0,  D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
            {0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
            D3DDECL_END()
        };

        // VertexPositionColorTexture (stride 24): POSITION0 (FLOAT3, 0), COLOR0 (D3DCOLOR, 12),
        // TEXCOORD0 (FLOAT2, 16).
        constexpr D3DVERTEXELEMENT9 kStride24[] = {
            {0, 0,  D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
            {0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,    0},
            {0, 16, D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
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
            case 32: count = 3; return kStride32;
            case 52: count = 5; return kStride52;
            default: count = 0; return nullptr;
        }
    }
}

// plan_dx.md Phase DIRECTX3 (DX-16-vtx).
#include "CNA/Internal/Renderers/D3DCommon/D3DVertexFormatHelper.hpp"

#include <iterator>

namespace CNA::Internal::Renderers::D3DCommon
{
    namespace
    {
        // Stride 16: VertexPositionColor.
        const D3D11_INPUT_ELEMENT_DESC kStride16[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,   0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        // Stride 20: VertexPositionTexture.
        const D3D11_INPUT_ELEMENT_DESC kStride20[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        // Stride 24: VertexPositionColorTexture.
        const D3D11_INPUT_ELEMENT_DESC kStride24[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,   0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        // Stride 32: VertexPositionNormalTexture.
        const D3D11_INPUT_ELEMENT_DESC kStride32[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        // Stride 52: VertexPositionNormalTextureSkinned.
        const D3D11_INPUT_ELEMENT_DESC kStride52[] = {
            { "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT,      0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        // plan_cnj.md CNB-58 follow-up: stride 48, VertexPositionNormalTangentTexture -- the
        // layout PbrEffect's normal mapping needs to build a per-pixel TBN basis. Matches
        // EasyGLRenderer::ApplyLayout's own case 48 byte-for-byte.
        const D3D11_INPUT_ELEMENT_DESC kStride48[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        // GLTF-386: the canonical rigid PBR dual-UV layout keeps stride 48's prefix byte-for-byte
        // and appends TEXCOORD_1 at offset 48. Four trailing padding bytes preserve CNA's shared
        // 60-byte CPU/GPU vertex contract.
        const D3D11_INPUT_ELEMENT_DESC kStride60[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,       0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        // plan_cnj.md CNB-67 follow-up: stride 56, the stride-52 SkinnedVertex layout above with a
        // per-vertex Color (normalized ubyte4) appended at the end (offset 52), rather than
        // inserted mid-layout -- keeps locations 0-4 byte-identical to kStride52, matching
        // EasyGLRenderer::ApplyLayout's own case 56 byte-for-byte.
        const D3D11_INPUT_ELEMENT_DESC kStride56[] = {
            { "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT,      0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",        0, DXGI_FORMAT_R8G8B8A8_UNORM,     0, 52, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        // plan_cnj.md CNB-58 follow-up: stride 68, VertexPositionNormalTangentTextureSkinned
        // (SkinnedPbrEffect) -- the stride-48 layout above with the stride-52 skinning suffix
        // (BlendWeight, BlendIndices) appended. Matches EasyGLRenderer::ApplyLayout's own
        // case 68 byte-for-byte.
        const D3D11_INPUT_ELEMENT_DESC kStride68[] = {
            { "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TANGENT",      0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT,      0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        // GLTF-386: skinned PBR dual-UV appends TEXCOORD_1 after stride 68's complete prefix.
        const D3D11_INPUT_ELEMENT_DESC kStride76[] = {
            { "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TANGENT",      0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT,      0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD",     1, DXGI_FORMAT_R32G32_FLOAT,       0, 68, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
    }

    const D3D11_INPUT_ELEMENT_DESC* InputElementsForStride(std::size_t strideInBytes, UINT& count)
    {
        switch (strideInBytes)
        {
            case 16: count = static_cast<UINT>(std::size(kStride16)); return kStride16;
            case 20: count = static_cast<UINT>(std::size(kStride20)); return kStride20;
            case 24: count = static_cast<UINT>(std::size(kStride24)); return kStride24;
            case 32: count = static_cast<UINT>(std::size(kStride32)); return kStride32;
            case 48: count = static_cast<UINT>(std::size(kStride48)); return kStride48;
            case 52: count = static_cast<UINT>(std::size(kStride52)); return kStride52;
            case 56: count = static_cast<UINT>(std::size(kStride56)); return kStride56;
            case 60: count = static_cast<UINT>(std::size(kStride60)); return kStride60;
            case 68: count = static_cast<UINT>(std::size(kStride68)); return kStride68;
            case 76: count = static_cast<UINT>(std::size(kStride76)); return kStride76;
            default: count = 0; return nullptr;
        }
    }

    namespace
    {
        // plan_dx.md Phase DX12 (DX-107): D3D12 counterparts of kStride16/20/24/32/52 above -- same
        // semantic names/byte offsets, just D3D12_INPUT_ELEMENT_DESC/D3D12_INPUT_PER_VERTEX_DATA
        // typed. Kept as separate arrays rather than a reinterpret_cast between the two structs --
        // the two types are verified field-for-field identical today (see this function's own
        // header doc comment), but reinterpret_casting across an SDK-defined struct boundary is the
        // kind of "assumed, not re-verified" shortcut this plan's Boundaries section explicitly
        // warns against if the SDK/MinGW header versions ever diverge.
        const D3D12_INPUT_ELEMENT_DESC kStride16D3D12[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,   0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        const D3D12_INPUT_ELEMENT_DESC kStride20D3D12[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        const D3D12_INPUT_ELEMENT_DESC kStride24D3D12[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,   0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        const D3D12_INPUT_ELEMENT_DESC kStride32D3D12[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        const D3D12_INPUT_ELEMENT_DESC kStride52D3D12[] = {
            { "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT,      0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        // D3D12 PBR/skinned-vertex-color reconciliation follow-up: D3D12 counterparts of
        // kStride48/56/68 above (D3D11-flavor, InputElementsForStride()) -- same semantic names/
        // byte offsets, just D3D12_INPUT_ELEMENT_DESC/D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA
        // typed, same "separate array, not a reinterpret_cast" discipline this file's own
        // kStride16D3D12 doc comment already established.

        // Stride 48: VertexPositionNormalTangentTexture (PbrEffect). Matches kStride48 exactly.
        const D3D12_INPUT_ELEMENT_DESC kStride48D3D12[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        // Stride 56: the stride-52 SkinnedVertex layout with a per-vertex Color appended at offset
        // 52 (SkinnedEffect.VertexColorEnabled, Skinned3dColored/Skinned3dVertexLitColored).
        // Matches kStride56 exactly.
        const D3D12_INPUT_ELEMENT_DESC kStride56D3D12[] = {
            { "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT,      0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR",        0, DXGI_FORMAT_R8G8B8A8_UNORM,     0, 52, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        // Stride 68: VertexPositionNormalTangentTextureSkinned (SkinnedPbrEffect) -- the stride-48
        // layout above with the stride-52 skinning suffix appended. Matches kStride68 exactly.
        const D3D12_INPUT_ELEMENT_DESC kStride68D3D12[] = {
            { "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TANGENT",      0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT,      0, 64, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };
    }

    const D3D12_INPUT_ELEMENT_DESC* InputElementsForStrideD3D12(std::size_t strideInBytes, UINT& count)
    {
        switch (strideInBytes)
        {
            case 16: count = static_cast<UINT>(std::size(kStride16D3D12)); return kStride16D3D12;
            case 20: count = static_cast<UINT>(std::size(kStride20D3D12)); return kStride20D3D12;
            case 24: count = static_cast<UINT>(std::size(kStride24D3D12)); return kStride24D3D12;
            case 32: count = static_cast<UINT>(std::size(kStride32D3D12)); return kStride32D3D12;
            case 48: count = static_cast<UINT>(std::size(kStride48D3D12)); return kStride48D3D12;
            case 52: count = static_cast<UINT>(std::size(kStride52D3D12)); return kStride52D3D12;
            case 56: count = static_cast<UINT>(std::size(kStride56D3D12)); return kStride56D3D12;
            case 68: count = static_cast<UINT>(std::size(kStride68D3D12)); return kStride68D3D12;
            default: count = 0; return nullptr;
        }
    }
}

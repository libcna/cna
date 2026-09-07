// plans/plan_dx.md Phase DIRECTX3 (DX-16-vtx).
#include "CNA/Internal/Renderers/D3DCommon/D3DVertexFormatHelper.hpp"

#include <iterator>

namespace CNA::Internal::Renderers::D3DCommon
{
    namespace
    {
        using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
        using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

        const char* SemanticName(VertexElementUsage usage)
        {
            switch (usage)
            {
                case VertexElementUsage::Position:          return "POSITION";
                case VertexElementUsage::Color:             return "COLOR";
                case VertexElementUsage::TextureCoordinate: return "TEXCOORD";
                case VertexElementUsage::Normal:            return "NORMAL";
                case VertexElementUsage::Binormal:          return "BINORMAL";
                case VertexElementUsage::Tangent:           return "TANGENT";
                case VertexElementUsage::BlendIndices:      return "BLENDINDICES";
                case VertexElementUsage::BlendWeight:       return "BLENDWEIGHT";
                case VertexElementUsage::Depth:             return "DEPTH";
                case VertexElementUsage::Fog:               return "FOG";
                case VertexElementUsage::PointSize:         return "PSIZE";
                case VertexElementUsage::Sample:            return "SAMPLE";
                case VertexElementUsage::TessellateFactor:  return "TESSFACTOR";
            }
            return nullptr;
        }

        DXGI_FORMAT DxgiFormat(VertexElementFormat format)
        {
            switch (format)
            {
                case VertexElementFormat::Single:           return DXGI_FORMAT_R32_FLOAT;
                case VertexElementFormat::Vector2:          return DXGI_FORMAT_R32G32_FLOAT;
                case VertexElementFormat::Vector3:          return DXGI_FORMAT_R32G32B32_FLOAT;
                case VertexElementFormat::Vector4:          return DXGI_FORMAT_R32G32B32A32_FLOAT;
                case VertexElementFormat::Color:            return DXGI_FORMAT_R8G8B8A8_UNORM;
                case VertexElementFormat::Byte4:            return DXGI_FORMAT_R8G8B8A8_UINT;
                case VertexElementFormat::Short2:           return DXGI_FORMAT_R16G16_SINT;
                case VertexElementFormat::Short4:           return DXGI_FORMAT_R16G16B16A16_SINT;
                case VertexElementFormat::NormalizedShort2: return DXGI_FORMAT_R16G16_SNORM;
                case VertexElementFormat::NormalizedShort4: return DXGI_FORMAT_R16G16B16A16_SNORM;
                case VertexElementFormat::HalfVector2:      return DXGI_FORMAT_R16G16_FLOAT;
                case VertexElementFormat::HalfVector4:      return DXGI_FORMAT_R16G16B16A16_FLOAT;
            }
            return DXGI_FORMAT_UNKNOWN;
        }

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

        // plans/plan_cnj.md CNB-58 follow-up: stride 48, VertexPositionNormalTangentTexture -- the
        // layout PbrEffect's normal mapping needs to build a per-pixel TBN basis. Matches
        // EasyGLRenderer::ApplyLayout's own case 48 byte-for-byte.
        const D3D11_INPUT_ELEMENT_DESC kStride48[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        // GLTF-386: the canonical rigid PBR dual-UV layout keeps stride 48's prefix byte-for-byte
        // and appends TEXCOORD_1 at offset 48. GLTF-462 gave the four trailing bytes GLTF-182 had
        // reserved to a packed COLOR_0, so every stride-60 record carries a colour -- the authored
        // one, or opaque white, the multiplier's identity.
        const D3D11_INPUT_ELEMENT_DESC kStride60[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,       0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,     0, 56, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        // plans/plan_cnj.md CNB-67 follow-up: stride 56, the stride-52 SkinnedVertex layout above with a
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

        // plans/plan_cnj.md CNB-58 follow-up: stride 68, VertexPositionNormalTangentTextureSkinned
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

        // plans/plan_gltf.md GLTF-463: stride 80 is stride 76's record with a packed COLOR_0 appended --
        // the skinned counterpart of stride 60's own colour slot.
        const D3D11_INPUT_ELEMENT_DESC kStride80[] = {
            { "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TANGENT",      0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT,      0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD",     1, DXGI_FORMAT_R32G32_FLOAT,       0, 68, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",        0, DXGI_FORMAT_R8G8B8A8_UNORM,     0, 76, D3D11_INPUT_PER_VERTEX_DATA, 0 },
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
            case 80: count = static_cast<UINT>(std::size(kStride80)); return kStride80;
            default: count = 0; return nullptr;
        }
    }

    D3DVertexDeclarationKey VertexDeclarationCacheKey(
        const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>& elements)
    {
        D3DVertexDeclarationKey key;
        key.reserve(elements.size());
        for (const auto& element : elements)
        {
            key.push_back({
                element.getOffsetProperty(),
                static_cast<int>(element.getVertexElementFormatProperty()),
                static_cast<int>(element.getVertexElementUsageProperty()),
                element.getUsageIndexProperty(),
            });
        }
        return key;
    }

    bool InputElementsForDeclaration(
        const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>& elements,
        std::vector<D3D11_INPUT_ELEMENT_DESC>& output)
    {
        output.clear();
        output.reserve(elements.size());
        for (const auto& element : elements)
        {
            const char* semantic = SemanticName(element.getVertexElementUsageProperty());
            const DXGI_FORMAT format = DxgiFormat(element.getVertexElementFormatProperty());
            if (semantic == nullptr || format == DXGI_FORMAT_UNKNOWN ||
                element.getOffsetProperty() < 0 || element.getUsageIndexProperty() < 0)
            {
                output.clear();
                return false;
            }

            output.push_back({
                semantic,
                static_cast<UINT>(element.getUsageIndexProperty()),
                format,
                0,
                static_cast<UINT>(element.getOffsetProperty()),
                D3D11_INPUT_PER_VERTEX_DATA,
                0,
            });
        }
        return !output.empty();
    }

    bool InputElementsForDeclarationD3D12(
        const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>& elements,
        std::vector<D3D12_INPUT_ELEMENT_DESC>& output)
    {
        output.clear();
        output.reserve(elements.size());
        for (const auto& element : elements)
        {
            const char* semantic = SemanticName(element.getVertexElementUsageProperty());
            const DXGI_FORMAT format = DxgiFormat(element.getVertexElementFormatProperty());
            if (semantic == nullptr || format == DXGI_FORMAT_UNKNOWN ||
                element.getOffsetProperty() < 0 || element.getUsageIndexProperty() < 0)
            {
                output.clear();
                return false;
            }

            output.push_back({
                semantic,
                static_cast<UINT>(element.getUsageIndexProperty()),
                format,
                0,
                static_cast<UINT>(element.getOffsetProperty()),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
                0,
            });
        }
        return !output.empty();
    }

    bool DeclarationHasElement(
        const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>& elements,
        Microsoft::Xna::Framework::Graphics::VertexElementUsage usage,
        int usageIndex)
    {
        for (const auto& element : elements)
        {
            if (element.getVertexElementUsageProperty() == usage &&
                element.getUsageIndexProperty() == usageIndex)
            {
                return true;
            }
        }
        return false;
    }

    namespace
    {
        // plans/plan_dx.md Phase DX12 (DX-107): D3D12 counterparts of kStride16/20/24/32/52 above -- same
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

        // GLTF-386: stride 48 plus TEXCOORD_1 at offset 48, and since GLTF-462 a packed COLOR_0 in
        // the four trailing bytes -- matching kStride60 exactly.
        const D3D12_INPUT_ELEMENT_DESC kStride60D3D12[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,       0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,     0, 56, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
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

        // GLTF-386: skinned PBR dual-UV layout, matching kStride76 exactly.
        const D3D12_INPUT_ELEMENT_DESC kStride76D3D12[] = {
            { "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TANGENT",      0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT,      0, 64, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD",     1, DXGI_FORMAT_R32G32_FLOAT,       0, 68, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        // plans/plan_gltf.md GLTF-463: stride 80, matching kStride80 exactly.
        const D3D12_INPUT_ELEMENT_DESC kStride80D3D12[] = {
            { "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TANGENT",      0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT,      0, 64, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD",     1, DXGI_FORMAT_R32G32_FLOAT,       0, 68, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR",        0, DXGI_FORMAT_R8G8B8A8_UNORM,     0, 76, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
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
            case 60: count = static_cast<UINT>(std::size(kStride60D3D12)); return kStride60D3D12;
            case 68: count = static_cast<UINT>(std::size(kStride68D3D12)); return kStride68D3D12;
            case 76: count = static_cast<UINT>(std::size(kStride76D3D12)); return kStride76D3D12;
            case 80: count = static_cast<UINT>(std::size(kStride80D3D12)); return kStride80D3D12;
            default: count = 0; return nullptr;
        }
    }
}

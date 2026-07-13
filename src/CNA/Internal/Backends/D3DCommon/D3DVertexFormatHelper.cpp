// plan_dx.md Phase DX3 (DX-16-vtx).
#include "CNA/Internal/Backends/D3DCommon/D3DVertexFormatHelper.hpp"

#include <iterator>

namespace CNA::Internal::Backends::D3DCommon
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
    }

    const D3D11_INPUT_ELEMENT_DESC* InputElementsForStride(std::size_t strideInBytes, UINT& count)
    {
        switch (strideInBytes)
        {
            case 16: count = static_cast<UINT>(std::size(kStride16)); return kStride16;
            case 20: count = static_cast<UINT>(std::size(kStride20)); return kStride20;
            case 24: count = static_cast<UINT>(std::size(kStride24)); return kStride24;
            case 32: count = static_cast<UINT>(std::size(kStride32)); return kStride32;
            case 52: count = static_cast<UINT>(std::size(kStride52)); return kStride52;
            default: count = 0; return nullptr;
        }
    }
}

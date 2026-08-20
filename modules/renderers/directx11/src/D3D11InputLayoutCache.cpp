// plans/plan_dx.md Phase DIRECTX5 (DX-32).
#include "CNA/Internal/Renderers/DirectX11/D3D11InputLayoutCache.hpp"

#include "CNA/Internal/Renderers/D3DCommon/D3DVertexFormatHelper.hpp"

namespace CNA::Internal::Renderers::DirectX11
{
    ComPtr<ID3D11InputLayout> D3D11InputLayoutCache::GetOrCreate(
        ID3D11Device* device, D3DShaderVariant variant, std::size_t strideInBytes)
    {
        const auto key = std::make_pair(static_cast<int>(variant), strideInBytes);
        const auto it = cache_.find(key);
        if (it != cache_.end())
            return it->second;

        ComPtr<ID3D11InputLayout> layout;

        UINT elementCount = 0;
        const D3D11_INPUT_ELEMENT_DESC* elements =
            CNA::Internal::Renderers::D3DCommon::InputElementsForStride(strideInBytes, elementCount);
        if (elements == nullptr || elementCount == 0 || device == nullptr)
        {
            cache_.emplace(key, layout);
            return layout;
        }

        const uint8_t* vsBytes = nullptr;
        std::size_t vsSize = 0;
        CNA::Internal::Renderers::D3DCommon::GetVertexShaderBytecode(variant, vsBytes, vsSize);
        if (vsBytes == nullptr || vsSize == 0)
        {
            cache_.emplace(key, layout);
            return layout;
        }

        device->CreateInputLayout(elements, elementCount, vsBytes, vsSize, layout.GetAddressOf());
        cache_.emplace(key, layout);
        return layout;
    }
}

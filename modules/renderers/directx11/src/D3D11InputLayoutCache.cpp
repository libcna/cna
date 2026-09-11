// plans/plan_dx.md Phase DIRECTX5 (DX-32).
#include "CNA/Internal/Renderers/DirectX11/D3D11InputLayoutCache.hpp"

#include "CNA/Internal/Renderers/D3DCommon/D3DVertexFormatHelper.hpp"

namespace CNA::Internal::Renderers::DirectX11
{
    ComPtr<ID3D11InputLayout> D3D11InputLayoutCache::GetOrCreate(
        ID3D11Device* device, D3DShaderVariant variant, std::size_t strideInBytes)
    {
        static const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement> noDeclaration;
        return GetOrCreate(device, variant, strideInBytes, noDeclaration);
    }

    ComPtr<ID3D11InputLayout> D3D11InputLayoutCache::GetOrCreate(
        ID3D11Device* device, D3DShaderVariant variant, std::size_t strideInBytes,
        const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>& declaration)
    {
        static const std::vector<D3DCommon::D3DVertexInputElement> noInputElements;
        return GetOrCreate(device, variant, strideInBytes, declaration, noInputElements);
    }

    ComPtr<ID3D11InputLayout> D3D11InputLayoutCache::GetOrCreate(
        ID3D11Device* device, D3DShaderVariant variant, std::size_t strideInBytes,
        const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>& declaration,
        const std::vector<D3DCommon::D3DVertexInputElement>& inputLayout)
    {
        const Key key{
            static_cast<int>(variant), strideInBytes,
            D3DCommon::VertexDeclarationCacheKey(declaration),
            D3DCommon::VertexInputLayoutCacheKey(inputLayout)};
        const auto it = cache_.find(key);
        if (it != cache_.end())
            return it->second;

        ComPtr<ID3D11InputLayout> layout;

        UINT elementCount = 0;
        std::vector<D3D11_INPUT_ELEMENT_DESC> translatedElements;
        const D3D11_INPUT_ELEMENT_DESC* elements = nullptr;
        if (!inputLayout.empty())
        {
            if (D3DCommon::InputElementsForLayout(inputLayout, translatedElements))
            {
                elements = translatedElements.data();
                elementCount = static_cast<UINT>(translatedElements.size());
            }
        }
        else if (!declaration.empty())
        {
            if (D3DCommon::InputElementsForDeclaration(declaration, translatedElements))
            {
                elements = translatedElements.data();
                elementCount = static_cast<UINT>(translatedElements.size());
            }
        }
        else
        {
            elements = D3DCommon::InputElementsForStride(strideInBytes, elementCount);
        }
        if (elements == nullptr || elementCount == 0 || device == nullptr)
        {
            cache_.emplace(key, layout);
            return layout;
        }

        const uint8_t* vsBytes = nullptr;
        std::size_t vsSize = 0;
        D3DCommon::GetVertexShaderBytecode(variant, vsBytes, vsSize);
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

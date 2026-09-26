// plans/plan_dx.md Phase DIRECTX8 (DX-58).
#include "CNA/Internal/Renderers/DirectX11/D3D11EffectRenderer.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11RenderTargets.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11Textures.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11Texture2DArray.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11StorageTexture2D.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <regex>
#include <stdexcept>

namespace CNA::Internal::Renderers::DirectX11
{
    namespace
    {
        std::string FormatHr(HRESULT hr)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
            return buf;
        }
    }

    D3D11EffectRenderer::D3D11EffectRenderer(ID3D11Device* device, ID3D11DeviceContext* context)
        : device_(device), context_(context)
    {
    }

    bool D3D11EffectRenderer::CompileProgram(const std::string& vertSrc, const std::string& fragSrc)
    {
        compileError_.clear();
        valid_ = false;
        vs_.Reset();
        baseInstanceVs_.Reset();
        ps_.Reset();
        vsBytecode_.Reset();
        baseInstanceVsBytecode_.Reset();
        psBytecode_.Reset();
        inputLayouts_.clear();
        baseInstanceInputLayouts_.clear();
        vertexSource_ = vertSrc;
        static const std::regex instanceIdSemantic(
            R"(:\s*SV_InstanceID\b)", std::regex_constants::icase);
        hasInstanceIdInput_ = std::regex_search(vertSrc, instanceIdSemantic);
        reflection_.Reset();
        constantBuffers_ = {};
        textures_ = {};

        const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;

        Microsoft::WRL::ComPtr<ID3DBlob> vsErr;
        HRESULT hr = D3DCompile(vertSrc.data(), vertSrc.size(), "ShaderEffect_vs", nullptr, nullptr,
                                "main", "vs_5_0", flags, 0,
                                vsBytecode_.GetAddressOf(), vsErr.GetAddressOf());
        if (FAILED(hr))
        {
            compileError_ = vsErr
                ? std::string(static_cast<const char*>(vsErr->GetBufferPointer()), vsErr->GetBufferSize())
                : ("D3DCompile (vertex) failed, hr=" + FormatHr(hr));
            return false;
        }

        Microsoft::WRL::ComPtr<ID3DBlob> psErr;
        hr = D3DCompile(fragSrc.data(), fragSrc.size(), "ShaderEffect_ps", nullptr, nullptr,
                        "main", "ps_5_0", flags, 0,
                        psBytecode_.GetAddressOf(), psErr.GetAddressOf());
        if (FAILED(hr))
        {
            compileError_ = psErr
                ? std::string(static_cast<const char*>(psErr->GetBufferPointer()), psErr->GetBufferSize())
                : ("D3DCompile (pixel) failed, hr=" + FormatHr(hr));
            return false;
        }

        if (!reflection_.AddShader(
                vsBytecode_->GetBufferPointer(), vsBytecode_->GetBufferSize(), compileError_) ||
            !reflection_.AddShader(
                psBytecode_->GetBufferPointer(), psBytecode_->GetBufferSize(), compileError_))
            return false;

        hr = device_->CreateVertexShader(vsBytecode_->GetBufferPointer(), vsBytecode_->GetBufferSize(),
                                         nullptr, vs_.ReleaseAndGetAddressOf());
        if (FAILED(hr)) { compileError_ = "CreateVertexShader failed, hr=" + FormatHr(hr); return false; }

        hr = device_->CreatePixelShader(psBytecode_->GetBufferPointer(), psBytecode_->GetBufferSize(),
                                        nullptr, ps_.ReleaseAndGetAddressOf());
        if (FAILED(hr)) { compileError_ = "CreatePixelShader failed, hr=" + FormatHr(hr); return false; }

        for (int slot = 0; slot < reflection_.GetConstantBufferCount(); ++slot)
        {
            const auto& reflected = reflection_.GetConstantBuffer(slot);
            if (!reflected.present)
                continue;
            D3D11_BUFFER_DESC cbDesc{};
            cbDesc.ByteWidth = static_cast<UINT>((reflected.data.size() + 15u) & ~15u);
            cbDesc.Usage = D3D11_USAGE_DYNAMIC;
            cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            hr = device_->CreateBuffer(
                &cbDesc, nullptr,
                constantBuffers_[static_cast<std::size_t>(slot)].ReleaseAndGetAddressOf());
            if (FAILED(hr))
            {
                compileError_ = "Constant buffer creation failed, hr=" + FormatHr(hr);
                return false;
            }
        }

        valid_ = true;
        return true;
    }

    void D3D11EffectRenderer::Bind()
    {
        if (!valid_)
            return;

        BindProgramEXT(true);
    }

    bool D3D11EffectRenderer::BindSpriteEXT()
    {
        if (!valid_)
            return false;
        BindProgramEXT(true);
        using Microsoft::Xna::Framework::Graphics::VertexElement;
        using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
        using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
        const std::vector<D3DCommon::D3DVertexInputElement> spriteElements{
            {VertexElement(0, VertexElementFormat::Vector2, VertexElementUsage::Position, 0), 0, 0, false},
            {VertexElement(8, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0), 0, 0, false},
            {VertexElement(16, VertexElementFormat::Vector4, VertexElementUsage::Color, 0), 0, 0, false},
        };
        const auto layout = GetOrCreateInputLayoutEXT({}, spriteElements);
        if (!layout)
            return false;
        context_->IASetInputLayout(layout.Get());
        return true;
    }

    void D3D11EffectRenderer::BindProgramEXT(const bool preserveImplicitTexture0)
    {
        if (!valid_)
            return;

        std::array<ID3D11Buffer*, D3DCommon::D3DProgramReflection::kMaxConstantBuffers> buffers{};
        for (int slot = 0; slot < reflection_.GetConstantBufferCount(); ++slot)
        {
            const auto& reflected = reflection_.GetConstantBuffer(slot);
            ID3D11Buffer* buffer = constantBuffers_[static_cast<std::size_t>(slot)].Get();
            buffers[static_cast<std::size_t>(slot)] = buffer;
            if (!reflected.present || buffer == nullptr)
                continue;
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(context_->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            {
                std::memcpy(mapped.pData, reflected.data.data(), reflected.data.size());
                context_->Unmap(buffer, 0);
            }
        }

        context_->VSSetShader(vs_.Get(), nullptr, 0);
        context_->PSSetShader(ps_.Get(), nullptr, 0);
        const UINT bufferCount = static_cast<UINT>(reflection_.GetConstantBufferCount());
        if (bufferCount > 0)
        {
            context_->VSSetConstantBuffers(0, bufferCount, buffers.data());
            context_->PSSetConstantBuffers(0, bufferCount, buffers.data());
        }

        std::array<ID3D11ShaderResourceView*,
                   D3DCommon::D3DProgramReflection::kMaxShaderResources> srvs{};
        for (int slot = 0; slot < reflection_.GetShaderResourceCount(); ++slot)
            srvs[static_cast<std::size_t>(slot)] = ResolveTextureSrvEXT(slot);
        const UINT resourceCount = static_cast<UINT>(reflection_.GetShaderResourceCount());
        if (resourceCount > 0)
        {
            if (!preserveImplicitTexture0 || textures_[0].explicitlySet)
                context_->PSSetShaderResources(0, 1, srvs.data());
            if (resourceCount > 1)
                context_->PSSetShaderResources(1, resourceCount - 1, srvs.data() + 1);
        }
    }

    void D3D11EffectRenderer::Unbind()
    {
        // D3D11 has no deferred program state to restore. The next sprite or 3D draw binds its own
        // shaders, resources and input layout before issuing work.
    }

    void D3D11EffectRenderer::SetUniformMat4(const char* name, const float* matrix)
    {
        reflection_.SetMat4(name, matrix);
    }

    void D3D11EffectRenderer::SetUniformVec4(const char* name, float x, float y, float z, float w)
    {
        reflection_.SetVec4(name, x, y, z, w);
    }

    void D3D11EffectRenderer::SetUniformVec3(const char* name, float x, float y, float z)
    {
        reflection_.SetVec3(name, x, y, z);
    }

    void D3D11EffectRenderer::SetUniformVec2(const char* name, float x, float y)
    {
        reflection_.SetVec2(name, x, y);
    }

    void D3D11EffectRenderer::SetUniformFloat(const char* name, float value)
    {
        reflection_.SetFloat(name, value);
    }

    void D3D11EffectRenderer::SetUniformInt(const char* name, int value)
    {
        reflection_.SetInt(name, value);
    }

    void D3D11EffectRenderer::SetUniformFloatArray(
        const char* name, const float* values, const int count)
    {
        reflection_.SetFloatArray(name, values, count);
    }

    void D3D11EffectRenderer::SetUniformVec2Array(
        const char* name, const float* values, const int count)
    {
        reflection_.SetVec2Array(name, values, count);
    }

    void D3D11EffectRenderer::SetUniformVec3Array(
        const char* name, const float* values, const int count)
    {
        reflection_.SetVec3Array(name, values, count);
    }

    void D3D11EffectRenderer::SetUniformMat4Array(
        const char* name, const float* matrices, const int count)
    {
        reflection_.SetMat4Array(name, matrices, count);
    }

    void D3D11EffectRenderer::BindTexture(const int unit, ITextureRenderer* texture)
    {
        if (unit < 0 || unit >= static_cast<int>(textures_.size()))
            return;
        textures_[static_cast<std::size_t>(unit)] =
            {TextureKind::Texture2D, texture, true};
    }

    void D3D11EffectRenderer::BindTextureCube(const int unit, ITextureCubeRenderer* texture)
    {
        if (unit < 0 || unit >= static_cast<int>(textures_.size()))
            return;
        textures_[static_cast<std::size_t>(unit)] =
            {TextureKind::TextureCube, texture, true};
    }

    void D3D11EffectRenderer::BindTexture3D(int unit, ITexture3DRenderer* texture)
    {
        if (unit < 0 || unit >= static_cast<int>(textures_.size()))
            return;
        textures_[static_cast<std::size_t>(unit)] =
            {TextureKind::Texture3D, texture, true};
    }

    bool D3D11EffectRenderer::BindTexture2DArrayEXT(
        int unit, std::shared_ptr<ITexture2DArrayRenderer> texture)
    {
        if (unit < 0 || unit >= static_cast<int>(textures_.size()))
            return false;
        auto* native = texture ? dynamic_cast<D3D11Texture2DArray*>(texture.get()) : nullptr;
        if (texture && (!native || native->GetDeviceEXT() != device_.Get()))
            return false;
        auto& binding = textures_[static_cast<std::size_t>(unit)];
        binding.kind = texture ? TextureKind::Texture2DArray : TextureKind::None;
        binding.texture = native;
        binding.explicitlySet = true;
        binding.retainedStorage.reset();
        binding.retainedArray = std::move(texture);
        return true;
    }

    bool D3D11EffectRenderer::BindStorageTexture2DEXT(
        int unit, std::shared_ptr<IStorageTexture2DRenderer> texture)
    {
        if (unit < 0 || unit >= static_cast<int>(textures_.size()))
            return false;
        auto* native = texture ? dynamic_cast<D3D11StorageTexture2D*>(texture.get()) : nullptr;
        if (texture && (!native || native->GetDeviceEXT() != device_.Get() ||
                        !native->GetShaderResourceViewEXT()))
            return false;
        auto& binding = textures_[static_cast<std::size_t>(unit)];
        binding.kind = texture ? TextureKind::StorageTexture2D : TextureKind::None;
        binding.texture = native;
        binding.explicitlySet = true;
        binding.retainedArray.reset();
        binding.retainedStorage = std::move(texture);
        return true;
    }

    void D3D11EffectRenderer::SetViewportSizeEXT(float width, float height)
    {
        reflection_.SetVec2("vpSize", width, height);
        reflection_.SetVec2("viewportSize", width, height);
    }

    ComPtr<ID3D11InputLayout> D3D11EffectRenderer::GetOrCreateInputLayoutEXT(
        const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>& declaration,
        const std::vector<D3DCommon::D3DVertexInputElement>& inputElements,
        const bool logicalInstanceId)
    {
        const InputLayoutKey key{
            D3DCommon::VertexDeclarationCacheKey(declaration),
            D3DCommon::VertexInputLayoutCacheKey(inputElements)};
        auto& layouts = logicalInstanceId ? baseInstanceInputLayouts_ : inputLayouts_;
        if (const auto found = layouts.find(key); found != layouts.end())
            return found->second;

        std::vector<D3D11_INPUT_ELEMENT_DESC> translated;
        const bool translatedOk = !inputElements.empty()
            ? D3DCommon::InputElementsForLayout(inputElements, translated)
            : D3DCommon::InputElementsForDeclaration(declaration, translated);
        if (logicalInstanceId)
            translated.push_back({"CNA_LOGICAL_INSTANCE_ID", 0, DXGI_FORMAT_R32_UINT,
                                  static_cast<UINT>(kMaxVertexStreams), 0,
                                  D3D11_INPUT_PER_INSTANCE_DATA, 1});
        ComPtr<ID3D11InputLayout> layout;
        ID3DBlob* bytecode = logicalInstanceId
            ? baseInstanceVsBytecode_.Get() : vsBytecode_.Get();
        if (translatedOk && bytecode)
        {
            device_->CreateInputLayout(
                translated.data(), static_cast<UINT>(translated.size()),
                bytecode->GetBufferPointer(), bytecode->GetBufferSize(),
                layout.ReleaseAndGetAddressOf());
        }
        layouts.emplace(key, layout);
        return layout;
    }

    bool D3D11EffectRenderer::EnsureBaseInstanceVertexShaderEXT()
    {
        if (baseInstanceVs_)
            return true;
        static const std::regex instanceIdSemantic(
            R"(:\s*SV_InstanceID\b)", std::regex_constants::icase);
        const std::string source = std::regex_replace(
            vertexSource_, instanceIdSemantic, ": CNA_LOGICAL_INSTANCE_ID");
        ComPtr<ID3DBlob> errors;
        const HRESULT compileResult = D3DCompile(
            source.data(), source.size(), "ShaderEffect_base_instance_vs", nullptr, nullptr,
            "main", "vs_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
            0, baseInstanceVsBytecode_.ReleaseAndGetAddressOf(), errors.GetAddressOf());
        if (FAILED(compileResult))
        {
            compileError_ = errors
                ? std::string(static_cast<const char*>(errors->GetBufferPointer()),
                              errors->GetBufferSize())
                : ("D3DCompile (base-instance vertex) failed, hr=" + FormatHr(compileResult));
            return false;
        }
        const HRESULT createResult = device_->CreateVertexShader(
            baseInstanceVsBytecode_->GetBufferPointer(),
            baseInstanceVsBytecode_->GetBufferSize(), nullptr,
            baseInstanceVs_.ReleaseAndGetAddressOf());
        if (FAILED(createResult))
        {
            compileError_ = "CreateVertexShader (base-instance) failed, hr=" +
                FormatHr(createResult);
            return false;
        }
        return true;
    }

    bool D3D11EffectRenderer::BindForDrawEXT(
        const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>& declaration,
        const std::vector<D3DCommon::D3DVertexInputElement>& inputElements)
    {
        if (!valid_)
            return false;
        const auto layout = GetOrCreateInputLayoutEXT(declaration, inputElements);
        if (!layout)
            return false;
        BindProgramEXT(false);
        context_->IASetInputLayout(layout.Get());
        return true;
    }

    bool D3D11EffectRenderer::BindForBaseInstanceDrawEXT(
        const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>& declaration,
        const std::vector<D3DCommon::D3DVertexInputElement>& inputElements,
        bool& usesLogicalIdStream)
    {
        usesLogicalIdStream = hasInstanceIdInput_;
        if (!hasInstanceIdInput_)
            return BindForDrawEXT(declaration, inputElements);
        if (!valid_ || !EnsureBaseInstanceVertexShaderEXT())
            return false;
        const auto layout = GetOrCreateInputLayoutEXT(
            declaration, inputElements, true);
        if (!layout)
        {
            compileError_ = "DirectX11 could not create the base-instance input layout.";
            return false;
        }
        BindProgramEXT(false);
        context_->VSSetShader(baseInstanceVs_.Get(), nullptr, 0);
        context_->IASetInputLayout(layout.Get());
        return true;
    }

    ID3D11ShaderResourceView* D3D11EffectRenderer::ResolveTextureSrvEXT(const int slot) const
    {
        if (!reflection_.HasShaderResource(slot) || slot < 0 ||
            slot >= static_cast<int>(textures_.size()))
            return nullptr;
        const auto& binding = textures_[static_cast<std::size_t>(slot)];
        switch (binding.kind)
        {
            case TextureKind::Texture2D:
                if (const auto* texture = dynamic_cast<const D3D11TextureRenderer*>(
                        static_cast<ITextureRenderer*>(binding.texture)))
                    return texture->GetShaderResourceViewEXT();
                if (const auto* target = dynamic_cast<const D3D11RenderTargetRenderer*>(
                        static_cast<ITextureRenderer*>(binding.texture)))
                    return target->GetShaderResourceViewEXT();
                break;
            case TextureKind::TextureCube:
                if (const auto* texture = dynamic_cast<const D3D11TextureCubeRenderer*>(
                        static_cast<ITextureCubeRenderer*>(binding.texture)))
                    return texture->GetShaderResourceViewEXT();
                if (const auto* target = dynamic_cast<const D3D11RenderTargetCubeRenderer*>(
                        static_cast<ITextureCubeRenderer*>(binding.texture)))
                    return target->GetShaderResourceViewEXT();
                break;
            case TextureKind::Texture3D:
                if (const auto* texture = dynamic_cast<const D3D11Texture3DRenderer*>(
                        static_cast<ITexture3DRenderer*>(binding.texture)))
                    return texture->GetShaderResourceViewEXT();
                break;
            case TextureKind::Texture2DArray:
                if (const auto* texture = dynamic_cast<const D3D11Texture2DArray*>(
                        static_cast<ITexture2DArrayRenderer*>(binding.texture)))
                    return texture->GetShaderResourceViewEXT();
                break;
            case TextureKind::StorageTexture2D:
                if (const auto* texture = dynamic_cast<const D3D11StorageTexture2D*>(
                        static_cast<IStorageTexture2DRenderer*>(binding.texture)))
                    return texture->GetShaderResourceViewEXT();
                break;
            case TextureKind::None:
                break;
        }
        return nullptr;
    }
}

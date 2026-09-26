// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/DirectX11/D3D11ComputeShader.hpp"

#include "CNA/Internal/Renderers/DirectX11/D3D11IndirectBuffer.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11RenderTargets.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11StorageTexture2D.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11Textures.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace CNA::Internal::Renderers::DirectX11
{
    D3D11ComputeShader::D3D11ComputeShader(
        ID3D11Device* device, ID3D11DeviceContext* context)
        : device_(device), context_(context)
    {
        if (device == nullptr || context == nullptr)
            throw std::invalid_argument("D3D11 compute program requires a device and context");
        D3D11_SAMPLER_DESC description{};
        description.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        description.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        description.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        description.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        description.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device_->CreateSamplerState(&description, sampler_.GetAddressOf())))
            throw std::runtime_error("D3D11 compute sampler creation failed");
    }

    bool D3D11ComputeShader::CompileProgram(const std::string& source)
    {
        shader_.Reset();
        uniformBuffers_ = {};
        storageBuffers_ = {};
        storageTextures_ = {};
        constantBuffers_ = {};
        textures_ = {};
        reflection_.Reset();
        compileError_.clear();

        Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const HRESULT compiled = D3DCompile(
            source.data(), source.size(), "ComputeShader_cs", nullptr, nullptr,
            "main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS |
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
            bytecode.GetAddressOf(), errors.GetAddressOf());
        if (FAILED(compiled))
        {
            if (errors != nullptr)
                compileError_.assign(
                    static_cast<const char*>(errors->GetBufferPointer()),
                    errors->GetBufferSize());
            else
            {
                char code[32];
                std::snprintf(code, sizeof(code), "0x%08lX",
                              static_cast<unsigned long>(compiled));
                compileError_ = std::string("D3DCompile (compute) failed: ") + code;
            }
            return false;
        }
        if (!reflection_.AddShader(
                bytecode->GetBufferPointer(), bytecode->GetBufferSize(), compileError_, true))
            return false;
        const HRESULT created = device_->CreateComputeShader(
            bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr,
            shader_.GetAddressOf());
        if (FAILED(created))
        {
            char code[32];
            std::snprintf(code, sizeof(code), "0x%08lX",
                          static_cast<unsigned long>(created));
            compileError_ = std::string("CreateComputeShader failed: ") + code;
            return false;
        }

        for (int slot = 0; slot < reflection_.GetConstantBufferCount(); ++slot)
        {
            const auto& reflected = reflection_.GetConstantBuffer(slot);
            if (!reflected.present) continue;
            D3D11_BUFFER_DESC description{};
            description.ByteWidth = static_cast<UINT>(
                (reflected.data.size() + 15u) & ~std::size_t{15});
            description.Usage = D3D11_USAGE_DYNAMIC;
            description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(device_->CreateBuffer(
                    &description, nullptr,
                    uniformBuffers_[static_cast<std::size_t>(slot)].GetAddressOf())))
            {
                shader_.Reset();
                compileError_ = "D3D11 compute uniform-buffer creation failed";
                return false;
            }
        }
        return true;
    }

    void D3D11ComputeShader::Bind()
    {
        context_->CSSetShader(shader_.Get(), nullptr, 0);
    }

    void D3D11ComputeShader::SetUniformInt(const char* name, int value)
    {
        reflection_.SetInt(name, value);
    }

    void D3D11ComputeShader::SetUniformFloat(const char* name, float value)
    {
        reflection_.SetFloat(name, value);
    }

    void D3D11ComputeShader::BindStorageBuffer(
        int binding, IStorageBufferRenderer* buffer)
    {
        if (binding < 0 || binding >= static_cast<int>(kUavSlots))
            throw std::out_of_range("D3D11 compute storage binding is out of range");
        auto& retained = storageBuffers_[static_cast<std::size_t>(binding)];
        if (buffer == nullptr)
        {
            retained.reset();
            return;
        }
        const auto* native = dynamic_cast<D3D11IndirectBuffer*>(buffer);
        if (native == nullptr || native->GetDeviceEXT() != device_.Get() ||
            native->GetUnorderedAccessViewEXT() == nullptr)
            throw std::invalid_argument("D3D11 compute storage buffer is not native to this device");
        retained = buffer->shared_from_this();
        storageTextures_[static_cast<std::size_t>(binding)].reset();
    }

    bool D3D11ComputeShader::BindStorageTexture2DEXT(
        int unit, std::shared_ptr<IStorageTexture2DRenderer> texture, int accessMode)
    {
        if (unit < 0 || unit >= static_cast<int>(kUavSlots) ||
            accessMode < 0 || accessMode > 2)
            return false;
        auto& retained = storageTextures_[static_cast<std::size_t>(unit)];
        if (!texture)
        {
            retained.reset();
            return true;
        }
        auto* native = dynamic_cast<D3D11StorageTexture2D*>(texture.get());
        const std::uint32_t required = accessMode == 0 ? UINT32_C(0x01) :
            accessMode == 1 ? UINT32_C(0x02) : UINT32_C(0x03);
        if (!native || native->GetDeviceEXT() != device_.Get() ||
            !native->GetUnorderedAccessViewEXT() ||
            (native->GetUsageEXT() & required) != required)
            return false;
        retained = std::move(texture);
        storageBuffers_[static_cast<std::size_t>(unit)].reset();
        return true;
    }

    bool D3D11ComputeShader::BindConstantBufferEXT(
        int binding, IStorageBufferRenderer* buffer)
    {
        if (binding < 0 || binding >= static_cast<int>(kConstantSlots)) return false;
        auto& retained = constantBuffers_[static_cast<std::size_t>(binding)];
        if (buffer == nullptr)
        {
            retained.reset();
            return true;
        }
        const auto* native = dynamic_cast<D3D11IndirectBuffer*>(buffer);
        if (native == nullptr || native->GetDeviceEXT() != device_.Get() ||
            native->GetConstantBufferEXT() == nullptr)
            return false;
        retained = buffer->shared_from_this();
        return true;
    }

    void D3D11ComputeShader::BindTexture(int unit, ITextureRenderer* texture)
    {
        if (unit < 0 || unit >= static_cast<int>(kTextureSlots))
            throw std::out_of_range("D3D11 compute sampled-texture binding is out of range");
        auto& retained = textures_[static_cast<std::size_t>(unit)];
        if (texture == nullptr)
        {
            retained.reset();
            return;
        }
        if (dynamic_cast<D3D11TextureRenderer*>(texture) == nullptr &&
            dynamic_cast<D3D11RenderTargetRenderer*>(texture) == nullptr)
            throw std::invalid_argument("D3D11 compute sampled texture is not native");
        retained = texture->shared_from_this();
    }

    void D3D11ComputeShader::Dispatch(int groupsX, int groupsY, int groupsZ)
    {
        if (!IsValid())
            throw std::runtime_error("D3D11 compute program is not valid");

        std::array<ID3D11Buffer*, kConstantSlots> nativeConstants{};
        for (std::size_t slot = 0; slot < kConstantSlots; ++slot)
        {
            if (constantBuffers_[slot])
            {
                auto* buffer = static_cast<D3D11IndirectBuffer*>(constantBuffers_[slot].get());
                nativeConstants[slot] = buffer->GetConstantBufferEXT();
                continue;
            }
            const auto& reflected = reflection_.GetConstantBuffer(static_cast<int>(slot));
            ID3D11Buffer* buffer = uniformBuffers_[slot].Get();
            nativeConstants[slot] = buffer;
            if (!reflected.present || buffer == nullptr) continue;
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(context_->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
                throw std::runtime_error("D3D11 compute uniform-buffer upload failed");
            std::memcpy(mapped.pData, reflected.data.data(), reflected.data.size());
            context_->Unmap(buffer, 0);
        }

        std::array<ID3D11UnorderedAccessView*, kUavSlots> nativeUavs{};
        for (std::size_t slot = 0; slot < kUavSlots; ++slot)
        {
            if (storageBuffers_[slot])
                nativeUavs[slot] = static_cast<D3D11IndirectBuffer*>(
                    storageBuffers_[slot].get())->GetUnorderedAccessViewEXT();
            else if (storageTextures_[slot])
                nativeUavs[slot] = static_cast<D3D11StorageTexture2D*>(
                    storageTextures_[slot].get())->GetUnorderedAccessViewEXT();
        }

        std::array<ID3D11ShaderResourceView*, kTextureSlots> nativeTextures{};
        std::array<ID3D11SamplerState*, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT> samplers{};
        for (std::size_t slot = 0; slot < kTextureSlots; ++slot)
        {
            if (auto* texture = dynamic_cast<D3D11TextureRenderer*>(textures_[slot].get()))
                nativeTextures[slot] = texture->GetShaderResourceViewEXT();
            else if (auto* target = dynamic_cast<D3D11RenderTargetRenderer*>(textures_[slot].get()))
                nativeTextures[slot] = target->GetShaderResourceViewEXT();
            if (nativeTextures[slot] != nullptr && slot < samplers.size())
                samplers[slot] = sampler_.Get();
        }

        std::array<ID3D11ShaderResourceView*, kTextureSlots> previousPixelResources{};
        std::array<ID3D11ShaderResourceView*, kTextureSlots> emptyPixelResources{};
        std::array<ID3D11ShaderResourceView*, kTextureSlots> previousVertexResources{};
        std::array<ID3D11ShaderResourceView*, kTextureSlots> emptyVertexResources{};
        if (std::any_of(nativeUavs.begin(), nativeUavs.end(),
                        [](const auto* view) { return view != nullptr; }))
        {
            // D3D11 otherwise auto-unbinds a graphics SRV that aliases a compute UAV.
            // Restore the prior graphics inputs after the dispatch so classic draws keep state.
            context_->PSGetShaderResources(0, static_cast<UINT>(kTextureSlots),
                                           previousPixelResources.data());
            context_->PSSetShaderResources(0, static_cast<UINT>(kTextureSlots),
                                           emptyPixelResources.data());
            context_->VSGetShaderResources(0, static_cast<UINT>(kTextureSlots),
                                           previousVertexResources.data());
            context_->VSSetShaderResources(0, static_cast<UINT>(kTextureSlots),
                                           emptyVertexResources.data());
        }

        Bind();
        context_->CSSetConstantBuffers(0, static_cast<UINT>(nativeConstants.size()),
                                       nativeConstants.data());
        context_->CSSetShaderResources(0, static_cast<UINT>(nativeTextures.size()),
                                       nativeTextures.data());
        context_->CSSetSamplers(0, static_cast<UINT>(samplers.size()), samplers.data());
        context_->CSSetUnorderedAccessViews(0, static_cast<UINT>(nativeUavs.size()),
                                            nativeUavs.data(), nullptr);
        context_->Dispatch(static_cast<UINT>(groupsX), static_cast<UINT>(groupsY),
                           static_cast<UINT>(groupsZ));

        nativeUavs.fill(nullptr);
        nativeTextures.fill(nullptr);
        nativeConstants.fill(nullptr);
        samplers.fill(nullptr);
        context_->CSSetUnorderedAccessViews(0, static_cast<UINT>(nativeUavs.size()),
                                            nativeUavs.data(), nullptr);
        context_->CSSetShaderResources(0, static_cast<UINT>(nativeTextures.size()),
                                       nativeTextures.data());
        context_->CSSetConstantBuffers(0, static_cast<UINT>(nativeConstants.size()),
                                       nativeConstants.data());
        context_->CSSetSamplers(0, static_cast<UINT>(samplers.size()), samplers.data());
        context_->CSSetShader(nullptr, nullptr, 0);
        if (std::any_of(previousPixelResources.begin(), previousPixelResources.end(),
                        [](const auto* view) { return view != nullptr; }))
        {
            context_->PSSetShaderResources(0, static_cast<UINT>(kTextureSlots),
                                           previousPixelResources.data());
        }
        if (std::any_of(previousVertexResources.begin(), previousVertexResources.end(),
                        [](const auto* view) { return view != nullptr; }))
        {
            context_->VSSetShaderResources(0, static_cast<UINT>(kTextureSlots),
                                           previousVertexResources.data());
        }
        for (auto* view : previousPixelResources)
            if (view) view->Release();
        for (auto* view : previousVertexResources)
            if (view) view->Release();
    }
}

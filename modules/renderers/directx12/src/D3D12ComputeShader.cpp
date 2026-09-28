// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/DirectX12/D3D12ComputeShader.hpp"

#include "CNA/Internal/Renderers/DirectX12/D3D12RenderTargets.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12StorageBuffer.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12StorageTexture2D.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12Textures.hpp"
#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"

#include <d3dcompiler.h>

#include <array>
#include <cstdio>
#include <stdexcept>

namespace CNA::Internal::Renderers::DirectX12
{
    namespace
    {
        std::string FormatHr(HRESULT hr)
        {
            char code[32];
            std::snprintf(code, sizeof(code), "0x%08lX", static_cast<unsigned long>(hr));
            return code;
        }

        bool BelongsToDevice(ID3D12Resource* resource, ID3D12Device* expected)
        {
            if (!resource || !expected) return false;
            Microsoft::WRL::ComPtr<ID3D12Device> actual;
            return SUCCEEDED(resource->GetDevice(IID_PPV_ARGS(actual.GetAddressOf()))) &&
                   actual.Get() == expected;
        }

        constexpr D3D12_RESOURCE_STATES kReadableTexture =
            static_cast<D3D12_RESOURCE_STATES>(
                static_cast<int>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) |
                static_cast<int>(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    }

    D3D12ComputeShader::D3D12ComputeShader(DirectX12Renderer* renderer)
        : renderer_(renderer, renderer ? renderer->GetLifetimeTokenEXT() : std::weak_ptr<void>{},
                    "D3D12ComputeShader"),
          device_(renderer ? renderer->GetDeviceEXT() : nullptr)
    {
        if (!renderer || !device_)
            throw std::invalid_argument("D3D12 compute shader requires a live device");
        renderer_->RegisterRecoverableResourceEXT(this);
    }

    D3D12ComputeShader::~D3D12ComputeShader()
    {
        ReleaseDeviceResourcesEXT();
        if (renderer_)
            renderer_->UnregisterRecoverableResourceEXT(this);
    }

    bool D3D12ComputeShader::CreateRootSignature()
    {
        constexpr int kRootCount = kConstantSlots + kTextureSlots + kStorageSlots;
        std::array<D3D12_ROOT_PARAMETER1, kRootCount> parameters{};
        std::array<D3D12_DESCRIPTOR_RANGE1, kTextureSlots + kStorageSlots> ranges{};
        std::array<D3D12_STATIC_SAMPLER_DESC, kTextureSlots> samplers{};

        for (int slot = 0; slot < kConstantSlots; ++slot)
        {
            auto& parameter = parameters[static_cast<std::size_t>(slot)];
            parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            parameter.Descriptor.ShaderRegister = static_cast<UINT>(slot);
            parameter.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
            parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        }
        for (int slot = 0; slot < kTextureSlots + kStorageSlots; ++slot)
        {
            auto& range = ranges[static_cast<std::size_t>(slot)];
            const bool sampled = slot < kTextureSlots;
            range.RangeType = sampled ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV
                                      : D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            range.NumDescriptors = 1;
            range.BaseShaderRegister = static_cast<UINT>(
                sampled ? slot : slot - kTextureSlots);
            range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
            range.OffsetInDescriptorsFromTableStart = 0;
            auto& parameter = parameters[static_cast<std::size_t>(kConstantSlots + slot)];
            parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameter.DescriptorTable.NumDescriptorRanges = 1;
            parameter.DescriptorTable.pDescriptorRanges = &range;
            parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        }
        for (int slot = 0; slot < kTextureSlots; ++slot)
        {
            auto& sampler = samplers[static_cast<std::size_t>(slot)];
            sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.MipLODBias = 0;
            sampler.MaxAnisotropy = 1;
            sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
            sampler.MinLOD = 0;
            sampler.MaxLOD = D3D12_FLOAT32_MAX;
            sampler.ShaderRegister = static_cast<UINT>(slot);
            sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        }

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC description{};
        description.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        description.Desc_1_1.NumParameters = static_cast<UINT>(parameters.size());
        description.Desc_1_1.pParameters = parameters.data();
        description.Desc_1_1.NumStaticSamplers = static_cast<UINT>(samplers.size());
        description.Desc_1_1.pStaticSamplers = samplers.data();
        description.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        Microsoft::WRL::ComPtr<ID3DBlob> serialized;
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        HRESULT hr = D3D12SerializeVersionedRootSignature(
            &description, serialized.GetAddressOf(), errors.GetAddressOf());
        if (FAILED(hr))
        {
            compileError_ = errors
                ? std::string(static_cast<const char*>(errors->GetBufferPointer()),
                              errors->GetBufferSize())
                : "D3D12 compute root-signature serialization failed: " + FormatHr(hr);
            return false;
        }
        hr = device_->CreateRootSignature(
            0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
            IID_PPV_ARGS(rootSignature_.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
        {
            compileError_ = "D3D12 compute root-signature creation failed: " + FormatHr(hr);
            return false;
        }
        return true;
    }

    bool D3D12ComputeShader::CreatePipelineState()
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC description{};
        description.pRootSignature = rootSignature_.Get();
        description.CS.pShaderBytecode = bytecode_->GetBufferPointer();
        description.CS.BytecodeLength = bytecode_->GetBufferSize();
        const HRESULT hr = device_->CreateComputePipelineState(
            &description, IID_PPV_ARGS(pso_.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
        {
            compileError_ = "D3D12 compute pipeline creation failed: " + FormatHr(hr);
            return false;
        }
        return true;
    }

    bool D3D12ComputeShader::CompileProgram(const std::string& source)
    {
        pso_.Reset();
        rootSignature_.Reset();
        bytecode_.Reset();
        reflection_.Reset();
        storageBuffers_ = {};
        storageTextures_ = {};
        imageTextures_ = {};
        storageTextureAccess_ = {};
        constantBuffers_ = {};
        textures_ = {};
        compileError_.clear();

        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        HRESULT hr = D3DCompile(
            source.data(), source.size(), "ComputeShader_cs", nullptr, nullptr,
            "main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS |
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
            bytecode_.GetAddressOf(), errors.GetAddressOf());
        if (FAILED(hr))
        {
            compileError_ = errors
                ? std::string(static_cast<const char*>(errors->GetBufferPointer()),
                              errors->GetBufferSize())
                : "D3DCompile (compute) failed: " + FormatHr(hr);
            bytecode_.Reset();
            return false;
        }
        if (!reflection_.AddShader(bytecode_->GetBufferPointer(),
                                   bytecode_->GetBufferSize(), compileError_, true))
        {
            bytecode_.Reset();
            return false;
        }
        if (!CreateRootSignature() || !CreatePipelineState())
        {
            bytecode_.Reset();
            rootSignature_.Reset();
            return false;
        }
        return true;
    }

    void D3D12ComputeShader::ReleaseDeviceResourcesEXT() noexcept
    {
        pso_.Reset();
        rootSignature_.Reset();
        device_.Reset();
    }

    void D3D12ComputeShader::RecreateDeviceResourcesEXT()
    {
        device_ = renderer_->GetDeviceEXT();
        if (!bytecode_) return;
        compileError_.clear();
        if (!CreateRootSignature() || !CreatePipelineState())
            throw std::runtime_error("D3D12 compute recovery failed: " + compileError_);
    }

    void D3D12ComputeShader::Bind() { (void)renderer_.Get(); }

    void D3D12ComputeShader::SetUniformInt(const char* name, int value)
    {
        reflection_.SetInt(name, value);
    }

    void D3D12ComputeShader::SetUniformFloat(const char* name, float value)
    {
        reflection_.SetFloat(name, value);
    }

    void D3D12ComputeShader::BindStorageBuffer(
        int binding, IStorageBufferRenderer* buffer)
    {
        if (binding < 0 || binding >= kStorageSlots)
            throw std::out_of_range("D3D12 compute storage binding is out of range");
        auto& held = storageBuffers_[static_cast<std::size_t>(binding)];
        if (!buffer) { held.reset(); return; }
        auto* native = dynamic_cast<D3D12StorageBuffer*>(buffer);
        if (!native || native->GetOwnerEXT() != renderer_.Get() ||
            (native->GetUsageEXT() & UINT32_C(0x01)) == 0 ||
            native->GetUavIndexEXT() == D3D12ShaderVisibleDescriptorAllocator::kInvalidIndex)
            throw std::invalid_argument(
                "D3D12 compute requires a same-device storage UAV buffer");
        held = buffer->shared_from_this();
        storageTextures_[static_cast<std::size_t>(binding)].reset();
        imageTextures_[static_cast<std::size_t>(binding)].reset();
    }

    bool D3D12ComputeShader::BindStorageTexture2DEXT(
        int unit, std::shared_ptr<IStorageTexture2DRenderer> texture, int accessMode)
    {
        if (unit < 0 || unit >= kStorageSlots || accessMode < 0 || accessMode > 2)
            return false;
        auto& held = storageTextures_[static_cast<std::size_t>(unit)];
        if (!texture)
        {
            held.reset();
            return true;
        }
        auto* native = dynamic_cast<D3D12StorageTexture2D*>(texture.get());
        const std::uint32_t required = accessMode == 0 ? UINT32_C(0x01) :
            accessMode == 1 ? UINT32_C(0x02) : UINT32_C(0x03);
        if (!native || native->GetOwnerEXT() != renderer_.Get() ||
            (native->GetUsageEXT() & required) != required ||
            native->GetUavGpuHandleEXT().ptr == 0)
            return false;
        held = std::move(texture);
        storageTextureAccess_[static_cast<std::size_t>(unit)] = accessMode;
        storageBuffers_[static_cast<std::size_t>(unit)].reset();
        imageTextures_[static_cast<std::size_t>(unit)].reset();
        return true;
    }

    void D3D12ComputeShader::BindImageTexture(
        int unit, ITextureRenderer* texture, int accessMode)
    {
        if (unit < 0 || unit >= kStorageSlots || accessMode < 0 || accessMode > 2)
            throw std::invalid_argument("D3D12 compute image binding has an invalid slot or access");
        auto& held = imageTextures_[static_cast<std::size_t>(unit)];
        if (!texture) { held.reset(); return; }
        auto* native = dynamic_cast<D3D12TextureRenderer*>(texture);
        constexpr std::uint32_t read = static_cast<std::uint32_t>(
            CNA::RendererFormatUsage::StorageRead);
        constexpr std::uint32_t write = static_cast<std::uint32_t>(
            CNA::RendererFormatUsage::StorageWrite);
        const std::uint32_t required = accessMode == 0 ? read :
            accessMode == 1 ? write : read | write;
        if (!native || !BelongsToDevice(native->GetResourceEXT(), device_.Get()) ||
            native->GetUnorderedAccessViewGpuHandleEXT().ptr == 0 ||
            (native->GetImageAccessEXT() & required) != required)
            throw std::invalid_argument(
                "D3D12 compute image binding requires a same-device Texture2D with typed UAV access");
        held = texture->shared_from_this();
        storageBuffers_[static_cast<std::size_t>(unit)].reset();
        storageTextures_[static_cast<std::size_t>(unit)].reset();
    }

    bool D3D12ComputeShader::BindConstantBufferEXT(
        int binding, IStorageBufferRenderer* buffer)
    {
        if (binding < 0 || binding >= kConstantSlots) return false;
        auto& held = constantBuffers_[static_cast<std::size_t>(binding)];
        if (!buffer) { held.reset(); return true; }
        auto* native = dynamic_cast<D3D12StorageBuffer*>(buffer);
        if (!native || native->GetOwnerEXT() != renderer_.Get() ||
            (native->GetUsageEXT() & UINT32_C(0x40)) == 0)
            return false;
        held = buffer->shared_from_this();
        return true;
    }

    void D3D12ComputeShader::BindTexture(int unit, ITextureRenderer* texture)
    {
        if (unit < 0 || unit >= kTextureSlots)
            throw std::out_of_range("D3D12 compute sampled-texture binding is out of range");
        auto& held = textures_[static_cast<std::size_t>(unit)];
        if (!texture) { held.reset(); return; }
        ID3D12Resource* resource = nullptr;
        if (auto* native = dynamic_cast<D3D12TextureRenderer*>(texture))
            resource = native->GetResourceEXT();
        else if (auto* target = dynamic_cast<D3D12RenderTargetRenderer*>(texture))
            resource = target->GetSampleableColorResourceEXT();
        if (!BelongsToDevice(resource, device_.Get()))
            throw std::invalid_argument(
                "D3D12 compute sampled texture is not native to this device");
        held = texture->shared_from_this();
    }

    void D3D12ComputeShader::Dispatch(int groupsX, int groupsY, int groupsZ)
    {
        if (!IsValid())
            throw std::runtime_error("D3D12 compute program is not valid");
        for (std::size_t slot = 0; slot < storageBuffers_.size(); ++slot)
        {
            const auto* resource = storageBuffers_[slot].get();
            if (!resource) continue;
            for (std::size_t other = slot + 1; other < storageBuffers_.size(); ++other)
                if (storageBuffers_[other].get() == resource)
                    throw std::invalid_argument(
                        "D3D12 compute cannot bind one buffer to multiple UAV slots");
            for (const auto& constant : constantBuffers_)
                if (constant.get() == resource)
                    throw std::invalid_argument(
                        "D3D12 compute cannot bind one buffer as both UAV and CBV");
        }
        for (std::size_t slot = 0; slot < storageTextures_.size(); ++slot)
        {
            const auto* resource = storageTextures_[slot].get();
            if (!resource) continue;
            for (std::size_t other = slot + 1; other < storageTextures_.size(); ++other)
                if (storageTextures_[other].get() == resource)
                    throw std::invalid_argument(
                        "D3D12 compute cannot bind one storage texture to multiple UAV slots");
        }
        for (std::size_t slot = 0; slot < imageTextures_.size(); ++slot)
        {
            const auto* resource = imageTextures_[slot].get();
            if (!resource) continue;
            for (std::size_t other = slot + 1; other < imageTextures_.size(); ++other)
                if (imageTextures_[other].get() == resource)
                    throw std::invalid_argument(
                        "D3D12 compute cannot bind one Texture2D to multiple UAV slots");
            for (const auto& sampled : textures_)
                if (sampled.get() == resource)
                    throw std::invalid_argument(
                        "D3D12 compute cannot bind one Texture2D as sampled input and image");
        }
        auto* owner = renderer_.Get();
        ID3D12GraphicsCommandList* commands = owner->GetFrameCommandListEXT();
        auto& states = owner->GetResourceStateTrackerEXT();
        owner->RetainFrameObjectEXT(rootSignature_.Get());
        owner->RetainFrameObjectEXT(pso_.Get());
        commands->SetComputeRootSignature(rootSignature_.Get());
        commands->SetPipelineState(pso_.Get());
        ID3D12DescriptorHeap* heap = owner->GetCbvSrvUavHeapEXT();
        commands->SetDescriptorHeaps(1, &heap);

        for (int slot = 0; slot < kConstantSlots; ++slot)
        {
            D3D12_GPU_VIRTUAL_ADDRESS address = 0;
            if (constantBuffers_[static_cast<std::size_t>(slot)])
            {
                auto* native = static_cast<D3D12StorageBuffer*>(
                    constantBuffers_[static_cast<std::size_t>(slot)].get());
                ID3D12Resource* resource = native->GetResourceEXT();
                owner->RetainFrameObjectEXT(resource);
                states.TransitionTo(commands, resource,
                                    D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
                address = resource->GetGPUVirtualAddress();
            }
            else
            {
                const auto& reflected = reflection_.GetConstantBuffer(slot);
                if (reflected.present)
                    address = owner->AllocateFrameConstantDataEXT(
                        reflected.data.data(), reflected.data.size());
            }
            if (address != 0)
                commands->SetComputeRootConstantBufferView(static_cast<UINT>(slot), address);
        }

        for (int slot = 0; slot < kTextureSlots; ++slot)
        {
            const auto& held = textures_[static_cast<std::size_t>(slot)];
            if (!held) continue;
            ID3D12Resource* resource = nullptr;
            D3D12_GPU_DESCRIPTOR_HANDLE handle{};
            if (auto* texture = dynamic_cast<D3D12TextureRenderer*>(held.get()))
            {
                resource = texture->GetResourceEXT();
                handle = texture->GetShaderResourceViewGpuHandleEXT();
            }
            else if (auto* target = dynamic_cast<D3D12RenderTargetRenderer*>(held.get()))
            {
                resource = target->GetSampleableColorResourceEXT();
                handle = target->GetShaderResourceViewGpuHandleEXT();
            }
            if (!resource)
                throw std::runtime_error("D3D12 compute sampled texture was released");
            owner->RetainFrameObjectEXT(resource);
            states.TransitionTo(commands, resource, kReadableTexture);
            commands->SetComputeRootDescriptorTable(
                static_cast<UINT>(kConstantSlots + slot), handle);
        }

        for (int slot = 0; slot < kStorageSlots; ++slot)
        {
            const std::size_t index = static_cast<std::size_t>(slot);
            D3D12_GPU_DESCRIPTOR_HANDLE handle{};
            ID3D12Resource* resource = nullptr;
            if (const auto& held = storageBuffers_[index])
            {
                auto* native = static_cast<D3D12StorageBuffer*>(held.get());
                resource = native->GetResourceEXT();
                handle = owner->GetCbvSrvUavGpuHandleEXT(native->GetUavIndexEXT());
            }
            else if (const auto& held = storageTextures_[index])
            {
                auto* native = static_cast<D3D12StorageTexture2D*>(held.get());
                resource = native->GetResourceEXT();
                handle = native->GetUavGpuHandleEXT();
            }
            else if (const auto& held = imageTextures_[index])
            {
                auto* native = static_cast<D3D12TextureRenderer*>(held.get());
                resource = native->GetResourceEXT();
                handle = native->GetUnorderedAccessViewGpuHandleEXT();
            }
            if (!resource) continue;
            owner->RetainFrameObjectEXT(resource);
            states.TransitionTo(commands, resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            commands->SetComputeRootDescriptorTable(
                static_cast<UINT>(kConstantSlots + kTextureSlots + slot), handle);
        }

        commands->Dispatch(static_cast<UINT>(groupsX), static_cast<UINT>(groupsY),
                           static_cast<UINT>(groupsZ));
        for (const auto& held : storageBuffers_)
        {
            if (!held) continue;
            auto* native = static_cast<D3D12StorageBuffer*>(held.get());
            native->InvalidateRecoveryShadowEXT();
            states.TransitionTo(commands, native->GetResourceEXT(),
                                D3D12_RESOURCE_STATE_GENERIC_READ);
        }
        for (std::size_t slot = 0; slot < storageTextures_.size(); ++slot)
        {
            const auto& held = storageTextures_[slot];
            if (!held) continue;
            auto* native = static_cast<D3D12StorageTexture2D*>(held.get());
            if (storageTextureAccess_[slot] != 0)
                native->InvalidateRecoveryShadowEXT();
            states.TransitionTo(commands, native->GetResourceEXT(), kReadableTexture);
        }
        for (const auto& held : imageTextures_)
        {
            if (!held) continue;
            auto* native = static_cast<D3D12TextureRenderer*>(held.get());
            states.TransitionTo(commands, native->GetResourceEXT(), kReadableTexture);
        }
    }
}

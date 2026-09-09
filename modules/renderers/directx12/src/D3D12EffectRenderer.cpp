// plans/plan_dx.md Phase DX13 (DX-121).
#include "CNA/Internal/Renderers/DirectX12/D3D12EffectRenderer.hpp"
#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12RenderTargets.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12TextureCube.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12Textures.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12Texture3D.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace CNA::Internal::Renderers::DirectX12
{
    namespace
    {
        std::atomic<std::uint64_t> nextProgramId{1};

        std::string FormatHr(HRESULT hr)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
            return buf;
        }
    }

    D3D12EffectRenderer::D3D12EffectRenderer(DirectX12Renderer* owner)
        : owner_(owner, owner ? owner->GetLifetimeTokenEXT() : std::weak_ptr<void>{},
                 "D3D12EffectRenderer"),
          device_(owner_->GetDeviceEXT())
    {
    }

    bool D3D12EffectRenderer::CompileProgram(const std::string& vertSrc, const std::string& fragSrc)
    {
        (void) owner_.Get();
        compileError_.clear();
        valid_ = false;
        pso_.Reset();
        rootSignature_.Reset();
        vsBytecode_.Reset();
        psBytecode_.Reset();
        reflection_.Reset();
        textures_ = {};
        programId_ = 0;

        const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;

        ComPtr<ID3DBlob> vsErr;
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

        ComPtr<ID3DBlob> psErr;
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

        rootSignature_ = owner_->GetRootSignatureCacheEXT().GetOrCreate(
            device_, reflection_.GetConstantBufferCount(),
            reflection_.GetShaderResourceCount(), reflection_.GetSamplerCount());
        if (!rootSignature_)
        {
            compileError_ = "D3D12EffectRenderer: failed to create the reflected root signature";
            return false;
        }

        programId_ = nextProgramId.fetch_add(1, std::memory_order_relaxed);
        valid_ = true;
        return true;
    }

    void D3D12EffectRenderer::Bind()
    {
        (void) owner_.Get();
        // Uniforms stay in reflection_. Each consuming draw snapshots them into its own frame range.
    }

    void D3D12EffectRenderer::Unbind()
    {
        // Root state belongs to each command-list recording. The next draw binds its own PSO and
        // reflected root parameters, so there is no persistent context state to restore here.
    }

    void D3D12EffectRenderer::SetUniformMat4(const char* name, const float* matrix)
    {
        reflection_.SetMat4(name, matrix);
    }

    void D3D12EffectRenderer::SetUniformVec4(const char* name, float x, float y, float z, float w)
    {
        reflection_.SetVec4(name, x, y, z, w);
    }

    void D3D12EffectRenderer::SetUniformVec3(const char* name, float x, float y, float z)
    {
        reflection_.SetVec3(name, x, y, z);
    }

    void D3D12EffectRenderer::SetUniformVec2(const char* name, float x, float y)
    {
        reflection_.SetVec2(name, x, y);
    }

    void D3D12EffectRenderer::SetUniformFloat(const char* name, float value)
    {
        reflection_.SetFloat(name, value);
    }

    void D3D12EffectRenderer::SetUniformInt(const char* name, int value)
    {
        reflection_.SetInt(name, value);
    }

    void D3D12EffectRenderer::SetUniformFloatArray(
        const char* name, const float* values, const int count)
    {
        reflection_.SetFloatArray(name, values, count);
    }

    void D3D12EffectRenderer::SetUniformVec2Array(
        const char* name, const float* values, const int count)
    {
        reflection_.SetVec2Array(name, values, count);
    }

    void D3D12EffectRenderer::SetUniformVec3Array(
        const char* name, const float* values, const int count)
    {
        reflection_.SetVec3Array(name, values, count);
    }

    void D3D12EffectRenderer::SetUniformMat4Array(
        const char* name, const float* matrices, const int count)
    {
        reflection_.SetMat4Array(name, matrices, count);
    }

    void D3D12EffectRenderer::BindTexture(const int unit, ITextureRenderer* texture)
    {
        if (unit < 0 || unit >= static_cast<int>(textures_.size()))
            return;
        textures_[static_cast<std::size_t>(unit)] =
            {TextureKind::Texture2D, texture, true};
    }

    void D3D12EffectRenderer::BindTextureCube(const int unit, ITextureCubeRenderer* texture)
    {
        if (unit < 0 || unit >= static_cast<int>(textures_.size()))
            return;
        textures_[static_cast<std::size_t>(unit)] =
            {TextureKind::TextureCube, texture, true};
    }

    void D3D12EffectRenderer::BindTexture3D(int unit, ITexture3DRenderer* texture)
    {
        if (unit < 0 || unit >= static_cast<int>(textures_.size()))
            return;
        textures_[static_cast<std::size_t>(unit)] =
            {TextureKind::Texture3D, texture, true};
    }

    ID3D12PipelineState* D3D12EffectRenderer::GetOrCreatePipelineStateEXT(
        D3D12PipelineStateDesc desc)
    {
        (void) owner_.Get();
        if (!valid_ || !rootSignature_ || !vsBytecode_ || !psBytecode_)
            return nullptr;
        desc.customProgramId = programId_;
        desc.customVertexShaderBytecode = vsBytecode_->GetBufferPointer();
        desc.customVertexShaderBytecodeSize = vsBytecode_->GetBufferSize();
        desc.customPixelShaderBytecode = psBytecode_->GetBufferPointer();
        desc.customPixelShaderBytecodeSize = psBytecode_->GetBufferSize();
        pso_ = owner_->psoCache_.GetOrCreate(device_, rootSignature_.Get(), desc);
        return pso_.Get();
    }

    D3D12_GPU_VIRTUAL_ADDRESS D3D12EffectRenderer::GetConstantBufferGpuAddressEXT(const int slot)
    {
        if (slot < 0 || slot >= reflection_.GetConstantBufferCount())
            return 0;
        const auto& reflected = reflection_.GetConstantBuffer(slot);
        if (!reflected.present || reflected.data.empty())
            return 0;
        return owner_->AllocateFrameConstantDataEXT(
            reflected.data.data(), reflected.data.size());
    }

    bool D3D12EffectRenderer::HasTextureBindingEXT(const int unit) const
    {
        return unit >= 0 && unit < static_cast<int>(textures_.size()) &&
               textures_[static_cast<std::size_t>(unit)].explicitlySet;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE D3D12EffectRenderer::GetTextureGpuHandleEXT(const int unit)
    {
        if (!HasTextureBindingEXT(unit) || !reflection_.HasShaderResource(unit))
            return {};
        const auto& binding = textures_[static_cast<std::size_t>(unit)];
        switch (binding.kind)
        {
            case TextureKind::Texture2D:
                if (const auto* texture = dynamic_cast<const D3D12TextureRenderer*>(
                        static_cast<ITextureRenderer*>(binding.texture)))
                {
                    owner_->RetainFrameObjectEXT(texture->GetResourceEXT());
                    return texture->GetShaderResourceViewGpuHandleEXT();
                }
                if (const auto* target = dynamic_cast<const D3D12RenderTargetRenderer*>(
                        static_cast<ITextureRenderer*>(binding.texture)))
                {
                    owner_->RetainFrameObjectEXT(target->GetSampleableColorResourceEXT());
                    return target->GetShaderResourceViewGpuHandleEXT();
                }
                break;
            case TextureKind::TextureCube:
                if (const auto* texture = dynamic_cast<const D3D12TextureCubeRenderer*>(
                        static_cast<ITextureCubeRenderer*>(binding.texture)))
                {
                    owner_->RetainFrameObjectEXT(texture->GetResourceEXT());
                    return texture->GetShaderResourceViewGpuHandleEXT();
                }
                if (const auto* target = dynamic_cast<const D3D12RenderTargetCubeRenderer*>(
                        static_cast<ITextureCubeRenderer*>(binding.texture)))
                {
                    owner_->RetainFrameObjectEXT(target->GetSampleableColorResourceEXT());
                    return target->GetShaderResourceViewGpuHandleEXT();
                }
                break;
            case TextureKind::Texture3D:
                if (const auto* texture = dynamic_cast<const D3D12Texture3DRenderer*>(
                        static_cast<ITexture3DRenderer*>(binding.texture)))
                {
                    owner_->RetainFrameObjectEXT(texture->GetResourceEXT());
                    return texture->GetShaderResourceViewGpuHandleEXT();
                }
                break;
            case TextureKind::None:
                break;
        }
        return {};
    }

    void D3D12EffectRenderer::SetViewportSizeEXT(float width, float height)
    {
        reflection_.SetVec2("vpSize", width, height);
    }
}

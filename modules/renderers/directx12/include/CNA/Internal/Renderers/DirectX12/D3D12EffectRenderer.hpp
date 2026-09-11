#pragma once

// plans/plan_dx.md Phase DX13 (DX-121): custom ShaderEffect -- runtime D3DCompile() of arbitrary HLSL
// DX-223 replaces the original fixed (1 CBV, 1 SRV, 1 sampler), fixed Sprite2DVertex PSO with
// shared D3D reflection. CompileProgram retains bytecode and reflected register layouts; PSOs are
// created lazily through DirectX12Renderer's complete state cache for the actual vertex layout,
// target formats, topology and sample count at draw time.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DProgramReflection.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12RendererReference.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12PipelineStateCache.hpp"

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <string>

namespace CNA::Internal::Renderers::DirectX12
{
    using Microsoft::WRL::ComPtr;

    class DirectX12Renderer;

    class D3D12EffectRenderer final : public IEffectRenderer
    {
    public:
        explicit D3D12EffectRenderer(DirectX12Renderer* owner);

        bool CompileProgram(const std::string& vertSrc, const std::string& fragSrc) override;
        /// Finalizes CPU-side reflected values. The consuming draw allocates immutable frame ranges.
        void Bind() override;
        void Unbind() override;
        [[nodiscard]] bool IsValid() const override { return valid_; }
        [[nodiscard]] std::string GetCompileError() const override { return compileError_; }

        void SetUniformMat4(const char* name, const float* matrix) override;
        void SetUniformVec4(const char* name, float x, float y, float z, float w) override;
        void SetUniformVec3(const char* name, float x, float y, float z) override;
        void SetUniformVec2(const char* name, float x, float y) override;
        void SetUniformFloat(const char* name, float value) override;
        void SetUniformInt(const char* name, int value) override;
        void SetUniformFloatArray(const char* name, const float* values, int count) override;
        void SetUniformVec2Array(const char* name, const float* values, int count) override;
        void SetUniformVec3Array(const char* name, const float* values, int count) override;
        void SetUniformMat4Array(const char* name, const float* matrices, int count) override;
        void BindTexture(int unit, ITextureRenderer* texture) override;
        void BindTextureCube(int unit, ITextureCubeRenderer* texture) override;
        void BindTexture3D(int unit, ITexture3DRenderer* texture) override;

        /** @brief Writes the reflected `vpSize` parameter used by the SpriteBatch convention. */
        void SetViewportSizeEXT(float width, float height);

        /** @brief Returns the most recently resolved custom PSO, if any. */
        [[nodiscard]] ID3D12PipelineState* GetPipelineStateEXT() const { return pso_.Get(); }
        /** @brief Resolves a custom PSO through the renderer's complete state cache. */
        [[nodiscard]] ID3D12PipelineState* GetOrCreatePipelineStateEXT(
            D3D12PipelineStateDesc desc);
        /** @brief Returns this program's reflected root signature. */
        [[nodiscard]] ID3D12RootSignature* GetRootSignatureEXT() const { return rootSignature_.Get(); }
        /** @brief Copies a reflected cbuffer into a frame-owned range and returns its GPU address. */
        [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS GetConstantBufferGpuAddressEXT(int slot = 0);
        /** @brief Returns one plus the highest reflected b-register. */
        [[nodiscard]] int GetConstantBufferCountEXT() const
        {
            return reflection_.GetConstantBufferCount();
        }
        /** @brief Returns one plus the highest reflected t-register. */
        [[nodiscard]] int GetShaderResourceCountEXT() const
        {
            return reflection_.GetShaderResourceCount();
        }
        /** @brief Returns one plus the highest reflected s-register. */
        [[nodiscard]] int GetSamplerCountEXT() const { return reflection_.GetSamplerCount(); }
        /** @brief Reports whether the caller explicitly bound a texture at @p unit. */
        [[nodiscard]] bool HasTextureBindingEXT(int unit) const;
        /** @brief Resolves the current texture binding at one reflected t-register. */
        [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetTextureGpuHandleEXT(int unit);

    private:
        enum class TextureKind
        {
            None,
            Texture2D,
            TextureCube,
            Texture3D,
        };

        struct TextureBinding
        {
            TextureKind kind = TextureKind::None;
            void* texture = nullptr;
            bool explicitlySet = false;
        };

        D3D12RendererReference owner_;
        ID3D12Device* device_;
        ComPtr<ID3D12PipelineState> pso_;
        ComPtr<ID3D12RootSignature> rootSignature_;
        ComPtr<ID3DBlob> vsBytecode_;
        ComPtr<ID3DBlob> psBytecode_;
        D3DCommon::D3DProgramReflection reflection_;
        std::array<TextureBinding,
                   D3DCommon::D3DProgramReflection::kMaxShaderResources> textures_{};
        std::uint64_t programId_ = 0;
        std::string compileError_;
        bool valid_ = false;
    };
}

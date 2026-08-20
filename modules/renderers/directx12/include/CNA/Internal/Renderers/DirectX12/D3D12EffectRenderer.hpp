#pragma once

// plans/plan_dx.md Phase DX13 (DX-121): custom ShaderEffect -- runtime D3DCompile() of arbitrary HLSL
// vertex+fragment source, mirroring D3D11EffectRenderer's (DX-58) exact fixed-slot convention and
// fixed Sprite2DVertex-shaped vertex contract (x,y|u,v|r,g,b,a, 32 bytes) -- this mechanism is a
// SpriteBatch-custom-shader facility, not a general arbitrary-vertex-format one.
//
// Real, D3D12-specific difference from D3D11: D3D12 bakes shader+input-layout+root-signature+
// blend/depth-stencil state into one indivisible ID3D12PipelineState, so CompileProgram() must
// build a real PSO up front (unlike D3D11's separate VSSetShader/PSSetShader calls). It reuses
// D3D12SpriteBatchRenderer's own (1,1,1) root-signature shape (CBV@b0, SRV table@t0, sampler
// table@s0) via D3D12RootSignatureCache -- the SAME cached object D3D12SpriteBatchRenderer's own
// stock sprite2d PSO already uses -- so a custom-compiled shader binds through the identical root
// signature FlushBatch() already sets. The PSO's RTV format is hardcoded to
// DXGI_FORMAT_R8G8B8A8_UNORM (matching every render target this whole renderer uses, DX-11-fmt's
// own "Color/RGBA8 almost everywhere" convention) rather than queried from the currently-bound
// target, since CompileProgram() may run before any render target is bound (XNA content loading
// typically compiles effects up front) -- an honest, documented simplification, not an oversight.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <d3d12.h>
#include <wrl/client.h>

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
        /// Updates the mapped constant buffer with the current uniform values -- D3D12 has no
        /// separate "bind shader to context" call the way D3D11 does (state lives in the PSO this
        /// class already built); D3D12SpriteBatchRenderer::FlushBatch() pulls
        /// GetPipelineStateEXT()/GetConstantBufferEXT() and binds them within its own single
        /// command-list recording, right after calling this.
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

        /// DX-121: mirrors D3D11EffectRenderer::SetViewportSizeEXT -- writes the [0..15]-byte vpSize
        /// slot this class's own fixed-slot convention reserves for it. CNAEXT.
        void SetViewportSizeEXT(float width, float height);

        /// CNAEXT: the real, compiled PSO -- D3D12SpriteBatchRenderer::FlushBatch() binds this in
        /// place of its own stock sprite2d PSO when a valid custom effect is active.
        [[nodiscard]] ID3D12PipelineState* GetPipelineStateEXT() const { return pso_.Get(); }
        /// CNAEXT: the real, mapped 128-byte constant buffer Bind() writes into.
        [[nodiscard]] ID3D12Resource* GetConstantBufferEXT() const { return constantBuffer_.Get(); }

    private:
        DirectX12Renderer* owner_;
        ID3D12Device* device_;
        ComPtr<ID3D12PipelineState> pso_;
        ComPtr<ID3D12Resource> constantBuffer_;
        void* constantBufferMapped_ = nullptr;
        float pushConst_[32] = {};
        std::string compileError_;
        bool valid_ = false;
    };
}

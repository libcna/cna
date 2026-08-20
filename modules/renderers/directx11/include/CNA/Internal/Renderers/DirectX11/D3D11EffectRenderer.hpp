#pragma once

// plans/plan_dx.md Phase DIRECTX8 (DX-58): custom ShaderEffect -- runtime D3DCompile() of arbitrary HLSL
// vertex+fragment source, separate from the offline stock-shader pipeline (design decision 5).
//
// Mirrors VulkanEffectRenderer's own established contract as closely as D3D11's model allows (same
// fixed 128-byte uniform slot convention: [16..79]=mat4 matrix, [80..95]=vec4 color, [96..99]=
// float/int slot 0 -- SetUniform*'s `name` parameter is deliberately ignored, matching Vulkan's own
// `/*name*/`-discarding convention) and the same fixed Sprite2DVertex-shaped vertex contract
// (x,y|u,v|r,g,b,a, 32 bytes) this project's custom-ShaderEffect mechanism is built around (it's a
// SpriteBatch-custom-shader facility, per IGraphicsRenderer::ISpriteBatchRenderer::SetCustomEffect).
//
// Scope boundary (honest, not a placeholder): CompileProgram()/Bind()/Unbind()/IsValid()/
// GetCompileError()/the uniform setters are real and independently GPU-tested here (compile a
// trivial custom vertex+pixel shader pair at runtime, draw a real triangle with it, read back the
// exact pixel color driven by a uniform). Phase DX9 (DX-71) wires Bind()'s output into
// D3D11SpriteBatchRenderer's own per-sprite draw loop, and adds SetViewportSizeEXT() below to fill
// the [0..15]-byte vpSize slot this file's push-constant-contract comment always reserved for it.
// BindTexture() is not overridden (uses IEffectRenderer's own no-op default) -- texture unit 0 is
// bound by the caller (SpriteBatch) for both the stock and custom-effect paths, per
// IEffectRenderer::BindTexture()'s own doc comment.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <d3d11.h>
#include <wrl/client.h>

#include <string>

namespace CNA::Internal::Renderers::DirectX11
{
    using Microsoft::WRL::ComPtr;

    class D3D11EffectRenderer final : public IEffectRenderer
    {
    public:
        D3D11EffectRenderer(ID3D11Device* device, ID3D11DeviceContext* context);

        bool CompileProgram(const std::string& vertSrc, const std::string& fragSrc) override;
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

        /// DX-71 (Phase DX9): writes the [0..15]-byte vpSize slot this class's own header comment
        /// already reserved for it -- mirrors VulkanEffectRenderer's "set automatically by the
        /// sprite-batch runtime" convention (the game/effect author never calls this; SpriteBatch's
        /// renderer does, once per FlushBatch(), immediately before Bind()). CNAEXT.
        void SetViewportSizeEXT(float width, float height);

    private:
        ComPtr<ID3D11Device> device_;
        ComPtr<ID3D11DeviceContext> context_;
        ComPtr<ID3D11VertexShader> vs_;
        ComPtr<ID3D11PixelShader> ps_;
        ComPtr<ID3D11InputLayout> inputLayout_;
        ComPtr<ID3D11Buffer> constantBuffer_;
        float pushConst_[32] = {};
        std::string compileError_;
        bool valid_ = false;
    };
}

#pragma once

// plan_dx.md Phase DX8 (DX-58): custom ShaderEffect -- runtime D3DCompile() of arbitrary HLSL
// vertex+fragment source, separate from the offline stock-shader pipeline (design decision 5).
//
// Mirrors VulkanEffectBackend's own established contract as closely as D3D11's model allows (same
// fixed 128-byte uniform slot convention: [16..79]=mat4 matrix, [80..95]=vec4 color, [96..99]=
// float/int slot 0 -- SetUniform*'s `name` parameter is deliberately ignored, matching Vulkan's own
// `/*name*/`-discarding convention) and the same fixed Sprite2DVertex-shaped vertex contract
// (x,y|u,v|r,g,b,a, 32 bytes) this project's custom-ShaderEffect mechanism is built around (it's a
// SpriteBatch-custom-shader facility, per IGraphicsBackend::ISpriteBatchBackend::SetCustomEffect).
//
// Scope boundary (honest, not a placeholder): CompileProgram()/Bind()/Unbind()/IsValid()/
// GetCompileError()/the uniform setters are real and independently GPU-tested here (compile a
// trivial custom vertex+pixel shader pair at runtime, draw a real triangle with it, read back the
// exact pixel color driven by a uniform). Actually wiring Bind()'s output into SpriteBatch's own
// per-sprite draw loop is Phase DX9's job -- SpriteBatch doesn't exist yet for this backend
// (D3D11GraphicsBackend::CreateSpriteBatch() still throws). BindTexture() is not overridden (uses
// IEffectBackend's own no-op default) -- deliberately out of this task's scope.

#include "../Common/IGraphicsBackend.hpp"

#include <d3d11.h>
#include <wrl/client.h>

#include <string>

namespace CNA::Internal::Backends::D3D11
{
    using Microsoft::WRL::ComPtr;

    class D3D11EffectBackend final : public IEffectBackend
    {
    public:
        D3D11EffectBackend(ID3D11Device* device, ID3D11DeviceContext* context);

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

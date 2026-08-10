#include "CNA/Internal/Renderers/D3D9/D3D9EffectRenderer.hpp"

#include <d3dcompiler.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace CNA::Internal::Renderers::D3D9
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

    D3D9EffectRenderer::D3D9EffectRenderer(IDirect3DDevice9* device, bool hiDef)
        : device_(device), hiDef_(hiDef)
    {
    }

    bool D3D9EffectRenderer::CompileProgram(const std::string& vertSrc, const std::string& fragSrc)
    {
        compileError_.clear();
        valid_ = false;
        vs_.Reset();
        ps_.Reset();
        vsConstants_.clear();
        psConstants_.clear();

        const char* vsTarget = hiDef_ ? "vs_3_0" : "vs_2_0";
        const char* psTarget = hiDef_ ? "ps_3_0" : "ps_2_0";
        const UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;

        Microsoft::WRL::ComPtr<ID3DBlob> vsBlob, vsErr;
        HRESULT hr = D3DCompile(vertSrc.data(), vertSrc.size(), "ShaderEffect_vs", nullptr, nullptr,
                                "main", vsTarget, flags, 0, vsBlob.GetAddressOf(), vsErr.GetAddressOf());
        if (FAILED(hr))
        {
            compileError_ = vsErr
                ? std::string(static_cast<const char*>(vsErr->GetBufferPointer()), vsErr->GetBufferSize())
                : ("D3DCompile (vertex) failed, hr=" + FormatHr(hr));
            return false;
        }

        Microsoft::WRL::ComPtr<ID3DBlob> psBlob, psErr;
        hr = D3DCompile(fragSrc.data(), fragSrc.size(), "ShaderEffect_ps", nullptr, nullptr,
                        "main", psTarget, flags, 0, psBlob.GetAddressOf(), psErr.GetAddressOf());
        if (FAILED(hr))
        {
            compileError_ = psErr
                ? std::string(static_cast<const char*>(psErr->GetBufferPointer()), psErr->GetBufferSize())
                : ("D3DCompile (pixel) failed, hr=" + FormatHr(hr));
            return false;
        }

        hr = device_->CreateVertexShader(static_cast<const DWORD*>(vsBlob->GetBufferPointer()),
                                         vs_.ReleaseAndGetAddressOf());
        if (FAILED(hr)) { compileError_ = "CreateVertexShader failed, hr=" + FormatHr(hr); return false; }

        hr = device_->CreatePixelShader(static_cast<const DWORD*>(psBlob->GetBufferPointer()),
                                        ps_.ReleaseAndGetAddressOf());
        if (FAILED(hr)) { compileError_ = "CreatePixelShader failed, hr=" + FormatHr(hr); return false; }

        // D9-110: real, per-stage, compiler-assigned name -> register tables. Unlike D3D11's own
        // fixed-slot convention, these genuinely differ per compiled shader.
        vsConstants_ = ParseConstantTableEXT(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize());
        psConstants_ = ParseConstantTableEXT(psBlob->GetBufferPointer(), psBlob->GetBufferSize());

        valid_ = true;
        return true;
    }

    void D3D9EffectRenderer::Bind()
    {
        if (!valid_) return;
        device_->SetVertexShader(vs_.Get());
        device_->SetPixelShader(ps_.Get());
    }

    void D3D9EffectRenderer::Unbind()
    {
        // Mirrors D3D11EffectRenderer::Unbind(): nothing to restore here either. The caller (D9-112:
        // SpriteBatch's own custom-effect path) is responsible for rebinding SpriteEffect's stock
        // shaders before its next stock-shader draw, exactly as it already restores vertex
        // declarations/sampler state between batches.
    }

    void D3D9EffectRenderer::UploadEXT(const char* name, const float* data, int componentCount)
    {
        if (!valid_) return;

        // D3D9 constant registers are always full float4 slots -- pad scalar/vec2/vec3 uploads
        // with zeros for the unused trailing components rather than reading past the caller's
        // buffer (SetVertexShaderConstantF/SetPixelShaderConstantF always read `count * 4` floats).
        float padded[16] = {};
        const int clamped = std::min(componentCount, 16);
        std::memcpy(padded, data, static_cast<std::size_t>(clamped) * sizeof(float));
        const int registerCount = (componentCount + 3) / 4;

        for (const auto& c : vsConstants_)
        {
            if (c.name == name && c.registerSet == D3D9RegisterSetEXT::Float4)
            {
                device_->SetVertexShaderConstantF(c.registerIndex, padded, std::min(registerCount, c.registerCount));
                break;
            }
        }
        for (const auto& c : psConstants_)
        {
            if (c.name == name && c.registerSet == D3D9RegisterSetEXT::Float4)
            {
                device_->SetPixelShaderConstantF(c.registerIndex, padded, std::min(registerCount, c.registerCount));
                break;
            }
        }
    }

    void D3D9EffectRenderer::SetUniformFloat(const char* name, float value)
    {
        UploadEXT(name, &value, 1);
    }

    void D3D9EffectRenderer::SetUniformInt(const char* name, int value)
    {
        // Matches D3D11EffectRenderer's own convention: custom-effect "int" uniforms are expected
        // to be declared as HLSL float constants shader-side (a value-converted upload through the
        // same Float4 register path, not the separate D3DXRS_INT4 register file) -- reasonable for
        // this mechanism's own scope (SpriteBatch-custom-shader tinting/parameterization, not a
        // general-purpose uniform system).
        const float f = static_cast<float>(value);
        UploadEXT(name, &f, 1);
    }

    void D3D9EffectRenderer::SetUniformVec2(const char* name, float x, float y)
    {
        const float v[2] = {x, y};
        UploadEXT(name, v, 2);
    }

    void D3D9EffectRenderer::SetUniformVec3(const char* name, float x, float y, float z)
    {
        const float v[3] = {x, y, z};
        UploadEXT(name, v, 3);
    }

    void D3D9EffectRenderer::SetUniformVec4(const char* name, float x, float y, float z, float w)
    {
        const float v[4] = {x, y, z, w};
        UploadEXT(name, v, 4);
    }

    void D3D9EffectRenderer::SetUniformMat4(const char* name, const float* matrix)
    {
        UploadEXT(name, matrix, 16);
    }
}

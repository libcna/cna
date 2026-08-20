// plans/plan_dx.md Phase DIRECTX8 (DX-58).
#include "CNA/Internal/Renderers/DirectX11/D3D11EffectRenderer.hpp"

#include <d3dcompiler.h>

#include <cstdio>
#include <cstring>

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
        vs_.Reset(); ps_.Reset(); inputLayout_.Reset(); constantBuffer_.Reset();

        const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;

        Microsoft::WRL::ComPtr<ID3DBlob> vsBlob, vsErr;
        HRESULT hr = D3DCompile(vertSrc.data(), vertSrc.size(), "ShaderEffect_vs", nullptr, nullptr,
                                "main", "vs_5_0", flags, 0, vsBlob.GetAddressOf(), vsErr.GetAddressOf());
        if (FAILED(hr))
        {
            compileError_ = vsErr
                ? std::string(static_cast<const char*>(vsErr->GetBufferPointer()), vsErr->GetBufferSize())
                : ("D3DCompile (vertex) failed, hr=" + FormatHr(hr));
            return false;
        }

        Microsoft::WRL::ComPtr<ID3DBlob> psBlob, psErr;
        hr = D3DCompile(fragSrc.data(), fragSrc.size(), "ShaderEffect_ps", nullptr, nullptr,
                        "main", "ps_5_0", flags, 0, psBlob.GetAddressOf(), psErr.GetAddressOf());
        if (FAILED(hr))
        {
            compileError_ = psErr
                ? std::string(static_cast<const char*>(psErr->GetBufferPointer()), psErr->GetBufferSize())
                : ("D3DCompile (pixel) failed, hr=" + FormatHr(hr));
            return false;
        }

        hr = device_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                         nullptr, vs_.ReleaseAndGetAddressOf());
        if (FAILED(hr)) { compileError_ = "CreateVertexShader failed, hr=" + FormatHr(hr); return false; }

        hr = device_->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                        nullptr, ps_.ReleaseAndGetAddressOf());
        if (FAILED(hr)) { compileError_ = "CreatePixelShader failed, hr=" + FormatHr(hr); return false; }

        // Fixed Sprite2DVertex-shaped contract (x,y | u,v | r,g,b,a -- 32 bytes), matching
        // VulkanEffectRenderer's own hardcoded custom-shader vertex layout -- this mechanism is a
        // SpriteBatch-custom-shader facility, not a general arbitrary-vertex-format one.
        static const D3D11_INPUT_ELEMENT_DESC kElements[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 8,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        hr = device_->CreateInputLayout(kElements, ARRAYSIZE(kElements),
                                        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                        inputLayout_.ReleaseAndGetAddressOf());
        if (FAILED(hr)) { compileError_ = "CreateInputLayout failed, hr=" + FormatHr(hr); return false; }

        // 128-byte uniform constant buffer, same size as VulkanEffectRenderer's push-constant range
        // (design decision 14: D3D11 constant buffer ByteWidth must be a 16-byte multiple; 128 is).
        D3D11_BUFFER_DESC cbDesc{};
        cbDesc.ByteWidth = sizeof(pushConst_);
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = device_->CreateBuffer(&cbDesc, nullptr, constantBuffer_.ReleaseAndGetAddressOf());
        if (FAILED(hr)) { compileError_ = "Constant buffer creation failed, hr=" + FormatHr(hr); return false; }

        valid_ = true;
        return true;
    }

    void D3D11EffectRenderer::Bind()
    {
        if (!valid_) return;

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(context_->Map(constantBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            std::memcpy(mapped.pData, pushConst_, sizeof(pushConst_));
            context_->Unmap(constantBuffer_.Get(), 0);
        }

        context_->IASetInputLayout(inputLayout_.Get());
        context_->VSSetShader(vs_.Get(), nullptr, 0);
        context_->PSSetShader(ps_.Get(), nullptr, 0);
        ID3D11Buffer* cb = constantBuffer_.Get();
        context_->VSSetConstantBuffers(0, 1, &cb);
        context_->PSSetConstantBuffers(0, 1, &cb);
    }

    void D3D11EffectRenderer::Unbind()
    {
        // Nothing to restore -- mirrors VulkanEffectRenderer::Unbind() (clears a "current custom
        // effect" tracking pointer only; the next real draw call rebinds whatever it needs). No
        // such tracking exists yet on this renderer since SpriteBatch (Phase DX9) is the only real
        // consumer and doesn't exist yet -- Bind() above already performs the real GPU state
        // change directly (D3D11 has no separate pipeline-object assembly step to defer, unlike
        // Vulkan), so there is no deferred state for Unbind() to undo.
    }

    // DX-58/DX-71: mirrors VulkanEffectRenderer's exact fixed-slot convention byte-for-byte
    // (128-byte buffer: [0..15]=vpSize (vec2, set automatically by D3D11SpriteBatchRenderer --
    // see SetViewportSizeEXT below), [16..79]=mat4 matrix, [80..95]=vec4 color, [96..99]=
    // float/int slot 0) -- `name` is deliberately ignored, matching Vulkan's own
    // `/*name*/`-discarding precedent.
    void D3D11EffectRenderer::SetUniformMat4(const char* /*name*/, const float* matrix)
    {
        std::memcpy(pushConst_ + 4, matrix, 64);
    }

    void D3D11EffectRenderer::SetUniformVec4(const char* /*name*/, float x, float y, float z, float w)
    {
        pushConst_[20] = x; pushConst_[21] = y; pushConst_[22] = z; pushConst_[23] = w;
    }

    void D3D11EffectRenderer::SetUniformVec3(const char* /*name*/, float x, float y, float z)
    {
        pushConst_[20] = x; pushConst_[21] = y; pushConst_[22] = z;
    }

    void D3D11EffectRenderer::SetUniformVec2(const char* /*name*/, float x, float y)
    {
        pushConst_[20] = x; pushConst_[21] = y;
    }

    void D3D11EffectRenderer::SetUniformFloat(const char* /*name*/, float value)
    {
        pushConst_[24] = value;
    }

    void D3D11EffectRenderer::SetUniformInt(const char* /*name*/, int value)
    {
        pushConst_[24] = static_cast<float>(value);
    }

    void D3D11EffectRenderer::SetViewportSizeEXT(float width, float height)
    {
        pushConst_[0] = width;
        pushConst_[1] = height;
    }
}

// plans/plan_dx.md Phase DX13 (DX-121).
#include "CNA/Internal/Renderers/DirectX12/D3D12EffectRenderer.hpp"
#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"

#include <d3dcompiler.h>

#include <cstdio>
#include <cstring>

namespace CNA::Internal::Renderers::DirectX12
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

    D3D12EffectRenderer::D3D12EffectRenderer(DirectX12Renderer* owner)
        : owner_(owner), device_(owner_->GetDeviceEXT())
    {
    }

    bool D3D12EffectRenderer::CompileProgram(const std::string& vertSrc, const std::string& fragSrc)
    {
        compileError_.clear();
        valid_ = false;
        pso_.Reset();
        constantBuffer_.Reset();
        constantBufferMapped_ = nullptr;

        const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;

        ComPtr<ID3DBlob> vsBlob, vsErr;
        HRESULT hr = D3DCompile(vertSrc.data(), vertSrc.size(), "ShaderEffect_vs", nullptr, nullptr,
                                "main", "vs_5_0", flags, 0, vsBlob.GetAddressOf(), vsErr.GetAddressOf());
        if (FAILED(hr))
        {
            compileError_ = vsErr
                ? std::string(static_cast<const char*>(vsErr->GetBufferPointer()), vsErr->GetBufferSize())
                : ("D3DCompile (vertex) failed, hr=" + FormatHr(hr));
            return false;
        }

        ComPtr<ID3DBlob> psBlob, psErr;
        hr = D3DCompile(fragSrc.data(), fragSrc.size(), "ShaderEffect_ps", nullptr, nullptr,
                        "main", "ps_5_0", flags, 0, psBlob.GetAddressOf(), psErr.GetAddressOf());
        if (FAILED(hr))
        {
            compileError_ = psErr
                ? std::string(static_cast<const char*>(psErr->GetBufferPointer()), psErr->GetBufferSize())
                : ("D3DCompile (pixel) failed, hr=" + FormatHr(hr));
            return false;
        }

        // Same (1,1,1) root-signature shape D3D12SpriteBatchRenderer's own stock sprite2d PSO uses
        // (CBV@b0, SRV table@t0, sampler table@s0) -- D3D12RootSignatureCache::GetOrCreate caches
        // by shape, so this is the exact SAME object FlushBatch() will later bind via
        // SetGraphicsRootSignature(), not a new/different one this class invents.
        ComPtr<ID3D12RootSignature> rootSig = owner_->GetRootSignatureCacheEXT().GetOrCreate(device_, 1, 1, 1);
        if (!rootSig)
        {
            compileError_ = "D3D12EffectRenderer: failed to obtain the shared (1,1,1) root signature";
            return false;
        }

        // Fixed Sprite2DVertex-shaped contract (x,y | u,v | r,g,b,a -- 32 bytes), matching
        // D3D12SpriteBatchRenderer's own hardcoded custom-shader vertex layout exactly.
        static const D3D12_INPUT_ELEMENT_DESC kElements[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 8,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = rootSig.Get();
        desc.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
        desc.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};
        desc.InputLayout = {kElements, static_cast<UINT>(std::size(kElements))};
        desc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.SampleMask = UINT_MAX;
        desc.SampleDesc.Count = 1;
        desc.NodeMask = 0;

        desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.RasterizerState.FrontCounterClockwise = FALSE;
        desc.RasterizerState.DepthClipEnable = TRUE;

        D3D12_RENDER_TARGET_BLEND_DESC& rt0 = desc.BlendState.RenderTarget[0];
        rt0.BlendEnable = FALSE;
        rt0.SrcBlend = D3D12_BLEND_ONE;
        rt0.DestBlend = D3D12_BLEND_ZERO;
        rt0.BlendOp = D3D12_BLEND_OP_ADD;
        rt0.SrcBlendAlpha = D3D12_BLEND_ONE;
        rt0.DestBlendAlpha = D3D12_BLEND_ZERO;
        rt0.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        rt0.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        desc.DepthStencilState.DepthEnable = FALSE;
        desc.DepthStencilState.StencilEnable = FALSE;

        desc.NumRenderTargets = 1;
        // Hardcoded, not queried from owner_->GetBoundColorFormatEXT() -- see this class's own
        // header doc comment for why (CompileProgram() may run before any target is bound).
        desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.DSVFormat = DXGI_FORMAT_UNKNOWN;

        hr = device_->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(pso_.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
        {
            compileError_ = "CreateGraphicsPipelineState failed, hr=" + FormatHr(hr);
            return false;
        }

        // 128-byte uniform constant buffer, same size/layout as D3D11EffectRenderer's own
        // pushConst_ (design decision 14: D3D12 constant buffer ByteWidth must be a 16-byte
        // multiple; 128 is).
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC cbDesc{};
        cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        cbDesc.Width = sizeof(pushConst_);
        cbDesc.Height = 1;
        cbDesc.DepthOrArraySize = 1;
        cbDesc.MipLevels = 1;
        cbDesc.Format = DXGI_FORMAT_UNKNOWN;
        cbDesc.SampleDesc.Count = 1;
        cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        hr = device_->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &cbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(constantBuffer_.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
        {
            compileError_ = "Constant buffer CreateCommittedResource failed, hr=" + FormatHr(hr);
            pso_.Reset();
            return false;
        }

        const D3D12_RANGE readRange{0, 0};
        hr = constantBuffer_->Map(0, &readRange, &constantBufferMapped_);
        if (FAILED(hr))
        {
            compileError_ = "Constant buffer Map failed, hr=" + FormatHr(hr);
            pso_.Reset();
            constantBuffer_.Reset();
            return false;
        }

        valid_ = true;
        return true;
    }

    void D3D12EffectRenderer::Bind()
    {
        if (!valid_) return;
        std::memcpy(constantBufferMapped_, pushConst_, sizeof(pushConst_));
    }

    void D3D12EffectRenderer::Unbind()
    {
        // Nothing to restore -- mirrors D3D11EffectRenderer::Unbind()'s own reasoning: the real
        // GPU-visible state change happens when D3D12SpriteBatchRenderer::FlushBatch() itself binds
        // GetPipelineStateEXT()/GetConstantBufferEXT() within its own command-list recording, not
        // here; there is no deferred/persistent context state for Unbind() to undo.
    }

    // DX-121: mirrors D3D11EffectRenderer's exact fixed-slot convention byte-for-byte (128-byte
    // buffer: [0..15]=vpSize (vec2, set automatically by D3D12SpriteBatchRenderer -- see
    // SetViewportSizeEXT below), [16..79]=mat4 matrix, [80..95]=vec4 color, [96..99]=float/int slot
    // 0) -- `name` is deliberately ignored, matching D3D11's own `/*name*/`-discarding precedent.
    void D3D12EffectRenderer::SetUniformMat4(const char* /*name*/, const float* matrix)
    {
        std::memcpy(pushConst_ + 4, matrix, 64);
    }

    void D3D12EffectRenderer::SetUniformVec4(const char* /*name*/, float x, float y, float z, float w)
    {
        pushConst_[20] = x; pushConst_[21] = y; pushConst_[22] = z; pushConst_[23] = w;
    }

    void D3D12EffectRenderer::SetUniformVec3(const char* /*name*/, float x, float y, float z)
    {
        pushConst_[20] = x; pushConst_[21] = y; pushConst_[22] = z;
    }

    void D3D12EffectRenderer::SetUniformVec2(const char* /*name*/, float x, float y)
    {
        pushConst_[20] = x; pushConst_[21] = y;
    }

    void D3D12EffectRenderer::SetUniformFloat(const char* /*name*/, float value)
    {
        pushConst_[24] = value;
    }

    void D3D12EffectRenderer::SetUniformInt(const char* /*name*/, int value)
    {
        pushConst_[24] = static_cast<float>(value);
    }

    void D3D12EffectRenderer::SetViewportSizeEXT(float width, float height)
    {
        pushConst_[0] = width;
        pushConst_[1] = height;
    }
}

// SPDX-License-Identifier: MS-PL
// plan_dx9.md Phase D9-8 (D9-83): real hardware instancing via SetStreamSourceFreq. Uses CNA's own
// CNAEXT "Instanced3D" shader (shaders/cna/Instanced3D.hlsl, embedded bytecode in
// shaders/d3d9_instanced3d_shader.hpp) -- NOT one of Microsoft's real Stock Effects.
//
// Real XNA 4.0 has no per-instance-aware Stock Effect vertex shader at all: BasicEffect/
// AlphaTestEffect/DualTextureEffect/EnvironmentMapEffect/SkinnedEffect's real VSInput* structs
// (transcribed verbatim from Structures.fxh across D9-82b-f) declare no per-instance semantic.
// Real XNA hardware instancing requires the game author's own custom Effect with a custom vertex
// shader that reads the extra per-instance stream -- there is no "instanced BasicEffect" in real
// XNA. This shader is CNA's own minimal stand-in for that custom-Effect role, matching every other
// renderer's own identical choice (D3D11/Vulkan/Bgfx each authored their own equivalent
// "instanced3d" shader rather than attempting to instance a stock effect) -- so this file does NOT
// dispatch through D3D9EffectDraw.cpp's DrawPrimitivesExImpl(), by design.

#include "CNA/Internal/Renderers/DirectX9/DirectX9Renderer.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9Buffers.hpp"
#include "shaders/d3d9_instanced3d_shader.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <cstdio>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Renderers::DirectX9
{
    using Microsoft::Xna::Framework::Matrix;

    namespace
    {
        D3DPRIMITIVETYPE ToD3D9Topology(PrimitiveType pt)
        {
            switch (pt)
            {
            case PrimitiveType::TriangleList:  return D3DPT_TRIANGLELIST;
            case PrimitiveType::TriangleStrip: return D3DPT_TRIANGLESTRIP;
            case PrimitiveType::LineList:      return D3DPT_LINELIST;
            case PrimitiveType::LineStrip:     return D3DPT_LINESTRIP;
            case PrimitiveType::PointListEXT:  return D3DPT_POINTLIST;
            }
            throw std::runtime_error(
                "DirectX9 instanced draw does not support the requested PrimitiveType value");
        }

        std::string FormatHr(HRESULT hr)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
            return buf;
        }
    }

    IDirect3DVertexDeclaration9* DirectX9Renderer::GetOrCreateInstancedVertexDeclarationEXT()
    {
        if (instancedVertexDecl_) return instancedVertexDecl_.Get();

        static constexpr D3DVERTEXELEMENT9 kElements[] = {
            {0, 0,  D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
            {1, 0,  D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
            {1, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2},
            {1, 32, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 3},
            {1, 48, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 4},
            D3DDECL_END()
        };

        const HRESULT hr = device_->CreateVertexDeclaration(kElements, instancedVertexDecl_.GetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("DirectX9Renderer: instanced CreateVertexDeclaration failed, hr=" + FormatHr(hr));
        return instancedVertexDecl_.Get();
    }

    void DirectX9Renderer::DrawInstancedPrimitivesEx(
        const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, int instanceCount, const GpuDrawParams& params)
    {
        // D9-83: matches DirectX11Renderer::DrawInstancedPrimitivesEx's own fallback -- no
        // per-instance VB means this isn't really an instanced draw at all.
        const auto* instanceStream = FirstInstanceStream(params);
        if (instanceStream == nullptr)
        {
            DrawIndexedPrimitivesEx(vb, ib, world, view, projection, primitive, primitiveCount, params);
            return;
        }

        EnsureRenderReadyEXT();

        const auto& d3dVb     = static_cast<const D3D9VertexBufferRenderer&>(vb);
        const auto& d3dIb     = static_cast<const D3D9IndexBufferRenderer&>(ib);
        // REMED-GFX-202: one stream of each rate (REMED-GFX-208 tracks widening it).
        RejectUnsupportedStreamCombination(params, "The D3D9 renderer");
        // REMED-GFX-DECL-GUARD: the geometry stream's declaration, same stride table.
        RequireFaithfulDeclarationEXT(vb, "instanced");
        const auto& d3dInstVb =
            static_cast<const D3D9VertexBufferRenderer&>(*instanceStream->buffer);
        const std::size_t perVertexStride = d3dVb.GetStrideEXT() > 0 ? d3dVb.GetStrideEXT() : 16;
        constexpr UINT kInstanceStride = 64; // 4 x float4 rows (matches every other renderer's own convention)

        if (!instancedVS_)
        {
            const HRESULT hr = device_->CreateVertexShader(
                reinterpret_cast<const DWORD*>(Shaders::kInstanced3DVSBytecode), instancedVS_.GetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("DrawInstancedPrimitivesEx: CreateVertexShader failed, hr=" + FormatHr(hr));
        }
        if (!instancedPS_)
        {
            const HRESULT hr = device_->CreatePixelShader(
                reinterpret_cast<const DWORD*>(Shaders::kInstanced3DPSBytecode), instancedPS_.GetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("DrawInstancedPrimitivesEx: CreatePixelShader failed, hr=" + FormatHr(hr));
        }
        device_->SetVertexShader(instancedVS_.Get());
        device_->SetPixelShader(instancedPS_.Get());

        // ViewProj: per-instance world comes from the instance stream instead, so (unlike every
        // other draw path in this renderer) the `world` parameter is deliberately unused here --
        // matches DirectX11Renderer::DrawInstancedPrimitivesEx's own identical choice. Same
        // register=column transpose convention used everywhere else in this renderer.
        const Matrix vpT = Matrix::Transpose(view * projection);
        float vpRegs[16];
        vpT.ToColumnMajor(vpRegs);
        device_->SetVertexShaderConstantF(0, vpRegs, 4);
        device_->SetVertexShaderConstantF(4, params.diffuseColor, 1);

        device_->SetVertexDeclaration(GetOrCreateInstancedVertexDeclarationEXT());
        device_->SetStreamSource(0, d3dVb.GetBufferEXT(), 0, static_cast<UINT>(perVertexStride));
        device_->SetStreamSource(1, d3dInstVb.GetBufferEXT(), 0, kInstanceStride);
        device_->SetIndices(d3dIb.GetBufferEXT());

        // Stream 0 (per-vertex geometry, drawn via the index buffer) advances once per instance;
        // stream 1 (per-instance data) supplies one new element per instance -- the MSDN-documented
        // "Efficiently Drawing Multiple Instances of Geometry" convention.
        device_->SetStreamSourceFreq(0, D3DSTREAMSOURCE_INDEXEDDATA | static_cast<UINT>(instanceCount));
        device_->SetStreamSourceFreq(1, D3DSTREAMSOURCE_INSTANCEDATA | 1);

        // REMED-GFX-060: honor DrawInstancedPrimitives baseVertex/startIndex (was hardcoded
        // BaseVertexIndex=0/StartIndex=0). These index the PER-VERTEX geometry stream (stream 0);
        // the per-instance stream 1's own offset stays 0 (SetStreamSource above) -- the two are
        // independent. NumVertices is stream 0's vertex-buffer remainder from baseVertex.
        device_->DrawIndexedPrimitive(ToD3D9Topology(primitive), static_cast<INT>(params.baseVertex), 0,
                                      static_cast<UINT>(d3dVb.GetVertexCount() - params.baseVertex),
                                      static_cast<UINT>(params.startIndex), static_cast<UINT>(primitiveCount));

        // D3D9 stream-frequency state persists on the device until explicitly changed, and every
        // OTHER draw path in this renderer reuses stream 0 -- leaving instancing semantics active
        // here would silently corrupt every subsequent non-instanced draw call. Reset immediately,
        // not deferred.
        device_->SetStreamSourceFreq(0, 1);
        device_->SetStreamSourceFreq(1, 1);
    }
}

// SPDX-License-Identifier: MS-PL
// D3D9 skinned-vertex-color porting task: real DrawPrimitivesEx/DrawIndexedPrimitivesEx dispatch
// for CNA's own CNAEXT "SkinnedVertexColor3D" shader (stride 56 -- SkinnedEffect + a vertex Color
// real XNA's own compiled SkinnedEffect.fx bytecode never carries), porting
// EasyGLRenderer.cpp's EnsureSkinnedProgram() vertex-color wiring (aColor/vColor/
// uVertexColorEnabled, multiplied into the combined diffuse+specular output AFTER the specular add)
// to real vs_3_0/ps_3_0 bytecode (shaders/cna/SkinnedVertexColor3D.hlsl -- see that file's own
// header comment for the SM3 justification and the documented World-normal-transform deviation
// from EnsureSkinnedProgram()'s own GLSL).
//
// Not a Microsoft Stock Effect -- see D3D9InstancedDraw.cpp's own header comment for the general
// "why a separate custom shader" rationale, and D3D9EffectDraw.cpp's own header comment for why the
// vendored SkinnedEffect.fx bytecode itself is never modified to add this. Selected ahead of the
// real SkinnedEffect dispatch (DrawSkinnedEffectEXT) by DrawPrimitivesExImpl when
// params.skinned && the bound vertex buffer's stride is 56 (see that function's own updated
// comment).
//
// Constant uploads go through D3D9ConstantUpload.cpp's existing, proven
// TryUpload{Vertex,Pixel}ShaderConstantEXT() helpers against this task's own real,
// D3DDisassemble()-verified register table (D3D9CnaShaderRegisters.hpp) -- see D3D9PbrDraw.cpp's
// own header comment for why (the World-register-count trap this avoids).

#include "CNA/Internal/Renderers/DirectX9/DirectX9Renderer.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9Buffers.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9Textures.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9RenderTargets.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9ConstantUpload.hpp"
#include "shaders/d3d9_skinned_vertex_color_shader.hpp"
#include "shaders/D3D9CnaShaderRegisters.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

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
                "DirectX9 skinned vertex-color draw does not support the requested PrimitiveType value");
        }

        std::string FormatHr(HRESULT hr)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
            return buf;
        }

        // Duplicated locally per this codebase's own established per-file convention -- see
        // D3D9PbrDraw.cpp's own identical copy for the precedent citation.
        IDirect3DTexture9* ResolveD3D9TextureEXT(const ITextureRenderer* tex)
        {
            if (tex == nullptr) return nullptr;
            if (const auto* t = dynamic_cast<const D3D9TextureRenderer*>(tex))
                return t->GetTextureEXT();
            if (const auto* rt = dynamic_cast<const D3D9RenderTargetRenderer*>(tex))
                return rt->GetTextureEXT();
            return nullptr;
        }

        struct Vec4Pad { float v[4]; };
        Vec4Pad Pad3(const float v3[3]) { return Vec4Pad{{v3[0], v3[1], v3[2], 0.0f}}; }

        void UploadMatrixConstantVS(IDirect3DDevice9* device, const Shaders::D3D9ShaderConstantSlot* table,
                                    int count, const char* name, const Matrix& m)
        {
            const Matrix transposed = Matrix::Transpose(m);
            float regs[16];
            transposed.ToColumnMajor(regs);
            TryUploadVertexShaderConstantEXT(device, table, count, name, regs);
        }

        void UploadBonesVS(IDirect3DDevice9* device, const Shaders::D3D9ShaderConstantSlot* table,
                          int count, const GpuDrawParams& params)
        {
            std::vector<float> regs(72 * 12, 0.0f);
            const int boneCount = params.boneCount < 72 ? params.boneCount : 72;
            for (int i = 0; i < boneCount; ++i)
            {
                const float* b = params.boneTransforms + i * 16;
                const Matrix boneMatrix(b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
                                        b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
                const Matrix transposed = Matrix::Transpose(boneMatrix);
                float full16[16];
                transposed.ToColumnMajor(full16);
                std::copy(full16, full16 + 12, regs.begin() + static_cast<std::ptrdiff_t>(i) * 12);
            }
            TryUploadVertexShaderConstantEXT(device, table, count, "Bones", regs.data());
        }
    }

    void DirectX9Renderer::DrawSkinnedVertexColorEXT(
        const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        using namespace Shaders;

        if (!params.texture0)
            throw std::runtime_error(
                "DirectX9Renderer::DrawPrimitivesEx (SkinnedVertexColor3D): requires non-null "
                "texture0 (matches DrawSkinnedEffectEXT's own identical requirement)");

        if (!skinnedVertexColorVS_)
        {
            const HRESULT hr = device_->CreateVertexShader(
                reinterpret_cast<const DWORD*>(kSkinnedVertexColor3DVSBytecode),
                skinnedVertexColorVS_.GetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("DrawSkinnedVertexColorEXT: CreateVertexShader failed, hr=" + FormatHr(hr));
        }
        if (!skinnedVertexColorPS_)
        {
            const HRESULT hr = device_->CreatePixelShader(
                reinterpret_cast<const DWORD*>(kSkinnedVertexColor3DPSBytecode),
                skinnedVertexColorPS_.GetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("DrawSkinnedVertexColorEXT: CreatePixelShader failed, hr=" + FormatHr(hr));
        }
        device_->SetVertexShader(skinnedVertexColorVS_.Get());
        device_->SetPixelShader(skinnedVertexColorPS_.Get());

        const auto* vsRegs = kSkinnedVertexColor3DVS_Registers;
        const int vsCount = kSkinnedVertexColor3DVS_RegistersCount;
        const auto* psRegs = kSkinnedVertexColor3DPS_Registers;
        const int psCount = kSkinnedVertexColor3DPS_RegistersCount;

        UploadMatrixConstantVS(device_.Get(), vsRegs, vsCount, "WorldViewProj", world * view * projection);
        UploadMatrixConstantVS(device_.Get(), vsRegs, vsCount, "World", world);
        // FogParams.w carries WeightsPerVertex. REMED-GFX-010: fog moved to its own FogVector
        // register (the FNA view-space fog vector), dotted with the POST-skin position in the VS;
        // FogParams.xyz are now unused.
        const float fogParams[4] = {
            0.0f, 0.0f, 0.0f, static_cast<float>(params.weightsPerVertex)};
        TryUploadVertexShaderConstantEXT(device_.Get(), vsRegs, vsCount, "FogParams", fogParams);
        const float fogVector[4] = {
            params.fogVector[0], params.fogVector[1], params.fogVector[2], params.fogVector[3]};
        TryUploadVertexShaderConstantEXT(device_.Get(), vsRegs, vsCount, "FogVector", fogVector);
        UploadBonesVS(device_.Get(), vsRegs, vsCount, params);

        // SkinnedEffect::FillGpuDrawParams() already pre-folds ambient into emissiveColor (matches
        // DrawSkinnedEffectEXT's own identical upload of params.emissiveColor as "EmissiveColor" --
        // see that function's own comment for the citation); reused verbatim here.
        TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, "DiffuseColor", params.diffuseColor);
        TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, "EmissiveColor", Pad3(params.emissiveColor).v);
        TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, "Light0Dir", Pad3(params.light0Dir).v);
        TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, "Light0Diffuse", Pad3(params.light0Diffuse).v);
        TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, "Light1Dir", Pad3(params.light1Dir).v);
        TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, "Light1Diffuse", Pad3(params.light1Diffuse).v);
        TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, "Light2Dir", Pad3(params.light2Dir).v);
        TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, "Light2Diffuse", Pad3(params.light2Diffuse).v);
        TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, "Light0Specular", Pad3(params.light0Specular).v);
        TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, "Light1Specular", Pad3(params.light1Specular).v);
        TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, "Light2Specular", Pad3(params.light2Specular).v);
        TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, "SpecularColor", Pad3(params.specularColor).v);
        const float specularPower4[4] = {params.specularPower, 0.0f, 0.0f, 0.0f};
        TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, "SpecularPowerPad", specularPower4);
        TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, "EyePosition", Pad3(params.eyePositionWorld).v);
        TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, "AlphaTest", params.alphaTest);
        TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, "FogColor", Pad3(params.fogColor).v);
        const float vertexColorEnabled4[4] = {params.vertexColorEnabled ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
        TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, "VertexColorEnabledPad", vertexColorEnabled4);

        device_->SetTexture(0, ResolveD3D9TextureEXT(params.texture0));

        device_->SetVertexDeclaration(GetOrCreateVertexDeclarationEXT(56));
        const auto& d3dVb = static_cast<const D3D9VertexBufferRenderer&>(vb);
        device_->SetStreamSource(0, d3dVb.GetBufferEXT(), 0, 56);

        if (ib)
        {
            const auto& d3dIb = static_cast<const D3D9IndexBufferRenderer&>(*ib);
            device_->SetIndices(d3dIb.GetBufferEXT());
            // REMED-GFX-060: honor DrawIndexedPrimitives baseVertex/startIndex (was hardcoded
            // BaseVertexIndex=0/StartIndex=0). NumVertices is the vertex-buffer remainder from
            // baseVertex; MinIndex stays 0 (CNA carries no tighter min-vertex-index range).
            device_->DrawIndexedPrimitive(ToD3D9Topology(primitive), static_cast<INT>(params.baseVertex), 0,
                                          static_cast<UINT>(d3dVb.GetVertexCount() - params.baseVertex),
                                          static_cast<UINT>(params.startIndex), static_cast<UINT>(primitiveCount));
        }
        else
        {
            // REMED-GFX-060: honor DrawPrimitives vertexStart (was hardcoded StartVertex=0).
            device_->DrawPrimitive(ToD3D9Topology(primitive), static_cast<UINT>(params.vertexStart),
                                   static_cast<UINT>(primitiveCount));
        }
    }
}

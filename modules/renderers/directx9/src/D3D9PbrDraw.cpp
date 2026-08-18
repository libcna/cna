// SPDX-License-Identifier: MS-PL
// D3D9 PBR porting task: real DrawPrimitivesEx/DrawIndexedPrimitivesEx dispatch for CNA's own
// CNAEXT "Pbr3D"/"PbrSkinned3D" shaders (PbrEffect/SkinnedPbrEffect, plan_cnj.md CNB-56..79
// equivalent), porting EasyGLRenderer.cpp's EnsurePbrProgram()/EnsurePbrSkinnedProgram() +
// SelectProgram()'s own pbr-highest-priority dispatch to real vs_3_0/ps_3_0 bytecode
// (shaders/cna/Pbr3D.hlsl, shaders/cna/PbrSkinned3D.hlsl -- see those files' own header comments
// for the BRDF source and the SM3-not-SM2 justification).
//
// Not a Microsoft Stock Effect: XNA 4.0 has no PBR effect at all, so this file does NOT dispatch
// through D3D9EffectDraw.cpp's DrawPrimitivesExImpl() stock-effect cascade in the way
// DrawBasicEffectEXT/DrawSkinnedEffectEXT/etc. do -- it IS one of that cascade's own branches
// (the highest-priority one, checked first), but its own shader creation/constant upload is fully
// self-contained here, matching D3D9InstancedDraw.cpp's own "separate file, separate shader
// objects" precedent for a CNA custom (non-stock) shader.
//
// Constant uploads go through D3D9ConstantUpload.cpp's existing, proven
// TryUpload{Vertex,Pixel}ShaderConstantEXT() helpers against this task's own real,
// D3DDisassemble()-verified register table (D3D9CnaShaderRegisters.hpp) -- NOT a hand-assumed
// "float4x4 always needs 4 registers" SetVertexShaderConstantF call. See
// D3D9CnaShaderRegisters.hpp's own header comment for the real bug this avoided during
// development (World's un-allocated 4th register collides with a compiler-emitted literal
// constant at the same index).

#include "CNA/Internal/Renderers/DirectX9/DirectX9Renderer.hpp"
#include "CNA/Internal/Renderers/Common/VertexColourPbrSupport.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9Buffers.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9Textures.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9RenderTargets.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9ConstantUpload.hpp"
#include "shaders/d3d9_pbr_shaders.hpp"
#include "shaders/D3D9CnaShaderRegisters.hpp"
#include "CNA/Internal/Graphics/ImageData.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Internal::Renderers::DirectX9
{
    using Microsoft::Xna::Framework::Matrix;
    using CNA::Internal::Graphics::ImageData;

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
                "DirectX9 PBR draw does not support the requested PrimitiveType value");
        }

        std::string FormatHr(HRESULT hr)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
            return buf;
        }

        // Same two-concrete-type resolution as D3D9EffectDraw.cpp's own ResolveD3D9TextureEXT /
        // D3D9SpriteBatch.cpp's own independent copy -- duplicated locally per this codebase's own
        // established per-file convention (see D3D9VertexDeclarations.cpp's own "Task 11.10"
        // comment for the precedent of accepting this kind of small duplication).
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

        /// EffectParameter.SetValue(Matrix)'s real XNA upload convention -- register k receives
        /// COLUMN k of the row-major matrix (the transpose of Matrix::ToColumnMajor()'s own
        /// row-major-flat reading order). Matches D3D9EffectDraw.cpp's own UploadMatrixConstantVS()
        /// exactly (duplicated locally, same reasoning as ResolveD3D9TextureEXT above).
        void UploadMatrixConstantVS(IDirect3DDevice9* device, const Shaders::D3D9ShaderConstantSlot* table,
                                    int count, const char* name, const Matrix& m)
        {
            const Matrix transposed = Matrix::Transpose(m);
            float regs[16];
            transposed.ToColumnMajor(regs);
            TryUploadVertexShaderConstantEXT(device, table, count, name, regs);
        }

        /// Real SkinnedEffect.fx's own Bones[72] packing (float4x3, 3 registers/bone -- the
        /// ColumnCount==4/RowCount==3 EffectParameter.SetValue(Matrix) branch), reused verbatim for
        /// PbrSkinned3D's own identically-shaped Bones[72] constant. Matches
        /// D3D9EffectDraw.cpp's own UploadBonesVS() exactly (duplicated locally).
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

        /// Binds `tex` (or `fallback` when `tex` is null) to sampler stage `stage`, matching
        /// EasyGLRenderer::BindDrawParams()'s own per-map fallback convention (see
        /// GetOrCreateDefault{FlatNormal,White}TextureEXT()'s own doc comments for which fallback
        /// each PBR map uses and why).
        void BindPbrSampler(IDirect3DDevice9* device, int stage, const ITextureRenderer* tex,
                            IDirect3DTexture9* fallback)
        {
            IDirect3DTexture9* d3dTex = ResolveD3D9TextureEXT(tex);
            device->SetTexture(stage, d3dTex ? d3dTex : fallback);
        }
    }

    ITextureRenderer* DirectX9Renderer::GetOrCreateDefaultFlatNormalTextureEXT()
    {
        if (!defaultFlatNormalTexture_)
        {
            ImageData data;
            data.width = 1;
            data.height = 1;
            data.pixels = {128, 128, 255, 255}; // RGBA8: decodes in-shader to normal (0,0,1)
            defaultFlatNormalTexture_ = CreateTexture(data);
        }
        return defaultFlatNormalTexture_.get();
    }

    ITextureRenderer* DirectX9Renderer::GetOrCreateDefaultWhiteTextureEXT()
    {
        if (!defaultWhiteTexture_)
        {
            ImageData data;
            data.width = 1;
            data.height = 1;
            data.pixels = {255, 255, 255, 255};
            defaultWhiteTexture_ = CreateTexture(data);
        }
        return defaultWhiteTexture_.get();
    }

    void DirectX9Renderer::DrawPbrEffectEXT(
        const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib, std::size_t stride,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        using namespace Shaders;

        // plan_gltf.md GLTF-465: stride 60/80 already fail the two stride checks below, but they fail
        // them as "no matching CNA vertex layout", which says nothing about the semantic that is
        // actually missing. Name it first, with the application's own opt-out, so the refusal is the
        // shared one every other renderer in this state gives.
        RequireVertexColourPbrSupportEXT(params, stride, "DIRECTX9");

        const bool skinned = params.skinned;
        if (skinned && stride != 68)
            throw std::runtime_error(
                "DirectX9Renderer::DrawPrimitivesEx (PbrEffect, skinned): stride " +
                std::to_string(stride) + " has no matching CNA vertex layout (expected 68, "
                "VertexPositionNormalTangentTextureSkinned)");
        if (!skinned && stride != 48)
            throw std::runtime_error(
                "DirectX9Renderer::DrawPrimitivesEx (PbrEffect): stride " +
                std::to_string(stride) + " has no matching CNA vertex layout (expected 48, "
                "VertexPositionNormalTangentTexture)");

        IDirect3DVertexShader9* vs;
        IDirect3DPixelShader9* ps;
        const D3D9ShaderConstantSlot* vsRegs;
        int vsCount;
        const D3D9ShaderConstantSlot* psRegs;
        int psCount;

        if (skinned)
        {
            if (!pbrSkinnedVS_)
            {
                const HRESULT hr = device_->CreateVertexShader(
                    reinterpret_cast<const DWORD*>(kPbrSkinned3DVSBytecode), pbrSkinnedVS_.GetAddressOf());
                if (FAILED(hr))
                    throw std::runtime_error("DrawPbrEffectEXT: CreateVertexShader (skinned) failed, hr=" + FormatHr(hr));
            }
            if (!pbrSkinnedPS_)
            {
                const HRESULT hr = device_->CreatePixelShader(
                    reinterpret_cast<const DWORD*>(kPbrSkinned3DPSBytecode), pbrSkinnedPS_.GetAddressOf());
                if (FAILED(hr))
                    throw std::runtime_error("DrawPbrEffectEXT: CreatePixelShader (skinned) failed, hr=" + FormatHr(hr));
            }
            vs = pbrSkinnedVS_.Get();
            ps = pbrSkinnedPS_.Get();
            vsRegs = kPbrSkinned3DVS_Registers; vsCount = kPbrSkinned3DVS_RegistersCount;
            psRegs = kPbrSkinned3DPS_Registers; psCount = kPbrSkinned3DPS_RegistersCount;
        }
        else
        {
            if (!pbrVS_)
            {
                const HRESULT hr = device_->CreateVertexShader(
                    reinterpret_cast<const DWORD*>(kPbr3DVSBytecode), pbrVS_.GetAddressOf());
                if (FAILED(hr))
                    throw std::runtime_error("DrawPbrEffectEXT: CreateVertexShader failed, hr=" + FormatHr(hr));
            }
            if (!pbrPS_)
            {
                const HRESULT hr = device_->CreatePixelShader(
                    reinterpret_cast<const DWORD*>(kPbr3DPSBytecode), pbrPS_.GetAddressOf());
                if (FAILED(hr))
                    throw std::runtime_error("DrawPbrEffectEXT: CreatePixelShader failed, hr=" + FormatHr(hr));
            }
            vs = pbrVS_.Get();
            ps = pbrPS_.Get();
            vsRegs = kPbr3DVS_Registers; vsCount = kPbr3DVS_RegistersCount;
            psRegs = kPbr3DPS_Registers; psCount = kPbr3DPS_RegistersCount;
        }

        device_->SetVertexShader(vs);
        device_->SetPixelShader(ps);

        UploadMatrixConstantVS(device_.Get(), vsRegs, vsCount, "WorldViewProj", world * view * projection);
        UploadMatrixConstantVS(device_.Get(), vsRegs, vsCount, "World", world);
        if (skinned)
        {
            // FogParams.w carries WeightsPerVertex for the skinned variant (PbrSkinned3D.hlsl's
            // own register layout, matching SkinnedVertexColor3D.hlsl's identical packing choice).
            // REMED-GFX-010: fog moved to its own FogVector register (the FNA view-space fog vector),
            // dotted with the POST-skin position in the VS; FogParams.xyz are now unused.
            const float fogParams[4] = {
                0.0f, 0.0f, 0.0f, static_cast<float>(params.weightsPerVertex)};
            TryUploadVertexShaderConstantEXT(device_.Get(), vsRegs, vsCount, "FogParams", fogParams);
            const float fogVector[4] = {
                params.fogVector[0], params.fogVector[1], params.fogVector[2], params.fogVector[3]};
            TryUploadVertexShaderConstantEXT(device_.Get(), vsRegs, vsCount, "FogVector", fogVector);
            UploadBonesVS(device_.Get(), vsRegs, vsCount, params);
        }
        else
        {
            const Matrix worldInverseTranspose = Matrix::Transpose(Matrix::Invert(world));
            UploadMatrixConstantVS(device_.Get(), vsRegs, vsCount, "NormalMatrix", worldInverseTranspose);
            // REMED-GFX-010: Pbr3D (non-skinned) has no WeightsPerVertex, so FogParams carries the
            // FNA view-space fog vector directly (dotted with the object position in the VS).
            const float fogParams[4] = {
                params.fogVector[0], params.fogVector[1], params.fogVector[2], params.fogVector[3]};
            TryUploadVertexShaderConstantEXT(device_.Get(), vsRegs, vsCount, "FogParams", fogParams);
        }

        TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, "DiffuseColor", params.diffuseColor);
        const float ambientColor[4] = {params.ambientColor[0], params.ambientColor[1],
                                       params.ambientColor[2],
                                       params.pbrBaseColorTextureIsSrgb ? 1.0f : 0.0f};
        const float emissiveColor[4] = {params.emissiveColor[0], params.emissiveColor[1],
                                        params.emissiveColor[2],
                                        params.pbrEmissiveTextureIsSrgb ? 1.0f : 0.0f};
        TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, "AmbientColor", ambientColor);
        TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, "EmissiveColor", emissiveColor);
        const float metallicRoughness[4] = {params.pbrMetallicFactor, params.pbrRoughnessFactor,
                                            params.pbrNormalScale, params.pbrOcclusionStrength};
        TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, "MetallicRoughnessFactor", metallicRoughness);
        TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, "Light0Dir", Pad3(params.light0Dir).v);
        TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, "Light0Diffuse", Pad3(params.light0Diffuse).v);
        TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, "Light1Dir", Pad3(params.light1Dir).v);
        TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, "Light1Diffuse", Pad3(params.light1Diffuse).v);
        TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, "Light2Dir", Pad3(params.light2Dir).v);
        TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, "Light2Diffuse", Pad3(params.light2Diffuse).v);
        TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, "EyePosition", Pad3(params.eyePositionWorld).v);
        TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, "AlphaTest", params.alphaTest);
        const float fogColor[4] = {params.fogColor[0], params.fogColor[1], params.fogColor[2],
                                   params.pbrEncodeOutputToSrgb ? 1.0f : 0.0f};
        TryUploadPixelShaderConstantEXT(device_.Get(), psRegs, psCount, "FogColor", fogColor);
        const float dielectricFresnel[4] = {
            params.pbrDielectricF0Unclamped[0], params.pbrDielectricF0Unclamped[1],
            params.pbrDielectricF0Unclamped[2], params.pbrSpecularFactor};
        TryUploadPixelShaderConstantEXT(
            device_.Get(), psRegs, psCount, "SpecularFresnelInputs", dielectricFresnel);
        const float specularMapFlags[4] = {
            params.pbrSpecularColorTextureIsSrgb ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
        TryUploadPixelShaderConstantEXT(
            device_.Get(), psRegs, psCount, "SpecularMapFlags", specularMapFlags);
        TryUploadPixelShaderConstantEXT(
            device_.Get(), psRegs, psCount, "TextureTransformRows",
            &params.pbrTextureTransformRows[0][0]);
        TryUploadPixelShaderConstantEXT(
            device_.Get(), psRegs, psCount, "SpecularTextureTransformRows",
            &params.pbrSpecularTextureTransformRows[0][0]);

        // Texture units: s0=base color, s1=NormalMap, s2=MetallicRoughnessMap, s3=EmissiveMap,
        // s4=OcclusionMap, s5=specular strength, s6=specular colour. Base color falls back to
        // opaque white (matches EasyGL's own
        // BindDrawParams() -- PbrEffect never requires a bound texture0, unlike the Stock Effects'
        // own DrawXxxEffectEXT "requires non-null texture0" checks).
        BindPbrSampler(device_.Get(), 0, params.texture0, ResolveD3D9TextureEXT(GetOrCreateDefaultWhiteTextureEXT()));
        BindPbrSampler(device_.Get(), 1, params.pbrNormalMap, ResolveD3D9TextureEXT(GetOrCreateDefaultFlatNormalTextureEXT()));
        BindPbrSampler(device_.Get(), 2, params.pbrMetallicRoughnessMap, ResolveD3D9TextureEXT(GetOrCreateDefaultWhiteTextureEXT()));
        BindPbrSampler(device_.Get(), 3, params.pbrEmissiveMap, ResolveD3D9TextureEXT(GetOrCreateDefaultWhiteTextureEXT()));
        BindPbrSampler(device_.Get(), 4, params.pbrOcclusionMap, ResolveD3D9TextureEXT(GetOrCreateDefaultWhiteTextureEXT()));
        BindPbrSampler(device_.Get(), 5, params.pbrSpecularMap, ResolveD3D9TextureEXT(GetOrCreateDefaultWhiteTextureEXT()));
        BindPbrSampler(device_.Get(), 6, params.pbrSpecularColorMap, ResolveD3D9TextureEXT(GetOrCreateDefaultWhiteTextureEXT()));

        device_->SetVertexDeclaration(GetOrCreateVertexDeclarationEXT(stride));
        const auto& d3dVb = static_cast<const D3D9VertexBufferRenderer&>(vb);
        device_->SetStreamSource(0, d3dVb.GetBufferEXT(), 0, static_cast<UINT>(stride));

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

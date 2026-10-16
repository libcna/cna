// SPDX-License-Identifier: MS-PL
// CNA-owned SM3 path for BasicEffect/SkinnedEffect's point/spot extension. Ordinary draws keep
// using Microsoft's untouched XNA bytecode; D3D9EffectDraw selects this only for an active lamp.

#include "CNA/Internal/Renderers/DirectX9/DirectX9Renderer.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9Buffers.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9ConstantUpload.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9RenderTargets.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9Textures.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9VertexDeclarations.hpp"
#include "D3D9ShadowUpload.hpp"
#include "shaders/D3D9CnaShaderRegisters.hpp"
#include "shaders/d3d9_pbr_shaders.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <algorithm>
#include <cstddef>
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
                "DirectX9 BasicEffect punctual draw does not support the requested PrimitiveType value");
        }

        std::string FormatHr(HRESULT hr)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
            return buf;
        }

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

        void UploadMatrixConstantVS(IDirect3DDevice9* device,
                                    const Shaders::D3D9ShaderConstantSlot* table, int count,
                                    const char* name, const Matrix& matrix)
        {
            const Matrix transposed = Matrix::Transpose(matrix);
            float registers[16];
            transposed.ToColumnMajor(registers);
            TryUploadVertexShaderConstantEXT(device, table, count, name, registers);
        }

        void UploadBonesVS(IDirect3DDevice9* device,
                           const Shaders::D3D9ShaderConstantSlot* table, int count,
                           const GpuDrawParams& params)
        {
            std::vector<float> registers(72 * 12, 0.0f);
            const int boneCount = std::min(params.boneCount, 72);
            for (int i = 0; i < boneCount; ++i)
            {
                const float* b = params.boneTransforms + i * 16;
                const Matrix bone(b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
                                  b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
                const Matrix transposed = Matrix::Transpose(bone);
                float full[16];
                transposed.ToColumnMajor(full);
                std::copy(full, full + 12,
                          registers.begin() + static_cast<std::ptrdiff_t>(i) * 12);
            }
            TryUploadVertexShaderConstantEXT(
                device, table, count, "Bones", registers.data());
        }
    }

    void DirectX9Renderer::DrawBasicPunctualEffectEXT(
        const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib, std::size_t stride,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        using namespace Shaders;

        ComPtr<IDirect3DVertexShader9>* vertexShader = nullptr;
        const uint8_t* vertexBytecode = nullptr;
        if (params.skinned && params.vertexColorEnabled)
        {
            vertexShader = &basicPunctualSkinnedColorVS_;
            vertexBytecode = kBasicPunctualSkinned3DColorVSBytecode;
        }
        else if (params.skinned)
        {
            vertexShader = &basicPunctualSkinnedVS_;
            vertexBytecode = kBasicPunctualSkinned3DVSBytecode;
        }
        else if (params.textureEnabled && params.vertexColorEnabled)
        {
            vertexShader = &basicPunctualTextureColorVS_;
            vertexBytecode = kBasicPunctual3DTextureColorVSBytecode;
        }
        else if (params.textureEnabled)
        {
            vertexShader = &basicPunctualTextureVS_;
            vertexBytecode = kBasicPunctual3DTextureVSBytecode;
        }
        else if (params.vertexColorEnabled)
        {
            vertexShader = &basicPunctualColorVS_;
            vertexBytecode = kBasicPunctual3DColorVSBytecode;
        }
        else
        {
            vertexShader = &basicPunctualVS_;
            vertexBytecode = kBasicPunctual3DVSBytecode;
        }
        if (!*vertexShader)
        {
            const HRESULT hr = device_->CreateVertexShader(
                reinterpret_cast<const DWORD*>(vertexBytecode), vertexShader->GetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error(
                    "DrawBasicPunctualEffectEXT: CreateVertexShader failed, hr=" + FormatHr(hr));
        }

        const bool wantsDirectionalShadow = params.shadowsEnabled && params.shadowMap != nullptr;
        const bool wantsPunctualShadow =
            (params.punctualKind == 1 && params.punctualShadowCube != nullptr) ||
            (params.punctualKind == 2 && params.punctualShadowMap != nullptr);
        const bool useShadowShader = SupportsShadowSamplingEXT() &&
                                     (wantsDirectionalShadow || wantsPunctualShadow);
        const bool useSpotShadowShader = useShadowShader && params.punctualKind == 2 &&
                                         params.punctualShadowMap != nullptr;
        const bool useSpotOnlyShadowShader = useSpotShadowShader && !wantsDirectionalShadow;
        ComPtr<IDirect3DPixelShader9>* pixelShader = nullptr;
        const uint8_t* pixelBytecode = nullptr;
        if (useShadowShader && params.textureEnabled)
        {
            pixelShader = useSpotShadowShader
                ? (useSpotOnlyShadowShader ? &basicPunctualTextureSpotOnlyShadowPS_
                                           : &basicPunctualTextureSpotDirectionalShadowPS_)
                : &basicPunctualTexturePointShadowPS_;
            pixelBytecode = useSpotShadowShader
                ? (useSpotOnlyShadowShader ? kBasicPunctual3DTextureSpotOnlyShadowPSBytecode
                                            : kBasicPunctual3DTextureSpotDirectionalShadowPSBytecode)
                : kBasicPunctual3DTexturePointShadowPSBytecode;
        }
        else if (useShadowShader)
        {
            pixelShader = useSpotShadowShader
                ? (useSpotOnlyShadowShader ? &basicPunctualSpotOnlyShadowPS_
                                           : &basicPunctualSpotDirectionalShadowPS_)
                : &basicPunctualPointShadowPS_;
            pixelBytecode = useSpotShadowShader
                ? (useSpotOnlyShadowShader ? kBasicPunctual3DSpotOnlyShadowPSBytecode
                                            : kBasicPunctual3DSpotDirectionalShadowPSBytecode)
                : kBasicPunctual3DPointShadowPSBytecode;
        }
        else if (params.textureEnabled)
        {
            pixelShader = &basicPunctualTexturePS_;
            pixelBytecode = kBasicPunctual3DTexturePSBytecode;
        }
        else
        {
            pixelShader = &basicPunctualPS_;
            pixelBytecode = kBasicPunctual3DPSBytecode;
        }
        if (!*pixelShader)
        {
            const HRESULT hr = device_->CreatePixelShader(
                reinterpret_cast<const DWORD*>(pixelBytecode), pixelShader->GetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error(
                    "DrawBasicPunctualEffectEXT: CreatePixelShader failed, hr=" + FormatHr(hr));
        }

        device_->SetVertexShader(vertexShader->Get());
        device_->SetPixelShader(pixelShader->Get());

        const auto* vsRegs = params.skinned
            ? kBasicPunctualSkinned3DVS_Registers : kBasicPunctual3DVS_Registers;
        const int vsCount = params.skinned
            ? kBasicPunctualSkinned3DVS_RegistersCount : kBasicPunctual3DVS_RegistersCount;
        const auto* psRegs = useSpotOnlyShadowShader
            ? kBasicPunctual3DSpotOnlyShadowPS_Registers
            : (useSpotShadowShader
                   ? kBasicPunctual3DShadowPS_Registers
                   : (useShadowShader ? kBasicPunctual3DPointShadowPS_Registers
                                      : kBasicPunctual3DPS_Registers));
        const int psCount = useSpotOnlyShadowShader
            ? kBasicPunctual3DSpotOnlyShadowPS_RegistersCount
            : (useSpotShadowShader
                   ? kBasicPunctual3DShadowPS_RegistersCount
                   : (useShadowShader ? kBasicPunctual3DPointShadowPS_RegistersCount
                                      : kBasicPunctual3DPS_RegistersCount));

        UploadMatrixConstantVS(device_.Get(), vsRegs, vsCount, "WorldViewProj",
                               world * view * projection);
        UploadMatrixConstantVS(device_.Get(), vsRegs, vsCount, "World", world);
        const Matrix worldInverseTranspose = Matrix::Transpose(Matrix::Invert(world));
        UploadMatrixConstantVS(device_.Get(), vsRegs, vsCount, "WorldInverseTranspose",
                               worldInverseTranspose);
        TryUploadVertexShaderConstantEXT(
            device_.Get(), vsRegs, vsCount, "FogVector", params.fogVector);
        if (params.skinned)
        {
            UploadBonesVS(device_.Get(), vsRegs, vsCount, params);
            const float skinningParams[4] = {
                static_cast<float>(params.weightsPerVertex), 0.0f, 0.0f, 0.0f};
            TryUploadVertexShaderConstantEXT(
                device_.Get(), vsRegs, vsCount, "SkinningParams", skinningParams);
        }

        TryUploadPixelShaderConstantEXT(
            device_.Get(), psRegs, psCount, "DiffuseColor", params.diffuseColor);
        const float emissiveFolded[4] = {
            params.emissiveColor[0] + params.ambientColor[0] * params.diffuseColor[0],
            params.emissiveColor[1] + params.ambientColor[1] * params.diffuseColor[1],
            params.emissiveColor[2] + params.ambientColor[2] * params.diffuseColor[2], 0.0f};
        TryUploadPixelShaderConstantEXT(
            device_.Get(), psRegs, psCount, "EmissiveColor", emissiveFolded);
        const float specularColorPower[4] = {
            params.specularColor[0], params.specularColor[1], params.specularColor[2],
            params.specularPower};
        TryUploadPixelShaderConstantEXT(
            device_.Get(), psRegs, psCount, "SpecularColorPower", specularColorPower);
        TryUploadPixelShaderConstantEXT(
            device_.Get(), psRegs, psCount, "EyePosition", Pad3(params.eyePositionWorld).v);

        const float* directions[] = {params.light0Dir, params.light1Dir, params.light2Dir};
        const float* diffuse[] = {params.light0Diffuse, params.light1Diffuse, params.light2Diffuse};
        const float* specular[] = {params.light0Specular, params.light1Specular, params.light2Specular};
        const char* directionNames[] = {"Light0Direction", "Light1Direction", "Light2Direction"};
        const char* diffuseNames[] = {"Light0Diffuse", "Light1Diffuse", "Light2Diffuse"};
        const char* specularNames[] = {"Light0Specular", "Light1Specular", "Light2Specular"};
        for (int i = 0; i < 3; ++i)
        {
            TryUploadPixelShaderConstantEXT(
                device_.Get(), psRegs, psCount, directionNames[i], Pad3(directions[i]).v);
            TryUploadPixelShaderConstantEXT(
                device_.Get(), psRegs, psCount, diffuseNames[i], Pad3(diffuse[i]).v);
            TryUploadPixelShaderConstantEXT(
                device_.Get(), psRegs, psCount, specularNames[i], Pad3(specular[i]).v);
        }

        const float punctualPositionRange[4] = {
            params.punctualPosition[0], params.punctualPosition[1],
            params.punctualPosition[2], params.punctualRange};
        const float punctualDirectionKind[4] = {
            params.punctualDirection[0], params.punctualDirection[1],
            params.punctualDirection[2], static_cast<float>(params.punctualKind)};
        const float punctualDiffuse[4] = {
            params.punctualDiffuse[0], params.punctualDiffuse[1],
            params.punctualDiffuse[2], 0.0f};
        const float punctualCone[4] = {
            params.punctualCosInner, params.punctualCosOuter, 0.0f, 0.0f};
        TryUploadPixelShaderConstantEXT(
            device_.Get(), psRegs, psCount, "PunctualPositionRange", punctualPositionRange);
        TryUploadPixelShaderConstantEXT(
            device_.Get(), psRegs, psCount, "PunctualDirectionKind", punctualDirectionKind);
        TryUploadPixelShaderConstantEXT(
            device_.Get(), psRegs, psCount, "PunctualDiffuse", punctualDiffuse);
        TryUploadPixelShaderConstantEXT(
            device_.Get(), psRegs, psCount, "PunctualCone", punctualCone);
        TryUploadPixelShaderConstantEXT(
            device_.Get(), psRegs, psCount, "FogColor", Pad3(params.fogColor).v);

        if (params.textureEnabled)
            device_->SetTexture(0, ResolveD3D9TextureEXT(params.texture0));
        if (useShadowShader)
            ShadowUploadDetail::UploadAndBind(device_.Get(), psRegs, psCount, params);

        device_->SetVertexDeclaration(GetOrCreateVertexDeclarationEXT(vb, stride));
        const auto& d3dVb = static_cast<const D3D9VertexBufferRenderer&>(vb);
        device_->SetStreamSource(0, d3dVb.GetBufferEXT(), 0, static_cast<UINT>(stride));
        if (ib)
        {
            const auto& d3dIb = static_cast<const D3D9IndexBufferRenderer&>(*ib);
            device_->SetIndices(d3dIb.GetBufferEXT());
            device_->DrawIndexedPrimitive(
                ToD3D9Topology(primitive), static_cast<INT>(params.baseVertex), 0,
                static_cast<UINT>(d3dVb.GetVertexCount() - params.baseVertex),
                static_cast<UINT>(params.startIndex), static_cast<UINT>(primitiveCount));
        }
        else
        {
            device_->DrawPrimitive(ToD3D9Topology(primitive),
                                   static_cast<UINT>(params.vertexStart),
                                   static_cast<UINT>(primitiveCount));
        }
    }
}

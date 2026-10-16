#pragma once

#include "CNA/Internal/Renderers/DirectX9/D3D9ConstantUpload.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9RenderTargets.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9Textures.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "shaders/D3D9CnaShaderRegisters.hpp"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace CNA::Internal::Renderers::DirectX9::ShadowUploadDetail
{
    using Microsoft::Xna::Framework::Matrix;

    inline IDirect3DTexture9* ResolveTexture2D(const ITextureRenderer* texture)
    {
        if (const auto* plain = dynamic_cast<const D3D9TextureRenderer*>(texture))
            return plain->GetTextureEXT();
        if (const auto* target = dynamic_cast<const D3D9RenderTargetRenderer*>(texture))
            return target->GetTextureEXT();
        return nullptr;
    }

    inline IDirect3DCubeTexture9* ResolveTextureCube(const ITextureCubeRenderer* texture)
    {
        if (const auto* plain = dynamic_cast<const D3D9TextureCubeRenderer*>(texture))
            return plain->GetTextureEXT();
        if (const auto* target = dynamic_cast<const D3D9RenderTargetCubeRenderer*>(texture))
            return target->GetTextureEXT();
        return nullptr;
    }

    inline Matrix MatrixFromGpuParams(const float* values)
    {
        return Matrix(values[0], values[1], values[2], values[3],
                      values[4], values[5], values[6], values[7],
                      values[8], values[9], values[10], values[11],
                      values[12], values[13], values[14], values[15]);
    }

    inline void UploadMatrixPS(IDirect3DDevice9* device,
                               const Shaders::D3D9ShaderConstantSlot* table, int count,
                               const char* name, const float* values)
    {
        const Matrix transposed = Matrix::Transpose(MatrixFromGpuParams(values));
        float registers[16];
        transposed.ToColumnMajor(registers);
        TryUploadPixelShaderConstantEXT(device, table, count, name, registers);
    }

    inline void UploadMatrixArrayPS(IDirect3DDevice9* device,
                                    const Shaders::D3D9ShaderConstantSlot* table, int count,
                                    const char* name, const float* values, int matrixCount)
    {
        std::vector<float> registers(static_cast<std::size_t>(matrixCount) * 16);
        for (int i = 0; i < matrixCount; ++i)
        {
            const Matrix transposed = Matrix::Transpose(MatrixFromGpuParams(values + i * 16));
            transposed.ToColumnMajor(registers.data() + i * 16);
        }
        TryUploadPixelShaderConstantEXT(device, table, count, name, registers.data());
    }

    inline void ConfigureShadowSampler(IDirect3DDevice9* device, DWORD stage)
    {
        device->SetSamplerState(stage, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        device->SetSamplerState(stage, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        device->SetSamplerState(stage, D3DSAMP_ADDRESSW, D3DTADDRESS_CLAMP);
        device->SetSamplerState(stage, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        device->SetSamplerState(stage, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        device->SetSamplerState(stage, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    }

    inline void UploadAndBind(IDirect3DDevice9* device,
                              const Shaders::D3D9ShaderConstantSlot* table, int count,
                              const GpuDrawParams& params)
    {
        IDirect3DTexture9* directional = ResolveTexture2D(params.shadowMap);
        const bool directionalEnabled = params.shadowsEnabled && directional != nullptr;
        const int cascadeCount = directionalEnabled ? std::clamp(params.cascadeCount, 0, 4) : 0;
        const float shadowParams[4] = {
            directionalEnabled ? 1.0f : 0.0f,
            params.shadowDepthBias,
            static_cast<float>(std::clamp(params.shadowPcfRadius, 0, 2)),
            static_cast<float>(cascadeCount)};

        float inverseWidth = 0.0f;
        float inverseHeight = 0.0f;
        if (directionalEnabled && params.shadowMap->GetWidth() > 0 && params.shadowMap->GetHeight() > 0)
        {
            inverseWidth = 1.0f / static_cast<float>(params.shadowMap->GetWidth());
            inverseHeight = 1.0f / static_cast<float>(params.shadowMap->GetHeight());
        }
        const float shadowTexelBlendDebug[4] = {
            inverseWidth, inverseHeight, params.cascadeBlendBand,
            params.cascadeDebugTint ? 1.0f : 0.0f};

        IDirect3DCubeTexture9* punctualCube = ResolveTextureCube(params.punctualShadowCube);
        IDirect3DTexture9* punctualMap = ResolveTexture2D(params.punctualShadowMap);
        const bool pointShadow = params.punctualKind == 1 && punctualCube != nullptr;
        const bool spotShadow = params.punctualKind == 2 && punctualMap != nullptr;
        float punctualInverseWidth = 0.0f;
        float punctualInverseHeight = 0.0f;
        if (spotShadow && params.punctualShadowMap->GetWidth() > 0 &&
            params.punctualShadowMap->GetHeight() > 0)
        {
            punctualInverseWidth = 1.0f / static_cast<float>(params.punctualShadowMap->GetWidth());
            punctualInverseHeight = 1.0f / static_cast<float>(params.punctualShadowMap->GetHeight());
        }
        const float punctualShadowParams[4] = {
            (pointShadow || spotShadow) ? 1.0f : 0.0f,
            params.punctualShadowBias, punctualInverseWidth, punctualInverseHeight};

        UploadMatrixPS(device, table, count, "CnaLightViewProj", params.lightViewProjColMajor);
        UploadMatrixArrayPS(device, table, count, "CnaCascadeMatrices",
                            params.cascadeMatricesColMajor, 4);
        UploadMatrixPS(device, table, count, "CnaPunctualViewProj",
                       params.punctualViewProjColMajor);
        TryUploadPixelShaderConstantEXT(device, table, count, "CnaShadowParams", shadowParams);
        TryUploadPixelShaderConstantEXT(device, table, count, "CnaShadowTexelBlendDebug",
                                        shadowTexelBlendDebug);
        TryUploadPixelShaderConstantEXT(device, table, count, "CnaCascadeSplits",
                                        params.cascadeSplits);
        TryUploadPixelShaderConstantEXT(device, table, count, "CnaCascadeViewZ",
                                        params.cascadeViewZRow);
        TryUploadPixelShaderConstantEXT(device, table, count, "CnaPunctualShadowParams",
                                        punctualShadowParams);

        IDirect3DBaseTexture9* stage7 = spotShadow
            ? static_cast<IDirect3DBaseTexture9*>(punctualMap)
            : static_cast<IDirect3DBaseTexture9*>(directional);
        IDirect3DBaseTexture9* stage8 = spotShadow
            ? static_cast<IDirect3DBaseTexture9*>(directional)
            : static_cast<IDirect3DBaseTexture9*>(pointShadow ? punctualCube : nullptr);
        if (FAILED(device->SetTexture(7, stage7)))
            throw std::runtime_error("DirectX9 shadow draw could not bind shadow sampler s7");
        if (FAILED(device->SetTexture(8, stage8)))
            throw std::runtime_error("DirectX9 shadow draw could not bind shadow sampler s8");
        ConfigureShadowSampler(device, 7);
        ConfigureShadowSampler(device, 8);
    }
}

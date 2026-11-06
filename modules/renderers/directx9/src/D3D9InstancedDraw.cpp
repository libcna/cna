// SPDX-License-Identifier: MS-PL
// DX9 Phase D9-8 (D9-83): real hardware instancing via SetStreamSourceFreq. Uses CNA's own
// CNAEXT "Instanced3D" shader (shaders/cna/Instanced3D.hlsl, embedded bytecode in
// shaders/d3d9_instanced3d_shader.hpp) -- NOT one of Microsoft's real Stock Effects.
//
// Real XNA 4.0 has no per-instance-aware Stock Effect vertex shader at all: BasicEffect/
// AlphaTestEffect/DualTextureEffect/EnvironmentMapEffect/SkinnedEffect's real VSInput* structs
// (transcribed verbatim from Structures.fxh across D9-82b-f) declare no per-instance semantic.
// Real XNA hardware instancing requires the game author's own custom Effect with a custom vertex
// shader that reads the extra per-instance stream -- there is no "instanced BasicEffect" in real
// XNA. This shader is CNA's own minimal stand-in for that custom-Effect role, the same choice the
// retired D3D11, Vulkan and Bgfx renderers made (each authored its own equivalent "instanced3d"
// shader rather than attempting to instance a stock effect) -- so this file does NOT dispatch
// through D3D9EffectDraw.cpp's DrawPrimitivesExImpl(), by design.

#include "CNA/Internal/Renderers/DirectX9/DirectX9Renderer.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9Buffers.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9VertexStreams.hpp"
#include "shaders/d3d9_instanced3d_shader.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
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

    void DirectX9Renderer::DrawInstancedPrimitivesEx(
        const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, int instanceCount, const GpuDrawParams& params)
    {
        // D9-83: without a per-instance stream D3D9 has nothing to drive SetStreamSourceFreq with, so
        // the instance count is honoured the way XNA's own fallback does: the same draw repeated.
        const auto* instanceStream = FirstInstanceStream(params);
        if (instanceStream == nullptr)
        {
            for (int instance = 0; instance < std::max(1, instanceCount); ++instance)
                DrawIndexedPrimitivesEx(vb, ib, world, view, projection, primitive, primitiveCount, params);
            return;
        }

        EnsureRenderReadyEXT();

        const auto& d3dVb     = static_cast<const D3D9VertexBufferRenderer&>(vb);
        const auto& d3dIb     = static_cast<const D3D9IndexBufferRenderer&>(ib);
#if defined(CNA_DIRECTX9_COMPILED_EFFECTS)
        if (params.compiledEffectRuntime != nullptr)
        {
            DrawCompiledEffectEXT(vb, &ib, primitive, primitiveCount,
                                  std::max(1, instanceCount), params,
                                  *params.compiledEffectRuntime);
            return;
        }
#endif
        D3D9VertexStreamsEXT streamScope(*this, vb, params, caps_.MaxStreams);
        if (params.skinned || params.pbr)
        {
            // CNAEXT matrix instancing: XNA stock shaders have no instance attributes. Apply
            // each authored world matrix through the real stock/PBR draw so skinning, normals,
            // textures and lighting keep their effect semantics. Read our upload snapshot because
            // D3D9 buffers are WRITEONLY; locking them for readback is not a valid contract.
            // Rate arithmetic matches Refresh DrawInstancedPrimitives IL_02cf-02e0.
            GpuDrawParams geometry = params;
            geometry.vertexStreamCount = 0;
            geometry.instanceCount = 1;
            geometry.firstInstance = 0;
            for (int stream = 0; stream < params.vertexStreamCount; ++stream)
            {
                const auto& source = params.vertexStreams[stream];
                if (source.instanceFrequency == 0)
                    geometry.vertexStreams[geometry.vertexStreamCount++] = source;
            }
            std::vector<Matrix> transforms;
            transforms.reserve(static_cast<std::size_t>(instanceCount));
            for (int instance = 0; instance < instanceCount; ++instance)
            {
                float matrix[16]{};
                int row = 0;
                for (int stream = 0; stream < params.vertexStreamCount; ++stream)
                {
                    const auto& source = params.vertexStreams[stream];
                    if (source.instanceFrequency <= 0) continue;
                    const auto* buffer = dynamic_cast<const D3D9VertexBufferRenderer*>(source.buffer);
                    if (!buffer || source.vertexOffset < 0 || source.strideInBytes <= 0)
                        throw System::NotSupportedException("DirectX9: invalid instance matrix stream.");
                    const auto bytes = buffer->GetCpuDataEXT();
                    const std::uint64_t record = static_cast<std::uint64_t>(source.vertexOffset) +
                        (static_cast<std::uint64_t>(params.firstInstance) + instance) / source.instanceFrequency;
                    if (record > std::numeric_limits<std::size_t>::max() / source.strideInBytes)
                        throw System::NotSupportedException("DirectX9: instance matrix offset overflow.");
                    const std::size_t start = static_cast<std::size_t>(record) * source.strideInBytes;
                    const auto copyRow = [&](int offset)
                    {
                        if (row >= 4 || offset < 0 || offset > source.strideInBytes ||
                            source.strideInBytes - offset < 16 || start > bytes.size() ||
                            static_cast<std::size_t>(offset) > bytes.size() - start ||
                            bytes.size() - start - offset < 16)
                            throw System::NotSupportedException("DirectX9: incomplete instance matrix stream.");
                        std::memcpy(matrix + row++ * 4, bytes.data() + start + offset, 16);
                    };
                    const auto& elements = buffer->GetDeclarationEXT().GetElements();
                    if (elements.empty() && source.strideInBytes == 64)
                        for (int offset = 0; offset < 64; offset += 16) copyRow(offset);
                    else
                        for (const auto& element : elements)
                        {
                            if (element.getVertexElementFormatProperty() !=
                                Microsoft::Xna::Framework::Graphics::VertexElementFormat::Vector4)
                                throw System::NotSupportedException("DirectX9: instance matrix rows require Vector4.");
                            copyRow(element.getOffsetProperty());
                        }
                }
                if (row != 4)
                    throw System::NotSupportedException("DirectX9: instance matrix needs four rows.");
                transforms.emplace_back(matrix[0],matrix[1],matrix[2],matrix[3],
                    matrix[4],matrix[5],matrix[6],matrix[7],matrix[8],matrix[9],matrix[10],matrix[11],
                    matrix[12],matrix[13],matrix[14],matrix[15]);
            }
            for (const auto& transform : transforms)
                DrawPrimitivesExImpl(vb, &ib, world * transform, view, projection,
                                     primitive, primitiveCount, geometry);
            return;
        }

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
        // other draw path in this renderer) the `world` parameter is deliberately unused here.
        // Same register=column transpose convention used everywhere else in this renderer.
        const Matrix vpT = Matrix::Transpose(view * projection);
        float vpRegs[16];
        vpT.ToColumnMajor(vpRegs);
        device_->SetVertexShaderConstantF(0, vpRegs, 4);
        device_->SetVertexShaderConstantF(4, params.diffuseColor, 1);

        streamScope.Bind(GetOrCreateStreamDeclarationEXT(vb, params, true), instanceCount);
        device_->SetIndices(d3dIb.GetBufferEXT());

        // BaseVertex/startIndex address geometry only. The binding scope applies every
        // independent stream offset; native instance frequencies never add baseVertex to matrices.
        device_->DrawIndexedPrimitive(ToD3D9Topology(primitive), static_cast<INT>(params.baseVertex), static_cast<UINT>(params.minVertexIndex),
                                      static_cast<UINT>(params.numVertices > 0 ? params.numVertices :
                                              d3dVb.GetVertexCount() - params.baseVertex),
                                      static_cast<UINT>(params.startIndex), static_cast<UINT>(primitiveCount));

        // streamScope restores every stream on success and exception (Refresh IL_01dc-0215).
    }
}

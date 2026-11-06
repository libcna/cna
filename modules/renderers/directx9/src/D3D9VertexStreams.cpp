// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/DirectX9/DirectX9Renderer.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9VertexStreams.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9Buffers.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9VertexDeclarations.hpp"
#include "System/NotSupportedException.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <string>

namespace CNA::Internal::Renderers::DirectX9
{
    D3D9VertexStreamsEXT::D3D9VertexStreamsEXT(DirectX9Renderer& owner, const IVertexBufferRenderer& fallback,
                         const GpuDrawParams& params, UINT maxStreams)
        : owner_(owner), device_(owner.GetDeviceEXT())
    {
        if (params.vertexStreamCount < 0 || params.vertexStreamCount > kMaxVertexStreams)
            Refuse();
        maxStreams_ = std::min<UINT>(maxStreams, kMaxVertexStreams);
        const auto add = [&](const IVertexBufferRenderer* source, int slot, int stride,
                             int offset, int frequency)
        {
            if (!source || slot < 0 || static_cast<UINT>(slot) >= maxStreams_ ||
                stride <= 0 || offset < 0 || frequency < 0 ||
                static_cast<std::uint64_t>(offset) * stride > std::numeric_limits<UINT>::max() ||
                std::any_of(streams.begin(), streams.end(),
                    [&](const Stream& s) { return s.slot == static_cast<UINT>(slot); }))
                Refuse();
            const auto* buffer = dynamic_cast<const D3D9VertexBufferRenderer*>(source);
            if (!buffer || !buffer->GetBufferEXT()) Refuse();
            streams.push_back({buffer, static_cast<UINT>(slot), static_cast<UINT>(stride),
                static_cast<UINT>(offset) * static_cast<UINT>(stride),
                static_cast<UINT>(frequency)});
        };
        if (params.vertexStreamCount == 0)
        {
            const auto& buffer = static_cast<const D3D9VertexBufferRenderer&>(fallback);
            add(&fallback, 0, static_cast<int>(buffer.GetStrideEXT()), 0, 0);
        }
        else
            for (int i = 0; i < params.vertexStreamCount; ++i)
            {
                const auto& s = params.vertexStreams[i];
                add(s.buffer, s.slot, s.strideInBytes, s.vertexOffset, s.instanceFrequency);
            }
    }

    D3D9VertexStreamsEXT::~D3D9VertexStreamsEXT()
    {
        if (!bound_) return;
        // Refresh GraphicsDevice.DrawInstancedPrimitives IL_01dc-0215: finally resets ALL rates.
        for (const auto& s : streams)
        {
            device_->SetStreamSourceFreq(s.slot, 1);
            device_->SetStreamSource(s.slot, nullptr, 0, 0);
        }
    }

    std::vector<D3DVERTEXELEMENT9> D3D9VertexStreamsEXT::Elements(bool instanceMatrix) const
    {
        std::vector<D3DVERTEXELEMENT9> result;
        unsigned matrixRow = 0;
        for (const auto& s : streams)
        {
            auto elements = TranslateVertexElementsD3D9(s.buffer->GetDeclarationEXT().GetElements());
            elements.pop_back();
            if (elements.empty())
            {
                if (instanceMatrix && s.frequency > 0 && s.stride == 64)
                    for (WORD row = 0; row < 4; ++row)
                        elements.push_back({0, static_cast<WORD>(row * 16), D3DDECLTYPE_FLOAT4,
                            D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, static_cast<BYTE>(row + 1)});
                else if (instanceMatrix && s.frequency == 0 && s.stride >= 12)
                {
                    // Legacy internal instanced buffers carry a float3 position at offset zero,
                    // including the position-only stride12 used by DirectX9_Instanced.
                    elements.push_back({0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT,
                        D3DDECLUSAGE_POSITION, 0});
                }
                else
                {
                    UINT count = 0;
                    const auto* legacy = VertexElementsForStrideD3D9(s.stride, count);
                    if (!legacy) Refuse();
                    elements.assign(legacy, legacy + count);
                }
            }
            for (auto e : elements)
            {
                if (instanceMatrix)
                {
                    if (s.frequency > 0)
                    {
                        if (e.Type != D3DDECLTYPE_FLOAT4 || matrixRow >= 4) Refuse();
                        e.Usage = D3DDECLUSAGE_TEXCOORD;
                        e.UsageIndex = static_cast<BYTE>(++matrixRow);
                    }
                    else if (e.Usage != D3DDECLUSAGE_POSITION || e.UsageIndex != 0)
                        continue; // Instanced3D only consumes Position0 from geometry.
                }
                // Refresh DeclarationManager.CreateNativeDeclaration IL_00c9-00dc,011c-0191:
                // retain local offset, assign binding slot, increment colliding usage indices.
                e.Stream = static_cast<WORD>(s.slot);
                const auto duplicate = [&] {
                    return std::any_of(result.begin(), result.end(), [&](const auto& prior) {
                        return e.Usage == prior.Usage && e.UsageIndex == prior.UsageIndex;
                    });
                };
                while (duplicate())
                {
                    if (e.UsageIndex >= 15) Refuse();
                    ++e.UsageIndex;
                }
                result.push_back(e);
            }
        }
        if (instanceMatrix && matrixRow != 4) Refuse();
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
            return a.Stream < b.Stream || (a.Stream == b.Stream && a.Offset < b.Offset);
        });
        result.push_back(D3DDECL_END());
        return result;
    }

    void D3D9VertexStreamsEXT::Bind(IDirect3DVertexDeclaration9* declaration, int instances)
    {
        bound_ = true; // set before the first native call, covering partial binding failures
        for (UINT slot = 0; slot < maxStreams_; ++slot)
            if (std::none_of(streams.begin(), streams.end(),
                    [&](const Stream& s) { return s.slot == slot; }))
            {
                Check(device_->SetStreamSourceFreq(slot, 1));
                Check(device_->SetStreamSource(slot, nullptr, 0, 0));
            }
        if (declaration) Check(device_->SetVertexDeclaration(declaration));
        for (const auto& s : streams)
        {
            // Refresh SetVertexBuffers IL_00f5-0121: byte offset = this stride * this offset.
            const UINT frequency = instances > 0
                ? (s.frequency > 0
                    ? D3DSTREAMSOURCE_INSTANCEDATA | std::min<UINT>(s.frequency, 0x3fffffff)
                    : D3DSTREAMSOURCE_INDEXEDDATA | static_cast<UINT>(instances))
                : 1u;
            Check(device_->SetStreamSourceFreq(s.slot, frequency));
            ++owner_.nativeCallCounts_[static_cast<std::size_t>(D3D9NativeOperationEXT::SetStreamSource)];
            const HRESULT injected = owner_.TakeInjectedResultEXT(D3D9NativeOperationEXT::SetStreamSource);
            Check(FAILED(injected) ? injected :
                device_->SetStreamSource(s.slot, s.buffer->GetBufferEXT(), s.offset, s.stride));
        }
    }

    [[noreturn]] void D3D9VertexStreamsEXT::Refuse()
    {
        throw System::NotSupportedException("DirectX9: invalid or unrepresentable vertex streams.");
    }

    void D3D9VertexStreamsEXT::Check(HRESULT hr)
    {
        if (FAILED(hr)) throw std::runtime_error("DirectX9: vertex stream binding failed, HRESULT=" +
            std::to_string(static_cast<unsigned long>(hr)));
    }

    bool DirectX9Renderer::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        return capability == CNA::GraphicsCapability::MultiStreamVertexInput
            ? caps_.MaxStreams > 1 : IGraphicsRenderer::SupportsCapability(capability);
    }

    int DirectX9Renderer::GetMaxVertexStreams() const
    {
        return caps_.MaxStreams < kMaxVertexStreams ? static_cast<int>(caps_.MaxStreams) : kMaxVertexStreams;
    }

    IDirect3DVertexDeclaration9* DirectX9Renderer::GetOrCreateStreamDeclarationEXT(
        const IVertexBufferRenderer& buffer, const GpuDrawParams& params, bool instanceMatrix)
    {
        D3D9VertexStreamsEXT streams(*this, buffer, params, caps_.MaxStreams);
        const auto elements = streams.Elements(instanceMatrix);
        static_assert(sizeof(D3DVERTEXELEMENT9) == sizeof(std::uint64_t));
        std::vector<std::uint64_t> key(elements.size());
        std::memcpy(key.data(), elements.data(), elements.size() * sizeof(D3DVERTEXELEMENT9));
        if (const auto it = streamDeclCache_.find(key); it != streamDeclCache_.end())
            return it->second.Get();
        ComPtr<IDirect3DVertexDeclaration9> declaration;
        const HRESULT hr = device_->CreateVertexDeclaration(elements.data(), declaration.GetAddressOf());
        if (FAILED(hr)) throw std::runtime_error("DirectX9: combined CreateVertexDeclaration failed.");
        auto* result = declaration.Get();
        streamDeclCache_.emplace(std::move(key), std::move(declaration));
        return result;
    }
}

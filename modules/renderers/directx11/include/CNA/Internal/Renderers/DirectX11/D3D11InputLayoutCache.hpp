#pragma once

// plans/plan_dx.md Phase DIRECTX5 (DX-32): wires DX-16-vtx's stride-keyed D3D11_INPUT_ELEMENT_DESC inference
// (D3DVertexFormatHelper) together with DX-15-embed's per-variant vertex shader bytecode
// (D3DShaderCache) into real ID3D11InputLayout objects, cached per (shader variant, stride) pair
// -- CreateInputLayout() is a real, non-trivial device call (it validates the element array
// against the vertex shader's own input signature), so this is deliberately not a bare struct
// lookup; each entry is created once and reused.
//
// ID3D11InputLayout is itself a D3D11-only COM type (D3D12 has no equivalent object -- input
// layout is folded directly into a D3D12_GRAPHICS_PIPELINE_STATE_DESC), so unlike
// D3DVertexFormatHelper/D3DShaderCache this cache class lives in D3D11, not D3DCommon
// (design decision 4: D3DCommon only holds what's genuinely shared).

#include "CNA/Internal/Renderers/D3DCommon/D3DShaderCache.hpp"

#include <d3d11.h>
#include <wrl/client.h>

#include <cstddef>
#include <map>
#include <utility>

namespace CNA::Internal::Renderers::DirectX11
{
    using Microsoft::WRL::ComPtr;
    using CNA::Internal::Renderers::D3DCommon::D3DShaderVariant;

    /// Caches ID3D11InputLayout objects keyed by (D3DShaderVariant, vertex stride in bytes).
    /// Thread-unsafe by design, same as every other per-device D3D11 renderer object in this
    /// project -- GraphicsDevice usage is single-threaded (matches the existing Vulkan/EasyGL
    /// renderer precedent).
    class D3D11InputLayoutCache
    {
    public:
        /// Returns the cached layout for (variant, strideInBytes), creating it on first request.
        /// Returns a null ComPtr (does not throw) if the stride isn't one of DX-16-vtx's 5
        /// established strides, or if CreateInputLayout() itself fails -- callers check the
        /// returned ComPtr, matching D3DShaderCache's own error-handling convention.
        ComPtr<ID3D11InputLayout> GetOrCreate(ID3D11Device* device, D3DShaderVariant variant,
                                              std::size_t strideInBytes);

        /// Drops every cached layout (CNAEXT -- device-lost recovery, DX-27, will need this once
        /// it does full three-lifetime-group teardown/recreation).
        void Clear() { cache_.clear(); }

    private:
        std::map<std::pair<int, std::size_t>, ComPtr<ID3D11InputLayout>> cache_;
    };
}

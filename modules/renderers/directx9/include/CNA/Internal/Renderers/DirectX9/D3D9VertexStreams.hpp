// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include <d3d9.h>
#include <vector>

namespace CNA::Internal::Renderers::DirectX9
{
    class D3D9VertexBufferRenderer;
    class DirectX9Renderer;

    /** @brief Resolves immutable draw bindings and restores native stream state on scope exit. */
    class D3D9VertexStreamsEXT final
    {
    public:
        /**
         * @brief Validates every stream before changing native state.
         * @param owner Renderer receiving the draw.
         * @param fallback Geometry buffer for legacy calls without a binding snapshot.
         * @param params Immutable per-draw binding snapshot.
         * @param maxStreams Native device stream limit.
         */
        D3D9VertexStreamsEXT(DirectX9Renderer& owner, const IVertexBufferRenderer& fallback,
                             const GpuDrawParams& params, UINT maxStreams);
        /** @brief Binding scopes cannot be copied. */
        D3D9VertexStreamsEXT(const D3D9VertexStreamsEXT&) = delete;
        /** @brief Binding scopes cannot be assigned. */
        D3D9VertexStreamsEXT& operator=(const D3D9VertexStreamsEXT&) = delete;
        /** @brief Restores frequencies and detaches buffers, including after a binding failure. */
        ~D3D9VertexStreamsEXT();

        /**
         * @brief Composes declarations across the binding slots.
         * @param instanceMatrix Whether instance elements supply CNAEXT Instanced3D matrix rows.
         * @return Native elements followed by D3DDECL_END.
         */
        [[nodiscard]] std::vector<D3DVERTEXELEMENT9> Elements(bool instanceMatrix = false) const;
        /**
         * @brief Binds sources with their independent offsets and input rates.
         * @param declaration Native declaration, or null when the caller already bound one.
         * @param instances Instance count; zero selects ordinary per-vertex input.
         */
        void Bind(IDirect3DVertexDeclaration9* declaration, int instances = 0);

    private:
        struct Stream
        {
            const D3D9VertexBufferRenderer* buffer;
            UINT slot, stride, offset, frequency;
        };
        [[noreturn]] static void Refuse();
        static void Check(HRESULT hr);
        std::vector<Stream> streams;
        DirectX9Renderer& owner_;
        IDirect3DDevice9* device_;
        UINT maxStreams_ = 0;
        bool bound_ = false;
    };
}

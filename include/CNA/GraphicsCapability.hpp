#pragma once

namespace CNA
{
    /**
     * @brief Identifies a graphics feature whose support genuinely varies across CNA backends
     *        (and, for some entries, across devices/drivers within the same backend).
     *
     * Query via GraphicsDevice::SupportsCapability()/IGraphicsBackend::SupportsCapability()
     * before relying on the corresponding feature, instead of calling it and handling the
     * resulting exception. Every entry here maps to a real, already-documented gap somewhere in
     * this codebase (see each value's own comment) -- this is not a speculative capability list.
     */
    enum class GraphicsCapability
    {
        /**
         * @brief The 3D pipeline as a whole (vertex/index buffers, 3D draw calls, depth/stencil
         * clears and state). SDL_Renderer/DX3/Canvas are intentionally 2D-only and lack this
         * entirely; every other backend supports it.
         */
        ThreeD,

        /** @brief A real depth/stencil buffer on the currently active render target. */
        DepthStencilBuffer,

        /** @brief Multi-sample anti-aliasing (any sample count above 1). */
        MultiSampleAntiAliasing,

        /** @brief More than one simultaneous render target (MRT). */
        MultipleRenderTargets,

        /** @brief Anisotropic texture filtering (device/driver-dependent on some backends). */
        AnisotropicFiltering,

        /** @brief RasterizerState.FillMode = FillMode::WireFrame. */
        WireFrame,

        /** @brief Real GPU occlusion queries (OcclusionQuery.Begin/End/PixelCount). */
        OcclusionQuery,

        /** @brief A custom (non-stock) Effect passed to SpriteBatch.Begin(). */
        CustomEffects,

        /**
         * @brief Real volume (3D) texture storage -- Texture3D::SetData()/GetData() actually
         * persist and retrieve pixel data, not just validate arguments. Headless has no real GPU
         * resource of any kind by design; Software's Texture3D support is an explicit, documented
         * v1 scope boundary (`plan_software.md` Boundaries) -- both currently leave
         * `IGraphicsBackend::CreateTexture3D()` at its shared default (returns `nullptr`), which
         * previously let `Texture3D::SetData()`/`GetData()` silently no-op instead of failing
         * cleanly (REMED-CONTENT-004).
         */
        Texture3D,

        /**
         * @brief More than one per-vertex `VertexBufferBinding` on the ORDINARY (non-instanced)
         * draw routes -- a `VertexDeclaration` whose elements are split across several bound
         * buffers (REMED-GFX-201).
         *
         * XNA 4.0 allows this on every draw route, and CNA's shared layer carries the complete
         * binding set to the backend boundary as `GpuDrawParams::vertexStreams`. Whether a backend
         * can then express the combined layout natively is a real per-backend gap: CNA's backends
         * derive their native input elements from a single byte stride, so a backend that has not
         * yet been taught to re-slot those elements across several bindings would silently render
         * from stream 0 alone. Such a backend reports false here and `DrawPrimitives`/
         * `DrawIndexedPrimitives` reject a multi-stream draw before native submission instead.
         *
         * The default is false: a newly added backend must make an explicit decision to claim
         * this, exactly like `IVertexBufferBackend::SetVertexDeclaration` being a required
         * override. Single-stream drawing is unaffected and needs no capability at all.
         */
        MultiStreamVertexInput
    };
} // CNA

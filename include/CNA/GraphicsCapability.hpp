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
        CustomEffects
    };
} // CNA

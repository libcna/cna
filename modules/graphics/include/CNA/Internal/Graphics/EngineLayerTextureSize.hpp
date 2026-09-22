// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
}

namespace CNA::Internal
{
    /**
     * @brief Lets the CNAEXT engine layer allocate render targets up to the renderer's real limit.
     *
     * XNA 4.0 caps every texture and render-target edge at the active profile's ceiling -- 2048
     * for Reach, 4096 for HiDef -- and CNA enforces that at creation (SOFTWARE-215). The
     * `CNA::Graphics` engine layer is not XNA code: `CascadedShadowMap` lays its cascades out
     * side by side in one atlas, so three High cascades are 6144 texels wide, and once the XNA
     * ceiling was enforced the atlas could no longer be created under any profile.
     *
     * While an instance is alive, a `RenderTarget2D` created on its device is held to
     * `GraphicsDevice::GetMaxTextureDimension()` -- what the live renderer really allocates --
     * instead of the profile's ceiling. Nothing else changes: a game's own render target outside
     * a scope still gets XNA's NotSupportedException, and the aspect-ratio limit still applies.
     * Engine-layer only; it is not part of the public API.
     */
    class EngineLayerTextureSizeScope
    {
    public:
        /**
         * @brief Opens the exemption on a device until this object is destroyed.
         * @param device The device the engine layer is allocating on.
         */
        explicit EngineLayerTextureSizeScope(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) noexcept;

        /** @brief Closes the exemption this object opened. */
        ~EngineLayerTextureSizeScope();

        EngineLayerTextureSizeScope(const EngineLayerTextureSizeScope&) = delete;
        EngineLayerTextureSizeScope& operator=(const EngineLayerTextureSizeScope&) = delete;

        /**
         * @brief The largest render-target edge a creation on the device may use right now.
         *
         * @param device The device the render target is created on.
         * @return The renderer's real maximum inside a scope; the profile's ceiling otherwise.
         */
        [[nodiscard]] static int MaxRenderTargetSize(
            const Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

    private:
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
    };
}

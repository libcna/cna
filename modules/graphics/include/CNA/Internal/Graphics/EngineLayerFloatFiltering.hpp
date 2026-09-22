// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
}

namespace CNA::Internal
{
    /**
     * @brief Lets the CNAEXT engine layer's own draws filter the float sources the renderer can.
     *
     * plans/plan_vulkan_modern_graphics.md VMG-0006. Microsoft XNA 4.0's `VerifyCanDraw` treats
     * `Single`, `Vector2`, `Vector4`, `HalfSingle`, `HalfVector2`, `HalfVector4` and `HdrBlendable`
     * as point-filter-only, and CNA enforces that for every XNA draw (SOFTWARE-217). The
     * `CNA::Graphics` engine layer is not XNA code: its post-process chain is built on
     * hardware-filtered HDR intermediates, and its passes already ask
     * `GraphicsCapability::HalfFloatTextureLinearFiltering` for the fallback they take. Once the
     * XNA rule was enforced, every such pass threw on every renderer that can filter.
     *
     * While an instance is alive, a draw on its device may use a non-point filter on one of those
     * formats exactly when @ref RendererFiltersFormat says the live renderer can filter it. Nothing
     * else changes: a game's own SpriteBatch or effect draw outside a scope still gets XNA's
     * NotSupportedException, and a format the renderer cannot filter is refused inside one too.
     * Engine-layer only; it is not part of the public API.
     */
    class EngineLayerFloatFilteringScope
    {
    public:
        /**
         * @brief Opens the exemption on a device until this object is destroyed.
         * @param device The device the engine layer is drawing on.
         */
        explicit EngineLayerFloatFilteringScope(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) noexcept;

        /** @brief Closes the exemption this object opened. */
        ~EngineLayerFloatFilteringScope();

        EngineLayerFloatFilteringScope(const EngineLayerFloatFilteringScope&) = delete;
        EngineLayerFloatFilteringScope& operator=(const EngineLayerFloatFilteringScope&) = delete;

        /**
         * @brief Whether the device's renderer can sample a format with a non-point filter.
         *
         * The detailed per-format profile answers when it classifies filtering for the format;
         * otherwise the half-float formats fall back to the coarse
         * `GraphicsCapability::HalfFloatTextureLinearFiltering` fact they are documented under, and
         * an unclassified 32-bit float format counts as not filterable -- unknown is never support.
         * Formats XNA does not restrict are always filterable.
         *
         * @param device The device whose renderer is asked.
         * @param format The source texture's format.
         * @return True when a filtered read of the format is something the renderer really does.
         */
        [[nodiscard]] static bool RendererFiltersFormat(
            const Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
            Microsoft::Xna::Framework::Graphics::SurfaceFormat format);

        /**
         * @brief Whether XNA restricts a format to point filtering.
         * @param format The format to classify.
         * @return True for the seven float and half-float formats `VerifyCanDraw` restricts.
         */
        [[nodiscard]] static bool IsPointFilterOnlyFormat(
            Microsoft::Xna::Framework::Graphics::SurfaceFormat format) noexcept;

    private:
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
    };
}

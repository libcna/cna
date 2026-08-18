// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/Graphics/PostProcessPass.hpp"
#include "CNA/Graphics/RenderQuality.hpp"

#include <memory>
#include <string>

namespace Microsoft::Xna::Framework::Graphics {
    class ShaderEffect;
}

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Smooths aliased edges after tonemapping, without multisampling.
     *
     * A luminance-based edge filter, reimplemented here rather than ported: it finds the local
     * contrast around a pixel, decides whether that pixel sits on an edge, and blends along the
     * edge's direction. It costs one fullscreen pass and no extra memory, which is what makes it
     * the practical choice where MSAA is unavailable -- notably on a float render target, where
     * multisampling is far less widely supported than on the back buffer.
     *
     * It runs after tonemapping, on displayed pixels. Edge detection on scene-referred values
     * would treat a highlight ten times brighter than white as an enormous edge and blur it into
     * its surroundings.
     */
    class FxaaPass final : public PostProcessPass
    {
    public:
        /**
         * @brief Creates the pass and compiles its shader.
         *
         * @param device The device to render with.
         */
        explicit FxaaPass(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /** @brief Destroys the pass and its shader. */
        ~FxaaPass() override;

        /**
         * @brief Anti-aliases @ref PostProcessContext::source into the destination.
         *
         * @param context The images and size for this invocation.
         */
        void apply(const PostProcessContext& context) override;

        /** @brief Returns `"FXAA"`. */
        [[nodiscard]] const std::string& getName() const override;

        /**
         * @brief Returns whether this renderer can run the pass.
         *
         * @param device The device whose renderer is queried.
         * @return True when custom effects are supported and the shader compiled.
         */
        [[nodiscard]] bool isSupported(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const override;

        /** @brief Returns the minimum local contrast that counts as an edge. */
        [[nodiscard]] float getEdgeThreshold() const;
        /**
         * @brief Sets the minimum local contrast that counts as an edge.
         *
         * Lower values smooth more and soften texture detail with it; the default (0.125) is the
         * usual compromise.
         */
        void setEdgeThreshold(float value);

        /**
         * @brief Returns the edge threshold a quality preset asks for.
         *
         * plan_modern.md `MOD-604`. FXAA has no sample count and no resolution to trade, so the dial
         * it does have is **which edges it bothers with**: the shader takes an early exit where
         * local contrast is below the threshold.
         *
         * | Quality | Threshold | What it filters |
         * |---|---|---|
         * | `Low`    | 0.250  | only strong, obvious edges |
         * | `Medium` | 0.125  | the default; the usual compromise |
         * | `High`   | 0.0625 | most visible aliasing |
         * | `Ultra`  | 0.0312 | the FXAA reference's own "quality" value |
         *
         * **This preset is not a performance dial**, which is worth saying because every other
         * preset in this layer is. Measured (`MOD-608`), a flat frame that takes the early exit on
         * every texel and a one-pixel checkerboard that takes it nowhere cost the same to within
         * noise, and the four presets do not separate either. The reason is in the shader: five of
         * its eleven texture samples happen *before* the threshold test, so the exit saves the
         * later six and nothing else — and on a fill-bound rasteriser that is not enough to
         * measure. Choose the preset for how much softening you want, and if FXAA needs to be
         * cheaper, switch it off.
         *
         * @param quality The preset to translate.
         * @return The threshold, always positive.
         */
        [[nodiscard]] static float edgeThresholdForQuality(RenderQuality quality);

    private:
        std::unique_ptr<FullscreenPass> fullscreen_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> effect_;
        float edgeThreshold_ = 0.125f;
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

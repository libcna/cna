// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include <memory>
#include <string>

namespace Microsoft::Xna::Framework::Graphics {
    class BlendState;
    class GraphicsDevice;
    class RenderTarget2D;
    class ShaderEffect;
    class Texture2D;
}

namespace CNA::Graphics {

    class FullscreenPass;

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Transparency that does not need sorting, and cannot be got wrong by ordering.
     *
     * plans/plan_modern.md `MOD-2106`. `TransparentDrawList` orders draws back to front, which is correct
     * for surfaces that do not interpenetrate and has no answer at all for surfaces that do — two
     * intersecting panes have no correct order, and no per-object sort can invent one. This is the
     * other answer: McGuire and Bavoil's weighted blended order-independent transparency, where
     * every surface contributes to two accumulation buffers with a depth-derived weight and the
     * composite is resolved once. **Submitting in any order produces the same frame**, which is the
     * property sorting cannot promise and this is tested for directly — to within the accumulation
     * buffer's last bit, because it is half-float and the framebuffer rounds after *each* blend.
     * Measured: the two orders differ by at most 1/255 per channel where plain alpha blending on
     * the same pair differs by 57.
     *
     * ```cpp
     * oit.begin(farPlane);
     * DrawTransparentGeometry(effect);   // its shader includes getAccumulationGlsl()
     * oit.end();
     * // ... the opaque frame is bound ...
     * oit.resolve(width, height);
     * ```
     *
     * **One pass, and getting there needed a reformulation.** The published technique accumulates
     * revealage multiplicatively while the colour accumulates additively, which needs a different
     * blend function per draw buffer — `glBlendFunci`, GL ES **3.2** or desktop GL 4.0, above this
     * layer's floor and absent from CNA's `BlendState` besides. Revealage is instead accumulated as
     * the **sum of `log(1 - alpha)`**, which is additive, so both targets share one blend state and
     * the geometry is drawn once; the resolve exponentiates it back. The alternative was a second
     * pass over every transparent surface.
     *
     * **It is an approximation, and the failure mode is stated rather than tuned away**
     * (`MOD-2108`). The weight is a function of depth alone, so a stack of many surfaces spread
     * across a large depth range reads flatter than sorting would — the near ones do not dominate
     * as strongly as they should. It is at its best where sorting is at its worst: many small
     * overlapping surfaces at similar depths.
     */
    class WeightedBlendedTransparency
    {
    public:
        /**
         * @brief Creates the accumulation targets and the resolve shader.
         *
         * @param device The device to allocate and compile on.
         * @param width  Target width in pixels; must be positive.
         * @param height Target height in pixels; must be positive.
         * @throws std::invalid_argument If a dimension is not positive.
         */
        WeightedBlendedTransparency(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                                    int width, int height);

        /** @brief Destroys the targets and the shader. */
        ~WeightedBlendedTransparency();

        WeightedBlendedTransparency(const WeightedBlendedTransparency&)            = delete;
        WeightedBlendedTransparency& operator=(const WeightedBlendedTransparency&) = delete;

        /** @brief Returns whether this renderer can run the whole route. */
        [[nodiscard]] bool isSupported() const;

        /** @brief Returns which requirement is missing, or an empty string when none is. */
        [[nodiscard]] const std::string& getUnsupportedReason() const;

        /**
         * @brief Resizes the targets, reallocating only when the size actually changed.
         *
         * @param width  New width in pixels; must be positive.
         * @param height New height in pixels; must be positive.
         * @throws std::invalid_argument If a dimension is not positive.
         * @throws std::logic_error If accumulation is open.
         */
        void resize(int width, int height);

        /**
         * @brief Binds and clears the accumulation targets, and sets the blend state they need.
         *
         * Depth **testing** stays on and depth **writing** goes off, which is what lets every
         * transparent surface see the opaque geometry in front of it without occluding the ones
         * behind it.
         *
         * @param farPlane The camera's far distance, which the weight is scaled against; must be
         *                 positive.
         * @throws std::invalid_argument If @p farPlane is not positive.
         * @throws std::logic_error If accumulation is already open.
         */
        void begin(float farPlane);

        /**
         * @brief Closes accumulation and restores the previously bound target and states.
         *
         * @throws std::logic_error If accumulation is not open.
         */
        void end();

        /**
         * @brief Composites what was accumulated over whatever target is currently bound.
         *
         * @param width  The bound target's width in pixels.
         * @param height The bound target's height in pixels.
         * @throws std::invalid_argument If a dimension is not positive.
         * @throws std::logic_error If accumulation is still open.
         */
        void resolve(int width, int height);

        /** @brief Returns whether @ref begin has been called without a matching @ref end. */
        [[nodiscard]] bool isAccumulating() const;

        /**
         * @brief The GLSL a transparent shader includes to contribute to the accumulation.
         *
         * Declares both fragment outputs and `cnaOitEmit(vec3 color, float alpha, float viewDepth)`.
         * A shader calls it instead of writing `FragColor`, and writes nothing else.
         *
         * @return GLSL source, to be concatenated ahead of the shader's own `main`.
         */
        [[nodiscard]] static std::string getAccumulationGlsl();

        /**
         * @brief The weight one surface contributes with, the CPU twin of `cnaOitWeight`.
         *
         * plans/plan_modern.md `MOD-2107`. Takes the same three arguments in the same units as the GLSL
         * and computes them in the same order, so the two can be compared on the GPU rather than
         * read side by side — the pattern Phase 20 named after six rows hit it. A twin that took a
         * pre-normalised depth would have been a second function that merely resembled the first.
         *
         * @param viewDepth The surface's distance along the view direction, in world units.
         * @param alpha     Its coverage, 0 to 1.
         * @param farPlane  The camera's far distance, which the depth is normalised against.
         * @return The weight; larger means the surface dominates the composite more.
         */
        [[nodiscard]] static float weight(float viewDepth, float alpha, float farPlane);

        /** @brief The accumulated premultiplied colour and weight. @return The texture. */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* getAccumulationTextureEXT() const;

        /** @brief The accumulated `log(1 - alpha)`. @return The texture. */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* getRevealageTextureEXT() const;

    private:
        void allocateTargets();

        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
        std::unique_ptr<FullscreenPass> fullscreen_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::RenderTarget2D> accumulation_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::RenderTarget2D> revealage_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> resolveEffect_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::BlendState> accumulateBlend_;

        std::string unsupportedReason_;
        int  width_  = 0;
        int  height_ = 0;
        bool accumulating_ = false;
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

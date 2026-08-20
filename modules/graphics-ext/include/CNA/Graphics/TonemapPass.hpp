// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/Graphics/PostProcessPass.hpp"
#include "CNA/Graphics/TonemappingMode.hpp"

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
     * @brief Maps a scene-referred HDR image into displayable range, then encodes it for the display.
     *
     * This is the pass that makes an HDR pipeline visible: without it, everything above 1.0 is
     * clamped at present time and the extra range bought nothing. It reads its operator, exposure
     * and gamma from @ref RenderPipelineSettings, so a game changes the look by changing settings
     * rather than by rebuilding the pass.
     *
     * Exposure scales the scene before the curve; gamma encodes after it. `Filmic` is the one
     * exception to the gamma step -- Hejl and Burgess-Dawson's curve has the display encode baked
     * in, so applying gamma again would double-encode it. That asymmetry is real, not an
     * implementation detail, and is why `TonemappingMode` documents it too.
     */
    class TonemapPass final : public PostProcessPass
    {
    public:
        /**
         * @brief Creates the pass and compiles its shader.
         *
         * Compilation failure is not thrown here: a pass that cannot compile reports
         * `isSupported() == false` and copies instead, which keeps a renderer-specific shader
         * problem from taking down a game that merely enabled tonemapping.
         *
         * @param device The device to render with.
         */
        explicit TonemapPass(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /** @brief Destroys the pass and its shader. */
        ~TonemapPass() override;

        /**
         * @brief Tonemaps @ref PostProcessContext::source into @ref PostProcessContext::destination.
         *
         * With no settings in the context, the pass's own current values are used.
         *
         * @param context The images, size and settings for this invocation.
         */
        void apply(const PostProcessContext& context) override;

        /** @brief Returns `"Tonemap"`. */
        [[nodiscard]] const std::string& getName() const override;

        /**
         * @brief Returns whether this renderer can run the pass.
         *
         * @param device The device whose renderer is queried.
         * @return True when custom effects are supported and the shader compiled.
         */
        [[nodiscard]] bool isSupported(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const override;

        /** @brief Returns the operator used when the context carries no settings. */
        [[nodiscard]] TonemappingMode getMode() const;
        /** @brief Sets the operator used when the context carries no settings. */
        void setMode(TonemappingMode mode);

        /** @brief Returns the exposure multiplier used when the context carries no settings. */
        [[nodiscard]] float getExposure() const;
        /** @brief Sets the exposure multiplier used when the context carries no settings. */
        void setExposure(float value);

        /** @brief Returns the display gamma used when the context carries no settings. */
        [[nodiscard]] float getGamma() const;
        /** @brief Sets the display gamma used when the context carries no settings. */
        void setGamma(float value);

        /** @brief Whether debanding dither is added after the transfer function. */
        [[nodiscard]] bool isDebandEnabled() const;

        /**
         * @brief Adds a small triangular noise to the output to break up quantisation banding.
         *
         * plan_modern.md `MOD-2132`. **Off by default**, so a frame that never asked for it is
         * unchanged to the bit.
         *
         * An eight-bit frame has 256 values to hold a gradient with. In the shadows, where the
         * transfer function is steepest, consecutive scene values land on the same output value for
         * a long stretch and then jump — and the eye reads those stretches as flat *bands* with
         * hard edges, far more visible than the quantisation error itself. A noise of about one
         * output unit turns each hard edge into a dithered boundary: the error per pixel is no
         * smaller, but it is spread rather than aligned, and the eye averages it back to the
         * gradient that was there.
         *
         * Two decisions are load-bearing and both are asserted rather than assumed. The noise is
         * added **after** the transfer function, never before — the curve's slope varies by more
         * than seven to one across the range, so a fixed perturbation in linear light arrives at
         * the display large in the shadows and invisible in the highlights. And it is **triangular
         * rather than uniform**, which is what makes the residual error independent of the signal;
         * uniform noise removes the bands and leaves flat areas looking grainier at some
         * brightnesses than at others.
         *
         * @param value True to add the dither.
         */
        void setDebandEnabled(bool value);

        /** @brief Returns the dither amplitude, in output units where 1 is one 8-bit step. */
        [[nodiscard]] float getDebandStrength() const;

        /**
         * @brief Sets the dither amplitude, in output units where 1 is one 8-bit step.
         *
         * The default of 1 is the right answer for an 8-bit target and is not a taste setting:
         * below one step the bands survive, above it the noise becomes the visible artefact.
         * Offered because a 10-bit target wants a smaller number.
         *
         * @param value The amplitude in 8-bit steps; clamped to 0..4.
         */
        void setDebandStrength(float value);

        /**
         * @brief Applies one operator to one channel value on the CPU, exactly as the shader does.
         *
         * Public because it is the only way to state "the shader and the specification agree"
         * as an assertion: the tests compare rendered pixels against this, and a game can use it
         * to compute a UI preview without a GPU round trip. Exposure and gamma are applied here
         * too, so the whole pass is reproducible in one call.
         *
         * @param mode     The operator to apply.
         * @param value    The linear scene-referred channel value.
         * @param exposure Pre-curve exposure multiplier.
         * @param gamma    Display gamma; ignored for `Filmic`, which bakes its own encode in.
         * @return The display-encoded channel value, in [0,1].
         */
        [[nodiscard]] static float tonemapChannel(TonemappingMode mode, float value,
                                                  float exposure, float gamma);

    private:
        std::unique_ptr<FullscreenPass> fullscreen_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> effect_;
        TonemappingMode mode_     = TonemappingMode::None;
        float           exposure_ = 1.0f;
        float           gamma_    = 2.2f;
        bool            deband_   = false;
        float           debandStrength_ = 1.0f;
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/CRTMaskType.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Full-screen CRT display emulation post-process effect.
     *
     * A `ShaderEffect` (GLSL, EasyGL renderer) that emulates a period CRT monitor: darkened
     * alternating scanlines, an RGB sub-pixel mask (aperture grille or shadow mask), mild
     * barrel-distortion curvature of the screen edges, and a corner vignette. Every parameter
     * is independently tunable and defaults to a moderate, generally-flattering combination.
     *
     * @note Requires a single full-screen source, unlike `DepthEffect`. `getCurvature()`/
     * `getVignetteIntensity()` measure position from the drawn quad's own texture coordinate
     * (`vTexCoord`), which is only a meaningful proxy for "where on screen this fragment is"
     * when the entire final frame is one full-screen quad. Bind `CRTEffect` to a multi-draw-call
     * scene (one `SpriteBatch::Draw()` per sprite) and each sprite gets its own little curvature/
     * vignette warped around its own local rect instead of one shared curved screen. Render the
     * scene into an offscreen `RenderTarget2D` first (with no `CRTEffect` bound), then redraw
     * that single composited texture full-screen through `CRTEffect` — see
     * `modules/graphics-ext/examples/crt_effect_demo_test.cpp`'s `RenderSceneToTexture()`/`DrawCrtPass()` for the
     * exact pattern, including the `SpriteEffects::FlipVertically` needed to compensate for
     * `RenderTarget2D` content being stored bottom-up. Scanlines and the RGB mask do not have
     * this restriction (they index by `gl_FragCoord`, real screen pixels), but the single-pass
     * requirement is documented as applying to the whole effect for simplicity.
     *
     * Deliberately a separate class from `DepthEffect` rather than folded-in options — CRT
     * emulation and colour-depth reduction are independent concerns with independent XNA/FNA-era
     * precedent (many retro-look games only wanted one or the other). Chaining both together
     * (scene → `DepthEffect` → `RenderTarget2D` → `CRTEffect` → backbuffer) uses the same
     * multi-pass pattern above, just with `DepthEffect` as the first pass instead of a plain
     * scene draw.
     *
     * CNAEXT extension — no XNA/FNA precedent; this is a from-scratch CNA post-process effect,
     * not a port.
     */
    class CRTEffect : public Microsoft::Xna::Framework::Graphics::ShaderEffect
    {
    public:
        /**
         * @brief Constructs a CRTEffect, compiling its built-in GLSL shader.
         *
         * @param device GraphicsDevice that owns this effect.
         */
        explicit CRTEffect(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /** @brief Returns the scanline darkening strength, in [0,1] (0 = no scanlines). */
        [[nodiscard]] float getScanlineIntensity() const;
        /** @brief Sets the scanline darkening strength. Clamped to [0,1]. */
        void setScanlineIntensity(float value);

        /** @brief Returns the barrel-distortion curvature amount, in [0,1] (0 = flat screen). */
        [[nodiscard]] float getCurvature() const;
        /** @brief Sets the barrel-distortion curvature amount. Clamped to [0,1]. */
        void setCurvature(float value);

        /** @brief Returns the corner vignette darkening strength, in [0,1]. */
        [[nodiscard]] float getVignetteIntensity() const;
        /** @brief Sets the corner vignette darkening strength. Clamped to [0,1]. */
        void setVignetteIntensity(float value);

        /** @brief Returns the RGB sub-pixel mask darkening strength, in [0,1]. */
        [[nodiscard]] float getMaskIntensity() const;
        /** @brief Sets the RGB sub-pixel mask darkening strength. Clamped to [0,1]. */
        void setMaskIntensity(float value);

        /** @brief Returns the active sub-pixel mask pattern. */
        [[nodiscard]] CRTMaskType getMaskType() const;
        /** @brief Sets the sub-pixel mask pattern applied on the next Apply(). */
        void setMaskType(CRTMaskType type);

        /** @brief Returns the fully qualified CNA type name. */
        [[nodiscard]] const std::string& GetTypeName() const override;

        /**
         * @brief Creates an independent clone of this effect with the same parameters.
         *
         * @return Pointer to the cloned effect. Caller takes ownership.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Effect* Clone() override;

    protected:
        /** @brief Binds the compiled shader and uploads the current CRT parameters. */
        void OnApply() override;

    private:
        float scanlineIntensity_ = 0.3f;
        float curvature_ = 0.08f;
        float vignetteIntensity_ = 0.25f;
        float maskIntensity_ = 0.35f;
        CRTMaskType maskType_ = CRTMaskType::ApertureGrille;
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

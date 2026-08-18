// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/TonemappingMode.hpp"
#include "CNA/CNAHelper.hpp"
#include "CNA/Graphics/RenderQuality.hpp"
#include "CNA/Graphics/ShadowQuality.hpp"

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */
    /**
     * @brief Stores configuration for the CNAEXT extended render pipeline.
     *
     * This is a pure settings bag — it does not perform any rendering itself.
     * The active renderer reads these settings and adjusts its render passes
     * accordingly (once renderer support is implemented for each feature).
     *
     * Construct via `GraphicsDevice::GetRenderPipelineSettings()` or standalone.
     */
    class RenderPipelineSettings
    {
    public:
        /** @brief Constructs settings with sensible defaults. */
        RenderPipelineSettings();

        // ── HDR ─────────────────────────────────────────────────────────────

        /** @brief Returns true if HDR rendering is enabled. */
        [[nodiscard]] bool isHDREnabled() const;
        /** @brief Enables or disables HDR rendering (RGBA16F render target). */
        void setHDREnabled(bool value);

        /** @brief Returns the scene exposure multiplier (HDR only). */
        [[nodiscard]] float getExposure() const;
        /** @brief Sets the scene exposure multiplier (HDR only). */
        void setExposure(float value);

        /** @brief Returns the display gamma (typically 2.2). */
        [[nodiscard]] float getGamma() const;
        /** @brief Sets the display gamma. */
        void setGamma(float value);

        // ── Tonemapping ──────────────────────────────────────────────────────

        /** @brief Returns the active tonemapping operator. */
        [[nodiscard]] TonemappingMode getTonemappingMode() const;
        /** @brief Sets the tonemapping operator (HDR only). */
        void setTonemappingMode(TonemappingMode mode);

        // ── Post-processing ──────────────────────────────────────────────────

        /** @brief Returns true if bloom is enabled. */
        [[nodiscard]] bool isBloomEnabled() const;
        /** @brief Enables or disables the bloom post-process pass. */
        void setBloomEnabled(bool value);

        /** @brief Returns the bloom intensity multiplier. */
        [[nodiscard]] float getBloomIntensity() const;
        /** @brief Sets the bloom intensity multiplier. */
        void setBloomIntensity(float value);

        /** @brief Returns the luminance above which a pixel contributes to bloom. */
        [[nodiscard]] float getBloomThreshold() const;
        /**
         * @brief Sets the bloom luminance threshold.
         *
         * Values at or below zero make every pixel bloom, which is a legitimate stylistic choice
         * rather than an error, so the value is not clamped.
         */
        void setBloomThreshold(float value);

        /** @brief Returns the bloom blur radius in half-resolution steps (the mip chain depth). */
        [[nodiscard]] int getBloomIterations() const;
        /**
         * @brief Sets how many half-resolution steps the bloom pyramid uses.
         *
         * Clamped to 1..8 when applied: a chain deeper than the target allows is not an error, and
         * the pass reduces it to what the viewport can actually hold.
         */
        void setBloomIterations(int value);

        /** @brief Returns true if SSAO is enabled. */
        [[nodiscard]] bool isSSAOEnabled() const;
        /** @brief Enables or disables the SSAO post-process pass. */
        void setSSAOEnabled(bool value);

        /** @brief Returns the SSAO hemisphere sampling radius, in world units. */
        [[nodiscard]] float getSSAORadius() const;
        /** @brief Sets the SSAO hemisphere sampling radius, in world units. */
        void setSSAORadius(float value);

        /** @brief Returns the SSAO occlusion strength multiplier. */
        [[nodiscard]] float getSSAOIntensity() const;
        /** @brief Sets the SSAO occlusion strength multiplier. */
        void setSSAOIntensity(float value);

        /** @brief Returns the number of SSAO samples per pixel. */
        [[nodiscard]] int getSSAOSampleCount() const;
        /**
         * @brief Sets the number of SSAO samples per pixel.
         *
         * The pass clamps this to the range its kernel generator supports (8..64) rather than
         * rejecting a value, so a quality preset can set it without knowing that range.
         */
        void setSSAOSampleCount(int value);

        // ── Anti-aliasing ────────────────────────────────────────────────────

        /** @brief Returns true if the FXAA post-process pass is enabled. */
        [[nodiscard]] bool isFXAAEnabled() const;
        /** @brief Enables or disables FXAA, which runs after tonemapping. */
        void setFXAAEnabled(bool value);

        // ── Quality ──────────────────────────────────────────────────────────

        /** @brief Returns the overall render quality preset. */
        [[nodiscard]] RenderQuality getRenderQuality() const;
        /** @brief Sets the overall render quality preset. */
        void setRenderQuality(RenderQuality quality);

        /**
         * @brief Writes the current quality preset's derived values into this settings bag.
         *
         * plan_modern.md `MOD-409`. `setRenderQuality` stores a preference and changes nothing
         * else; this is the explicit step that turns it into numbers. The two are separate on
         * purpose — a game that has tuned `bloomIterations` by hand should not have that value
         * rewritten because something set the quality, and a settings menu that *wants* the preset
         * applied says so in one call.
         *
         * Today it derives bloom's pyramid level count (`BloomPass::iterationsForQuality`). Passes
         * whose quality dial has not been decided yet are deliberately left alone rather than given
         * a guessed mapping; tonemapping has none at all and never will (`MOD-320`).
         */
        CNAEXT void applyRenderQualityPresetEXT();

        /** @brief Returns the shadow quality preset. */
        [[nodiscard]] ShadowQuality getShadowQuality() const;
        /** @brief Sets the shadow quality preset. */
        void setShadowQuality(ShadowQuality quality);

        /** @brief Returns true if shadow rendering is enabled. */
        [[nodiscard]] bool isShadowsEnabled() const;
        /** @brief Enables or disables shadow rendering. */
        void setShadowsEnabled(bool value);

    private:
        bool            hdrEnabled_      = false;
        float           exposure_        = 1.0f;
        float           gamma_           = 2.2f;
        TonemappingMode tonemappingMode_ = TonemappingMode::None;

        bool            bloomEnabled_    = false;
        float           bloomIntensity_  = 1.0f;

        float           bloomThreshold_  = 1.0f;
        int             bloomIterations_ = 4;

        bool            ssaoEnabled_     = false;
        float           ssaoRadius_      = 0.5f;
        float           ssaoIntensity_   = 1.0f;
        int             ssaoSampleCount_ = 16;

        bool            fxaaEnabled_     = false;

        RenderQuality   renderQuality_   = RenderQuality::Medium;
        ShadowQuality   shadowQuality_   = ShadowQuality::Disabled;
        bool            shadowsEnabled_  = false;
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

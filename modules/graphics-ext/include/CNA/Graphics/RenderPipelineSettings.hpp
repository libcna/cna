// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/TonemappingMode.hpp"
#include "CNA/CNAHelper.hpp"
#include "CNA/Graphics/RenderQuality.hpp"

#include <string>
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
     * Owned by a `RenderPipeline` (`getSettings()`), or constructed standalone and handed to a
     * pass through `PostProcessContext::settings`.
     *
     * plan_modern.md `MOD-728`/`MOD-729`: this used to say "construct via
     * `GraphicsDevice::GetRenderPipelineSettings()`", which never existed and is not going to.
     * Exposing this type from `GraphicsDevice` would give an XNA type a member whose type only
     * exists under a compile option.
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

        /** @brief Returns true if screen-space reflections are enabled. */
        [[nodiscard]] bool isSSREnabled() const;
        /** @brief Enables or disables the screen-space reflection pass. */
        void setSSREnabled(bool value);

        /** @brief Returns how far a reflected ray travels, in world units. */
        [[nodiscard]] float getSSRMaxDistance() const;
        /** @brief Sets how far a reflected ray travels, in world units. */
        void setSSRMaxDistance(float value);

        /** @brief Returns how many steps a reflected ray is marched in. */
        [[nodiscard]] int getSSRStepCount() const;
        /** @brief Sets how many steps a reflected ray is marched in. */
        void setSSRStepCount(int value);

        /** @brief Returns how far behind a surface a ray may pass and still count as a hit. */
        [[nodiscard]] float getSSRThickness() const;
        /** @brief Sets how far behind a surface a ray may pass and still count as a hit. */
        void setSSRThickness(float value);

        /** @brief Returns how far past a surface a ray must travel before a hit counts. */
        [[nodiscard]] float getSSRDepthBias() const;
        /** @brief Sets how far past a surface a ray must travel before a hit counts. */
        void setSSRDepthBias(float value);

        /** @brief Returns how wide the fade at the edge of the screen is, in screen fractions. */
        [[nodiscard]] float getSSREdgeFade() const;
        /** @brief Sets how wide the fade at the edge of the screen is, in screen fractions. */
        void setSSREdgeFade(float value);

        /** @brief Returns true if depth of field is enabled. */
        [[nodiscard]] bool isDOFEnabled() const;
        /** @brief Enables or disables the depth-of-field pass. */
        void setDOFEnabled(bool value);

        /** @brief Returns the distance the lens is focused at, in world units. */
        [[nodiscard]] float getDOFFocusDistance() const;
        /** @brief Sets the distance the lens is focused at, in world units. */
        void setDOFFocusDistance(float value);

        /** @brief Returns the focal length in millimetres. */
        [[nodiscard]] float getDOFFocalLength() const;
        /** @brief Sets the focal length in millimetres. */
        void setDOFFocalLength(float value);

        /** @brief Returns the f-number. */
        [[nodiscard]] float getDOFFNumber() const;
        /** @brief Sets the f-number; smaller opens the aperture and shortens the field. */
        void setDOFFNumber(float value);

        /** @brief Returns the largest blur radius the pass will use, in screen fractions. */
        [[nodiscard]] float getDOFMaxRadius() const;
        /** @brief Sets the largest blur radius the pass will use, in screen fractions. */
        void setDOFMaxRadius(float value);

        /** @brief Returns how far a fully-rough surface spreads its reflection, in screen fractions. */
        [[nodiscard]] float getSSRRoughnessBlur() const;
        /** @brief Sets how far a fully-rough surface spreads its reflection, in screen fractions. */
        void setSSRRoughnessBlur(float value);

        /** @brief Returns how strongly the reflection is mixed over the frame. */
        [[nodiscard]] float getSSRIntensity() const;
        /** @brief Sets how strongly the reflection is mixed over the frame. */
        void setSSRIntensity(float value);

        // ── Anti-aliasing ────────────────────────────────────────────────────

        /** @brief Returns true if the FXAA post-process pass is enabled. */
        [[nodiscard]] bool isFXAAEnabled() const;
        /** @brief Enables or disables FXAA, which runs after tonemapping. */
        void setFXAAEnabled(bool value);

        /**
         * @brief Returns the minimum local contrast FXAA treats as an edge.
         *
         * plan_modern.md `MOD-604`. Lives here rather than only on the pass so a quality preset can
         * set it: `applyRenderQualityPresetEXT()` writes it, and `FxaaPass` reads it whenever a
         * settings bag is supplied.
         *
         * @return The threshold; the default 0.125 is `RenderQuality::Medium`.
         */
        CNAEXT [[nodiscard]] float getFXAAEdgeThresholdEXT() const;

        /**
         * @brief Sets the minimum local contrast FXAA treats as an edge.
         *
         * @param value The threshold. Stored as given; the pass clamps nothing, matching how the
         *              other out-of-range settings in this bag behave.
         */
        CNAEXT void setFXAAEdgeThresholdEXT(float value);

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

        /**
         * @brief The smallest gamma this bag will store.
         *
         * plan_modern.md `MOD-730`. Gamma is applied as `pow(colour, 1/gamma)`, so zero is a
         * division by zero and the frame comes back as infinities. Clamped rather than rejected,
         * because a settings file with a stale zero in it should still produce a picture.
         */
        static constexpr float kMinimumGamma = 0.01f;

        /** @brief The smallest FXAA edge threshold this bag will store; zero would blur every texel. */
        static constexpr float kMinimumFxaaEdgeThreshold = 0.001f;

        /**
         * @brief Writes every field as `key=value;` text.
         *
         * plan_modern.md `MOD-731`. For demos, for tests that need a scene's look pinned in one
         * line, and for a settings file. Deliberately the simplest format that round-trips: no
         * quoting, no nesting, no escapes — a value here is a number or an enum name, and adding a
         * parser that could fail in interesting ways would be a worse trade than the format's
         * limits.
         *
         * @return The serialized settings.
         */
        CNAEXT [[nodiscard]] std::string toStringEXT() const;

        /**
         * @brief Reads fields written by @ref toStringEXT, leaving unmentioned fields alone.
         *
         * Unknown keys are **ignored rather than refused**, which is what lets an older build read
         * a newer settings string. Malformed values are ignored the same way — a settings string is
         * a convenience, and refusing to load a whole look because one field is `bloomIntensity=x`
         * helps nobody. Every value it does accept goes through the ordinary setters, so the
         * clamping above applies.
         *
         * @param text The serialized settings.
         * @return How many fields were recognised and applied.
         */
        CNAEXT int applyFromStringEXT(const std::string& text);

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
        bool            ssrEnabled_      = false;
        float           ssrMaxDistance_  = 8.0f;
        int             ssrStepCount_    = 32;
        float           ssrThickness_    = 0.5f;
        float           ssrDepthBias_    = 0.05f;
        float           ssrEdgeFade_     = 0.1f;
        float           ssrRoughnessBlur_ = 0.02f;
        float           ssrIntensity_    = 1.0f;
        bool            dofEnabled_      = false;
        float           dofFocusDistance_ = 10.0f;
        float           dofFocalLength_   = 50.0f;
        float           dofFNumber_       = 5.6f;
        float           dofMaxRadius_     = 0.02f;
        float           fxaaEdgeThreshold_ = 0.125f;

        bool            fxaaEnabled_     = false;

        RenderQuality   renderQuality_   = RenderQuality::Medium;
        ShadowQuality   shadowQuality_   = ShadowQuality::Disabled;
        bool            shadowsEnabled_  = false;
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

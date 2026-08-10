// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_NOXNA

#include "CNA/Graphics/TonemappingMode.hpp"
#include "CNA/Graphics/RenderQuality.hpp"
#include "CNA/Graphics/ShadowQuality.hpp"

namespace CNA::Graphics {
    /**
     * @brief Stores configuration for the NOXNA extended render pipeline.
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

        /** @brief Returns true if SSAO is enabled. */
        [[nodiscard]] bool isSSAOEnabled() const;
        /** @brief Enables or disables the SSAO post-process pass. */
        void setSSAOEnabled(bool value);

        // ── Quality ──────────────────────────────────────────────────────────

        /** @brief Returns the overall render quality preset. */
        [[nodiscard]] RenderQuality getRenderQuality() const;
        /** @brief Sets the overall render quality preset. */
        void setRenderQuality(RenderQuality quality);

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

        bool            ssaoEnabled_     = false;

        RenderQuality   renderQuality_   = RenderQuality::Medium;
        ShadowQuality   shadowQuality_   = ShadowQuality::Disabled;
        bool            shadowsEnabled_  = false;
    };

} // namespace CNA::Graphics

#endif // CNA_NOXNA

// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/Graphics/PostProcessPass.hpp"

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
     * @brief Adds film-like noise, strongest in the midtones.
     *
     * Grain that is uniform across the range reads as digital noise rather than as film: real grain
     * is invisible in blown highlights and buried in blacks, and lives in the midtones where the
     * emulsion is actually responding. The weighting here is that curve.
     *
     * The pattern is a function of the pixel and of @ref PostProcessContext::elapsedSeconds, so it
     * is **deterministic**: the same frame at the same time produces the same grain, which is what
     * makes a rendered sequence reproducible rather than merely noisy.
     */
    class FilmGrainPass final : public PostProcessPass
    {
    public:
        /** @brief Creates the pass and compiles its shader. @param device The device to render with. */
        explicit FilmGrainPass(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
        /** @brief Destroys the pass and its shader. */
        ~FilmGrainPass() override;

        /** @brief Grains @ref PostProcessContext::source into the destination. @param context The images, time and size. */
        void apply(const PostProcessContext& context) override;
        /** @brief Returns `"FilmGrain"`. */
        [[nodiscard]] const std::string& getName() const override;
        /** @brief Returns whether this renderer can run the pass. @param device The device queried. @return True when its shader compiled. */
        [[nodiscard]] bool isSupported(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const override;

        /** @brief Returns how strong the grain is at its peak. */
        [[nodiscard]] float getIntensity() const;
        /**
         * @brief Sets how strong the grain is at its peak, in the midtones.
         *
         * @param value The amplitude as a fraction of the range, clamped to [0, 1].
         */
        void setIntensity(float value);

    private:
        std::unique_ptr<FullscreenPass> fullscreen_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> effect_;
        float intensity_ = 0.0f;
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

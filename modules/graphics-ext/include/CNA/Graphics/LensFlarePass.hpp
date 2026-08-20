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
     * @brief Repeats the frame's bright spots along the line through the centre.
     *
     * The ghosts a lens throws are reflections between its elements, and they land on the opposite
     * side of the optical axis from what caused them: a bright window at the top of the frame puts
     * its ghosts along the bottom. That is the one property that makes flare read as a lens rather
     * than as a smear, and it is what this pass's test asserts.
     *
     * It runs **before tonemapping**, on scene-referred values, for the reason bloom does: the
     * threshold that decides what is bright enough to flare has to separate a genuinely bright
     * light from a merely white wall, and after tonemapping those are the same number.
     */
    class LensFlarePass final : public PostProcessPass
    {
    public:
        /** @brief Creates the pass and compiles its shader. @param device The device to render with. */
        explicit LensFlarePass(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
        /** @brief Destroys the pass and its shader. */
        ~LensFlarePass() override;

        /** @brief Adds flare to @ref PostProcessContext::source. @param context The images and size. */
        void apply(const PostProcessContext& context) override;
        /** @brief Returns `"LensFlare"`. */
        [[nodiscard]] const std::string& getName() const override;
        /** @brief Returns whether this renderer can run the pass. @param device The device queried. @return True when its shader compiled. */
        [[nodiscard]] bool isSupported(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const override;

        /** @brief Returns the brightness a pixel must reach before it flares. */
        [[nodiscard]] float getThreshold() const;
        /**
         * @brief Sets the brightness a pixel must reach before it flares.
         *
         * @param value The threshold in scene-referred units. Negative values are ignored.
         */
        void setThreshold(float value);

        /** @brief Returns how strongly the ghosts are added to the frame. */
        [[nodiscard]] float getIntensity() const;
        /**
         * @brief Sets how strongly the ghosts are added to the frame.
         *
         * @param value The multiplier; negative values are ignored.
         */
        void setIntensity(float value);

        /** @brief Returns how far apart successive ghosts sit along the centre line. */
        [[nodiscard]] float getDispersal() const;
        /**
         * @brief Sets how far apart successive ghosts sit along the centre line.
         *
         * @param value The spacing as a fraction of the distance to the centre, clamped to [0, 1].
         */
        void setDispersal(float value);

        /** @brief How many ghosts the pass casts. */
        static constexpr int kGhostCount = 4;

    private:
        std::unique_ptr<FullscreenPass> fullscreen_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> effect_;
        float threshold_ = 1.0f;
        float intensity_ = 0.0f;
        float dispersal_ = 0.35f;
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

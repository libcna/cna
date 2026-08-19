// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/Graphics/PostProcessPass.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

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
     * @brief Draws the shafts of light that appear where something bright is partly hidden.
     *
     * Every pixel walks towards the light's position on screen, gathering what it finds and letting
     * it decay with distance. Where the path is clear the gathered light piles up into a shaft;
     * where an occluder stands in the way there is nothing to gather and the shaft is absent. That
     * absence is the effect -- shafts are the *shape of the occluder*, which is why this needs no
     * volume and no marching through one.
     *
     * **The light's position is the app's to supply**, as a screen coordinate: the layer does not
     * know which of a scene's lights is the sun, and projecting one is a line of arithmetic the app
     * already has the matrices for. It may be outside the frame, and the pass fades the effect out
     * as it goes -- a light just past the edge still throws shafts inward, and a hard cut-off at
     * the border is the giveaway this avoids.
     *
     * It runs before tonemapping, on scene-referred values, for the reason bloom and flare do: the
     * threshold that decides what is bright enough to be a light has to separate a light from a
     * white wall, and after tonemapping those are the same number.
     */
    class LightShaftPass final : public PostProcessPass
    {
    public:
        /** @brief Creates the pass and compiles its shader. @param device The device to render with. */
        explicit LightShaftPass(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
        /** @brief Destroys the pass and its shader. */
        ~LightShaftPass() override;

        /** @brief Adds shafts to @ref PostProcessContext::source. @param context The images and size. */
        void apply(const PostProcessContext& context) override;
        /** @brief Returns `"LightShafts"`. */
        [[nodiscard]] const std::string& getName() const override;
        /** @brief Returns whether this renderer can run the pass. @param device The device queried. @return True when its shader compiled. */
        [[nodiscard]] bool isSupported(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const override;

        /** @brief Returns the light's position on screen, in texture coordinates. */
        [[nodiscard]] Microsoft::Xna::Framework::Vector2 getLightScreenPosition() const;
        /**
         * @brief Sets the light's position on screen, in texture coordinates.
         *
         * The centre of the frame is (0.5, 0.5). Values outside [0, 1] are accepted and meaningful:
         * a light just past the edge still throws shafts into the frame, and the effect fades with
         * how far outside it is rather than stopping at the border.
         *
         * @param value The position.
         */
        void setLightScreenPosition(const Microsoft::Xna::Framework::Vector2& value);

        /** @brief Returns the brightness a pixel must reach before it feeds a shaft. */
        [[nodiscard]] float getThreshold() const;
        /** @brief Sets the brightness a pixel must reach before it feeds a shaft. @param value The threshold; negatives are ignored. */
        void setThreshold(float value);

        /** @brief Returns how strongly the shafts are added to the frame; 0 disables the pass. */
        [[nodiscard]] float getIntensity() const;
        /** @brief Sets how strongly the shafts are added to the frame. @param value The multiplier; negatives are ignored. */
        void setIntensity(float value);

        /** @brief Returns how quickly a shaft fades along its length. */
        [[nodiscard]] float getDecay() const;
        /**
         * @brief Sets how quickly a shaft fades along its length.
         *
         * @param value The per-step survival, clamped to [0, 1]. 1 is a shaft that never fades,
         *              which reads as a wash over the frame rather than as light.
         */
        void setDecay(float value);

        /** @brief How many steps each pixel walks towards the light. */
        static constexpr int kStepCount = 24;

    private:
        std::unique_ptr<FullscreenPass> fullscreen_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> effect_;

        Microsoft::Xna::Framework::Vector2 lightScreenPosition_{0.5f, 0.5f};
        float threshold_ = 0.7f;
        float intensity_ = 0.0f;
        float decay_     = 0.92f;
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

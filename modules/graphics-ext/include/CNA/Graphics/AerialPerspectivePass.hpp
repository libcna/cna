// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/Graphics/PostProcessPass.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

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
     * @brief Puts the sky's own air in front of the geometry, not only behind it.
     *
     * `AtmosphericSky` computes what a clear sky looks like by integrating scattering along a ray
     * that goes all the way out of the atmosphere. Everything drawn in front of that sky is
     * currently rendered as though the air between it and the camera were not there — so a mountain
     * twenty kilometres away arrives at full contrast and full saturation against a sky that is
     * visibly atmospheric. That mismatch is the single clearest tell that a sky is a backdrop rather
     * than a place.
     *
     * This pass runs **the same integral over a shorter path**. For each pixel it takes the distance
     * the prepass recorded, converts it to air masses, attenuates the geometry's colour by what that
     * much air absorbs and adds what that much air scatters in. At the far end the two terms meet
     * the sky exactly: a surface far enough away contributes nothing of its own and is replaced by
     * the radiance `AtmosphericSky` would have drawn there. It is one model called twice rather than
     * two models that agree until someone edits one — see @ref AtmosphericSky::getModelGlsl.
     *
     * **It is not fog, and stacking it with fog is double-counting.** `HeightFogPass` and
     * `VolumetricFogPass` each make a different physical claim about the same air; see
     * `docs/cnaext-engine-layer.md` for which to pick.
     *
     * Requires @ref PostProcessContext::sourceDepth, a far plane and the inverse view-projection.
     * Without them the pass copies its input through and names what was missing in
     * @ref getFallbackReason.
     */
    class AerialPerspectivePass final : public PostProcessPass
    {
    public:
        /**
         * @brief Creates the pass and compiles its shader.
         *
         * @param device The device to render with.
         */
        explicit AerialPerspectivePass(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /** @brief Destroys the pass and its resources. */
        ~AerialPerspectivePass() override;

        /**
         * @brief Applies the atmosphere to the context's source image.
         *
         * @param context The images, camera parameters and settings for this invocation.
         */
        void apply(const PostProcessContext& context) override;

        /** @brief Returns `"AerialPerspective"`. */
        [[nodiscard]] const std::string& getName() const override;

        /**
         * @brief Returns whether this renderer can run the pass.
         *
         * @param device The device whose renderer is queried.
         * @return True when custom effects are supported and the shader compiled.
         */
        [[nodiscard]] bool isSupported(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const override;

        /** @brief Returns the direction the sunlight travels, in world space. */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getSunDirection() const;

        /**
         * @brief Sets the direction the sunlight travels, in world space.
         *
         * The same convention `AtmosphericSky` and @ref DirectionalLightEXT use, so one value drives
         * all three. Set it to the same vector as the sky or the two will disagree about where the
         * sun is, which reads as a wrongly lit haze rather than as a mistake.
         *
         * @param value The direction the sunlight travels.
         */
        void setSunDirection(const Microsoft::Xna::Framework::Vector3& value);

        /** @brief Returns how hazy the air is; 1 is the clearest the model admits. */
        [[nodiscard]] float getTurbidity() const;

        /**
         * @brief Sets how hazy the air is.
         *
         * @param value The turbidity; clamped to at least 1, where the aerosol term vanishes.
         */
        void setTurbidity(float value);

        /** @brief Returns the overall brightness multiplier applied to the scattered light. */
        [[nodiscard]] float getIntensity() const;

        /**
         * @brief Sets the overall brightness multiplier applied to the scattered light.
         *
         * Match it to `AtmosphericSky::setIntensity` — the two are describing the same sunlight.
         *
         * @param value The multiplier; negatives are ignored.
         */
        void setIntensity(float value);

        /** @brief Returns the distance over which one vertical column of air accumulates. */
        [[nodiscard]] float getScaleHeight() const;

        /**
         * @brief Sets the distance over which one vertical column of air accumulates.
         *
         * The one number that ties the model to a game's world scale, and the only one worth
         * touching. The scattering coefficients are optical depth through a single vertical column
         * of atmosphere, so a horizontal distance divided by this is already in the model's own
         * units and needs no conversion. The default of 8400 is the real atmosphere's scale height
         * in metres: correct where one world unit is a metre, and wrong by exactly the scale factor
         * anywhere else. A game whose unit is a centimetre wants 840000; one whose visible world is
         * a few hundred units wants a few hundred, or the effect will be invisible.
         *
         * @param value The scale height in world units; clamped to a positive value.
         */
        void setScaleHeight(float value);

        /**
         * @brief Returns why the last @ref apply copied its input through, or an empty string.
         *
         * @return The reason, or an empty string when the pass ran.
         */
        [[nodiscard]] const std::string& getFallbackReason() const;

        /**
         * @brief The air masses a ray of this length looking this way passes through.
         *
         * The CPU twin of the shader's `cnaAerialAirMass`, written separately and compared against
         * it on the GPU. Exposed because it is where the world scale enters the model, which is the
         * one part of this pass a game has to reason about.
         *
         * @param viewDirection Where the ray is looking, in world space; it is normalised.
         * @param distance      How far the geometry is, in world units.
         * @param scaleHeight   The distance over which one vertical column accumulates.
         * @return The air mass, capped at the whole atmosphere along that direction.
         */
        [[nodiscard]] static float airMassForDistance(
            const Microsoft::Xna::Framework::Vector3& viewDirection, float distance,
            float scaleHeight);

        /**
         * @brief What survives of a colour after this much air, per channel.
         *
         * @param turbidity How hazy the air is.
         * @param airMass   The air mass the light passed through.
         * @return The per-channel transmittance, each in [0, 1].
         */
        [[nodiscard]] static Microsoft::Xna::Framework::Vector3 transmittance(float turbidity,
                                                                             float airMass);

    private:
        std::unique_ptr<FullscreenPass> fullscreen_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> effect_;

        Microsoft::Xna::Framework::Vector3 sunDirection_{0.0f, -1.0f, 0.0f};
        float turbidity_   = 2.5f;
        float intensity_   = 1.0f;
        float scaleHeight_ = 8400.0f;
        std::string fallbackReason_;
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

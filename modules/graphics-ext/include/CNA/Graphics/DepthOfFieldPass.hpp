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
     * @brief Blurs the frame by how far each pixel is from the plane the camera is focused on.
     *
     * The amount of blur is not invented: it is the **circle of confusion** a thin lens would
     * produce, computed from the focus distance, the focal length and the f-number, so the settings
     * mean what a photographer means by them. A 50 mm lens at f/1.4 focused two metres away gives a
     * shallow depth of field here for the same reason it does on a camera.
     *
     * It reads @ref PostProcessContext::sourceDepth -- the same image `SsaoPass` and `SsrPass` read,
     * produced by `DepthNormalPrepass` -- and the camera's far plane. Without them the pass copies
     * its source through, the same contract every other pass has.
     *
     * **Units.** Focal length is in millimetres, as on a lens. Focus distance is in world units, and
     * the pass assumes one world unit is one metre; a game measuring in centimetres wants a focus
     * distance a hundred times larger, not a different setting.
     */
    class DepthOfFieldPass final : public PostProcessPass
    {
    public:
        /**
         * @brief Creates the pass and compiles its shader.
         *
         * @param device The device to render with.
         */
        explicit DepthOfFieldPass(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /** @brief Destroys the pass and its shader. */
        ~DepthOfFieldPass() override;

        /**
         * @brief Blurs @ref PostProcessContext::source into the destination by depth.
         *
         * @param context The images, camera and size for this invocation.
         */
        void apply(const PostProcessContext& context) override;

        /** @brief Returns `"DepthOfField"`. */
        [[nodiscard]] const std::string& getName() const override;

        /**
         * @brief Returns whether this renderer can run the pass.
         *
         * @param device The device whose renderer is queried.
         * @return True when the renderer executes shader source and the shader compiled.
         */
        [[nodiscard]] bool isSupported(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const override;

        /**
         * @brief The diameter of the circle of confusion a thin lens produces, in millimetres.
         *
         * The optics this pass is named after, exposed so the shader's arithmetic can be checked
         * against a reference rather than against a screenshot. Zero at the focus distance, growing
         * on both sides of it, and asymptotic in front of the lens.
         *
         * @param depth         Distance from the camera to the surface, in world units.
         * @param focusDistance Distance the lens is focused at, in world units.
         * @param focalLength   Focal length in millimetres.
         * @param fNumber       The f-number (aperture ratio); smaller means a shallower field.
         * @return The circle's diameter in millimetres, or 0 for a degenerate configuration.
         */
        [[nodiscard]] static float circleOfConfusionMillimetres(float depth, float focusDistance,
                                                                float focalLength, float fNumber);

        /** @brief Returns the distance the lens is focused at, in world units. */
        [[nodiscard]] float getFocusDistance() const;
        /**
         * @brief Sets the distance the lens is focused at, in world units.
         *
         * @param value The distance. Values at or below zero are ignored.
         */
        void setFocusDistance(float value);

        /** @brief Returns the focal length in millimetres. */
        [[nodiscard]] float getFocalLength() const;
        /**
         * @brief Sets the focal length in millimetres.
         *
         * @param value The focal length. Values at or below zero are ignored.
         */
        void setFocalLength(float value);

        /** @brief Returns the f-number. */
        [[nodiscard]] float getFNumber() const;
        /**
         * @brief Sets the f-number, the ratio of focal length to aperture diameter.
         *
         * Smaller opens the aperture and shortens the depth of field, exactly as on a lens.
         *
         * @param value The f-number. Values at or below zero are ignored.
         */
        void setFNumber(float value);

        /** @brief Returns the largest blur radius the pass will use, in screen fractions. */
        [[nodiscard]] float getMaxRadius() const;
        /**
         * @brief Sets the largest blur radius the pass will use, in screen fractions.
         *
         * The optics can ask for a circle far wider than a gather can afford, so this caps it. It
         * is a budget rather than a look: raising it costs nothing until something is far enough
         * out of focus to reach it.
         *
         * @param value The radius as a fraction of the frame, clamped to [0, 0.25].
         */
        void setMaxRadius(float value);

        /** @brief The sensor height the circle of confusion is measured against, in millimetres. */
        static constexpr float kSensorHeightMillimetres = 24.0f;

    private:
        std::unique_ptr<FullscreenPass> fullscreen_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> effect_;

        float focusDistance_ = 10.0f;
        float focalLength_   = 50.0f;
        float fNumber_       = 5.6f;
        float maxRadius_     = 0.02f;
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

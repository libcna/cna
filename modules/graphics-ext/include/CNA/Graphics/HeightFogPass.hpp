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
     * @brief Fades the scene into a fog whose density falls off with height.
     *
     * Not a distance fade with a height term bolted on: the fog is a medium whose density is
     * `density * exp(-falloff * (height - baseHeight))`, and what reaches the camera is that
     * density **integrated along the view ray**. A valley fills while the hilltop above it stays
     * clear, and looking down from the hill through the valley fogs correctly, because the integral
     * knows the ray passed through the thick part.
     *
     * The integral has a closed form, so there is no marching and no step count to tune. It is
     * exposed as @ref opticalDepth so the shader's arithmetic can be checked against a reference
     * rather than against a screenshot.
     *
     * It runs before tonemapping, on scene-referred values: fog replaces distant light with its own,
     * and mixing display-referred values would fade a bright sky and a white wall by the same
     * amount.
     */
    class HeightFogPass final : public PostProcessPass
    {
    public:
        /** @brief Creates the pass and compiles its shader. @param device The device to render with. */
        explicit HeightFogPass(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
        /** @brief Destroys the pass and its shader. */
        ~HeightFogPass() override;

        /** @brief Fogs @ref PostProcessContext::source into the destination. @param context The images, camera and size. */
        void apply(const PostProcessContext& context) override;
        /** @brief Returns `"HeightFog"`. */
        [[nodiscard]] const std::string& getName() const override;
        /** @brief Returns whether this renderer can run the pass. @param device The device queried. @return True when its shader compiled. */
        [[nodiscard]] bool isSupported(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const override;

        /**
         * @brief How much fog a view ray passes through, integrated along its length.
         *
         * The closed form of `∫ density·exp(-falloff·(h - baseHeight)) ds` along a straight ray.
         * Zero means a clear line of sight; larger values mean more of the surface's light was
         * replaced by the fog's own.
         *
         * @param cameraHeight Where the ray starts, in world units.
         * @param rayHeightStep How much the ray climbs per unit travelled -- the Y of its unit
         *                      direction. Zero is a level look, and is handled rather than divided by.
         * @param distance     How far the ray travels before it hits something, in world units.
         * @param density      Fog density at @p baseHeight.
         * @param falloff      How quickly density drops with height; larger is a shallower layer.
         * @param baseHeight   The height at which @p density applies.
         * @return The optical depth, never negative.
         */
        [[nodiscard]] static float opticalDepth(float cameraHeight, float rayHeightStep,
                                                float distance, float density, float falloff,
                                                float baseHeight);

        /** @brief Returns the fog's colour. */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getColor() const;
        /** @brief Sets the fog's colour, in scene-referred units. @param value The colour. */
        void setColor(const Microsoft::Xna::Framework::Vector3& value);

        /** @brief Returns the density at the base height; zero disables the pass. */
        [[nodiscard]] float getDensity() const;
        /** @brief Sets the density at the base height. @param value The density; negatives are ignored. */
        void setDensity(float value);

        /** @brief Returns how quickly density drops with height. */
        [[nodiscard]] float getFalloff() const;
        /**
         * @brief Sets how quickly density drops with height.
         *
         * @param value The falloff per world unit; larger makes a shallower layer. Values at or
         *              below zero are ignored -- a falloff of zero is uniform fog, which this pass
         *              is not, and the closed form divides by it.
         */
        void setFalloff(float value);

        /** @brief Returns the height at which the density applies. */
        [[nodiscard]] float getBaseHeight() const;
        /** @brief Sets the height at which the density applies. @param value The height in world units. */
        void setBaseHeight(float value);

    private:
        std::unique_ptr<FullscreenPass> fullscreen_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> effect_;

        Microsoft::Xna::Framework::Vector3 color_{0.62f, 0.68f, 0.78f};
        float density_    = 0.0f;
        float falloff_    = 0.1f;
        float baseHeight_ = 0.0f;
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

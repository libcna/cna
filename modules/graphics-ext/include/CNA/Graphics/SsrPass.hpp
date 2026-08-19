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
     * @brief Reflects the scene in itself by marching rays through the depth buffer.
     *
     * The reflection of a surface is found by walking the reflected ray forward in view space,
     * projecting each step back to a screen position, and asking the depth image whether anything
     * is standing there. Where something is, its colour is the reflection; where nothing is, the
     * pixel keeps the colour it had.
     *
     * It reads the same two images `SsaoPass` does -- @ref PostProcessContext::sourceDepth and
     * @ref PostProcessContext::sourceNormals, produced by `DepthNormalPrepass` -- and needs no
     * other input. Without them the pass reports `isSupported() == false` and copies its source
     * through, exactly as SSAO does when the prepass was never run.
     *
     * **What screen-space reflection cannot do.** Only what is already on screen can be reflected.
     * A surface facing away from the camera, an object outside the viewport, and anything hidden
     * behind nearer geometry have no colour in the source image, so they have no reflection. This
     * is the defining limit of the technique rather than a shortcoming of this implementation, and
     * a scene that depends on reflecting off-screen content needs an environment map instead --
     * `ImageBasedLightEXT`, which this pass does not replace.
     */
    class SsrPass final : public PostProcessPass
    {
    public:
        /**
         * @brief Creates the pass and compiles its shader.
         *
         * @param device The device to render with.
         */
        explicit SsrPass(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /** @brief Destroys the pass and its shader. */
        ~SsrPass() override;

        /**
         * @brief Reflects @ref PostProcessContext::source into the destination.
         *
         * @param context The images, camera and size for this invocation.
         */
        void apply(const PostProcessContext& context) override;

        /** @brief Returns `"SSR"`. */
        [[nodiscard]] const std::string& getName() const override;

        /**
         * @brief Returns whether this renderer can run the pass.
         *
         * @param device The device whose renderer is queried.
         * @return True when the renderer executes shader source and the shader compiled.
         */
        [[nodiscard]] bool isSupported(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const override;

        /** @brief Returns how far a reflected ray travels, in world units. */
        [[nodiscard]] float getMaxDistance() const;
        /**
         * @brief Sets how far a reflected ray travels, in world units.
         *
         * @param value The distance. Values at or below zero are ignored.
         */
        void setMaxDistance(float value);

        /** @brief Returns how many steps a reflected ray is marched in. */
        [[nodiscard]] int getStepCount() const;
        /**
         * @brief Sets how many steps a reflected ray is marched in.
         *
         * More steps find thinner occluders at a proportional cost. The value is clamped to the
         * range the shader accepts on use rather than on assignment.
         *
         * @param value The step count.
         */
        void setStepCount(int value);

        /** @brief Returns how far behind a surface a ray may pass and still count as a hit. */
        [[nodiscard]] float getThickness() const;
        /**
         * @brief Sets how far behind a surface a ray may pass and still count as a hit.
         *
         * The depth image records one distance per pixel and says nothing about how deep the
         * object behind it is, so this stands in for that missing thickness. Too small and rays
         * pass through everything; too large and a ray reflects off a surface it flew well behind.
         *
         * @param value The tolerance in world units. Values at or below zero are ignored.
         */
        void setThickness(float value);

        /** @brief Returns how far past a surface a ray must travel before a hit counts. */
        [[nodiscard]] float getDepthBias() const;
        /**
         * @brief Sets how far past a surface a ray must travel before a hit counts.
         *
         * The first step of a ray leaving a flat surface is still level with that surface, so
         * without this every mirror reflects its own colour. It is the same trade `ShadowMap`'s
         * depth bias makes: too small and a surface reflects itself, too large and a reflection
         * close to the surface -- the contact where an object meets the floor -- goes missing.
         *
         * @param value The bias in world units. Values at or below zero are ignored.
         */
        void setDepthBias(float value);

        /** @brief Returns how wide the fade at the edge of the screen is, in screen fractions. */
        [[nodiscard]] float getEdgeFade() const;
        /**
         * @brief Sets how wide the fade at the edge of the screen is, in screen fractions.
         *
         * Nothing outside the viewport was ever drawn, so a reflection ending near the border is
         * about to reflect information the frame does not have. Fading it is the difference between
         * a reflection that thins away and one that stops along a hard line down the edge of the
         * screen, which is the usual giveaway of the technique. Zero disables the fade.
         *
         * @param value The width as a fraction of the frame, clamped to [0, 0.5].
         */
        void setEdgeFade(float value);

        /** @brief Returns how strongly the reflection is mixed over the source. */
        [[nodiscard]] float getIntensity() const;
        /**
         * @brief Sets how strongly the reflection is mixed over the source.
         *
         * @param value 0 leaves the frame untouched, 1 shows the reflection at full strength.
         */
        void setIntensity(float value);

        /** @brief The smallest step count the shader will march. */
        static constexpr int kMinStepCount = 4;
        /** @brief The largest step count the shader will march. */
        static constexpr int kMaxStepCount = 64;

    private:
        std::unique_ptr<FullscreenPass> fullscreen_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> effect_;

        float maxDistance_ = 8.0f;
        int   stepCount_   = 32;
        float thickness_   = 0.5f;
        float depthBias_   = 0.05f;
        float edgeFade_    = 0.1f;
        float intensity_   = 1.0f;
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

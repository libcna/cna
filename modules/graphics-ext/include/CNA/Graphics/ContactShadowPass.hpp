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
     * @brief Screen-space contact shadows: the short-range shadow a shadow map cannot resolve.
     *
     * A shadow map answers "is this point visible from the light" at the resolution of its texture.
     * Where an object meets the surface it rests on, the answer changes over a distance far smaller
     * than one shadow texel, so the map cannot represent it at any resolution a real frame can
     * afford -- the object floats, and no amount of extra shadow-map pixels fixes it, because the
     * error is at the shadow map's own filter width rather than in its content.
     *
     * This pass fills exactly that gap. For each pixel it walks a short ray from the surface toward
     * the light through the prepass depth image and asks whether anything on screen crosses it. A
     * hit within the ray's length darkens the pixel. The range is deliberately short: the ray is
     * paying for the last few centimetres a shadow map rounds away, not for the shadow itself.
     *
     * **It is a complement, not a replacement.** It sees only what the camera sees, so an occluder
     * off screen or hidden behind the surface casts nothing, and the shadow it produces ends where
     * the march does. A scene rendered with contact shadows and no shadow map has short dark
     * contacts and nothing else. The two combine by multiplying their visibility -- see
     * @ref combineVisibility for why that and not adding their occlusions.
     *
     * Requires @ref PostProcessContext::sourceDepth, a projection and its inverse, a far plane and
     * @ref PostProcessContext::inverseView. Without any of them the pass copies its input through
     * and names what was missing in @ref getFallbackReason, so a misconfigured pipeline renders an
     * unshadowed frame rather than failing.
     */
    class ContactShadowPass final : public PostProcessPass
    {
    public:
        /**
         * @brief Creates the pass and compiles its shader.
         *
         * @param device The device to render with.
         */
        explicit ContactShadowPass(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /** @brief Destroys the pass and its resources. */
        ~ContactShadowPass() override;

        /**
         * @brief Darkens the context's source image where the short ray finds an occluder.
         *
         * @param context The images, camera parameters and settings for this invocation.
         */
        void apply(const PostProcessContext& context) override;

        /** @brief Returns `"ContactShadow"`. */
        [[nodiscard]] const std::string& getName() const override;

        /**
         * @brief Returns whether this renderer can run the pass.
         *
         * @param device The device whose renderer is queried.
         * @return True when custom effects are supported and the shader compiled.
         */
        [[nodiscard]] bool isSupported(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const override;

        /**
         * @brief Returns the direction the light travels, in world space.
         *
         * @return The light's travel direction, as it was set.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getLightDirection() const;

        /**
         * @brief Sets the direction the light travels, in world space.
         *
         * The same convention as @ref DirectionalLightEXT::Direction -- the direction light moves
         * in, not the direction toward the light -- so the two can be fed from one value. It is
         * normalized on use rather than on assignment, and transformed into view space per frame
         * from the context's inverse view matrix.
         *
         * @param value The direction the light travels.
         */
        void setLightDirection(const Microsoft::Xna::Framework::Vector3& value);

        /** @brief Returns how far the ray travels, in world units. */
        [[nodiscard]] float getMaxDistance() const;

        /**
         * @brief Sets how far the ray travels, in world units.
         *
         * This is the whole character of the pass. Short is the point: a metre-long ray is a bad,
         * noisy, expensive shadow map, while a few centimetres is the detail a shadow map cannot
         * hold. Longer rays also miss more, because each step covers more screen distance and the
         * march walks over thin geometry between samples.
         *
         * @param value The ray length in world units; clamped to a positive value on use.
         */
        void setMaxDistance(float value);

        /** @brief Returns the number of samples taken along the ray. */
        [[nodiscard]] int getStepCount() const;

        /**
         * @brief Sets the number of samples taken along the ray.
         *
         * Clamped to 4..64 on use. Cost is linear in this, and so is the smallest occluder the
         * march can see: with the ray length fixed, halving the steps doubles the gap a thin
         * object can hide in.
         *
         * @param value The step count.
         */
        void setStepCount(int value);

        /** @brief Returns the assumed occluder thickness, in world units. */
        [[nodiscard]] float getThickness() const;

        /**
         * @brief Sets the assumed occluder thickness, in world units.
         *
         * A depth image records where a surface is and nothing at all about how far back the object
         * behind it goes, so this number stands in for a thickness the pass cannot know. It is not
         * a quality dial with a right answer; it decides which of two wrong answers the pass gives.
         * Too small and a ray that really did pass behind a solid object reports no hit, so the
         * shadow disappears at the far end of every occluder. Too large and a thin occluder -- a
         * railing, a leaf -- shadows everything behind it as though it were a solid block.
         *
         * @param value The thickness in world units.
         */
        void setThickness(float value);

        /** @brief Returns how much of the light a hit removes, 0..1. */
        [[nodiscard]] float getIntensity() const;

        /**
         * @brief Sets how much of the light a hit removes, 0..1.
         *
         * @param value The strength; clamped to 0..1 on use.
         */
        void setIntensity(float value);

        /** @brief Returns the depth tolerance that keeps a surface from shadowing itself. */
        [[nodiscard]] float getBias() const;

        /**
         * @brief Sets the depth tolerance that keeps a surface from shadowing itself.
         *
         * The first step of a ray leaving a flat surface is still level with that surface, so the
         * depth difference is zero in exact arithmetic and a few ULPs either side of it in real
         * arithmetic. Without this, half the pixels of every flat lit surface find themselves.
         *
         * @param value The tolerance in world units.
         */
        void setBias(float value);

        /**
         * @brief Returns why the last @ref apply copied its input through, or an empty string.
         *
         * The fallback is silent by construction -- an unshadowed frame looks like a frame nobody
         * asked for contact shadows in. This is the difference between the two.
         *
         * @return The reason, or an empty string when the pass ran.
         */
        [[nodiscard]] const std::string& getFallbackReason() const;

        /**
         * @brief Whether a hit is recorded for one step of the march.
         *
         * The CPU twin of the shader's own `cnaContactOccluded`, written separately and compared
         * against it on the GPU rather than assumed equal.
         *
         * @param rayViewDepth   Distance from the eye to this point on the ray, in world units.
         * @param sceneViewDepth Distance from the eye to whatever the depth image holds there.
         * @param bias           The self-shadowing tolerance.
         * @param thickness      The assumed occluder thickness.
         * @return True when the depth image holds a surface in front of the ray, but not so far in
         *         front that the ray has passed behind it.
         */
        [[nodiscard]] static bool isOccluded(float rayViewDepth, float sceneViewDepth, float bias,
                                             float thickness);

        /**
         * @brief The GLSL declaration of the shader's occlusion test.
         *
         * Exposed so a test can compile the shader's own predicate and compare it against
         * @ref isOccluded on the GPU, which is the only comparison that proves the two agree.
         *
         * @return A GLSL fragment declaring `bool cnaContactOccluded(float, float, float, float)`.
         */
        [[nodiscard]] static std::string getOcclusionTestGlsl();

        /**
         * @brief Combines a shadow map's visibility with this pass's.
         *
         * They multiply. The tempting alternative is to add their occlusions -- `1 - (s + c)` --
         * and it produces the artefact that gives screen-space shadows their reputation: the two
         * terms agree over most of a real contact, so wherever both fire the pixel is darkened
         * twice and the shadow gains a black core with a visible edge around it. A product cannot
         * do that. It is also the composition a screen-space pass gets for free, because the image
         * it multiplies into already carries the shadow map's term.
         *
         * @param shadowMapVisibility The shadow map's term, 0 fully shadowed to 1 fully lit.
         * @param contactVisibility   This pass's term, on the same scale.
         * @return The combined visibility, on the same scale.
         */
        [[nodiscard]] static float combineVisibility(float shadowMapVisibility,
                                                     float contactVisibility);

    private:
        std::unique_ptr<FullscreenPass> fullscreen_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> effect_;

        Microsoft::Xna::Framework::Vector3 lightDirection_{0.0f, -1.0f, 0.0f};
        float maxDistance_ = 0.25f;
        float thickness_   = 0.15f;
        float intensity_   = 1.0f;
        float bias_        = 0.02f;
        int   stepCount_   = 12;
        std::string fallbackReason_;
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

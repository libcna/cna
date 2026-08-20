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
     * @brief Smears the frame along the direction each pixel moved since the last one.
     *
     * The movement is worked out rather than stored: a pixel's world position comes from the depth
     * image and the camera, and putting that position through the *previous* frame's camera says
     * where it used to be on screen. The difference is its velocity, and the pass blurs along it.
     *
     * **This is camera motion only.** A turning or advancing camera blurs correctly; a car crossing
     * a static shot does not, because nothing in the depth image says the car moved rather than the
     * world. Per-object velocity needs a third prepass output and a previous world matrix per draw,
     * which is a contract change on the application -- `plan_modern.md` `MOD-2033` -- rather than
     * something this pass can infer.
     *
     * The first frame after a start or a resize has no history, and the pass leaves it alone rather
     * than blurring it along an arbitrary direction.
     */
    class MotionBlurPass final : public PostProcessPass
    {
    public:
        /** @brief Creates the pass and compiles its shader. @param device The device to render with. */
        explicit MotionBlurPass(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
        /** @brief Destroys the pass and its shader. */
        ~MotionBlurPass() override;

        /** @brief Blurs @ref PostProcessContext::source along its motion. @param context The images, cameras and size. */
        void apply(const PostProcessContext& context) override;
        /** @brief Returns `"MotionBlur"`. */
        [[nodiscard]] const std::string& getName() const override;
        /** @brief Returns whether this renderer can run the pass. @param device The device queried. @return True when its shader compiled. */
        [[nodiscard]] bool isSupported(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const override;

        /** @brief Returns how much of a pixel's movement is smeared. */
        [[nodiscard]] float getStrength() const;
        /**
         * @brief Sets how much of a pixel's movement is smeared.
         *
         * 1 smears the whole distance travelled since the last frame, which is what a camera with a
         * 360-degree shutter would record; a real shutter is open for part of the frame, so lower
         * values are the physical ones.
         *
         * @param value The fraction, clamped to [0, 1]. Zero disables the pass.
         */
        void setStrength(float value);

        /** @brief Returns the furthest a pixel may be smeared, in screen fractions. */
        [[nodiscard]] float getMaxDistance() const;
        /**
         * @brief Sets the furthest a pixel may be smeared, in screen fractions.
         *
         * A single slow frame makes every velocity enormous, and without a cap one stutter smears
         * the whole image. This is what keeps a hitch from looking like a bug in the blur.
         *
         * @param value The distance as a fraction of the frame, clamped to [0, 0.25].
         */
        void setMaxDistance(float value);

        /** @brief How many samples are taken along the velocity. */
        static constexpr int kSampleCount = 8;

    private:
        std::unique_ptr<FullscreenPass> fullscreen_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> effect_;
        float strength_    = 0.0f;
        float maxDistance_ = 0.05f;
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

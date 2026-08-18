// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/Graphics/PostProcessPass.hpp"
#include "CNA/Graphics/RenderTargetPool.hpp"

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
     * @brief Adds the light bleed a bright source produces in a real lens.
     *
     * Three stages: extract the pixels above the threshold, blur them through a chain of
     * half-resolution steps, then add the result back onto the scene. Working at successively
     * lower resolutions is what makes a wide blur affordable -- the alternative, a single blur
     * wide enough to look right, costs an order of magnitude more taps for the same spread.
     *
     * Bloom is an HDR effect but not an HDR-only one. With a float scene target the threshold
     * separates genuinely bright pixels from merely white ones, which is the effect people mean by
     * bloom; on an 8-bit target a threshold below 1.0 still produces a usable glow, and the pass
     * says so rather than refusing to run.
     */
    class BloomPass final : public PostProcessPass
    {
    public:
        /**
         * @brief Creates the pass and compiles its three shaders.
         *
         * A compilation failure is not thrown: the pass reports `isSupported() == false` and
         * copies its input instead, so a renderer-specific shader problem cannot take down a game
         * that merely enabled bloom.
         *
         * @param device The device to render with.
         */
        explicit BloomPass(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /** @brief Destroys the pass, its shaders and its intermediate targets. */
        ~BloomPass() override;

        /**
         * @brief Blooms @ref PostProcessContext::source into @ref PostProcessContext::destination.
         *
         * @param context The images, size and settings for this invocation.
         */
        void apply(const PostProcessContext& context) override;

        /** @brief Returns `"Bloom"`. */
        [[nodiscard]] const std::string& getName() const override;

        /**
         * @brief Returns whether this renderer can run the pass.
         *
         * @param device The device whose renderer is queried.
         * @return True when custom effects are supported and all three shaders compiled.
         */
        [[nodiscard]] bool isSupported(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const override;

        /** @brief Returns the luminance threshold used when the context carries no settings. */
        [[nodiscard]] float getThreshold() const;
        /** @brief Sets the luminance threshold used when the context carries no settings. */
        void setThreshold(float value);

        /** @brief Returns the intensity used when the context carries no settings. */
        [[nodiscard]] float getIntensity() const;
        /** @brief Sets the intensity used when the context carries no settings. */
        void setIntensity(float value);

        /** @brief Returns the number of half-resolution steps used when no settings are supplied. */
        [[nodiscard]] int getIterations() const;
        /**
         * @brief Sets the number of half-resolution steps.
         *
         * Clamped to 1..8 on use, and further reduced when the viewport runs out of resolution --
         * a chain step below 2 pixels contributes nothing but a draw call.
         */
        void setIterations(int value);

        /** @brief Releases the intermediate targets. Call on resize. */
        void resetTargets();

        /**
         * @brief Computes the bloom contribution of one channel value on the CPU.
         *
         * The extract stage's soft-knee curve, reproduced exactly. Public for the same reason
         * `TonemapPass::tonemapChannel` is: it lets a test assert that the shader implements the
         * intended curve rather than merely producing a plausible image.
         *
         * @param value     The linear channel value.
         * @param threshold The luminance threshold.
         * @return The extracted value, zero below the threshold.
         */
        [[nodiscard]] static float extractChannel(float value, float threshold);

    private:
        std::unique_ptr<FullscreenPass> fullscreen_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> extractEffect_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> blurEffect_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> combineEffect_;
        RenderTargetPool pool_;

        float threshold_  = 1.0f;
        float intensity_  = 1.0f;
        int   iterations_ = 4;
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

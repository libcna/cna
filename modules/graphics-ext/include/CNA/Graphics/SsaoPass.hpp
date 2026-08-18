// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/Graphics/PostProcessPass.hpp"
#include "CNA/Graphics/RenderQuality.hpp"
#include "CNA/Graphics/RenderTargetPool.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics {
    class ShaderEffect;
    class Texture2D;
}

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Darkens creases and contact points using scene depth and normals.
     *
     * Ambient light in the direct-lighting model arrives equally from everywhere, which is exactly
     * wrong in a corner: a surface with geometry crowding around it receives less of it. SSAO
     * estimates that occlusion from what is already on screen -- for each pixel, it samples points
     * in the hemisphere around the surface and asks how many of them are behind the depth buffer.
     *
     * It is the one post-process in this layer that needs more than the colour image. Depth and
     * view-space normals must be supplied through @ref PostProcessContext, along with the camera
     * projection used to render them. Without those the pass cannot run and copies its input
     * instead, so a pipeline that enables SSAO without a prepass renders an unoccluded frame
     * rather than failing.
     *
     * The occlusion is applied as a multiply over the whole image. That deliberately darkens
     * direct light along with ambient, which is not physically right; doing it correctly means
     * feeding the AO term into each lit effect's ambient contribution, which is a change to every
     * lit shader rather than a post-process. The screen-space multiply is the standard
     * approximation and is documented as such rather than presented as correct.
     */
    class SsaoPass final : public PostProcessPass
    {
    public:
        /**
         * @brief Creates the pass, compiles its shaders and generates its sampling kernel.
         *
         * @param device The device to render with.
         */
        explicit SsaoPass(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /** @brief Destroys the pass and its resources. */
        ~SsaoPass() override;

        /**
         * @brief Applies ambient occlusion to the context's source image.
         *
         * Requires @ref PostProcessContext::sourceDepth, @ref PostProcessContext::sourceNormals
         * and a projection matrix; copies its input unchanged when any of them is missing.
         *
         * @param context The images, camera parameters and settings for this invocation.
         */
        void apply(const PostProcessContext& context) override;

        /** @brief Returns `"SSAO"`. */
        [[nodiscard]] const std::string& getName() const override;

        /**
         * @brief Returns whether this renderer can run the pass.
         *
         * @param device The device whose renderer is queried.
         * @return True when custom effects are supported and both shaders compiled.
         */
        [[nodiscard]] bool isSupported(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const override;

        /** @brief Returns the sampling radius in world units, used when no settings are supplied. */
        [[nodiscard]] float getRadius() const;
        /** @brief Sets the sampling radius in world units. */
        void setRadius(float value);

        /** @brief Returns the occlusion strength multiplier. */
        [[nodiscard]] float getIntensity() const;
        /** @brief Sets the occlusion strength multiplier. */
        void setIntensity(float value);

        /** @brief Returns the number of hemisphere samples per pixel. */
        [[nodiscard]] int getSampleCount() const;
        /**
         * @brief Sets the number of hemisphere samples per pixel.
         *
         * Clamped to 8..64 on use. Fewer samples are noisier rather than wrong, which the blur
         * then has to hide; more cost linearly.
         */
        void setSampleCount(int value);

        /** @brief Releases the intermediate targets. Call on resize. */
        void resetTargets();

        /**
         * @brief Returns the generated hemisphere kernel.
         *
         * Exposed so its distribution can be asserted rather than eyeballed: every sample must lie
         * in the +Z hemisphere and the set must be biased toward the origin, which is what makes
         * near-field contact darker than distant geometry.
         *
         * @return The kernel offsets, in tangent space.
         */
        [[nodiscard]] const std::vector<Microsoft::Xna::Framework::Vector3>& getKernel() const;

        /**
         * @brief Returns the hemisphere sample count a quality preset asks for.
         *
         * plan_modern.md `MOD-522`. A function rather than a side effect, for the reason
         * `MOD-409` records: `setRenderQuality` rewrites nothing, and
         * `RenderPipelineSettings::applyRenderQualityPresetEXT()` is the explicit step.
         *
         * | Quality | Samples |
         * |---|---|
         * | `Low`    | 8 |
         * | `Medium` | 16 |
         * | `High`   | 32 |
         * | `Ultra`  | 64 |
         *
         * Unlike bloom's level count, this **is** a real performance dial: the occlusion shader
         * loops over the kernel per texel, so the cost is very nearly linear in the sample count.
         * See `docs/cnaext-perf.md`.
         *
         * @param quality The preset to translate.
         * @return The sample count, within the range `apply()` accepts.
         */
        [[nodiscard]] static int sampleCountForQuality(RenderQuality quality);

        /**
         * @brief Whether the occlusion buffer is computed at half resolution.
         *
         * @return True when the half-resolution path is in use.
         */
        [[nodiscard]] bool isHalfResolution() const;

        /**
         * @brief Computes occlusion at half resolution and upsamples it.
         *
         * plan_modern.md `MOD-523`. **Off by default.** AO is a low-frequency signal — a blurred
         * estimate of a neighbourhood — so halving the resolution costs much less quality than it
         * looks like it should, and the compose pass's bilinear read is the upsample. It is not
         * free, though: thin contact shadows lose definition, which is exactly where AO earns its
         * keep, so this is offered rather than assumed.
         *
         * @param value True to compute at half resolution.
         */
        void setHalfResolution(bool value);

    private:
        void generateKernel();

        std::unique_ptr<FullscreenPass> fullscreen_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> occlusionEffect_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> composeEffect_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> noiseTexture_;
        RenderTargetPool pool_;

        std::vector<Microsoft::Xna::Framework::Vector3> kernel_;
        float radius_      = 0.5f;
        float intensity_   = 1.0f;
        int   sampleCount_ = 16;
        bool  halfResolution_ = false;
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

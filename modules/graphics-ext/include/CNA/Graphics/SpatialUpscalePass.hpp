// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/FullscreenPass.hpp"

#include <memory>
#include <string>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class ShaderEffect;
    class Texture2D;
}

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Renders at a lower resolution and puts the result on screen at full size.
     *
     * The cheapest performance dial there is: everything before this pass runs at, say, two thirds
     * the pixels, and this pass is the only thing that touches all of them. What makes it worth
     * having over the hardware's own bilinear stretch is that it is **edge-aware** -- it works out
     * which way an edge runs and filters *along* it rather than across it, so a diagonal comes out
     * as a line rather than as a staircase.
     *
     * Two stages, which is FSR 1's shape: an edge-adaptive upsample, then a contrast-adaptive
     * sharpen that is clamped to the neighbourhood it sharpened from -- which is what stops a
     * sharpening filter ringing.
     *
     * **This is written from the published description of that shape, not from a vendor SDK**, and
     * it is not bit-identical to AMD's reference implementation. It is pure shader arithmetic with
     * no library to link, which is the reason it can live in this layer at all.
     *
     * **At a 1:1 scale it copies through, pixel for pixel.** A pass with nothing to do that changed
     * the image anyway would make the resolution dial impossible to calibrate against.
     */
    class SpatialUpscalePass
    {
    public:
        /**
         * @brief Creates the pass and compiles its shaders.
         *
         * @param device The device to render with.
         */
        explicit SpatialUpscalePass(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /** @brief Destroys the pass and its shaders. */
        ~SpatialUpscalePass();

        SpatialUpscalePass(const SpatialUpscalePass&)            = delete;
        SpatialUpscalePass& operator=(const SpatialUpscalePass&) = delete;

        /** @brief Returns whether this renderer can run the pass. */
        [[nodiscard]] bool isSupported() const;

        /**
         * @brief Draws the source across the currently bound target, upscaled.
         *
         * @param source       The lower-resolution image.
         * @param sourceWidth  Its width in pixels.
         * @param sourceHeight Its height in pixels.
         * @param targetWidth  The bound target's width in pixels.
         * @param targetHeight The bound target's height in pixels.
         * @throws std::invalid_argument If any dimension is not positive, or the source is null.
         */
        void draw(Microsoft::Xna::Framework::Graphics::Texture2D* source, int sourceWidth,
                  int sourceHeight, int targetWidth, int targetHeight);

        /** @brief Returns how strongly the second stage sharpens. */
        [[nodiscard]] float getSharpness() const;
        /**
         * @brief Sets how strongly the second stage sharpens.
         *
         * @param value 0 disables the sharpen entirely; 1 is the strongest the clamp allows.
         *              Clamped to [0, 1].
         */
        void setSharpness(float value);

        /** @brief Returns whether the upsample is edge-adaptive rather than plain bilinear. */
        [[nodiscard]] bool isEdgeAdaptive() const;
        /**
         * @brief Turns the edge-adaptive stage off, leaving a bilinear stretch.
         *
         * Offered because it is the comparison a game actually wants to make -- and because a test
         * that claims the adaptive path helps has to be able to run the one it beats.
         *
         * @param value True for the edge-adaptive filter, false for plain bilinear.
         */
        void setEdgeAdaptive(bool value);

        /**
         * @brief Returns whether a given scale is the identity this pass copies through.
         *
         * @param sourceWidth  The source width.
         * @param sourceHeight The source height.
         * @param targetWidth  The target width.
         * @param targetHeight The target height.
         * @return True when the two sizes are equal, so nothing is resampled.
         */
        [[nodiscard]] static bool isIdentityScale(int sourceWidth, int sourceHeight,
                                                  int targetWidth, int targetHeight);

    private:
        std::unique_ptr<FullscreenPass> fullscreen_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> effect_;

        float sharpness_ = 0.4f;
        bool  edgeAdaptive_ = true;
        bool  supported_ = false;
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

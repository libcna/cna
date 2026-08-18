// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include <memory>
#include <vector>

namespace Microsoft::Xna::Framework {
    struct Color;
    struct Vector3;
}

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class Texture2D;
    class TextureCube;
}

namespace CNA::Graphics {

    /**
     * @brief Turns environment images into the forms the renderer can sample.
     *
     * plan_modern.md Phase 11/12. The first thing it does is the one every HDR environment needs:
     * the panoramas people actually have are **equirectangular** -- one wide image, longitude
     * across and latitude down -- and a renderer samples a **cube**. Nothing else in the sky or IBL
     * path can start until that conversion exists.
     *
     * The conversion runs on the CPU rather than as six render passes. That is a deliberate trade
     * and worth stating: a render-to-cube version would be faster, but it would need float render
     * targets, cube render targets and custom effects all present, and this has to work on the
     * renderers that have none of them -- the conversion is a load-time cost paid once, so the
     * faster path buys very little and costs a capability gate.
     */
    class EnvironmentProcessor
    {
    public:
        /**
         * @brief Creates a processor for one device.
         *
         * @param device The device the generated textures are created on.
         */
        explicit EnvironmentProcessor(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /** @brief Destroys the processor. Generated textures it returned are owned by the caller. */
        ~EnvironmentProcessor();

        EnvironmentProcessor(const EnvironmentProcessor&)            = delete;
        EnvironmentProcessor& operator=(const EnvironmentProcessor&) = delete;

        /**
         * @brief Converts an equirectangular panorama into a cube map.
         *
         * @param panorama Longitude across, latitude down: the layout every HDR sky ships in.
         *                 Its width should be twice its height; other ratios are accepted and
         *                 sampled as given rather than refused, because a slightly-off panorama is
         *                 far more common than a deliberate one and refusing it helps nobody.
         * @param faceSize Edge length of each generated face, in pixels. A quarter of the
         *                 panorama's width is the usual choice and roughly preserves detail.
         * @return The cube map, owned by the caller.
         * @throws std::invalid_argument If @p panorama is null or @p faceSize is not positive.
         */
        [[nodiscard]] std::unique_ptr<Microsoft::Xna::Framework::Graphics::TextureCube>
        convertEquirectangular(Microsoft::Xna::Framework::Graphics::Texture2D* panorama,
                               int faceSize);

        /**
         * @brief Returns the world direction a cube face's texel looks along.
         *
         * The convention in one place, so the converter and any test agree by construction rather
         * than by both being written from the same diagram. Exposed because a face wired the wrong
         * way round still produces a complete cube -- one that is mirrored in a sixth of the sky.
         *
         * @param face  The face index, 0 to 5, in `CubeMapFace` order.
         * @param u     Horizontal position across the face, 0 to 1.
         * @param v     Vertical position down the face, 0 to 1.
         * @return The normalized direction.
         */
        [[nodiscard]] static Microsoft::Xna::Framework::Vector3 faceDirection(int face, float u,
                                                                              float v);

        /**
         * @brief Returns the equirectangular texel a direction maps to.
         *
         * The inverse of @ref faceDirection's job, and the other half of the pair a conversion has
         * to get consistent. Longitude runs across, latitude down, with -Z at the centre of the
         * image -- the convention the panorama formats use.
         *
         * @param direction The direction; need not be normalized.
         * @param u         Receives the horizontal coordinate, 0 to 1.
         * @param v         Receives the vertical coordinate, 0 to 1.
         */
        static void directionToEquirectangular(const Microsoft::Xna::Framework::Vector3& direction,
                                                float& u, float& v);

    private:
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
    };

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

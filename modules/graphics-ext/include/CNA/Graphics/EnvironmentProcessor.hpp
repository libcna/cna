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

/** @addtogroup cnaext_engine
 *  @{
 */

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
         * @brief Convolves an environment into a diffuse irradiance cube.
         *
         * plan_modern.md `MOD-1202`. Each output texel is the cosine-weighted average of every
         * direction in the hemisphere around it -- what a matte surface facing that way receives
         * from the whole environment. It is deliberately small: irradiance is a very low-frequency
         * signal, and 32 pixels a face is already more than the function has detail to fill.
         *
         * @param environment The source cube. Must not be null.
         * @param size        Edge length of each irradiance face.
         * @param sampleCount Samples per axis of the hemisphere sweep; the cost is its square.
         *                    16 is fast and slightly noisy, 64 is smooth and slow.
         * @return The irradiance cube, owned by the caller.
         * @throws std::invalid_argument If the environment is null, or a size is not positive.
         */
        [[nodiscard]] std::unique_ptr<Microsoft::Xna::Framework::Graphics::TextureCube>
        generateIrradiance(Microsoft::Xna::Framework::Graphics::TextureCube* environment,
                           int size = 32, int sampleCount = 32);

        /**
         * @brief Prefilters an environment for specular reflection, one mip per roughness.
         *
         * plan_modern.md `MOD-1204`. The split-sum approximation's first half: mip 0 is the
         * environment as it is (a mirror), and each mip after it is the GGX lobe for a rougher
         * surface. A shader then reads the roughness it needs as a mip level, which is why the
         * mapping between the two has to be one function -- @ref mipForRoughness -- rather than
         * two that happen to agree.
         *
         * @param environment The source cube. Must not be null.
         * @param baseSize    Edge length of mip 0.
         * @param mipCount    Number of mips; each is half the previous.
         * @param sampleCount Importance samples per texel.
         * @return The prefiltered cube, owned by the caller.
         * @throws std::invalid_argument If the environment is null, or any count is not positive.
         */
        [[nodiscard]] std::unique_ptr<Microsoft::Xna::Framework::Graphics::TextureCube>
        generatePrefilteredSpecular(Microsoft::Xna::Framework::Graphics::TextureCube* environment,
                                    int baseSize = 128, int mipCount = 5, int sampleCount = 64);

        /**
         * @brief Generates the split-sum BRDF lookup table.
         *
         * plan_modern.md `MOD-1207`. The second half of the split sum, and the half that depends on
         * nothing but the BRDF: a 2D table indexed by (N·V, roughness) holding the scale and bias
         * to apply to a surface's F0. It is the same table for every scene, so it is generated once
         * and could equally be a shipped asset.
         *
         * Stored in the red and green channels of an 8-bit texture, which is a real precision
         * limit and is why `MOD-1208`'s two-format plan collapsed to one: CNA's `Texture2D` accepts
         * `SurfaceFormat::Color` and nothing else, so there is no `HalfVector2` path to choose.
         *
         * @param size        Edge length of the square table.
         * @param sampleCount Importance samples per entry.
         * @return The table, owned by the caller.
         * @throws std::invalid_argument If a size or count is not positive.
         */
        [[nodiscard]] std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D>
        generateBrdfLut(int size = 128, int sampleCount = 128);

        /**
         * @brief Returns the mip level a roughness reads from a prefiltered cube.
         *
         * The one place this mapping is written. Generation walks mips and asks what roughness each
         * one is; sampling holds a roughness and asks which mip it is. Two functions that happen to
         * agree is one edit away from a scene whose reflections sharpen as the surface gets rougher.
         *
         * @param roughness Surface roughness, 0 to 1; clamped.
         * @param mipCount  The prefiltered cube's mip count.
         * @return The mip level, as a float for trilinear sampling.
         */
        [[nodiscard]] static float mipForRoughness(float roughness, int mipCount);

        /** @brief The inverse of @ref mipForRoughness. @param mip The level. @param mipCount Levels. */
        [[nodiscard]] static float roughnessForMip(float mip, int mipCount);

        /**
         * @brief Returns the i-th point of the Hammersley sequence over @p count points.
         *
         * plan_modern.md `MOD-1206`. Exposed because every importance-sampled integral here rests
         * on it, and a radical inverse with a wrong bit twiddle produces a sequence that still
         * looks random and still converges -- to the wrong number.
         *
         * @param index The point index.
         * @param count The sequence length.
         * @param x     Receives the first coordinate.
         * @param y     Receives the second, the radical inverse.
         */
        static void hammersley(int index, int count, float& x, float& y);

        /**
         * @brief Importance-samples the GGX distribution around a normal.
         *
         * @param x         First Hammersley coordinate.
         * @param y         Second Hammersley coordinate.
         * @param normal    The surface normal to sample around.
         * @param roughness The GGX roughness.
         * @return The sampled half-vector, in world space.
         */
        [[nodiscard]] static Microsoft::Xna::Framework::Vector3 importanceSampleGgx(
            float x, float y, const Microsoft::Xna::Framework::Vector3& normal, float roughness);

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

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include <memory>
#include <string>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class Texture2D;
}

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief The BRDF terms an area light needs, generated rather than shipped.
     *
     * Shading a surface with a light that has *area* means integrating the BRDF over the light's
     * solid angle. The published way to do that cheaply is **linearly transformed cosines**: a
     * fitted 4-coefficient matrix per (roughness, view angle) turns the GGX lobe into a clamped
     * cosine, whose integral over a polygon has a closed form.
     *
     * **The fitted matrix is not generated here, and the reason is arithmetic rather than
     * preference.** That table is the output of a Nelder–Mead fit run per cell: at the published
     * 64×64 resolution, with the order of forty iterations each evaluating a 32×32 sample set,
     * generating it costs upwards of a hundred million BRDF evaluations -- seconds to minutes on a
     * CPU, every time a game starts. It is a build-time artefact, and this layer has no asset path
     * to ship one through (the constraint that made `MOD-610` refuse SMAA).
     *
     * What *is* cheap, and is what this table holds, are the terms that need only importance
     * sampling and no fit at all:
     *
     * - **magnitude** -- the BRDF's directional albedo, the fraction of incoming light a surface of
     *   this roughness reflects at this view angle. This is the term that makes the result
     *   *energetically* right, and it is what a reference integration can be compared against.
     * - **fresnel** -- the weight of the `(1 - V·H)^5` term, so F0 and F90 can be recombined
     *   afterwards rather than baked in.
     * - **the average reflection direction**, in the plane the view and normal span, measured
     *   against the reflection-side tangent so it stays positive. This is what
     *   `AreaLightShading` aims its representative point along, and it is where the lobe's
     *   off-specular tilt at grazing angles comes from.
     *
     * Stored in the four channels of an 8-bit `Color` texture, because CNA's `Texture2D` accepts
     * `SurfaceFormat::Color` and nothing else -- the same real precision limit `generateBrdfLut`
     * records. Every value is in [0, 1] by construction, so the encoding is a plain scale rather
     * than the byte-packing `ClusteredLightBuffer` needs.
     */
    class AreaLightBrdfTable
    {
    public:
        /** @brief Edge length of the table by default. */
        static constexpr int kDefaultSize = 32;

        /** @brief Importance samples per entry by default. */
        static constexpr int kDefaultSampleCount = 64;

        /** @brief The terms one (roughness, view angle) cell holds. */
        struct Terms
        {
            /** @brief The BRDF's directional albedo, 0 to 1. */
            float Magnitude = 0.0f;
            /** @brief The weight of the Schlick Fresnel term, 0 to 1. */
            float Fresnel = 0.0f;
            /**
             * @brief The average reflection direction's component along the reflection-side
             *        tangent, `normalize(N * (N·V) - V)`.
             *
             * Measured against that axis rather than against the view's own tangent because a
             * reflected direction leans *away* from the view, so its component along the view's
             * tangent is negative -- and a value that has to fit in [0, 1] to survive an 8-bit
             * texture would store a negative as zero and flatten the lobe's tilt away entirely.
             */
            float AverageTangent = 0.0f;
            /** @brief The average reflection direction's component along the normal. */
            float AverageNormal = 1.0f;
        };

        /**
         * @brief Generates the table and uploads it.
         *
         * @param device      The device to create the texture on.
         * @param size        Edge length of the square table; must be positive.
         * @param sampleCount Importance samples per entry; must be positive.
         * @throws std::invalid_argument When a size or count is not positive.
         */
        explicit AreaLightBrdfTable(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                                    int size = kDefaultSize,
                                    int sampleCount = kDefaultSampleCount);

        /** @brief Destroys the table and its texture. */
        ~AreaLightBrdfTable();

        AreaLightBrdfTable(const AreaLightBrdfTable&)            = delete;
        AreaLightBrdfTable& operator=(const AreaLightBrdfTable&) = delete;

        /**
         * @brief Computes one cell's terms directly, without a texture.
         *
         * The same routine the texture is filled from, so a test can check the optics rather than
         * the upload, and a caller can ask for a value the 8-bit table would have rounded.
         *
         * @param roughness   Surface roughness, 0 to 1; clamped away from zero.
         * @param cosTheta    The cosine of the view angle, N·V; clamped away from zero.
         * @param sampleCount Importance samples; must be positive.
         * @return The terms.
         * @throws std::invalid_argument When the sample count is not positive.
         */
        [[nodiscard]] static Terms evaluate(float roughness, float cosTheta, int sampleCount);

        /** @brief Returns the table's texture, indexed by (N·V, roughness). */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* getTexture() const;

        /** @brief Returns the table's edge length. */
        [[nodiscard]] int getSize() const;

        /** @brief Returns how many importance samples each entry was built from. */
        [[nodiscard]] int getSampleCount() const;

        /**
         * @brief Returns how long generating the table took, in milliseconds.
         *
         * Offered because the reason the fitted table is absent is a cost, and a claim about cost
         * should come with the cost of what replaced it.
         */
        [[nodiscard]] double getGenerationMilliseconds() const;

        /**
         * @brief Returns the GLSL that reads the table.
         *
         * Declares `uCnaAreaBrdf` and defines `cnaAreaBrdfTerms(float nDotV, float roughness)`,
         * returning the four channels in the order @ref Terms lists them.
         *
         * @return The GLSL source, with no `#version` line.
         */
        [[nodiscard]] static std::string getLookupGlsl();

    private:
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> texture_;
        int    size_ = 0;
        int    sampleCount_ = 0;
        double generationMilliseconds_ = 0.0;
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

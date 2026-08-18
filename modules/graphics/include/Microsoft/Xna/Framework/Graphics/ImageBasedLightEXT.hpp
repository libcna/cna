// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework::Graphics {

    class Texture2D;
    class TextureCube;

    /**
     * @brief An environment as a lit effect consumes it: the three products of the split sum.
     *
     * CNAEXT: not part of the XNA 4.0 API. XNA's lit effects have one flat `AmbientLightColor` and
     * nothing else, so lighting a surface from an environment is an addition.
     *
     * The split-sum approximation needs three things at once, and they must have been generated
     * together: the diffuse irradiance cube, the specular cube whose mips are a roughness ramp,
     * and the BRDF table indexed by (N·V, roughness). `CNA::Graphics::EnvironmentProcessor`
     * produces all three. Pairing a prefiltered cube with a mip count from a different one is the
     * failure this struct exists to prevent -- it does not look like a mismatch, it looks like the
     * material's roughness is wrong.
     *
     * **In the XNA namespace, deliberately** (plan_modern.md `MOD-1222`). The alternative was
     * `CNA::Graphics`, which would have made an always-compiled XNA header include a header that
     * exists only under `CNA_CNAEXT` -- a layering inversion that also makes an effect's public
     * surface depend on a build flag. Two neighbours here, `ShadowCascadeStateEXT` and
     * `PunctualLightEXT`, are in the XNA namespace for exactly that reason, and this follows them.
     *
     * A bundle is inert unless all three textures are present: two thirds of a split sum is not
     * two thirds of the answer, it is a wrong one. @ref IsValidEXT is that test, and an effect
     * given an invalid bundle falls back to its flat ambient term.
     */
    CNAEXT struct ImageBasedLightEXT
    {
        /** @brief Cosine-convolved diffuse irradiance, indexed by the surface normal. */
        TextureCube* Irradiance = nullptr;

        /** @brief GGX-prefiltered specular, one mip per roughness, indexed by the reflection. */
        TextureCube* PrefilteredSpecular = nullptr;

        /** @brief The scale/bias table indexed by (N·V across, roughness down). */
        Texture2D* BrdfLut = nullptr;

        /**
         * @brief How many mips @ref PrefilteredSpecular actually has.
         *
         * Carried rather than queried, because the roughness-to-mip mapping is defined against the
         * count the cube was *generated* with; a renderer that guessed it from the texture would
         * be right only as long as nothing had trimmed the chain.
         */
        int PrefilteredMipCount = 1;

        /**
         * @brief Multiplies the whole environment contribution.
         *
         * The one place an environment's brightness lives, because the products are 8-bit: an
         * environment brighter than 1.0 cannot be stored in its texels and has to be carried
         * here instead. 1 leaves the generated values as they are.
         */
        float Intensity = 1.0f;

        /**
         * @brief Whether this bundle is complete enough to light with.
         *
         * @return True when all three textures are present, the mip count is at least one and the
         *         intensity is not negative.
         */
        CNAEXT [[nodiscard]] bool IsValidEXT() const
        {
            return Irradiance != nullptr && PrefilteredSpecular != nullptr && BrdfLut != nullptr
                && PrefilteredMipCount >= 1 && Intensity >= 0.0f;
        }
    };

} // namespace Microsoft::Xna::Framework::Graphics

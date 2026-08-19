// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <cstddef>
#include <string>

namespace Microsoft::Xna::Framework::Graphics {
    class Texture2D;
}

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief The glTF material extensions beyond what `PbrEffect` implements.
     *
     * **A separate type from `PbrMaterial`, deliberately** (plan_modern.md `MOD-2070`).
     * `PbrMaterial`'s defining property is that it is *lossless*: every field on it corresponds to
     * exactly one piece of `PbrEffect` state, so `applyMaterial` followed by `extractMaterial`
     * returns an equal material. That invariant is what Phase 13 existed to establish, and putting
     * a field here that `PbrEffect` has no state for would quietly break it -- the round trip would
     * drop the field and the two materials would compare unequal for a reason nothing in the type
     * explains.
     *
     * These lobes are therefore carried beside a `PbrMaterial` rather than inside one, and they are
     * consumed by `ClusteredForwardEffect`, which owns its own shader source and can implement
     * them. A game using `PbrEffect` is unaffected in every respect, including its round trip.
     *
     * Every extension here is **off when its factor is zero**, which is its default, so a material
     * that names none of them is the material it was before.
     */
    class PbrMaterialExtensions
    {
    public:
        /** @brief Constructs the neutral extension set: every lobe off. */
        PbrMaterialExtensions();

        // ── KHR_materials_clearcoat ──────────────────────────────────────────

        /** @brief Returns how strongly the clearcoat lobe is applied, 0 to 1. */
        [[nodiscard]] float getClearcoatFactor() const;
        /**
         * @brief Sets how strongly the clearcoat lobe is applied.
         *
         * A clearcoat is a second, thin specular layer over the whole material -- the lacquer on a
         * car, the varnish on a table. It is not a brighter highlight: it is a *second* highlight,
         * with its own roughness and its own normal, over a base that keeps its own.
         *
         * @param value 0 disables the layer entirely; 1 is a full coat. Clamped to [0, 1].
         */
        void setClearcoatFactor(float value);

        /** @brief Returns the clearcoat layer's roughness, 0 to 1. */
        [[nodiscard]] float getClearcoatRoughness() const;
        /**
         * @brief Sets the clearcoat layer's roughness.
         *
         * Independent of the base material's, which is the point: a rough base under a smooth coat
         * is what brushed metal under lacquer looks like, and one roughness cannot describe it.
         *
         * @param value Clamped to [0, 1].
         */
        void setClearcoatRoughness(float value);

        /** @brief Returns the clearcoat's own strength map, or null. */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* getClearcoatTexture() const;
        /**
         * @brief Sets the clearcoat's strength map, multiplied by the factor (R channel).
         *
         * @param texture The map, or null. Borrowed, never owned -- the same rule `PbrMaterial`
         *                follows, and for the same reason.
         */
        void setClearcoatTexture(Microsoft::Xna::Framework::Graphics::Texture2D* texture);

        /** @brief Returns the clearcoat's own roughness map, or null. */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D*
        getClearcoatRoughnessTexture() const;
        /**
         * @brief Sets the clearcoat's roughness map, multiplied by the roughness (G channel).
         *
         * @param texture The map, or null. Borrowed, never owned.
         */
        void setClearcoatRoughnessTexture(
            Microsoft::Xna::Framework::Graphics::Texture2D* texture);

        /** @brief Returns the clearcoat's own normal map, or null. */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D*
        getClearcoatNormalTexture() const;
        /**
         * @brief Sets the clearcoat's normal map, which is separate from the base material's.
         *
         * A coat is smooth over a base that is not: an orange-peel lacquer over a moulded plastic
         * has two different surface normals at the same point, and that is what this describes.
         *
         * @param texture The map, or null. Borrowed, never owned.
         */
        void setClearcoatNormalTexture(Microsoft::Xna::Framework::Graphics::Texture2D* texture);

        /** @brief Returns the scale applied to the clearcoat normal map's tangent-space X and Y. */
        [[nodiscard]] float getClearcoatNormalScale() const;
        /**
         * @brief Sets the scale applied to the clearcoat normal map.
         *
         * @param value Non-negative; negatives are ignored.
         */
        void setClearcoatNormalScale(float value);

        // ── Value semantics ──────────────────────────────────────────────────

        /**
         * @brief Compares every field, textures included (by pointer identity).
         *
         * @param other The extension set to compare with.
         * @return True when the two describe the same lobes.
         */
        [[nodiscard]] bool operator==(const PbrMaterialExtensions& other) const;

        /**
         * @brief The negation of @ref operator==.
         *
         * @param other The extension set to compare with.
         * @return True when the two differ in any field.
         */
        [[nodiscard]] bool operator!=(const PbrMaterialExtensions& other) const;

        /**
         * @brief A hash consistent with @ref operator==.
         *
         * @return A hash of every compared field; equal sets hash equally.
         */
        [[nodiscard]] std::size_t GetHashCode() const;

        /**
         * @brief A one-line summary naming only the lobes that are on.
         *
         * A set with nothing enabled reads `{}`, which is the common case and should not cost a
         * line of zeros to say.
         *
         * @return The summary.
         */
        [[nodiscard]] std::string ToString() const;

        /** @brief Returns whether every lobe is off, so the set changes nothing. */
        [[nodiscard]] bool isNeutral() const;

    private:
        using Tex2D = Microsoft::Xna::Framework::Graphics::Texture2D;

        float clearcoatFactor_      = 0.0f;
        float clearcoatRoughness_   = 0.0f;
        float clearcoatNormalScale_ = 1.0f;

        Tex2D* clearcoatTexture_          = nullptr;
        Tex2D* clearcoatRoughnessTexture_ = nullptr;
        Tex2D* clearcoatNormalTexture_    = nullptr;
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

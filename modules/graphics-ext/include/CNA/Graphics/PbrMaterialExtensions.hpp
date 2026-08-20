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
     * **A separate type from `PbrMaterial`, deliberately** (plans/plan_modern.md `MOD-2070`).
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

        // ── KHR_materials_sheen ──────────────────────────────────────────────

        /** @brief Returns the sheen lobe's colour; black disables the lobe. */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getSheenColorFactor() const;
        /**
         * @brief Sets the sheen lobe's colour.
         *
         * Sheen is what makes velvet look like velvet: a retroreflective lobe that brightens at
         * *grazing* angles, where a normal specular highlight is fading. It is the rim of light
         * along the edge of a cushion, and no roughness setting on the base material produces it --
         * the distribution is a different shape, peaking where the half-vector is perpendicular to
         * the normal rather than aligned with it.
         *
         * @param value Linear, per channel clamped to [0, 1]. Black is how the lobe is turned off.
         */
        void setSheenColorFactor(const Microsoft::Xna::Framework::Vector3& value);

        /** @brief Returns the sheen lobe's roughness, 0 to 1. */
        [[nodiscard]] float getSheenRoughness() const;
        /**
         * @brief Sets the sheen lobe's roughness.
         *
         * Wider than it sounds: the sheen distribution's exponent is `1 / roughness^2`, so small
         * values give a rim so tight it is invisible at any sensible resolution. The glTF default
         * of 0 is treated as its floor rather than as a mirror, for that reason.
         *
         * @param value Clamped to [0, 1].
         */
        void setSheenRoughness(float value);

        /** @brief Returns the sheen colour map, or null. */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* getSheenColorTexture() const;
        /**
         * @brief Sets the sheen colour map, multiplied by the factor (RGB).
         *
         * @param texture The map, or null. Borrowed, never owned.
         */
        void setSheenColorTexture(Microsoft::Xna::Framework::Graphics::Texture2D* texture);

        /** @brief Returns the sheen roughness map, or null. */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D*
        getSheenRoughnessTexture() const;
        /**
         * @brief Sets the sheen roughness map, multiplied by the roughness (A channel).
         *
         * @param texture The map, or null. Borrowed, never owned.
         */
        void setSheenRoughnessTexture(Microsoft::Xna::Framework::Graphics::Texture2D* texture);

        // ── KHR_materials_transmission and KHR_materials_volume ──────────────

        /** @brief Returns how much light passes through the surface, 0 to 1. */
        [[nodiscard]] float getTransmissionFactor() const;
        /**
         * @brief Sets how much light passes through the surface.
         *
         * Not transparency. An alpha-blended surface *lets the background through*; a transmissive
         * one **refracts** it, so what is behind a glass sphere is displaced and inverted rather
         * than merely visible. That is why this needs a copy of the opaque frame and alpha blending
         * does not.
         *
         * @param value 0 is an opaque surface; 1 is clear glass. Clamped to [0, 1].
         */
        void setTransmissionFactor(float value);

        /** @brief Returns the transmission map, or null. */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D*
        getTransmissionTexture() const;
        /**
         * @brief Sets the transmission map, multiplied by the factor (R channel).
         *
         * @param texture The map, or null. Borrowed, never owned.
         */
        void setTransmissionTexture(Microsoft::Xna::Framework::Graphics::Texture2D* texture);

        /** @brief Returns the volume's thickness in world units; 0 makes it a thin surface. */
        [[nodiscard]] float getThicknessFactor() const;
        /**
         * @brief Sets the volume's thickness in world units.
         *
         * Zero is meaningful and is the glTF default: a thin surface refracts at its entry face and
         * has no interior, so nothing is absorbed and the ray is not displaced. A positive
         * thickness is what turns a pane of glass into a solid.
         *
         * @param value Non-negative; negatives are ignored.
         */
        void setThicknessFactor(float value);

        /** @brief Returns the thickness map, or null. */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* getThicknessTexture() const;
        /**
         * @brief Sets the thickness map, multiplied by the factor (G channel).
         *
         * @param texture The map, or null. Borrowed, never owned.
         */
        void setThicknessTexture(Microsoft::Xna::Framework::Graphics::Texture2D* texture);

        /** @brief Returns the distance over which the volume attenuates light to its colour. */
        [[nodiscard]] float getAttenuationDistance() const;
        /**
         * @brief Sets the distance over which the volume attenuates light to its colour.
         *
         * The Beer–Lambert length: after travelling this far through the medium, white light has
         * become @ref getAttenuationColor. A non-positive value means an infinite distance, which
         * is glTF's default and describes a medium that absorbs nothing.
         *
         * @param value The distance; non-positive means infinite.
         */
        void setAttenuationDistance(float value);

        /** @brief Returns the colour white light becomes after one attenuation distance. */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getAttenuationColor() const;
        /**
         * @brief Sets the colour white light becomes after one attenuation distance.
         *
         * @param value Linear, per channel clamped to [0, 1]. White absorbs nothing.
         */
        void setAttenuationColor(const Microsoft::Xna::Framework::Vector3& value);

        // ── KHR_materials_iridescence ────────────────────────────────────────

        /** @brief Returns how strongly the thin-film colour replaces the ordinary Fresnel, 0 to 1. */
        [[nodiscard]] float getIridescenceFactor() const;
        /**
         * @brief Sets how strongly the thin-film colour replaces the ordinary Fresnel.
         *
         * @param value 0 disables the film; 1 is the full interference. Clamped to [0, 1].
         */
        void setIridescenceFactor(float value);

        /** @brief Returns the film's own index of refraction; 1.3 is glTF's default. */
        [[nodiscard]] float getIridescenceIor() const;
        /**
         * @brief Sets the film's own index of refraction.
         *
         * @param value At least 1; smaller values are ignored, as a vacuum is the floor.
         */
        void setIridescenceIor(float value);

        /** @brief Returns the film's thickness in nanometres where its map reads black. */
        [[nodiscard]] float getIridescenceThicknessMinimum() const;
        /**
         * @brief Sets the film's thickness in nanometres where its thickness map reads black.
         *
         * @param value Non-negative nanometres; negatives are ignored.
         */
        void setIridescenceThicknessMinimum(float value);

        /** @brief Returns the film's thickness in nanometres where its map reads white. */
        [[nodiscard]] float getIridescenceThicknessMaximum() const;
        /**
         * @brief Sets the film's thickness in nanometres where its thickness map reads white.
         *
         * With no map bound this is the thickness used everywhere, which is what glTF specifies --
         * so it is the one number that decides a uniformly iridescent surface's colour.
         *
         * @param value Non-negative nanometres; negatives are ignored.
         */
        void setIridescenceThicknessMaximum(float value);

        /** @brief Returns the iridescence strength map, or null. */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D*
        getIridescenceTexture() const;
        /**
         * @brief Sets the iridescence strength map, multiplied by the factor (R channel).
         *
         * @param texture The map, or null. Borrowed, never owned.
         */
        void setIridescenceTexture(Microsoft::Xna::Framework::Graphics::Texture2D* texture);

        /** @brief Returns the film thickness map, or null. */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D*
        getIridescenceThicknessTexture() const;
        /**
         * @brief Sets the film thickness map, interpolating minimum to maximum (G channel).
         *
         * @param texture The map, or null. Borrowed, never owned.
         */
        void setIridescenceThicknessTexture(
            Microsoft::Xna::Framework::Graphics::Texture2D* texture);

        // ── Subsurface scattering (a CNA addition, not a glTF extension) ─────

        /**
         * @brief Returns the colour light takes on after travelling inside the surface.
         *
         * Black -- the default -- disables the approximation entirely.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getSubsurfaceColor() const;
        /**
         * @brief Sets the colour light takes on after travelling inside the surface.
         *
         * **This is a wrapped-diffuse approximation and it says so.** Real subsurface scattering is
         * a screen-space diffusion: light enters at one pixel and leaves at another, which needs a
         * separate diffuse-only buffer and a depth-aware separable blur over it. What this does
         * instead is per-pixel -- it softens the terminator and adds a back-lit glow -- so it
         * captures the *look* of skin, wax or leaves at grazing light without simulating anything
         * travelling sideways. A thin object lit from behind glows; a thick one glows just as much,
         * because nothing here knows how thick it is.
         *
         * @param value Linear, per channel clamped to [0, 1]. Black turns it off.
         */
        void setSubsurfaceColor(const Microsoft::Xna::Framework::Vector3& value);

        /** @brief Returns how far the light wraps past the terminator, 0 to 1. */
        [[nodiscard]] float getSubsurfaceWrap() const;
        /**
         * @brief Sets how far the light wraps past the terminator.
         *
         * 0 is an ordinary Lambert terminator; 1 lets light reach a full quarter-turn around the
         * surface, which is what makes an ear or a leaf look lit from within.
         *
         * @param value Clamped to [0, 1].
         */
        void setSubsurfaceWrap(float value);

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

        /** @brief Returns whether the subsurface approximation is on, i.e. its colour is not black. */
        [[nodiscard]] bool isSubsurfaceEnabled() const;

        /** @brief Returns whether the thin film is on, which is whether its factor is above zero. */
        [[nodiscard]] bool isIridescenceEnabled() const;

        /** @brief Returns whether the surface transmits, which is whether its factor is above zero. */
        [[nodiscard]] bool isTransmissionEnabled() const;

        /** @brief Returns whether the sheen lobe is on, which is whether its colour is not black. */
        [[nodiscard]] bool isSheenEnabled() const;

        /** @brief Returns whether every lobe is off, so the set changes nothing. */
        [[nodiscard]] bool isNeutral() const;

    private:
        using Tex2D = Microsoft::Xna::Framework::Graphics::Texture2D;

        Microsoft::Xna::Framework::Vector3 subsurfaceColor_{0.0f, 0.0f, 0.0f};
        float subsurfaceWrap_ = 0.5f;

        float iridescenceFactor_           = 0.0f;
        float iridescenceIor_              = 1.3f;
        float iridescenceThicknessMinimum_ = 100.0f;
        float iridescenceThicknessMaximum_ = 400.0f;
        Tex2D* iridescenceTexture_          = nullptr;
        Tex2D* iridescenceThicknessTexture_ = nullptr;

        float transmissionFactor_  = 0.0f;
        float thicknessFactor_     = 0.0f;
        float attenuationDistance_ = 0.0f;
        Microsoft::Xna::Framework::Vector3 attenuationColor_{1.0f, 1.0f, 1.0f};
        Tex2D* transmissionTexture_ = nullptr;
        Tex2D* thicknessTexture_    = nullptr;

        Microsoft::Xna::Framework::Vector3 sheenColorFactor_{0.0f, 0.0f, 0.0f};
        float sheenRoughness_ = 0.0f;
        Tex2D* sheenColorTexture_     = nullptr;
        Tex2D* sheenRoughnessTexture_ = nullptr;

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

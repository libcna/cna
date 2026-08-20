// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/PunctualLightEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShadowCascadeStateEXT.hpp"

namespace Microsoft::Xna::Framework::Graphics {

    class Texture2D;

    /**
     * @brief Implemented by the lit effects that can sample a shadow map.
     *
     * CNAEXT: not part of the XNA 4.0 API. XNA had no shadow support of any kind, so this is an
     * addition rather than a reinterpretation of something that existed.
     *
     * It is deliberately always compiled, unlike the `CNA::Graphics` engine layer that generates
     * the maps. Receiving a shadow is a per-draw property of a material -- exactly the shape of an
     * `Effect` member -- and gating it behind a compile option would mean an effect's public
     * surface changed with a build flag. Generating a shadow map is frame-level orchestration and
     * stays in the engine layer; this is the seam between the two.
     *
     * An effect that implements this carries the three values into `GpuDrawParams`. A renderer
     * without a shadow-sampling shader variant accepts them and ignores them, the same convention
     * the PBR fields already use, so enabling shadows on such a renderer produces an unshadowed
     * image rather than an error.
     */
    class IShadowReceiverEXT
    {
    public:
        /** @brief Virtual destructor. */
        virtual ~IShadowReceiverEXT() = default;

        /**
         * @brief Sets the shadow map to sample.
         *
         * The texture holds light-space distance rather than a depth buffer; see
         * `CNA::Graphics::ShadowMap` for why.
         *
         * @param shadowMap The map, or null to detach it.
         */
        CNAEXT virtual void setShadowMapEXT(Texture2D* shadowMap) = 0;

        /** @brief Returns the shadow map currently attached, or null. */
        CNAEXT [[nodiscard]] virtual Texture2D* getShadowMapEXT() const = 0;

        /**
         * @brief Sets the matrix that takes a world-space position into shadow-map space.
         *
         * @param lightViewProjection The generating light's view-projection matrix.
         */
        CNAEXT virtual void setLightViewProjectionEXT(const Matrix& lightViewProjection) = 0;

        /** @brief Returns the light view-projection matrix currently set. */
        CNAEXT [[nodiscard]] virtual Matrix getLightViewProjectionEXT() const = 0;

        /**
         * @brief Enables or disables shadow sampling for draws with this effect.
         *
         * Separate from attaching a map so a game can keep the map attached across a frame and
         * turn reception off for the few draws that should not receive shadows.
         *
         * @param enabled True to sample the shadow map.
         */
        CNAEXT virtual void setShadowsEnabledEXT(bool enabled) = 0;

        /** @brief Returns whether shadow sampling is enabled. */
        CNAEXT [[nodiscard]] virtual bool isShadowsEnabledEXT() const = 0;

        /**
         * @brief Sets the depth bias used when comparing against the map.
         *
         * @param bias The bias; see `CNA::Graphics::ShadowMap::getDepthBias` for what it trades.
         */
        CNAEXT virtual void setShadowDepthBiasEXT(float bias) = 0;

        /** @brief Returns the depth bias used when comparing against the map. */
        CNAEXT [[nodiscard]] virtual float getShadowDepthBiasEXT() const = 0;

        /**
         * @brief Sets the percentage-closer-filtering radius, in shadow-map texels.
         *
         * 0 takes a single sample and gives a hard, stair-stepped edge; 1 averages a 3x3
         * neighbourhood and 2 a 5x5 one. A plain integer rather than a `ShadowQuality`, because
         * that enumeration belongs to the engine layer, which is compiled out by default -- an
         * effect's public surface must not change with a build flag. `CNA::Graphics::ShadowMap`
         * maps its quality onto this.
         *
         * @param radius Radius in texels; renderers clamp values outside 0..2.
         */
        CNAEXT virtual void setShadowFilterRadiusEXT(int radius) = 0;

        /** @brief Returns the percentage-closer-filtering radius, in shadow-map texels. */
        CNAEXT [[nodiscard]] virtual int getShadowFilterRadiusEXT() const = 0;

        /**
         * @brief Supplies a cascaded shadow set, replacing the single-map light matrix.
         *
         * The shadow map itself still arrives through @ref setShadowMapEXT -- a cascade atlas is
         * an ordinary texture. What changes is how a fragment finds its place in it: instead of
         * one light matrix there is one per cascade, chosen by view depth.
         *
         * `Count == 0` restores the single-map behaviour, which is also the default, so an effect
         * that never hears about cascades behaves exactly as it did before they existed.
         *
         * @param state The cascade set, as one value; see `ShadowCascadeStateEXT` for why.
         */
        CNAEXT virtual void setShadowCascadesEXT(const ShadowCascadeStateEXT& state) = 0;

        /** @brief Returns the cascade set currently in use; `Count == 0` means a single map. */
        CNAEXT [[nodiscard]] virtual const ShadowCascadeStateEXT& getShadowCascadesEXT() const = 0;

        /**
         * @brief Supplies one punctual light -- point or spot -- and its shadow.
         *
         * Separate from the directional shadow above rather than folded into it, because the two
         * shadow *different lights*: the directional map answers "is the sun blocked here", this
         * one answers "is that lamp blocked here", and multiplying one light's contribution by the
         * other's visibility is the kind of mistake that produces a plausible image and no clue.
         *
         * `Kind == None` is the default and leaves the draw exactly as it was, so an effect that
         * never hears about punctual lights behaves as it did before they existed. One light per
         * draw; see `PunctualLightEXT` for why that ceiling is deliberate.
         *
         * @param light The light and its shadow, as one value.
         */
        CNAEXT virtual void setPunctualLightEXT(const PunctualLightEXT& light) = 0;

        /** @brief Returns the punctual light in use; `Kind == None` means there is none. */
        CNAEXT [[nodiscard]] virtual const PunctualLightEXT& getPunctualLightEXT() const = 0;
    };

} // namespace Microsoft::Xna::Framework::Graphics

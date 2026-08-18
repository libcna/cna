// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/ShadowQuality.hpp"
#include "CNA/Graphics/SpotLightEXT.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <memory>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class RenderTarget2D;
    class ShaderEffect;
    class Texture2D;
}

namespace CNA::Graphics {

    /**
     * @brief A spot light's shadow: one perspective map covering the cone.
     *
     * plan_modern.md `MOD-1004`. A spot light only illuminates a cone, so unlike a point light it
     * needs one map rather than six, and unlike a directional light that map is perspective rather
     * than orthographic -- fitted to the cone's own field of view, which is what keeps its texels
     * where the light actually reaches.
     *
     * **Distance, not projected depth**, exactly as `CubeShadowMap` stores it. There is no
     * face-selection argument for it here, but there is a better one: a receiver that handles both
     * kinds of light then applies one rule to both maps instead of two rules it can confuse.
     */
    class SpotShadowMap
    {
    public:
        /**
         * @brief Creates a spot shadow map at the resolution implied by @p quality.
         *
         * @param device  The device to render with.
         * @param quality Low = 512, Medium = 1024, High = 2048, Ultra = 4096 -- the full table, not
         *                the cube's capped one, because there is only one map to pay for.
         */
        SpotShadowMap(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                      ShadowQuality quality);

        /** @brief Destroys the map and the caster effect. */
        ~SpotShadowMap();

        SpotShadowMap(const SpotShadowMap&)            = delete;
        SpotShadowMap& operator=(const SpotShadowMap&) = delete;

        /** @brief Returns whether this device can generate spot shadows at all. */
        [[nodiscard]] bool isSupported() const;

        /**
         * @brief Binds the map, clears it, and computes the cone's view-projection.
         *
         * @param light The light to render from; its direction is normalized here.
         * @throws std::logic_error      If a pass is already open.
         * @throws std::invalid_argument If the range is not positive or the outer angle is not
         *                               inside (0, pi/2) -- past that the cone is a hemisphere and
         *                               a perspective projection cannot cover it.
         */
        void begin(const SpotLightEXT& light);

        /**
         * @brief Ends the pass and restores the back buffer.
         *
         * @throws std::logic_error If no pass is open.
         */
        void end();

        /** @brief Returns the rendered map. */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* getShadowTexture() const;

        /** @brief Returns the caster effect, or null where the renderer cannot compile it. */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::ShaderEffect* getCasterEffect() const;

        /** @brief Returns the map's edge length in pixels. */
        [[nodiscard]] int getSize() const;

        /** @brief Returns the configured quality. */
        [[nodiscard]] ShadowQuality getQuality() const;

        /** @brief Returns the cone's view-projection matrix, identity before the first `begin`. */
        [[nodiscard]] Microsoft::Xna::Framework::Matrix getLightViewProjection() const;

        /** @brief Returns the light's world position, as of the last `begin`. */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getLightPosition() const;

        /** @brief Returns the light's range, as of the last `begin`. */
        [[nodiscard]] float getLightRange() const;

        /** @brief Returns the depth bias, as a fraction of the light's range. */
        [[nodiscard]] float getDepthBias() const;

        /** @brief Sets the depth bias. See @ref getDepthBias for its units. */
        void setDepthBias(float value);

        /**
         * @brief Returns the view matrix for a spot light.
         *
         * Exposed so it can be asserted without rendering. A cone pointing straight down breaks
         * the obvious up vector, which is the case this handles and a naive implementation does not.
         *
         * @param light The light.
         * @return The view matrix.
         */
        [[nodiscard]] static Microsoft::Xna::Framework::Matrix computeLightView(
            const SpotLightEXT& light);

        /**
         * @brief Returns the projection covering a cone.
         *
         * The field of view is *twice* the outer half-angle, which is the one factor of two worth
         * stating: a projection built from the half-angle covers half the cone and leaves its rim
         * permanently unshadowed.
         *
         * @param light The light.
         * @return The projection matrix.
         */
        [[nodiscard]] static Microsoft::Xna::Framework::Matrix computeLightProjection(
            const SpotLightEXT& light);

    private:
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
        ShadowQuality quality_;
        int   size_       = 0;
        bool  supported_  = false;
        bool  passOpen_   = false;
        float depthBias_  = 0.004f;
        float lightRange_ = 0.0f;
        Microsoft::Xna::Framework::Vector3 lightPosition_{0.0f, 0.0f, 0.0f};
        Microsoft::Xna::Framework::Matrix lightViewProjection_{};

        std::unique_ptr<Microsoft::Xna::Framework::Graphics::RenderTarget2D> target_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> casterEffect_;
    };

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

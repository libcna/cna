// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/DirectionalLightEXT.hpp"
#include "CNA/Graphics/ShadowQuality.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <memory>

namespace Microsoft::Xna::Framework {
    struct BoundingBox;
}

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class RenderTarget2D;
    class ShaderEffect;
    class Texture2D;
}

namespace CNA::Graphics {

    /**
     * @brief Renders the scene from a directional light's point of view, so that shading can ask
     *        whether a point is lit.
     *
     * Usage mirrors the pipeline's own shape -- the game draws its geometry, the subsystem takes
     * care of everything around it:
     *
     * ```
     * shadowMap.begin(sun, sceneBounds);
     * //   ... draw the shadow-casting geometry with shadowMap.getCasterEffect() ...
     * shadowMap.end();
     * ```
     *
     * **What is stored is distance, not a depth buffer.** CNA has no API for sampling a render
     * target's depth attachment as a texture -- `RenderTarget2D` exposes its colour texture --
     * so the caster effect writes normalized light-space distance into the colour channel
     * instead. On a renderer with float targets that is a single-channel float image with no
     * precision loss worth speaking of; elsewhere it is packed into 8-bit RGB, which is coarser
     * but correct. Either way the receiver samples a colour texture, which every renderer can do.
     */
    class ShadowMap
    {
    public:
        /**
         * @brief Creates a shadow map at the resolution implied by @p quality.
         *
         * @param device  The device to render with.
         * @param quality Low = 512, Medium = 1024, High = 2048, Ultra = 4096; `Disabled` still
         *                constructs, at the Low size, so a game can toggle quality without
         *                recreating the object.
         */
        ShadowMap(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, ShadowQuality quality);

        /** @brief Destroys the shadow map and its target. */
        ~ShadowMap();

        ShadowMap(const ShadowMap&)            = delete;
        ShadowMap& operator=(const ShadowMap&) = delete;

        /**
         * @brief Binds the shadow target and computes the light's view and projection.
         *
         * The projection is fitted to @p sceneBounds: an orthographic volume just large enough to
         * contain the scene as seen from the light. Fitting matters more than it looks -- a volume
         * twice the size it needs to be halves the effective resolution in each axis.
         *
         * @param light       The light to render from; its direction is normalized here.
         * @param sceneBounds World-space bounds of everything that should cast a shadow.
         * @throws std::logic_error If a shadow pass is already open.
         */
        void begin(const DirectionalLightEXT& light,
                   const Microsoft::Xna::Framework::BoundingBox& sceneBounds);

        /**
         * @brief Ends the shadow pass and restores the back buffer.
         *
         * @throws std::logic_error If no shadow pass is open.
         */
        void end();

        /**
         * @brief Returns the effect the caster geometry must be drawn with.
         *
         * It writes light-space distance and nothing else -- no lighting, no textures -- so a
         * shadow pass costs a fraction of a shading pass.
         *
         * @return The caster effect, or null where the renderer cannot compile it.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::ShaderEffect* getCasterEffect() const;

        /**
         * @brief Returns the rendered shadow map.
         *
         * @return The distance texture, or null before the first `begin()`.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* getShadowTexture() const;

        /**
         * @brief Returns the matrix that takes a world-space position into shadow-map space.
         *
         * @return The light's view-projection matrix, identity before the first `begin()`.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Matrix getLightViewProjection() const;

        /** @brief Returns the shadow map's edge length in pixels. */
        [[nodiscard]] int getSize() const;

        /** @brief Returns the configured quality. */
        [[nodiscard]] ShadowQuality getQuality() const;

        /**
         * @brief Returns the depth bias applied when comparing a receiver against the map.
         *
         * Bias trades one artefact for another and there is no value that avoids both: too little
         * and a surface shadows itself in stripes (acne), too much and a shadow detaches from the
         * object casting it (peter-panning). The default is tuned for the Medium size.
         */
        [[nodiscard]] float getDepthBias() const;
        /** @brief Sets the depth bias. See @ref getDepthBias for what it trades. */
        void setDepthBias(float value);

        /**
         * @brief Computes the light's view matrix for a scene, without binding anything.
         *
         * Exposed so the matrix can be asserted directly rather than inferred from an image.
         *
         * @param light       The light.
         * @param sceneBounds The scene bounds to centre on.
         * @return The view matrix.
         */
        [[nodiscard]] static Microsoft::Xna::Framework::Matrix computeLightView(
            const DirectionalLightEXT& light,
            const Microsoft::Xna::Framework::BoundingBox& sceneBounds);

        /**
         * @brief Computes the orthographic projection fitted to a scene, in light space.
         *
         * @param lightView   The light's view matrix.
         * @param sceneBounds The scene bounds to fit.
         * @return The projection matrix.
         */
        [[nodiscard]] static Microsoft::Xna::Framework::Matrix computeLightProjection(
            const Microsoft::Xna::Framework::Matrix& lightView,
            const Microsoft::Xna::Framework::BoundingBox& sceneBounds);

        /**
         * @brief Returns the map size a quality level implies.
         *
         * @param quality The quality level.
         * @return The edge length in pixels.
         */
        [[nodiscard]] static int sizeForQuality(ShadowQuality quality);

    private:
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
        ShadowQuality quality_;
        int  size_ = 0;
        bool passOpen_ = false;
        float depthBias_ = 0.0015f;

        std::unique_ptr<Microsoft::Xna::Framework::Graphics::RenderTarget2D> target_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> casterEffect_;
        Microsoft::Xna::Framework::Matrix lightViewProjection_{};
    };

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

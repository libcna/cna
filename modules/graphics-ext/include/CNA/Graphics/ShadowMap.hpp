// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/DirectionalLightEXT.hpp"
#include "CNA/Graphics/ShadowQuality.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <memory>
#include <vector>

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

/** @addtogroup cnaext_engine
 *  @{
 */

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
         * @brief Returns whether this device can generate shadow maps at all.
         *
         * plan_modern.md MOD-811, and design decision D1: no renderer is mandatory, and a
         * subsystem that cannot run says so instead of failing. Generation needs two things --
         * the renderer must raster 3D triangles, and it must compile the caster's GLSL. Where
         * either is missing the object still constructs and `begin`/`end` still work; they simply
         * leave the map meaning "nothing occludes", so a game that switches shadows on gets an
         * unshadowed image rather than an exception. The reason is logged once per map.
         *
         * @return True if a shadow pass will actually render anything.
         */
        [[nodiscard]] bool isSupported() const;

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
         * @brief Returns the effect skinned caster geometry must be drawn with.
         *
         * plan_modern.md MOD-810. A skinned mesh drawn with the rigid caster above casts its
         * *bind pose*, which is a shadow of a character standing still under one that is running.
         * This variant applies the same bone palette `SkinnedEffect` does, so the silhouette in
         * the map is the animated one.
         *
         * @return The skinned caster effect, or null where the renderer cannot compile it.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::ShaderEffect* getSkinnedCasterEffect() const;

        /**
         * @brief Applies the rigid caster effect and re-uploads the current light matrix.
         *
         * `begin` already does this, so it is only needed to switch back after drawing skinned
         * casters in the same pass.
         *
         * @throws std::logic_error If no shadow pass is open.
         */
        void applyCaster();

        /**
         * @brief Applies the skinned caster effect with a bone palette, inside an open pass.
         *
         * The palette is uploaded here rather than read from an effect, because the shadow pass
         * does not know which of the app's effects a given mesh is shaded with -- only the app
         * does, and it already holds the same matrices it gives `SkinnedEffect`.
         *
         * @param boneTransforms   The skinning palette, in the same order `SkinnedEffect` takes it.
         * @param weightsPerVertex 1, 2 or 4 -- only the first N weight/index pairs contribute,
         *                         matching XNA's own property range.
         * @throws std::logic_error    If no shadow pass is open.
         * @throws std::invalid_argument If @p weightsPerVertex is not 1, 2 or 4, or the palette is
         *                               empty or longer than the shader's 72 bones.
         */
        void applySkinnedCaster(
            const std::vector<Microsoft::Xna::Framework::Matrix>& boneTransforms,
            int weightsPerVertex);

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

        /**
         * @brief Returns the PCF filter radius, in map texels, that a quality level implies.
         *
         * plan_modern.md MOD-840. The radius is what a receiving effect wants
         * (`IShadowReceiverEXT::setShadowFilterRadiusEXT`), so the mapping lives here rather than
         * inside a renderer: two renderers reading the same quality must reach the same kernel.
         *
         * | Quality  | Size | Radius | Kernel |
         * |----------|------|--------|--------|
         * | Disabled | 512  | 0      | 1 tap  |
         * | Low      | 512  | 0      | 1 tap  |
         * | Medium   | 1024 | 1      | 3x3    |
         * | High     | 2048 | 2      | 5x5    |
         * | Ultra    | 4096 | 2      | 5x5    |
         *
         * Ultra buys its quality from resolution rather than from a wider kernel: past 5x5 a
         * box filter blurs the shadow instead of resolving it, which is what a Poisson disc
         * would be for -- not implemented here, and named as absent rather than implied.
         *
         * @param quality The quality level.
         * @return The radius in texels, 0 to 2.
         */
        [[nodiscard]] static int filterRadiusForQuality(ShadowQuality quality);

        /** @brief Returns the PCF filter radius implied by this map's own quality level. */
        [[nodiscard]] int getFilterRadius() const;

    private:
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
        ShadowQuality quality_;
        int  size_ = 0;
        bool passOpen_  = false;
        bool supported_ = false;
        float depthBias_ = 0.0015f;

        std::unique_ptr<Microsoft::Xna::Framework::Graphics::RenderTarget2D> target_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> casterEffect_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> skinnedCasterEffect_;
        Microsoft::Xna::Framework::Matrix lightViewProjection_{};
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

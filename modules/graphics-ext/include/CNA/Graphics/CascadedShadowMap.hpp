// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/DirectionalLightEXT.hpp"
#include "CNA/Graphics/ShadowQuality.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <array>
#include <memory>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class RenderTarget2D;
    class ShaderEffect;
    class Texture2D;
}

namespace CNA::Graphics {

    /**
     * @brief A directional shadow map split into several depth ranges, so that a large scene keeps
     *        usable shadow resolution near the camera.
     *
     * plan_modern.md Phase 9. One map stretched over a whole outdoor view spends most of its texels
     * on ground the player will never look at closely; splitting the view frustum by distance and
     * fitting a map to each slice spends them where they are seen. The cost is that the casting
     * geometry is drawn once per cascade, which is the contract the app has to accept.
     *
     * Usage is per cascade, and app-driven for the same reason `ShadowMap` is:
     *
     * ```
     * cascades.update(sun, cameraView, cameraProjection);
     * for (int i = 0; i < cascades.getCascadeCount(); ++i)
     * {
     *     cascades.begin(i);
     *     //   ... draw the casters that matter at this range ...
     *     cascades.end();
     * }
     * ```
     *
     * **Storage is an atlas, not a texture array** (`MOD-907`). CNA's renderer interface has no
     * array-texture concept, so every renderer would need one before a single cascade could be
     * stored that way. One wide `RenderTarget2D` with a viewport per cascade needs nothing that is
     * not already there, and the receiver samples it with a UV scale-and-offset it is given.
     *
     * **Fitting is sphere-based** (`MOD-903`). Fitting a box to the cascade's frustum corners
     * produces extents that change as the camera turns, and a shadow map whose extents change
     * every frame shimmers along every edge. The bounding *sphere* of those corners has a radius
     * that does not depend on the camera's orientation at all, so the fitted volume is stable and
     * the only thing that moves is its centre -- which `MOD-904` then snaps to whole texels.
     */
    class CascadedShadowMap
    {
    public:
        /** @brief The largest number of cascades this supports; the shader carries one set per. */
        static constexpr int kMaxCascades = 4;

        /**
         * @brief Creates a cascaded shadow map.
         *
         * @param device        The device to render with.
         * @param quality       Decides each cascade's resolution, the same table `ShadowMap` uses.
         * @param cascadeCount  2 to 4.
         * @throws std::invalid_argument If @p cascadeCount is outside 2..4.
         */
        CascadedShadowMap(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                          ShadowQuality quality, int cascadeCount);

        /** @brief Destroys the atlas and the caster effects. */
        ~CascadedShadowMap();

        CascadedShadowMap(const CascadedShadowMap&)            = delete;
        CascadedShadowMap& operator=(const CascadedShadowMap&) = delete;

        /**
         * @brief Returns whether this device can generate cascades at all.
         *
         * Same contract as `ShadowMap::isSupported`: where it is false the object still works and
         * the atlas keeps meaning "nothing occludes", so the frame renders unshadowed.
         */
        [[nodiscard]] bool isSupported() const;

        /**
         * @brief Recomputes every cascade's split, fit and matrix for this frame's camera.
         *
         * Call once per frame before the first `begin`. Separated from `begin` because all the
         * cascades share one camera and one light, and recomputing the splits inside each pass
         * would do the same work N times.
         *
         * @param light            The light to generate from.
         * @param cameraView       The camera's view matrix.
         * @param cameraProjection The camera's projection matrix; its near and far planes are what
         *                         the cascades are split across.
         * @throws std::logic_error If a cascade pass is open.
         */
        void update(const DirectionalLightEXT& light,
                    const Microsoft::Xna::Framework::Matrix& cameraView,
                    const Microsoft::Xna::Framework::Matrix& cameraProjection);

        /**
         * @brief Binds the atlas and the viewport of one cascade.
         *
         * The atlas is cleared once, by the first `begin` of a frame, rather than per cascade: a
         * clear is a full-target operation and clearing it four times would erase the three
         * cascades already drawn.
         *
         * @param cascadeIndex 0 to `getCascadeCount() - 1`.
         * @throws std::logic_error      If a cascade pass is already open, or `update` has never run.
         * @throws std::out_of_range     If @p cascadeIndex names no cascade.
         */
        void begin(int cascadeIndex);

        /**
         * @brief Ends the current cascade's pass and restores the back buffer and viewport.
         *
         * @throws std::logic_error If no cascade pass is open.
         */
        void end();

        /** @brief Returns the number of cascades. */
        [[nodiscard]] int getCascadeCount() const;

        /** @brief Returns each cascade's resolution in the atlas, in pixels. */
        [[nodiscard]] int getCascadeSize() const;

        /** @brief Returns the atlas holding every cascade side by side. */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* getShadowTexture() const;

        /** @brief Returns the caster effect, or null where the renderer cannot compile it. */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::ShaderEffect* getCasterEffect() const;

        /**
         * @brief Returns one cascade's world-to-shadow-atlas matrix.
         *
         * Already includes the atlas sub-rectangle, so a receiver transforms a world position by
         * this and samples the result directly -- there is no separate UV offset to apply and
         * therefore no way to apply it to the wrong cascade.
         *
         * @param cascadeIndex 0 to `getCascadeCount() - 1`.
         * @throws std::out_of_range If @p cascadeIndex names no cascade.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Matrix getCascadeMatrix(int cascadeIndex) const;

        /**
         * @brief Returns the view-space distance at which a cascade stops being used.
         *
         * @param cascadeIndex 0 to `getCascadeCount() - 1`; the last one returns the camera's far
         *                     plane.
         * @throws std::out_of_range If @p cascadeIndex names no cascade.
         */
        [[nodiscard]] float getSplitDistance(int cascadeIndex) const;

        /**
         * @brief Returns the cascade that covers a view-space depth.
         *
         * The same rule the receiver shader applies, kept here so it can be tested against
         * hand-computed depths rather than only inferred from an image. A depth beyond the last
         * split still returns the last cascade: the alternative is an unshadowed band at the
         * horizon, which looks like a missing shadow rather than an out-of-range depth.
         *
         * @param viewDepth Distance in front of the camera, positive.
         * @return The cascade index, 0 to `getCascadeCount() - 1`.
         */
        [[nodiscard]] int selectCascade(float viewDepth) const;

        /**
         * @brief Returns the practical-split-scheme blend factor.
         *
         * 0 splits the range uniformly, which wastes the near cascades on distance the camera
         * barely resolves; 1 splits it logarithmically, which starves the far ones. The default,
         * 0.75, is the usual compromise and is what the split table below was measured with.
         */
        [[nodiscard]] float getSplitLambda() const;

        /** @brief Sets the split blend factor; clamped to 0..1. Takes effect at the next `update`. */
        void setSplitLambda(float lambda);

        /**
         * @brief Computes the practical split distances for a view range.
         *
         * The scheme is Zhang's: a blend of the logarithmic split, which is correct for perspective
         * projection, and the uniform one, which keeps the near cascade from collapsing onto the
         * near plane.
         *
         *     d(i) = lambda * near*(far/near)^(i/N) + (1 - lambda) * (near + (far-near)*i/N)
         *
         * @param nearPlane    The camera's near distance; must be positive.
         * @param farPlane     The camera's far distance; must exceed @p nearPlane.
         * @param cascadeCount 2 to 4.
         * @param lambda       0 for uniform, 1 for logarithmic; clamped.
         * @return The far distance of each cascade, ascending, the last being @p farPlane.
         * @throws std::invalid_argument If the range or the count is not usable.
         */
        [[nodiscard]] static std::vector<float> computeSplitDistances(
            float nearPlane, float farPlane, int cascadeCount, float lambda);

        /**
         * @brief Returns the eight world-space corners of a view frustum.
         *
         * Order: near plane first (bottom-left, bottom-right, top-left, top-right), then far.
         *
         * @param view       The view matrix.
         * @param projection The projection matrix.
         * @return The corners, in world space.
         */
        [[nodiscard]] static std::array<Microsoft::Xna::Framework::Vector3, 8> computeFrustumCorners(
            const Microsoft::Xna::Framework::Matrix& view,
            const Microsoft::Xna::Framework::Matrix& projection);

        /**
         * @brief Returns the bounding sphere of eight frustum corners, as a centre and radius.
         *
         * The radius is what makes the fit stable: it is a property of the frustum's shape, not of
         * where it is pointing, so turning the camera cannot change it.
         *
         * @param corners The eight corners.
         * @param centre  Receives the sphere's centre, in the corners' own space.
         * @return The radius.
         */
        static float computeBoundingSphere(
            const std::array<Microsoft::Xna::Framework::Vector3, 8>& corners,
            Microsoft::Xna::Framework::Vector3& centre);

        /**
         * @brief Snaps a light-space centre to whole shadow-map texels.
         *
         * Without this, translating the camera by a fraction of a texel moves the whole map by that
         * fraction, and every shadow edge crawls. Snapping makes sub-texel camera motion produce a
         * bit-identical map.
         *
         * @param centre           The centre to snap, in light space.
         * @param radius           The cascade's fitted radius.
         * @param cascadeSize      The cascade's resolution in texels.
         * @return The snapped centre.
         */
        [[nodiscard]] static Microsoft::Xna::Framework::Vector3 snapToTexelGrid(
            const Microsoft::Xna::Framework::Vector3& centre, float radius, int cascadeSize);

    private:
        struct Cascade
        {
            Microsoft::Xna::Framework::Matrix matrix{};
            float splitDistance = 0.0f;
            float radius = 0.0f;
        };

        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
        ShadowQuality quality_;
        int  cascadeCount_ = 0;
        int  cascadeSize_  = 0;
        bool supported_    = false;
        bool updated_      = false;
        bool passOpen_     = false;
        bool atlasCleared_ = false;
        int  openCascade_  = -1;
        float splitLambda_ = 0.75f;

        std::array<Cascade, kMaxCascades> cascades_{};
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::RenderTarget2D> atlas_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> casterEffect_;
    };

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

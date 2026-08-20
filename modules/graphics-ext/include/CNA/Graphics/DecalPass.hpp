// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <memory>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class ShaderEffect;
    class SpriteBatch;
    class Texture2D;
}

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Projects a texture onto whatever the depth prepass says is already there.
     *
     * plans/plan_modern.md `MOD-2094`. Bullet holes, scorch marks, puddles, tyre tracks: a decal is an
     * image glued onto geometry it knows nothing about. This is the screen-space form of that --
     * for every pixel, the prepass depth gives a position, the decal's inverse transform puts that
     * position in the decal's own space, and the pixel is painted only if it lands **inside the
     * decal's unit box**. Nothing about the receiving mesh has to change, and no geometry is
     * generated.
     *
     * The box is the whole contract, and it is what stops the classic failure: a decal that landed
     * on the wall behind the crate as well as on the crate. A surface further away than the box's
     * far face is outside it, so it is not painted.
     *
     * The decal occupies the local cube from -0.5 to +0.5 on every axis; the texture maps to its
     * local X and Y, and it projects along local **+Z**. So `Matrix::CreateScale(2, 2, 0.5) *
     * Matrix::CreateTranslation(where)` is a two-unit decal that reaches a quarter of a unit in
     * front of and behind the surface it is aimed at.
     *
     * **Give it the prepass normals if you have them.** Without them a decal projected at a glancing
     * angle smears across a surface nearly parallel to its own axis, which is the other classic
     * decal artefact; with them, @ref setMaxSlopeAngle rejects those pixels outright.
     */
    class DecalPass
    {
    public:
        /**
         * @brief Creates the pass and compiles its shader.
         *
         * @param device The device to render with.
         */
        explicit DecalPass(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /** @brief Destroys the pass and its shader. */
        ~DecalPass();

        DecalPass(const DecalPass&)            = delete;
        DecalPass& operator=(const DecalPass&) = delete;

        /** @brief Returns whether this renderer can run the pass. */
        [[nodiscard]] bool isSupported() const;

        /**
         * @brief Supplies the prepass output the decals are projected onto.
         *
         * @param depth   The prepass depth texture. Required; without it nothing is drawn.
         * @param normals The prepass normal texture, or null to skip the slope test.
         */
        void setPrepassInputs(Microsoft::Xna::Framework::Graphics::Texture2D* depth,
                              Microsoft::Xna::Framework::Graphics::Texture2D* normals);

        /**
         * @brief Supplies the camera the prepass was drawn with.
         *
         * @param view       The view matrix.
         * @param projection The projection matrix.
         * @param farPlane   The far plane the prepass normalised its depth by; must be positive.
         */
        void setCamera(const Microsoft::Xna::Framework::Matrix& view,
                       const Microsoft::Xna::Framework::Matrix& projection, float farPlane);

        /**
         * @brief Draws one decal over the currently bound target.
         *
         * @param decal      The decal image; its alpha is the mask.
         * @param decalWorld The decal's world transform -- a unit box scaled and placed.
         * @param width      The bound target's width in pixels.
         * @param height     The bound target's height in pixels.
         * @throws std::invalid_argument If @p decal is null or a dimension is not positive.
         */
        void draw(Microsoft::Xna::Framework::Graphics::Texture2D* decal,
                  const Microsoft::Xna::Framework::Matrix& decalWorld, int width, int height);

        /** @brief Returns how strongly the decal is applied. */
        [[nodiscard]] float getOpacity() const;
        /**
         * @brief Sets how strongly the decal is applied.
         *
         * @param value 0 makes it invisible, 1 uses the image's own alpha. Clamped to [0, 1].
         */
        void setOpacity(float value);

        /** @brief Returns the colour every decal texel is multiplied by. */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getTint() const;
        /**
         * @brief Sets the colour every decal texel is multiplied by.
         *
         * @param value The tint; white leaves the image alone.
         */
        void setTint(const Microsoft::Xna::Framework::Vector3& value);

        /** @brief Returns the widest surface slope the decal is still painted onto, in radians. */
        [[nodiscard]] float getMaxSlopeAngle() const;
        /**
         * @brief Sets the widest angle between the surface and the decal's own axis to accept.
         *
         * Only consulted when prepass normals were supplied. The default accepts a little past a
         * right angle's worth of tilt, which keeps a decal on a curved surface while dropping the
         * near-parallel pixels that would otherwise smear.
         *
         * @param radians The angle; clamped to [0, pi/2].
         */
        void setMaxSlopeAngle(float radians);

        /**
         * @brief Returns whether a point in the decal's own space is inside its box.
         *
         * The same test the shader performs, offered separately so the rule can be checked without
         * a GPU -- and so a game can ask the question before spending a fullscreen pass.
         *
         * @param decalLocalPosition A position in the decal's local space.
         * @return True when every component is within -0.5 and 0.5.
         */
        [[nodiscard]] static bool isInsideDecalBox(
            const Microsoft::Xna::Framework::Vector3& decalLocalPosition);

    private:
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteBatch> spriteBatch_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> effect_;

        Microsoft::Xna::Framework::Graphics::Texture2D* depth_   = nullptr;
        Microsoft::Xna::Framework::Graphics::Texture2D* normals_ = nullptr;
        Microsoft::Xna::Framework::Matrix view_{};
        Microsoft::Xna::Framework::Matrix inverseProjection_{};
        Microsoft::Xna::Framework::Vector3 tint_{1.0f, 1.0f, 1.0f};
        float farPlane_      = 0.0f;
        float opacity_       = 1.0f;
        float maxSlopeAngle_ = 1.2217305f;   // 70 degrees
        bool  supported_     = false;
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

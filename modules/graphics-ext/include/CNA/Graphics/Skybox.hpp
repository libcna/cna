// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <memory>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class RenderTarget2D;
    class ShaderEffect;
    class Texture2D;
    class TextureCube;
}

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    class FullscreenPass;

    /**
     * @brief Draws an environment cube map as the sky behind a scene.
     *
     * plan_modern.md Phase 11. Small on its own, and the reason image-based lighting is worth
     * looking at: an IBL-lit object reflects an environment the viewer cannot see is a hard sell.
     *
     * **One fullscreen triangle, no cube mesh** (`MOD-1102`). A sky drawn as a box needs a mesh,
     * needs its faces oriented, and puts a seam wherever two of them meet. Reconstructing the view
     * ray per pixel from the inverse view-projection needs none of that: the direction it produces
     * *is* the cube-map lookup, so there is nothing to seam.
     *
     * **The view's translation is stripped** (`MOD-1101`). The sky is infinitely far away, so
     * moving the camera must not move it -- only turning the camera may. Keeping the translation
     * makes the sky slide past like a painted backdrop a few metres out.
     *
     * **Drawn before the scene, not after** (`MOD-1103`, with a recorded deviation). What that row
     * asks for is that the sky never occludes geometry, and it proposes the usual way of getting
     * it: draw last, at the far plane, with `LessEqual` and depth writes off. This draws the sky
     * first instead, because the engine layer's fullscreen mechanism is `SpriteBatch`-based and
     * carries no depth configuration. The guarantee is the same and is asserted; what is given up
     * is the optimisation of skipping sky pixels the scene will cover.
     */
    class Skybox
    {
    public:
        /**
         * @brief Creates a skybox for one device.
         *
         * @param device      The device to render with.
         * @param environment The cube map to draw, or null to attach one later. **Not owned** --
         *                    the common case is an environment the content pipeline loaded and
         *                    something else already holds. See @ref setOwnedEnvironment for the
         *                    other case.
         */
        Skybox(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
               Microsoft::Xna::Framework::Graphics::TextureCube* environment);

        /** @brief Destroys the skybox, and the environment only if this object owns it. */
        ~Skybox();

        Skybox(const Skybox&)            = delete;
        Skybox& operator=(const Skybox&) = delete;

        /**
         * @brief Returns whether this device can draw a sky at all.
         *
         * Needs a renderer that compiles custom effects and samples cube maps. Where it cannot,
         * @ref draw does nothing and says so once, rather than throwing -- a game switching the sky
         * on should get a scene without one, not a crash.
         */
        [[nodiscard]] bool isSupported() const;

        /**
         * @brief Draws the sky across the currently bound target.
         *
         * Call before the scene's opaque geometry; see the class comment for why.
         *
         * @param view       The camera's view matrix; its translation is ignored.
         * @param projection The camera's projection matrix.
         * @param width      Target width in pixels.
         * @param height     Target height in pixels.
         * @throws std::invalid_argument If either dimension is not positive.
         */
        void draw(const Microsoft::Xna::Framework::Matrix& view,
                  const Microsoft::Xna::Framework::Matrix& projection,
                  int width, int height);

        /** @brief Returns the environment cube map, or null. */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::TextureCube* getEnvironment() const;

        /**
         * @brief Attaches an environment this object does not own.
         *
         * Replaces an owned one, releasing it -- otherwise attaching a borrowed cube over an owned
         * one would leak the owned one silently.
         *
         * @param environment The cube map, or null to draw nothing.
         */
        void setEnvironment(Microsoft::Xna::Framework::Graphics::TextureCube* environment);

        /**
         * @brief Attaches an environment this object takes ownership of.
         *
         * The `PbrEffect` precedent: a generated cube -- from an equirectangular panorama, or from
         * `EnvironmentProcessor` -- has no other owner, and making the caller hold it alive
         * separately is a lifetime bug waiting to be written.
         *
         * @param environment The cube map to take.
         */
        void setOwnedEnvironment(
            std::unique_ptr<Microsoft::Xna::Framework::Graphics::TextureCube> environment);

        /** @brief Returns the yaw applied to the sampled direction, in radians. */
        [[nodiscard]] float getYaw() const;

        /**
         * @brief Rotates the sky about the world's Y axis.
         *
         * The one orientation control worth having: a panorama's horizon is level and its "front"
         * is arbitrary, so lining the sun up with a scene's key light is a yaw and nothing else.
         *
         * @param radians The rotation.
         */
        void setYaw(float radians);

        /** @brief Returns the multiplier applied to the sampled colour. */
        [[nodiscard]] float getIntensity() const;

        /**
         * @brief Scales the sky's brightness.
         *
         * Meaningful above 1 when the scene target is a float one: an HDR panorama's sun is
         * several times white, and that is what makes bloom and tonemapping do anything.
         *
         * @param intensity The multiplier; negative values are clamped to zero.
         */
        void setIntensity(float intensity);

        /** @brief Returns the colour the sky is tinted by. */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getTint() const;

        /** @brief Tints the sky. White leaves it unchanged. @param tint The tint. */
        void setTint(const Microsoft::Xna::Framework::Vector3& tint);

        /**
         * @brief Returns the world-space view ray for a point on the screen.
         *
         * The CPU twin of what the shader computes, exposed so the reconstruction can be checked
         * against arithmetic rather than against an image -- a ray that is subtly wrong still
         * produces a sky, just one that turns at the wrong rate.
         *
         * @param view       The camera's view matrix; its translation is ignored.
         * @param projection The camera's projection matrix.
         * @param ndcX       Horizontal position in normalized device coordinates, -1 to 1.
         * @param ndcY       Vertical position, -1 to 1.
         * @param yaw        Rotation about Y, in radians.
         * @return The normalized direction the sky is sampled along.
         */
        [[nodiscard]] static Microsoft::Xna::Framework::Vector3 computeViewRay(
            const Microsoft::Xna::Framework::Matrix& view,
            const Microsoft::Xna::Framework::Matrix& projection,
            float ndcX, float ndcY, float yaw);

    private:
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
        Microsoft::Xna::Framework::Graphics::TextureCube* environment_ = nullptr;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::TextureCube> ownedEnvironment_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> dummySource_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> effect_;
        std::unique_ptr<FullscreenPass> fullscreen_;

        float yaw_       = 0.0f;
        float intensity_ = 1.0f;
        Microsoft::Xna::Framework::Vector3 tint_{1.0f, 1.0f, 1.0f};
        bool  supported_ = false;
        mutable bool warned_ = false;
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

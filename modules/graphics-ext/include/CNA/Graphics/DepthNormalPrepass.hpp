// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <memory>
#include <string>

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
     * @brief Renders the scene's linear view depth and view-space normals for screen-space effects.
     *
     * plan_modern.md `MOD-501`. SSAO — and any later effect that needs to know the shape of the
     * scene rather than its colour — reads two images: how far each pixel is, and which way it
     * faces. This produces them, and the app drives it the way it drives `ShadowMap`: bracket a
     * draw of the scene, using the effect this hands back.
     *
     * ```cpp
     * for (int pass = 0; pass < prepass.getPassCount(); ++pass)
     * {
     *     prepass.begin(pass, view, projection, nearPlane, farPlane);
     *     DrawSceneGeometry(prepass.getPrepassEffect());   // or getSkinnedPrepassEffect()
     *     prepass.end();
     * }
     * pipeline.setDepthNormalInputs(prepass.getDepthTexture(), prepass.getNormalTexture());
     * ```
     *
     * **Why a prepass and not the depth attachment** (`MOD-500`). Sampling the depth buffer the
     * scene already wrote would be free, and CNA cannot do it portably: several renderers never
     * expose their depth attachment as a texture, the ones that do disagree about its precision and
     * about whether it is readable while still bound, and the value in it is non-linear in a way
     * that differs per API. Rendering depth explicitly costs a second pass over the geometry and is
     * the same everywhere, which is the trade this layer makes throughout.
     *
     * **The loop is not decoration.** With `MultipleRenderTargets` the prepass writes both images in
     * one pass and `getPassCount()` is 1. Without it, depth and normals are written in two passes
     * over the same geometry and the count is 2. Writing the loop means an app is correct on both,
     * and on the renderers that have MRT it costs one iteration.
     */
    class DepthNormalPrepass
    {
    public:
        /**
         * @brief Creates the prepass and its targets.
         *
         * @param device The device to render on.
         * @param width  Target width in pixels; must be positive.
         * @param height Target height in pixels; must be positive.
         * @throws std::invalid_argument If a dimension is not positive.
         */
        DepthNormalPrepass(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, int width,
                           int height);

        /** @brief Destroys the prepass, its effects and its targets. */
        ~DepthNormalPrepass();

        DepthNormalPrepass(const DepthNormalPrepass&)            = delete;
        DepthNormalPrepass& operator=(const DepthNormalPrepass&) = delete;

        /**
         * @brief Resizes the targets, reallocating only when the size actually changed.
         *
         * @param width  New width in pixels; must be positive.
         * @param height New height in pixels; must be positive.
         * @throws std::invalid_argument If a dimension is not positive.
         * @throws std::logic_error If a pass is open.
         */
        void resize(int width, int height);

        /**
         * @brief How many times the scene must be drawn to fill both images.
         *
         * @return 1 where the renderer has multiple render targets, otherwise 2.
         */
        [[nodiscard]] int getPassCount() const;

        /**
         * @brief Opens pass @p passIndex and binds its target.
         *
         * @param passIndex  0-based, less than @ref getPassCount.
         * @param view       The camera's view matrix — normals are produced in **view** space.
         * @param projection The camera's projection matrix.
         * @param nearPlane  Camera near distance; must be positive.
         * @param farPlane   Camera far distance; must exceed @p nearPlane.
         * @throws std::logic_error If a pass is already open.
         * @throws std::out_of_range If @p passIndex is not a valid pass.
         * @throws std::invalid_argument If the plane distances are not a usable range.
         */
        void begin(int passIndex, const Microsoft::Xna::Framework::Matrix& view,
                   const Microsoft::Xna::Framework::Matrix& projection, float nearPlane,
                   float farPlane);

        /**
         * @brief Closes the open pass and restores the previous render target.
         *
         * @throws std::logic_error If no pass is open.
         */
        void end();

        /**
         * @brief The effect the scene must be drawn with inside a pass.
         *
         * @return The effect, owned by this prepass, or null where the renderer cannot run it.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::ShaderEffect* getPrepassEffect() const;

        /**
         * @brief The skinned variant, for meshes with bone transforms (`MOD-503`).
         *
         * A skinned mesh drawn with the rigid effect appears in the depth buffer in its **bind
         * pose**, so it occludes the wrong part of the screen — a failure that looks like the AO
         * being wrong rather than the prepass being wrong.
         *
         * @return The effect, owned by this prepass, or null where the renderer cannot run it.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::ShaderEffect*
        getSkinnedPrepassEffect() const;

        /** @brief Linear view depth, normalised to 0..1 by the far plane. @return The texture. */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* getDepthTexture() const;

        /** @brief View-space normals, encoded as `n * 0.5 + 0.5`. @return The texture. */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* getNormalTexture() const;

        /**
         * @brief Whether this renderer can run the prepass at all.
         *
         * @param device The device whose renderer is queried.
         * @return True when the effects compiled and will actually shade the draw.
         */
        [[nodiscard]] bool isSupported(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const;

        /** @brief Whether both images are written in one pass. @return True with MRT. */
        [[nodiscard]] bool isUsingMultipleRenderTargets() const;

        /**
         * @brief Whether depth is packed into an 8-bit target rather than stored as a float.
         *
         * plan_modern.md `MOD-507`. Where float render targets are missing, linear depth is packed
         * across the four channels of a `Color` target. It round-trips to about 1 part in 2^24
         * rather than a half-float's 11 bits of mantissa — *more* precise, in fact, but banded
         * differently, and the packing costs arithmetic on both ends.
         *
         * @return True when the packed path is in use.
         */
        [[nodiscard]] bool isDepthPacked() const;

        /**
         * @brief The GLSL a consumer includes to read this prepass's depth.
         *
         * plan_modern.md `MOD-504`. One shared function rather than a copy per effect: the depth
         * encoding and its inverse must agree, and two copies that happen to agree are one edit
         * away from an SSAO that darkens the wrong pixels. Declares
         * `float cnaDecodeLinearDepth(vec4)` and `vec3 cnaViewPositionFromDepth(vec2, float, mat4)`.
         *
         * @param packed Whether to emit the packed-depth decoder or the float one.
         * @return GLSL source, to be concatenated ahead of a consumer's own `main`.
         */
        [[nodiscard]] static std::string getDepthDecodeGlsl(bool packed);

        /**
         * @brief The CPU twin of the packed-depth encoding, for verification.
         *
         * @param value The 0..1 depth to pack.
         * @param r     Receives the red channel, 0..1.
         * @param g     Receives the green channel.
         * @param b     Receives the blue channel.
         * @param a     Receives the alpha channel.
         */
        static void packDepth(float value, float& r, float& g, float& b, float& a);

        /**
         * @brief The CPU twin of the packed-depth decoding.
         *
         * @param r Red channel, 0..1.
         * @param g Green channel.
         * @param b Blue channel.
         * @param a Alpha channel.
         * @return The recovered depth.
         */
        [[nodiscard]] static float unpackDepth(float r, float g, float b, float a);

    private:
        void allocateTargets();

        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
        int  width_  = 0;
        int  height_ = 0;
        bool passOpen_ = false;
        int  openPass_ = -1;
        bool useMrt_    = false;
        bool packDepth_ = false;
        bool supported_ = false;

        std::unique_ptr<Microsoft::Xna::Framework::Graphics::RenderTarget2D> depthTarget_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::RenderTarget2D> normalTarget_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect>   effect_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect>   skinnedEffect_;
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

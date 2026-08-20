// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/DepthEncoding.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

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

        /**
         * @brief Creates the prepass with a chosen depth encoding.
         *
         * plan_modern.md `MOD-2035`. The encoding is normally not a caller's business — the layer
         * packs everywhere, because a half-float depth target defeated every screen-space effect on
         * the reference renderer and the reason for that is still open. This overload exists so the
         * failing shape can still be *built*: a policy whose alternative cannot be constructed can
         * never be re-examined, and the investigation that closes `MOD-2035` needs to reproduce the
         * failure before it can bisect it.
         *
         * @param device   The device to render on.
         * @param width    Target width in pixels; must be positive.
         * @param height   Target height in pixels; must be positive.
         * @param encoding How to store depth. @ref DepthEncoding::HalfFloat is known to break this
         *                 layer's screen-space passes and is not a supported configuration.
         * @throws std::invalid_argument If a dimension is not positive.
         */
        DepthNormalPrepass(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, int width,
                           int height, DepthEncoding encoding);

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
         * @brief Whether a prepass on this device packs its depth, asked without constructing one.
         *
         * plan_modern.md `MOD-2035`. Single-sourced deliberately: six passes decode this prepass's
         * depth and every one of them has to reach the *same* answer it did. Deriving it separately
         * in each was the shape of a bug waiting to happen -- and this function's answer has since
         * changed, which would have broken every copy that did not change with it.
         *
         * @param device The device to ask about.
         * @return True when the packed 8-bit path is used.
         */
        [[nodiscard]] static bool usesPackedDepthEXT(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /** @brief Returns the roughness written into the normal target's alpha. */
        [[nodiscard]] float getRoughness() const;

        /**
         * @brief Sets the roughness written into the normal target's alpha.
         *
         * Screen-space reflections need to know how sharp a surface's reflection should be, and
         * roughness lives in the material rather than in the geometry, so the prepass has to be
         * told. It rides in the normal target's alpha, which carried nothing before: a third render
         * target would make this pass's existing two-pass fallback a three-pass one for the sake of
         * one scalar.
         *
         * Set it between draws inside an open pass, the way a scene with more than one material has
         * to describe itself. **The default is 0 -- a mirror** rather than glTF's fully-rough
         * default, so an app that never sets it gets the sharp reflections it got before this
         * existed rather than a silently blurred frame.
         *
         * @param value The roughness, clamped to [0, 1].
         */
        void setRoughness(float value);

        /**
         * @brief Returns whether a third image, per-pixel screen velocity, is being produced.
         *
         * @return True when the velocity target exists and is written.
         */
        [[nodiscard]] bool isVelocityEnabledEXT() const;

        /**
         * @brief Turns the per-object velocity target on or off.
         *
         * plan_modern.md `MOD-2033`. Off by default, and the default is the point: this is **an
         * obligation on the application**, not a switch that makes motion blur better on its own.
         * With it on, every draw inside a pass must be preceded by @ref setPreviousWorldEXT with
         * that object's world matrix *from the previous frame*, and @ref setPreviousCameraEXT must
         * carry the previous frame's camera. An app that turns this on and supplies nothing gets a
         * velocity image that says everything is stationary — which is exactly what it gets today
         * with the feature off, so nothing breaks; it simply gains nothing either.
         *
         * The cost is honest and worth stating: with MRT the prepass writes three targets in one
         * pass instead of two. **Without MRT its existing two-pass fallback becomes three passes**
         * over the geometry, which is why this is not on by default.
         *
         * @param value True to produce velocity.
         * @throws std::logic_error If a pass is open.
         */
        void setVelocityEnabledEXT(bool value);

        /**
         * @brief Per-pixel screen-space velocity, in UV units, encoded as `v * 0.5 + 0.5`.
         *
         * **Alpha is inverted, and deliberately.** A texel whose alpha is *below* 0.5 carries a
         * velocity; one at 1.0 does not. The MRT path issues a single clear for the whole bound set
         * and the depth target must clear to white, so "no velocity here" is the colour a shared
         * white clear already produces. Inverting the flag costs one comparison; a second clear
         * would cost a bind of a discard-contents target, which is not safe.
         *
         * @return The texture, or null when velocity is off.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* getVelocityTextureEXT() const;

        /**
         * @brief Supplies the world matrix the next draw's object had in the previous frame.
         *
         * Set it between draws inside an open pass, exactly as @ref setRoughness is set: the
         * prepass draws whatever the app hands it and cannot know which object is which. An object
         * that has not moved passes the same matrix it passes this frame, and a newly spawned one
         * should pass its current matrix rather than the identity — the identity would give it a
         * one-frame smear from the world origin.
         *
         * @param value The previous frame's world matrix.
         */
        void setPreviousWorldEXT(const Microsoft::Xna::Framework::Matrix& value);

        /**
         * @brief Supplies the previous frame's camera, for the half of the velocity the camera owns.
         *
         * Call it before @ref begin. Passing this frame's camera is correct and means "the camera
         * did not move"; the first frame after a start or a resize has no history and should pass
         * the current camera rather than the identity.
         *
         * @param previousView       Last frame's view matrix.
         * @param previousProjection Last frame's projection matrix.
         */
        void setPreviousCameraEXT(const Microsoft::Xna::Framework::Matrix& previousView,
                                  const Microsoft::Xna::Framework::Matrix& previousProjection);

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
         * @brief The GLSL that both writes and reads this prepass's velocity image.
         *
         * plan_modern.md `MOD-2033`/`MOD-2035`. One string rather than a copy at each end, for the
         * reason @ref getDepthDecodeGlsl gives and which `MOD-2035` then demonstrated the hard way:
         * a consumer that decodes an encoded image by hand is one edit away from decoding an
         * encoding nothing writes any more, and the frame it produces looks plausible. The velocity
         * encoding is exactly the kind that invites the mistake -- a scaled UV delta with an
         * **inverted** alpha flag.
         *
         * Declares `cnaEncodeVelocity(vec2)`, `cnaNoVelocity()`, `cnaHasVelocity(vec4)` and
         * `cnaDecodeVelocity(vec4)`. The prepass uses the first two and a consumer the last two.
         *
         * @return GLSL source, to be concatenated ahead of a shader's own `main`.
         */
        [[nodiscard]] static std::string getVelocityDecodeGlsl();

        /**
         * @brief Whether a velocity texel carries a velocity at all.
         *
         * The CPU twin of `cnaHasVelocity`, for tests and tools.
         *
         * @param texel A texel of the velocity image.
         * @return True when something wrote it.
         */
        [[nodiscard]] static bool hasVelocityEXT(const Microsoft::Xna::Framework::Color& texel);

        /**
         * @brief The screen-space velocity a texel carries, in UV units.
         *
         * The CPU twin of `cnaDecodeVelocity`. A texel nothing wrote decodes to zero.
         *
         * @param texel A texel of the velocity image.
         * @return The velocity, where 1 is a whole screen in one frame.
         */
        [[nodiscard]] static Microsoft::Xna::Framework::Vector2 decodeVelocityEXT(
            const Microsoft::Xna::Framework::Color& texel);

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
        void probeMultipleRenderTargets();

        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
        int  width_  = 0;
        int  height_ = 0;
        bool passOpen_ = false;
        int  openPass_ = -1;
        bool useMrt_    = false;
        bool packDepth_ = false;
        float roughness_ = 0.0f;
        bool supported_ = false;
        bool velocity_  = false;
        Microsoft::Xna::Framework::Matrix previousWorld_{};
        Microsoft::Xna::Framework::Matrix previousViewProjection_{};
        bool hasPreviousCamera_ = false;

        std::unique_ptr<Microsoft::Xna::Framework::Graphics::RenderTarget2D> depthTarget_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::RenderTarget2D> normalTarget_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::RenderTarget2D> velocityTarget_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect>   effect_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect>   skinnedEffect_;
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/PointLightEXT.hpp"
#include "CNA/Graphics/ShadowQuality.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"

#include <memory>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class RenderTargetCube;
    class ShaderEffect;
    class TextureCube;
}

namespace CNA::Graphics {

    /**
     * @brief A point light's shadow, as six faces of a cube map.
     *
     * plan_modern.md Phase 10. A point light casts in every direction, so one map cannot hold its
     * shadow; the scene is rendered six times, once down each axis.
     *
     * **Cube rather than dual-paraboloid** (`MOD-1001`). Dual-paraboloid halves the passes, but it
     * warps geometry in the vertex shader, which means a triangle spanning the paraboloid's seam
     * is wrong unless it is tessellated -- a correctness problem that gets worse with larger
     * triangles, which are exactly the ones a shadow pass wants. `RenderTargetCube` already exists
     * in CNA with real render-into-a-face support, so the cube costs six passes and no new
     * concepts.
     *
     * **What is stored is linear distance from the light, divided by its range** (`MOD-1003`), not
     * projected depth. Projected depth is non-linear and is defined by the projection the *face*
     * used, so comparing against it means reconstructing which face a direction came from and
     * re-deriving that projection. Distance over range is the same number whichever face it landed
     * on, so a receiver samples the cube by direction and compares directly. The cost is that a
     * range far larger than the light actually reaches spends precision on empty space.
     *
     * Usage mirrors `ShadowMap`, once per face:
     *
     * ```
     * cube.update(light);
     * for (int face = 0; face < 6; ++face)
     * {
     *     cube.begin(face);
     *     //   ... draw the casters within the light's range ...
     *     cube.end();
     * }
     * ```
     */
    class CubeShadowMap
    {
    public:
        /** @brief The number of faces; six, and not a tuning knob. */
        static constexpr int kFaceCount = 6;

        /**
         * @brief Creates a cube shadow map at the resolution implied by @p quality.
         *
         * @param device  The device to render with.
         * @param quality The quality preset; see @ref sizeForQuality for why it is capped here.
         */
        CubeShadowMap(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                      ShadowQuality quality);

        /** @brief Destroys the cube and the caster effect. */
        ~CubeShadowMap();

        CubeShadowMap(const CubeShadowMap&)            = delete;
        CubeShadowMap& operator=(const CubeShadowMap&) = delete;

        /**
         * @brief Returns whether this device can generate point shadows at all.
         *
         * Same contract as `ShadowMap::isSupported`: where it is false the object still works and
         * the cube keeps meaning "nothing occludes", so the frame renders unshadowed.
         */
        [[nodiscard]] bool isSupported() const;

        /**
         * @brief Recomputes the six face matrices for a light.
         *
         * @param light The light to generate from; its position and range are what matter.
         * @throws std::logic_error      If a face pass is open.
         * @throws std::invalid_argument If the light's range is not positive -- the stored distance
         *                               is divided by it, and a zero range is a silent NaN.
         */
        void update(const PointLightEXT& light);

        /**
         * @brief Binds one face of the cube and clears it.
         *
         * Each face is cleared as it is bound, unlike the cascade atlas: the faces are separate
         * images, so clearing one cannot erase another.
         *
         * @param faceIndex 0 to 5, in `CubeMapFace` order.
         * @throws std::logic_error  If a face pass is already open, or `update` has never run.
         * @throws std::out_of_range If @p faceIndex names no face.
         */
        void begin(int faceIndex);

        /**
         * @brief Ends the current face's pass and restores the back buffer.
         *
         * @throws std::logic_error If no face pass is open.
         */
        void end();

        /** @brief Returns the cube holding the six distance faces. */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::TextureCube* getShadowTexture() const;

        /** @brief Returns the caster effect, or null where the renderer cannot compile it. */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::ShaderEffect* getCasterEffect() const;

        /** @brief Returns each face's edge length in pixels. */
        [[nodiscard]] int getSize() const;

        /** @brief Returns the configured quality. */
        [[nodiscard]] ShadowQuality getQuality() const;

        /** @brief Returns the light's world position, as of the last `update`. */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getLightPosition() const;

        /** @brief Returns the light's range, as of the last `update`. */
        [[nodiscard]] float getLightRange() const;

        /**
         * @brief Returns the depth bias used when comparing a receiver against the cube.
         *
         * In the same units as the stored value -- a fraction of the light's range -- so the same
         * bias means the same world distance only for lights of the same range.
         */
        [[nodiscard]] float getDepthBias() const;

        /** @brief Sets the depth bias. See @ref getDepthBias for its units. */
        void setDepthBias(float value);

        /**
         * @brief Returns the view matrix for one cube face.
         *
         * Exposed so the six orientations can be asserted directly. A face whose up vector is
         * upside down still renders a perfectly plausible shadow -- of a mirrored world.
         *
         * @param face     The face.
         * @param position The light's world position.
         * @return The view matrix.
         */
        [[nodiscard]] static Microsoft::Xna::Framework::Matrix computeFaceView(
            Microsoft::Xna::Framework::Graphics::CubeMapFace face,
            const Microsoft::Xna::Framework::Vector3& position);

        /**
         * @brief Returns the projection every face shares: 90 degrees, square, out to @p range.
         *
         * @param range The light's range, used as the far plane.
         * @return The projection matrix.
         */
        [[nodiscard]] static Microsoft::Xna::Framework::Matrix computeFaceProjection(float range);

        /**
         * @brief Returns the face size a quality level implies.
         *
         * Capped at 1024 whatever the quality asks for, and the cap is not timidity: six faces at
         * 4096 is 100 million texels, 400 MB at R32F, for one light. The quality table is written
         * for a single 2D map and means something different once it is multiplied by six.
         *
         * @param quality The quality level.
         * @return The edge length in pixels.
         */
        [[nodiscard]] static int sizeForQuality(ShadowQuality quality);

    private:
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
        ShadowQuality quality_;
        int   size_       = 0;
        bool  supported_  = false;
        bool  updated_    = false;
        bool  passOpen_   = false;
        float depthBias_  = 0.004f;
        float lightRange_ = 0.0f;
        Microsoft::Xna::Framework::Vector3 lightPosition_{0.0f, 0.0f, 0.0f};
        Microsoft::Xna::Framework::Matrix faceViewProjection_[kFaceCount]{};

        std::unique_ptr<Microsoft::Xna::Framework::Graphics::RenderTargetCube> cube_;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> casterEffect_;
    };

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

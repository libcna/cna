// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <memory>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class ShaderEffect;
}

namespace CNA::Graphics {

    class ClusteredLightBuffer;
    struct ClusteredLightEXT;

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Shades geometry with every light its cluster holds, rather than with a fixed few.
     *
     * This is the shading half of clustered forward. Geometry is drawn once; the fragment shader
     * finds its own cluster from its screen position and view distance, and walks that cluster's
     * light list. A scene with hundreds of lights costs each fragment only the handful that
     * actually reach it.
     *
     * ```cpp
     * effect.begin(world, view, projection, cameraPosition, lightBuffer);
     * effect.setBaseColor({0.8f, 0.8f, 0.8f});
     * effect.setRoughness(0.4f);
     * effect.getEffect()->Apply();
     * device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, triangleCount);
     * ```
     *
     * **It is a separate effect rather than an extension of `PbrEffect`**, and the reason is
     * structural rather than stylistic: `PbrEffect` owns no shader source at all. It fills a
     * `GpuDrawParams` and the *renderer* generates the program, so teaching it a light loop means
     * changing EasyGL's built-in effect family -- code compiled into every game whether or not
     * `CNA_CNAEXT` is on, which is exactly what the engine layer's guard rule forbids. What a game
     * gives up by using this instead is `PbrEffect`'s texture set and its shadowed punctual light;
     * what it gains is the light count.
     *
     * The vertex format is position and normal, in that attribute order, matching
     * `DepthNormalPrepass`.
     */
    class ClusteredForwardEffect
    {
    public:
        /**
         * @brief The most lights one fragment will walk, whatever its cluster holds.
         *
         * A bound the shader's loop can be written against. A cluster holding more than this is not
         * an error and does not fail; the lights past it simply do not contribute, and
         * `ClusteredLightAssignment::getMaxLightsPerCluster` is what tells a game it is happening.
         */
        static constexpr int kMaxLightsPerFragment = 128;

        /**
         * @brief Creates the effect and compiles its shader.
         *
         * @param device The device to render with.
         */
        explicit ClusteredForwardEffect(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /** @brief Destroys the effect and its shader. */
        ~ClusteredForwardEffect();

        ClusteredForwardEffect(const ClusteredForwardEffect&)            = delete;
        ClusteredForwardEffect& operator=(const ClusteredForwardEffect&) = delete;

        /** @brief Returns whether this renderer can run the effect. */
        [[nodiscard]] bool isSupported() const;

        /**
         * @brief Sets the transforms, the camera and the light list for the draws that follow.
         *
         * @param world          Object to world.
         * @param view           World to view.
         * @param projection     View to clip; must be the projection the grid was built with.
         * @param cameraPosition The camera's world position, for the specular term.
         * @param lights         The uploaded light list.
         * @throws std::runtime_error When the light buffer holds nothing.
         */
        void begin(const Microsoft::Xna::Framework::Matrix& world,
                   const Microsoft::Xna::Framework::Matrix& view,
                   const Microsoft::Xna::Framework::Matrix& projection,
                   const Microsoft::Xna::Framework::Vector3& cameraPosition,
                   const ClusteredLightBuffer& lights);

        /**
         * @brief Returns the underlying effect, for the caller to apply before drawing.
         *
         * @return The effect, or null when the shader did not compile.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::ShaderEffect* getEffect() const;

        /** @brief Returns the surface's base colour. */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getBaseColor() const;
        /** @brief Sets the surface's base colour. @param value Linear, per channel in [0, 1]. */
        void setBaseColor(const Microsoft::Xna::Framework::Vector3& value);

        /** @brief Returns how metallic the surface is. */
        [[nodiscard]] float getMetallic() const;
        /** @brief Sets how metallic the surface is. @param value Clamped to [0, 1]. */
        void setMetallic(float value);

        /** @brief Returns the surface's roughness. */
        [[nodiscard]] float getRoughness() const;
        /** @brief Sets the surface's roughness. @param value Clamped to [0.04, 1]. */
        void setRoughness(float value);

        /** @brief Returns the ambient term added once per fragment. */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getAmbient() const;
        /** @brief Sets the ambient term added once per fragment. @param value Linear, non-negative. */
        void setAmbient(const Microsoft::Xna::Framework::Vector3& value);

        /**
         * @brief One light's contribution to one surface point, computed on the CPU.
         *
         * The same arithmetic the shader runs, so the shading can be checked against numbers rather
         * than against a screenshot -- and so a game can ask what a light does to a point without
         * drawing it.
         *
         * @param light          The light, with its colour and intensity kept apart as the struct
         *                       stores them; the product is formed here, exactly as the upload does.
         * @param surface        The world-space point being lit.
         * @param normal         The surface normal, normalised.
         * @param cameraPosition The world-space eye position.
         * @param baseColor      The surface's base colour.
         * @param metallic       How metallic the surface is.
         * @param roughness      The surface's roughness.
         * @return The contribution, unbounded above; zero when the point is out of range.
         */
        [[nodiscard]] static Microsoft::Xna::Framework::Vector3 contribution(
            const ClusteredLightEXT& light,
            const Microsoft::Xna::Framework::Vector3& surface,
            const Microsoft::Xna::Framework::Vector3& normal,
            const Microsoft::Xna::Framework::Vector3& cameraPosition,
            const Microsoft::Xna::Framework::Vector3& baseColor, float metallic, float roughness);

    private:
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> effect_;

        Microsoft::Xna::Framework::Vector3 baseColor_{0.8f, 0.8f, 0.8f};
        Microsoft::Xna::Framework::Vector3 ambient_{0.0f, 0.0f, 0.0f};
        float metallic_  = 0.0f;
        float roughness_ = 0.5f;
        bool  supported_ = false;
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <memory>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class ShaderEffect;
    class Texture2D;
}

namespace Microsoft::Xna::Framework::Graphics {
    struct AreaLightEXT;
}

namespace CNA::Graphics {

    class AreaLightBrdfTable;
    class ClusteredLightBuffer;
    class PbrMaterialExtensions;
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

        /**
         * @brief Adds one area light to the draws that follow.
         *
         * **One per draw**, and deliberately so, for the reason `PunctualLightEXT` gives for its own
         * budget of one: an area light's integral is an edge sum over a clipped polygon, which is
         * an order of magnitude more work per fragment than a punctual light's dot products. A
         * scene wanting many of them wants them in the cluster grid, and the cluster grid holds
         * punctual lights.
         *
         * @param light The light; an invalid one clears the slot instead.
         * @param table The BRDF table the specular term reads; borrowed, not owned.
         */
        void setAreaLight(const Microsoft::Xna::Framework::Graphics::AreaLightEXT& light,
                          const AreaLightBrdfTable& table);

        /** @brief Removes the area light, so the following draws are lit by the clusters alone. */
        void clearAreaLight();

        /** @brief Returns whether an area light is set. */
        [[nodiscard]] bool hasAreaLight() const;

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

        /**
         * @brief Sets the material extensions the following draws are shaded with.
         *
         * **Only the scalar lobes are consumed.** This effect binds no material textures at all --
         * it has a base colour, a metallic and a roughness, not a texture set -- so a clearcoat's
         * strength, roughness and normal *maps* are carried by the extension set for the importer
         * and the round trip, and are not read here. The factors are.
         *
         * @param extensions The extensions; a neutral set turns every lobe off.
         */
        void setMaterialExtensions(const PbrMaterialExtensions& extensions);

        /** @brief Returns the material extensions the following draws are shaded with. */
        [[nodiscard]] const PbrMaterialExtensions& getMaterialExtensions() const;

        /** @brief Returns the surface's index of refraction. */
        [[nodiscard]] float getIor() const;
        /**
         * @brief Sets the surface's index of refraction (`KHR_materials_ior`).
         *
         * @param value 1.5 is glass and the default; 1 is a vacuum and the floor. Values below 1
         *              would refract the wrong way and are ignored.
         */
        void setIor(float value);

        /** @brief Returns the copy of the opaque frame transmissive materials refract against. */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* getOpaqueFrame() const;
        /**
         * @brief Gives the effect a copy of the frame's opaque pass, for transmission to refract.
         *
         * **This copy is the cost of transmission**, and it is not small: the opaque geometry has
         * to be drawn, resolved and copied before any transmissive surface can be drawn at all, so
         * a scene with one pane of glass in it pays for a second full-resolution image. The layer
         * does not make that copy for the application, because only the application knows when its
         * opaque pass ended.
         *
         * A transmissive material drawn without one is **refused** by @ref begin rather than
         * approximated: the result would not be slightly wrong, it would be an opaque object where
         * a glass one was asked for.
         *
         * @param frame The copy, or null to withdraw it. Borrowed, never owned.
         */
        void setOpaqueFrame(Microsoft::Xna::Framework::Graphics::Texture2D* frame);

        /**
         * @brief Returns what a volume does to light crossing it, by Beer's law.
         *
         * @param attenuationColor    The colour white light becomes after one distance.
         * @param attenuationDistance The distance; non-positive means the volume absorbs nothing.
         * @param thickness           How far the light travelled through the volume.
         * @return The per-channel survival fraction, each in [0, 1].
         */
        [[nodiscard]] static Microsoft::Xna::Framework::Vector3 volumeAttenuation(
            const Microsoft::Xna::Framework::Vector3& attenuationColor, float attenuationDistance,
            float thickness);

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
         * @param clearcoat      How strongly the clearcoat lobe is applied, 0 to 1.
         * @param clearcoatRoughness The clearcoat layer's own roughness.
         * @param sheenColor     The sheen lobe's colour; black disables it.
         * @param sheenRoughness The sheen lobe's roughness.
         * @param iridescence    How strongly a thin film replaces the Fresnel term.
         * @param iridescenceIor The film's own index of refraction.
         * @param iridescenceThickness The film's thickness in nanometres.
         * @return The contribution, unbounded above; zero when the point is out of range.
         */
        [[nodiscard]] static Microsoft::Xna::Framework::Vector3 contribution(
            const ClusteredLightEXT& light,
            const Microsoft::Xna::Framework::Vector3& surface,
            const Microsoft::Xna::Framework::Vector3& normal,
            const Microsoft::Xna::Framework::Vector3& cameraPosition,
            const Microsoft::Xna::Framework::Vector3& baseColor, float metallic, float roughness,
            float clearcoat = 0.0f, float clearcoatRoughness = 0.0f,
            const Microsoft::Xna::Framework::Vector3& sheenColor = {0.0f, 0.0f, 0.0f},
            float sheenRoughness = 0.0f, float iridescence = 0.0f, float iridescenceIor = 1.3f,
            float iridescenceThickness = 400.0f);

        /**
         * @brief The same contribution, with the extra lobes taken from an extension set.
         *
         * The overload a caller holding a material should use: it cannot get the order of the
         * scalar arguments wrong, and it picks up a lobe added later without a signature change.
         *
         * @param light          The light.
         * @param surface        The world-space point being lit.
         * @param normal         The surface normal, normalised.
         * @param cameraPosition The world-space eye position.
         * @param baseColor      The surface's base colour.
         * @param metallic       How metallic the surface is.
         * @param roughness      The surface's roughness.
         * @param extensions     The material's extra lobes.
         * @return The contribution, unbounded above; zero when the point is out of range.
         */
        [[nodiscard]] static Microsoft::Xna::Framework::Vector3 contribution(
            const ClusteredLightEXT& light,
            const Microsoft::Xna::Framework::Vector3& surface,
            const Microsoft::Xna::Framework::Vector3& normal,
            const Microsoft::Xna::Framework::Vector3& cameraPosition,
            const Microsoft::Xna::Framework::Vector3& baseColor, float metallic, float roughness,
            const PbrMaterialExtensions& extensions);

    private:
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> effect_;

        Microsoft::Xna::Framework::Vector3 baseColor_{0.8f, 0.8f, 0.8f};
        Microsoft::Xna::Framework::Vector3 ambient_{0.0f, 0.0f, 0.0f};
        float metallic_  = 0.0f;
        float roughness_ = 0.5f;
        bool  supported_ = false;

        std::unique_ptr<Microsoft::Xna::Framework::Graphics::AreaLightEXT> areaLight_;
        const AreaLightBrdfTable* areaTable_ = nullptr;
        std::unique_ptr<PbrMaterialExtensions> extensions_;
        Microsoft::Xna::Framework::Graphics::Texture2D* opaqueFrame_ = nullptr;
        float ior_ = 1.5f;
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

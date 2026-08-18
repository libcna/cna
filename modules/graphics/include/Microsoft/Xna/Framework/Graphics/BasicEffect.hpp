// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectFog.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectLights.hpp"
#include "Microsoft/Xna/Framework/Graphics/IShadowReceiverEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectMatrices.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;

    /**
     * @brief Built-in effect that supports optional texturing, vertex coloring, fog, and lighting.
     */
    class BasicEffect : public Effect, public IEffectMatrices, public IEffectFog, public IEffectLights, public IShadowReceiverEXT
    {
    public:
        /**
         * @brief Constructs a BasicEffect for the given graphics device.
         *
         * @param device The graphics device that will own this effect.
         */
        explicit BasicEffect(GraphicsDevice& device);

        /**
         * @brief Creates a clone of this effect.
         *
         * @return Pointer to the cloned Effect.
         */
        [[nodiscard]] Effect* Clone() override;

        /** @brief The world matrix applied to drawn geometry. */
        Matrix World      = Matrix::getIdentityProperty();
        /** @brief The view matrix representing the camera transform. */
        Matrix View       = Matrix::getIdentityProperty();
        /** @brief The projection matrix applied to transform geometry to clip space. */
        Matrix Projection = Matrix::getIdentityProperty();

        /** @brief Gets or sets whether per-vertex color is used for rendering. */
        bool VertexColorEnabled = false;

        /** @brief Returns the fully qualified .NET type name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /**
         * @brief Gets the world matrix.
         *
         * @return The current world matrix.
         */
        [[nodiscard]] Matrix getWorldProperty() const override      { return World; }

        /**
         * @brief Sets the world matrix.
         *
         * @param v The new world matrix.
         */
        void setWorldProperty(const Matrix& v) override             { World = v; }

        /**
         * @brief Gets the view matrix.
         *
         * @return The current view matrix.
         */
        [[nodiscard]] Matrix getViewProperty() const override       { return View; }

        /**
         * @brief Sets the view matrix.
         *
         * @param v The new view matrix.
         */
        void setViewProperty(const Matrix& v) override              { View = v; }

        /**
         * @brief Gets the projection matrix.
         *
         * @return The current projection matrix.
         */
        [[nodiscard]] Matrix getProjectionProperty() const override  { return Projection; }

        /**
         * @brief Sets the projection matrix.
         *
         * @param v The new projection matrix.
         */
        void setProjectionProperty(const Matrix& v) override        { Projection = v; }

        // IEffectLights

        /**
         * @brief Gets the ambient light color applied to the scene.
         *
         * @return The ambient light color as a Vector3.
         */
        [[nodiscard]] Vector3 getAmbientLightColorProperty() const override;

        /**
         * @brief Sets the ambient light color applied to the scene.
         *
         * @param value The new ambient light color.
         */
        void setAmbientLightColorProperty(const Vector3& value) override;

        /**
         * @brief Gets whether per-vertex lighting is enabled.
         *
         * @return True if lighting is enabled.
         */
        [[nodiscard]] bool getLightingEnabledProperty() const override;

        /**
         * @brief Sets whether per-vertex lighting is enabled.
         *
         * @param value True to enable lighting.
         */
        void setLightingEnabledProperty(bool value) override;

        /**
         * @brief Gets the first directional light source.
         *
         * @return Reference to DirectionalLight0.
         */
        [[nodiscard]] DirectionalLight& getDirectionalLight0Property() override;

        /**
         * @brief Gets the second directional light source.
         *
         * @return Reference to DirectionalLight1.
         */
        [[nodiscard]] DirectionalLight& getDirectionalLight1Property() override;

        /**
         * @brief Gets the third directional light source.
         *
         * @return Reference to DirectionalLight2.
         */
        [[nodiscard]] DirectionalLight& getDirectionalLight2Property() override;

        /**
         * @brief Activates default three-point lighting (key, fill, and back lights).
         */
        void EnableDefaultLighting() override;

        /** @brief The first directional light source. */
        DirectionalLight DirectionalLight0;
        /** @brief The second directional light source. */
        DirectionalLight DirectionalLight1;
        /** @brief The third directional light source. */
        DirectionalLight DirectionalLight2;

        /**
         * @brief Gets whether per-pixel lighting is preferred over per-vertex lighting.
         *
         * @return True if per-pixel lighting is preferred.
         */
        [[nodiscard]] bool getPreferPerPixelLightingProperty() const;

        /**
         * @brief Sets whether per-pixel lighting is preferred.
         *
         * @param value True to prefer per-pixel lighting.
         */
        void setPreferPerPixelLightingProperty(bool value);

        /**
         * @brief Gets the material diffuse color (range 0 to 1).
         *
         * @return The diffuse color as a Vector3.
         */
        [[nodiscard]] Vector3 getDiffuseColorProperty() const;

        /**
         * @brief Sets the material diffuse color (range 0 to 1).
         *
         * @param value The new diffuse color.
         */
        void setDiffuseColorProperty(const Vector3& value);

        /**
         * @brief Gets the material emissive color.
         *
         * @return The emissive color as a Vector3.
         */
        [[nodiscard]] Vector3 getEmissiveColorProperty() const;

        /**
         * @brief Sets the material emissive color.
         *
         * @param value The new emissive color.
         */
        void setEmissiveColorProperty(const Vector3& value);

        /**
         * @brief Gets the material specular color.
         *
         * @return The specular color as a Vector3.
         */
        [[nodiscard]] Vector3 getSpecularColorProperty() const;

        /**
         * @brief Sets the material specular color.
         *
         * @param value The new specular color.
         */
        void setSpecularColorProperty(const Vector3& value);

        /**
         * @brief Gets the material specular power (shininess exponent).
         *
         * @return The specular power.
         */
        [[nodiscard]] float getSpecularPowerProperty() const;

        /**
         * @brief Sets the material specular power (shininess exponent).
         *
         * @param value The new specular power.
         */
        void setSpecularPowerProperty(float value);

        /**
         * @brief Gets the material alpha (opacity), in the range [0, 1].
         *
         * @return The alpha value.
         */
        [[nodiscard]] float getAlphaProperty() const;

        /**
         * @brief Sets the material alpha (opacity), in the range [0, 1].
         *
         * @param value The new alpha value.
         */
        void setAlphaProperty(float value);

        /**
         * @brief Gets whether texture mapping is enabled.
         *
         * @return True if texturing is enabled.
         */
        [[nodiscard]] bool getTextureEnabledProperty() const;

        /**
         * @brief Sets whether texture mapping is enabled.
         *
         * @param value True to enable texturing.
         */
        void setTextureEnabledProperty(bool value);

        /**
         * @brief Gets the texture applied to geometry rendered with this effect.
         *
         * @return Pointer to the current Texture2D, or nullptr if none.
         */
        [[nodiscard]] Texture2D* getTextureProperty() const;

        /**
         * @brief Sets the texture applied to geometry rendered with this effect.
         *
         * @param value Pointer to the Texture2D to use.
         */
        void setTextureProperty(Texture2D* value);

        /**
         * @brief Gives this effect shared ownership of a texture, keeping it alive for as long as
         *        the effect exists -- matching real XNA's GC-tracked `Effect.Texture` reference.
         *
         * `setTextureProperty(Texture2D*)` stores a non-owning pointer (used by, e.g., a `Model`
         * that owns a shared texture pool across multiple mesh parts); a standalone content-loaded
         * effect (`content.Load<BasicEffect>()`, plan_xnb.md XNB-32) has no such external owner,
         * so it must keep its own texture reference alive instead.
         *
         * @param texture The texture to take shared ownership of; also becomes the effect's
         *                current texture (as if passed to `setTextureProperty()`).
         */
        CNAEXT void SetOwnedTexture(std::shared_ptr<Texture2D> texture);

        // IEffectFog

        /**
         * @brief Gets the fog color.
         *
         * @return The fog color as a Vector3.
         */
        [[nodiscard]] Vector3 getFogColorProperty() const override;

        /**
         * @brief Sets the fog color.
         *
         * @param value The new fog color.
         */
        void setFogColorProperty(const Vector3& value) override;

        /**
         * @brief Gets whether distance fog is enabled.
         *
         * @return True if fog is enabled.
         */
        [[nodiscard]] bool getFogEnabledProperty() const override;

        /**
         * @brief Sets whether distance fog is enabled.
         *
         * @param value True to enable fog.
         */
        void setFogEnabledProperty(bool value) override;

        /**
         * @brief Gets the camera-space distance at which fog begins.
         *
         * @return The fog start distance.
         */
        [[nodiscard]] float getFogStartProperty() const override;

        /**
         * @brief Sets the camera-space distance at which fog begins.
         *
         * @param value The new fog start distance.
         */
        void setFogStartProperty(float value) override;

        /**
         * @brief Gets the camera-space distance at which fog reaches full density.
         *
         * @return The fog end distance.
         */
        [[nodiscard]] float getFogEndProperty() const override;

        /**
         * @brief Sets the camera-space distance at which fog reaches full density.
         *
         * @param value The new fog end distance.
         */
        void setFogEndProperty(float value) override;

        /**
         * @brief Fills a GpuDrawParams struct with this effect's current render parameters.
         *
         * Populates texture, diffuse color, alpha, ambient color, directional light 0,
         * world matrix, and the textureEnabled / vertexColorEnabled / lightingEnabled flags.
         *
         * @param params Output struct to populate.
         */
        CNAEXT void FillGpuDrawParams(CNA::Internal::Renderers::GpuDrawParams& params) const override;

    protected:
        /**
         * @brief Applies shader parameters to the graphics device before drawing.
         */
        void OnApply() override;


    public:
        // ---- CNAEXT: shadow reception (plan_modern.md MOD-820) ----------------------------

        /** @brief Sets the shadow map this effect samples. @param shadowMap The map, or null. */
        CNAEXT void setShadowMapEXT(Texture2D* shadowMap) override;
        /** @brief Returns the attached shadow map, or null. */
        CNAEXT [[nodiscard]] Texture2D* getShadowMapEXT() const override;
        /** @brief Sets the light's view-projection matrix. @param lightViewProjection The matrix. */
        CNAEXT void setLightViewProjectionEXT(const Matrix& lightViewProjection) override;
        /** @brief Returns the light's view-projection matrix. */
        CNAEXT [[nodiscard]] Matrix getLightViewProjectionEXT() const override;
        /** @brief Enables or disables shadow sampling. @param enabled True to sample. */
        CNAEXT void setShadowsEnabledEXT(bool enabled) override;
        /** @brief Returns whether shadow sampling is enabled. */
        CNAEXT [[nodiscard]] bool isShadowsEnabledEXT() const override;
        /** @brief Sets the shadow comparison bias. @param bias The bias. */
        CNAEXT void setShadowDepthBiasEXT(float bias) override;
        /** @brief Returns the shadow comparison bias. */
        CNAEXT [[nodiscard]] float getShadowDepthBiasEXT() const override;
        /** @brief Sets the PCF radius in shadow-map texels. @param radius 0, 1 or 2. */
        CNAEXT void setShadowFilterRadiusEXT(int radius) override;
        /** @brief Returns the PCF radius in shadow-map texels. */
        CNAEXT [[nodiscard]] int getShadowFilterRadiusEXT() const override;
        /** @brief Supplies a cascaded shadow set. @param state The cascades. */
        CNAEXT void setShadowCascadesEXT(const ShadowCascadeStateEXT& state) override;
        /** @brief Returns the cascade set in use; `Count == 0` means a single map. */
        CNAEXT [[nodiscard]] const ShadowCascadeStateEXT& getShadowCascadesEXT() const override;
        /** @brief Supplies one punctual light and its shadow. @param light The light. */
        CNAEXT void setPunctualLightEXT(const PunctualLightEXT& light) override;
        /** @brief Returns the punctual light in use; `Kind == None` means there is none. */
        CNAEXT [[nodiscard]] const PunctualLightEXT& getPunctualLightEXT() const override;

    private:

        // CNAEXT shadow reception (MOD-820). Inert by default: an effect that is never given a
        // shadow map renders exactly as it did before this existed.
        Texture2D* shadowMapEXT_ = nullptr;
        Matrix lightViewProjectionEXT_{};
        bool  shadowsEnabledEXT_ = false;
        float shadowDepthBiasEXT_ = 0.0015f;
        int   shadowFilterRadiusEXT_ = 1;   // 3x3, the default ShadowQuality::Medium asks for
        ShadowCascadeStateEXT shadowCascadesEXT_{};
        PunctualLightEXT punctualLightEXT_{};
        explicit BasicEffect(const BasicEffect& cloneSource);

        Vector3 diffuseColor_           = Vector3{1.0f, 1.0f, 1.0f};
        Vector3 emissiveColor_          = Vector3::Zero;
        Vector3 specularColor_          = Vector3{1.0f, 1.0f, 1.0f};
        float   specularPower_          = 16.0f;
        Vector3 ambientLightColor_      = Vector3::Zero;
        float   alpha_                  = 1.0f;
        bool    lightingEnabled_        = false;
        bool    preferPerPixelLighting_ = false;
        bool    textureEnabled_         = false;
        Texture2D* texture_             = nullptr;
        std::shared_ptr<Texture2D> ownedTexture_; // see SetOwnedTexture()
        bool    fogEnabled_             = false;
        Vector3 fogColor_               = Vector3::Zero;
        float   fogStart_               = 0.0f;
        float   fogEnd_                 = 1.0f;
    };
}

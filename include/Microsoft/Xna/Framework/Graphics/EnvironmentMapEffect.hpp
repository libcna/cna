// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectFog.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectLights.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectMatrices.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class EffectParameter;

    /**
     * @brief Built-in effect that applies cube-map environment mapping with optional fog and lighting.
     */
    class EnvironmentMapEffect : public Effect, public IEffectMatrices, public IEffectLights, public IEffectFog
    {
    public:
        /**
         * @brief Constructs an EnvironmentMapEffect for the given graphics device.
         *
         * @param device The graphics device that will own this effect.
         */
        explicit EnvironmentMapEffect(GraphicsDevice& device);

        /**
         * @brief Creates a clone of this effect.
         *
         * @return Pointer to the cloned Effect.
         */
        [[nodiscard]] Effect* Clone();

        // IEffectMatrices

        /**
         * @brief Gets the world matrix.
         *
         * @return The current world matrix.
         */
        [[nodiscard]] Matrix getWorldProperty() const override;

        /**
         * @brief Sets the world matrix.
         *
         * @param value The new world matrix.
         */
        void setWorldProperty(const Matrix& value) override;

        /**
         * @brief Gets the view matrix.
         *
         * @return The current view matrix.
         */
        [[nodiscard]] Matrix getViewProperty() const override;

        /**
         * @brief Sets the view matrix.
         *
         * @param value The new view matrix.
         */
        void setViewProperty(const Matrix& value) override;

        /**
         * @brief Gets the projection matrix.
         *
         * @return The current projection matrix.
         */
        [[nodiscard]] Matrix getProjectionProperty() const override;

        /**
         * @brief Sets the projection matrix.
         *
         * @param value The new projection matrix.
         */
        void setProjectionProperty(const Matrix& value) override;

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
         * @brief Gets the diffuse texture applied to geometry rendered with this effect.
         *
         * @return Pointer to the current Texture2D, or nullptr if none.
         */
        [[nodiscard]] Texture2D* getTextureProperty() const;

        /**
         * @brief Sets the diffuse texture applied to geometry rendered with this effect.
         *
         * @param value Pointer to the Texture2D to use.
         */
        void setTextureProperty(Texture2D* value);

        /**
         * @brief Gets the cube-map texture used for environment mapping.
         *
         * @return Pointer to the current TextureCube, or nullptr if none.
         */
        [[nodiscard]] TextureCube* getEnvironmentMapProperty() const;

        /**
         * @brief Sets the cube-map texture used for environment mapping.
         *
         * @param value Pointer to the TextureCube to use.
         */
        void setEnvironmentMapProperty(TextureCube* value);

        /**
         * @brief Gets the amount of environment map to blend, in the range [0, 1].
         *
         * @return The environment map blend amount.
         */
        [[nodiscard]] float getEnvironmentMapAmountProperty() const;

        /**
         * @brief Sets the amount of environment map to blend, in the range [0, 1].
         *
         * @param value The new blend amount.
         */
        void setEnvironmentMapAmountProperty(float value);

        /**
         * @brief Gets the specular color contribution from the environment map.
         *
         * @return The specular tint color as a Vector3.
         */
        [[nodiscard]] Vector3 getEnvironmentMapSpecularProperty() const;

        /**
         * @brief Sets the specular color contribution from the environment map.
         *
         * @param value The new specular tint color.
         */
        void setEnvironmentMapSpecularProperty(const Vector3& value);

        /**
         * @brief Gets the Fresnel factor that controls the environment map blend at oblique angles.
         *
         * @return The Fresnel factor.
         */
        [[nodiscard]] float getFresnelFactorProperty() const;

        /**
         * @brief Sets the Fresnel factor that controls the environment map blend at oblique angles.
         *
         * @param value The new Fresnel factor.
         */
        void setFresnelFactorProperty(float value);

    protected:
        /**
         * @brief Applies shader parameters to the graphics device before drawing.
         */
        void OnApply() override;

    private:
        explicit EnvironmentMapEffect(const EnvironmentMapEffect& cloneSource);

        void CacheEffectParameters();

        // Textures stored directly (Texture2D/TextureCube do not inherit from Texture)
        Texture2D*   texture_        = nullptr;
        TextureCube* environmentMap_ = nullptr;

        EffectParameter* environmentMapAmountParam_   = nullptr;
        EffectParameter* environmentMapSpecularParam_ = nullptr;
        EffectParameter* fresnelFactorParam_          = nullptr;
        EffectParameter* diffuseColorParam_           = nullptr;
        EffectParameter* emissiveColorParam_          = nullptr;
        EffectParameter* eyePositionParam_            = nullptr;
        EffectParameter* fogColorParam_               = nullptr;
        EffectParameter* fogVectorParam_              = nullptr;
        EffectParameter* worldParam_                  = nullptr;
        EffectParameter* worldInverseTransposeParam_  = nullptr;
        EffectParameter* worldViewProjParam_          = nullptr;
        EffectParameter* shaderIndexParam_            = nullptr;

        bool    oneLight_       = false;
        bool    fogEnabled_     = false;
        bool    fresnelEnabled_ = true;
        bool    specularEnabled_= false;

        Matrix  world_      = Matrix::getIdentityProperty();
        Matrix  view_       = Matrix::getIdentityProperty();
        Matrix  projection_ = Matrix::getIdentityProperty();
        Matrix  worldView_;

        Vector3 diffuseColor_     = Vector3{1.0f, 1.0f, 1.0f};
        Vector3 emissiveColor_    = Vector3{0.0f, 0.0f, 0.0f};
        Vector3 ambientLightColor_= Vector3{0.0f, 0.0f, 0.0f};
        float   alpha_            = 1.0f;

        float   fogStart_ = 0.0f;
        float   fogEnd_   = 1.0f;

        float   environmentMapAmount_   = 1.0f;
        Vector3 environmentMapSpecular_ = Vector3{0.0f, 0.0f, 0.0f};
        float   fresnelFactor_          = 1.0f;

        int dirtyFlags_;
    };
}

// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectFog.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectLights.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectMatrices.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;

    /// Built-in effect that supports per-pixel lighting, fog, and texture mapping.
    class BasicEffect : public Effect, public IEffectMatrices, public IEffectFog, public IEffectLights
    {
    public:
        /// Constructs a BasicEffect for the given graphics device.
        explicit BasicEffect(GraphicsDevice& device);

        // XNA public member vars (also exposed via IEffectMatrices)
        /// The world matrix applied to drawn geometry.
        Matrix World      = Matrix::getIdentityProperty();
        /// The view matrix representing the camera transform.
        Matrix View       = Matrix::getIdentityProperty();
        /// The projection matrix applied to transform to clip space.
        Matrix Projection = Matrix::getIdentityProperty();

        /// Gets or sets whether per-vertex color is used for rendering.
        bool VertexColorEnabled = true;

        // IEffectMatrices — delegate to World/View/Projection member vars
        /// Gets the world matrix.
        [[nodiscard]] Matrix getWorldProperty() const override      { return World; }
        /// Sets the world matrix.
        void setWorldProperty(const Matrix& v) override             { World = v; }
        /// Gets the view matrix.
        [[nodiscard]] Matrix getViewProperty() const override       { return View; }
        /// Sets the view matrix.
        void setViewProperty(const Matrix& v) override              { View = v; }
        /// Gets the projection matrix.
        [[nodiscard]] Matrix getProjectionProperty() const override  { return Projection; }
        /// Sets the projection matrix.
        void setProjectionProperty(const Matrix& v) override        { Projection = v; }

        // IEffectLights
        /// Gets the ambient light color.
        [[nodiscard]] Vector3 getAmbientLightColorProperty() const override;
        /// Sets the ambient light color.
        void setAmbientLightColorProperty(const Vector3& value) override;
        /// Gets whether lighting is enabled.
        [[nodiscard]] bool getLightingEnabledProperty() const override;
        /// Sets whether lighting is enabled.
        void setLightingEnabledProperty(bool value) override;
        /// Gets the first directional light.
        [[nodiscard]] DirectionalLight& getDirectionalLight0Property() override;
        /// Gets the second directional light.
        [[nodiscard]] DirectionalLight& getDirectionalLight1Property() override;
        /// Gets the third directional light.
        [[nodiscard]] DirectionalLight& getDirectionalLight2Property() override;
        /// Activates default three-point lighting.
        void EnableDefaultLighting() override;

        /// The first directional light source.
        DirectionalLight DirectionalLight0;
        /// The second directional light source.
        DirectionalLight DirectionalLight1;
        /// The third directional light source.
        DirectionalLight DirectionalLight2;

        /// Gets whether per-pixel lighting is preferred over per-vertex lighting.
        [[nodiscard]] bool getPreferPerPixelLightingProperty() const;
        /// Sets whether per-pixel lighting is preferred.
        void setPreferPerPixelLightingProperty(bool value);

        /// Gets the diffuse color of the material.
        [[nodiscard]] Vector3 getDiffuseColorProperty() const;
        /// Sets the diffuse color of the material.
        void setDiffuseColorProperty(const Vector3& value);
        /// Gets the emissive color of the material.
        [[nodiscard]] Vector3 getEmissiveColorProperty() const;
        /// Sets the emissive color of the material.
        void setEmissiveColorProperty(const Vector3& value);
        /// Gets the specular color of the material.
        [[nodiscard]] Vector3 getSpecularColorProperty() const;
        /// Sets the specular color of the material.
        void setSpecularColorProperty(const Vector3& value);
        /// Gets the specular power (shininess) of the material.
        [[nodiscard]] float getSpecularPowerProperty() const;
        /// Sets the specular power (shininess) of the material.
        void setSpecularPowerProperty(float value);
        /// Gets the alpha (opacity) of the material.
        [[nodiscard]] float getAlphaProperty() const;
        /// Sets the alpha (opacity) of the material.
        void setAlphaProperty(float value);

        /// Gets whether texture mapping is enabled.
        [[nodiscard]] bool getTextureEnabledProperty() const;
        /// Sets whether texture mapping is enabled.
        void setTextureEnabledProperty(bool value);
        /// Gets the texture applied to geometry rendered with this effect.
        [[nodiscard]] Texture2D* getTextureProperty() const;
        /// Sets the texture applied to geometry rendered with this effect.
        void setTextureProperty(Texture2D* value);

        // IEffectFog
        /// Gets the fog color.
        [[nodiscard]] Vector3 getFogColorProperty() const override;
        /// Sets the fog color.
        void setFogColorProperty(const Vector3& value) override;
        /// Gets whether fog is enabled.
        [[nodiscard]] bool getFogEnabledProperty() const override;
        /// Sets whether fog is enabled.
        void setFogEnabledProperty(bool value) override;
        /// Gets the distance at which fog begins.
        [[nodiscard]] float getFogStartProperty() const override;
        /// Sets the distance at which fog begins.
        void setFogStartProperty(float value) override;
        /// Gets the distance at which fog reaches full density.
        [[nodiscard]] float getFogEndProperty() const override;
        /// Sets the distance at which fog reaches full density.
        void setFogEndProperty(float value) override;

    protected:
        /// Applies shader parameters to the graphics device before drawing.
        void OnApply() override;

    private:
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
        bool    fogEnabled_             = false;
        Vector3 fogColor_               = Vector3::Zero;
        float   fogStart_               = 0.0f;
        float   fogEnd_                 = 1.0f;
    };
}

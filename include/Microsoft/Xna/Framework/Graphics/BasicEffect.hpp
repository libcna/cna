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

    class BasicEffect : public Effect, public IEffectMatrices, public IEffectFog, public IEffectLights
    {
    public:
        explicit BasicEffect(GraphicsDevice& device);

        // XNA public member vars (also exposed via IEffectMatrices)
        Matrix World      = Matrix::getIdentityProperty();
        Matrix View       = Matrix::getIdentityProperty();
        Matrix Projection = Matrix::getIdentityProperty();

        bool VertexColorEnabled = true;

        // IEffectMatrices — delegate to World/View/Projection member vars
        [[nodiscard]] Matrix getWorldProperty() const override      { return World; }
        void setWorldProperty(const Matrix& v) override             { World = v; }
        [[nodiscard]] Matrix getViewProperty() const override       { return View; }
        void setViewProperty(const Matrix& v) override              { View = v; }
        [[nodiscard]] Matrix getProjectionProperty() const override  { return Projection; }
        void setProjectionProperty(const Matrix& v) override        { Projection = v; }

        // IEffectLights
        [[nodiscard]] Vector3 getAmbientLightColorProperty() const override;
        void setAmbientLightColorProperty(const Vector3& value) override;
        [[nodiscard]] bool getLightingEnabledProperty() const override;
        void setLightingEnabledProperty(bool value) override;
        [[nodiscard]] DirectionalLight& getDirectionalLight0Property() override;
        [[nodiscard]] DirectionalLight& getDirectionalLight1Property() override;
        [[nodiscard]] DirectionalLight& getDirectionalLight2Property() override;
        void EnableDefaultLighting() override;

        DirectionalLight DirectionalLight0;
        DirectionalLight DirectionalLight1;
        DirectionalLight DirectionalLight2;

        [[nodiscard]] bool getPreferPerPixelLightingProperty() const;
        void setPreferPerPixelLightingProperty(bool value);

        [[nodiscard]] Vector3 getDiffuseColorProperty() const;
        void setDiffuseColorProperty(const Vector3& value);
        [[nodiscard]] Vector3 getEmissiveColorProperty() const;
        void setEmissiveColorProperty(const Vector3& value);
        [[nodiscard]] Vector3 getSpecularColorProperty() const;
        void setSpecularColorProperty(const Vector3& value);
        [[nodiscard]] float getSpecularPowerProperty() const;
        void setSpecularPowerProperty(float value);
        [[nodiscard]] float getAlphaProperty() const;
        void setAlphaProperty(float value);

        [[nodiscard]] bool getTextureEnabledProperty() const;
        void setTextureEnabledProperty(bool value);
        [[nodiscard]] Texture2D* getTextureProperty() const;
        void setTextureProperty(Texture2D* value);

        // IEffectFog
        [[nodiscard]] Vector3 getFogColorProperty() const override;
        void setFogColorProperty(const Vector3& value) override;
        [[nodiscard]] bool getFogEnabledProperty() const override;
        void setFogEnabledProperty(bool value) override;
        [[nodiscard]] float getFogStartProperty() const override;
        void setFogStartProperty(float value) override;
        [[nodiscard]] float getFogEndProperty() const override;
        void setFogEndProperty(float value) override;

    protected:
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

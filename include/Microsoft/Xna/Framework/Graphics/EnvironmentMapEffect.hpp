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

    /// Built-in effect that supports environment mapping.
    class EnvironmentMapEffect : public Effect, public IEffectMatrices, public IEffectLights, public IEffectFog
    {
    public:
        explicit EnvironmentMapEffect(GraphicsDevice& device);

        [[nodiscard]] Effect* Clone();

        // IEffectMatrices
        [[nodiscard]] Matrix getWorldProperty() const override;
        void setWorldProperty(const Matrix& value) override;
        [[nodiscard]] Matrix getViewProperty() const override;
        void setViewProperty(const Matrix& value) override;
        [[nodiscard]] Matrix getProjectionProperty() const override;
        void setProjectionProperty(const Matrix& value) override;

        [[nodiscard]] Vector3 getDiffuseColorProperty() const;
        void setDiffuseColorProperty(const Vector3& value);

        [[nodiscard]] Vector3 getEmissiveColorProperty() const;
        void setEmissiveColorProperty(const Vector3& value);

        [[nodiscard]] float getAlphaProperty() const;
        void setAlphaProperty(float value);

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

        // IEffectFog
        [[nodiscard]] Vector3 getFogColorProperty() const override;
        void setFogColorProperty(const Vector3& value) override;
        [[nodiscard]] bool getFogEnabledProperty() const override;
        void setFogEnabledProperty(bool value) override;
        [[nodiscard]] float getFogStartProperty() const override;
        void setFogStartProperty(float value) override;
        [[nodiscard]] float getFogEndProperty() const override;
        void setFogEndProperty(float value) override;

        [[nodiscard]] Texture2D* getTextureProperty() const;
        void setTextureProperty(Texture2D* value);

        [[nodiscard]] TextureCube* getEnvironmentMapProperty() const;
        void setEnvironmentMapProperty(TextureCube* value);

        [[nodiscard]] float getEnvironmentMapAmountProperty() const;
        void setEnvironmentMapAmountProperty(float value);

        [[nodiscard]] Vector3 getEnvironmentMapSpecularProperty() const;
        void setEnvironmentMapSpecularProperty(const Vector3& value);

        [[nodiscard]] float getFresnelFactorProperty() const;
        void setFresnelFactorProperty(float value);

    protected:
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

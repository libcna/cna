// SPDX-License-Identifier: MS-PL
#pragma once

#include <vector>

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
    class EffectParameter;

    /// Built-in effect for rendering skinned character models.
    class SkinnedEffect : public Effect, public IEffectMatrices, public IEffectLights, public IEffectFog
    {
    public:
        static const int MaxBones = 72;

        explicit SkinnedEffect(GraphicsDevice& device);

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

        [[nodiscard]] Vector3 getSpecularColorProperty() const;
        void setSpecularColorProperty(const Vector3& value);

        [[nodiscard]] float getSpecularPowerProperty() const;
        void setSpecularPowerProperty(float value);

        [[nodiscard]] float getAlphaProperty() const;
        void setAlphaProperty(float value);

        [[nodiscard]] bool getPreferPerPixelLightingProperty() const;
        void setPreferPerPixelLightingProperty(bool value);

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

        [[nodiscard]] int getWeightsPerVertexProperty() const;
        void setWeightsPerVertexProperty(int value);

        void SetBoneTransforms(const std::vector<Matrix>& boneTransforms);
        [[nodiscard]] std::vector<Matrix> GetBoneTransforms(int count) const;

    protected:
        void OnApply() override;

    private:
        explicit SkinnedEffect(const SkinnedEffect& cloneSource);

        void CacheEffectParameters();

        // Texture stored directly (Texture2D does not inherit from Texture in CNA)
        Texture2D* texture_ = nullptr;

        EffectParameter* diffuseColorParam_          = nullptr;
        EffectParameter* emissiveColorParam_         = nullptr;
        EffectParameter* specularColorParam_         = nullptr;
        EffectParameter* specularPowerParam_         = nullptr;
        EffectParameter* eyePositionParam_           = nullptr;
        EffectParameter* fogColorParam_              = nullptr;
        EffectParameter* fogVectorParam_             = nullptr;
        EffectParameter* worldParam_                 = nullptr;
        EffectParameter* worldInverseTransposeParam_ = nullptr;
        EffectParameter* worldViewProjParam_         = nullptr;
        EffectParameter* bonesParam_                 = nullptr;
        EffectParameter* shaderIndexParam_           = nullptr;

        bool    preferPerPixelLighting_ = false;
        bool    oneLight_               = false;
        bool    fogEnabled_             = false;

        Matrix  world_      = Matrix::getIdentityProperty();
        Matrix  view_       = Matrix::getIdentityProperty();
        Matrix  projection_ = Matrix::getIdentityProperty();
        Matrix  worldView_;

        Vector3 diffuseColor_      = Vector3{1.0f, 1.0f, 1.0f};
        Vector3 emissiveColor_     = Vector3{0.0f, 0.0f, 0.0f};
        Vector3 ambientLightColor_ = Vector3{0.0f, 0.0f, 0.0f};
        float   alpha_             = 1.0f;

        Vector3 specularColor_ = Vector3{1.0f, 1.0f, 1.0f};
        float   specularPower_ = 16.0f;

        float   fogStart_ = 0.0f;
        float   fogEnd_   = 1.0f;

        int weightsPerVertex_ = 4;

        int dirtyFlags_;
    };
}

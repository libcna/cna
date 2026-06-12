// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectFog.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectMatrices.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class EffectParameter;

    /// Built-in effect that supports two-layer multitexturing.
    class DualTextureEffect : public Effect, public IEffectMatrices, public IEffectFog
    {
    public:
        explicit DualTextureEffect(GraphicsDevice& device);

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

        [[nodiscard]] float getAlphaProperty() const;
        void setAlphaProperty(float value);

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

        [[nodiscard]] Texture2D* getTexture2Property() const;
        void setTexture2Property(Texture2D* value);

        [[nodiscard]] bool getVertexColorEnabledProperty() const;
        void setVertexColorEnabledProperty(bool value);

    protected:
        void OnApply() override;

    private:
        explicit DualTextureEffect(const DualTextureEffect& cloneSource);

        void CacheEffectParameters();

        // Textures stored directly (Texture2D does not inherit from Texture in CNA)
        Texture2D* texture_  = nullptr;
        Texture2D* texture2_ = nullptr;

        EffectParameter* diffuseColorParam_  = nullptr;
        EffectParameter* fogColorParam_      = nullptr;
        EffectParameter* fogVectorParam_     = nullptr;
        EffectParameter* worldViewProjParam_ = nullptr;
        EffectParameter* shaderIndexParam_   = nullptr;

        bool    fogEnabled_         = false;
        bool    vertexColorEnabled_ = false;

        Matrix  world_      = Matrix::getIdentityProperty();
        Matrix  view_       = Matrix::getIdentityProperty();
        Matrix  projection_ = Matrix::getIdentityProperty();
        Matrix  worldView_;

        Vector3 diffuseColor_ = Vector3{1.0f, 1.0f, 1.0f};
        float   alpha_        = 1.0f;

        float   fogStart_ = 0.0f;
        float   fogEnd_   = 1.0f;

        int dirtyFlags_;
    };
}

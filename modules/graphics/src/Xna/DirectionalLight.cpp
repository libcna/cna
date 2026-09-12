// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    DirectionalLight::DirectionalLight()
        : DirectionalLight(nullptr, nullptr, nullptr, nullptr)
    {
    }

    DirectionalLight::DirectionalLight(
        EffectParameter* directionParameter,
        EffectParameter* diffuseColorParameter,
        EffectParameter* specularColorParameter,
        const DirectionalLight* cloneSource)
        : diffuseColorParameter_(diffuseColorParameter)
        , directionParameter_(directionParameter)
        , specularColorParameter_(specularColorParameter)
    {
        if (cloneSource != nullptr)
        {
            setDiffuseColorProperty(cloneSource->getDiffuseColorProperty());
            setDirectionProperty(cloneSource->getDirectionProperty());
            setSpecularColorProperty(cloneSource->getSpecularColorProperty());
            setEnabledProperty(cloneSource->getEnabledProperty());
            return;
        }

        // The Microsoft XNA 4.0 constructor explicitly assigns these defaults; FNA leaves the
        // three backing vectors zero-initialized.
        setDirectionProperty(Vector3::Down);
        setDiffuseColorProperty(Vector3::One);
        setSpecularColorProperty(Vector3::Zero);
    }

    Vector3 DirectionalLight::getDiffuseColorProperty() const { return diffuseColor_; }
    void DirectionalLight::setDiffuseColorProperty(const Vector3& v)
    {
        diffuseColor_ = v;
        if (enabled_ && diffuseColorParameter_ != nullptr)
        {
            diffuseColorParameter_->SetValue(diffuseColor_);
        }
    }

    Vector3 DirectionalLight::getDirectionProperty() const { return direction_; }
    void DirectionalLight::setDirectionProperty(const Vector3& v)
    {
        direction_ = v;
        if (directionParameter_ != nullptr)
        {
            directionParameter_->SetValue(direction_);
        }
    }

    Vector3 DirectionalLight::getSpecularColorProperty() const { return specularColor_; }
    void DirectionalLight::setSpecularColorProperty(const Vector3& v)
    {
        specularColor_ = v;
        if (enabled_ && specularColorParameter_ != nullptr)
        {
            specularColorParameter_->SetValue(specularColor_);
        }
    }

    bool DirectionalLight::getEnabledProperty() const { return enabled_; }
    void DirectionalLight::setEnabledProperty(bool v)
    {
        if (enabled_ == v) { return; }
        enabled_ = v;
        if (diffuseColorParameter_ != nullptr)
        {
            diffuseColorParameter_->SetValue(enabled_ ? diffuseColor_ : Vector3::Zero);
        }
        if (specularColorParameter_ != nullptr)
        {
            specularColorParameter_->SetValue(enabled_ ? specularColor_ : Vector3::Zero);
        }
    }
}

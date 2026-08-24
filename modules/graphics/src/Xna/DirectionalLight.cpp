// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    DirectionalLight::DirectionalLight()
        : diffuseColor_(Vector3::One)
        , direction_(Vector3::Down)
        , specularColor_(Vector3::Zero)
        , enabled_(false)
    {
        // The Microsoft XNA 4.0 constructor initializes these cached property values explicitly.
        // This intentionally differs from FNA's zero-initialized backing fields.
    }

    Vector3 DirectionalLight::getDiffuseColorProperty() const { return diffuseColor_; }
    void DirectionalLight::setDiffuseColorProperty(const Vector3& v) { diffuseColor_ = v; }

    Vector3 DirectionalLight::getDirectionProperty() const { return direction_; }
    void DirectionalLight::setDirectionProperty(const Vector3& v) { direction_ = v; }

    Vector3 DirectionalLight::getSpecularColorProperty() const { return specularColor_; }
    void DirectionalLight::setSpecularColorProperty(const Vector3& v) { specularColor_ = v; }

    bool DirectionalLight::getEnabledProperty() const { return enabled_; }
    void DirectionalLight::setEnabledProperty(bool v) { enabled_ = v; }
}

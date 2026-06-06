#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    DirectionalLight::DirectionalLight()
        : diffuseColor_(Vector3::Zero)
        , direction_(Vector3::Forward)
        , specularColor_(Vector3::Zero)
        , enabled_(false)
    {
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

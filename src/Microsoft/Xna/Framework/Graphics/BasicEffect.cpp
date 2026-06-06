#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    BasicEffect::BasicEffect(GraphicsDevice& device)
        : Effect(device)
    {
    }

    void BasicEffect::OnApply()
    {
        if (device_)
        {
            device_->SetCurrentEffect(this);
        }
    }

    Vector3 BasicEffect::getDiffuseColorProperty() const { return diffuseColor_; }
    void BasicEffect::setDiffuseColorProperty(const Vector3& v) { diffuseColor_ = v; }

    Vector3 BasicEffect::getEmissiveColorProperty() const { return emissiveColor_; }
    void BasicEffect::setEmissiveColorProperty(const Vector3& v) { emissiveColor_ = v; }

    Vector3 BasicEffect::getSpecularColorProperty() const { return specularColor_; }
    void BasicEffect::setSpecularColorProperty(const Vector3& v) { specularColor_ = v; }

    float BasicEffect::getSpecularPowerProperty() const { return specularPower_; }
    void BasicEffect::setSpecularPowerProperty(float v) { specularPower_ = v; }

    Vector3 BasicEffect::getAmbientLightColorProperty() const { return ambientLightColor_; }
    void BasicEffect::setAmbientLightColorProperty(const Vector3& v) { ambientLightColor_ = v; }

    float BasicEffect::getAlphaProperty() const { return alpha_; }
    void BasicEffect::setAlphaProperty(float v) { alpha_ = v; }

    bool BasicEffect::getLightingEnabledProperty() const { return lightingEnabled_; }
    void BasicEffect::setLightingEnabledProperty(bool v) { lightingEnabled_ = v; }

    bool BasicEffect::getPreferPerPixelLightingProperty() const { return preferPerPixelLighting_; }
    void BasicEffect::setPreferPerPixelLightingProperty(bool v) { preferPerPixelLighting_ = v; }

    bool BasicEffect::getTextureEnabledProperty() const { return textureEnabled_; }
    void BasicEffect::setTextureEnabledProperty(bool v) { textureEnabled_ = v; }

    Texture2D* BasicEffect::getTextureProperty() const { return texture_; }
    void BasicEffect::setTextureProperty(Texture2D* v) { texture_ = v; }

    bool BasicEffect::getFogEnabledProperty() const { return fogEnabled_; }
    void BasicEffect::setFogEnabledProperty(bool v) { fogEnabled_ = v; }

    Vector3 BasicEffect::getFogColorProperty() const { return fogColor_; }
    void BasicEffect::setFogColorProperty(const Vector3& v) { fogColor_ = v; }

    float BasicEffect::getFogStartProperty() const { return fogStart_; }
    void BasicEffect::setFogStartProperty(float v) { fogStart_ = v; }

    float BasicEffect::getFogEndProperty() const { return fogEnd_; }
    void BasicEffect::setFogEndProperty(float v) { fogEnd_ = v; }

    void BasicEffect::EnableDefaultLighting()
    {
        lightingEnabled_ = true;
        ambientLightColor_ = Vector3{0.05333332f, 0.09882354f, 0.1819608f};

        DirectionalLight0.setDiffuseColorProperty(Vector3{1.0f, 0.9607844f, 0.8078432f});
        DirectionalLight0.setDirectionProperty(Vector3{-0.5265408f, -0.5735765f, -0.6275069f});
        DirectionalLight0.setSpecularColorProperty(Vector3{1.0f, 0.9607844f, 0.8078432f});
        DirectionalLight0.setEnabledProperty(true);

        DirectionalLight1.setDiffuseColorProperty(Vector3{0.9647059f, 0.7607844f, 0.4078432f});
        DirectionalLight1.setDirectionProperty(Vector3{0.7198464f, 0.3420201f, 0.6040227f});
        DirectionalLight1.setSpecularColorProperty(Vector3::Zero);
        DirectionalLight1.setEnabledProperty(true);

        DirectionalLight2.setDiffuseColorProperty(Vector3{0.3231373f, 0.3607843f, 0.3937255f});
        DirectionalLight2.setDirectionProperty(Vector3{0.4545195f, -0.7660444f, 0.4545195f});
        DirectionalLight2.setSpecularColorProperty(Vector3::Zero);
        DirectionalLight2.setEnabledProperty(true);
    }
}

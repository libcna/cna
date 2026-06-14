// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    BasicEffect::BasicEffect(GraphicsDevice& device)
        : Effect(device)
    {
    }

    void BasicEffect::OnApply()
    {
        // SetCurrentEffect is now called by Effect::Apply() after OnApply() returns.
    }

    void BasicEffect::FillGpuDrawParams(CNA::Internal::Backends::GpuDrawParams& p) const
    {
        using namespace CNA::Internal::Backends;

        p.textureEnabled     = textureEnabled_;
        p.vertexColorEnabled = VertexColorEnabled;
        p.lightingEnabled    = lightingEnabled_;

        if (p.textureEnabled)
        {
            if (texture_) p.texture0 = &texture_->GetBackend();
        }

        p.diffuseColor[0] = diffuseColor_.X * alpha_;
        p.diffuseColor[1] = diffuseColor_.Y * alpha_;
        p.diffuseColor[2] = diffuseColor_.Z * alpha_;
        p.diffuseColor[3] = alpha_;

        p.ambientColor[0] = ambientLightColor_.X;
        p.ambientColor[1] = ambientLightColor_.Y;
        p.ambientColor[2] = ambientLightColor_.Z;

        const Vector3 ld  = DirectionalLight0.getDiffuseColorProperty();
        const Vector3 dir = DirectionalLight0.getDirectionProperty();
        p.light0Dir[0]     = dir.X; p.light0Dir[1]     = dir.Y; p.light0Dir[2]     = dir.Z;
        p.light0Diffuse[0] = ld.X;  p.light0Diffuse[1] = ld.Y;  p.light0Diffuse[2] = ld.Z;

        World.ToColumnMajor(p.worldColMajor);
    }

    // IEffectLights
    Vector3 BasicEffect::getAmbientLightColorProperty() const { return ambientLightColor_; }
    void BasicEffect::setAmbientLightColorProperty(const Vector3& v) { ambientLightColor_ = v; }
    bool BasicEffect::getLightingEnabledProperty() const { return lightingEnabled_; }
    void BasicEffect::setLightingEnabledProperty(bool v) { lightingEnabled_ = v; }
    DirectionalLight& BasicEffect::getDirectionalLight0Property() { return DirectionalLight0; }
    DirectionalLight& BasicEffect::getDirectionalLight1Property() { return DirectionalLight1; }
    DirectionalLight& BasicEffect::getDirectionalLight2Property() { return DirectionalLight2; }

    bool BasicEffect::getPreferPerPixelLightingProperty() const { return preferPerPixelLighting_; }
    void BasicEffect::setPreferPerPixelLightingProperty(bool v) { preferPerPixelLighting_ = v; }

    Vector3 BasicEffect::getDiffuseColorProperty() const { return diffuseColor_; }
    void BasicEffect::setDiffuseColorProperty(const Vector3& v) { diffuseColor_ = v; }
    Vector3 BasicEffect::getEmissiveColorProperty() const { return emissiveColor_; }
    void BasicEffect::setEmissiveColorProperty(const Vector3& v) { emissiveColor_ = v; }
    Vector3 BasicEffect::getSpecularColorProperty() const { return specularColor_; }
    void BasicEffect::setSpecularColorProperty(const Vector3& v) { specularColor_ = v; }
    float BasicEffect::getSpecularPowerProperty() const { return specularPower_; }
    void BasicEffect::setSpecularPowerProperty(float v) { specularPower_ = v; }
    float BasicEffect::getAlphaProperty() const { return alpha_; }
    void BasicEffect::setAlphaProperty(float v) { alpha_ = v; }

    bool BasicEffect::getTextureEnabledProperty() const { return textureEnabled_; }
    void BasicEffect::setTextureEnabledProperty(bool v) { textureEnabled_ = v; }
    Texture2D* BasicEffect::getTextureProperty() const { return texture_; }
    void BasicEffect::setTextureProperty(Texture2D* v) { texture_ = v; }

    // IEffectFog
    Vector3 BasicEffect::getFogColorProperty() const { return fogColor_; }
    void BasicEffect::setFogColorProperty(const Vector3& v) { fogColor_ = v; }
    bool BasicEffect::getFogEnabledProperty() const { return fogEnabled_; }
    void BasicEffect::setFogEnabledProperty(bool v) { fogEnabled_ = v; }
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

    const std::string& BasicEffect::GetTypeName() const
    {
        static const std::string name = "Microsoft.Xna.Framework.Graphics.BasicEffect";
        return name;
    }
}

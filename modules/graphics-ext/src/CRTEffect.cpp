// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/CRTEffect.hpp"

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "PostProcessShaderPackages.hpp"
#include "shaders/post_process/PostProcessShaderPackage.generated.hpp"

#include <algorithm>

using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::ShaderEffect;

namespace CNA::Graphics {

    CRTEffect::CRTEffect(GraphicsDevice& device)
        : ShaderEffect(
            device, detail::CreateCrtShaderPackage(),
            std::string(detail::PostProcessGenerated::kFullscreenEsVertexSource),
            std::string(detail::PostProcessGenerated::kCrtEsFragmentSource))
    {
    }

    float CRTEffect::getScanlineIntensity() const { return scanlineIntensity_; }
    void CRTEffect::setScanlineIntensity(float value) { scanlineIntensity_ = std::clamp(value, 0.0f, 1.0f); }

    float CRTEffect::getCurvature() const { return curvature_; }
    void CRTEffect::setCurvature(float value) { curvature_ = std::clamp(value, 0.0f, 1.0f); }

    float CRTEffect::getVignetteIntensity() const { return vignetteIntensity_; }
    void CRTEffect::setVignetteIntensity(float value) { vignetteIntensity_ = std::clamp(value, 0.0f, 1.0f); }

    float CRTEffect::getMaskIntensity() const { return maskIntensity_; }
    void CRTEffect::setMaskIntensity(float value) { maskIntensity_ = std::clamp(value, 0.0f, 1.0f); }

    CRTMaskType CRTEffect::getMaskType() const { return maskType_; }
    void CRTEffect::setMaskType(CRTMaskType type) { maskType_ = type; }

    const std::string& CRTEffect::GetTypeName() const
    {
        static const std::string name = "CNA.Graphics.CRTEffect";
        return name;
    }

    void CRTEffect::OnApply()
    {
        ShaderEffect::OnApply();
        SetUniformVec4("uCrtParams", scanlineIntensity_, curvature_,
                       vignetteIntensity_, maskIntensity_);
        SetUniformInt("uMaskType", static_cast<int>(maskType_));
    }

    Effect* CRTEffect::Clone()
    {
        auto* clone = new CRTEffect(*device_);
        clone->setScanlineIntensity(scanlineIntensity_);
        clone->setCurvature(curvature_);
        clone->setVignetteIntensity(vignetteIntensity_);
        clone->setMaskIntensity(maskIntensity_);
        clone->setMaskType(maskType_);
        return clone;
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

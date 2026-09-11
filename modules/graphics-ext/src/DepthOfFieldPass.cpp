// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/DepthOfFieldPass.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"
#include "CNA/GraphicsCapability.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "PostProcessShaderPackages.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;

    DepthOfFieldPass::DepthOfFieldPass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
        , packedDepth_(DepthNormalPrepass::usesPackedDepthEXT(device))
    {
        const ShaderPackageEXT package = detail::CreateDepthOfFieldShaderPackage();
        if (package.selectFor(device).isUsable())
            effect_ = std::make_unique<ShaderEffect>(device, package);
        bool logged = false;
        detail::reportShaderCompileFailure(device, "DepthOfFieldPass", effect_.get(), logged);
    }

    DepthOfFieldPass::~DepthOfFieldPass() = default;

    float DepthOfFieldPass::circleOfConfusionMillimetres(const float depth,
                                                        const float focusDistance,
                                                        const float focalLength,
                                                        const float fNumber)
    {
        if (depth <= 0.0f || focusDistance <= 0.0f || focalLength <= 0.0f || fNumber <= 0.0f)
            return 0.0f;

        const float focusMm = focusDistance * 1000.0f;
        const float depthMm = depth * 1000.0f;
        // A lens cannot focus at or inside its own focal length, and the formula divides by exactly
        // that difference. Answering zero is the honest reading: such a configuration has no
        // circle of confusion because it has no image.
        if (focusMm <= focalLength)
            return 0.0f;

        return (focalLength * focalLength / (fNumber * (focusMm - focalLength)))
             * std::fabs(depthMm - focusMm) / depthMm;
    }

    void DepthOfFieldPass::apply(const PostProcessContext& context)
    {
        const bool haveInputs = context.sourceDepth != nullptr && context.farPlane > 0.0f;
        if (effect_ == nullptr || !effect_->IsEffectValid() || !haveInputs)
        {
            fullscreen_->draw(context.source, context.destination, nullptr,
                              context.width, context.height);
            return;
        }

        const RenderPipelineSettings* settings = context.settings;
        const float focus  = settings != nullptr ? settings->getDOFFocusDistance() : focusDistance_;
        const float length = settings != nullptr ? settings->getDOFFocalLength()   : focalLength_;
        const float number = settings != nullptr ? settings->getDOFFNumber()       : fNumber_;
        const float radius = settings != nullptr ? settings->getDOFMaxRadius()     : maxRadius_;

        effect_->Apply();
        effect_->SetUniformInt("uDepthSampler", 1);
        effect_->SetTexture(1, *context.sourceDepth);
        const std::array dofScalars{
            context.farPlane, focus, length, number, radius, packedDepth_ ? 1.0f : 0.0f};
        effect_->SetUniformFloatArray("uDofScalars", dofScalars.data(),
                                      static_cast<int>(dofScalars.size()));

        fullscreen_->draw(context.source, context.destination, effect_.get(),
                          context.width, context.height);
    }

    const std::string& DepthOfFieldPass::getName() const
    {
        static const std::string name = "DepthOfField";
        return name;
    }

    bool DepthOfFieldPass::isSupported(GraphicsDevice& device) const
    {
        return device.SupportsCapability(CNA::GraphicsCapability::CustomEffects)
            && effect_ && effect_->IsEffectValid();
    }

    float DepthOfFieldPass::getFocusDistance() const { return focusDistance_; }
    void  DepthOfFieldPass::setFocusDistance(const float value)
    {
        if (value > 0.0f) focusDistance_ = value;
    }

    float DepthOfFieldPass::getFocalLength() const { return focalLength_; }
    void  DepthOfFieldPass::setFocalLength(const float value)
    {
        if (value > 0.0f) focalLength_ = value;
    }

    float DepthOfFieldPass::getFNumber() const { return fNumber_; }
    void  DepthOfFieldPass::setFNumber(const float value)
    {
        if (value > 0.0f) fNumber_ = value;
    }

    float DepthOfFieldPass::getMaxRadius() const { return maxRadius_; }
    void  DepthOfFieldPass::setMaxRadius(const float value)
    {
        maxRadius_ = std::clamp(value, 0.0f, 0.25f);
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

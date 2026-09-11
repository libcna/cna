// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/MotionBlurPass.hpp"
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
#include <string>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;

    MotionBlurPass::MotionBlurPass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
        , packedDepth_(DepthNormalPrepass::usesPackedDepthEXT(device))
    {
        const ShaderPackageEXT package = detail::CreateMotionBlurShaderPackage();
        if (package.selectFor(device).isUsable())
            effect_ = std::make_unique<ShaderEffect>(device, package);
        bool logged = false;
        detail::reportShaderCompileFailure(device, "MotionBlurPass", effect_.get(), logged);
    }

    MotionBlurPass::~MotionBlurPass() = default;

    void MotionBlurPass::apply(const PostProcessContext& context)
    {
        const RenderPipelineSettings* settings = context.settings;
        const float strength = settings != nullptr ? settings->getMotionBlurStrength() : strength_;
        const float maxDistance =
            settings != nullptr ? settings->getMotionBlurMaxDistance() : maxDistance_;

        // Four things have to be true, and the third is the one that is easy to forget: without a
        // previous frame there is no velocity, only the identity matrix pretending to be one.
        const bool ready = effect_ != nullptr && effect_->IsEffectValid()
                        && context.sourceDepth != nullptr
                        && context.hasPreviousFrame
                        && context.farPlane > 0.0f;
        // MOD-2033: the velocity image is a per-pixel *upgrade*, not a second mode. Everything
        // above still has to hold, because a pixel the velocity image does not cover -- the sky,
        // any object drawn without a previous world -- still gets the camera reconstruction.
        const bool hasVelocity = context.sourceVelocity != nullptr;
        if (!ready || strength <= 0.0f)
        {
            fullscreen_->draw(context.source, context.destination, nullptr,
                              context.width, context.height);
            return;
        }

        effect_->Apply();
        effect_->SetUniformInt("uDepthSampler", 1);
        effect_->SetTexture(1, *context.sourceDepth);
        effect_->SetUniformFloat("uHasVelocity", hasVelocity ? 1.0f : 0.0f);
        if (hasVelocity)
        {
            effect_->SetUniformInt("uVelocitySampler", 2);
            effect_->SetTexture(2, *context.sourceVelocity);
        }
        std::array<float, 48> motionMatrices{};
        context.inverseProjection.ToColumnMajor(motionMatrices.data());
        context.inverseView.ToColumnMajor(motionMatrices.data() + 16);
        context.previousViewProjection.ToColumnMajor(motionMatrices.data() + 32);
        const std::array motionScalars{
            hasVelocity ? 1.0f : 0.0f, context.farPlane, strength, maxDistance,
            static_cast<float>(kSampleCount), packedDepth_ ? 1.0f : 0.0f};
        effect_->SetUniformMat4Array("uMotionMatrices", motionMatrices.data(), 3);
        effect_->SetUniformFloatArray("uMotionScalars", motionScalars.data(),
                                      static_cast<int>(motionScalars.size()));

        fullscreen_->draw(context.source, context.destination, effect_.get(),
                          context.width, context.height);
    }

    const std::string& MotionBlurPass::getName() const
    {
        static const std::string name = "MotionBlur";
        return name;
    }

    bool MotionBlurPass::isSupported(GraphicsDevice& device) const
    {
        return device.SupportsCapability(CNA::GraphicsCapability::CustomEffects)
            && effect_ && effect_->IsEffectValid();
    }

    float MotionBlurPass::getStrength() const { return strength_; }
    void  MotionBlurPass::setStrength(const float value)
    {
        strength_ = std::clamp(value, 0.0f, 1.0f);
    }

    float MotionBlurPass::getMaxDistance() const { return maxDistance_; }
    void  MotionBlurPass::setMaxDistance(const float value)
    {
        maxDistance_ = std::clamp(value, 0.0f, 0.25f);
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

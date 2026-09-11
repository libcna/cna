// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/HeightFogPass.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"
#include "CNA/GraphicsCapability.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "PostProcessShaderPackages.hpp"

#include <array>
#include <cmath>
#include <string>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;

    HeightFogPass::HeightFogPass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
        , packedDepth_(DepthNormalPrepass::usesPackedDepthEXT(device))
    {
        const ShaderPackageEXT package = detail::CreateHeightFogShaderPackage();
        if (package.selectFor(device).isUsable())
            effect_ = std::make_unique<ShaderEffect>(device, package);
        bool logged = false;
        detail::reportShaderCompileFailure(device, "HeightFogPass", effect_.get(), logged);
    }

    HeightFogPass::~HeightFogPass() = default;

    float HeightFogPass::opticalDepth(const float cameraHeight, const float rayHeightStep,
                                      const float distance, const float density,
                                      const float falloff, const float baseHeight)
    {
        if (density <= 0.0f || distance <= 0.0f || falloff <= 0.0f) return 0.0f;

        const float atCamera = density * std::exp(-falloff * (cameraHeight - baseHeight));
        const float climb    = falloff * rayHeightStep;
        // A level look is not the general case with a small number in it: the general case divides
        // by the climb, so nudging it away from zero would make a level view's fog depend on the
        // size of the nudge.
        if (std::fabs(climb) < 1e-5f)
            return std::max(atCamera * distance, 0.0f);
        return std::max(atCamera * (1.0f - std::exp(-climb * distance)) / climb, 0.0f);
    }

    void HeightFogPass::apply(const PostProcessContext& context)
    {
        const RenderPipelineSettings* settings = context.settings;
        const float density = settings != nullptr ? settings->getHeightFogDensity() : density_;
        const float falloff = settings != nullptr ? settings->getHeightFogFalloff() : falloff_;
        const float base    = settings != nullptr ? settings->getHeightFogBaseHeight() : baseHeight_;

        const bool ready = effect_ != nullptr && effect_->IsEffectValid()
                        && context.sourceDepth != nullptr && context.farPlane > 0.0f;
        if (!ready || density <= 0.0f || falloff <= 0.0f)
        {
            fullscreen_->draw(context.source, context.destination, nullptr,
                              context.width, context.height);
            return;
        }

        effect_->Apply();
        effect_->SetUniformInt("uDepthSampler", 1);
        effect_->SetTexture(1, *context.sourceDepth);
        std::array<float, 32> fogMatrices{};
        context.inverseProjection.ToColumnMajor(fogMatrices.data());
        context.inverseView.ToColumnMajor(fogMatrices.data() + 16);
        const std::array fogColor{color_.X, color_.Y, color_.Z};
        const std::array fogScalars{
            context.farPlane, density, falloff, base, packedDepth_ ? 1.0f : 0.0f};
        effect_->SetUniformMat4Array("uFogMatrices", fogMatrices.data(), 2);
        effect_->SetUniformVec3Array("uFogVectors", fogColor.data(), 1);
        effect_->SetUniformFloatArray("uFogScalars", fogScalars.data(),
                                      static_cast<int>(fogScalars.size()));

        fullscreen_->draw(context.source, context.destination, effect_.get(),
                          context.width, context.height);
    }

    const std::string& HeightFogPass::getName() const
    {
        static const std::string name = "HeightFog";
        return name;
    }

    bool HeightFogPass::isSupported(GraphicsDevice& device) const
    {
        return device.SupportsCapability(CNA::GraphicsCapability::CustomEffects)
            && effect_ && effect_->IsEffectValid();
    }

    Vector3 HeightFogPass::getColor() const { return color_; }
    void    HeightFogPass::setColor(const Vector3& value) { color_ = value; }

    float HeightFogPass::getDensity() const { return density_; }
    void  HeightFogPass::setDensity(const float value) { if (value >= 0.0f) density_ = value; }

    float HeightFogPass::getFalloff() const { return falloff_; }
    void  HeightFogPass::setFalloff(const float value) { if (value > 0.0f) falloff_ = value; }

    float HeightFogPass::getBaseHeight() const { return baseHeight_; }
    void  HeightFogPass::setBaseHeight(const float value) { baseHeight_ = value; }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

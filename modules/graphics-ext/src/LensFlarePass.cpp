// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/LensFlarePass.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "PostProcessShaderPackages.hpp"

#include <algorithm>
#include <string>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;

    // ── Lens flare ───────────────────────────────────────────────────────────

    LensFlarePass::LensFlarePass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
    {
        const ShaderPackageEXT package = detail::CreateLensFlareShaderPackage();
        if (package.selectFor(device).isUsable())
            effect_ = std::make_unique<ShaderEffect>(device, package);
        bool logged = false;
        detail::reportShaderCompileFailure(device, "LensFlarePass", effect_.get(), logged);
    }

    LensFlarePass::~LensFlarePass() = default;

    void LensFlarePass::apply(const PostProcessContext& context)
    {
        const RenderPipelineSettings* settings = context.settings;
        const float threshold = settings != nullptr ? settings->getLensFlareThreshold() : threshold_;
        const float intensity = settings != nullptr ? settings->getLensFlareIntensity() : intensity_;
        const float dispersal = settings != nullptr ? settings->getLensFlareDispersal() : dispersal_;

        if (effect_ == nullptr || !effect_->IsEffectValid() || intensity <= 0.0f)
        {
            fullscreen_->draw(context.source, context.destination, nullptr,
                              context.width, context.height);
            return;
        }

        effect_->Apply();
        effect_->SetUniformVec4("uLensFlareParams", threshold, intensity, dispersal,
                                static_cast<float>(kGhostCount));
        fullscreen_->draw(context.source, context.destination, effect_.get(),
                          context.width, context.height);
    }

    const std::string& LensFlarePass::getName() const
    {
        static const std::string name = "LensFlare";
        return name;
    }

    bool LensFlarePass::isSupported(GraphicsDevice& device) const
    {
        return device.SupportsCapability(CNA::GraphicsCapability::CustomEffects)
            && effect_ && effect_->IsEffectValid();
    }

    float LensFlarePass::getThreshold() const { return threshold_; }
    void  LensFlarePass::setThreshold(const float value)
    {
        if (value >= 0.0f) threshold_ = value;
    }

    float LensFlarePass::getIntensity() const { return intensity_; }
    void  LensFlarePass::setIntensity(const float value)
    {
        if (value >= 0.0f) intensity_ = value;
    }

    float LensFlarePass::getDispersal() const { return dispersal_; }
    void  LensFlarePass::setDispersal(const float value)
    {
        dispersal_ = std::clamp(value, 0.0f, 1.0f);
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

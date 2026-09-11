// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/FxaaPass.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "PostProcessShaderPackages.hpp"
#include "shaders/post_process/PostProcessShaderPackage.generated.hpp"

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;

    std::string FxaaPass::getFragmentGlsl()
    {
        return std::string(detail::PostProcessGenerated::kFxaaEsFragmentSource);
    }

    FxaaPass::FxaaPass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
    {
        const ShaderPackageEXT package = detail::CreateFxaaShaderPackage();
        if (package.selectFor(device).isUsable())
            effect_ = std::make_unique<ShaderEffect>(device, package);
        // plans/plan_modern.md MOD-219: a failed compile makes this pass copy its input through, which is
        // correct and completely silent. This names the pass and prints the compiler's log once.
        bool logged = false;
        detail::reportShaderCompileFailure(device, "FxaaPass", effect_.get(), logged);
    }

    FxaaPass::~FxaaPass() = default;

    void FxaaPass::apply(const PostProcessContext& context)
    {
        if (effect_ == nullptr || !effect_->IsEffectValid())
        {
            fullscreen_->draw(context.source, context.destination, nullptr,
                              context.width, context.height);
            return;
        }

        effect_->Apply();
        effect_->SetUniformVec2("uTexelSize",
                                context.width > 0 ? 1.0f / static_cast<float>(context.width) : 0.0f,
                                context.height > 0 ? 1.0f / static_cast<float>(context.height) : 0.0f);
        // The settings bag wins where one is supplied, matching every other pass: a pipeline that
        // applied a quality preset must not be overruled by a pass-local default nobody set.
        const float threshold = context.settings != nullptr
                                    ? context.settings->getFXAAEdgeThresholdEXT()
                                    : edgeThreshold_;
        effect_->SetUniformFloat("uEdgeThreshold", threshold);

        fullscreen_->draw(context.source, context.destination, effect_.get(),
                          context.width, context.height);
    }

    float FxaaPass::edgeThresholdForQuality(const RenderQuality quality)
    {
        switch (quality)
        {
        case RenderQuality::Low:    return 0.250f;
        case RenderQuality::High:   return 0.0625f;
        case RenderQuality::Ultra:  return 0.0312f;
        case RenderQuality::Medium:
        default:                    return 0.125f;
        }
    }

    const std::string& FxaaPass::getName() const
    {
        static const std::string name = "FXAA";
        return name;
    }

    bool FxaaPass::isSupported(GraphicsDevice& device) const
    {
        return device.SupportsCapability(CNA::GraphicsCapability::CustomEffects)
            && effect_ != nullptr
            && effect_->IsEffectValid();
    }

    float FxaaPass::getEdgeThreshold() const            { return edgeThreshold_; }
    void  FxaaPass::setEdgeThreshold(const float value) { edgeThreshold_ = value; }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

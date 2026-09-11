// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/LightShaftPass.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "PostProcessShaderPackages.hpp"

#include <algorithm>
#include <string>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;

    LightShaftPass::LightShaftPass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
    {
        const ShaderPackageEXT package = detail::CreateLightShaftShaderPackage();
        if (package.selectFor(device).isUsable())
            effect_ = std::make_unique<ShaderEffect>(device, package);
        bool logged = false;
        detail::reportShaderCompileFailure(device, "LightShaftPass", effect_.get(), logged);
    }

    LightShaftPass::~LightShaftPass() = default;

    void LightShaftPass::apply(const PostProcessContext& context)
    {
        const RenderPipelineSettings* settings = context.settings;
        const float threshold = settings != nullptr ? settings->getLightShaftThreshold() : threshold_;
        const float intensity = settings != nullptr ? settings->getLightShaftIntensity() : intensity_;
        const float decay     = settings != nullptr ? settings->getLightShaftDecay()     : decay_;

        if (effect_ == nullptr || !effect_->IsEffectValid() || intensity <= 0.0f)
        {
            fullscreen_->draw(context.source, context.destination, nullptr,
                              context.width, context.height);
            return;
        }

        effect_->Apply();
        effect_->SetUniformVec4("uLightShaftParams", lightScreenPosition_.X,
                                lightScreenPosition_.Y, threshold, intensity);
        effect_->SetUniformFloat("uDecay", decay);
        const CNA::ShaderLanguageEXT language = effect_->GetSelectedShaderLanguageEXT();
        if (language == CNA::ShaderLanguageEXT::GlslEs
            || language == CNA::ShaderLanguageEXT::GlslDesktop)
        {
            // EasyGL flips a render target's SpriteBatch UVs into GL storage coordinates. This
            // pass also walks towards an absolute screen-space point, which must follow that same
            // transform; ordinary uploaded textures and top-left APIs need no correction.
            const float flipV = dynamic_cast<RenderTarget2D*>(context.source) != nullptr ? 1.0f : 0.0f;
            effect_->SetUniformVec4("uRtFlipV", flipV, 0.0f, 0.0f, 0.0f);
        }

        fullscreen_->draw(context.source, context.destination, effect_.get(),
                          context.width, context.height);
    }

    const std::string& LightShaftPass::getName() const
    {
        static const std::string name = "LightShafts";
        return name;
    }

    bool LightShaftPass::isSupported(GraphicsDevice& device) const
    {
        return device.SupportsCapability(CNA::GraphicsCapability::CustomEffects)
            && effect_ && effect_->IsEffectValid();
    }

    Vector2 LightShaftPass::getLightScreenPosition() const { return lightScreenPosition_; }
    void    LightShaftPass::setLightScreenPosition(const Vector2& value)
    {
        lightScreenPosition_ = value;
    }

    float LightShaftPass::getThreshold() const { return threshold_; }
    void  LightShaftPass::setThreshold(const float value) { if (value >= 0.0f) threshold_ = value; }

    float LightShaftPass::getIntensity() const { return intensity_; }
    void  LightShaftPass::setIntensity(const float value) { if (value >= 0.0f) intensity_ = value; }

    float LightShaftPass::getDecay() const { return decay_; }
    void  LightShaftPass::setDecay(const float value) { decay_ = std::clamp(value, 0.0f, 1.0f); }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

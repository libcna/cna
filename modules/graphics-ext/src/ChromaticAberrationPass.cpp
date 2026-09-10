// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/ChromaticAberrationPass.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "PostProcessShaderPackages.hpp"

#include <algorithm>
#include <string>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;

    // ── Chromatic aberration ─────────────────────────────────────────────────

    ChromaticAberrationPass::ChromaticAberrationPass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
    {
        const ShaderPackageEXT package = detail::CreateChromaticAberrationShaderPackage();
        if (package.selectFor(device).isUsable())
            effect_ = std::make_unique<ShaderEffect>(device, package);
        bool logged = false;
        detail::reportShaderCompileFailure(device, "ChromaticAberrationPass", effect_.get(), logged);
    }

    ChromaticAberrationPass::~ChromaticAberrationPass() = default;

    void ChromaticAberrationPass::apply(const PostProcessContext& context)
    {
        const RenderPipelineSettings* settings = context.settings;
        const float strength =
            settings != nullptr ? settings->getChromaticAberrationStrength() : strength_;

        if (effect_ == nullptr || !effect_->IsEffectValid() || strength <= 0.0f)
        {
            fullscreen_->draw(context.source, context.destination, nullptr,
                              context.width, context.height);
            return;
        }

        effect_->Apply();
        effect_->SetUniformFloat("uStrength", strength);
        fullscreen_->draw(context.source, context.destination, effect_.get(),
                          context.width, context.height);
    }

    const std::string& ChromaticAberrationPass::getName() const
    {
        static const std::string name = "ChromaticAberration";
        return name;
    }

    bool ChromaticAberrationPass::isSupported(GraphicsDevice& device) const
    {
        return device.SupportsCapability(CNA::GraphicsCapability::CustomEffects)
            && effect_ && effect_->IsEffectValid();
    }

    float ChromaticAberrationPass::getStrength() const { return strength_; }
    void  ChromaticAberrationPass::setStrength(const float value)
    {
        strength_ = std::clamp(value, 0.0f, 0.1f);
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

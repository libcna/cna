// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/FilmGrainPass.hpp"
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

    // ── Film grain ───────────────────────────────────────────────────────────

    FilmGrainPass::FilmGrainPass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
    {
        const ShaderPackageEXT package = detail::CreateFilmGrainShaderPackage();
        if (package.selectFor(device).isUsable())
            effect_ = std::make_unique<ShaderEffect>(device, package);
        bool logged = false;
        detail::reportShaderCompileFailure(device, "FilmGrainPass", effect_.get(), logged);
    }

    FilmGrainPass::~FilmGrainPass() = default;

    void FilmGrainPass::apply(const PostProcessContext& context)
    {
        const RenderPipelineSettings* settings = context.settings;
        const float intensity = settings != nullptr ? settings->getFilmGrainIntensity() : intensity_;

        if (effect_ == nullptr || !effect_->IsEffectValid() || intensity <= 0.0f)
        {
            fullscreen_->draw(context.source, context.destination, nullptr,
                              context.width, context.height);
            return;
        }

        effect_->Apply();
        effect_->SetUniformVec4("uGrainParams", static_cast<float>(context.width),
                                static_cast<float>(context.height), intensity,
                                context.elapsedSeconds);
        fullscreen_->draw(context.source, context.destination, effect_.get(),
                          context.width, context.height);
    }

    const std::string& FilmGrainPass::getName() const
    {
        static const std::string name = "FilmGrain";
        return name;
    }

    bool FilmGrainPass::isSupported(GraphicsDevice& device) const
    {
        return device.SupportsCapability(CNA::GraphicsCapability::CustomEffects)
            && effect_ && effect_->IsEffectValid();
    }

    float FilmGrainPass::getIntensity() const { return intensity_; }
    void  FilmGrainPass::setIntensity(const float value)
    {
        intensity_ = std::clamp(value, 0.0f, 1.0f);
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

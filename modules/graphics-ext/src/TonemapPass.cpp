// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/TonemapPass.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "PostProcessShaderPackages.hpp"

#include <algorithm>
#include <cmath>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;

    namespace {

        float ReinhardChannel(const float c) { return c / (1.0f + c); }

        float FilmicChannel(const float c)
        {
            const float x = std::max(0.0f, c - 0.004f);
            return (x * (6.2f * x + 0.5f)) / (x * (6.2f * x + 1.7f) + 0.06f);
        }

        float AcesChannel(const float c)
        {
            constexpr float a = 2.51f, b = 0.03f, cc = 2.43f, d = 0.59f, e = 0.14f;
            return std::clamp((c * (a * c + b)) / (c * (cc * c + d) + e), 0.0f, 1.0f);
        }

        float Uncharted2Curve(const float x)
        {
            constexpr float A = 0.15f, B = 0.50f, C = 0.10f, D = 0.20f, E = 0.02f, F = 0.30f;
            return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
        }

    } // namespace

    TonemapPass::TonemapPass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
    {
        const ShaderPackageEXT package = detail::CreateTonemapShaderPackage();
        if (package.selectFor(device).isUsable())
            effect_ = std::make_unique<ShaderEffect>(device, package);
        // plans/plan_modern.md MOD-219: a failed compile makes this pass copy its input through, which is
        // correct and completely silent. This names the pass and prints the compiler's log once.
        bool logged = false;
        detail::reportShaderCompileFailure(device, "TonemapPass", effect_.get(), logged);
    }

    TonemapPass::~TonemapPass() = default;

    float TonemapPass::tonemapChannel(const TonemappingMode mode, const float value,
                                      const float exposure, const float gamma)
    {
        float color = value * exposure;

        switch (mode)
        {
        case TonemappingMode::None:                                     break;
        case TonemappingMode::Reinhard:   color = ReinhardChannel(color); break;
        case TonemappingMode::Filmic:     color = FilmicChannel(color);   break;
        case TonemappingMode::Aces:       color = AcesChannel(color);     break;
        case TonemappingMode::Uncharted2:
            color = Uncharted2Curve(color) / Uncharted2Curve(11.2f);
            break;
        }

        color = std::clamp(color, 0.0f, 1.0f);

        if (mode != TonemappingMode::Filmic && gamma > 0.0f)
            color = std::pow(color, 1.0f / gamma);

        return color;
    }

    void TonemapPass::apply(const PostProcessContext& context)
    {
        TonemappingMode mode     = mode_;
        float           exposure = exposure_;
        float           gamma    = gamma_;
        if (context.settings != nullptr)
        {
            mode     = context.settings->getTonemappingMode();
            exposure = context.settings->getExposure();
            gamma    = context.settings->getGamma();
        }

        if (effect_ == nullptr || !effect_->IsEffectValid())
        {
            // Documented fallback: the frame still reaches the destination, untonemapped, rather
            // than the pipeline stopping because one renderer could not compile a shader.
            fullscreen_->draw(context.source, context.destination, nullptr,
                              context.width, context.height);
            return;
        }

        effect_->Apply();
        effect_->SetUniformVec4("uTonemapParams", static_cast<float>(mode), exposure,
                                gamma > 0.0f ? 1.0f / gamma : 1.0f,
                                deband_ ? debandStrength_ / 255.0f : 0.0f);

        fullscreen_->draw(context.source, context.destination, effect_.get(),
                          context.width, context.height);
    }

    bool TonemapPass::isDebandEnabled() const { return deband_; }

    void TonemapPass::setDebandEnabled(const bool value) { deband_ = value; }

    float TonemapPass::getDebandStrength() const { return debandStrength_; }

    void TonemapPass::setDebandStrength(const float value)
    {
        debandStrength_ = std::clamp(value, 0.0f, 4.0f);
    }

    const std::string& TonemapPass::getName() const
    {
        static const std::string name = "Tonemap";
        return name;
    }

    bool TonemapPass::isSupported(GraphicsDevice& device) const
    {
        return device.SupportsCapability(CNA::GraphicsCapability::CustomEffects)
            && effect_ != nullptr
            && effect_->IsEffectValid();
    }

    TonemappingMode TonemapPass::getMode() const          { return mode_; }
    void            TonemapPass::setMode(TonemappingMode m) { mode_ = m; }

    float TonemapPass::getExposure() const           { return exposure_; }
    void  TonemapPass::setExposure(const float value) { exposure_ = value; }

    float TonemapPass::getGamma() const            { return gamma_; }
    void  TonemapPass::setGamma(const float value) { gamma_ = value; }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

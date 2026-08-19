// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/FilmGrainPass.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "LensPassVertexSource.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"

#include <algorithm>
#include <string>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;

    namespace {

        constexpr const char* kVertexSource = detail::kLensVertexSource;

        // The weight is what separates grain from noise. Blown highlights and crushed blacks carry
        // none -- the emulsion is not responding there -- and the midtones carry all of it.
        constexpr const char* kGrainSource = R"(#version 300 es
precision highp float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform vec2  uFrameSize;
uniform float uIntensity;
uniform float uTime;

float cnaHash(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    vec4 source = texture(texture1, TexCoord);
    // Quantised to whole pixels and offset by time: grain that moved smoothly with the sampler
    // would read as a crawling texture rather than as film.
    vec2 pixel = floor(TexCoord * uFrameSize);
    float noise = cnaHash(pixel + vec2(uTime * 71.0, uTime * 113.0)) - 0.5;

    float luma = dot(source.rgb, vec3(0.299, 0.587, 0.114));
    float weight = 1.0 - abs(clamp(luma, 0.0, 1.0) * 2.0 - 1.0);

    FragColor = vec4(source.rgb + noise * uIntensity * weight, source.a);
}
)";

    } // namespace

    // ── Film grain ───────────────────────────────────────────────────────────

    FilmGrainPass::FilmGrainPass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
    {
        effect_ = std::make_unique<ShaderEffect>(device, kVertexSource, kGrainSource);
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
        effect_->SetUniformVec2("uFrameSize", static_cast<float>(context.width),
                                static_cast<float>(context.height));
        effect_->SetUniformFloat("uIntensity", intensity);
        effect_->SetUniformFloat("uTime", context.elapsedSeconds);
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
        return PostProcessPass::isSupported(device) && effect_ && effect_->IsEffectValid();
    }

    float FilmGrainPass::getIntensity() const { return intensity_; }
    void  FilmGrainPass::setIntensity(const float value)
    {
        intensity_ = std::clamp(value, 0.0f, 1.0f);
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

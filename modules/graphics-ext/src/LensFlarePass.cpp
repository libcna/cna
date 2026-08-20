// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/LensFlarePass.hpp"
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

        // Ghosts land on the far side of the axis from what threw them, which is why the step is
        // the vector *towards* the centre and is walked past it. A pass that stepped away from the
        // centre would put a window's ghosts on top of the window.
        constexpr const char* kFlareSource = R"(#version 300 es
precision highp float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform float uThreshold;
uniform float uIntensity;
uniform float uDispersal;
uniform int   uGhostCount;

vec3 cnaBright(vec2 uv) {
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return vec3(0.0);
    return max(texture(texture1, uv).rgb - vec3(uThreshold), vec3(0.0));
}

void main() {
    vec4 source = texture(texture1, TexCoord);
    vec2 toCentre = vec2(0.5) - TexCoord;

    vec3 ghosts = vec3(0.0);
    for (int i = 1; i <= 8; ++i) {
        if (i > uGhostCount) break;
        // Walking past the centre: at i * uDispersal greater than 1 the sample is on the opposite
        // side, which is where a lens actually puts its reflections.
        vec2 uv = TexCoord + toCentre * (1.0 + float(i) * uDispersal);
        // Nearer ghosts are brighter, as the reflections that made them are.
        ghosts += cnaBright(uv) / float(i);
    }

    FragColor = vec4(source.rgb + ghosts * uIntensity, source.a);
}
)";

    } // namespace

    // ── Lens flare ───────────────────────────────────────────────────────────

    LensFlarePass::LensFlarePass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
    {
        effect_ = std::make_unique<ShaderEffect>(device, kVertexSource, kFlareSource);
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
        effect_->SetUniformFloat("uThreshold", threshold);
        effect_->SetUniformFloat("uIntensity", intensity);
        effect_->SetUniformFloat("uDispersal", dispersal);
        effect_->SetUniformInt("uGhostCount", kGhostCount);
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
        return PostProcessPass::isSupported(device) && effect_ && effect_->IsEffectValid();
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

// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/ChromaticAberrationPass.hpp"
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

        // Aberration grows with distance from the axis, which is why scaling the *offset from the
        // centre* is the whole effect: at the centre the scale multiplies zero and the three
        // channels sample the same texel however strong the setting is.
        constexpr const char* kAberrationSource = R"(#version 300 es
precision highp float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform float uStrength;

void main() {
    vec2 fromCentre = TexCoord - vec2(0.5);
    vec2 redUv  = vec2(0.5) + fromCentre * (1.0 + uStrength);
    vec2 blueUv = vec2(0.5) + fromCentre * (1.0 - uStrength);
    FragColor = vec4(texture(texture1, redUv).r,
                     texture(texture1, TexCoord).g,
                     texture(texture1, blueUv).b,
                     texture(texture1, TexCoord).a);
}
)";

    } // namespace

    // ── Chromatic aberration ─────────────────────────────────────────────────

    ChromaticAberrationPass::ChromaticAberrationPass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
    {
        effect_ = std::make_unique<ShaderEffect>(device, kVertexSource, kAberrationSource);
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
        return PostProcessPass::isSupported(device) && effect_ && effect_->IsEffectValid();
    }

    float ChromaticAberrationPass::getStrength() const { return strength_; }
    void  ChromaticAberrationPass::setStrength(const float value)
    {
        strength_ = std::clamp(value, 0.0f, 0.1f);
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

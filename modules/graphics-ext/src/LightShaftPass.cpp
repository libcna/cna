// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/LightShaftPass.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "LensPassVertexSource.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"

#include <algorithm>
#include <string>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;

    namespace {

        constexpr const char* kVertexSource = detail::kLensVertexSource;

        constexpr const char* kFragmentSource = R"(#version 300 es
precision highp float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform vec2  uLightPosition;
uniform float uThreshold;
uniform float uIntensity;
uniform float uDecay;
uniform int   uStepCount;

vec3 cnaBright(vec2 uv) {
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return vec3(0.0);
    return max(texture(texture1, uv).rgb - vec3(uThreshold), vec3(0.0));
}

void main() {
    vec4 source = texture(texture1, TexCoord);

    // Nothing outside the frame was ever drawn, so a light past the edge has less and less of its
    // surroundings on screen to gather. Fading with how far outside it is turns the hard cut-off a
    // border test would produce into the falling-off a real shaft has.
    vec2 outside = max(vec2(0.0) - uLightPosition, uLightPosition - vec2(1.0));
    float offScreen = max(max(outside.x, outside.y), 0.0);
    float reach = clamp(1.0 - offScreen * 2.0, 0.0, 1.0);
    if (reach <= 0.0) {
        FragColor = source;
        return;
    }

    // The walk is towards the light and the sample spacing is fixed, so a pixel far from the light
    // takes bigger steps than one beside it -- which is what keeps a shaft the same length in
    // screen terms wherever it starts.
    vec2 step = (uLightPosition - TexCoord) / float(uStepCount);
    vec3 gathered = vec3(0.0);
    float weight = 1.0;
    vec2 uv = TexCoord;
    for (int i = 0; i < 64; ++i) {
        if (i >= uStepCount) break;
        uv += step;
        gathered += cnaBright(uv) * weight;
        weight *= uDecay;
    }

    FragColor = vec4(source.rgb + gathered * (uIntensity / float(uStepCount)) * reach, source.a);
}
)";

    } // namespace

    LightShaftPass::LightShaftPass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
    {
        effect_ = std::make_unique<ShaderEffect>(device, kVertexSource, kFragmentSource);
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
        effect_->SetUniformVec2("uLightPosition", lightScreenPosition_.X, lightScreenPosition_.Y);
        effect_->SetUniformFloat("uThreshold", threshold);
        effect_->SetUniformFloat("uIntensity", intensity);
        effect_->SetUniformFloat("uDecay", decay);
        effect_->SetUniformInt("uStepCount", kStepCount);

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
        return PostProcessPass::isSupported(device) && effect_ && effect_->IsEffectValid();
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

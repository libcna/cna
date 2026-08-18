// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/FxaaPass.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;

    namespace {

        constexpr const char* kVertexSource = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
out vec2 TexCoord;
uniform mat4 projection;
void main() {
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";

        // A luminance edge filter in the FXAA tradition, written from the description rather than
        // ported: sample the four neighbours, take the local luminance range, leave the pixel alone
        // where that range is below the threshold, and otherwise blend along the edge's own
        // direction. The directional step is what distinguishes it from a blur -- a plain average
        // would soften every edge including the ones that are meant to be sharp.
        constexpr const char* kFragmentSource = R"(#version 300 es
precision highp float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform vec2  uTexelSize;
uniform float uEdgeThreshold;

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
    vec3 center = texture(texture1, TexCoord).rgb;
    float lumaCenter = luma(center);
    float lumaNW = luma(texture(texture1, TexCoord + vec2(-uTexelSize.x, -uTexelSize.y)).rgb);
    float lumaNE = luma(texture(texture1, TexCoord + vec2( uTexelSize.x, -uTexelSize.y)).rgb);
    float lumaSW = luma(texture(texture1, TexCoord + vec2(-uTexelSize.x,  uTexelSize.y)).rgb);
    float lumaSE = luma(texture(texture1, TexCoord + vec2( uTexelSize.x,  uTexelSize.y)).rgb);

    float lumaMin = min(lumaCenter, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaCenter, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    if (lumaMax - lumaMin < uEdgeThreshold) {
        FragColor = vec4(center, 1.0);
        return;
    }

    vec2 direction = vec2(-((lumaNW + lumaNE) - (lumaSW + lumaSE)),
                           ((lumaNW + lumaSW) - (lumaNE + lumaSE)));
    float scale = 1.0 / (min(abs(direction.x), abs(direction.y)) + 0.125);
    direction = clamp(direction * scale, vec2(-8.0), vec2(8.0)) * uTexelSize;

    vec3 blended = 0.5 * (texture(texture1, TexCoord + direction * (1.0 / 3.0 - 0.5)).rgb +
                          texture(texture1, TexCoord + direction * (2.0 / 3.0 - 0.5)).rgb);
    vec3 wider   = blended * 0.5 +
                   0.25 * (texture(texture1, TexCoord + direction * -0.5).rgb +
                           texture(texture1, TexCoord + direction *  0.5).rgb);

    // The wider blend is only trusted while it stays inside the local luminance range; outside it
    // the filter has reached past the edge and would smear an unrelated neighbour into this pixel.
    float lumaWider = luma(wider);
    FragColor = vec4((lumaWider < lumaMin || lumaWider > lumaMax) ? blended : wider, 1.0);
}
)";

    } // namespace

    FxaaPass::FxaaPass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
    {
        effect_ = std::make_unique<ShaderEffect>(device, kVertexSource, kFragmentSource);
        // plan_modern.md MOD-219: a failed compile makes this pass copy its input through, which is
        // correct and completely silent. This names the pass and prints the compiler's log once.
        bool logged = false;
        detail::ReportShaderCompileFailure(device, "FxaaPass", effect_.get(), logged);
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
        return PostProcessPass::isSupported(device) && effect_ != nullptr
            && effect_->IsEffectValid();
    }

    float FxaaPass::getEdgeThreshold() const            { return edgeThreshold_; }
    void  FxaaPass::setEdgeThreshold(const float value) { edgeThreshold_ = value; }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

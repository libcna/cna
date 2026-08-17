// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/BloomPass.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"

#include <algorithm>
#include <cmath>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Graphics::DepthFormat;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;

    namespace {

        constexpr int kMinIterations = 1;
        constexpr int kMaxIterations = 8;
        /// Below this, a chain step contributes nothing but a draw call.
        constexpr int kMinChainExtent = 2;

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

        // Extract with a soft knee. A hard cut-off makes bloom pop in and out as a highlight
        // crosses the threshold, which is far more visible in motion than the missing energy just
        // below it; the knee trades a little correctness for that stability.
        constexpr const char* kExtractSource = R"(#version 300 es
precision highp float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform float uThreshold;
void main() {
    vec3 c = texture(texture1, TexCoord).rgb;
    float luminance = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float knee = max(uThreshold * 0.5, 1e-4);
    float contribution = clamp((luminance - uThreshold + knee) / (2.0 * knee), 0.0, 1.0);
    contribution *= contribution;
    FragColor = vec4(c * contribution, 1.0);
}
)";

        // A separable Gaussian: one pass horizontal, one vertical, driven by a direction uniform,
        // because two 9-tap passes cost 18 samples where one 9x9 kernel costs 81.
        constexpr const char* kBlurSource = R"(#version 300 es
precision highp float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform vec2 uTexelDirection;
void main() {
    // Normalized 9-tap Gaussian (sigma ~= 2).
    const float w0 = 0.2270270270;
    const float w1 = 0.1945945946;
    const float w2 = 0.1216216216;
    const float w3 = 0.0540540541;
    const float w4 = 0.0162162162;
    vec3 sum = texture(texture1, TexCoord).rgb * w0;
    sum += texture(texture1, TexCoord + uTexelDirection * 1.0).rgb * w1;
    sum += texture(texture1, TexCoord - uTexelDirection * 1.0).rgb * w1;
    sum += texture(texture1, TexCoord + uTexelDirection * 2.0).rgb * w2;
    sum += texture(texture1, TexCoord - uTexelDirection * 2.0).rgb * w2;
    sum += texture(texture1, TexCoord + uTexelDirection * 3.0).rgb * w3;
    sum += texture(texture1, TexCoord - uTexelDirection * 3.0).rgb * w3;
    sum += texture(texture1, TexCoord + uTexelDirection * 4.0).rgb * w4;
    sum += texture(texture1, TexCoord - uTexelDirection * 4.0).rgb * w4;
    FragColor = vec4(sum, 1.0);
}
)";

        // Additive composite. The scene arrives in texture1 (SpriteBatch's own slot) and the
        // blurred bloom in slot 1, so an intensity of zero reproduces the scene exactly -- which
        // is what makes "bloom disabled" and "bloom at zero" the same image.
        constexpr const char* kCombineSource = R"(#version 300 es
precision highp float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform sampler2D uBloomSampler;
uniform float uIntensity;
void main() {
    vec4 scene = texture(texture1, TexCoord);
    vec3 bloom = texture(uBloomSampler, TexCoord).rgb;
    FragColor = vec4(scene.rgb + bloom * uIntensity, scene.a);
}
)";

    } // namespace

    BloomPass::BloomPass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device)), pool_(device)
    {
        extractEffect_ = std::make_unique<ShaderEffect>(device, kVertexSource, kExtractSource);
        blurEffect_    = std::make_unique<ShaderEffect>(device, kVertexSource, kBlurSource);
        combineEffect_ = std::make_unique<ShaderEffect>(device, kVertexSource, kCombineSource);
    }

    BloomPass::~BloomPass() = default;

    float BloomPass::extractChannel(const float value, const float threshold)
    {
        const float knee = std::max(threshold * 0.5f, 1e-4f);
        float contribution = std::clamp((value - threshold + knee) / (2.0f * knee), 0.0f, 1.0f);
        contribution *= contribution;
        return value * contribution;
    }

    void BloomPass::apply(const PostProcessContext& context)
    {
        float threshold  = threshold_;
        float intensity  = intensity_;
        int   iterations = iterations_;
        if (context.settings != nullptr)
        {
            threshold  = context.settings->getBloomThreshold();
            intensity  = context.settings->getBloomIntensity();
            iterations = context.settings->getBloomIterations();
        }

        const bool shadersReady = extractEffect_ && extractEffect_->IsEffectValid()
                               && blurEffect_ && blurEffect_->IsEffectValid()
                               && combineEffect_ && combineEffect_->IsEffectValid();
        if (!shadersReady)
        {
            fullscreen_->draw(context.source, context.destination, nullptr,
                              context.width, context.height);
            return;
        }

        iterations = std::clamp(iterations, kMinIterations, kMaxIterations);

        // Intermediates carry the source's format so an HDR scene stays HDR through the chain;
        // clamping here would remove exactly the highlights bloom exists to spread.
        const auto format = context.source->getFormatProperty();

        int chainWidth  = std::max(kMinChainExtent, context.width / 2);
        int chainHeight = std::max(kMinChainExtent, context.height / 2);

        // Stage 1: extract, at half resolution -- the downsample is free filtering.
        RenderTarget2D* extracted =
            pool_.acquire(chainWidth, chainHeight, format, DepthFormat::None, 0);
        extractEffect_->Apply();
        extractEffect_->SetUniformFloat("uThreshold", threshold);
        fullscreen_->draw(context.source, extracted, extractEffect_.get(), chainWidth, chainHeight);

        // Stage 2: alternate horizontal and vertical blurs, halving the resolution each iteration.
        RenderTarget2D* current = extracted;
        for (int iteration = 0; iteration < iterations; ++iteration)
        {
            const int nextWidth  = std::max(kMinChainExtent, chainWidth / (iteration == 0 ? 1 : 2));
            const int nextHeight = std::max(kMinChainExtent, chainHeight / (iteration == 0 ? 1 : 2));

            RenderTarget2D* horizontal =
                pool_.acquire(nextWidth, nextHeight, format, DepthFormat::None, 1);
            blurEffect_->Apply();
            blurEffect_->SetUniformVec2("uTexelDirection", 1.0f / static_cast<float>(nextWidth), 0.0f);
            fullscreen_->draw(current, horizontal, blurEffect_.get(), nextWidth, nextHeight);

            RenderTarget2D* vertical =
                pool_.acquire(nextWidth, nextHeight, format, DepthFormat::None, 2);
            blurEffect_->Apply();
            blurEffect_->SetUniformVec2("uTexelDirection", 0.0f, 1.0f / static_cast<float>(nextHeight));
            fullscreen_->draw(horizontal, vertical, blurEffect_.get(), nextWidth, nextHeight);

            current     = vertical;
            chainWidth  = nextWidth;
            chainHeight = nextHeight;

            if (chainWidth <= kMinChainExtent && chainHeight <= kMinChainExtent)
                break;   // nothing left to halve
        }

        // Stage 3: composite the blurred highlights back onto the untouched scene.
        combineEffect_->Apply();
        combineEffect_->SetUniformInt("uBloomSampler", 1);
        combineEffect_->SetTexture(1, *current);
        combineEffect_->SetUniformFloat("uIntensity", intensity);
        fullscreen_->draw(context.source, context.destination, combineEffect_.get(),
                          context.width, context.height);
    }

    const std::string& BloomPass::getName() const
    {
        static const std::string name = "Bloom";
        return name;
    }

    bool BloomPass::isSupported(GraphicsDevice& device) const
    {
        return PostProcessPass::isSupported(device)
            && extractEffect_ && extractEffect_->IsEffectValid()
            && blurEffect_ && blurEffect_->IsEffectValid()
            && combineEffect_ && combineEffect_->IsEffectValid();
    }

    float BloomPass::getThreshold() const            { return threshold_; }
    void  BloomPass::setThreshold(const float value) { threshold_ = value; }

    float BloomPass::getIntensity() const            { return intensity_; }
    void  BloomPass::setIntensity(const float value) { intensity_ = value; }

    int  BloomPass::getIterations() const          { return iterations_; }
    void BloomPass::setIterations(const int value) { iterations_ = value; }

    void BloomPass::resetTargets()
    {
        pool_.reset();
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

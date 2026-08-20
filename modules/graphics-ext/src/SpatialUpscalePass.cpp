// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/SpatialUpscalePass.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;
    using Microsoft::Xna::Framework::Graphics::Texture2D;

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

        constexpr const char* kFragmentSource = R"(#version 300 es
precision highp float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform vec2  uSourceSize;
uniform vec2  uSourceTexel;
uniform float uSharpness;
uniform float uEdgeAdaptive;
uniform float uIdentity;

float cnaLuma(vec3 colour) { return dot(colour, vec3(0.2126, 0.7152, 0.0722)); }

vec3 cnaFetch(vec2 texel) {
    return texture(texture1, (clamp(texel, vec2(0.5), uSourceSize - 0.5)) * uSourceTexel).rgb;
}

void main() {
    // Nothing to resample. Not "almost nothing" -- the identity has to be exact, or the resolution
    // dial cannot be calibrated against a frame the pass did not touch.
    if (uIdentity > 0.5) {
        FragColor = texture(texture1, TexCoord);
        return;
    }

    // The source-space position of this output pixel, in texels, with the half-texel offset that
    // makes texel centres line up rather than corners.
    vec2 position = TexCoord * uSourceSize - 0.5;
    vec2 base = floor(position);
    vec2 f = position - base;

    vec3 c00 = cnaFetch(base + vec2(0.5, 0.5));
    vec3 c10 = cnaFetch(base + vec2(1.5, 0.5));
    vec3 c01 = cnaFetch(base + vec2(0.5, 1.5));
    vec3 c11 = cnaFetch(base + vec2(1.5, 1.5));

    vec3 upscaled = mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);

    if (uEdgeAdaptive > 0.5) {
        // Which way the edge runs, from the luma gradients of the same four taps. The filter then
        // leans *along* that direction rather than across it, which is what turns a staircase back
        // into a line -- and it is the whole difference from the hardware's bilinear stretch.
        float l00 = cnaLuma(c00), l10 = cnaLuma(c10), l01 = cnaLuma(c01), l11 = cnaLuma(c11);
        vec2 gradient = vec2((l10 + l11) - (l00 + l01), (l01 + l11) - (l00 + l10));
        float strength = length(gradient);

        if (strength > 1e-4) {
            vec2 edge = normalize(vec2(-gradient.y, gradient.x));

            // One tap either side, a texel apart along the edge. Averaging with them keeps what
            // runs along the edge and discards what crosses it.
            vec3 along = cnaFetch(base + vec2(0.5, 0.5) + f + edge)
                       + cnaFetch(base + vec2(0.5, 0.5) + f - edge);

            // How much to trust the direction: a weak gradient is noise, a strong one is an edge.
            float trust = clamp(strength * 2.0, 0.0, 1.0) * 0.5;
            upscaled = mix(upscaled, along * 0.5, trust);
        }
    }

    if (uSharpness > 0.0) {
        // Contrast-adaptive sharpening, and the clamp is the point: the result is held inside the
        // neighbourhood it was sharpened from, so the filter cannot invent a value brighter than
        // anything around it. That is what stops a sharpener ringing at a hard edge.
        vec3 up    = cnaFetch(base + vec2(0.5, -0.5));
        vec3 down  = cnaFetch(base + vec2(0.5,  1.5));
        vec3 left  = cnaFetch(base + vec2(-0.5, 0.5));
        vec3 right = cnaFetch(base + vec2( 1.5, 0.5));

        vec3 neighbourhood = (up + down + left + right) * 0.25;
        vec3 sharpened = upscaled + (upscaled - neighbourhood) * uSharpness;

        vec3 lowest  = min(min(min(up, down), min(left, right)), upscaled);
        vec3 highest = max(max(max(up, down), max(left, right)), upscaled);
        upscaled = clamp(sharpened, lowest, highest);
    }

    FragColor = vec4(upscaled, 1.0);
}
)";

    } // namespace

    SpatialUpscalePass::SpatialUpscalePass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
    {
        effect_ = std::make_unique<ShaderEffect>(device, kVertexSource, kFragmentSource);
        bool logged = false;
        detail::reportShaderCompileFailure(device, "SpatialUpscalePass", effect_.get(), logged);
        supported_ = effect_ != nullptr && effect_->IsEffectValid() &&
                     device.ExecutesShaderEffectSourceEXT();
    }

    SpatialUpscalePass::~SpatialUpscalePass() = default;

    bool SpatialUpscalePass::isSupported() const { return supported_; }

    bool SpatialUpscalePass::isIdentityScale(const int sourceWidth, const int sourceHeight,
                                             const int targetWidth, const int targetHeight)
    {
        return sourceWidth == targetWidth && sourceHeight == targetHeight;
    }

    void SpatialUpscalePass::draw(Texture2D* source, const int sourceWidth, const int sourceHeight,
                                  const int targetWidth, const int targetHeight)
    {
        if (source == nullptr)
            throw std::invalid_argument(
                "CNA::Graphics::SpatialUpscalePass::draw: there is nothing to upscale");
        if (sourceWidth <= 0 || sourceHeight <= 0 || targetWidth <= 0 || targetHeight <= 0)
            throw std::invalid_argument(
                "CNA::Graphics::SpatialUpscalePass::draw: every dimension must be positive");
        if (effect_ == nullptr || !effect_->IsEffectValid())
        {
            fullscreen_->drawOverCurrentTarget(source, nullptr, targetWidth, targetHeight);
            return;
        }

        const bool identity = isIdentityScale(sourceWidth, sourceHeight, targetWidth, targetHeight);

        effect_->Apply();
        effect_->SetUniformVec2("uSourceSize", static_cast<float>(sourceWidth),
                                static_cast<float>(sourceHeight));
        effect_->SetUniformVec2("uSourceTexel", 1.0f / static_cast<float>(sourceWidth),
                                1.0f / static_cast<float>(sourceHeight));
        // A 1:1 draw sharpens nothing either: the pass is asked to change nothing, and a sharpen is
        // a change.
        effect_->SetUniformFloat("uSharpness", identity ? 0.0f : sharpness_);
        effect_->SetUniformFloat("uEdgeAdaptive", edgeAdaptive_ ? 1.0f : 0.0f);
        effect_->SetUniformFloat("uIdentity", identity ? 1.0f : 0.0f);

        fullscreen_->drawOverCurrentTarget(source, effect_.get(), targetWidth, targetHeight);
    }

    float SpatialUpscalePass::getSharpness() const { return sharpness_; }
    void  SpatialUpscalePass::setSharpness(const float value)
    {
        sharpness_ = std::clamp(value, 0.0f, 1.0f);
    }

    bool SpatialUpscalePass::isEdgeAdaptive() const { return edgeAdaptive_; }
    void SpatialUpscalePass::setEdgeAdaptive(const bool value) { edgeAdaptive_ = value; }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

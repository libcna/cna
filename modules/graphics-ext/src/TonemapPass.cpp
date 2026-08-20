// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/TonemapPass.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"

#include <algorithm>
#include <cmath>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;

    namespace {

        // The SpriteBatch vertex contract: position, texture coordinate and vertex colour in
        // locations 0..2, with the batch's own projection matrix. A pass shader has to speak it
        // because FullscreenPass draws through SpriteBatch (see its header for why).
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

        // highp throughout: mediump on an ES device tops out around 2^14, and a scene-referred
        // highlight can exceed that before the curve compresses it -- the one place where the
        // usual "mediump is fine for colour" reasoning does not hold.
        constexpr const char* kFragmentSource = R"(#version 300 es
precision highp float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform int   uTonemapMode;
uniform float uExposure;
uniform float uInvGamma;
uniform float uDitherAmplitude;

vec3 reinhard(vec3 c) { return c / (1.0 + c); }

// Hejl / Burgess-Dawson. Its output is already display-encoded, so the caller skips gamma.
vec3 filmic(vec3 c) {
    vec3 x = max(vec3(0.0), c - 0.004);
    return (x * (6.2 * x + 0.5)) / (x * (6.2 * x + 1.7) + 0.06);
}

// Narkowicz's fit of the ACES filmic curve -- the fit, not the full ACES transform.
vec3 aces(vec3 c) {
    const float a = 2.51, b = 0.03, cc = 2.43, d = 0.59, e = 0.14;
    return clamp((c * (a * c + b)) / (c * (cc * c + d) + e), 0.0, 1.0);
}

// Hable's Uncharted 2 curve, normalized against the white point W.
vec3 uncharted2Curve(vec3 x) {
    const float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}
vec3 uncharted2(vec3 c) {
    const float W = 11.2;
    return uncharted2Curve(c) / uncharted2Curve(vec3(W));
}

// MOD-2132. A value in [0, 1) from a screen position, with no texture and no state.
//
// The constants are Gonzalez-Vallejo's, and what matters about them is not the particular numbers
// but that the result decorrelates across neighbouring pixels: a hash that varies smoothly is a
// gradient, and adding a gradient to a gradient moves a band rather than removing it.
float cnaDitherHash(vec2 position) {
    return fract(sin(dot(position, vec2(12.9898, 78.233))) * 43758.5453);
}

// A triangular probability distribution, from two independent uniforms.
//
// Uniform noise of one quantisation step removes the banding and leaves the *noise level* varying
// with the signal, which is its own visible artefact -- flat areas look grainier at some
// brightnesses than at others. A triangular distribution is what makes the error independent of
// the signal, at the cost of twice the noise power. This is the standard result from audio
// dithering and it holds unchanged here.
float cnaTriangularDither(vec2 position) {
    return cnaDitherHash(position) - cnaDitherHash(position + vec2(17.0, 23.0));
}

void main() {
    vec4 source = texture(texture1, TexCoord);
    vec3 color = source.rgb * uExposure;

    if (uTonemapMode == 1)      color = reinhard(color);
    else if (uTonemapMode == 2) color = filmic(color);
    else if (uTonemapMode == 3) color = aces(color);
    else if (uTonemapMode == 4) color = uncharted2(color);

    color = clamp(color, 0.0, 1.0);

    // Filmic already encodes for the display; encoding it again would wash the image out.
    if (uTonemapMode != 2) color = pow(color, vec3(uInvGamma));

    // MOD-2132: **after** the transfer function, never before. The curve's slope varies by more
    // than seven to one across the range, so a fixed perturbation applied in linear light arrives
    // at the display as a large one in the shadows and almost nothing in the highlights -- which is
    // the opposite of what is needed, the shadows being where the banding is. Applied here, one
    // unit is one unit everywhere.
    if (uDitherAmplitude > 0.0)
        color += vec3(cnaTriangularDither(gl_FragCoord.xy) * uDitherAmplitude);

    FragColor = vec4(color, source.a);
}
)";

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
        effect_ = std::make_unique<ShaderEffect>(device, kVertexSource, kFragmentSource);
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
        effect_->SetUniformInt("uTonemapMode", static_cast<int>(mode));
        effect_->SetUniformFloat("uExposure", exposure);
        effect_->SetUniformFloat("uInvGamma", gamma > 0.0f ? 1.0f / gamma : 1.0f);
        // In output units of the 8-bit target this pass writes to: one step is 1/255.
        effect_->SetUniformFloat("uDitherAmplitude",
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
        return PostProcessPass::isSupported(device) && effect_ != nullptr
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

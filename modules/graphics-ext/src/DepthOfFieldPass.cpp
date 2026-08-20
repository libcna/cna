// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/DepthOfFieldPass.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

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

        constexpr const char* kFragmentBody = R"(
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform sampler2D uDepthSampler;
uniform vec2  uFrameSize;
uniform float uFarPlane;
uniform float uFocusDistance;
uniform float uFocalLength;
uniform float uFNumber;
uniform float uMaxRadius;

// The same thin-lens diameter DepthOfFieldPass::circleOfConfusionMillimetres computes on the CPU,
// written twice on purpose: the CPU copy is what a test can check the optics against, and a shader
// that called into it would not be a shader. The two are compared against each other by
// DepthOfFieldTest.TheShaderMatchesTheCpuReference.
float cnaCircleOfConfusionMm(float depthWorld) {
    if (depthWorld <= 0.0 || uFocusDistance <= 0.0 || uFNumber <= 0.0) return 0.0;
    float focusMm = uFocusDistance * 1000.0;
    float depthMm = depthWorld * 1000.0;
    // A lens cannot focus at or inside its own focal length; the formula divides by that difference.
    if (focusMm <= uFocalLength) return 0.0;
    return (uFocalLength * uFocalLength / (uFNumber * (focusMm - uFocalLength)))
         * abs(depthMm - focusMm) / depthMm;
}

/// The blur radius a pixel deserves, as a fraction of the frame.
float cnaBlurRadius(float linearDepth) {
    float diameterMm = cnaCircleOfConfusionMm(linearDepth * uFarPlane);
    // Half the diameter, expressed against the sensor: a circle as tall as the sensor covers the
    // whole frame, so its radius is half the frame.
    float radius = 0.5 * diameterMm / kCnaSensorHeightMm;
    return min(radius, uMaxRadius);
}

// Sixteen points on a golden-angle spiral: even coverage without the rings a concentric layout
// produces, and cheap enough to write out rather than loop with a sin/cos per tap.
const int kTaps = 16;
const vec2 kDisc[16] = vec2[16](
    vec2( 0.2165,  0.0745), vec2(-0.1863,  0.2549), vec2(-0.0851, -0.3777), vec2( 0.3966,  0.2154),
    vec2(-0.4644,  0.1509), vec2( 0.2260, -0.4707), vec2( 0.1751,  0.5411), vec2(-0.5527, -0.2338),
    vec2( 0.6033, -0.2467), vec2(-0.2249,  0.6414), vec2(-0.3225, -0.6321), vec2( 0.7108,  0.2116),
    vec2(-0.7357,  0.2420), vec2( 0.2941, -0.7628), vec2( 0.2497,  0.8004), vec2(-0.8098, -0.2646)
);

void main() {
    vec3 centerColor = texture(texture1, TexCoord).rgb;
    float centerDepth = cnaDecodeLinearDepth(texture(uDepthSampler, TexCoord));

    // Nothing was drawn here. The sky sits at the far plane whatever the prepass wrote, and blurring
    // it against a depth of zero would put it at the camera and smear it across the frame.
    if (centerDepth <= 0.0 || centerDepth >= 0.999) {
        FragColor = vec4(centerColor, 1.0);
        return;
    }

    float centerRadius = cnaBlurRadius(centerDepth);
    vec3 sum = centerColor;
    float weight = 1.0;

    for (int i = 0; i < kTaps; ++i) {
        vec2 offset = kDisc[i] * centerRadius;
        vec2 tapUv = TexCoord + offset;
        if (tapUv.x < 0.0 || tapUv.x > 1.0 || tapUv.y < 0.0 || tapUv.y > 1.0) continue;

        float tapDepth = cnaDecodeLinearDepth(texture(uDepthSampler, tapUv));
        if (tapDepth <= 0.0 || tapDepth >= 0.999) continue;

        // Bleed control, and the whole reason this is not a depth-weighted blur. A tap contributes
        // only if its *own* circle of confusion is wide enough to reach this pixel -- which is what
        // scattering light onto a neighbour means. Without the test, an in-focus subject spreads
        // its colour into the blurred background beside it and loses its silhouette, the failure
        // every naive gather produces: sharp things must not smear onto blurred neighbours.
        float tapRadius = cnaBlurRadius(tapDepth);
        float reach = length(offset);
        float accept = smoothstep(0.0, max(reach, 1e-5), tapRadius);

        sum += texture(texture1, tapUv).rgb * accept;
        weight += accept;
    }

    FragColor = vec4(sum / max(weight, 1e-5), 1.0);
}
)";

        std::string MakeFragmentSource(const bool packedDepth)
        {
            std::string source = "#version 300 es\nprecision highp float;\n";
            // Injected rather than written twice: the header's constant is the one a caller reads
            // when converting a circle of confusion into a screen radius by hand.
            source += "const float kCnaSensorHeightMm = " +
                      std::to_string(DepthOfFieldPass::kSensorHeightMillimetres) + ";\n";
            source += DepthNormalPrepass::getDepthDecodeGlsl(packedDepth);
            source += kFragmentBody;
            return source;
        }

    } // namespace

    DepthOfFieldPass::DepthOfFieldPass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
    {
        const bool packed = DepthNormalPrepass::usesPackedDepthEXT(device);
        effect_ = std::make_unique<ShaderEffect>(device, kVertexSource, MakeFragmentSource(packed));
        bool logged = false;
        detail::reportShaderCompileFailure(device, "DepthOfFieldPass", effect_.get(), logged);
    }

    DepthOfFieldPass::~DepthOfFieldPass() = default;

    float DepthOfFieldPass::circleOfConfusionMillimetres(const float depth,
                                                        const float focusDistance,
                                                        const float focalLength,
                                                        const float fNumber)
    {
        if (depth <= 0.0f || focusDistance <= 0.0f || focalLength <= 0.0f || fNumber <= 0.0f)
            return 0.0f;

        const float focusMm = focusDistance * 1000.0f;
        const float depthMm = depth * 1000.0f;
        // A lens cannot focus at or inside its own focal length, and the formula divides by exactly
        // that difference. Answering zero is the honest reading: such a configuration has no
        // circle of confusion because it has no image.
        if (focusMm <= focalLength)
            return 0.0f;

        return (focalLength * focalLength / (fNumber * (focusMm - focalLength)))
             * std::fabs(depthMm - focusMm) / depthMm;
    }

    void DepthOfFieldPass::apply(const PostProcessContext& context)
    {
        const bool haveInputs = context.sourceDepth != nullptr && context.farPlane > 0.0f;
        if (effect_ == nullptr || !effect_->IsEffectValid() || !haveInputs)
        {
            fullscreen_->draw(context.source, context.destination, nullptr,
                              context.width, context.height);
            return;
        }

        const RenderPipelineSettings* settings = context.settings;
        const float focus  = settings != nullptr ? settings->getDOFFocusDistance() : focusDistance_;
        const float length = settings != nullptr ? settings->getDOFFocalLength()   : focalLength_;
        const float number = settings != nullptr ? settings->getDOFFNumber()       : fNumber_;
        const float radius = settings != nullptr ? settings->getDOFMaxRadius()     : maxRadius_;

        effect_->Apply();
        effect_->SetUniformInt("uDepthSampler", 1);
        effect_->SetTexture(1, *context.sourceDepth);
        effect_->SetUniformVec2("uFrameSize", static_cast<float>(context.width),
                                static_cast<float>(context.height));
        effect_->SetUniformFloat("uFarPlane", context.farPlane);
        effect_->SetUniformFloat("uFocusDistance", focus);
        effect_->SetUniformFloat("uFocalLength", length);
        effect_->SetUniformFloat("uFNumber", number);
        effect_->SetUniformFloat("uMaxRadius", radius);

        fullscreen_->draw(context.source, context.destination, effect_.get(),
                          context.width, context.height);
    }

    const std::string& DepthOfFieldPass::getName() const
    {
        static const std::string name = "DepthOfField";
        return name;
    }

    bool DepthOfFieldPass::isSupported(GraphicsDevice& device) const
    {
        return PostProcessPass::isSupported(device) && effect_ && effect_->IsEffectValid();
    }

    float DepthOfFieldPass::getFocusDistance() const { return focusDistance_; }
    void  DepthOfFieldPass::setFocusDistance(const float value)
    {
        if (value > 0.0f) focusDistance_ = value;
    }

    float DepthOfFieldPass::getFocalLength() const { return focalLength_; }
    void  DepthOfFieldPass::setFocalLength(const float value)
    {
        if (value > 0.0f) focalLength_ = value;
    }

    float DepthOfFieldPass::getFNumber() const { return fNumber_; }
    void  DepthOfFieldPass::setFNumber(const float value)
    {
        if (value > 0.0f) fNumber_ = value;
    }

    float DepthOfFieldPass::getMaxRadius() const { return maxRadius_; }
    void  DepthOfFieldPass::setMaxRadius(const float value)
    {
        maxRadius_ = std::clamp(value, 0.0f, 0.25f);
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

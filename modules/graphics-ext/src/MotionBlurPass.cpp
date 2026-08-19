// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/MotionBlurPass.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "LensPassVertexSource.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <algorithm>
#include <string>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

    namespace {

        constexpr const char* kVertexSource = detail::kLensVertexSource;

        constexpr const char* kFragmentBody = R"(
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform sampler2D uDepthSampler;
uniform mat4  uInverseProjection;
uniform mat4  uInverseView;
uniform mat4  uPreviousViewProjection;
uniform float uFarPlane;
uniform float uStrength;
uniform float uMaxDistance;
uniform int   uSampleCount;

void main() {
    vec4 source = texture(texture1, TexCoord);
    float depth = cnaDecodeLinearDepth(texture(uDepthSampler, TexCoord));

    // The sky has no position to reproject: it is infinitely far, so it does not move with the
    // camera's translation and reprojecting it through a rotation would be right only by accident.
    if (depth <= 0.0 || depth >= 0.999) {
        FragColor = source;
        return;
    }

    // View space is not enough here. Anything comparing pixels *within* a frame can stay in view
    // space, but view space moved with the camera, so a comparison *across* frames has to happen
    // somewhere that did not move -- the world.
    vec3 viewPosition = cnaViewPositionFromDepth(TexCoord, depth, uInverseProjection) * uFarPlane;
    vec4 world = uInverseView * vec4(viewPosition, 1.0);

    vec4 previousClip = uPreviousViewProjection * world;
    if (previousClip.w <= 0.0) {
        FragColor = source;
        return;
    }
    vec2 previousUv = (previousClip.xy / previousClip.w) * 0.5 + 0.5;

    vec2 velocity = (TexCoord - previousUv) * uStrength;
    float distance = length(velocity);
    // One slow frame makes every velocity enormous. Without the cap a single stutter smears the
    // whole image, which reads as a defect in the blur rather than as the hitch it is.
    if (distance > uMaxDistance) velocity *= uMaxDistance / distance;

    vec3 sum = source.rgb;
    float weight = 1.0;
    for (int i = 1; i <= 16; ++i) {
        if (i >= uSampleCount) break;
        // Walking backwards along the velocity: the smear trails where the pixel came *from*, which
        // is what a shutter open across the movement records.
        vec2 uv = TexCoord - velocity * (float(i) / float(uSampleCount - 1));
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) continue;
        sum += texture(texture1, uv).rgb;
        weight += 1.0;
    }

    FragColor = vec4(sum / weight, source.a);
}
)";

        std::string MakeFragmentSource(const bool packedDepth)
        {
            std::string source = "#version 300 es\nprecision highp float;\n";
            source += DepthNormalPrepass::getDepthDecodeGlsl(packedDepth);
            source += kFragmentBody;
            return source;
        }

    } // namespace

    MotionBlurPass::MotionBlurPass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
    {
        const bool packed =
            !device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HalfSingle);
        effect_ = std::make_unique<ShaderEffect>(device, kVertexSource, MakeFragmentSource(packed));
        bool logged = false;
        detail::reportShaderCompileFailure(device, "MotionBlurPass", effect_.get(), logged);
    }

    MotionBlurPass::~MotionBlurPass() = default;

    void MotionBlurPass::apply(const PostProcessContext& context)
    {
        const RenderPipelineSettings* settings = context.settings;
        const float strength = settings != nullptr ? settings->getMotionBlurStrength() : strength_;
        const float maxDistance =
            settings != nullptr ? settings->getMotionBlurMaxDistance() : maxDistance_;

        // Four things have to be true, and the third is the one that is easy to forget: without a
        // previous frame there is no velocity, only the identity matrix pretending to be one.
        const bool ready = effect_ != nullptr && effect_->IsEffectValid()
                        && context.sourceDepth != nullptr
                        && context.hasPreviousFrame
                        && context.farPlane > 0.0f;
        if (!ready || strength <= 0.0f)
        {
            fullscreen_->draw(context.source, context.destination, nullptr,
                              context.width, context.height);
            return;
        }

        effect_->Apply();
        effect_->SetUniformInt("uDepthSampler", 1);
        effect_->SetTexture(1, *context.sourceDepth);
        effect_->SetUniformMat4("uInverseProjection", &context.inverseProjection.M11);
        effect_->SetUniformMat4("uInverseView", &context.inverseView.M11);
        effect_->SetUniformMat4("uPreviousViewProjection", &context.previousViewProjection.M11);
        effect_->SetUniformFloat("uFarPlane", context.farPlane);
        effect_->SetUniformFloat("uStrength", strength);
        effect_->SetUniformFloat("uMaxDistance", maxDistance);
        effect_->SetUniformInt("uSampleCount", kSampleCount);

        fullscreen_->draw(context.source, context.destination, effect_.get(),
                          context.width, context.height);
    }

    const std::string& MotionBlurPass::getName() const
    {
        static const std::string name = "MotionBlur";
        return name;
    }

    bool MotionBlurPass::isSupported(GraphicsDevice& device) const
    {
        return PostProcessPass::isSupported(device) && effect_ && effect_->IsEffectValid();
    }

    float MotionBlurPass::getStrength() const { return strength_; }
    void  MotionBlurPass::setStrength(const float value)
    {
        strength_ = std::clamp(value, 0.0f, 1.0f);
    }

    float MotionBlurPass::getMaxDistance() const { return maxDistance_; }
    void  MotionBlurPass::setMaxDistance(const float value)
    {
        maxDistance_ = std::clamp(value, 0.0f, 0.25f);
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

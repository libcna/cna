// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/HeightFogPass.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "LensPassVertexSource.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cmath>
#include <string>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Vector3;
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
uniform vec3  uFogColor;
uniform float uFarPlane;
uniform float uDensity;
uniform float uFalloff;
uniform float uBaseHeight;

// The closed form of the density integral along a straight ray. The level-look case is not a
// special case of the general one -- the general one divides by the ray's climb -- so it is written
// out rather than nudged away from zero, which would make a level view's fog depend on the epsilon.
float cnaOpticalDepth(float cameraHeight, float rayHeightStep, float distance) {
    float atCamera = uDensity * exp(-uFalloff * (cameraHeight - uBaseHeight));
    float climb = uFalloff * rayHeightStep;
    if (abs(climb) < 1e-5) return max(atCamera * distance, 0.0);
    return max(atCamera * (1.0 - exp(-climb * distance)) / climb, 0.0);
}

void main() {
    vec4 source = texture(texture1, TexCoord);
    float depth = cnaDecodeLinearDepth(texture(uDepthSampler, TexCoord));

    // The sky is at the far plane whatever the prepass wrote, and it is exactly what fog should
    // fade *into*, so it is fogged at the far distance rather than skipped.
    float travelled = (depth <= 0.0 || depth >= 0.999) ? uFarPlane : depth * uFarPlane;

    vec3 viewPosition = cnaViewPositionFromDepth(TexCoord, max(depth, 1e-4), uInverseProjection)
                      * uFarPlane;
    vec4 world = uInverseView * vec4(viewPosition, 1.0);
    vec4 cameraWorld = uInverseView * vec4(0.0, 0.0, 0.0, 1.0);

    vec3 alongRay = world.xyz - cameraWorld.xyz;
    float length = max(length(alongRay), 1e-4);
    float rayHeightStep = alongRay.y / length;

    float optical = cnaOpticalDepth(cameraWorld.y, rayHeightStep, travelled);
    float fog = 1.0 - exp(-optical);

    FragColor = vec4(mix(source.rgb, uFogColor, clamp(fog, 0.0, 1.0)), source.a);
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

    HeightFogPass::HeightFogPass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
    {
        const bool packed = DepthNormalPrepass::usesPackedDepthEXT(device);
        effect_ = std::make_unique<ShaderEffect>(device, kVertexSource, MakeFragmentSource(packed));
        bool logged = false;
        detail::reportShaderCompileFailure(device, "HeightFogPass", effect_.get(), logged);
    }

    HeightFogPass::~HeightFogPass() = default;

    float HeightFogPass::opticalDepth(const float cameraHeight, const float rayHeightStep,
                                      const float distance, const float density,
                                      const float falloff, const float baseHeight)
    {
        if (density <= 0.0f || distance <= 0.0f || falloff <= 0.0f) return 0.0f;

        const float atCamera = density * std::exp(-falloff * (cameraHeight - baseHeight));
        const float climb    = falloff * rayHeightStep;
        // A level look is not the general case with a small number in it: the general case divides
        // by the climb, so nudging it away from zero would make a level view's fog depend on the
        // size of the nudge.
        if (std::fabs(climb) < 1e-5f)
            return std::max(atCamera * distance, 0.0f);
        return std::max(atCamera * (1.0f - std::exp(-climb * distance)) / climb, 0.0f);
    }

    void HeightFogPass::apply(const PostProcessContext& context)
    {
        const RenderPipelineSettings* settings = context.settings;
        const float density = settings != nullptr ? settings->getHeightFogDensity() : density_;
        const float falloff = settings != nullptr ? settings->getHeightFogFalloff() : falloff_;
        const float base    = settings != nullptr ? settings->getHeightFogBaseHeight() : baseHeight_;

        const bool ready = effect_ != nullptr && effect_->IsEffectValid()
                        && context.sourceDepth != nullptr && context.farPlane > 0.0f;
        if (!ready || density <= 0.0f || falloff <= 0.0f)
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
        effect_->SetUniformVec3("uFogColor", color_.X, color_.Y, color_.Z);
        effect_->SetUniformFloat("uFarPlane", context.farPlane);
        effect_->SetUniformFloat("uDensity", density);
        effect_->SetUniformFloat("uFalloff", falloff);
        effect_->SetUniformFloat("uBaseHeight", base);

        fullscreen_->draw(context.source, context.destination, effect_.get(),
                          context.width, context.height);
    }

    const std::string& HeightFogPass::getName() const
    {
        static const std::string name = "HeightFog";
        return name;
    }

    bool HeightFogPass::isSupported(GraphicsDevice& device) const
    {
        return PostProcessPass::isSupported(device) && effect_ && effect_->IsEffectValid();
    }

    Vector3 HeightFogPass::getColor() const { return color_; }
    void    HeightFogPass::setColor(const Vector3& value) { color_ = value; }

    float HeightFogPass::getDensity() const { return density_; }
    void  HeightFogPass::setDensity(const float value) { if (value >= 0.0f) density_ = value; }

    float HeightFogPass::getFalloff() const { return falloff_; }
    void  HeightFogPass::setFalloff(const float value) { if (value > 0.0f) falloff_ = value; }

    float HeightFogPass::getBaseHeight() const { return baseHeight_; }
    void  HeightFogPass::setBaseHeight(const float value) { baseHeight_ = value; }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

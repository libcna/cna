// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/SsaoPass.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <algorithm>
#include <cmath>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::DepthFormat;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    using Microsoft::Xna::Framework::Graphics::Texture2D;

    namespace {

        constexpr int kMinSamples  = 8;
        constexpr int kMaxSamples  = 64;
        constexpr int kNoiseExtent = 4;

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

        // Occlusion estimate. texture1 is the linear-depth image (SpriteBatch's own slot), slot 1
        // the view-space normals, slot 2 the rotation noise.
        //
        // The range check is what keeps a distant silhouette from darkening the surface in front of
        // it: a sample is only counted when the geometry it hit is within the sampling radius, so
        // an object far behind the pixel occludes nothing.
        constexpr const char* kOcclusionSource = R"(#version 300 es
precision highp float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform sampler2D uNormalSampler;
uniform sampler2D uNoiseSampler;
uniform vec3  uKernel[64];
uniform vec2  uNoiseScale;
uniform float uRadius;
uniform float uBias;
uniform float uDepthRange;
uniform int   uSampleCount;

void main() {
    float centerDepth = texture(texture1, TexCoord).r;

    // Nothing was rendered here (the prepass cleared to "infinitely far"); the sky is not occluded.
    if (centerDepth <= 0.0) {
        FragColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    // Every normalize() here is guarded. normalize(vec3(0)) is NaN, every comparison against NaN
    // is false, and the occlusion test below is a comparison -- so one degenerate vector does not
    // produce a wrong pixel, it silently produces an entirely unoccluded frame with no error
    // anywhere to point at. A mid-grey noise texel is enough to do it: (0.5, 0.5) decodes to (0, 0).
    vec3 rawNormal = texture(uNormalSampler, TexCoord).xyz * 2.0 - 1.0;
    vec3 normal = length(rawNormal) > 1e-4 ? normalize(rawNormal) : vec3(0.0, 0.0, 1.0);

    // A per-pixel rotation from a tiled 4x4 noise texture. Without it every pixel samples the same
    // pattern and the result bands visibly; with it the error becomes noise the blur can remove.
    vec3 rawRandom = vec3(texture(uNoiseSampler, TexCoord * uNoiseScale).xy * 2.0 - 1.0, 0.0);
    vec3 randomVector = length(rawRandom) > 1e-4 ? normalize(rawRandom) : vec3(1.0, 0.0, 0.0);

    vec3 rawTangent = randomVector - normal * dot(randomVector, normal);
    // The rotation vector can land parallel to the normal, leaving nothing to build a tangent from.
    vec3 tangent = length(rawTangent) > 1e-4
                     ? normalize(rawTangent)
                     : normalize(cross(normal, vec3(0.0, 1.0, 0.0)) + vec3(1e-3, 0.0, 0.0));
    vec3 bitangent = cross(normal, tangent);
    mat3 tbn = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;
    int count = uSampleCount;
    for (int i = 0; i < 64; ++i) {
        if (i >= count) break;

        vec3 samplePosition = tbn * uKernel[i];
        vec2 sampleUv = TexCoord + samplePosition.xy * uRadius;
        float sampleDepth = textureLod(texture1, sampleUv, 0.0).r;
        if (sampleDepth <= 0.0) continue;

        // An occluder is simply something nearer to the camera than this pixel. The obvious
        // extra term -- offsetting the comparison by the sample's own depth component times the
        // radius -- belongs to a view-space formulation, and this pass has no view space: its
        // radius is a screen-space offset in UV and its depths are a normalized texture. Mixing
        // the two makes the comparison depend on a quantity in the wrong units, and the symptom
        // is not a wrong-looking image but an entirely unoccluded one at most radii, because the
        // offset swamps the depth difference it is being compared against.
        if (sampleDepth < centerDepth - uBias) {
            // Distant geometry seen past a silhouette must not darken this pixel; uDepthRange is
            // how far away an occluder may be and still count, in the depth texture's own units.
            float rangeCheck =
                smoothstep(0.0, 1.0, uDepthRange / max(abs(centerDepth - sampleDepth), 1e-5));
            occlusion += rangeCheck;
        }
    }

    float visibility = 1.0 - occlusion / float(count);
    FragColor = vec4(visibility, visibility, visibility, 1.0);
}
)";

        // Blur the AO buffer and multiply it into the scene in one pass: the AO term is noisy by
        // construction, and a separate blur pass would need a third intermediate for no gain at
        // this kernel size.
        constexpr const char* kComposeSource = R"(#version 300 es
precision highp float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform sampler2D uOcclusionSampler;
uniform vec2  uTexelSize;
uniform float uIntensity;

void main() {
    float blurred = 0.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            blurred += texture(uOcclusionSampler,
                               TexCoord + vec2(float(x), float(y)) * uTexelSize).r;
        }
    }
    blurred /= 25.0;

    float visibility = clamp(1.0 - (1.0 - blurred) * uIntensity, 0.0, 1.0);
    vec4 scene = texture(texture1, TexCoord);
    FragColor = vec4(scene.rgb * visibility, scene.a);
}
)";

    } // namespace

    SsaoPass::SsaoPass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device)), pool_(device)
    {
        occlusionEffect_ = std::make_unique<ShaderEffect>(device, kVertexSource, kOcclusionSource);
        composeEffect_   = std::make_unique<ShaderEffect>(device, kVertexSource, kComposeSource);

        // plan_modern.md MOD-219: a failed compile makes this pass copy its input through, which is
        // correct and completely silent. This names the pass and prints the compiler's log once.
        bool logged = false;
        detail::ReportShaderCompileFailure(device, "SsaoPass (occlusion)", occlusionEffect_.get(),
                                           logged);
        detail::ReportShaderCompileFailure(device, "SsaoPass (compose)", composeEffect_.get(),
                                           logged);

        generateKernel();

        // A deterministic 4x4 rotation texture. Deterministic on purpose: a seeded pattern makes
        // the pass reproducible frame to frame and test to test, and randomness here buys nothing
        // that a fixed well-distributed set does not.
        noiseTexture_ = std::make_unique<Texture2D>(device, kNoiseExtent, kNoiseExtent);
        std::vector<Color> noise;
        noise.reserve(static_cast<std::size_t>(kNoiseExtent) * kNoiseExtent);
        for (int index = 0; index < kNoiseExtent * kNoiseExtent; ++index)
        {
            const float angle = static_cast<float>(index) * 0.39269908f;   // 22.5 degrees apart
            const float x = std::cos(angle) * 0.5f + 0.5f;
            const float y = std::sin(angle) * 0.5f + 0.5f;
            noise.emplace_back(static_cast<int>(x * 255.0f), static_cast<int>(y * 255.0f), 0, 255);
        }
        noiseTexture_->SetData(noise.data(), static_cast<int>(noise.size()));
    }

    SsaoPass::~SsaoPass() = default;

    void SsaoPass::generateKernel()
    {
        kernel_.clear();
        kernel_.reserve(kMaxSamples);

        // A deterministic low-discrepancy set rather than a random one, for the same reason the
        // noise texture is fixed: the pass must produce the same image twice. The scale term biases
        // samples toward the origin so nearby geometry dominates -- without it, contact shadows
        // wash out into a uniform grey.
        for (int index = 0; index < kMaxSamples; ++index)
        {
            const float u1 = (static_cast<float>(index) + 0.5f) / static_cast<float>(kMaxSamples);
            // Van der Corput radical inverse, base 2.
            unsigned int bits = static_cast<unsigned int>(index);
            bits = (bits << 16u) | (bits >> 16u);
            bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
            bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
            bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
            bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
            const float u2 = static_cast<float>(bits) * 2.3283064365386963e-10f;

            const float phi      = 6.2831853f * u1;
            const float cosTheta = u2;                       // biased toward the pole (+Z)
            const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));

            Vector3 sample(std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta);

            float scale = static_cast<float>(index) / static_cast<float>(kMaxSamples);
            scale = 0.1f + 0.9f * scale * scale;
            sample.X *= scale;
            sample.Y *= scale;
            sample.Z *= scale;
            kernel_.push_back(sample);
        }
    }

    void SsaoPass::apply(const PostProcessContext& context)
    {
        float radius    = radius_;
        float intensity = intensity_;
        int   samples   = sampleCount_;
        if (context.settings != nullptr)
        {
            radius    = context.settings->getSSAORadius();
            intensity = context.settings->getSSAOIntensity();
            samples   = context.settings->getSSAOSampleCount();
        }
        samples = std::clamp(samples, kMinSamples, kMaxSamples);

        const bool ready = occlusionEffect_ && occlusionEffect_->IsEffectValid()
                        && composeEffect_ && composeEffect_->IsEffectValid();
        const bool haveInputs = context.sourceDepth != nullptr && context.sourceNormals != nullptr;

        if (!ready || !haveInputs)
        {
            // Documented fallback: an unoccluded frame, not a failure. A pipeline that enables SSAO
            // without running a depth/normal prepass is misconfigured, not broken.
            fullscreen_->draw(context.source, context.destination, nullptr,
                              context.width, context.height);
            return;
        }

        RenderTarget2D* occlusion = pool_.acquire(context.width, context.height,
                                                  SurfaceFormat::Color, DepthFormat::None, 0);

        occlusionEffect_->Apply();
        occlusionEffect_->SetUniformInt("uNormalSampler", 1);
        occlusionEffect_->SetTexture(1, *context.sourceNormals);
        occlusionEffect_->SetUniformInt("uNoiseSampler", 2);
        occlusionEffect_->SetTexture(2, *noiseTexture_);
        occlusionEffect_->SetUniformVec3Array("uKernel", &kernel_[0].X, kMaxSamples);
        occlusionEffect_->SetUniformVec2(
            "uNoiseScale",
            static_cast<float>(context.width) / static_cast<float>(kNoiseExtent),
            static_cast<float>(context.height) / static_cast<float>(kNoiseExtent));
        occlusionEffect_->SetUniformFloat("uRadius", radius);
        occlusionEffect_->SetUniformFloat("uBias", 0.005f);
        // The depth-side companion to the screen-space radius: how far, in the depth texture's own
        // 0..1 units, an occluder may be and still count. Tied to the radius so one setting still
        // controls "how big is the ambient neighbourhood", but never used as a UV offset.
        occlusionEffect_->SetUniformFloat("uDepthRange", std::max(radius * 0.25f, 0.01f));
        occlusionEffect_->SetUniformInt("uSampleCount", samples);

        fullscreen_->draw(context.sourceDepth, occlusion, occlusionEffect_.get(),
                          context.width, context.height);

        composeEffect_->Apply();
        composeEffect_->SetUniformInt("uOcclusionSampler", 1);
        composeEffect_->SetTexture(1, *occlusion);
        composeEffect_->SetUniformVec2("uTexelSize",
                                       1.0f / static_cast<float>(context.width),
                                       1.0f / static_cast<float>(context.height));
        composeEffect_->SetUniformFloat("uIntensity", intensity);

        fullscreen_->draw(context.source, context.destination, composeEffect_.get(),
                          context.width, context.height);
    }

    const std::string& SsaoPass::getName() const
    {
        static const std::string name = "SSAO";
        return name;
    }

    bool SsaoPass::isSupported(GraphicsDevice& device) const
    {
        return PostProcessPass::isSupported(device)
            && occlusionEffect_ && occlusionEffect_->IsEffectValid()
            && composeEffect_ && composeEffect_->IsEffectValid();
    }

    float SsaoPass::getRadius() const            { return radius_; }
    void  SsaoPass::setRadius(const float value) { radius_ = value; }

    float SsaoPass::getIntensity() const            { return intensity_; }
    void  SsaoPass::setIntensity(const float value) { intensity_ = value; }

    int  SsaoPass::getSampleCount() const          { return sampleCount_; }
    void SsaoPass::setSampleCount(const int value) { sampleCount_ = value; }

    void SsaoPass::resetTargets()
    {
        pool_.reset();
    }

    const std::vector<Vector3>& SsaoPass::getKernel() const
    {
        return kernel_;
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

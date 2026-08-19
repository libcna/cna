// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/SsrPass.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
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

        // The march, in the scaled view space `cnaViewPositionFromDepth` returns: that helper
        // divides by the far plane, so a position's -z *is* the stored depth and the two can be
        // compared without converting either. Every distance the app supplies is in world units and
        // is divided by the far plane on the way in, which is the only place the two spaces meet.
        //
        // The camera projection arrives as uCameraProjection rather than `projection`: the latter
        // is the fullscreen quad's own orthographic matrix, set by FullscreenPass, and reusing the
        // name would silently overwrite one with the other.
        constexpr const char* kFragmentBody = R"(
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform sampler2D uDepthSampler;
uniform sampler2D uNormalSampler;
uniform vec2  uDepthSize;
uniform mat4  uCameraProjection;
uniform mat4  uInverseProjection;
uniform float uFarPlaneScale;
uniform float uMaxDistance;
uniform float uDepthBias;
uniform float uThickness;
uniform float uIntensity;
uniform float uEdgeFade;
uniform float uRoughnessBlur;
uniform int   uStepCount;

// Point sampling, done here rather than through a sampler state.
//
// Filtering a depth edge averages two surfaces at different distances into a distance nothing in
// the scene is at, and the march finds its hit against that invented surface -- one texel early, in
// front of the object it was looking for, where the colour image still holds the background. A
// normal filtered across a crease points somewhere neither surface faces. The obvious fix is to bind
// the depth and normal units with PointClamp, and it does not work: the per-unit sampler state does
// not reach a texture bound through `ShaderEffect::SetTexture` -- measured, linear and point give
// the same filtered value. Snapping the coordinate to the texel centre gives the same result through
// arithmetic the shader owns.
// One texel short of the far plane. The clear writes exactly 1.0, and a real surface at the far
// plane is already outside anything worth reflecting.
const float kCnaSkyDepth = 0.999;

// Bisections of the bracket the march ends on. Six halvings take a step down to 1/64 of itself,
// which is below one texel for every step count this pass accepts -- past that the snapping to
// texel centres decides the answer and further halving changes nothing.
const int kCnaRefineSteps = 6;

vec2 cnaSnapToTexel(vec2 uv, vec2 size) {
    return (floor(uv * size) + 0.5) / size;
}

void main() {
    vec3 sourceColor = texture(texture1, TexCoord).rgb;
    float centerDepth = cnaDecodeLinearDepth(texture(uDepthSampler, cnaSnapToTexel(TexCoord, uDepthSize)));

    // Nothing was drawn here, and "nothing" has two spellings. `DepthNormalPrepass` clears depth to
    // **white**, so an empty pixel decodes to 1.0 -- the far plane -- and a pass that only tested
    // for zero would treat the sky as a surface sitting at the camera and march a reflection out of
    // it. Zero is still checked because a renderer that clears its target to black produces it.
    if (centerDepth <= 0.0 || centerDepth >= kCnaSkyDepth) {
        FragColor = vec4(sourceColor, 1.0);
        return;
    }

    vec4 normalTexel = texture(uNormalSampler, cnaSnapToTexel(TexCoord, uDepthSize));
    // Roughness rides in the normal target's alpha (MOD-2003). A surface the prepass was never told
    // about reads 0 and reflects sharply, which is what this pass did before roughness existed.
    float roughness = clamp(normalTexel.a, 0.0, 1.0);
    vec3 rawNormal = normalTexel.xyz * 2.0 - 1.0;
    // normalize(vec3(0)) is NaN and every comparison against NaN is false, so a degenerate normal
    // would not produce a wrong reflection -- it would produce no reflection anywhere, with nothing
    // to point at. Guarded for the same reason SsaoPass guards its own.
    vec3 normal = length(rawNormal) > 1e-4 ? normalize(rawNormal) : vec3(0.0, 0.0, 1.0);

    vec3 position = cnaViewPositionFromDepth(TexCoord, centerDepth, uInverseProjection);
    vec3 incident = length(position) > 1e-6 ? normalize(position) : vec3(0.0, 0.0, -1.0);
    vec3 reflected = normalize(reflect(incident, normal));

    float stepLength = uMaxDistance / float(uStepCount);
    vec3 hitColor = vec3(0.0);
    float hit = 0.0;
    // The last point known to be in *front* of whatever the ray is passing over. Half of the
    // refinement's bracket, and the reason the loop starts its walk from the surface itself.
    vec3 lastClear = position;

    for (int i = 1; i <= 64; ++i) {
        if (i > uStepCount) break;

        vec3 samplePosition = position + reflected * (stepLength * float(i));
        // Behind the eye: there is no screen position for this point, and projecting it would
        // produce one anyway -- mirrored through the origin -- which reads as a plausible hit.
        if (samplePosition.z >= -1e-6) break;

        vec4 clip = uCameraProjection * vec4(samplePosition * uFarPlaneScale, 1.0);
        if (clip.w <= 0.0) break;
        vec2 sampleUv = (clip.xy / clip.w) * 0.5 + 0.5;
        if (sampleUv.x < 0.0 || sampleUv.x > 1.0 || sampleUv.y < 0.0 || sampleUv.y > 1.0) break;

        vec2 snappedUv = cnaSnapToTexel(sampleUv, uDepthSize);
        float sceneDepth = cnaDecodeLinearDepth(textureLod(uDepthSampler, snappedUv, 0.0));
        // The sky is not an occluder: a ray passing over empty frame must keep going rather than
        // stop against the far plane, which is behind everything and would swallow every ray.
        if (sceneDepth <= 0.0 || sceneDepth >= kCnaSkyDepth) continue;

        // Two tolerances, and they guard opposite errors.
        //
        // uDepthBias is the lower one, and without it the pass reflects nothing but itself: the
        // first step of a ray leaving a flat surface is still level with that surface, so the
        // difference is zero in exact arithmetic and a few ULPs either side of it in real
        // arithmetic. Half the time that is positive, the pixel "hits" the surface it started from,
        // and every mirror in the scene shows its own colour. It is the same trade the shadow bias
        // makes: too small and a surface self-reflects, too large and a real reflection close to
        // the surface -- the contact where an object meets the floor -- is the one that goes
        // missing.
        //
        // uThickness is the upper one. The depth image records where a surface is and nothing about
        // how deep the object behind it goes, so this stands in for the thickness it would have
        // had. Without it a ray that flew far behind a foreground object would report a hit on it.
        float difference = -samplePosition.z - sceneDepth;
        if (difference > uDepthBias && difference < uThickness) {
            // The march has established a bracket: lastClear is in front of the surface and
            // samplePosition is behind it, so the crossing is somewhere between. Without this the
            // reflection lands wherever the step happened to fall, so its position depends on the
            // step count -- a coarse march stair-steps every reflected edge, and halving the step
            // moves the whole reflection rather than sharpening it.
            vec3 near = lastClear;
            vec3 far  = samplePosition;
            for (int k = 0; k < kCnaRefineSteps; ++k) {
                vec3 middle = (near + far) * 0.5;
                vec4 middleClip = uCameraProjection * vec4(middle * uFarPlaneScale, 1.0);
                if (middleClip.w <= 0.0) break;
                vec2 middleUv = cnaSnapToTexel((middleClip.xy / middleClip.w) * 0.5 + 0.5, uDepthSize);
                float middleDepth = cnaDecodeLinearDepth(textureLod(uDepthSampler, middleUv, 0.0));
                bool behind = middleDepth > 0.0 && middleDepth < kCnaSkyDepth &&
                              (-middle.z - middleDepth) > uDepthBias;
                if (behind) far = middle; else near = middle;
            }

            vec4 hitClip = uCameraProjection * vec4(far * uFarPlaneScale, 1.0);
            vec2 hitUv = cnaSnapToTexel((hitClip.xy / hitClip.w) * 0.5 + 0.5, uDepthSize);

            // The ray may have arrived at the *back* of what it hit. A surface whose normal points
            // the same way the ray is travelling is one the ray came up behind, and reflecting its
            // front-facing colour puts the far side of an object into a mirror that cannot see it.
            vec3 rawHitNormal = texture(uNormalSampler, hitUv).xyz * 2.0 - 1.0;
            vec3 hitNormal = length(rawHitNormal) > 1e-4 ? normalize(rawHitNormal) : vec3(0.0, 0.0, 1.0);
            if (dot(reflected, hitNormal) > 0.0) break;

            // Nothing outside the viewport was ever drawn, so a reflection that ends near the border
            // is about to reflect information the frame does not have. Fading it out is the
            // difference between a reflection that thins away and one that stops along a hard line
            // down the edge of the screen -- the giveaway of the technique.
            vec2 toEdge = min(hitUv, vec2(1.0) - hitUv);
            float fade = uEdgeFade > 0.0
                ? min(smoothstep(0.0, uEdgeFade, toEdge.x), smoothstep(0.0, uEdgeFade, toEdge.y))
                : 1.0;

            // A rough surface does not reflect one point, it reflects a cone. Four taps around the
            // hit, widened by roughness, stand in for that: not the integral a real BRDF asks for,
            // but the difference between a mirror and a brushed floor, which is what the roughness
            // is describing. The taps are deliberately *not* snapped to texel centres -- filtering
            // is the point here, where for the depth comparison it was the bug.
            vec2 blur = vec2(roughness * uRoughnessBlur);
            vec3 gathered = texture(texture1, hitUv).rgb
                          + texture(texture1, hitUv + vec2( blur.x, 0.0)).rgb
                          + texture(texture1, hitUv + vec2(-blur.x, 0.0)).rgb
                          + texture(texture1, hitUv + vec2(0.0,  blur.y)).rgb
                          + texture(texture1, hitUv + vec2(0.0, -blur.y)).rgb;
            hitColor = gathered * 0.2;
            hit = fade;
            break;
        }

        lastClear = samplePosition;
    }

    FragColor = vec4(mix(sourceColor, hitColor, hit * uIntensity), 1.0);
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

    SsrPass::SsrPass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
    {
        // The same question DepthNormalPrepass asks to decide how it *writes* the depth, asked here
        // to decide how to read it. A pass cannot be told: PostProcessContext carries the texture
        // and not the prepass that produced it, and guessing wrong does not fail -- it reads the
        // finest fraction of a packed value as if it were the whole depth.
        const bool packed =
            !device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HalfSingle);
        effect_ = std::make_unique<ShaderEffect>(device, kVertexSource,
                                                 MakeFragmentSource(packed));
        bool logged = false;
        detail::reportShaderCompileFailure(device, "SsrPass", effect_.get(), logged);
    }

    SsrPass::~SsrPass() = default;

    void SsrPass::apply(const PostProcessContext& context)
    {
        // A camera is as much an input as the two images: without a projection there is no view
        // space to march in, and a pass that guessed one would reflect the scene through an
        // invented lens. The pipeline supplies it from `setCamera`; a caller who never called that
        // gets its frame back rather than a wrong reflection.
        const bool haveInputs = context.sourceDepth != nullptr &&
                                context.sourceNormals != nullptr && context.farPlane > 0.0f;
        if (effect_ == nullptr || !effect_->IsEffectValid() || !haveInputs)
        {
            fullscreen_->draw(context.source, context.destination, nullptr,
                              context.width, context.height);
            return;
        }

        const float far = context.farPlane;

        // The settings bag wins where one is supplied, matching every other pass: a pipeline that
        // applied a quality preset must not be overruled by a pass-local default nobody set.
        const RenderPipelineSettings* settings = context.settings;
        const float maxDistance = settings != nullptr ? settings->getSSRMaxDistance() : maxDistance_;
        const float thickness   = settings != nullptr ? settings->getSSRThickness()   : thickness_;
        const float depthBias   = settings != nullptr ? settings->getSSRDepthBias()   : depthBias_;
        const float edgeFade    = settings != nullptr ? settings->getSSREdgeFade()    : edgeFade_;
        const float roughnessBlur =
            settings != nullptr ? settings->getSSRRoughnessBlur() : roughnessBlur_;
        const float intensity   = settings != nullptr ? settings->getSSRIntensity()   : intensity_;
        const int   stepCount   = settings != nullptr ? settings->getSSRStepCount()   : stepCount_;

        effect_->Apply();
        effect_->SetUniformMat4("uCameraProjection", &context.projection.M11);
        effect_->SetUniformMat4("uInverseProjection", &context.inverseProjection.M11);
        effect_->SetUniformInt("uDepthSampler", 1);
        effect_->SetTexture(1, *context.sourceDepth);
        effect_->SetUniformInt("uNormalSampler", 2);
        effect_->SetTexture(2, *context.sourceNormals);

        // Every distance crosses into the scaled view space here and nowhere else.
        effect_->SetUniformVec2("uDepthSize", static_cast<float>(context.width),
                                static_cast<float>(context.height));
        effect_->SetUniformFloat("uFarPlaneScale", far);
        effect_->SetUniformFloat("uMaxDistance", maxDistance / far);
        effect_->SetUniformFloat("uDepthBias", depthBias / far);
        effect_->SetUniformFloat("uThickness", thickness / far);
        effect_->SetUniformFloat("uIntensity", intensity);
        effect_->SetUniformFloat("uEdgeFade", edgeFade);
        effect_->SetUniformFloat("uRoughnessBlur", roughnessBlur);
        effect_->SetUniformInt("uStepCount",
                               std::clamp(stepCount, kMinStepCount, kMaxStepCount));

        fullscreen_->draw(context.source, context.destination, effect_.get(),
                          context.width, context.height);
    }

    const std::string& SsrPass::getName() const
    {
        static const std::string name = "SSR";
        return name;
    }

    bool SsrPass::isSupported(GraphicsDevice& device) const
    {
        return PostProcessPass::isSupported(device) && effect_ && effect_->IsEffectValid();
    }

    float SsrPass::getMaxDistance() const { return maxDistance_; }
    void  SsrPass::setMaxDistance(const float value)
    {
        if (value > 0.0f) maxDistance_ = value;
    }

    int  SsrPass::getStepCount() const { return stepCount_; }
    void SsrPass::setStepCount(const int value) { stepCount_ = value; }

    float SsrPass::getThickness() const { return thickness_; }
    void  SsrPass::setThickness(const float value)
    {
        if (value > 0.0f) thickness_ = value;
    }

    float SsrPass::getDepthBias() const { return depthBias_; }
    void  SsrPass::setDepthBias(const float value)
    {
        if (value > 0.0f) depthBias_ = value;
    }

    float SsrPass::getRoughnessBlur() const { return roughnessBlur_; }
    void  SsrPass::setRoughnessBlur(const float value)
    {
        roughnessBlur_ = std::clamp(value, 0.0f, 0.25f);
    }

    float SsrPass::getEdgeFade() const { return edgeFade_; }
    void  SsrPass::setEdgeFade(const float value)
    {
        edgeFade_ = std::clamp(value, 0.0f, 0.5f);
    }

    float SsrPass::getIntensity() const { return intensity_; }
    void  SsrPass::setIntensity(const float value) { intensity_ = value; }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

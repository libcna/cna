// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/VolumetricFogPass.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/ShadowMap.hpp"
#include "LensPassVertexSource.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <algorithm>
#include <string>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::DepthFormat;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

    namespace {

        constexpr const char* kVertexSource = detail::kLensVertexSource;

        // Shared by both programs: where a froxel is, and how the atlas is laid out. Written once
        // so the build pass and the resolve pass cannot disagree about which pixel is which froxel,
        // which is the failure a slice atlas makes easy and silent.
        constexpr const char* kAtlasGlsl = R"(
uniform float uSliceCount;
uniform float uSliceResolution;

/// Which slice an atlas u belongs to, and where inside it.
void cnaAtlasSplit(vec2 atlasUv, out float slice, out vec2 inside) {
    float scaled = atlasUv.x * uSliceCount;
    slice  = min(floor(scaled), uSliceCount - 1.0);
    inside = vec2(scaled - slice, atlasUv.y);
}

/// The atlas coordinate of a slice's interior point, half a texel in from each end so the outermost
/// texels are sampled at their centres rather than averaged with the neighbouring slice.
vec2 cnaAtlasJoin(float slice, vec2 inside) {
    float sliceWidth = 1.0 / uSliceCount;
    float texelWidth = sliceWidth / uSliceResolution;
    float u = slice * sliceWidth + 0.5 * texelWidth + inside.x * texelWidth * (uSliceResolution - 1.0);
    return vec2(u, inside.y);
}

/// Slices are spaced so the near ones are thinner: a froxel far away covers more world, which is
/// what keeps the useful resolution near the camera where the eye is.
float cnaSliceDepth(float slice, float range) {
    float t = (slice + 0.5) / uSliceCount;
    return range * t * t;
}
)";

        constexpr const char* kBuildBody = R"(
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;          // unused by the build; FullscreenPass needs a source
uniform sampler2D uShadowSampler;
uniform mat4  uInverseProjection;
uniform mat4  uInverseView;
uniform mat4  uLightViewProjection;
uniform vec3  uLightDirection;
uniform vec3  uLightColor;
uniform float uDensity;
uniform float uAnisotropy;
uniform float uRange;
uniform int   uHasShadowMap;

// Henyey-Greenstein. Forward-biased scattering is why fog glows towards the sun and not away from
// it, and it is the one cue that separates airlight from a grey wash.
float cnaPhase(float cosAngle) {
    float g = uAnisotropy;
    float gg = g * g;
    float d = 1.0 + gg - 2.0 * g * cosAngle;
    return (1.0 - gg) / (12.566370614 * max(pow(max(d, 1e-4), 1.5), 1e-4));
}

float cnaLitFraction(vec3 world) {
    if (uHasShadowMap == 0) return 1.0;
    vec4 lightClip = uLightViewProjection * vec4(world, 1.0);
    if (lightClip.w <= 0.0) return 1.0;
    vec3 lightNdc = lightClip.xyz / lightClip.w;
    vec2 lightUv = lightNdc.xy * 0.5 + 0.5;
    // Outside the map is not "in shadow": the map covers what the light was fitted to, and treating
    // everything beyond it as occluded would put a hard dark box around the lit region.
    if (lightUv.x < 0.0 || lightUv.x > 1.0 || lightUv.y < 0.0 || lightUv.y > 1.0) return 1.0;
    float stored = texture(uShadowSampler, lightUv).r;
    float here = lightNdc.z * 0.5 + 0.5;
    return here - 0.002 > stored ? 0.0 : 1.0;
}

void main() {
    float slice;
    vec2 inside;
    cnaAtlasSplit(TexCoord, slice, inside);

    float depth = cnaSliceDepth(slice, uRange);
    // The froxel's own view ray, from the slice's position inside the frustum.
    vec4 ray = uInverseProjection * vec4(inside * 2.0 - 1.0, 1.0, 1.0);
    vec3 direction = ray.xyz / ray.w;
    vec3 viewPosition = direction * (depth / max(-direction.z, 1e-6));
    vec4 world = uInverseView * vec4(viewPosition, 1.0);
    vec4 cameraWorld = uInverseView * vec4(0.0, 0.0, 0.0, 1.0);

    vec3 toCamera = normalize(cameraWorld.xyz - world.xyz);
    // The scattering angle is between the direction the light *travels* and the direction to the
    // viewer. Light coming towards the camera means looking into it, which is where forward
    // scattering makes fog glow -- negating this computes the backward lobe and makes a view into
    // the sun the darkest one in the scene.
    float phase = cnaPhase(dot(normalize(uLightDirection), toCamera));

    float lit = cnaLitFraction(world.xyz);
    // In-scattering at this froxel, times how much of the medium is in front of it. The extinction
    // to the camera is applied at resolve, where the pixel's own depth is known.
    vec3 scattered = uLightColor * lit * phase * uDensity;

    FragColor = vec4(scattered, uDensity);
}
)";

        constexpr const char* kResolveBody = R"(
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;          // the frame
uniform sampler2D uDepthSampler;
uniform sampler2D uVolumeSampler;
uniform float uFarPlane;
uniform float uRange;

void main() {
    vec4 source = texture(texture1, TexCoord);
    float depth = cnaDecodeLinearDepth(texture(uDepthSampler, TexCoord));
    float travelled = (depth <= 0.0 || depth >= 0.999) ? uRange : min(depth * uFarPlane, uRange);

    // Walk the slices in front of this pixel and sum what each contributed, thinning what is
    // already accumulated as it passes through the medium in between. Marching here rather than
    // prefix-summing in the atlas is what keeps the build to a single fullscreen draw.
    vec3 scattered = vec3(0.0);
    float transmittance = 1.0;
    float previousDepth = 0.0;
    for (int i = 0; i < 64; ++i) {
        if (float(i) >= uSliceCount) break;
        float sliceDepth = cnaSliceDepth(float(i), uRange);
        if (sliceDepth > travelled) break;
        float thickness = sliceDepth - previousDepth;
        previousDepth = sliceDepth;

        vec4 froxel = texture(uVolumeSampler, cnaAtlasJoin(float(i), TexCoord));
        float extinction = froxel.a * thickness;
        scattered += froxel.rgb * thickness * transmittance;
        transmittance *= exp(-extinction);
    }

    FragColor = vec4(source.rgb * transmittance + scattered, source.a);
}
)";

        std::string MakeBuildSource()
        {
            return std::string("#version 300 es\nprecision highp float;\n") + kAtlasGlsl + kBuildBody;
        }

        std::string MakeResolveSource(const bool packedDepth)
        {
            std::string source = "#version 300 es\nprecision highp float;\n";
            source += DepthNormalPrepass::getDepthDecodeGlsl(packedDepth);
            source += kAtlasGlsl;
            source += kResolveBody;
            return source;
        }

    } // namespace

    VolumetricFogPass::VolumetricFogPass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device)), pool_(device)
    {
        const bool packed =
            !device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HalfSingle);
        buildEffect_   = std::make_unique<ShaderEffect>(device, kVertexSource, MakeBuildSource());
        resolveEffect_ = std::make_unique<ShaderEffect>(device, kVertexSource,
                                                        MakeResolveSource(packed));
        bool logged = false;
        detail::reportShaderCompileFailure(device, "VolumetricFogPass (build)", buildEffect_.get(),
                                           logged);
        detail::reportShaderCompileFailure(device, "VolumetricFogPass (resolve)",
                                           resolveEffect_.get(), logged);
    }

    VolumetricFogPass::~VolumetricFogPass() = default;

    void VolumetricFogPass::setLight(ShadowMap* shadowMap, const Vector3& lightDirection,
                                     const Vector3& lightColor)
    {
        shadowMap_      = shadowMap;
        lightDirection_ = lightDirection;
        lightColor_     = lightColor;
    }

    void VolumetricFogPass::apply(const PostProcessContext& context)
    {
        const RenderPipelineSettings* settings = context.settings;
        const float density = settings != nullptr ? settings->getVolumetricFogDensity() : density_;

        const bool ready = buildEffect_ && buildEffect_->IsEffectValid()
                        && resolveEffect_ && resolveEffect_->IsEffectValid()
                        && context.sourceDepth != nullptr && context.farPlane > 0.0f;
        if (!ready || density <= 0.0f)
        {
            fullscreen_->draw(context.source, context.destination, nullptr,
                              context.width, context.height);
            return;
        }

        RenderTarget2D* volume = pool_.acquire(kSliceCount * kSliceResolution, kSliceResolution,
                                               SurfaceFormat::Color, DepthFormat::None, 0);

        buildEffect_->Apply();
        buildEffect_->SetUniformFloat("uSliceCount", static_cast<float>(kSliceCount));
        buildEffect_->SetUniformFloat("uSliceResolution", static_cast<float>(kSliceResolution));
        buildEffect_->SetUniformMat4("uInverseProjection", &context.inverseProjection.M11);
        buildEffect_->SetUniformMat4("uInverseView", &context.inverseView.M11);
        buildEffect_->SetUniformVec3("uLightDirection", lightDirection_.X, lightDirection_.Y,
                                     lightDirection_.Z);
        buildEffect_->SetUniformVec3("uLightColor", lightColor_.X, lightColor_.Y, lightColor_.Z);
        buildEffect_->SetUniformFloat("uDensity", density);
        buildEffect_->SetUniformFloat("uAnisotropy", anisotropy_);
        buildEffect_->SetUniformFloat("uRange", range_);

        const bool haveShadow = shadowMap_ != nullptr && shadowMap_->getShadowTexture() != nullptr;
        buildEffect_->SetUniformInt("uHasShadowMap", haveShadow ? 1 : 0);
        if (haveShadow)
        {
            const auto lightViewProjection = shadowMap_->getLightViewProjection();
            buildEffect_->SetUniformMat4("uLightViewProjection", &lightViewProjection.M11);
            buildEffect_->SetUniformInt("uShadowSampler", 1);
            buildEffect_->SetTexture(1, *shadowMap_->getShadowTexture());
        }

        fullscreen_->draw(context.source, volume, buildEffect_.get(),
                          kSliceCount * kSliceResolution, kSliceResolution);

        resolveEffect_->Apply();
        resolveEffect_->SetUniformFloat("uSliceCount", static_cast<float>(kSliceCount));
        resolveEffect_->SetUniformFloat("uSliceResolution", static_cast<float>(kSliceResolution));
        resolveEffect_->SetUniformInt("uDepthSampler", 1);
        resolveEffect_->SetTexture(1, *context.sourceDepth);
        resolveEffect_->SetUniformInt("uVolumeSampler", 2);
        resolveEffect_->SetTexture(2, *volume);
        resolveEffect_->SetUniformFloat("uFarPlane", context.farPlane);
        resolveEffect_->SetUniformFloat("uRange", range_);

        fullscreen_->draw(context.source, context.destination, resolveEffect_.get(),
                          context.width, context.height);
    }

    const std::string& VolumetricFogPass::getName() const
    {
        static const std::string name = "VolumetricFog";
        return name;
    }

    bool VolumetricFogPass::isSupported(GraphicsDevice& device) const
    {
        return PostProcessPass::isSupported(device)
            && buildEffect_ && buildEffect_->IsEffectValid()
            && resolveEffect_ && resolveEffect_->IsEffectValid();
    }

    float VolumetricFogPass::getDensity() const { return density_; }
    void  VolumetricFogPass::setDensity(const float value) { if (value >= 0.0f) density_ = value; }

    float VolumetricFogPass::getAnisotropy() const { return anisotropy_; }
    void  VolumetricFogPass::setAnisotropy(const float value)
    {
        anisotropy_ = std::clamp(value, -0.95f, 0.95f);
    }

    float VolumetricFogPass::getRange() const { return range_; }
    void  VolumetricFogPass::setRange(const float value) { if (value > 0.0f) range_ = value; }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

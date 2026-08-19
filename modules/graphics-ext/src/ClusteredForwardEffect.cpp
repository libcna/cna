// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/ClusteredForwardEffect.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/ClusteredLightBuffer.hpp"
#include "CNA/Graphics/ClusteredLightEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;

    namespace {

        constexpr const char* kVertexSource = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
uniform mat4 uWorld;
uniform mat4 uView;
uniform mat4 uProjection;
out vec3  vWorldPosition;
out vec3  vWorldNormal;
out vec4  vClipPosition;
out float vViewDistance;
void main() {
    vec4 world = uWorld * vec4(aPosition, 1.0);
    vec4 view  = uView * world;
    gl_Position = uProjection * view;

    vWorldPosition = world.xyz;
    // The upper 3x3 is the normal matrix only for a uniformly scaled world; the prepass documents
    // the same limitation, and correcting it needs an inverse per draw nothing else here pays for.
    vWorldNormal   = mat3(uWorld) * aNormal;
    vClipPosition  = gl_Position;
    vViewDistance  = -view.z;
}
)";

        // The reflectance model is the same GGX / Smith / Schlick trio the rest of this layer uses,
        // and the falloff is glTF's windowed inverse square: physical up to the range, and exactly
        // zero at it, which is what makes a light's cluster assignment honest -- a light that faded
        // asymptotically would still be contributing outside the sphere it was sorted by.
        constexpr const char* kShadingGlsl = R"(
const float kCnaPi = 3.14159265359;

float cnaDistribution(float NoH, float roughness) {
    float a = roughness * roughness;
    float aa = a * a;
    float d = NoH * NoH * (aa - 1.0) + 1.0;
    return aa / max(kCnaPi * d * d, 1e-7);
}

float cnaGeometry(float NoV, float NoL, float roughness) {
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float gv = NoV / max(NoV * (1.0 - k) + k, 1e-7);
    float gl = NoL / max(NoL * (1.0 - k) + k, 1e-7);
    return gv * gl;
}

vec3 cnaFresnel(float VoH, vec3 f0) {
    return f0 + (vec3(1.0) - f0) * pow(clamp(1.0 - VoH, 0.0, 1.0), 5.0);
}

float cnaFalloff(float distance, float range) {
    float ratio = distance / max(range, 1e-4);
    float window = clamp(1.0 - ratio * ratio * ratio * ratio, 0.0, 1.0);
    return window * window / max(distance * distance, 1e-4);
}

vec3 cnaShade(CnaClusteredLight light, vec3 surface, vec3 normal, vec3 viewDirection,
              vec3 baseColor, float metallic, float roughness) {
    vec3 toLight = light.position - surface;
    float distance = length(toLight);
    if (distance >= light.range || distance <= 0.0) return vec3(0.0);
    vec3 L = toLight / distance;

    float attenuation = cnaFalloff(distance, light.range);
    if (light.isSpot > 0.5) {
        // The cone, measured from the light outwards, so -L is the direction the light travels.
        float cosAngle = dot(-L, light.direction);
        attenuation *= clamp((cosAngle - light.cosOuter) / max(light.cosInner - light.cosOuter, 1e-4),
                             0.0, 1.0);
    }
    if (attenuation <= 0.0) return vec3(0.0);

    vec3 H = normalize(L + viewDirection);
    float NoL = max(dot(normal, L), 0.0);
    float NoV = max(dot(normal, viewDirection), 1e-4);
    float NoH = max(dot(normal, H), 0.0);
    float VoH = max(dot(viewDirection, H), 0.0);
    if (NoL <= 0.0) return vec3(0.0);

    vec3 f0 = mix(vec3(0.04), baseColor, metallic);
    vec3 fresnel = cnaFresnel(VoH, f0);
    vec3 specular = fresnel * cnaDistribution(NoH, roughness) * cnaGeometry(NoV, NoL, roughness)
                  / max(4.0 * NoV * NoL, 1e-7);
    vec3 diffuse = (vec3(1.0) - fresnel) * (1.0 - metallic) * baseColor / kCnaPi;

    return (diffuse + specular) * light.colour * attenuation * NoL;
}
)";

        constexpr const char* kFragmentBody = R"(
in vec3  vWorldPosition;
in vec3  vWorldNormal;
in vec4  vClipPosition;
in float vViewDistance;
out vec4 FragColor;

uniform vec3  uCameraPosition;
uniform vec3  uBaseColor;
uniform vec3  uAmbient;
uniform float uMetallic;
uniform float uRoughness;

void main() {
    vec3 normal = normalize(vWorldNormal);
    vec3 viewDirection = normalize(uCameraPosition - vWorldPosition);
    vec2 ndc = vClipPosition.xy / max(abs(vClipPosition.w), 1e-6) * sign(vClipPosition.w);

    int cluster = cnaClusterFromNdc(ndc, vViewDistance);
    int count = cnaClusterLightCount(cluster);

    vec3 colour = uAmbient * uBaseColor;
    for (int i = 0; i < kCnaMaxLightsPerFragment; ++i) {
        if (i >= count) break;
        CnaClusteredLight light = cnaLoadLight(cnaClusterLightIndex(cluster, i));
        colour += cnaShade(light, vWorldPosition, normal, viewDirection, uBaseColor, uMetallic,
                           uRoughness);
    }
    FragColor = vec4(colour, 1.0);
}
)";

        std::string MakeFragmentSource()
        {
            std::string source = "#version 300 es\nprecision highp float;\n";
            source += "const int kCnaMaxLightsPerFragment = " +
                      std::to_string(ClusteredForwardEffect::kMaxLightsPerFragment) + ";\n";
            source += ClusteredLightBuffer::getLightLookupGlsl();
            source += kShadingGlsl;
            source += kFragmentBody;
            return source;
        }

        float Dot(const Vector3& a, const Vector3& b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }

        Vector3 Normalized(const Vector3& v)
        {
            const float length = std::sqrt(Dot(v, v));
            if (!(length > 1e-6f)) return Vector3(0.0f, 0.0f, 0.0f);
            return Vector3(v.X / length, v.Y / length, v.Z / length);
        }

    } // namespace

    ClusteredForwardEffect::ClusteredForwardEffect(GraphicsDevice& device)
    {
        effect_ = std::make_unique<ShaderEffect>(device, kVertexSource, MakeFragmentSource());
        bool logged = false;
        detail::reportShaderCompileFailure(device, "ClusteredForwardEffect", effect_.get(), logged);
        supported_ = effect_ != nullptr && effect_->IsEffectValid() &&
                     device.ExecutesShaderEffectSourceEXT();
    }

    ClusteredForwardEffect::~ClusteredForwardEffect() = default;

    bool ClusteredForwardEffect::isSupported() const { return supported_; }

    void ClusteredForwardEffect::begin(const Matrix& world, const Matrix& view,
                                       const Matrix& projection, const Vector3& cameraPosition,
                                       const ClusteredLightBuffer& lights)
    {
        if (!lights.isUploaded())
            throw std::runtime_error(
                "CNA::Graphics::ClusteredForwardEffect::begin: the light buffer holds nothing, so "
                "there is no cluster table for the shader to walk");
        if (effect_ == nullptr || !effect_->IsEffectValid()) return;

        effect_->Apply();
        effect_->SetUniformMat4("uWorld", &world.M11);
        effect_->SetUniformMat4("uView", &view.M11);
        effect_->SetUniformMat4("uProjection", &projection.M11);
        effect_->SetUniformVec3("uCameraPosition", cameraPosition.X, cameraPosition.Y,
                                cameraPosition.Z);
        effect_->SetUniformVec3("uBaseColor", baseColor_.X, baseColor_.Y, baseColor_.Z);
        effect_->SetUniformVec3("uAmbient", ambient_.X, ambient_.Y, ambient_.Z);
        effect_->SetUniformFloat("uMetallic", metallic_);
        effect_->SetUniformFloat("uRoughness", roughness_);

        lights.bind(*effect_, 1);
    }

    ShaderEffect* ClusteredForwardEffect::getEffect() const { return effect_.get(); }

    Vector3 ClusteredForwardEffect::getBaseColor() const { return baseColor_; }
    void    ClusteredForwardEffect::setBaseColor(const Vector3& value)
    {
        baseColor_ = Vector3(std::clamp(value.X, 0.0f, 1.0f), std::clamp(value.Y, 0.0f, 1.0f),
                             std::clamp(value.Z, 0.0f, 1.0f));
    }

    float ClusteredForwardEffect::getMetallic() const { return metallic_; }
    void  ClusteredForwardEffect::setMetallic(const float value)
    {
        metallic_ = std::clamp(value, 0.0f, 1.0f);
    }

    float ClusteredForwardEffect::getRoughness() const { return roughness_; }
    void  ClusteredForwardEffect::setRoughness(const float value)
    {
        // The floor is not a taste: a perfectly smooth microfacet distribution is a division by
        // zero in the specular term, and the visible result is a single blown-out pixel.
        roughness_ = std::clamp(value, 0.04f, 1.0f);
    }

    Vector3 ClusteredForwardEffect::getAmbient() const { return ambient_; }
    void    ClusteredForwardEffect::setAmbient(const Vector3& value)
    {
        ambient_ = Vector3(std::max(value.X, 0.0f), std::max(value.Y, 0.0f),
                           std::max(value.Z, 0.0f));
    }

    Vector3 ClusteredForwardEffect::contribution(const ClusteredLightEXT& light,
                                                 const Vector3& surface, const Vector3& normal,
                                                 const Vector3& cameraPosition,
                                                 const Vector3& baseColor, const float metallic,
                                                 const float roughness)
    {
        const Vector3 toLight(light.Position.X - surface.X, light.Position.Y - surface.Y,
                              light.Position.Z - surface.Z);
        const float distance = std::sqrt(Dot(toLight, toLight));
        if (distance >= light.Range || distance <= 0.0f) return Vector3(0.0f, 0.0f, 0.0f);

        const Vector3 L(toLight.X / distance, toLight.Y / distance, toLight.Z / distance);

        const float ratio = distance / std::max(light.Range, 1e-4f);
        const float window = std::clamp(1.0f - ratio * ratio * ratio * ratio, 0.0f, 1.0f);
        float attenuation = window * window / std::max(distance * distance, 1e-4f);

        if (light.Type == ClusteredLightType::Spot)
        {
            const Vector3 direction = Normalized(light.Direction);
            const float cosAngle = -Dot(L, direction);
            const float cosOuter = std::cos(light.OuterAngle);
            const float cosInner = std::cos(light.InnerAngle);
            attenuation *= std::clamp((cosAngle - cosOuter) / std::max(cosInner - cosOuter, 1e-4f),
                                      0.0f, 1.0f);
        }
        if (attenuation <= 0.0f) return Vector3(0.0f, 0.0f, 0.0f);

        const Vector3 viewDirection = Normalized(Vector3(cameraPosition.X - surface.X,
                                                         cameraPosition.Y - surface.Y,
                                                         cameraPosition.Z - surface.Z));
        const Vector3 H = Normalized(Vector3(L.X + viewDirection.X, L.Y + viewDirection.Y,
                                             L.Z + viewDirection.Z));

        const float NoL = std::max(Dot(normal, L), 0.0f);
        const float NoV = std::max(Dot(normal, viewDirection), 1e-4f);
        const float NoH = std::max(Dot(normal, H), 0.0f);
        const float VoH = std::max(Dot(viewDirection, H), 0.0f);
        if (NoL <= 0.0f) return Vector3(0.0f, 0.0f, 0.0f);

        const float a = roughness * roughness;
        const float aa = a * a;
        const float d = NoH * NoH * (aa - 1.0f) + 1.0f;
        const float distributionTerm = aa / std::max(3.14159265359f * d * d, 1e-7f);

        const float k = (roughness + 1.0f) * (roughness + 1.0f) / 8.0f;
        const float geometryTerm = (NoV / std::max(NoV * (1.0f - k) + k, 1e-7f)) *
                                   (NoL / std::max(NoL * (1.0f - k) + k, 1e-7f));

        const float schlick = std::pow(std::clamp(1.0f - VoH, 0.0f, 1.0f), 5.0f);
        Vector3 result(0.0f, 0.0f, 0.0f);
        float* out = &result.X;
        const float* base = &baseColor.X;
        const float* emitted = &light.Color.X;
        for (int channel = 0; channel < 3; ++channel)
        {
            const float f0 = 0.04f + (base[channel] - 0.04f) * metallic;
            const float fresnel = f0 + (1.0f - f0) * schlick;
            const float specular = fresnel * distributionTerm * geometryTerm /
                                   std::max(4.0f * NoV * NoL, 1e-7f);
            const float diffuse = (1.0f - fresnel) * (1.0f - metallic) * base[channel] /
                                  3.14159265359f;
            out[channel] = (diffuse + specular) * emitted[channel] * light.Intensity *
                           attenuation * NoL;
        }
        return result;
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

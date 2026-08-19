// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/ClusteredForwardEffect.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/AreaLightBrdfTable.hpp"
#include "CNA/Graphics/AreaLightShading.hpp"
#include "CNA/Graphics/ClusteredLightBuffer.hpp"
#include "CNA/Graphics/ClusteredLightEXT.hpp"
#include "CNA/Graphics/PbrMaterialExtensions.hpp"
#include "Microsoft/Xna/Framework/Graphics/AreaLightEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::AreaLightEXT;
    using Microsoft::Xna::Framework::Graphics::AreaLightShapeEXT;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;
    using Microsoft::Xna::Framework::Graphics::Texture2D;

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

uniform float uClearcoat;
uniform float uClearcoatRoughness;
uniform vec3  uSheenColor;
uniform float uSheenRoughness;
uniform float uTransmission;
uniform float uThickness;
uniform float uAttenuationDistance;
uniform vec3  uAttenuationColor;
uniform float uIor;
uniform mat4  uViewProjection;
uniform sampler2D uOpaqueFrame;

// KHR_materials_sheen: the Charlie distribution with Ashikhmin's visibility. Its peak is where the
// half-vector is *perpendicular* to the normal, which is the opposite of a specular lobe -- that is
// why sheen appears as a rim at grazing angles and why no roughness on the base material produces
// it. The alpha floor is not cosmetic: the exponent is 1/alpha, so a small roughness gives a rim
// too tight to survive any sensible resolution.
float cnaSheenDistribution(float NoH, float roughness) {
    float alpha = max(roughness * roughness, 0.07);
    float inverseAlpha = 1.0 / alpha;
    float sinSquared = max(1.0 - NoH * NoH, 0.0078125);
    return (2.0 + inverseAlpha) * pow(sinSquared, inverseAlpha * 0.5) / 6.28318530718;
}

float cnaSheenVisibility(float NoV, float NoL) {
    return 1.0 / max(4.0 * (NoL + NoV - NoL * NoV), 1e-7);
}

vec3 cnaShade(CnaClusteredLight light, vec3 surface, vec3 normal, vec3 viewDirection,
              vec3 baseColor, float metallic, float roughness, out vec3 diffuseOut) {
    diffuseOut = vec3(0.0);
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
    // The diffuse term leaves separately, because KHR_materials_transmission replaces *it* with
    // what is behind the surface and leaves the highlights alone -- glass with no highlight is the
    // thing that stops looking like glass.
    diffuseOut = diffuse * light.colour * attenuation * NoL;
    vec3 layered = specular;

    if (uSheenColor.r + uSheenColor.g + uSheenColor.b > 0.0) {
        layered += uSheenColor * cnaSheenDistribution(NoH, uSheenRoughness)
                 * cnaSheenVisibility(NoV, NoL);
    }

    // KHR_materials_clearcoat: a second, thin specular layer over the whole material, with its own
    // roughness. Not a brighter highlight -- a *second* one. What it takes from the base layer is
    // exactly what it reflects, so a coat brightens the surface where it catches the light and
    // darkens it everywhere else, which is what makes lacquer look like lacquer.
    if (uClearcoat > 0.0) {
        float ccRoughness = max(uClearcoatRoughness, 0.04);
        float ccFresnel = 0.04 + 0.96 * pow(clamp(1.0 - VoH, 0.0, 1.0), 5.0);
        float ccSpecular = ccFresnel * cnaDistribution(NoH, ccRoughness)
                         * cnaGeometry(NoV, NoL, ccRoughness) / max(4.0 * NoV * NoL, 1e-7);
        layered = layered * (1.0 - uClearcoat * ccFresnel) + vec3(uClearcoat * ccSpecular);
    }

    return layered * light.colour * attenuation * NoL;
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

    vec3 diffuseSum = uAmbient * uBaseColor;
    diffuseSum += cnaAreaContribution(vWorldPosition, normal, viewDirection, uBaseColor, uMetallic,
                                      uRoughness);
    vec3 otherSum = vec3(0.0);
    for (int i = 0; i < kCnaMaxLightsPerFragment; ++i) {
        if (i >= count) break;
        CnaClusteredLight light = cnaLoadLight(cnaClusterLightIndex(cluster, i));
        vec3 lightDiffuse;
        otherSum += cnaShade(light, vWorldPosition, normal, viewDirection, uBaseColor, uMetallic,
                             uRoughness, lightDiffuse);
        diffuseSum += lightDiffuse;
    }

    if (uTransmission > 0.0) {
        // Refraction, not transparency: the ray bends entering the surface, travels the volume's
        // thickness, and leaves somewhere else -- so what shows through is *displaced*, which is
        // the whole visual difference from alpha blending. The exit point is projected back to
        // screen space to find it in the copy of the opaque frame.
        vec3 refracted = refract(-viewDirection, normal, 1.0 / max(uIor, 1.0));
        vec3 exitPoint = vWorldPosition + refracted * uThickness;
        vec4 exitClip = uViewProjection * vec4(exitPoint, 1.0);
        vec2 uv = exitClip.xy / max(abs(exitClip.w), 1e-4) * sign(exitClip.w) * 0.5 + 0.5;
        vec3 behind = texture(uOpaqueFrame, clamp(uv, 0.0, 1.0)).rgb;

        vec3 absorbed = vec3(1.0);
        if (uAttenuationDistance > 0.0 && uThickness > 0.0) {
            vec3 sigma = -log(clamp(uAttenuationColor, 1e-4, 1.0)) / uAttenuationDistance;
            absorbed = exp(-sigma * uThickness);
        }
        diffuseSum = mix(diffuseSum, behind * absorbed, uTransmission);
    }

    FragColor = vec4(diffuseSum + otherSum, 1.0);
}
)";

        std::string MakeFragmentSource()
        {
            std::string source = "#version 300 es\nprecision highp float;\n";
            source += "const int kCnaMaxLightsPerFragment = " +
                      std::to_string(ClusteredForwardEffect::kMaxLightsPerFragment) + ";\n";
            source += ClusteredLightBuffer::getLightLookupGlsl();
            source += AreaLightBrdfTable::getLookupGlsl();
            source += AreaLightShading::getShadingGlsl();
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
        effect_->SetUniformFloat("uClearcoat",
                                 extensions_ != nullptr ? extensions_->getClearcoatFactor() : 0.0f);
        effect_->SetUniformFloat("uClearcoatRoughness",
                                 extensions_ != nullptr ? extensions_->getClearcoatRoughness()
                                                        : 0.0f);
        const Vector3 sheen = extensions_ != nullptr ? extensions_->getSheenColorFactor()
                                                     : Vector3(0.0f, 0.0f, 0.0f);
        effect_->SetUniformVec3("uSheenColor", sheen.X, sheen.Y, sheen.Z);
        effect_->SetUniformFloat("uSheenRoughness",
                                 extensions_ != nullptr ? extensions_->getSheenRoughness() : 0.0f);

        const bool transmits = extensions_ != nullptr && extensions_->isTransmissionEnabled();
        if (transmits && opaqueFrame_ == nullptr)
            throw std::runtime_error(
                "CNA::Graphics::ClusteredForwardEffect::begin: this material transmits, and no copy "
                "of the opaque frame has been given to refract against. Refused rather than "
                "approximated: a transmissive material drawn without one is not slightly wrong, it "
                "is an opaque object where a glass one was asked for -- see setOpaqueFrame");
        effect_->SetUniformFloat("uTransmission",
                                 transmits ? extensions_->getTransmissionFactor() : 0.0f);
        effect_->SetUniformFloat("uIor", ior_);
        if (transmits)
        {
            effect_->SetUniformFloat("uThickness", extensions_->getThicknessFactor());
            effect_->SetUniformFloat("uAttenuationDistance",
                                     extensions_->getAttenuationDistance());
            const Vector3 attenuation = extensions_->getAttenuationColor();
            effect_->SetUniformVec3("uAttenuationColor", attenuation.X, attenuation.Y,
                                    attenuation.Z);
            const Matrix viewProjection = view * projection;
            effect_->SetUniformMat4("uViewProjection", &viewProjection.M11);
            effect_->SetUniformInt("uOpaqueFrame", 5);
            effect_->SetTexture(5, *opaqueFrame_);
        }

        lights.bind(*effect_, 1);

        const bool haveArea = areaLight_ != nullptr && areaTable_ != nullptr &&
                              areaTable_->getTexture() != nullptr;
        effect_->SetUniformInt("uAreaShape", haveArea ? static_cast<int>(areaLight_->Shape) : -1);
        if (haveArea)
        {
            effect_->SetUniformInt("uCnaAreaBrdf", 4);
            effect_->SetTexture(4, *areaTable_->getTexture());
            effect_->SetUniformFloat("uCnaAreaBrdfSize",
                                     static_cast<float>(areaTable_->getSize()));
            effect_->SetUniformVec3("uAreaPosition", areaLight_->Position.X, areaLight_->Position.Y,
                                    areaLight_->Position.Z);
            effect_->SetUniformVec3("uAreaRight", areaLight_->RightAxis.X, areaLight_->RightAxis.Y,
                                    areaLight_->RightAxis.Z);
            effect_->SetUniformVec3("uAreaUp", areaLight_->UpAxis.X, areaLight_->UpAxis.Y,
                                    areaLight_->UpAxis.Z);
            effect_->SetUniformVec3("uAreaColour", areaLight_->Color.X * areaLight_->Intensity,
                                    areaLight_->Color.Y * areaLight_->Intensity,
                                    areaLight_->Color.Z * areaLight_->Intensity);
            effect_->SetUniformFloat("uAreaRange", areaLight_->Range);
            effect_->SetUniformFloat("uAreaTwoSided", areaLight_->TwoSided ? 1.0f : 0.0f);
        }
    }

    void ClusteredForwardEffect::setAreaLight(const AreaLightEXT& light,
                                              const AreaLightBrdfTable& table)
    {
        if (!light.IsValidEXT())
        {
            clearAreaLight();
            return;
        }
        areaLight_ = std::make_unique<AreaLightEXT>(light);
        areaTable_ = &table;
    }

    void ClusteredForwardEffect::clearAreaLight()
    {
        areaLight_.reset();
        areaTable_ = nullptr;
    }

    bool ClusteredForwardEffect::hasAreaLight() const { return areaLight_ != nullptr; }

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

    void ClusteredForwardEffect::setMaterialExtensions(const PbrMaterialExtensions& extensions)
    {
        extensions_ = std::make_unique<PbrMaterialExtensions>(extensions);
    }

    const PbrMaterialExtensions& ClusteredForwardEffect::getMaterialExtensions() const
    {
        static const PbrMaterialExtensions neutral;
        return extensions_ != nullptr ? *extensions_ : neutral;
    }

    float ClusteredForwardEffect::getIor() const { return ior_; }
    void  ClusteredForwardEffect::setIor(const float value)
    {
        // Below 1 a surface would refract the wrong way; the vacuum is the floor of what a material
        // can be, not a value to interpolate through.
        if (value >= 1.0f) ior_ = value;
    }

    Texture2D* ClusteredForwardEffect::getOpaqueFrame() const { return opaqueFrame_; }
    void       ClusteredForwardEffect::setOpaqueFrame(Texture2D* frame) { opaqueFrame_ = frame; }

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
                                                 const float roughness,
                                                 const PbrMaterialExtensions& extensions)
    {
        return contribution(light, surface, normal, cameraPosition, baseColor, metallic, roughness,
                            extensions.getClearcoatFactor(), extensions.getClearcoatRoughness(),
                            extensions.getSheenColorFactor(), extensions.getSheenRoughness());
    }

    Vector3 ClusteredForwardEffect::contribution(const ClusteredLightEXT& light,
                                                 const Vector3& surface, const Vector3& normal,
                                                 const Vector3& cameraPosition,
                                                 const Vector3& baseColor, const float metallic,
                                                 const float roughness, const float clearcoat,
                                                 const float clearcoatRoughness,
                                                 const Vector3& sheenColor,
                                                 const float sheenRoughness)
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

        float sheenTerm = 0.0f;
        if (sheenColor.X > 0.0f || sheenColor.Y > 0.0f || sheenColor.Z > 0.0f)
        {
            const float sheenAlpha = std::max(sheenRoughness * sheenRoughness, 0.07f);
            const float inverseAlpha = 1.0f / sheenAlpha;
            const float sinSquared = std::max(1.0f - NoH * NoH, 0.0078125f);
            const float distribution = (2.0f + inverseAlpha) *
                                       std::pow(sinSquared, inverseAlpha * 0.5f) /
                                       6.28318530718f;
            const float visibility = 1.0f / std::max(4.0f * (NoL + NoV - NoL * NoV), 1e-7f);
            sheenTerm = distribution * visibility;
        }

        float clearcoatFresnel = 0.0f;
        float clearcoatSpecular = 0.0f;
        if (clearcoat > 0.0f)
        {
            const float ccRoughness = std::max(clearcoatRoughness, 0.04f);
            const float ccA = ccRoughness * ccRoughness;
            const float ccAA = ccA * ccA;
            const float ccD = NoH * NoH * (ccAA - 1.0f) + 1.0f;
            const float ccDistribution = ccAA / std::max(3.14159265359f * ccD * ccD, 1e-7f);
            const float ccK = (ccRoughness + 1.0f) * (ccRoughness + 1.0f) / 8.0f;
            const float ccGeometry = (NoV / std::max(NoV * (1.0f - ccK) + ccK, 1e-7f)) *
                                     (NoL / std::max(NoL * (1.0f - ccK) + ccK, 1e-7f));
            clearcoatFresnel = 0.04f + 0.96f * schlick;
            clearcoatSpecular = clearcoatFresnel * ccDistribution * ccGeometry /
                                std::max(4.0f * NoV * NoL, 1e-7f);
        }

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
            float layered = diffuse + specular + (&sheenColor.X)[channel] * sheenTerm;
            if (clearcoat > 0.0f)
                layered = layered * (1.0f - clearcoat * clearcoatFresnel) +
                          clearcoat * clearcoatSpecular;
            out[channel] = layered * emitted[channel] * light.Intensity * attenuation * NoL;
        }
        return result;
    }

    Vector3 ClusteredForwardEffect::volumeAttenuation(const Vector3& attenuationColor,
                                                      const float attenuationDistance,
                                                      const float thickness)
    {
        if (!(attenuationDistance > 0.0f) || !(thickness > 0.0f)) return Vector3(1.0f, 1.0f, 1.0f);

        Vector3 result(1.0f, 1.0f, 1.0f);
        float* out = &result.X;
        const float* colour = &attenuationColor.X;
        for (int channel = 0; channel < 3; ++channel)
        {
            const float clamped = std::clamp(colour[channel], 1e-4f, 1.0f);
            const float sigma = -std::log(clamped) / attenuationDistance;
            out[channel] = std::exp(-sigma * thickness);
        }
        return result;
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT

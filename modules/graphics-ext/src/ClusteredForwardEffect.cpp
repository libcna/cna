// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/ClusteredForwardEffect.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/AreaLightBrdfTable.hpp"
#include "CNA/Graphics/ClusteredLightBuffer.hpp"
#include "CNA/Graphics/ClusteredLightEXT.hpp"
#include "CNA/Graphics/LightProbeEXT.hpp"
#include "CNA/Graphics/LightProbeVolumeEXT.hpp"
#include "CNA/Graphics/PbrMaterialExtensions.hpp"
#include "CNA/Graphics/ShaderCodeEXT.hpp"
#include "CNA/Graphics/ShaderPackageEXT.hpp"
#include "CNA/Graphics/ThinFilmIridescence.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/AreaLightEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "shaders/clustered_forward/ClusteredForwardShaderPackage.generated.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::AreaLightEXT;
    using Microsoft::Xna::Framework::Graphics::AreaLightShapeEXT;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;
    using Microsoft::Xna::Framework::Graphics::Texture2D;

    namespace {

        [[nodiscard]] std::vector<std::uint8_t> ToBytes(
            const std::uint32_t* words, const std::size_t byteSize)
        {
            const auto* begin = reinterpret_cast<const std::uint8_t*>(words);
            return std::vector<std::uint8_t>(begin, begin + byteSize);
        }

        [[nodiscard]] ShaderPackageEXT CreateClusteredForwardPackage()
        {
            using namespace detail::ClusteredForwardGenerated;
            return ShaderPackageEXT(
                {
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslEs,
                                  CNA::ShaderStageEXT::Vertex, "main",
                                  "clustered_forward/forward.es.vert.glsl",
                                  std::string(kForwardEsVertexSource)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslEs,
                                  CNA::ShaderStageEXT::Fragment, "main",
                                  "clustered_forward/forward.es.frag.glsl",
                                  std::string(kForwardEsFragmentSource)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::SpirV,
                                  CNA::ShaderStageEXT::Vertex, "main",
                                  "clustered_forward/forward.vulkan.vert.spv",
                                  ToBytes(kForwardVulkanVertexSpirV,
                                          kForwardVulkanVertexSpirVByteSize)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::SpirV,
                                  CNA::ShaderStageEXT::Fragment, "main",
                                  "clustered_forward/forward.vulkan.frag.spv",
                                  ToBytes(kForwardVulkanFragmentSpirV,
                                          kForwardVulkanFragmentSpirVByteSize)),
                },
                {CNA::ShaderStageEXT::Vertex, CNA::ShaderStageEXT::Fragment},
                {
                    ShaderBindingRequirementEXT(
                        "uCnaAreaBrdf", 0, ShaderBindingTypeEXT::SampledTexture2D,
                        CNA::ShaderStageEXT::Fragment),
                    ShaderBindingRequirementEXT(
                        "uOpaqueFrame", 1, ShaderBindingTypeEXT::SampledTexture2D,
                        CNA::ShaderStageEXT::Fragment),
                    ShaderBindingRequirementEXT(
                        "uCnaLightData", 2, ShaderBindingTypeEXT::SampledTexture2D,
                        CNA::ShaderStageEXT::Fragment),
                    ShaderBindingRequirementEXT(
                        "uCnaClusterTable", 3, ShaderBindingTypeEXT::SampledTexture2D,
                        CNA::ShaderStageEXT::Fragment),
                    ShaderBindingRequirementEXT(
                        "uCnaLightIndices", 4, ShaderBindingTypeEXT::SampledTexture2D,
                        CNA::ShaderStageEXT::Fragment),
                    ShaderBindingRequirementEXT(
                        "CnaClusteredLights", 6, ShaderBindingTypeEXT::StorageBuffer,
                        CNA::ShaderStageEXT::Fragment),
                    ShaderBindingRequirementEXT(
                        "CnaClusterTable", 7, ShaderBindingTypeEXT::StorageBuffer,
                        CNA::ShaderStageEXT::Fragment),
                    ShaderBindingRequirementEXT(
                        "CnaLightIndices", 8, ShaderBindingTypeEXT::StorageBuffer,
                        CNA::ShaderStageEXT::Fragment),
                });
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
        const ShaderPackageEXT package = CreateClusteredForwardPackage();
        if (device.SupportsCapability(CNA::GraphicsCapability::CustomEffects) &&
            package.selectFor(device).isUsable())
            effect_ = std::make_unique<ShaderEffect>(device, package);
        bool logged = false;
        detail::reportShaderCompileFailure(device, "ClusteredForwardEffect", effect_.get(), logged);
        supported_ = effect_ != nullptr && effect_->IsEffectValid();
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

        // The volume is sampled at the world matrix's translation, which is the object's origin.
        const LightProbeEXT* activeProbe = probe_.get();
        LightProbeEXT sampled;
        if (probeVolume_ != nullptr)
        {
            sampled = probeVolume_->sampleProbe(Vector3(world.M41, world.M42, world.M43));
            activeProbe = &sampled;
        }

        effect_->Apply();
        if (effect_->GetSelectedShaderLanguageEXT() == CNA::ShaderLanguageEXT::SpirV)
        {
            const bool transmits = extensions_ != nullptr && extensions_->isTransmissionEnabled();
            if (transmits && opaqueFrame_ == nullptr)
                throw std::runtime_error(
                    "CNA::Graphics::ClusteredForwardEffect::begin: this material transmits, and no "
                    "copy of the opaque frame has been given to refract against. Refused rather "
                    "than approximated: a transmissive material drawn without one is not slightly "
                    "wrong, it is an opaque object where a glass one was asked for -- see "
                    "setOpaqueFrame");
            const bool haveArea = areaLight_ != nullptr && areaTable_ != nullptr &&
                                  areaTable_->getTexture() != nullptr;

            std::array<float, 4 * 16> matrices{};
            world.ToColumnMajor(matrices.data());
            view.ToColumnMajor(matrices.data() + 16);
            projection.ToColumnMajor(matrices.data() + 32);
            const Matrix viewProjection = view * projection;
            viewProjection.ToColumnMajor(matrices.data() + 48);

            std::array<float, 19 * 3> vectors{};
            const auto setVector = [&vectors](const int index, const Vector3& value) {
                vectors[static_cast<std::size_t>(index) * 3] = value.X;
                vectors[static_cast<std::size_t>(index) * 3 + 1] = value.Y;
                vectors[static_cast<std::size_t>(index) * 3 + 2] = value.Z;
            };
            if (activeProbe != nullptr)
                for (int index = 0; index < LightProbeEXT::kCoefficientCount; ++index)
                    setVector(index, activeProbe->getCoefficient(index));
            setVector(9, cameraPosition);
            setVector(10, baseColor_);
            setVector(11, ambient_);
            const Vector3 sheen = extensions_ != nullptr
                                      ? extensions_->getSheenColorFactor()
                                      : Vector3::Zero;
            const Vector3 subsurface = extensions_ != nullptr
                                           ? extensions_->getSubsurfaceColor()
                                           : Vector3::Zero;
            const Vector3 attenuation = extensions_ != nullptr
                                            ? extensions_->getAttenuationColor()
                                            : Vector3::One;
            setVector(12, sheen);
            setVector(13, subsurface);
            setVector(14, attenuation);
            if (haveArea)
            {
                setVector(15, areaLight_->Position);
                setVector(16, areaLight_->RightAxis);
                setVector(17, areaLight_->UpAxis);
                setVector(18, Vector3(
                                  areaLight_->Color.X * areaLight_->Intensity,
                                  areaLight_->Color.Y * areaLight_->Intensity,
                                  areaLight_->Color.Z * areaLight_->Intensity));
            }

            const std::array<float, 24> scalars{
                activeProbe != nullptr ? 1.0f : 0.0f,
                metallic_,
                roughness_,
                extensions_ != nullptr ? extensions_->getClearcoatFactor() : 0.0f,
                extensions_ != nullptr ? extensions_->getClearcoatRoughness() : 0.0f,
                extensions_ != nullptr ? extensions_->getSheenRoughness() : 0.0f,
                extensions_ != nullptr ? extensions_->getSubsurfaceWrap() : 0.5f,
                extensions_ != nullptr ? extensions_->getIridescenceFactor() : 0.0f,
                extensions_ != nullptr ? extensions_->getIridescenceIor() : 1.3f,
                extensions_ != nullptr ? extensions_->getIridescenceThicknessMaximum() : 400.0f,
                transmits ? extensions_->getTransmissionFactor() : 0.0f,
                ior_,
                transmits ? extensions_->getThicknessFactor() : 0.0f,
                transmits ? extensions_->getAttenuationDistance() : 0.0f,
                haveArea ? static_cast<float>(areaLight_->Shape) : -1.0f,
                haveArea ? static_cast<float>(areaTable_->getSize()) : 1.0f,
                haveArea ? areaLight_->Range : 0.0f,
                haveArea && areaLight_->TwoSided ? 1.0f : 0.0f,
                static_cast<float>(lights.tilesX_),
                static_cast<float>(lights.tilesY_),
                static_cast<float>(lights.sliceCount_),
                static_cast<float>(lights.lightCount_),
                lights.nearPlane_,
                lights.farPlane_,
            };
            effect_->SetUniformMat4Array(
                "uClusterMatrices", matrices.data(), 4);
            effect_->SetUniformVec3Array(
                "uClusterVectors", vectors.data(), 19);
            effect_->SetUniformFloatArray(
                "uClusterScalars", scalars.data(), static_cast<int>(scalars.size()));
            lights.bindStorageForDraw();
            if (haveArea) effect_->SetTexture(0, *areaTable_->getTexture());
            if (transmits) effect_->SetTexture(1, *opaqueFrame_);
            return;
        }

        effect_->SetUniformMat4("uWorld", &world.M11);
        effect_->SetUniformMat4("uView", &view.M11);
        effect_->SetUniformMat4("uProjection", &projection.M11);
        effect_->SetUniformVec3("uCameraPosition", cameraPosition.X, cameraPosition.Y,
                                cameraPosition.Z);
        effect_->SetUniformVec3("uBaseColor", baseColor_.X, baseColor_.Y, baseColor_.Z);
        effect_->SetUniformVec3("uAmbient", ambient_.X, ambient_.Y, ambient_.Z);

        effect_->SetUniformFloat("uHasProbe", activeProbe != nullptr ? 1.0f : 0.0f);
        if (activeProbe != nullptr)
        {
            float coefficients[LightProbeEXT::kCoefficientCount * 3];
            for (int index = 0; index < LightProbeEXT::kCoefficientCount; ++index)
            {
                const Vector3 value = activeProbe->getCoefficient(index);
                coefficients[index * 3 + 0] = value.X;
                coefficients[index * 3 + 1] = value.Y;
                coefficients[index * 3 + 2] = value.Z;
            }
            effect_->SetUniformVec3Array("uProbeCoefficients", coefficients,
                                         LightProbeEXT::kCoefficientCount);
        }
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

        const Vector3 subsurface = extensions_ != nullptr ? extensions_->getSubsurfaceColor()
                                                          : Vector3(0.0f, 0.0f, 0.0f);
        effect_->SetUniformVec3("uSubsurfaceColor", subsurface.X, subsurface.Y, subsurface.Z);
        effect_->SetUniformFloat("uSubsurfaceWrap",
                                 extensions_ != nullptr ? extensions_->getSubsurfaceWrap() : 0.5f);
        effect_->SetUniformFloat("uIridescence",
                                 extensions_ != nullptr ? extensions_->getIridescenceFactor()
                                                        : 0.0f);
        effect_->SetUniformFloat("uIridescenceIor",
                                 extensions_ != nullptr ? extensions_->getIridescenceIor() : 1.3f);
        // With no thickness map bound, glTF uses the maximum everywhere -- so this one number is
        // what decides a uniformly iridescent surface's colour.
        effect_->SetUniformFloat("uIridescenceThickness",
                                 extensions_ != nullptr
                                     ? extensions_->getIridescenceThicknessMaximum()
                                     : 400.0f);

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
            effect_->SetUniformInt("uOpaqueFrame", 1);
            effect_->SetTexture(1, *opaqueFrame_);
        }

        lights.bind(*effect_, 2);

        const bool haveArea = areaLight_ != nullptr && areaTable_ != nullptr &&
                              areaTable_->getTexture() != nullptr;
        effect_->SetUniformInt("uAreaShape", haveArea ? static_cast<int>(areaLight_->Shape) : -1);
        if (haveArea)
        {
            effect_->SetUniformInt("uCnaAreaBrdf", 0);
            effect_->SetTexture(0, *areaTable_->getTexture());
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

    void ClusteredForwardEffect::setLightProbe(const LightProbeEXT& probe)
    {
        probe_ = std::make_unique<LightProbeEXT>(probe);
        probeVolume_ = nullptr;
    }

    void ClusteredForwardEffect::setLightProbeVolume(const LightProbeVolumeEXT* volume)
    {
        probeVolume_ = volume;
        if (volume != nullptr) probe_.reset();
    }

    bool ClusteredForwardEffect::hasLightProbe() const
    {
        return probe_ != nullptr || probeVolume_ != nullptr;
    }

    void ClusteredForwardEffect::clearLightProbe()
    {
        probe_.reset();
        probeVolume_ = nullptr;
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
                                                 const float roughness,
                                                 const PbrMaterialExtensions& extensions)
    {
        return contribution(light, surface, normal, cameraPosition, baseColor, metallic, roughness,
                            extensions.getClearcoatFactor(), extensions.getClearcoatRoughness(),
                            extensions.getSheenColorFactor(), extensions.getSheenRoughness(),
                            extensions.getIridescenceFactor(), extensions.getIridescenceIor(),
                            extensions.getIridescenceThicknessMaximum(),
                            extensions.getSubsurfaceColor(), extensions.getSubsurfaceWrap());
    }

    Vector3 ClusteredForwardEffect::contribution(const ClusteredLightEXT& light,
                                                 const Vector3& surface, const Vector3& normal,
                                                 const Vector3& cameraPosition,
                                                 const Vector3& baseColor, const float metallic,
                                                 const float roughness, const float clearcoat,
                                                 const float clearcoatRoughness,
                                                 const Vector3& sheenColor,
                                                 const float sheenRoughness,
                                                 const float iridescence,
                                                 const float iridescenceIor,
                                                 const float iridescenceThickness,
                                                 const Vector3& subsurfaceColor,
                                                 const float subsurfaceWrap)
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
        // Guarded for the same reason the shader is: with the light exactly behind the surface the
        // half-vector's sum is zero, and a NaN there survives being multiplied by a zero N.L.
        const Vector3 halfSum(L.X + viewDirection.X, L.Y + viewDirection.Y, L.Z + viewDirection.Z);
        const Vector3 H = Dot(halfSum, halfSum) > 1e-8f ? Normalized(halfSum) : normal;

        const float rawNoL = Dot(normal, L);
        const float NoL = std::max(rawNoL, 0.0f);
        const bool subsurface = subsurfaceColor.X > 0.0f || subsurfaceColor.Y > 0.0f ||
                                subsurfaceColor.Z > 0.0f;
        float wrappedNoL = NoL;
        if (subsurface)
            wrappedNoL = std::clamp((rawNoL + subsurfaceWrap) /
                                        ((1.0f + subsurfaceWrap) * (1.0f + subsurfaceWrap)),
                                    0.0f, 1.0f);
        float backScatter = 0.0f;
        if (subsurface)
            backScatter = std::pow(std::clamp(-Dot(viewDirection, L), 0.0f, 1.0f), 4.0f);
        const float NoV = std::max(Dot(normal, viewDirection), 1e-4f);
        const float NoH = std::max(Dot(normal, H), 0.0f);
        const float VoH = std::max(Dot(viewDirection, H), 0.0f);
        if (NoL <= 0.0f && wrappedNoL <= 0.0f && backScatter <= 0.0f)
            return Vector3(0.0f, 0.0f, 0.0f);

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

        Vector3 baseFresnel0(0.0f, 0.0f, 0.0f);
        for (int channel = 0; channel < 3; ++channel)
            (&baseFresnel0.X)[channel] = 0.04f + ((&baseColor.X)[channel] - 0.04f) * metallic;

        Vector3 film(0.0f, 0.0f, 0.0f);
        if (iridescence > 0.0f)
            film = ThinFilmIridescence::evaluate(1.0f, iridescenceIor, NoV, iridescenceThickness,
                                                 baseFresnel0);

        Vector3 result(0.0f, 0.0f, 0.0f);
        float* out = &result.X;
        const float* base = &baseColor.X;
        const float* emitted = &light.Color.X;
        for (int channel = 0; channel < 3; ++channel)
        {
            const float f0 = (&baseFresnel0.X)[channel];
            float fresnel = f0 + (1.0f - f0) * schlick;
            if (iridescence > 0.0f)
                fresnel += ((&film.X)[channel] - fresnel) * iridescence;
            const float specular = fresnel * distributionTerm * geometryTerm /
                                   std::max(4.0f * NoV * NoL, 1e-7f);
            const float diffuse = (1.0f - fresnel) * (1.0f - metallic) * base[channel] /
                                  3.14159265359f;
            float layered = specular + (&sheenColor.X)[channel] * sheenTerm;
            if (clearcoat > 0.0f)
                layered = layered * (1.0f - clearcoat * clearcoatFresnel) +
                          clearcoat * clearcoatSpecular;
            const float diffuseTerm = diffuse * wrappedNoL +
                                      (&subsurfaceColor.X)[channel] * backScatter;
            out[channel] = (layered * NoL + diffuseTerm) * emitted[channel] * light.Intensity *
                           attenuation;
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

// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/VolumetricFogPass.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/ShaderCodeEXT.hpp"
#include "CNA/Graphics/ShaderPackageEXT.hpp"
#include "CNA/Graphics/ShadowMap.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "shaders/volumetric_fog/VolumetricFogShaderPackage.generated.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::DepthFormat;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

    namespace {

        [[nodiscard]] std::vector<std::uint8_t> ToBytes(
            const std::uint32_t* words, const std::size_t byteSize)
        {
            const auto* begin = reinterpret_cast<const std::uint8_t*>(words);
            return std::vector<std::uint8_t>(begin, begin + byteSize);
        }

        struct TextStage
        {
            std::string_view source;
            const char* label;
        };

        struct SpirVStage
        {
            const std::uint32_t* words;
            std::size_t byteSize;
            const char* label;
        };

        [[nodiscard]] ShaderPackageEXT MakeVolumetricFogPackage(
            const TextStage& esFragment, const TextStage& desktopFragment,
            const SpirVStage& vulkanFragment,
            std::vector<ShaderBindingRequirementEXT> additionalRequirements)
        {
            using ShaderCodeEXT = CNA::Graphics::ShaderCodeEXT;
            using namespace CNA::Graphics::detail::VolumetricFogGenerated;
            std::vector<ShaderBindingRequirementEXT> requirements;
            requirements.reserve(additionalRequirements.size() + 1);
            requirements.emplace_back(
                "texture1", 0, ShaderBindingTypeEXT::SampledTexture2D,
                CNA::ShaderStageEXT::Fragment);
            for (ShaderBindingRequirementEXT& requirement : additionalRequirements)
                requirements.push_back(std::move(requirement));
            return ShaderPackageEXT(
                {
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslEs,
                                  CNA::ShaderStageEXT::Vertex, "main",
                                  "volumetric_fog/fullscreen.es.vert.glsl",
                                  std::string(kFullscreenEsVertexSource)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslEs,
                                  CNA::ShaderStageEXT::Fragment, "main",
                                  esFragment.label, std::string(esFragment.source)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslDesktop,
                                  CNA::ShaderStageEXT::Vertex, "main",
                                  "volumetric_fog/fullscreen.desktop.vert.glsl",
                                  std::string(kFullscreenDesktopVertexSource)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslDesktop,
                                  CNA::ShaderStageEXT::Fragment, "main",
                                  desktopFragment.label, std::string(desktopFragment.source)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::SpirV,
                                  CNA::ShaderStageEXT::Vertex, "main",
                                  "volumetric_fog/fullscreen.vulkan.vert.spv",
                                  ToBytes(kFullscreenVulkanVertexSpirV,
                                          kFullscreenVulkanVertexSpirVByteSize)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::SpirV,
                                  CNA::ShaderStageEXT::Fragment, "main",
                                  vulkanFragment.label,
                                  ToBytes(vulkanFragment.words, vulkanFragment.byteSize)),
                },
                {CNA::ShaderStageEXT::Vertex, CNA::ShaderStageEXT::Fragment},
                std::move(requirements));
        }

        [[nodiscard]] ShaderPackageEXT CreateBuildPackage()
        {
            using namespace CNA::Graphics::detail::VolumetricFogGenerated;
            return MakeVolumetricFogPackage(
                {kBuildEsFragmentSource, "volumetric_fog/build.es.frag.glsl"},
                {kBuildDesktopFragmentSource, "volumetric_fog/build.desktop.frag.glsl"},
                {kBuildVulkanFragmentSpirV, kBuildVulkanFragmentSpirVByteSize,
                 "volumetric_fog/build.vulkan.frag.spv"},
                {ShaderBindingRequirementEXT(
                    "uShadowSampler", 1, ShaderBindingTypeEXT::SampledTexture2D,
                    CNA::ShaderStageEXT::Fragment)});
        }

        [[nodiscard]] ShaderPackageEXT CreateResolvePackage()
        {
            using namespace CNA::Graphics::detail::VolumetricFogGenerated;
            return MakeVolumetricFogPackage(
                {kResolveEsFragmentSource, "volumetric_fog/resolve.es.frag.glsl"},
                {kResolveDesktopFragmentSource,
                 "volumetric_fog/resolve.desktop.frag.glsl"},
                {kResolveVulkanFragmentSpirV, kResolveVulkanFragmentSpirVByteSize,
                 "volumetric_fog/resolve.vulkan.frag.spv"},
                {
                    ShaderBindingRequirementEXT(
                        "uDepthSampler", 1, ShaderBindingTypeEXT::SampledTexture2D,
                        CNA::ShaderStageEXT::Fragment),
                    ShaderBindingRequirementEXT(
                        "uVolumeSampler", 2, ShaderBindingTypeEXT::SampledTexture2D,
                        CNA::ShaderStageEXT::Fragment),
                });
        }

    } // namespace

    VolumetricFogPass::VolumetricFogPass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
        , pool_(device)
        , packedDepth_(DepthNormalPrepass::usesPackedDepthEXT(device))
    {
        const ShaderPackageEXT buildPackage = CreateBuildPackage();
        const ShaderPackageEXT resolvePackage = CreateResolvePackage();
        if (buildPackage.selectFor(device).isUsable())
            buildEffect_ = std::make_unique<ShaderEffect>(device, buildPackage);
        if (resolvePackage.selectFor(device).isUsable())
            resolveEffect_ = std::make_unique<ShaderEffect>(device, resolvePackage);
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
        const bool haveShadow = shadowMap_ != nullptr && shadowMap_->getShadowTexture() != nullptr;
        Matrix lightViewProjection = Matrix::getIdentityProperty();
        if (haveShadow)
        {
            lightViewProjection = shadowMap_->getLightViewProjection();
            buildEffect_->SetUniformInt("uShadowSampler", 1);
            buildEffect_->SetTexture(1, *shadowMap_->getShadowTexture());
        }
        std::array<float, 48> buildMatrices{};
        context.inverseProjection.ToColumnMajor(buildMatrices.data());
        context.inverseView.ToColumnMajor(buildMatrices.data() + 16);
        lightViewProjection.ToColumnMajor(buildMatrices.data() + 32);
        const std::array buildVectors{
            lightDirection_.X, lightDirection_.Y, lightDirection_.Z,
            lightColor_.X, lightColor_.Y, lightColor_.Z,
        };
        const std::array buildScalars{
            static_cast<float>(kSliceCount),
            static_cast<float>(kSliceResolution),
            density,
            anisotropy_,
            range_,
            haveShadow ? 1.0f : 0.0f,
        };
        buildEffect_->SetUniformMat4Array("uVolumetricBuildMatrices",
                                          buildMatrices.data(), 3);
        buildEffect_->SetUniformVec3Array("uVolumetricBuildVectors",
                                          buildVectors.data(), 2);
        buildEffect_->SetUniformFloatArray("uVolumetricBuildScalars",
                                           buildScalars.data(),
                                           static_cast<int>(buildScalars.size()));

        fullscreen_->draw(context.source, volume, buildEffect_.get(),
                          kSliceCount * kSliceResolution, kSliceResolution);

        resolveEffect_->Apply();
        resolveEffect_->SetUniformInt("uDepthSampler", 1);
        resolveEffect_->SetTexture(1, *context.sourceDepth);
        resolveEffect_->SetUniformInt("uVolumeSampler", 2);
        resolveEffect_->SetTexture(2, *volume);
        const std::array resolveScalars{
            static_cast<float>(kSliceCount),
            static_cast<float>(kSliceResolution),
            context.farPlane,
            range_,
            packedDepth_ ? 1.0f : 0.0f,
        };
        resolveEffect_->SetUniformFloatArray("uVolumetricResolveScalars",
                                             resolveScalars.data(),
                                             static_cast<int>(resolveScalars.size()));

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
        return device.SupportsCapability(CNA::GraphicsCapability::CustomEffects)
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

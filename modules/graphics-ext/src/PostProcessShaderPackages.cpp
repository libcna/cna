// SPDX-License-Identifier: MS-PL
#include "PostProcessShaderPackages.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/ShaderCodeEXT.hpp"
#include "shaders/post_process/PostProcessShaderPackage.generated.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CNA::Graphics::detail
{
    namespace
    {
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

        [[nodiscard]] ShaderPackageEXT MakeFullscreenPackage(
            const TextStage& esFragment, const TextStage& desktopFragment,
            const SpirVStage& vulkanFragment,
            std::vector<ShaderBindingRequirementEXT> additionalRequirements = {})
        {
            using ShaderCodeEXT = CNA::Graphics::ShaderCodeEXT;
            using namespace CNA::Graphics::detail::PostProcessGenerated;
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
                                  "post_process/fullscreen.es.vert.glsl",
                                  std::string(kFullscreenEsVertexSource)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslEs,
                                  CNA::ShaderStageEXT::Fragment, "main",
                                  esFragment.label, std::string(esFragment.source)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslDesktop,
                                  CNA::ShaderStageEXT::Vertex, "main",
                                  "post_process/fullscreen.desktop.vert.glsl",
                                  std::string(kFullscreenDesktopVertexSource)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslDesktop,
                                  CNA::ShaderStageEXT::Fragment, "main",
                                  desktopFragment.label,
                                  std::string(desktopFragment.source)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::SpirV,
                                  CNA::ShaderStageEXT::Vertex, "main",
                                  "post_process/fullscreen.vulkan.vert.spv",
                                  ToBytes(kFullscreenVulkanVertexSpirV,
                                          kFullscreenVulkanVertexSpirVByteSize)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::SpirV,
                                  CNA::ShaderStageEXT::Fragment, "main",
                                  vulkanFragment.label,
                                  ToBytes(vulkanFragment.words,
                                          vulkanFragment.byteSize)),
                },
                {CNA::ShaderStageEXT::Vertex, CNA::ShaderStageEXT::Fragment},
                std::move(requirements));
        }
    }

    ShaderPackageEXT CreateBloomExtractShaderPackage()
    {
        using namespace CNA::Graphics::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kBloomExtractEsFragmentSource, "post_process/bloom_extract.es.frag.glsl"},
            {kBloomExtractDesktopFragmentSource,
             "post_process/bloom_extract.desktop.frag.glsl"},
            {kBloomExtractVulkanFragmentSpirV,
             kBloomExtractVulkanFragmentSpirVByteSize,
             "post_process/bloom_extract.vulkan.frag.spv"});
    }

    ShaderPackageEXT CreateBloomBlurShaderPackage()
    {
        using namespace CNA::Graphics::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kBloomBlurEsFragmentSource, "post_process/bloom_blur.es.frag.glsl"},
            {kBloomBlurDesktopFragmentSource,
             "post_process/bloom_blur.desktop.frag.glsl"},
            {kBloomBlurVulkanFragmentSpirV,
             kBloomBlurVulkanFragmentSpirVByteSize,
             "post_process/bloom_blur.vulkan.frag.spv"});
    }

    ShaderPackageEXT CreateBloomUpsampleShaderPackage()
    {
        using namespace CNA::Graphics::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kBloomUpsampleEsFragmentSource,
             "post_process/bloom_upsample.es.frag.glsl"},
            {kBloomUpsampleDesktopFragmentSource,
             "post_process/bloom_upsample.desktop.frag.glsl"},
            {kBloomUpsampleVulkanFragmentSpirV,
             kBloomUpsampleVulkanFragmentSpirVByteSize,
             "post_process/bloom_upsample.vulkan.frag.spv"},
            {ShaderBindingRequirementEXT(
                "uSmallerSampler", 1, ShaderBindingTypeEXT::SampledTexture2D,
                CNA::ShaderStageEXT::Fragment)});
    }

    ShaderPackageEXT CreateBloomCombineShaderPackage()
    {
        using namespace CNA::Graphics::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kBloomCombineEsFragmentSource,
             "post_process/bloom_combine.es.frag.glsl"},
            {kBloomCombineDesktopFragmentSource,
             "post_process/bloom_combine.desktop.frag.glsl"},
            {kBloomCombineVulkanFragmentSpirV,
             kBloomCombineVulkanFragmentSpirVByteSize,
             "post_process/bloom_combine.vulkan.frag.spv"},
            {ShaderBindingRequirementEXT(
                "uBloomSampler", 1, ShaderBindingTypeEXT::SampledTexture2D,
                CNA::ShaderStageEXT::Fragment)});
    }

    ShaderPackageEXT CreateChromaticAberrationShaderPackage()
    {
        using namespace CNA::Graphics::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kChromaticEsFragmentSource, "post_process/chromatic.es.frag.glsl"},
            {kChromaticDesktopFragmentSource,
             "post_process/chromatic.desktop.frag.glsl"},
            {kChromaticVulkanFragmentSpirV, kChromaticVulkanFragmentSpirVByteSize,
             "post_process/chromatic.vulkan.frag.spv"});
    }

    ShaderPackageEXT CreateColorGradeStripShaderPackage()
    {
        using namespace CNA::Graphics::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kColorGradeStripEsFragmentSource,
             "post_process/color_grade_strip.es.frag.glsl"},
            {kColorGradeStripDesktopFragmentSource,
             "post_process/color_grade_strip.desktop.frag.glsl"},
            {kColorGradeStripVulkanFragmentSpirV,
             kColorGradeStripVulkanFragmentSpirVByteSize,
             "post_process/color_grade_strip.vulkan.frag.spv"},
            {ShaderBindingRequirementEXT(
                "uLutSampler", 1, ShaderBindingTypeEXT::SampledTexture2D,
                CNA::ShaderStageEXT::Fragment)});
    }

    ShaderPackageEXT CreateColorGradeInterpolatedStripShaderPackage()
    {
        using namespace CNA::Graphics::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kColorGradeInterpolatedStripEsFragmentSource,
             "post_process/color_grade_interpolated_strip.es.frag.glsl"},
            {kColorGradeInterpolatedStripDesktopFragmentSource,
             "post_process/color_grade_interpolated_strip.desktop.frag.glsl"},
            {kColorGradeInterpolatedStripVulkanFragmentSpirV,
             kColorGradeInterpolatedStripVulkanFragmentSpirVByteSize,
             "post_process/color_grade_interpolated_strip.vulkan.frag.spv"},
            {ShaderBindingRequirementEXT(
                "uLutSampler", 1, ShaderBindingTypeEXT::SampledTexture2D,
                CNA::ShaderStageEXT::Fragment)});
    }

    ShaderPackageEXT CreateColorGradeVolumeShaderPackage()
    {
        using namespace CNA::Graphics::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kColorGradeVolumeEsFragmentSource,
             "post_process/color_grade_volume.es.frag.glsl"},
            {kColorGradeVolumeDesktopFragmentSource,
             "post_process/color_grade_volume.desktop.frag.glsl"},
            {kColorGradeVolumeVulkanFragmentSpirV,
             kColorGradeVolumeVulkanFragmentSpirVByteSize,
             "post_process/color_grade_volume.vulkan.frag.spv"},
            {ShaderBindingRequirementEXT(
                "uLutVolume", 1, ShaderBindingTypeEXT::SampledTexture3D,
                CNA::ShaderStageEXT::Fragment)});
    }

    ShaderPackageEXT CreateFxaaShaderPackage()
    {
        using namespace CNA::Graphics::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kFxaaEsFragmentSource, "post_process/fxaa.es.frag.glsl"},
            {kFxaaDesktopFragmentSource, "post_process/fxaa.desktop.frag.glsl"},
            {kFxaaVulkanFragmentSpirV, kFxaaVulkanFragmentSpirVByteSize,
             "post_process/fxaa.vulkan.frag.spv"});
    }

    ShaderPackageEXT CreateFilmGrainShaderPackage()
    {
        using namespace CNA::Graphics::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kFilmGrainEsFragmentSource, "post_process/film_grain.es.frag.glsl"},
            {kFilmGrainDesktopFragmentSource,
             "post_process/film_grain.desktop.frag.glsl"},
            {kFilmGrainVulkanFragmentSpirV,
             kFilmGrainVulkanFragmentSpirVByteSize,
             "post_process/film_grain.vulkan.frag.spv"});
    }

    ShaderPackageEXT CreateTonemapShaderPackage()
    {
        using namespace CNA::Graphics::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kTonemapEsFragmentSource, "post_process/tonemap.es.frag.glsl"},
            {kTonemapDesktopFragmentSource, "post_process/tonemap.desktop.frag.glsl"},
            {kTonemapVulkanFragmentSpirV, kTonemapVulkanFragmentSpirVByteSize,
             "post_process/tonemap.vulkan.frag.spv"});
    }

    ShaderPackageEXT CreateLensFlareShaderPackage()
    {
        using namespace CNA::Graphics::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kLensFlareEsFragmentSource, "post_process/lens_flare.es.frag.glsl"},
            {kLensFlareDesktopFragmentSource,
             "post_process/lens_flare.desktop.frag.glsl"},
            {kLensFlareVulkanFragmentSpirV,
             kLensFlareVulkanFragmentSpirVByteSize,
             "post_process/lens_flare.vulkan.frag.spv"});
    }

    ShaderPackageEXT CreateHdrDisplayShaderPackage()
    {
        using namespace CNA::Graphics::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kHdrDisplayEsFragmentSource, "post_process/hdr_display.es.frag.glsl"},
            {kHdrDisplayDesktopFragmentSource,
             "post_process/hdr_display.desktop.frag.glsl"},
            {kHdrDisplayVulkanFragmentSpirV,
             kHdrDisplayVulkanFragmentSpirVByteSize,
             "post_process/hdr_display.vulkan.frag.spv"});
    }

    ShaderPackageEXT CreateSpatialUpscaleShaderPackage()
    {
        using namespace CNA::Graphics::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kSpatialUpscaleEsFragmentSource,
             "post_process/spatial_upscale.es.frag.glsl"},
            {kSpatialUpscaleDesktopFragmentSource,
             "post_process/spatial_upscale.desktop.frag.glsl"},
            {kSpatialUpscaleVulkanFragmentSpirV,
             kSpatialUpscaleVulkanFragmentSpirVByteSize,
             "post_process/spatial_upscale.vulkan.frag.spv"});
    }

    ShaderPackageEXT CreateLightShaftShaderPackage()
    {
        using namespace CNA::Graphics::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kLightShaftEsFragmentSource, "post_process/light_shaft.es.frag.glsl"},
            {kLightShaftDesktopFragmentSource,
             "post_process/light_shaft.desktop.frag.glsl"},
            {kLightShaftVulkanFragmentSpirV,
             kLightShaftVulkanFragmentSpirVByteSize,
             "post_process/light_shaft.vulkan.frag.spv"});
    }
}

#endif // CNA_CNAEXT

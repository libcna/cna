// SPDX-License-Identifier: MS-PL
#include "PostProcessShaderPackages.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/ShaderCodeEXT.hpp"
#include "shaders/post_process/PostProcessShaderPackage.generated.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
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
            const SpirVStage& vulkanFragment)
        {
            using ShaderCodeEXT = CNA::Graphics::ShaderCodeEXT;
            using namespace CNA::Graphics::detail::PostProcessGenerated;
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
                {ShaderBindingRequirementEXT(
                    "texture1", 0, ShaderBindingTypeEXT::SampledTexture2D,
                    CNA::ShaderStageEXT::Fragment)});
        }
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

    ShaderPackageEXT CreateFxaaShaderPackage()
    {
        using namespace CNA::Graphics::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kFxaaEsFragmentSource, "post_process/fxaa.es.frag.glsl"},
            {kFxaaDesktopFragmentSource, "post_process/fxaa.desktop.frag.glsl"},
            {kFxaaVulkanFragmentSpirV, kFxaaVulkanFragmentSpirVByteSize,
             "post_process/fxaa.vulkan.frag.spv"});
    }
}

#endif // CNA_CNAEXT

// SPDX-License-Identifier: MS-PL
#include "PostProcessShaderPackages.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/ShaderCodeEXT.hpp"
#include "shaders/post_process/PostProcessShaderPackage.generated.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
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
    }

    ShaderPackageEXT CreateChromaticAberrationShaderPackage()
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
                              "post_process/chromatic.es.frag.glsl",
                              std::string(kChromaticEsFragmentSource)),
                ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslDesktop,
                              CNA::ShaderStageEXT::Vertex, "main",
                              "post_process/fullscreen.desktop.vert.glsl",
                              std::string(kFullscreenDesktopVertexSource)),
                ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslDesktop,
                              CNA::ShaderStageEXT::Fragment, "main",
                              "post_process/chromatic.desktop.frag.glsl",
                              std::string(kChromaticDesktopFragmentSource)),
                ShaderCodeEXT(CNA::ShaderLanguageEXT::SpirV,
                              CNA::ShaderStageEXT::Vertex, "main",
                              "post_process/fullscreen.vulkan.vert.spv",
                              ToBytes(kFullscreenVulkanVertexSpirV,
                                      kFullscreenVulkanVertexSpirVByteSize)),
                ShaderCodeEXT(CNA::ShaderLanguageEXT::SpirV,
                              CNA::ShaderStageEXT::Fragment, "main",
                              "post_process/chromatic.vulkan.frag.spv",
                              ToBytes(kChromaticVulkanFragmentSpirV,
                                      kChromaticVulkanFragmentSpirVByteSize)),
            },
            {CNA::ShaderStageEXT::Vertex, CNA::ShaderStageEXT::Fragment},
            {ShaderBindingRequirementEXT(
                "texture1", 0, ShaderBindingTypeEXT::SampledTexture2D,
                CNA::ShaderStageEXT::Fragment)});
    }
}

#endif // CNA_CNAEXT

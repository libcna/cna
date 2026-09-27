// SPDX-License-Identifier: MS-PL
#include "PostProcessShaderPackages.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/ShaderCodeEXT.hpp"
#include "shaders/post_process/PostProcessHlsl.generated.hpp"
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
            const SpirVStage& vulkanFragment, const TextStage& wgslFragment,
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
            std::vector<ShaderCodeEXT> variants{
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
                    // plans/plan_webgpu_modern_graphics.md WMG-0005: the WGSL the generator derives
                    // from the same Vulkan GLSL, so a WGSL renderer runs the pass rather than
                    // falling back to a copy of its input.
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::Wgsl,
                                  CNA::ShaderStageEXT::Vertex, "main",
                                  "post_process/fullscreen.vulkan.vert.wgsl",
                                  std::string(kFullscreenVulkanVertexWgsl)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::Wgsl,
                                  CNA::ShaderStageEXT::Fragment, "main",
                                  wgslFragment.label, std::string(wgslFragment.source)),
                };
            const std::string_view hlslFragment =
                PostProcessHlslGenerated::FindFragment(vulkanFragment.label);
            if (!hlslFragment.empty())
            {
                variants.emplace_back(
                    CNA::ShaderLanguageEXT::Hlsl, CNA::ShaderStageEXT::Vertex,
                    "main", "post_process/fullscreen.hlsl",
                    std::string(PostProcessHlslGenerated::kFullscreenVertexHlsl));
                variants.emplace_back(
                    CNA::ShaderLanguageEXT::Hlsl, CNA::ShaderStageEXT::Fragment,
                    "main", std::string(vulkanFragment.label) + " -> hlsl",
                    std::string(hlslFragment));
            }
            return ShaderPackageEXT(
                std::move(variants),
                {CNA::ShaderStageEXT::Vertex, CNA::ShaderStageEXT::Fragment},
                std::move(requirements));
        }
    }

    ShaderPackageEXT CreateCrtShaderPackage()
    {
        using namespace CNA::Graphics::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kCrtEsFragmentSource, "post_process/crt.es.frag.glsl"},
            {kCrtDesktopFragmentSource, "post_process/crt.desktop.frag.glsl"},
            {kCrtVulkanFragmentSpirV, kCrtVulkanFragmentSpirVByteSize,
             "post_process/crt.vulkan.frag.spv"},
            {kCrtVulkanFragmentWgsl,
             "post_process/crt.vulkan.frag.wgsl"});
    }

    ShaderPackageEXT CreateDepthEffectShaderPackage()
    {
        using namespace CNA::Graphics::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kDepthEffectEsFragmentSource,
             "post_process/depth_effect.es.frag.glsl"},
            {kDepthEffectDesktopFragmentSource,
             "post_process/depth_effect.desktop.frag.glsl"},
            {kDepthEffectVulkanFragmentSpirV,
             kDepthEffectVulkanFragmentSpirVByteSize,
             "post_process/depth_effect.vulkan.frag.spv"},
            {kDepthEffectVulkanFragmentWgsl,
             "post_process/depth_effect.vulkan.frag.wgsl"},
            {ShaderBindingRequirementEXT(
                "uPalette", 1, ShaderBindingTypeEXT::SampledTexture2D,
                CNA::ShaderStageEXT::Fragment)});
    }

} // namespace CNA::Graphics::detail

#endif // CNA_CNAEXT

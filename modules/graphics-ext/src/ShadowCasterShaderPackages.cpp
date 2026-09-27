// SPDX-License-Identifier: MS-PL
#include "ShadowCasterShaderPackages.hpp"

#ifdef CNA_CNAEXT

#include "shaders/shadow_caster/ShadowCasterShaderPackage.generated.hpp"
#include "shaders/SceneHlsl.generated.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace CNA::Graphics::detail
{
    namespace
    {
        using CNA::Graphics::ShaderCodeEXT;
        using Generated::kCubeDesktopVertexSource;
        using Generated::kCubeEsVertexSource;
        using Generated::kDirectionalDesktopFragmentSource;
        using Generated::kDirectionalDesktopVertexSource;
        using Generated::kDirectionalEsFragmentSource;
        using Generated::kDirectionalEsVertexSource;
        using Generated::kDirectionalVulkanFragmentSpirV;
        using Generated::kDirectionalVulkanFragmentSpirVByteSize;
        using Generated::kDirectionalVulkanVertexSpirV;
        using Generated::kDirectionalVulkanVertexSpirVByteSize;
        using Generated::kPunctualDesktopFragmentSource;
        using Generated::kPunctualEsFragmentSource;
        using Generated::kPunctualVulkanFragmentSpirV;
        using Generated::kPunctualVulkanFragmentSpirVByteSize;
        using Generated::kPunctualVulkanVertexSpirV;
        using Generated::kPunctualVulkanVertexSpirVByteSize;
        using Generated::kSkinnedDesktopVertexSource;
        using Generated::kSkinnedEsVertexSource;
        using Generated::kSkinnedVulkanVertexSpirV;
        using Generated::kSkinnedVulkanVertexSpirVByteSize;
        using Generated::kSpotDesktopVertexSource;
        using Generated::kSpotEsVertexSource;

        using Generated::kDirectionalVulkanFragmentWgsl;
        using Generated::kDirectionalVulkanVertexWgsl;
        using Generated::kPunctualVulkanFragmentWgsl;
        using Generated::kPunctualVulkanVertexWgsl;
        using Generated::kSkinnedVulkanVertexWgsl;

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

        [[nodiscard]] std::vector<std::uint8_t> ToBytes(const SpirVStage& stage)
        {
            const auto* begin = reinterpret_cast<const std::uint8_t*>(stage.words);
            return std::vector<std::uint8_t>(begin, begin + stage.byteSize);
        }

        [[nodiscard]] ShaderPackageEXT MakePackage(
            const TextStage& esVertex, const TextStage& esFragment,
            const TextStage& desktopVertex, const TextStage& desktopFragment,
            const SpirVStage& vulkanVertex, const SpirVStage& vulkanFragment,
            const TextStage& wgslVertex, const TextStage& wgslFragment)
        {
            return ShaderPackageEXT(
                {
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslEs,
                                  CNA::ShaderStageEXT::Vertex, "main", esVertex.label,
                                  std::string(esVertex.source)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslEs,
                                  CNA::ShaderStageEXT::Fragment, "main", esFragment.label,
                                  std::string(esFragment.source)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslDesktop,
                                  CNA::ShaderStageEXT::Vertex, "main", desktopVertex.label,
                                  std::string(desktopVertex.source)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslDesktop,
                                  CNA::ShaderStageEXT::Fragment, "main", desktopFragment.label,
                                  std::string(desktopFragment.source)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::SpirV,
                                  CNA::ShaderStageEXT::Vertex, "main", vulkanVertex.label,
                                  ToBytes(vulkanVertex)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::SpirV,
                                  CNA::ShaderStageEXT::Fragment, "main", vulkanFragment.label,
                                  ToBytes(vulkanFragment)),
                    // plans/plan_webgpu_modern_graphics.md WMG-0005: the same program as WGSL.
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::Wgsl,
                                  CNA::ShaderStageEXT::Vertex, "main", wgslVertex.label,
                                  std::string(wgslVertex.source)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::Wgsl,
                                  CNA::ShaderStageEXT::Fragment, "main", wgslFragment.label,
                                  std::string(wgslFragment.source)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::Hlsl,
                                  CNA::ShaderStageEXT::Vertex, "main",
                                  std::string(vulkanVertex.label) + " -> hlsl",
                                  std::string(CNA::Graphics::detail::SceneHlslGenerated::FindStage(
                                      vulkanVertex.label))),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::Hlsl,
                                  CNA::ShaderStageEXT::Fragment, "main",
                                  std::string(vulkanFragment.label) + " -> hlsl",
                                  std::string(CNA::Graphics::detail::SceneHlslGenerated::FindStage(
                                      vulkanFragment.label))),
                },
                {CNA::ShaderStageEXT::Vertex, CNA::ShaderStageEXT::Fragment});
        }
    }

    ShaderPackageEXT CreateDirectionalShadowCasterPackage()
    {
        return MakePackage(
            {kDirectionalEsVertexSource, "shadow_caster/directional.es.vert.glsl"},
            {kDirectionalEsFragmentSource, "shadow_caster/directional.es.frag.glsl"},
            {kDirectionalDesktopVertexSource, "shadow_caster/directional.desktop.vert.glsl"},
            {kDirectionalDesktopFragmentSource, "shadow_caster/directional.desktop.frag.glsl"},
            {kDirectionalVulkanVertexSpirV, kDirectionalVulkanVertexSpirVByteSize,
             "shadow_caster/directional.vulkan.vert.spv"},
            {kDirectionalVulkanFragmentSpirV, kDirectionalVulkanFragmentSpirVByteSize,
             "shadow_caster/directional.vulkan.frag.spv"},
            {kDirectionalVulkanVertexWgsl, "shadow_caster/directional.vulkan.vert.wgsl"},
            {kDirectionalVulkanFragmentWgsl, "shadow_caster/directional.vulkan.frag.wgsl"});
    }

    ShaderPackageEXT CreateSkinnedDirectionalShadowCasterPackage()
    {
        return MakePackage(
            {kSkinnedEsVertexSource, "shadow_caster/skinned.es.vert.glsl"},
            {kDirectionalEsFragmentSource, "shadow_caster/directional.es.frag.glsl"},
            {kSkinnedDesktopVertexSource, "shadow_caster/skinned.desktop.vert.glsl"},
            {kDirectionalDesktopFragmentSource, "shadow_caster/directional.desktop.frag.glsl"},
            {kSkinnedVulkanVertexSpirV, kSkinnedVulkanVertexSpirVByteSize,
             "shadow_caster/skinned.vulkan.vert.spv"},
            {kDirectionalVulkanFragmentSpirV, kDirectionalVulkanFragmentSpirVByteSize,
             "shadow_caster/directional.vulkan.frag.spv"},
            {kSkinnedVulkanVertexWgsl, "shadow_caster/skinned.vulkan.vert.wgsl"},
            {kDirectionalVulkanFragmentWgsl, "shadow_caster/directional.vulkan.frag.wgsl"});
    }

    ShaderPackageEXT CreateCubeShadowCasterPackage()
    {
        return MakePackage(
            {kCubeEsVertexSource, "shadow_caster/cube.es.vert.glsl"},
            {kPunctualEsFragmentSource, "shadow_caster/punctual.es.frag.glsl"},
            {kCubeDesktopVertexSource, "shadow_caster/cube.desktop.vert.glsl"},
            {kPunctualDesktopFragmentSource, "shadow_caster/punctual.desktop.frag.glsl"},
            {kPunctualVulkanVertexSpirV, kPunctualVulkanVertexSpirVByteSize,
             "shadow_caster/punctual.vulkan.vert.spv"},
            {kPunctualVulkanFragmentSpirV, kPunctualVulkanFragmentSpirVByteSize,
             "shadow_caster/punctual.vulkan.frag.spv"},
            {kPunctualVulkanVertexWgsl, "shadow_caster/punctual.vulkan.vert.wgsl"},
            {kPunctualVulkanFragmentWgsl, "shadow_caster/punctual.vulkan.frag.wgsl"});
    }

    ShaderPackageEXT CreateSpotShadowCasterPackage()
    {
        return MakePackage(
            {kSpotEsVertexSource, "shadow_caster/spot.es.vert.glsl"},
            {kPunctualEsFragmentSource, "shadow_caster/punctual.es.frag.glsl"},
            {kSpotDesktopVertexSource, "shadow_caster/spot.desktop.vert.glsl"},
            {kPunctualDesktopFragmentSource, "shadow_caster/punctual.desktop.frag.glsl"},
            {kPunctualVulkanVertexSpirV, kPunctualVulkanVertexSpirVByteSize,
             "shadow_caster/punctual.vulkan.vert.spv"},
            {kPunctualVulkanFragmentSpirV, kPunctualVulkanFragmentSpirVByteSize,
             "shadow_caster/punctual.vulkan.frag.spv"},
            {kPunctualVulkanVertexWgsl, "shadow_caster/punctual.vulkan.vert.wgsl"},
            {kPunctualVulkanFragmentWgsl, "shadow_caster/punctual.vulkan.frag.wgsl"});
    }
}

#endif // CNA_CNAEXT

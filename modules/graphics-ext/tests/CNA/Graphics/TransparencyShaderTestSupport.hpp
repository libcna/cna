// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/ShaderCodeEXT.hpp"
#include "CNA/Graphics/ShaderPackageEXT.hpp"
#include "TransparencyShaderPackage.generated.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace CNA::Tests::Transparency
{
    namespace Detail
    {
        template <std::size_t N>
        [[nodiscard]] inline std::vector<std::uint8_t> ToBytes(
            const std::uint32_t (&words)[N])
        {
            const auto* begin = reinterpret_cast<const std::uint8_t*>(words);
            return std::vector<std::uint8_t>(begin, begin + sizeof(words));
        }

        template <std::size_t VulkanVertexSize, std::size_t VulkanFragmentSize>
        [[nodiscard]] inline CNA::Graphics::ShaderPackageEXT CreatePackage(
            const std::string_view esVertex, const std::string_view desktopVertex,
            const std::uint32_t (&vulkanVertex)[VulkanVertexSize],
            const std::string_view esFragment, const std::string_view desktopFragment,
            const std::uint32_t (&vulkanFragment)[VulkanFragmentSize], const char* label)
        {
            using CNA::Graphics::ShaderCodeEXT;
            using namespace CNA::Tests::TransparencyGenerated;
            return CNA::Graphics::ShaderPackageEXT(
                {
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslEs,
                                  CNA::ShaderStageEXT::Vertex, "main",
                                  std::string(label) + ".es.vert.glsl",
                                  std::string(esVertex)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslEs,
                                  CNA::ShaderStageEXT::Fragment, "main",
                                  std::string(label) + ".es.frag.glsl",
                                  std::string(esFragment)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslDesktop,
                                  CNA::ShaderStageEXT::Vertex, "main",
                                  std::string(label) + ".desktop.vert.glsl",
                                  std::string(desktopVertex)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslDesktop,
                                  CNA::ShaderStageEXT::Fragment, "main",
                                  std::string(label) + ".desktop.frag.glsl",
                                  std::string(desktopFragment)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::SpirV,
                                  CNA::ShaderStageEXT::Vertex, "main",
                                  std::string(label) + ".vulkan.vert.spv",
                                  ToBytes(vulkanVertex)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::SpirV,
                                  CNA::ShaderStageEXT::Fragment, "main",
                                  std::string(label) + ".vulkan.frag.spv",
                                  ToBytes(vulkanFragment)),
                },
                {CNA::ShaderStageEXT::Vertex, CNA::ShaderStageEXT::Fragment});
        }
    }

    [[nodiscard]] inline CNA::Graphics::ShaderPackageEXT CreateEmitterPackage()
    {
        using namespace CNA::Tests::TransparencyGenerated;
        return Detail::CreatePackage(kBasicEsVertexSource,
                                     kBasicDesktopVertexSource,
                                     kBasicVulkanVertexSpirV,
                                     kEmitterEsFragmentSource,
                                     kEmitterDesktopFragmentSource,
                                     kEmitterVulkanFragmentSpirV,
                                     "transparency/emitter");
    }

    [[nodiscard]] inline CNA::Graphics::ShaderPackageEXT CreateFlatPackage()
    {
        using namespace CNA::Tests::TransparencyGenerated;
        return Detail::CreatePackage(kBasicEsVertexSource,
                                     kBasicDesktopVertexSource,
                                     kBasicVulkanVertexSpirV,
                                     kFlatEsFragmentSource,
                                     kFlatDesktopFragmentSource,
                                     kFlatVulkanFragmentSpirV,
                                     "transparency/flat");
    }

    [[nodiscard]] inline CNA::Graphics::ShaderPackageEXT CreateWeightProbePackage()
    {
        using namespace CNA::Tests::TransparencyGenerated;
        return Detail::CreatePackage(kDirectEsVertexSource,
                                     kDirectDesktopVertexSource,
                                     kDirectVulkanVertexSpirV,
                                     kWeightProbeEsFragmentSource,
                                     kWeightProbeDesktopFragmentSource,
                                     kWeightProbeVulkanFragmentSpirV,
                                     "transparency/weight_probe");
    }
}

#endif // CNA_CNAEXT

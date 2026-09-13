// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/ShaderCodeEXT.hpp"
#include "CNA/Graphics/ShaderPackageEXT.hpp"
#include "TransparencyExampleShaderPackage.generated.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace CNA::Examples::Transparency
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

        template <std::size_t N>
        [[nodiscard]] inline CNA::Graphics::ShaderPackageEXT CreatePackage(
            const std::string_view esFragment, const std::string_view desktopFragment,
            const std::uint32_t (&vulkanFragment)[N], const char* label)
        {
            using CNA::Graphics::ShaderCodeEXT;
            using namespace CNA::Examples::TransparencyGenerated;
            return CNA::Graphics::ShaderPackageEXT(
                {
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslEs,
                                  CNA::ShaderStageEXT::Vertex, "main",
                                  "transparency/basic.es.vert.glsl",
                                  std::string(kBasicEsVertexSource)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslEs,
                                  CNA::ShaderStageEXT::Fragment, "main",
                                  std::string(label) + ".es.frag.glsl",
                                  std::string(esFragment)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslDesktop,
                                  CNA::ShaderStageEXT::Vertex, "main",
                                  "transparency/basic.desktop.vert.glsl",
                                  std::string(kBasicDesktopVertexSource)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslDesktop,
                                  CNA::ShaderStageEXT::Fragment, "main",
                                  std::string(label) + ".desktop.frag.glsl",
                                  std::string(desktopFragment)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::SpirV,
                                  CNA::ShaderStageEXT::Vertex, "main",
                                  "transparency/basic.vulkan.vert.spv",
                                  ToBytes(kBasicVulkanVertexSpirV)),
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
        using namespace CNA::Examples::TransparencyGenerated;
        return Detail::CreatePackage(kEmitterEsFragmentSource,
                                     kEmitterDesktopFragmentSource,
                                     kEmitterVulkanFragmentSpirV,
                                     "transparency/emitter");
    }

    [[nodiscard]] inline CNA::Graphics::ShaderPackageEXT CreateFlatPackage()
    {
        using namespace CNA::Examples::TransparencyGenerated;
        return Detail::CreatePackage(kFlatEsFragmentSource,
                                     kFlatDesktopFragmentSource,
                                     kFlatVulkanFragmentSpirV,
                                     "transparency/flat");
    }
}

#endif // CNA_CNAEXT

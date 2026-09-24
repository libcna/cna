// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/ShaderPackageEXT.hpp"
#include "common/PortableTintShaderPackage.generated.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace CNA::Examples::PortableTint
{
    namespace Detail
    {
        /**
         * @brief Copies a statically embedded SPIR-V word array into an owning byte payload.
         * @param words Generated SPIR-V words.
         * @return Byte-for-byte copy suitable for ShaderCodeEXT.
         */
        template <std::size_t N>
        [[nodiscard]] inline std::vector<std::uint8_t> ToBytes(
            const std::uint32_t (&words)[N])
        {
            const auto* begin = reinterpret_cast<const std::uint8_t*>(words);
            return std::vector<std::uint8_t>(begin, begin + sizeof(words));
        }
    }

    /**
     * @brief Creates the portable tint fixture from every checked-in generated variant.
     * @return An owning package containing GLSL ES, desktop GLSL, SPIR-V and WGSL vertex/fragment
     *         pairs.
     */
    [[nodiscard]] inline CNA::Graphics::ShaderPackageEXT CreatePackage()
    {
        using CNA::Graphics::ShaderCodeEXT;
        return CNA::Graphics::ShaderPackageEXT(
            {
                ShaderCodeEXT(
                    CNA::ShaderLanguageEXT::GlslEs, CNA::ShaderStageEXT::Vertex,
                    "main", std::string(kPayloads[0].source),
                    std::string(kEasyGlVertexSource)),
                ShaderCodeEXT(
                    CNA::ShaderLanguageEXT::GlslEs, CNA::ShaderStageEXT::Fragment,
                    "main", std::string(kPayloads[1].source),
                    std::string(kEasyGlFragmentSource)),
                // plans/plan_opengl4_modern_graphics.md GL4-0018: the desktop GLSL pair a desktop
                // core context (EasyGL OPENGL33, OPENGL4) selects; without it the package had no
                // variant those renderers accept.
                ShaderCodeEXT(
                    CNA::ShaderLanguageEXT::GlslDesktop, CNA::ShaderStageEXT::Vertex,
                    "main", std::string(kPayloads[2].source),
                    std::string(kDesktopVertexSource)),
                ShaderCodeEXT(
                    CNA::ShaderLanguageEXT::GlslDesktop, CNA::ShaderStageEXT::Fragment,
                    "main", std::string(kPayloads[3].source),
                    std::string(kDesktopFragmentSource)),
                ShaderCodeEXT(
                    CNA::ShaderLanguageEXT::SpirV, CNA::ShaderStageEXT::Vertex,
                    "main", std::string(kPayloads[4].source),
                    Detail::ToBytes(kVulkanVertexSpirV)),
                ShaderCodeEXT(
                    CNA::ShaderLanguageEXT::Wgsl, CNA::ShaderStageEXT::Vertex,
                    "main", std::string(kPayloads[5].source),
                    std::string(kVulkanVertexWgsl)),
                ShaderCodeEXT(
                    CNA::ShaderLanguageEXT::SpirV, CNA::ShaderStageEXT::Fragment,
                    "main", std::string(kPayloads[6].source),
                    Detail::ToBytes(kVulkanFragmentSpirV)),
                ShaderCodeEXT(
                    CNA::ShaderLanguageEXT::Wgsl, CNA::ShaderStageEXT::Fragment,
                    "main", std::string(kPayloads[7].source),
                    std::string(kVulkanFragmentWgsl)),
            },
            {CNA::ShaderStageEXT::Vertex, CNA::ShaderStageEXT::Fragment});
    }
}

#endif // CNA_CNAEXT

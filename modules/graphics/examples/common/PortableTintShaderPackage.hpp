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
     * @return An owning package containing GLSL ES and SPIR-V vertex/fragment pairs.
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
                ShaderCodeEXT(
                    CNA::ShaderLanguageEXT::SpirV, CNA::ShaderStageEXT::Vertex,
                    "main", std::string(kPayloads[2].source),
                    Detail::ToBytes(kVulkanVertexSpirV)),
                ShaderCodeEXT(
                    CNA::ShaderLanguageEXT::SpirV, CNA::ShaderStageEXT::Fragment,
                    "main", std::string(kPayloads[3].source),
                    Detail::ToBytes(kVulkanFragmentSpirV)),
            },
            {CNA::ShaderStageEXT::Vertex, CNA::ShaderStageEXT::Fragment});
    }
}

#endif // CNA_CNAEXT

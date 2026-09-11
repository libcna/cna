// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/ShaderLanguageEXT.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace CNA::Graphics
{
    /** @addtogroup cnaext_engine
     *  @{
     */

    /**
     * @brief Owned source text or binary code for one explicitly identified shader stage.
     *
     * plans/plan_modern.md `MOD-2211`. The language and payload form are declared rather than
     * inferred from contents. Construction copies or moves every string/byte into this value, so
     * no renderer or future shader package depends on caller-owned storage.
     */
    class ShaderCodeEXT final
    {
    public:
        /**
         * @brief Creates an owned textual shader payload.
         *
         * @param language Exact textual source dialect.
         * @param stage Programmable stage implemented by the source.
         * @param entryPoint Non-empty entry-point name.
         * @param sourceLabel Caller-facing source name used by diagnostics; it may be empty.
         * @param sourceText Non-empty source text, copied or moved into this value.
         * @throws std::invalid_argument If an identity is unknown/invalid, the language denotes a
         *         binary format, the entry point is empty, or the source text is empty.
         */
        ShaderCodeEXT(
            CNA::ShaderLanguageEXT language, CNA::ShaderStageEXT stage,
            std::string entryPoint, std::string sourceLabel, std::string sourceText);

        /**
         * @brief Creates an owned binary shader payload.
         *
         * @param language Exact binary code format.
         * @param stage Programmable stage implemented by the code.
         * @param entryPoint Non-empty entry-point name.
         * @param sourceLabel Caller-facing source name used by diagnostics; it may be empty.
         * @param binaryCode Non-empty code bytes, copied or moved into this value.
         * @throws std::invalid_argument If an identity is unknown/invalid, the language denotes
         *         source text, the entry point/code is empty, or SPIR-V is not whole 32-bit words.
         */
        ShaderCodeEXT(
            CNA::ShaderLanguageEXT language, CNA::ShaderStageEXT stage,
            std::string entryPoint, std::string sourceLabel,
            std::vector<std::uint8_t> binaryCode);

        /**
         * @brief Returns the explicitly declared source language or binary format.
         * @return Language supplied at construction.
         */
        [[nodiscard]] CNA::ShaderLanguageEXT getLanguage() const noexcept;

        /**
         * @brief Returns the explicitly declared programmable stage.
         * @return Stage supplied at construction.
         */
        [[nodiscard]] CNA::ShaderStageEXT getStage() const noexcept;

        /**
         * @brief Returns the entry-point name.
         * @return Non-empty owned entry-point string.
         */
        [[nodiscard]] const std::string& getEntryPoint() const noexcept;

        /**
         * @brief Returns the diagnostic source label.
         * @return Owned label supplied at construction, possibly empty.
         */
        [[nodiscard]] const std::string& getSourceLabel() const noexcept;

        /**
         * @brief Returns whether this value owns textual shader source.
         * @return True for a GLSL, HLSL, MSL or WGSL payload.
         */
        [[nodiscard]] bool isText() const noexcept;

        /**
         * @brief Returns whether this value owns binary shader code.
         * @return True for a SPIR-V or DXIL payload.
         */
        [[nodiscard]] bool isBinary() const noexcept;

        /**
         * @brief Returns the exact payload size without exposing a native shader object.
         * @return Text bytes or binary bytes owned by this value.
         */
        [[nodiscard]] std::size_t getPayloadByteSize() const noexcept;

        /**
         * @brief Returns the owned textual source.
         * @return Source text supplied at construction.
         * @throws std::logic_error If this value contains binary code.
         */
        [[nodiscard]] const std::string& getText() const;

        /**
         * @brief Returns the owned binary code.
         * @return Code bytes supplied at construction.
         * @throws std::logic_error If this value contains textual source.
         */
        [[nodiscard]] const std::vector<std::uint8_t>& getBytes() const;

    private:
        CNA::ShaderLanguageEXT language_;
        CNA::ShaderStageEXT stage_;
        std::string entryPoint_;
        std::string sourceLabel_;
        std::variant<std::string, std::vector<std::uint8_t>> payload_;
    };

    /** @} */ // end of cnaext_engine
}

#endif // CNA_CNAEXT

// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/ShaderCodeEXT.hpp"

#ifdef CNA_CNAEXT

#include <stdexcept>
#include <utility>

namespace CNA::Graphics
{
    namespace
    {
        enum class PayloadForm
        {
            Invalid,
            Text,
            Binary
        };

        [[nodiscard]] PayloadForm GetPayloadForm(const CNA::ShaderLanguageEXT language) noexcept
        {
            switch (language)
            {
                case CNA::ShaderLanguageEXT::GlslDesktop:
                case CNA::ShaderLanguageEXT::GlslEs:
                case CNA::ShaderLanguageEXT::GlslVulkan:
                case CNA::ShaderLanguageEXT::Hlsl:
                case CNA::ShaderLanguageEXT::Msl:
                case CNA::ShaderLanguageEXT::Wgsl:
                    return PayloadForm::Text;
                case CNA::ShaderLanguageEXT::SpirV:
                case CNA::ShaderLanguageEXT::Dxil:
                    return PayloadForm::Binary;
                case CNA::ShaderLanguageEXT::Unknown:
                case CNA::ShaderLanguageEXT::Count:
                    return PayloadForm::Invalid;
            }
            return PayloadForm::Invalid;
        }

        void ValidateStage(const CNA::ShaderStageEXT stage)
        {
            switch (stage)
            {
                case CNA::ShaderStageEXT::Vertex:
                case CNA::ShaderStageEXT::Fragment:
                case CNA::ShaderStageEXT::Compute:
                    return;
                case CNA::ShaderStageEXT::Unknown:
                case CNA::ShaderStageEXT::Count:
                    break;
            }
            throw std::invalid_argument("ShaderCodeEXT: the shader stage is not recognized");
        }

        void ValidateEntryPoint(const std::string& entryPoint)
        {
            if (entryPoint.empty())
                throw std::invalid_argument("ShaderCodeEXT: the entry point must not be empty");
        }
    }

    ShaderCodeEXT::ShaderCodeEXT(
        const CNA::ShaderLanguageEXT language, const CNA::ShaderStageEXT stage,
        std::string entryPoint, std::string sourceLabel, std::string sourceText)
        : language_(language)
        , stage_(stage)
        , entryPoint_(std::move(entryPoint))
        , sourceLabel_(std::move(sourceLabel))
        , payload_(std::move(sourceText))
    {
        ValidateStage(stage_);
        ValidateEntryPoint(entryPoint_);
        if (GetPayloadForm(language_) != PayloadForm::Text)
            throw std::invalid_argument(
                "ShaderCodeEXT: a text payload requires a textual shader language");
        if (std::get<std::string>(payload_).empty())
            throw std::invalid_argument("ShaderCodeEXT: shader source must not be empty");
    }

    ShaderCodeEXT::ShaderCodeEXT(
        const CNA::ShaderLanguageEXT language, const CNA::ShaderStageEXT stage,
        std::string entryPoint, std::string sourceLabel,
        std::vector<std::uint8_t> binaryCode)
        : language_(language)
        , stage_(stage)
        , entryPoint_(std::move(entryPoint))
        , sourceLabel_(std::move(sourceLabel))
        , payload_(std::move(binaryCode))
    {
        ValidateStage(stage_);
        ValidateEntryPoint(entryPoint_);
        if (GetPayloadForm(language_) != PayloadForm::Binary)
            throw std::invalid_argument(
                "ShaderCodeEXT: a binary payload requires a binary shader format");
        const auto& bytes = std::get<std::vector<std::uint8_t>>(payload_);
        if (bytes.empty())
            throw std::invalid_argument("ShaderCodeEXT: binary shader code must not be empty");
        if (language_ == CNA::ShaderLanguageEXT::SpirV && bytes.size() % 4U != 0U)
            throw std::invalid_argument(
                "ShaderCodeEXT: SPIR-V code must contain complete 32-bit words");
    }

    CNA::ShaderLanguageEXT ShaderCodeEXT::getLanguage() const noexcept { return language_; }

    CNA::ShaderStageEXT ShaderCodeEXT::getStage() const noexcept { return stage_; }

    const std::string& ShaderCodeEXT::getEntryPoint() const noexcept { return entryPoint_; }

    const std::string& ShaderCodeEXT::getSourceLabel() const noexcept { return sourceLabel_; }

    bool ShaderCodeEXT::isText() const noexcept
    {
        return std::holds_alternative<std::string>(payload_);
    }

    bool ShaderCodeEXT::isBinary() const noexcept
    {
        return std::holds_alternative<std::vector<std::uint8_t>>(payload_);
    }

    std::size_t ShaderCodeEXT::getPayloadByteSize() const noexcept
    {
        if (isText()) return std::get<std::string>(payload_).size();
        return std::get<std::vector<std::uint8_t>>(payload_).size();
    }

    const std::string& ShaderCodeEXT::getText() const
    {
        if (!isText())
            throw std::logic_error("ShaderCodeEXT: the payload is binary, not text");
        return std::get<std::string>(payload_);
    }

    const std::vector<std::uint8_t>& ShaderCodeEXT::getBytes() const
    {
        if (!isBinary())
            throw std::logic_error("ShaderCodeEXT: the payload is text, not binary");
        return std::get<std::vector<std::uint8_t>>(payload_);
    }
}

#endif // CNA_CNAEXT

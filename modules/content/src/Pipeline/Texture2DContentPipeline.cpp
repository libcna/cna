// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Pipeline/Texture2DContentPipeline.hpp"

#include <array>
#include <charconv>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>

#include "CNA/Content/Cnb/CnbFormat.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

namespace CNA::Content::Pipeline
{
    namespace
    {
        using Microsoft::Xna::Framework::Content::ContentLoadException;

        constexpr const char* kImageImporterName = "CNA.ImageImporter";
        constexpr const char* kTextureProcessorName = "CNA.TextureProcessor";
        constexpr const char* kTextureWriterName = "CNA.Texture2DContentWriter";

        std::uint8_t ParseColorComponent(std::string_view text)
        {
            if (text.empty())
            {
                throw std::invalid_argument(
                    "TextureProcessor parameter 'colorKey' has an empty component.");
            }
            std::uint32_t value = 0u;
            const auto [end, error] =
                std::from_chars(text.data(), text.data() + text.size(), value, 10);
            if (error != std::errc{} || end != text.data() + text.size() || value > 255u)
            {
                throw std::invalid_argument(
                    "TextureProcessor parameter 'colorKey' must contain exactly three decimal "
                    "bytes in R,G,B form.");
            }
            return static_cast<std::uint8_t>(value);
        }

        std::array<std::uint8_t, 3> ParseColorKey(const std::string& text)
        {
            std::array<std::uint8_t, 3> result{};
            std::size_t start = 0u;
            for (std::size_t component = 0u; component < result.size(); ++component)
            {
                const std::size_t comma = text.find(',', start);
                if ((component + 1u == result.size()) != (comma == std::string::npos))
                {
                    throw std::invalid_argument(
                        "TextureProcessor parameter 'colorKey' must contain exactly three "
                        "components in R,G,B form.");
                }
                const std::size_t end = comma == std::string::npos ? text.size() : comma;
                result[component] =
                    ParseColorComponent(std::string_view(text).substr(start, end - start));
                start = end + 1u;
            }
            return result;
        }

        std::optional<std::array<std::uint8_t, 3>> ReadColorKey(
            const ContentProcessorParameters& parameters)
        {
            const ContentProcessorParameterValue* value =
                parameters.Find(TextureColorKeyParameter);
            if (value == nullptr) { return std::nullopt; }
            const std::string* text = std::get_if<std::string>(value);
            if (text == nullptr)
            {
                throw std::invalid_argument(
                    "TextureProcessor parameter 'colorKey' must be a string in R,G,B form.");
            }
            return ParseColorKey(*text);
        }
    }

    ContentComponentIdentity ImageImporter::Identity() const
    {
        return {kImageImporterName, "1"};
    }

    std::vector<std::string> ImageImporter::SourceExtensions() const
    {
        return {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".psd", ".hdr",
                ".pic", ".pnm"};
    }

    std::string ImageImporter::OutputType() const
    {
        return ImportedImageType;
    }

    ContentValue ImageImporter::Import(ContentImporterContext& context) const
    {
        CNA::Internal::Graphics::ImageData image =
            CNA::Internal::Graphics::ImageLoader::Load(context.SourcePath().string());
        if (image.width <= 0 || image.height <= 0)
        {
            throw ContentLoadException(
                "image decoded to " + std::to_string(image.width) + "x" +
                std::to_string(image.height) + ".");
        }
        const std::uint64_t expected =
            static_cast<std::uint64_t>(image.width) * static_cast<std::uint64_t>(image.height) * 4u;
        if (image.pixels.size() != expected)
        {
            throw ContentLoadException(
                "image decoded to " + std::to_string(image.pixels.size()) + " bytes, but " +
                std::to_string(image.width) + "x" + std::to_string(image.height) +
                " Rgba8 needs " + std::to_string(expected) + ".");
        }
        if (static_cast<std::uint64_t>(image.width) >
                std::numeric_limits<std::uint32_t>::max() ||
            static_cast<std::uint64_t>(image.height) >
                std::numeric_limits<std::uint32_t>::max())
        {
            throw ContentLoadException("decoded image dimensions exceed the CNB Texture2D range.");
        }

        ImportedImage imported;
        imported.width = static_cast<std::uint32_t>(image.width);
        imported.height = static_cast<std::uint32_t>(image.height);
        imported.rgbaPixels = std::move(image.pixels);
        context.LogInfo("decoded " + std::to_string(imported.width) + "x" +
                        std::to_string(imported.height) + " Rgba8 image.");
        return ContentValue::Create(ImportedImageType, std::move(imported));
    }

    ContentComponentIdentity TextureProcessor::Identity() const
    {
        return {kTextureProcessorName, "1"};
    }

    std::string TextureProcessor::InputType() const
    {
        return ImportedImageType;
    }

    std::string TextureProcessor::OutputType() const
    {
        return ProcessedTexture2DType;
    }

    void TextureProcessor::ValidateParameters(
        const ContentProcessorParameters& parameters) const
    {
        for (const auto& [name, value] : parameters.Values())
        {
            static_cast<void>(value);
            if (name != TextureColorKeyParameter)
            {
                throw std::invalid_argument("TextureProcessor does not recognize parameter '" +
                                            name + "'.");
            }
        }
        static_cast<void>(ReadColorKey(parameters));
    }

    ContentValue TextureProcessor::Process(const ContentValue& input,
                                           ContentProcessorContext& context) const
    {
        const ImportedImage& image = input.Get<ImportedImage>();
        std::vector<std::uint8_t> pixels = image.rgbaPixels;
        if (const auto colorKey = ReadColorKey(context.Parameters()); colorKey.has_value())
        {
            for (std::size_t index = 0u; index + 3u < pixels.size(); index += 4u)
            {
                if (pixels[index] == (*colorKey)[0] && pixels[index + 1u] == (*colorKey)[1] &&
                    pixels[index + 2u] == (*colorKey)[2])
                {
                    pixels[index + 3u] = 0u;
                }
            }
        }

        Cnb::CnbTextureData texture =
            Cnb::MakeRgba8Texture2DData(image.width, image.height, std::move(pixels));
        context.LogInfo("prepared Texture2D Rgba8 level 0 for CNB encoding.");
        return ContentValue::Create(ProcessedTexture2DType, std::move(texture));
    }

    ContentComponentIdentity Texture2DContentWriter::Identity() const
    {
        return {kTextureWriterName, "1"};
    }

    std::string Texture2DContentWriter::InputType() const
    {
        return ProcessedTexture2DType;
    }

    ContentWriteResult Texture2DContentWriter::Write(const ContentValue& input,
                                                     const std::string& logicalName) const
    {
        const Cnb::CnbTextureData& texture = input.Get<Cnb::CnbTextureData>();
        return {Cnb::EncodeTexture2DToCnb(texture, logicalName),
                Cnb::CnbAssetTypeId::Texture2D,
                "Microsoft.Xna.Framework.Graphics.Texture2D"};
    }

    void RegisterTexture2DContentPipeline(ContentPipelineRegistry& registry)
    {
        registry.RegisterImporter(std::make_shared<ImageImporter>());
        registry.RegisterProcessor(std::make_shared<TextureProcessor>());
        registry.RegisterWriter(std::make_shared<Texture2DContentWriter>());
    }
}

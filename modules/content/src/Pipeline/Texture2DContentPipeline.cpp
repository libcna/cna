// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Pipeline/Texture2DContentPipeline.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <fstream>
#include <iterator>
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

        /** @brief Lowercases an ASCII parameter value for case-insensitive matching. */
        [[nodiscard]] std::string Lowercase(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(),
                           [](const unsigned char character)
                           { return static_cast<char>(std::tolower(character)); });
            return text;
        }

        /** @brief Reads a strict `true`/`false` processor parameter. */
        [[nodiscard]] bool ReadBooleanParameter(const ContentProcessorParameters& parameters,
                                                const char* name, const bool fallback)
        {
            const ContentProcessorParameterValue* value = parameters.Find(name);
            if (value == nullptr) { return fallback; }
            const std::string* text = std::get_if<std::string>(value);
            if (text != nullptr)
            {
                const std::string lowered = Lowercase(*text);
                if (lowered == "true") { return true; }
                if (lowered == "false") { return false; }
            }
            else if (const bool* boolean = std::get_if<bool>(value); boolean != nullptr)
            {
                return *boolean;
            }
            throw std::invalid_argument(std::string("TextureProcessor parameter '") + name +
                                        "' must be true or false.");
        }

        /** @brief Reads and validates the `textureFormat` parameter. */
        [[nodiscard]] TextureBuildFormat ReadFormatParameter(
            const ContentProcessorParameters& parameters)
        {
            const ContentProcessorParameterValue* value =
                parameters.Find(TextureFormatParameter);
            if (value == nullptr) { return TextureBuildFormat::NoChange; }
            const std::string* text = std::get_if<std::string>(value);
            if (text == nullptr)
            {
                throw std::invalid_argument(
                    "TextureProcessor parameter 'textureFormat' must be a string.");
            }
            const std::optional<TextureBuildFormat> parsed = TryParseTextureBuildFormat(*text);
            if (!parsed.has_value())
            {
                throw std::invalid_argument(
                    "TextureProcessor parameter 'textureFormat' is '" + *text +
                    "'; it must be NoChange, Color, DxtCompressed, Dxt1, Dxt3 or Dxt5.");
            }
            return *parsed;
        }

        /** @brief The CNB format a resolved build format stores, or Rgba8 when uncompressed. */
        [[nodiscard]] Cnb::CnbTextureFormat CnbFormatFor(const TextureBuildFormat format)
        {
            switch (format)
            {
            case TextureBuildFormat::Dxt1: return Cnb::CnbTextureFormat::Bc1;
            case TextureBuildFormat::Dxt3: return Cnb::CnbTextureFormat::Bc2;
            case TextureBuildFormat::Dxt5: return Cnb::CnbTextureFormat::Bc3;
            case TextureBuildFormat::NoChange:
            case TextureBuildFormat::Color:
            case TextureBuildFormat::DxtCompressed:
            default: return Cnb::CnbTextureFormat::Rgba8;
            }
        }

        /** @brief Whether any texel's alpha is neither fully opaque nor fully transparent. */
        [[nodiscard]] bool ImageHasPartialAlpha(const Cnb::CnbTextureData& texture)
        {
            for (const Cnb::CnbTextureRepresentation& representation : texture.representations)
            {
                if (representation.format != Cnb::CnbTextureFormat::Rgba8) { continue; }
                for (const std::vector<std::uint8_t>& level : representation.levels)
                {
                    for (std::size_t texel = 3u; texel < level.size(); texel += 4u)
                    {
                        if (level[texel] != 0u && level[texel] != 255u) { return true; }
                    }
                }
            }
            return false;
        }

        /** @brief Replaces a texture's Rgba8 representation with a block-compressed one. */
        void CompressTextureLevels(Cnb::CnbTextureData& texture,
                                   const Cnb::CnbTextureFormat target,
                                   const TextureBlockEncoder& encoder)
        {
            for (Cnb::CnbTextureRepresentation& representation : texture.representations)
            {
                if (representation.format != Cnb::CnbTextureFormat::Rgba8) { continue; }
                for (std::size_t index = 0; index < representation.levels.size(); ++index)
                {
                    std::uint32_t width = 0;
                    std::uint32_t height = 0;
                    std::uint32_t depth = 0;
                    // Levels are stored face-major then mip, so the mip a flat index names is
                    // its position within one face.
                    const std::uint32_t mip = static_cast<std::uint32_t>(
                        texture.mipCount == 0u ? 0u : index % texture.mipCount);
                    Cnb::CnbTextureLevelDimensions(texture, mip, width, height, depth);
                    representation.levels[index] =
                        encoder(target, representation.levels[index], width, height);
                }
                representation.format = target;
                return;
            }
        }

        /**
         * @brief Resamples one axis of an image, area-averaging down and interpolating up.
         *
         * Down-sampling accumulates each destination texel's exact overlap with the source
         * texels it covers, in integers, so a two-to-one reduction is a plain average and an
         * awkward ratio is still an area average rather than a dropped column.
         */
        [[nodiscard]] std::vector<std::uint8_t> ResampleAxis(
            const std::vector<std::uint8_t>& source, const std::uint32_t sourceLength,
            const std::uint32_t targetLength, const std::uint32_t otherLength,
            const bool horizontal)
        {
            std::vector<std::uint8_t> target(static_cast<std::size_t>(targetLength) *
                                             otherLength * 4u);
            const auto sourceIndex = [&](const std::uint32_t along, const std::uint32_t across)
            {
                return (horizontal ? static_cast<std::size_t>(across) * sourceLength + along
                                   : static_cast<std::size_t>(along) * otherLength + across) *
                       4u;
            };
            const auto targetIndex = [&](const std::uint32_t along, const std::uint32_t across)
            {
                return (horizontal ? static_cast<std::size_t>(across) * targetLength + along
                                   : static_cast<std::size_t>(along) * otherLength + across) *
                       4u;
            };

            for (std::uint32_t along = 0; along < targetLength; ++along)
            {
                if (targetLength <= sourceLength)
                {
                    const std::uint64_t start = static_cast<std::uint64_t>(along) * sourceLength;
                    const std::uint64_t end =
                        static_cast<std::uint64_t>(along + 1u) * sourceLength;
                    const std::uint32_t first =
                        static_cast<std::uint32_t>(start / targetLength);
                    const std::uint32_t last = static_cast<std::uint32_t>(
                        std::min<std::uint64_t>((end - 1u) / targetLength, sourceLength - 1u));
                    for (std::uint32_t across = 0; across < otherLength; ++across)
                    {
                        std::array<std::uint64_t, 4> sums{};
                        for (std::uint32_t sample = first; sample <= last; ++sample)
                        {
                            const std::uint64_t low =
                                std::max<std::uint64_t>(start, static_cast<std::uint64_t>(sample) *
                                                                   targetLength);
                            const std::uint64_t high = std::min<std::uint64_t>(
                                end, static_cast<std::uint64_t>(sample + 1u) * targetLength);
                            const std::uint64_t weight = high - low;
                            const std::size_t offset = sourceIndex(sample, across);
                            for (std::size_t channel = 0; channel < 4u; ++channel)
                            {
                                sums[channel] += weight * source[offset + channel];
                            }
                        }
                        const std::size_t destination = targetIndex(along, across);
                        for (std::size_t channel = 0; channel < 4u; ++channel)
                        {
                            target[destination + channel] = static_cast<std::uint8_t>(
                                (sums[channel] + sourceLength / 2u) / sourceLength);
                        }
                    }
                }
                else
                {
                    // Sample the source at the destination texel's centre, which keeps a
                    // magnified image aligned with the original rather than shifted half a texel.
                    const std::int64_t scaled =
                        (static_cast<std::int64_t>(2u * along + 1u) * sourceLength * 65536) /
                            (2 * static_cast<std::int64_t>(targetLength)) -
                        32768;
                    const std::int64_t clamped = std::clamp<std::int64_t>(
                        scaled, 0, static_cast<std::int64_t>(sourceLength - 1u) * 65536);
                    const std::uint32_t first =
                        static_cast<std::uint32_t>(clamped / 65536);
                    const std::uint32_t second = std::min(first + 1u, sourceLength - 1u);
                    const std::uint64_t fraction =
                        static_cast<std::uint64_t>(clamped % 65536);
                    for (std::uint32_t across = 0; across < otherLength; ++across)
                    {
                        const std::size_t low = sourceIndex(first, across);
                        const std::size_t high = sourceIndex(second, across);
                        const std::size_t destination = targetIndex(along, across);
                        for (std::size_t channel = 0; channel < 4u; ++channel)
                        {
                            const std::uint64_t blended =
                                static_cast<std::uint64_t>(source[low + channel]) *
                                    (65536u - fraction) +
                                static_cast<std::uint64_t>(source[high + channel]) * fraction;
                            target[destination + channel] =
                                static_cast<std::uint8_t>((blended + 32768u) / 65536u);
                        }
                    }
                }
            }
            return target;
        }

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

    std::vector<std::string> ImageImporter::OutputTypes() const
    {
        return {ImportedImageType};
    }

    ContentValue ImageImporter::Import(ContentImporterContext& context) const
    {
        ImportedImage imported = DecodeImportedImage(context.SourcePath());
        context.LogInfo("decoded " + std::to_string(imported.width) + "x" +
                        std::to_string(imported.height) + " Rgba8 image.");
        return ContentValue::Create(ImportedImageType, std::move(imported));
    }

    ImportedImage DecodeImportedImage(const std::filesystem::path& source)
    {
        std::ifstream stream(source, std::ios::binary);
        if (!stream)
        {
            throw ContentLoadException("cannot open image source.");
        }
        const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(stream),
                                              std::istreambuf_iterator<char>()};
        CNA::Internal::Graphics::ImageData image =
            CNA::Internal::Graphics::ImageLoader::LoadFromMemory(bytes.data(), bytes.size());
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
        return imported;
    }

    std::optional<TextureBuildFormat> TryParseTextureBuildFormat(const std::string& value)
    {
        const std::string lowered = Lowercase(value);
        if (lowered == "nochange") { return TextureBuildFormat::NoChange; }
        if (lowered == "color") { return TextureBuildFormat::Color; }
        if (lowered == "dxtcompressed") { return TextureBuildFormat::DxtCompressed; }
        if (lowered == "dxt1") { return TextureBuildFormat::Dxt1; }
        if (lowered == "dxt3") { return TextureBuildFormat::Dxt3; }
        if (lowered == "dxt5") { return TextureBuildFormat::Dxt5; }
        return std::nullopt;
    }

    std::string TextureBuildFormatName(const TextureBuildFormat format)
    {
        switch (format)
        {
        case TextureBuildFormat::NoChange: return "NoChange";
        case TextureBuildFormat::Color: return "Color";
        case TextureBuildFormat::DxtCompressed: return "DxtCompressed";
        case TextureBuildFormat::Dxt1: return "Dxt1";
        case TextureBuildFormat::Dxt3: return "Dxt3";
        case TextureBuildFormat::Dxt5: return "Dxt5";
        }
        return "Unknown";
    }

    std::uint32_t NextPowerOfTwoDimension(const std::uint32_t value)
    {
        std::uint32_t power = 1u;
        while (power < value && power < 0x40000000u) { power *= 2u; }
        return power;
    }

    std::vector<std::uint8_t> ResampleRgbaImage(std::span<const std::uint8_t> rgba,
                                                const std::uint32_t sourceWidth,
                                                const std::uint32_t sourceHeight,
                                                const std::uint32_t targetWidth,
                                                const std::uint32_t targetHeight)
    {
        if (sourceWidth == 0u || sourceHeight == 0u || targetWidth == 0u || targetHeight == 0u)
        {
            throw std::invalid_argument("ResampleRgbaImage: every dimension must be at least 1.");
        }
        const std::size_t expected =
            static_cast<std::size_t>(sourceWidth) * sourceHeight * 4u;
        if (rgba.size() != expected)
        {
            throw std::invalid_argument(
                "ResampleRgbaImage: " + std::to_string(sourceWidth) + "x" +
                std::to_string(sourceHeight) + " needs exactly " + std::to_string(expected) +
                " RGBA bytes, but " + std::to_string(rgba.size()) + " were supplied.");
        }

        std::vector<std::uint8_t> pixels(rgba.begin(), rgba.end());
        if (targetWidth != sourceWidth)
        {
            pixels = ResampleAxis(pixels, sourceWidth, targetWidth, sourceHeight, true);
        }
        if (targetHeight != sourceHeight)
        {
            pixels = ResampleAxis(pixels, sourceHeight, targetHeight, targetWidth, false);
        }
        return pixels;
    }

    void PremultiplyRgbaAlpha(std::vector<std::uint8_t>& rgba)
    {
        for (std::size_t texel = 0; texel + 3u < rgba.size(); texel += 4u)
        {
            const std::uint32_t alpha = rgba[texel + 3u];
            if (alpha == 255u) { continue; }
            for (std::size_t channel = 0; channel < 3u; ++channel)
            {
                rgba[texel + channel] = static_cast<std::uint8_t>(
                    (static_cast<std::uint32_t>(rgba[texel + channel]) * alpha + 127u) / 255u);
            }
        }
    }

    std::vector<std::vector<std::uint8_t>> GenerateRgbaMipChain(
        std::span<const std::uint8_t> level0, const std::uint32_t width,
        const std::uint32_t height)
    {
        std::vector<std::vector<std::uint8_t>> levels;
        std::vector<std::uint8_t> current(level0.begin(), level0.end());
        std::uint32_t currentWidth = width;
        std::uint32_t currentHeight = height;
        while (currentWidth > 1u || currentHeight > 1u)
        {
            const std::uint32_t nextWidth = std::max(1u, currentWidth / 2u);
            const std::uint32_t nextHeight = std::max(1u, currentHeight / 2u);
            current = ResampleRgbaImage(current, currentWidth, currentHeight, nextWidth,
                                        nextHeight);
            currentWidth = nextWidth;
            currentHeight = nextHeight;
            levels.push_back(current);
        }
        return levels;
    }

    Cnb::CnbTextureData BuildCnbTexture2DData(ImportedImage image)
    {
        std::vector<std::vector<std::uint8_t>> levels;
        levels.reserve(1u + image.additionalRgbaMipLevels.size());
        levels.push_back(std::move(image.rgbaPixels));
        levels.insert(levels.end(),
                      std::make_move_iterator(image.additionalRgbaMipLevels.begin()),
                      std::make_move_iterator(image.additionalRgbaMipLevels.end()));

        Cnb::CnbTextureData texture;
        texture.width = image.width;
        texture.height = image.height;
        texture.depth = 1u;
        texture.faceCount = 1u;
        texture.mipCount = static_cast<std::uint32_t>(levels.size());
        Cnb::CnbTextureRepresentation representation;
        representation.format = Cnb::CnbTextureFormat::Rgba8;
        representation.levels = std::move(levels);
        texture.representations.push_back(std::move(representation));
        return texture;
    }

    TextureProcessor::TextureProcessor(TextureBlockEncoder encoder)
        : encoder_(std::move(encoder))
    {
    }

    ContentComponentIdentity TextureProcessor::Identity() const
    {
        // Build version 2: the processor gained the textureFormat, generateMipmaps,
        // premultiplyAlpha and resizeToPowerOfTwo parameters, and premultiplication is on by
        // default, so previously built outputs must not be treated as current.
        return {kTextureProcessorName, "2"};
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
            if (name != TextureColorKeyParameter && name != TextureFormatParameter &&
                name != TextureGenerateMipmapsParameter &&
                name != TexturePremultiplyAlphaParameter &&
                name != TextureResizeToPowerOfTwoParameter)
            {
                throw std::invalid_argument("TextureProcessor does not recognize parameter '" +
                                            name + "'.");
            }
        }
        static_cast<void>(ReadColorKey(parameters));
        const TextureBuildFormat format = ReadFormatParameter(parameters);
        static_cast<void>(ReadBooleanParameter(parameters, TextureGenerateMipmapsParameter,
                                               false));
        static_cast<void>(ReadBooleanParameter(parameters, TexturePremultiplyAlphaParameter,
                                               false));
        static_cast<void>(ReadBooleanParameter(parameters, TextureResizeToPowerOfTwoParameter,
                                               false));

        if (CnbFormatFor(format) != Cnb::CnbTextureFormat::Rgba8 ||
            format == TextureBuildFormat::DxtCompressed)
        {
            if (!encoder_)
            {
                throw std::invalid_argument(
                    "TextureProcessor parameter 'textureFormat' asks for " +
                    TextureBuildFormatName(format) +
                    ", but this registry was built without a block-compression encoder. "
                    "Register the pipeline through cna_content_pipeline (the cna-content tool "
                    "does) to enable compressed texture formats.");
            }
        }
    }

    ContentValue TextureProcessor::Process(const ContentValue& input,
                                           ContentProcessorContext& context) const
    {
        ImportedImage image = input.Get<ImportedImage>();
        const ContentProcessorParameters& parameters = context.Parameters();

        // The order below is the whole texture policy, and it is deliberate:
        //   colour key -> resize -> premultiply -> mip chain -> block compression.
        // Premultiplication precedes mip generation because averaging colours that have not been
        // multiplied by their own alpha mixes the colour of invisible texels into visible ones,
        // which is what produces dark or bright halos around cut-out edges in a distant mip.
        std::optional<std::array<std::uint8_t, 3>> colorKey = ReadColorKey(parameters);
        if (!colorKey.has_value()) { colorKey = image.authoredColorKey; }
        if (colorKey.has_value())
        {
            const auto applyColorKey = [&](std::vector<std::uint8_t>& pixels)
            {
                for (std::size_t index = 0u; index + 3u < pixels.size(); index += 4u)
                {
                    if (pixels[index] == (*colorKey)[0] &&
                        pixels[index + 1u] == (*colorKey)[1] &&
                        pixels[index + 2u] == (*colorKey)[2])
                    {
                        pixels[index + 3u] = 0u;
                    }
                }
            };
            applyColorKey(image.rgbaPixels);
            for (std::vector<std::uint8_t>& pixels : image.additionalRgbaMipLevels)
            {
                applyColorKey(pixels);
            }
        }

        if (ReadBooleanParameter(parameters, TextureResizeToPowerOfTwoParameter, false))
        {
            const std::uint32_t width = NextPowerOfTwoDimension(image.width);
            const std::uint32_t height = NextPowerOfTwoDimension(image.height);
            if (width != image.width || height != image.height)
            {
                image.rgbaPixels = ResampleRgbaImage(image.rgbaPixels, image.width, image.height,
                                                     width, height);
                if (!image.additionalRgbaMipLevels.empty())
                {
                    // The source's own mip chain described the original dimensions and no longer
                    // describes anything; saying so is better than silently shipping levels that
                    // do not match their own level zero.
                    context.LogWarning(
                        "resizeToPowerOfTwo replaced level zero, so the source's " +
                        std::to_string(image.additionalRgbaMipLevels.size()) +
                        " authored mip level(s) were discarded.");
                    image.additionalRgbaMipLevels.clear();
                }
                context.LogInfo("resized " + std::to_string(image.width) + "x" +
                                std::to_string(image.height) + " to " + std::to_string(width) +
                                "x" + std::to_string(height) + ".");
                image.width = width;
                image.height = height;
            }
        }

        // XNA 4.0's TextureProcessor defaults PremultiplyAlpha to true, and CNA's own
        // BlendState::AlphaBlend -- SpriteBatch's default -- is the premultiplied blend that
        // expects it. CNA's default is nevertheless false, because flipping it would silently
        // change the bytes of every texture every existing CNA project has already built, and
        // three separate equivalence contracts in this repository pin the current output. The
        // divergence is recorded in plans/plan_xnapipeline.md rather than hidden; a project that
        // wants XNA's appearance sets the parameter.
        if (ReadBooleanParameter(parameters, TexturePremultiplyAlphaParameter, false))
        {
            PremultiplyRgbaAlpha(image.rgbaPixels);
            for (std::vector<std::uint8_t>& pixels : image.additionalRgbaMipLevels)
            {
                PremultiplyRgbaAlpha(pixels);
            }
        }

        if (ReadBooleanParameter(parameters, TextureGenerateMipmapsParameter, false) &&
            image.additionalRgbaMipLevels.empty())
        {
            image.additionalRgbaMipLevels =
                GenerateRgbaMipChain(image.rgbaPixels, image.width, image.height);
        }

        const TextureBuildFormat requested = ReadFormatParameter(parameters);
        Cnb::CnbTextureData texture = BuildCnbTexture2DData(std::move(image));

        TextureBuildFormat resolved = requested;
        if (resolved == TextureBuildFormat::DxtCompressed)
        {
            // BC1 carries one bit of alpha exactly, so a cut-out mask costs nothing; anything in
            // between needs BC3's interpolated alpha. This is the choice XNA's own DxtCompressed
            // makes, and it is made from the pixels rather than from the file's extension.
            resolved = ImageHasPartialAlpha(texture) ? TextureBuildFormat::Dxt5
                                                     : TextureBuildFormat::Dxt1;
        }

        const Cnb::CnbTextureFormat target = CnbFormatFor(resolved);
        if (target != Cnb::CnbTextureFormat::Rgba8)
        {
            if (context.OutputFormat() != ContentOutputFormat::Xnb)
            {
                // CNB texture schema 1 is frozen to Rgba8 (plans/plan_cnb.md CNBF-101A), so the
                // compressed representation has nowhere to go in a .cnb. Building the asset
                // uncompressed and saying so beats failing a whole build over a format the
                // other container would have accepted.
                context.LogWarning(
                    "textureFormat " + TextureBuildFormatName(resolved) +
                    " has no representation in CNB texture schema 1, which stores Rgba8 only; "
                    "this .cnb keeps the uncompressed pixels. Build with --format xnb to get "
                    "the compressed texture.");
            }
            else if (!encoder_)
            {
                throw std::invalid_argument(
                    "textureFormat " + TextureBuildFormatName(resolved) +
                    " needs a block-compression encoder and this registry has none.");
            }
            else
            {
                CompressTextureLevels(texture, target, encoder_);
                context.LogInfo("compressed " + std::to_string(texture.mipCount) +
                                " mip level(s) to " + Cnb::CnbTextureFormatToString(target) +
                                ".");
            }
        }

        context.LogInfo("prepared Texture2D mip data for encoding.");
        return ContentValue::Create(ProcessedTexture2DType, std::move(texture));
    }

    ContentComponentIdentity Texture2DContentWriter::Identity() const
    {
        return {kTextureWriterName, "1"};
    }

    std::vector<ContentWriterSchemaIdentity>
    Texture2DContentWriter::OutputSchemaIdentities() const
    {
        return {{Cnb::CnbAssetTypeId::Texture2D, Cnb::CnbTextureSchemaVersion,
                 "Microsoft.Xna.Framework.Graphics.Texture2D",
                 {"CNA.Cnb.EncodeTexture2DToCnb", "1"}}};
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
                "Microsoft.Xna.Framework.Graphics.Texture2D",
                Cnb::CnbTextureSchemaVersion};
    }

    void RegisterTexture2DContentPipeline(ContentPipelineRegistry& registry,
                                          TextureBlockEncoder encoder)
    {
        registry.RegisterImporter(std::make_shared<ImageImporter>());
        registry.RegisterProcessor(std::make_shared<TextureProcessor>(std::move(encoder)));
        registry.RegisterWriter(std::make_shared<Texture2DContentWriter>());
    }
}

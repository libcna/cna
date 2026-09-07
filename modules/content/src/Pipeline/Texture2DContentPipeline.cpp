// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Pipeline/Texture2DContentPipeline.hpp"

#include "CNA/Content/Cnb/CnbSourceImport.hpp"
#include "CNA/Content/Pipeline/CnjContentPipeline.hpp"

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
#include "CNA/Internal/Graphics/DdsSurfaceReader.hpp"
#include "CNA/Internal/Graphics/DxtUtil.hpp"
#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include "CNA/Internal/Graphics/PfmDecoder.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
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
            throw ContentParameterError(ContentParameterFault::UnconvertibleValue, name,
                                        std::string("TextureProcessor parameter '") + name +
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
                throw ContentParameterError(
                    ContentParameterFault::UnconvertibleValue, TextureFormatParameter,
                    "TextureProcessor parameter 'textureFormat' must be a string.");
            }
            const std::optional<TextureBuildFormat> parsed = TryParseTextureBuildFormat(*text);
            if (!parsed.has_value())
            {
                throw ContentParameterError(
                    ContentParameterFault::UnconvertibleValue, TextureFormatParameter,
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

        std::array<std::uint8_t, 4> ParseColorKey(const std::string& text)
        {
            // Three components or four. XNA's own `ColorKeyColor` is a Color and a `.contentproj`
            // writes all four of them, so a build that only accepted `R,G,B` refused every project
            // that named a key at all (plans/plan_xnapipeline_parity.md XNAPP-251). Written with
            // three, the alpha is 255, which is the only value an opaque source has.
            std::vector<std::string_view> components;
            std::size_t start = 0u;
            while (true)
            {
                const std::size_t comma = text.find(',', start);
                const std::size_t end = comma == std::string::npos ? text.size() : comma;
                components.push_back(std::string_view(text).substr(start, end - start));
                if (comma == std::string::npos) { break; }
                start = end + 1u;
            }
            if (components.size() != 3u && components.size() != 4u)
            {
                throw ContentParameterError(
                    ContentParameterFault::UnconvertibleValue, TextureColorKeyParameter,
                    "TextureProcessor parameter 'colorKey' must contain three or four components "
                    "in R,G,B or R,G,B,A form.");
            }
            std::array<std::uint8_t, 4> result{0u, 0u, 0u, 255u};
            for (std::size_t component = 0u; component < components.size(); ++component)
            {
                result[component] = ParseColorComponent(components[component]);
            }
            return result;
        }


        /**
         * @brief The level-0 surface of a DDS, as RGBA8.
         *
         * A DDS is the one source here whose payload can already be compressed, and this route's
         * output is `Rgba8`, so the blocks are decompressed on the way through. That is not a
         * shortcut: CNB texture schema 1 stores `Rgba8`, and the XNA façade -- which does keep DXT
         * blocks, because an `.xnb` can carry them -- reads the same file through the same reader.
         */
        [[nodiscard]] CNA::Internal::Graphics::ImageData DecodeDdsLevelZero(
            const std::vector<std::uint8_t>& bytes, const std::string& origin)
        {
            const CNA::Internal::Graphics::DdsSurfaces surfaces =
                CNA::Internal::Graphics::ReadDdsSurfaces(bytes, origin);
            if (surfaces.isCube || surfaces.isVolume)
            {
                throw ContentLoadException(
                    std::string("DDS source is a ") + (surfaces.isCube ? "cube map" : "volume") +
                    "; this route builds a Texture2D. Build it through the TextureCube or "
                    "Texture3D route instead.");
            }
            if (surfaces.surfaces.empty() || surfaces.surfaces.front().empty())
            {
                throw ContentLoadException("DDS source carries no surface.");
            }
            const std::vector<std::uint8_t>& payload = surfaces.surfaces.front().front();
            const int width = static_cast<int>(surfaces.width);
            const int height = static_cast<int>(surfaces.height);
            CNA::Internal::Graphics::ImageData image;
            image.width = width;
            image.height = height;
            switch (surfaces.format)
            {
                case CNA::Internal::Graphics::DdsSurfaceFormat::Color:
                    image.pixels = payload;
                    break;
                case CNA::Internal::Graphics::DdsSurfaceFormat::Dxt1:
                    image.pixels = CNA::Internal::Graphics::DxtUtil::DecompressDxt1(
                        payload.data(), payload.size(), width, height);
                    break;
                case CNA::Internal::Graphics::DdsSurfaceFormat::Dxt3:
                    image.pixels = CNA::Internal::Graphics::DxtUtil::DecompressDxt3(
                        payload.data(), payload.size(), width, height);
                    break;
                case CNA::Internal::Graphics::DdsSurfaceFormat::Dxt5:
                    image.pixels = CNA::Internal::Graphics::DxtUtil::DecompressDxt5(
                        payload.data(), payload.size(), width, height);
                    break;
            }
            return image;
        }

        /**
         * @brief One surface of a DDS, as Rgba8.
         *
         * @param surfaces The whole DDS as the shared reader answered it.
         * @param payload The surface's own bytes.
         * @return The decoded texels.
         */
        [[nodiscard]] std::vector<std::uint8_t> DdsSurfaceAsRgba8(
            const CNA::Internal::Graphics::DdsSurfaces& surfaces,
            const std::vector<std::uint8_t>& payload)
        {
            const int width = static_cast<int>(surfaces.width);
            const int height = static_cast<int>(surfaces.height);
            switch (surfaces.format)
            {
            case CNA::Internal::Graphics::DdsSurfaceFormat::Dxt1:
                return CNA::Internal::Graphics::DxtUtil::DecompressDxt1(
                    payload.data(), payload.size(), width, height);
            case CNA::Internal::Graphics::DdsSurfaceFormat::Dxt3:
                return CNA::Internal::Graphics::DxtUtil::DecompressDxt3(
                    payload.data(), payload.size(), width, height);
            case CNA::Internal::Graphics::DdsSurfaceFormat::Dxt5:
                return CNA::Internal::Graphics::DxtUtil::DecompressDxt5(
                    payload.data(), payload.size(), width, height);
            case CNA::Internal::Graphics::DdsSurfaceFormat::Color:
            default:
                return payload;
            }
        }

        /**
         * @brief A DDS cube map's six faces as canonical texture data.
         *
         * Built from the shared surface reader rather than from the older cube decoder beside it,
         * which accepts a narrower set of pixel formats: this route has to take whatever
         * `XNAPP-165`'s reader takes, or a source XNA builds would be one CNA refuses
         * (plans/plan_xnapipeline_parity.md XNAPP-255).
         *
         * @param surfaces The whole DDS as the shared reader answered it.
         * @param origin Text naming the source in a diagnostic.
         * @return The cube, one Rgba8 representation of six faces.
         */
        [[nodiscard]] Cnb::CnbTextureData DecodeDdsCube(
            const CNA::Internal::Graphics::DdsSurfaces& surfaces, const std::string& origin)
        {
            if (surfaces.surfaces.size() < 6u)
            {
                throw ContentLoadException(
                    "'" + origin + "' declares a cube map and carries " +
                    std::to_string(surfaces.surfaces.size()) + " face(s).");
            }
            Cnb::CnbTextureData cube;
            cube.width = surfaces.width;
            cube.height = surfaces.height;
            cube.depth = 1u;
            cube.faceCount = 6u;
            cube.mipCount = 1u;
            Cnb::CnbTextureRepresentation representation;
            representation.format = Cnb::CnbTextureFormat::Rgba8;
            for (std::uint32_t face = 0u; face < 6u; ++face)
            {
                if (surfaces.surfaces[face].empty())
                {
                    throw ContentLoadException("'" + origin + "' has an empty cube face.");
                }
                representation.levels.push_back(
                    DdsSurfaceAsRgba8(surfaces, surfaces.surfaces[face].front()));
            }
            cube.representations.push_back(std::move(representation));
            return cube;
        }

        /**
         * @brief Every slice of a DDS volume's level zero, as one tightly packed Rgba8 block.
         *
         * The same conversion DecodeDdsLevelZero() does, applied slice by slice: a volume's
         * surfaces arrive as one entry per slice, and `ImportedTexture3D` wants them concatenated
         * in slice order, which is the order the CNB and XNB writers store them in
         * (plans/plan_xnapipeline_parity.md XNAPP-255).
         *
         * @param surfaces The whole DDS as the shared reader answered it.
         * @param origin Text naming the source in a diagnostic.
         * @return The volume.
         */
        [[nodiscard]] ImportedTexture3D DecodeDdsVolume(
            const CNA::Internal::Graphics::DdsSurfaces& surfaces, const std::string& origin)
        {
            ImportedTexture3D volume;
            volume.width = surfaces.width;
            volume.height = surfaces.height;
            volume.depth = surfaces.depth;
            if (surfaces.surfaces.size() < surfaces.depth)
            {
                throw ContentLoadException(
                    "'" + origin + "' declares a volume of depth " +
                    std::to_string(surfaces.depth) + " and carries " +
                    std::to_string(surfaces.surfaces.size()) + " surface(s).");
            }
            for (std::uint32_t slice = 0u; slice < surfaces.depth; ++slice)
            {
                if (surfaces.surfaces[slice].empty())
                {
                    throw ContentLoadException("'" + origin + "' has an empty volume slice.");
                }
                const std::vector<std::uint8_t> rgba =
                    DdsSurfaceAsRgba8(surfaces, surfaces.surfaces[slice].front());
                volume.rgbaPixels.insert(volume.rgbaPixels.end(), rgba.begin(), rgba.end());
            }
            return volume;
        }

        /**
         * @brief A portable float map, as RGBA8.
         *
         * The floats are packed with the same rule `Color(Vector4)` uses, so a PFM compiled here
         * holds the bytes the XNA façade's `Vector4` bitmap would have produced once its processor
         * asked for `Color` -- one conversion rule, not two.
         */
        [[nodiscard]] CNA::Internal::Graphics::ImageData DecodePfmAsRgba8(
            const std::vector<std::uint8_t>& bytes, const std::string& origin)
        {
            const CNA::Internal::Graphics::DecodedPfm decoded =
                CNA::Internal::Graphics::DecodePfm(bytes, origin);
            CNA::Internal::Graphics::ImageData image;
            image.width = static_cast<int>(decoded.width);
            image.height = static_cast<int>(decoded.height);
            image.pixels.resize(decoded.pixels.size());
            for (std::size_t at = 0; at + 3u < decoded.pixels.size(); at += 4u)
            {
                const Microsoft::Xna::Framework::Color color(Microsoft::Xna::Framework::Vector4(
                    decoded.pixels[at], decoded.pixels[at + 1u], decoded.pixels[at + 2u],
                    decoded.pixels[at + 3u]));
                image.pixels[at] = color.getRProperty();
                image.pixels[at + 1u] = color.getGProperty();
                image.pixels[at + 2u] = color.getBProperty();
                image.pixels[at + 3u] = color.getAProperty();
            }
            return image;
        }

        /** @brief Every source this route reads, decoded to the RGBA8 the rest of it expects. */
        [[nodiscard]] CNA::Internal::Graphics::ImageData DecodeToRgba8(
            const std::vector<std::uint8_t>& bytes, const std::filesystem::path& source)
        {
            const std::string origin = source.filename().string();
            if (CNA::Internal::Graphics::IsDds(bytes)) { return DecodeDdsLevelZero(bytes, origin); }
            if (CNA::Internal::Graphics::IsPfm(bytes)) { return DecodePfmAsRgba8(bytes, origin); }
            // Everything else -- a `.dib` among them, which the shared decoder re-heads itself.
            return CNA::Internal::Graphics::ImageLoader::LoadFromMemory(bytes.data(), bytes.size());
        }

        std::optional<std::array<std::uint8_t, 4>> ReadColorKey(
            const ContentProcessorParameters& parameters)
        {
            const ContentProcessorParameterValue* value =
                parameters.Find(TextureColorKeyParameter);
            if (value == nullptr) { return std::nullopt; }
            const std::string* text = std::get_if<std::string>(value);
            if (text == nullptr)
            {
                throw ContentParameterError(
                    ContentParameterFault::UnconvertibleValue, TextureColorKeyParameter,
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
        // The last four are the ones XNA's own TextureImporter accepts and a plain stb decode does
        // not: a DDS surface, a headerless DIB, a portable float map and the `.ppm` spelling of the
        // portable anymap `.pnm` already covers. They are listed here rather than only on the XNA
        // façade because the façade is a façade -- a source the XNA importer accepts and the
        // canonical graph cannot route is a source that imports and never reaches an `.xnb`
        // (plans/plan_xnapipeline_parity.md XNAPP-021).
        return {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".psd", ".hdr",
                ".pic", ".pnm", ".dds", ".dib", ".pfm", ".ppm"};
    }

    std::vector<std::string> ImageImporter::OutputTypes() const
    {
        // Three, because a DDS is three formats wearing one extension. XNA's own TextureImporter
        // answers a Texture2DContent, a TextureCubeContent or a Texture3DContent depending on what
        // the header says, and this importer does the same: the shape of the source decides, and
        // the graph then resolves whichever processor takes what came out
        // (plans/plan_xnapipeline_parity.md XNAPP-255).
        return {ImportedImageType, ImportedTextureCubeType, ImportedTexture3DType};
    }

    ContentValue ImageImporter::Import(ContentImporterContext& context) const
    {
        std::ifstream stream(context.SourcePath(), std::ios::binary);
        if (!stream) { throw ContentLoadException("cannot open image source."); }
        const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(stream),
                                              std::istreambuf_iterator<char>()};
        const std::string origin = context.SourcePath().filename().string();
        if (CNA::Internal::Graphics::IsDds(bytes))
        {
            const CNA::Internal::Graphics::DdsSurfaces surfaces =
                CNA::Internal::Graphics::ReadDdsSurfaces(bytes, origin);
            if (surfaces.isCube)
            {
                ImportedTextureCube cube;
                cube.sourceData = DecodeDdsCube(surfaces, origin);
                context.LogInfo("decoded a " + std::to_string(surfaces.width) + "x" +
                                std::to_string(surfaces.height) + " DDS cube map.");
                return ContentValue::Create(ImportedTextureCubeType, std::move(cube));
            }
            if (surfaces.isVolume)
            {
                ImportedTexture3D volume = DecodeDdsVolume(surfaces, origin);
                context.LogInfo("decoded a " + std::to_string(volume.width) + "x" +
                                std::to_string(volume.height) + "x" +
                                std::to_string(volume.depth) + " DDS volume.");
                return ContentValue::Create(ImportedTexture3DType, std::move(volume));
            }
        }
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
        CNA::Internal::Graphics::ImageData image = DecodeToRgba8(bytes, source);
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
                // Truncated, not rounded. XNA's own pipeline answers 23 for a channel of 30 at
                // alpha 200 where rounding answers 24, and the difference reaches every texel of
                // every partially transparent texture (measured: `phone/png_texture` in the
                // differential corpus, plans/plan_xnapipeline_parity.md XNAPP-251).
                rgba[texel + channel] = static_cast<std::uint8_t>(
                    (static_cast<std::uint32_t>(rgba[texel + channel]) * alpha) / 255u);
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
        // Build version 3: premultiplyAlpha now defaults to true, matching XNA 4.0's own
        // TextureProcessor (plans/plan_xnapipeline.md XNAP-96). That changes the bytes of every
        // alpha-bearing texture, so the version must move or an incremental build would skip
        // artifacts that are no longer current. (Version 2 was the textureFormat/generateMipmaps/
        // premultiplyAlpha/resizeToPowerOfTwo parameter set arriving.)
        return {kTextureProcessorName, "3"};
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
                throw ContentParameterError(
                    ContentParameterFault::UnknownName, name,
                    "TextureProcessor does not recognize parameter '" + name + "'.");
            }
        }
        static_cast<void>(ReadColorKey(parameters));
        const TextureBuildFormat format = ReadFormatParameter(parameters);
        static_cast<void>(ReadBooleanParameter(parameters, TextureGenerateMipmapsParameter,
                                               false));
        static_cast<void>(ReadBooleanParameter(parameters, TexturePremultiplyAlphaParameter,
                                               true));
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
        // A key the *build* asked for is XNA's and clears the colour with the alpha; a key a
        // `.cnj` authored is CNA's own and keeps it. The two really do differ, and the difference
        // is invisible until premultiplication is turned off. XNA's rule is measured
        // (`texture/png4x4_no_premultiply`, plans/plan_xnapipeline_parity.md XNAPP-251); the
        // authored one is what every existing `.cnj` already compiles to, and changing that would
        // change what a committed document means (plans/plan_xnapipeline.md XNAP-96).
        std::optional<std::array<std::uint8_t, 4>> colorKey = ReadColorKey(parameters);
        bool clearKeyedColor = colorKey.has_value();
        if (!colorKey.has_value() && image.authoredColorKey.has_value())
        {
            const std::array<std::uint8_t, 3>& authored = *image.authoredColorKey;
            colorKey = std::array<std::uint8_t, 4>{authored[0], authored[1], authored[2], 255u};
            clearKeyedColor = false;
        }
        if (colorKey.has_value())
        {
            const auto applyColorKey = [&](std::vector<std::uint8_t>& pixels)
            {
                for (std::size_t index = 0u; index + 3u < pixels.size(); index += 4u)
                {
                    // Three channels, not four. XNA's `ColorKeyColor` is a Color and a project
                    // writes its alpha, but nothing measured here says whether that alpha takes
                    // part in the match: every case the corpus has is an opaque key against an
                    // opaque texel. Comparing it would be a guess, and a guess that stops keying
                    // texels a build used to key.
                    if (pixels[index] == (*colorKey)[0] &&
                        pixels[index + 1u] == (*colorKey)[1] &&
                        pixels[index + 2u] == (*colorKey)[2])
                    {
                        if (clearKeyedColor)
                        {
                            pixels[index] = 0u;
                            pixels[index + 1u] = 0u;
                            pixels[index + 2u] = 0u;
                        }
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

        // The graphics profile's own limits on a Texture2D, in XNA's own sentences and in XNA's
        // own order: the size first, then the aspect ratio. Measured by building the same image
        // under both profiles (`profile/*` in the differential corpus,
        // plans/plan_xnapipeline_parity.md XNAPP-253) -- a 2049x1 is refused by Reach for its size
        // and by HiDef for its shape, a 4097x1 by HiDef for its size, and a 2048x1 is fine in both.
        //
        // Checked after the resize, because that is the texture the profile has to hold, and only
        // when the build is producing an `.xnb`. A `.cnb` has no target profile at all: nothing in
        // that container records one, and refusing a 4096-wide texture because a flag defaulted to
        // Reach would be inventing a limit CNA's own format does not have.
        if (context.OutputFormat() == ContentOutputFormat::Xnb)
        {
            namespace XnaGraphics = Microsoft::Xna::Framework::Graphics;
            const bool hiDef =
                context.Environment().targetProfile == XnaGraphics::GraphicsProfile::HiDef;
            const std::string sized = std::to_string(image.width) + "x" +
                                      std::to_string(image.height);
            const std::uint32_t maximum = hiDef ? 4096u : 2048u;
            if (image.width > maximum || image.height > maximum)
            {
                throw ContentLoadException(
                    std::string("XNA Framework ") + (hiDef ? "HiDef" : "Reach") +
                    " profile supports a maximum Texture2D size of " + std::to_string(maximum) +
                    ", but this Texture2D is " + sized + ".");
            }
            // Only HiDef: a Reach texture wide enough to break an aspect limit of 2048 is already
            // wider than Reach's own 2048, so whether Reach has this rule cannot be observed and
            // is not asserted here.
            if (hiDef)
            {
                const std::uint32_t longer = std::max(image.width, image.height);
                const std::uint32_t shorter = std::max(1u, std::min(image.width, image.height));
                if (longer / shorter > 2048u)
                {
                    throw ContentLoadException(
                        "XNA Framework HiDef profile supports a maximum Texture2D aspect ratio of "
                        "2048, but this Texture2D is sized " + sized + ".");
                }
            }
            if (!hiDef && ReadBooleanParameter(parameters, TextureGenerateMipmapsParameter, false) &&
                (NextPowerOfTwoDimension(image.width) != image.width ||
                 NextPowerOfTwoDimension(image.height) != image.height))
            {
                throw ContentLoadException(
                    "XNA Framework Reach profile requires mipmapped Texture2D sizes to be powers "
                    "of two, but this Texture2D is " + sized +
                    ". Resize it to a power of two, or remove the mipmaps.");
            }
        }

        // XNAP-96: premultiplication defaults to on, exactly as XNA 4.0's TextureProcessor does,
        // because BlendState::AlphaBlend -- what SpriteBatch::Begin() selects when given no blend
        // state, in XNA and in CNA alike -- is the premultiplied blend. Content built without it
        // renders with dark fringes under the default blend state, which is a bug the author did
        // not write. A source that defines its own answer says so through
        // ImportedImage::authoredPremultiplyAlpha; an explicit parameter beats both.
        const bool premultiply = ReadBooleanParameter(
            parameters, TexturePremultiplyAlphaParameter,
            image.authoredPremultiplyAlpha.value_or(true));
        if (premultiply)
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
                // XNA refuses to block-compress a texture whose level-0 dimensions are not
                // multiples of four, and says so in exactly these words. Measured on the genuine
                // BuildContent from both sides: 2x2 and 3x2 are refused, 4x4 builds
                // (tests/reference/xna40/differential, cases texture/png_texture_dxt,
                // texture/png3x2_texture_dxt, texture/png4x4_texture_dxt). Without it a project
                // that asks for DXT on such a texture fails its build under XNA and quietly ships
                // a padded one from CNA (plans/plan_xnapipeline_parity.md XNAPP-265).
                //
                // Only on the path that actually compresses: a `.cnb` keeps the uncompressed
                // pixels and says so above, so XNA's rule has nothing to refuse there.
                if (texture.width % 4u != 0u || texture.height % 4u != 0u)
                {
                    throw ContentLoadException(
                        "Invalid texture. Face 0 is sized " + std::to_string(texture.width) + "x" +
                        std::to_string(texture.height) +
                        ", but textures using DXT compressed formats must be multiples of four.");
                }
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

// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Cnb/CnbTextureCodec.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "CNA/Content/Cnb/CnbArithmetic.hpp"
#include "CNA/Content/Cnb/CnbByteReader.hpp"
#include "CNA/Content/Cnb/CnbByteWriter.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;

namespace CNA::Content::Cnb
{
    namespace
    {
        // The canonical .NET names recorded in CMET. For a built-in type these are diagnostic
        // rather than load-bearing (docs/cnb-format.md §7), but they are what cnb_info prints.
        constexpr const char* kTexture2DName = "Microsoft.Xna.Framework.Graphics.Texture2D";
        constexpr const char* kTextureCubeName = "Microsoft.Xna.Framework.Graphics.TextureCube";
        constexpr const char* kTexture3DName = "Microsoft.Xna.Framework.Graphics.Texture3D";

        // 16-byte payload alignment: block-compressed formats have 8- and 16-byte units, and a
        // future memory-mapped reader (CNBF-108) wants the payload start to be at least as
        // aligned as the largest unit. The Model schema already aligns geometry the same way.
        constexpr std::uint32_t kPayloadAlignment = 16u;

        /// The shape rules an asset type imposes on the shared texture layout. Keeping them in one
        /// struct is what lets 2D, cube and 3D share a codec instead of three near-copies.
        struct TextureShape
        {
            std::uint32_t assetTypeId;
            const char* canonicalTypeName;
            const char* label;
            std::uint32_t requiredFaceCount;
            bool requireDepthOne;
            bool requireSquare;
        };

        constexpr TextureShape kTexture2D{CnbAssetTypeId::Texture2D, kTexture2DName, "Texture2D",
                                          1u, true, false};
        constexpr TextureShape kTextureCube{CnbAssetTypeId::TextureCube, kTextureCubeName,
                                            "TextureCube", CnbTextureCubeFaceCount, true, true};
        constexpr TextureShape kTexture3D{CnbAssetTypeId::Texture3D, kTexture3DName, "Texture3D",
                                          1u, false, false};

        [[noreturn]] void Fail(const TextureShape& shape, const std::string& what)
        {
            throw ContentLoadException(std::string("CNB ") + shape.label + ": " + what);
        }

        /// Validates the dimension/count fields both directions share, so an encoder cannot write
        /// a file its own decoder would reject.
        void ValidateShape(const TextureShape& shape, const CnbTextureData& data)
        {
            if (data.width == 0u || data.height == 0u || data.depth == 0u)
            {
                Fail(shape, "a texture cannot have a zero dimension.");
            }
            if (data.mipCount == 0u || data.mipCount > CnbMaxTextureMipLevels)
            {
                Fail(shape, "declares " + std::to_string(data.mipCount) +
                                " mip levels; the range is 1-" +
                                std::to_string(CnbMaxTextureMipLevels) + ".");
            }
            if (data.faceCount != shape.requiredFaceCount)
            {
                Fail(shape, "declares " + std::to_string(data.faceCount) + " faces; a " +
                                shape.label + " has exactly " +
                                std::to_string(shape.requiredFaceCount) + ".");
            }
            if (shape.requireDepthOne && data.depth != 1u)
            {
                Fail(shape, "declares depth " + std::to_string(data.depth) +
                                "; only a Texture3D may have a depth other than 1.");
            }
            if (shape.requireSquare && data.width != data.height)
            {
                Fail(shape, "declares a " + std::to_string(data.width) + "x" +
                                std::to_string(data.height) + " face; a cube face is square.");
            }
            if (data.representations.empty())
            {
                Fail(shape, "declares no representations; a texture must carry at least one.");
            }
            if (data.representations.size() > CnbMaxTextureRepresentations)
            {
                Fail(shape, "declares " + std::to_string(data.representations.size()) +
                                " representations; the ceiling is " +
                                std::to_string(CnbMaxTextureRepresentations) + ".");
            }
        }

        /// The decode-side counterpart of ValidateShape(). It is separate rather than shared
        /// because every value here came out of a file: the diagnostics must name the file, and
        /// the checks must run BEFORE the level arithmetic that would otherwise multiply
        /// attacker-chosen counts together.
        void ValidateShapeForDecode(const TextureShape& shape, const CnbTextureData& data,
                                    std::uint32_t representationCount, CnbByteReader& reader)
        {
            if (data.width == 0u || data.height == 0u || data.depth == 0u)
            {
                reader.Fail("declares a zero dimension (" + std::to_string(data.width) + "x" +
                            std::to_string(data.height) + "x" + std::to_string(data.depth) + ").");
            }
            if (data.mipCount == 0u || data.mipCount > CnbMaxTextureMipLevels)
            {
                reader.Fail("declares " + std::to_string(data.mipCount) +
                            " mip levels; the range is 1-" +
                            std::to_string(CnbMaxTextureMipLevels) + ".");
            }
            if (data.faceCount != shape.requiredFaceCount)
            {
                reader.Fail("declares " + std::to_string(data.faceCount) + " faces; a " +
                            shape.label + " has exactly " +
                            std::to_string(shape.requiredFaceCount) + ".");
            }
            if (shape.requireDepthOne && data.depth != 1u)
            {
                reader.Fail("declares depth " + std::to_string(data.depth) + "; only a Texture3D "
                            "may have a depth other than 1.");
            }
            if (shape.requireSquare && data.width != data.height)
            {
                reader.Fail("declares a " + std::to_string(data.width) + "x" +
                            std::to_string(data.height) + " face; a cube face is square.");
            }
            if (representationCount == 0u || representationCount > CnbMaxTextureRepresentations)
            {
                reader.Fail("declares " + std::to_string(representationCount) +
                            " representations; the range is 1-" +
                            std::to_string(CnbMaxTextureRepresentations) + ".");
            }
        }

        std::uint64_t LevelBytes(const CnbTextureData& data, CnbTextureFormat format,
                                 std::uint32_t level)
        {
            std::uint32_t w = 0u;
            std::uint32_t h = 0u;
            std::uint32_t d = 0u;
            CnbTextureLevelDimensions(data, level, w, h, d);
            return CnbTextureLevelByteSize(format, w, h, d);
        }

        /// The chunk-producing half of EncodeTexture(), factored out so a SpriteFont can embed a
        /// texture with byte-identical layout instead of growing its own copy of it.
        void AppendTextureChunks(const TextureShape& shape, const CnbTextureData& data,
                                 CnbWriter& writer)
        {
            ValidateShape(shape, data);

            const std::uint64_t levelsPerRepresentation =
                static_cast<std::uint64_t>(data.faceCount) * data.mipCount;

            CnbByteWriter header;
            header.WriteU32(data.width);
            header.WriteU32(data.height);
            header.WriteU32(data.depth);
            header.WriteU32(data.faceCount);
            header.WriteU32(data.mipCount);
            header.WriteU32(static_cast<std::uint32_t>(data.representations.size()));

            CnbByteWriter descriptors;
            std::vector<std::vector<std::uint8_t>> payloads;
            std::uint32_t nextOrdinal = 0u;

            for (const CnbTextureRepresentation& representation : data.representations)
            {
                if (!IsKnownCnbTextureFormat(static_cast<std::uint32_t>(representation.format)))
                {
                    Fail(shape, "a representation names " +
                                    CnbTextureFormatToString(representation.format) + ".");
                }
                // CNBF-101A: schema 1 encodes the portable baseline only. The descriptor table and
                // the format numbering are complete, so adding a block format later is a writer
                // change rather than a schema break -- which is the whole reason the layout is
                // multi-representation before there is a second representation to write.
                if (representation.format != CnbTextureFormat::Rgba8)
                {
                    Fail(shape, "CNB texture schema 1 encodes Rgba8 only; " +
                                    CnbTextureFormatToString(representation.format) +
                                    " is a reserved identifier with no writer yet.");
                }
                if (representation.levels.size() != levelsPerRepresentation)
                {
                    Fail(shape, "a representation carries " +
                                    std::to_string(representation.levels.size()) +
                                    " level payloads; " + std::to_string(data.faceCount) +
                                    " faces x " + std::to_string(data.mipCount) + " mips is " +
                                    std::to_string(levelsPerRepresentation) + ".");
                }

                std::uint64_t total = 0u;
                for (std::uint32_t face = 0u; face < data.faceCount; ++face)
                {
                    for (std::uint32_t mip = 0u; mip < data.mipCount; ++mip)
                    {
                        const std::size_t index =
                            static_cast<std::size_t>(face) * data.mipCount + mip;
                        const std::uint64_t expected =
                            LevelBytes(data, representation.format, mip);
                        if (representation.levels[index].size() != expected)
                        {
                            Fail(shape, "face " + std::to_string(face) + " mip " +
                                            std::to_string(mip) + " carries " +
                                            std::to_string(representation.levels[index].size()) +
                                            " bytes; its dimensions require " +
                                            std::to_string(expected) + ".");
                        }
                        total = CheckedAdd(total, expected, "CNB texture representation");
                    }
                }

                descriptors.WriteU32(static_cast<std::uint32_t>(representation.format));
                descriptors.WriteU32(nextOrdinal);
                descriptors.WriteU32(static_cast<std::uint32_t>(levelsPerRepresentation));
                descriptors.WriteU32(0u); // flags: reserved, must be zero
                descriptors.WriteU64(total);

                for (const std::vector<std::uint8_t>& level : representation.levels)
                {
                    payloads.push_back(level);
                }
                nextOrdinal += static_cast<std::uint32_t>(levelsPerRepresentation);
            }

            writer.AddChunk(CnbTextureChunk::Header, header.Take(), CnbChunkFlags::Mandatory, 4u);
            writer.AddChunk(CnbTextureChunk::Representations, descriptors.Take(),
                            CnbChunkFlags::Mandatory, 4u);
            for (std::vector<std::uint8_t>& payload : payloads)
            {
                writer.AddChunk(CnbTextureChunk::Payload, std::move(payload),
                                CnbChunkFlags::Mandatory, kPayloadAlignment);
            }
        }

        std::vector<std::uint8_t> EncodeTexture(const TextureShape& shape,
                                                const CnbTextureData& data,
                                                const std::string& contentName)
        {
            CnbWriter writer(shape.assetTypeId, CnbTextureSchemaVersion);
            writer.SetMetadata(shape.canonicalTypeName, contentName);
            AppendTextureChunks(shape, data, writer);
            return writer.Build();
        }

        /// The chunk-consuming half of DecodeTexture(). An embedded atlas reaches this directly:
        /// its owner has already checked the asset type, and the file's mandatory-chunk audit is
        /// the owner's job because the owner knows its own chunks too.
        CnbTextureData ReadTextureChunks(const TextureShape& shape, const CnbDocument& document)
        {
            CnbByteReader header = document.OpenChunk(document.RequireSingle(CnbTextureChunk::Header));
            CnbTextureData data;
            data.width = header.ReadU32();
            data.height = header.ReadU32();
            data.depth = header.ReadU32();
            data.faceCount = header.ReadU32();
            data.mipCount = header.ReadU32();
            const std::uint32_t representationCount = header.ReadU32();
            header.RequireExhausted();

            // Shape first: it is cheaper than anything below it and it is what makes the
            // level-count arithmetic safe to perform at all.
            ValidateShapeForDecode(shape, data, representationCount, header);

            const std::uint64_t levelsPerRepresentation =
                static_cast<std::uint64_t>(data.faceCount) * data.mipCount;

            const std::vector<std::size_t> payloadChunks =
                document.FindAll(CnbTextureChunk::Payload);

            CnbByteReader descriptors =
                document.OpenChunk(document.RequireSingle(CnbTextureChunk::Representations));
            const std::uint64_t descriptorBytes =
                CheckedMultiply(representationCount, CnbTextureRepresentationStride,
                                "CNB texture representations");
            if (descriptors.Remaining() != descriptorBytes)
            {
                descriptors.Fail("declares " + std::to_string(representationCount) +
                                 " representations, which is " + std::to_string(descriptorBytes) +
                                 " bytes, but the chunk holds " +
                                 std::to_string(descriptors.Remaining()) + ".");
            }

            data.representations.reserve(representationCount);
            std::uint64_t expectedOrdinal = 0u;
            for (std::uint32_t r = 0u; r < representationCount; ++r)
            {
                const std::uint32_t rawFormat = descriptors.ReadU32();
                const std::uint32_t firstOrdinal = descriptors.ReadU32();
                const std::uint32_t payloadCount = descriptors.ReadU32();
                const std::uint32_t flags = descriptors.ReadU32();
                const std::uint64_t declaredBytes = descriptors.ReadU64();

                if (!IsKnownCnbTextureFormat(rawFormat))
                {
                    descriptors.Fail("representation " + std::to_string(r) + " declares format " +
                                     std::to_string(rawFormat) +
                                     ", which this build does not understand.");
                }
                if (flags != 0u)
                {
                    descriptors.Fail("representation " + std::to_string(r) +
                                     " sets reserved flag bits; this container version defines "
                                     "none.");
                }
                if (payloadCount != levelsPerRepresentation)
                {
                    descriptors.Fail("representation " + std::to_string(r) + " claims " +
                                     std::to_string(payloadCount) + " payloads; " +
                                     std::to_string(data.faceCount) + " faces x " +
                                     std::to_string(data.mipCount) + " mips is " +
                                     std::to_string(levelsPerRepresentation) + ".");
                }
                // The representations must tile the TEXD list in order, with no gap and no
                // overlap. Anything else would leave a payload owned by two representations or by
                // none, and "owned by none" is a chunk the reader silently ignores.
                if (firstOrdinal != expectedOrdinal)
                {
                    descriptors.Fail("representation " + std::to_string(r) +
                                     " starts at payload ordinal " + std::to_string(firstOrdinal) +
                                     "; the previous representations end at " +
                                     std::to_string(expectedOrdinal) + ".");
                }

                const auto format = static_cast<CnbTextureFormat>(rawFormat);
                CnbTextureRepresentation representation;
                representation.format = format;
                representation.levels.resize(static_cast<std::size_t>(levelsPerRepresentation));

                std::uint64_t total = 0u;
                for (std::uint32_t face = 0u; face < data.faceCount; ++face)
                {
                    for (std::uint32_t mip = 0u; mip < data.mipCount; ++mip)
                    {
                        const std::uint64_t ordinal =
                            expectedOrdinal + static_cast<std::uint64_t>(face) * data.mipCount + mip;
                        if (ordinal >= payloadChunks.size())
                        {
                            descriptors.Fail("representation " + std::to_string(r) +
                                             " names payload ordinal " + std::to_string(ordinal) +
                                             ", but the file holds only " +
                                             std::to_string(payloadChunks.size()) +
                                             " TEXD chunks.");
                        }
                        const std::uint64_t expected = LevelBytes(data, format, mip);
                        const std::span<const std::uint8_t> bytes =
                            document.ChunkData(payloadChunks[static_cast<std::size_t>(ordinal)]);
                        if (bytes.size() != expected)
                        {
                            descriptors.Fail(
                                "representation " + std::to_string(r) + " face " +
                                std::to_string(face) + " mip " + std::to_string(mip) + " holds " +
                                std::to_string(bytes.size()) + " bytes; its dimensions require " +
                                std::to_string(expected) + ".");
                        }
                        const std::size_t index =
                            static_cast<std::size_t>(face) * data.mipCount + mip;
                        representation.levels[index].assign(bytes.begin(), bytes.end());
                        total = CheckedAdd(total, expected, "CNB texture representation");
                    }
                }
                if (declaredBytes != total)
                {
                    descriptors.Fail("representation " + std::to_string(r) + " declares " +
                                     std::to_string(declaredBytes) +
                                     " total payload bytes; its levels hold " +
                                     std::to_string(total) + ".");
                }

                data.representations.push_back(std::move(representation));
                expectedOrdinal += levelsPerRepresentation;
            }
            descriptors.RequireExhausted();

            if (expectedOrdinal != payloadChunks.size())
            {
                throw ContentLoadException(
                    std::string("CNB ") + shape.label + ": the file holds " +
                    std::to_string(payloadChunks.size()) + " TEXD chunks, but its representations "
                    "account for " + std::to_string(expectedOrdinal) +
                    ". A payload owned by no representation would be silently ignored.");
            }
            return data;
        }

        CnbTextureData DecodeTexture(const TextureShape& shape, const CnbDocument& document)
        {
            document.RequireAsset(shape.assetTypeId, CnbTextureSchemaVersion);
            const CnbChunkId known[] = {CnbTextureChunk::Header, CnbTextureChunk::Representations,
                                        CnbTextureChunk::Payload};
            document.RequireMandatoryChunksUnderstood(known);
            return ReadTextureChunks(shape, document);
        }
    }

    void AppendEmbeddedTexture2DChunks(CnbWriter& writer, const CnbTextureData& data,
                                       const char* label)
    {
        TextureShape shape = kTexture2D;
        shape.label = label;
        AppendTextureChunks(shape, data, writer);
    }

    CnbTextureData ReadEmbeddedTexture2DChunks(const CnbDocument& document, const char* label)
    {
        TextureShape shape = kTexture2D;
        shape.label = label;
        return ReadTextureChunks(shape, document);
    }

    void GenerateRgba8MipChain(CnbTextureData& data, CnbMipColorSpace colorSpace)
    {
        if (data.faceCount != 1u || data.depth != 1u || data.mipCount != 1u
            || data.representations.size() != 1u
            || data.representations.front().format != CnbTextureFormat::Rgba8
            || data.representations.front().levels.size() != 1u)
        {
            throw ContentLoadException(
                "GenerateRgba8MipChain: expected a single-mip, single-face, 2D Rgba8 texture.");
        }
        if (data.width == 0u || data.height == 0u)
        {
            throw ContentLoadException("GenerateRgba8MipChain: texture has a zero dimension.");
        }

        std::vector<std::uint8_t> level = data.representations.front().levels.front();
        const std::size_t expected =
            static_cast<std::size_t>(data.width) * data.height * 4u;
        if (level.size() != expected)
        {
            throw ContentLoadException(
                "GenerateRgba8MipChain: level 0 is " + std::to_string(level.size())
                + " bytes, expected " + std::to_string(expected) + ".");
        }

        // The sRGB transfer function, exactly as IEC 61966-2-1 states it. A
        // gamma-2.2 approximation would be cheaper and would be wrong by up to
        // three per cent in the darks, which is where a mip chain's error shows.
        const auto toLinear = [](std::uint8_t value) {
            const double s = static_cast<double>(value) / 255.0;
            return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
        };
        const auto toSrgb = [](double linear) {
            const double s = linear <= 0.0031308 ? linear * 12.92
                                                 : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
            const double scaled = std::clamp(s, 0.0, 1.0) * 255.0 + 0.5;
            return static_cast<std::uint8_t>(scaled);
        };

        std::uint32_t width = data.width;
        std::uint32_t height = data.height;
        std::vector<std::vector<std::uint8_t>> levels;
        levels.push_back(level);

        while (width > 1u || height > 1u)
        {
            const std::uint32_t nextWidth = std::max(1u, width / 2u);
            const std::uint32_t nextHeight = std::max(1u, height / 2u);
            std::vector<std::uint8_t> next(static_cast<std::size_t>(nextWidth) * nextHeight * 4u);

            for (std::uint32_t y = 0u; y < nextHeight; ++y)
            {
                // A dimension that has already reached 1 stops halving, and the
                // two source rows (or columns) collapse onto the same one. Not
                // clamping here reads off the end of a 1xN texture's last level.
                const std::uint32_t y0 = std::min(y * 2u, height - 1u);
                const std::uint32_t y1 = std::min(y * 2u + 1u, height - 1u);
                for (std::uint32_t x = 0u; x < nextWidth; ++x)
                {
                    const std::uint32_t x0 = std::min(x * 2u, width - 1u);
                    const std::uint32_t x1 = std::min(x * 2u + 1u, width - 1u);
                    const std::size_t source[4] = {
                        (static_cast<std::size_t>(y0) * width + x0) * 4u,
                        (static_cast<std::size_t>(y0) * width + x1) * 4u,
                        (static_cast<std::size_t>(y1) * width + x0) * 4u,
                        (static_cast<std::size_t>(y1) * width + x1) * 4u};
                    const std::size_t target =
                        (static_cast<std::size_t>(y) * nextWidth + x) * 4u;

                    for (std::size_t channel = 0u; channel < 3u; ++channel)
                    {
                        if (colorSpace == CnbMipColorSpace::Srgb)
                        {
                            double sum = 0.0;
                            for (const std::size_t at : source)
                                sum += toLinear(level[at + channel]);
                            next[target + channel] = toSrgb(sum * 0.25);
                        }
                        else
                        {
                            std::uint32_t sum = 0u;
                            for (const std::size_t at : source)
                                sum += level[at + channel];
                            next[target + channel] = static_cast<std::uint8_t>((sum + 2u) / 4u);
                        }
                    }
                    // Alpha is coverage, never a colour, so it is averaged
                    // linearly whatever the other three channels are.
                    std::uint32_t alpha = 0u;
                    for (const std::size_t at : source) alpha += level[at + 3u];
                    next[target + 3u] = static_cast<std::uint8_t>((alpha + 2u) / 4u);
                }
            }

            levels.push_back(next);
            level = std::move(next);
            width = nextWidth;
            height = nextHeight;
        }

        if (levels.size() > CnbMaxTextureMipLevels)
        {
            throw ContentLoadException(
                "GenerateRgba8MipChain: a chain of " + std::to_string(levels.size())
                + " levels exceeds the container's limit of "
                + std::to_string(CnbMaxTextureMipLevels) + ".");
        }

        data.mipCount = static_cast<std::uint32_t>(levels.size());
        data.representations.front().levels = std::move(levels);
    }

    void CnbTextureLevelDimensions(const CnbTextureData& data, std::uint32_t level,
                                   std::uint32_t& width, std::uint32_t& height,
                                   std::uint32_t& depth)
    {
        width = data.width;
        height = data.height;
        depth = data.depth;
        for (std::uint32_t i = 0u; i < level; ++i)
        {
            width = width > 1u ? width / 2u : 1u;
            height = height > 1u ? height / 2u : 1u;
            depth = depth > 1u ? depth / 2u : 1u;
        }
    }

    CnbTextureData MakeRgba8Texture2DData(std::uint32_t width, std::uint32_t height,
                                          std::vector<std::uint8_t> rgba)
    {
        if (width == 0u || height == 0u)
        {
            throw ContentLoadException("CNB Texture2D: a texture cannot have a zero dimension.");
        }
        const std::uint64_t expected =
            CnbTextureLevelByteSize(CnbTextureFormat::Rgba8, width, height, 1u);
        if (rgba.size() != expected)
        {
            throw ContentLoadException(
                "CNB Texture2D: " + std::to_string(width) + "x" + std::to_string(height) +
                " Rgba8 requires " + std::to_string(expected) + " bytes, but " +
                std::to_string(rgba.size()) + " were supplied.");
        }

        CnbTextureData data;
        data.width = width;
        data.height = height;
        data.depth = 1u;
        data.faceCount = 1u;
        data.mipCount = 1u;
        CnbTextureRepresentation representation;
        representation.format = CnbTextureFormat::Rgba8;
        representation.levels.push_back(std::move(rgba));
        data.representations.push_back(std::move(representation));
        return data;
    }

    std::vector<std::uint8_t> EncodeTexture2DToCnb(const CnbTextureData& data,
                                                   const std::string& contentName)
    {
        return EncodeTexture(kTexture2D, data, contentName);
    }

    std::vector<std::uint8_t> EncodeTextureCubeToCnb(const CnbTextureData& data,
                                                     const std::string& contentName)
    {
        return EncodeTexture(kTextureCube, data, contentName);
    }

    std::vector<std::uint8_t> EncodeTexture3DToCnb(const CnbTextureData& data,
                                                   const std::string& contentName)
    {
        return EncodeTexture(kTexture3D, data, contentName);
    }

    CnbTextureData DecodeTexture2DFromCnb(const CnbDocument& document)
    {
        return DecodeTexture(kTexture2D, document);
    }

    CnbTextureData DecodeTextureCubeFromCnb(const CnbDocument& document)
    {
        return DecodeTexture(kTextureCube, document);
    }

    CnbTextureData DecodeTexture3DFromCnb(const CnbDocument& document)
    {
        return DecodeTexture(kTexture3D, document);
    }

    std::size_t SelectCnbTextureRepresentation(
        const CnbTextureData& data, const std::function<bool(CnbTextureFormat)>& supported)
    {
        for (std::size_t i = 0; i < data.representations.size(); ++i)
        {
            if (supported && supported(data.representations[i].format)) { return i; }
        }
        return data.representations.size();
    }
}

// SPDX-License-Identifier: MS-PL
// The writing half of the .cnb texture codec: EncodeTexture2D/Cube/3DToCnb() and
// AppendEmbeddedTexture2DChunks().
//
// Link boundary: apart from the decoder (CnbTextureCodec.cpp), which every program that loads a
// .cnb texture links, so that loading one does not link CnbWriter (docs/link-size-architecture.md).
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CnbCodecDetail.hpp"

#include <string>

#include "CNA/Content/Cnb/CnbArithmetic.hpp"
#include "CNA/Content/Cnb/CnbByteWriter.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;

namespace CNA::Content::Cnb
{
    namespace
    {
        using namespace TextureCodecDetail;

        // 16-byte payload alignment: block-compressed formats have 8- and 16-byte units, and a
        // future memory-mapped reader (CNBF-108) wants the payload start to be at least as
        // aligned as the largest unit. The Model schema already aligns geometry the same way.
        constexpr std::uint32_t kPayloadAlignment = 16u;

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

        /// The chunk-producing half of EncodeTexture(), factored out so a SpriteFont can embed a
        /// texture with byte-identical layout instead of growing its own copy of it.
        void AppendTextureChunks(const TextureShape& shape, const CnbTextureData& data,
                                 CnbWriter& writer, const bool allowClassicBlockCompression)
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
                // The schema and reader have understood every frozen format identifier from the
                // start. Texture2D now writes the three classic XNA block formats as well as the
                // portable baseline; cube, volume and embedded-atlas writers remain Rgba8-only
                // until their runtime upload paths have the matching contract.
                const bool classicBlockFormat =
                    representation.format == CnbTextureFormat::Bc1 ||
                    representation.format == CnbTextureFormat::Bc2 ||
                    representation.format == CnbTextureFormat::Bc3;
                if (representation.format != CnbTextureFormat::Rgba8 &&
                    !(allowClassicBlockCompression && classicBlockFormat))
                {
                    Fail(shape, "this CNB texture writer does not encode " +
                                    CnbTextureFormatToString(representation.format) +
                                    "; Texture2D accepts Rgba8, Bc1, Bc2 and Bc3, while "
                                    "TextureCube, Texture3D and embedded atlases accept Rgba8.");
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
            AppendTextureChunks(shape, data, writer,
                                shape.assetTypeId == CnbAssetTypeId::Texture2D);
            return writer.Build();
        }
    }

    void AppendEmbeddedTexture2DChunks(CnbWriter& writer, const CnbTextureData& data,
                                       const char* label)
    {
        TextureShape shape = kTexture2D;
        shape.label = label;
        AppendTextureChunks(shape, data, writer, false);
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
}

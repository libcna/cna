// SPDX-License-Identifier: MS-PL
// The canonical .xnb texture decoders (Texture2D, Texture3D, TextureCube) and the texture's
// conversion to .cnb.
//
// Link boundary: one translation unit per asset family -- the decoders of every .xnb type used to
// share XnbCanonicalData.cpp, so loading a texture linked the model and effect conversions.
#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"
#include "XnbCanonicalReaderAccess.hpp"
#include "XnbModelGraphReader.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

#include "CNA/Internal/Audio/WavDecoder.hpp"
#include "CNA/Internal/Audio/WavWrapper.hpp"
#include "CNA/Internal/ContentPath.hpp"
#include "CNA/Internal/Graphics/DxtUtil.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "CNA/Internal/Xnb/XnbArithmetic.hpp"
#include "CNA/Internal/Xnb/XnbDecompression.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"
#include "System/IO/BinaryReader.hpp"
#include "System/IO/MemoryStream.hpp"

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Content::ContentLoadException;
    using Microsoft::Xna::Framework::Content::ContentReader;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

    namespace
    {
        [[nodiscard]] bool IsDxt(const SurfaceFormat format)
        {
            return format == SurfaceFormat::Dxt1 || format == SurfaceFormat::Dxt3 ||
                   format == SurfaceFormat::Dxt5;
        }


        [[nodiscard]] std::uint32_t MaxMipCount(
            std::uint32_t width, std::uint32_t height, std::uint32_t depth)
        {
            std::uint32_t count = 1u;
            while (width > 1u || height > 1u || depth > 1u)
            {
                width = std::max(1u, width / 2u);
                height = std::max(1u, height / 2u);
                depth = std::max(1u, depth / 2u);
                ++count;
            }
            return count;
        }

        void ValidateMipCount(const char* readerName, const XnbTextureData& data,
                              const bool requireCompleteChain)
        {
            const std::uint32_t maximum = MaxMipCount(data.width, data.height, data.depth);
            if (data.mipCount == 0u || data.mipCount > maximum)
            {
                throw ContentLoadException(
                    std::string(readerName) + ": declared mip level count (" +
                    std::to_string(data.mipCount) + ") is outside 1.." +
                    std::to_string(maximum) + ".");
            }
            if (requireCompleteChain && data.mipCount != 1u && data.mipCount != maximum)
            {
                throw ContentLoadException(
                    std::string(readerName) +
                    ": incomplete mip chain; expected level zero only or all " +
                    std::to_string(maximum) + " levels.");
            }
        }

        [[nodiscard]] std::size_t DxtLevelBytes(
            const SurfaceFormat format, const std::uint32_t width,
            const std::uint32_t height, const std::uint32_t depth = 1u)
        {
            const std::size_t blocksWide = (static_cast<std::size_t>(width) + 3u) / 4u;
            const std::size_t blocksHigh = (static_cast<std::size_t>(height) + 3u) / 4u;
            const std::size_t bytesPerBlock = format == SurfaceFormat::Dxt1 ? 8u : 16u;
            return blocksWide * blocksHigh * bytesPerBlock * depth;
        }

        [[nodiscard]] std::size_t RawLevelBytes(
            const SurfaceFormat format, const std::uint32_t width,
            const std::uint32_t height, const std::uint32_t depth)
        {
            if (IsDxt(format)) { return DxtLevelBytes(format, width, height, depth); }
            const std::size_t bytesPerPixel = static_cast<std::size_t>(
                Microsoft::Xna::Framework::Graphics::Texture::GetFormatSizeEXT(format));
            return static_cast<std::size_t>(width) * height * depth * bytesPerPixel;
        }

        [[nodiscard]] SurfaceFormat ReadTexture2DFormat(ContentReader& input)
        {
            if (input.getVersionProperty() >= 5)
            {
                return static_cast<SurfaceFormat>(input.ReadInt32());
            }
            switch (input.ReadInt32())
            {
                case 1: return SurfaceFormat::ColorBgraEXT;
                case 28: return SurfaceFormat::Dxt1;
                case 30: return SurfaceFormat::Dxt3;
                case 32: return SurfaceFormat::Dxt5;
                default:
                    throw ContentLoadException(
                        "Texture2DReader: unsupported legacy surface format.");
            }
        }

        void RequireTextureFormat(const char* readerName, const SurfaceFormat format,
                                  const XnbTextureKind kind)
        {
            using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
            using Microsoft::Xna::Framework::Graphics::Texture;
            const int ordinal = static_cast<int>(format);
            const bool classic = ordinal >= static_cast<int>(SurfaceFormat::Color) &&
                ordinal <= static_cast<int>(SurfaceFormat::HdrBlendable);
            const bool accepted = kind == XnbTextureKind::Texture3D
                ? Texture::IsVolumeFormatAllowedByProfileEXT(GraphicsProfile::HiDef, format)
                : (kind == XnbTextureKind::TextureCube
                    ? classic && Texture::IsCubeFormatAllowedByProfileEXT(
                        GraphicsProfile::HiDef, format)
                    : classic || format == SurfaceFormat::ColorBgraEXT);
            if (accepted)
            {
                return;
            }
            throw ContentLoadException(
                std::string(readerName) +
                ": SurfaceFormat is not supported by CNA's .xnb reader.");
        }

        [[nodiscard]] std::uint32_t PositiveDimension(
            const std::int32_t value, const char* readerName, const char* field)
        {
            if (value <= 0)
            {
                throw ContentLoadException(
                    std::string(readerName) + ": invalid " + field + ".");
            }
            return static_cast<std::uint32_t>(value);
        }

        void ReadTextureLevels(ContentReader& input, XnbTextureData& data,
                               const char* readerName)
        {
            data.levels.reserve(static_cast<std::size_t>(data.faceCount) * data.mipCount);
            std::int64_t cumulativeDecodedBytes = 0;
            for (std::uint32_t face = 0u; face < data.faceCount; ++face)
            {
                std::uint32_t width = data.width;
                std::uint32_t height = data.height;
                std::uint32_t depth = data.depth;
                for (std::uint32_t mip = 0u; mip < data.mipCount; ++mip)
                {
                    const std::int32_t byteCount = input.ReadInt32();
                    std::vector<std::uint8_t> bytes =
                        input.ReadBytesExactOrThrow(byteCount, readerName);
                    const std::size_t expected =
                        RawLevelBytes(data.surfaceFormat, width, height, depth);
                    if (bytes.size() != expected)
                    {
                        throw ContentLoadException(
                            std::string(readerName) + ": face " + std::to_string(face) +
                            " level " + std::to_string(mip) + " byte count (" +
                            std::to_string(bytes.size()) + ") does not match the required " +
                            std::to_string(expected) + " bytes.");
                    }
                    const std::int64_t decodedLevelBytes = IsDxt(data.surfaceFormat)
                        ? CheckedMultiplyOrThrow({width, height, depth, 4}, readerName)
                        : static_cast<std::int64_t>(expected);
                    if (cumulativeDecodedBytes >
                        std::numeric_limits<std::int64_t>::max() - decodedLevelBytes)
                    {
                        throw ContentLoadException(
                            std::string(readerName) +
                            ": cumulative decoded texture byte size overflows.");
                    }
                    cumulativeDecodedBytes += decodedLevelBytes;
                    input.CheckDecodedByteSize(cumulativeDecodedBytes, readerName);
                    data.levels.push_back(std::move(bytes));
                    width = std::max(1u, width / 2u);
                    height = std::max(1u, height / 2u);
                    depth = std::max(1u, depth / 2u);
                }
            }
        }


        [[nodiscard]] std::vector<std::uint8_t> DecompressDxtLevel(
            const SurfaceFormat format, const std::vector<std::uint8_t>& bytes,
            const std::uint32_t width, const std::uint32_t height, const std::uint32_t depth)
        {
            std::vector<std::uint8_t> rgba;
            rgba.reserve(static_cast<std::size_t>(width) * height * depth * 4u);
            const std::size_t sliceBytes = DxtLevelBytes(format, width, height);
            for (std::uint32_t slice = 0u; slice < depth; ++slice)
            {
                const std::uint8_t* source = bytes.data() + slice * sliceBytes;
                std::vector<std::uint8_t> decoded;
                switch (format)
                {
                    case SurfaceFormat::Dxt1:
                        decoded = Graphics::DxtUtil::DecompressDxt1(
                            source, sliceBytes, static_cast<int>(width), static_cast<int>(height));
                        break;
                    case SurfaceFormat::Dxt3:
                        decoded = Graphics::DxtUtil::DecompressDxt3(
                            source, sliceBytes, static_cast<int>(width), static_cast<int>(height));
                        break;
                    case SurfaceFormat::Dxt5:
                        decoded = Graphics::DxtUtil::DecompressDxt5(
                            source, sliceBytes, static_cast<int>(width), static_cast<int>(height));
                        break;
                    default:
                        throw ContentLoadException("XNB texture: unsupported DXT format.");
                }
                rgba.insert(rgba.end(), decoded.begin(), decoded.end());
            }
            return rgba;
        }
    }

    XnbTextureData DecodeTexture2DXnbData(
        ContentReader& input, const std::uint32_t maximumDimension)
    {
        XnbTextureData data;
        data.kind = XnbTextureKind::Texture2D;
        data.surfaceFormat = ReadTexture2DFormat(input);
        RequireTextureFormat("Texture2DReader", data.surfaceFormat, XnbTextureKind::Texture2D);
        data.width = PositiveDimension(input.ReadInt32(), "Texture2DReader", "width");
        data.height = PositiveDimension(input.ReadInt32(), "Texture2DReader", "height");
        data.depth = 1u;
        data.faceCount = 1u;
        const std::int32_t mipCount = input.ReadInt32();
        if (mipCount <= 0)
        {
            throw ContentLoadException("Texture2DReader: invalid mip level count.");
        }
        data.mipCount = static_cast<std::uint32_t>(mipCount);
        data.platform = input.getPlatformProperty();
        if (data.width > maximumDimension || data.height > maximumDimension)
        {
            throw ContentLoadException(
                "Texture2DReader: " + std::to_string(data.width) + "x" +
                std::to_string(data.height) +
                " exceeds the target's maximum texture dimension of " +
                std::to_string(maximumDimension) + ".");
        }
        input.CheckDecodedByteSize(
            CheckedMultiplyOrThrow(
                {data.width, data.height,
                 data.surfaceFormat == SurfaceFormat::NormalizedByte2 ? 2u : 4u},
                "Texture2DReader"),
            "Texture2DReader");
        ValidateMipCount("Texture2DReader", data, true);
        ReadTextureLevels(input, data, "Texture2DReader");
        return data;
    }

    XnbTextureData DecodeTexture3DXnbData(ContentReader& input)
    {
        XnbTextureData data;
        data.kind = XnbTextureKind::Texture3D;
        data.surfaceFormat = static_cast<SurfaceFormat>(input.ReadInt32());
        RequireTextureFormat("Texture3DReader", data.surfaceFormat, XnbTextureKind::Texture3D);
        data.width = PositiveDimension(input.ReadInt32(), "Texture3DReader", "width");
        data.height = PositiveDimension(input.ReadInt32(), "Texture3DReader", "height");
        data.depth = PositiveDimension(input.ReadInt32(), "Texture3DReader", "depth");
        data.faceCount = 1u;
        const std::int32_t mipCount = input.ReadInt32();
        if (mipCount <= 0)
        {
            throw ContentLoadException("Texture3DReader: invalid mip level count.");
        }
        data.mipCount = static_cast<std::uint32_t>(mipCount);
        data.platform = input.getPlatformProperty();
        input.CheckDecodedByteSize(
            CheckedMultiplyOrThrow(
                {data.width, data.height, data.depth,
                 Microsoft::Xna::Framework::Graphics::Texture::GetFormatSizeEXT(
                     data.surfaceFormat)},
                "Texture3DReader"),
            "Texture3DReader");
        ValidateMipCount("Texture3DReader", data, false);
        ReadTextureLevels(input, data, "Texture3DReader");
        return data;
    }

    XnbTextureData DecodeTextureCubeXnbData(ContentReader& input)
    {
        XnbTextureData data;
        data.kind = XnbTextureKind::TextureCube;
        data.surfaceFormat = static_cast<SurfaceFormat>(input.ReadInt32());
        RequireTextureFormat("TextureCubeReader", data.surfaceFormat, XnbTextureKind::TextureCube);
        data.width = PositiveDimension(input.ReadInt32(), "TextureCubeReader", "size");
        data.height = data.width;
        data.depth = 1u;
        data.faceCount = 6u;
        const std::int32_t mipCount = input.ReadInt32();
        if (mipCount <= 0)
        {
            throw ContentLoadException("TextureCubeReader: invalid mip level count.");
        }
        data.mipCount = static_cast<std::uint32_t>(mipCount);
        data.platform = input.getPlatformProperty();
        input.CheckDecodedByteSize(
            CheckedMultiplyOrThrow({data.width, data.height, 4u}, "TextureCubeReader"),
            "TextureCubeReader");
        ValidateMipCount("TextureCubeReader", data, false);
        ReadTextureLevels(input, data, "TextureCubeReader");
        return data;
    }

    CNA::Content::Cnb::CnbTextureData ConvertXnbTextureToCnbRgba8(
        const XnbTextureData& source, const bool allowXboxPayload)
    {
        if (source.platform == 'x' && !allowXboxPayload)
        {
            throw ContentLoadException(
                "XNB texture transcoding does not support Xbox 360 byte-swizzled payloads.");
        }
        if (source.surfaceFormat != SurfaceFormat::Color && !IsDxt(source.surfaceFormat))
        {
            throw ContentLoadException(
                "XNB texture surface format cannot be represented by frozen CNB schema 1 "
                "without changing its observable format; only Color and Dxt1/Dxt3/Dxt5 are "
                "transcodable.");
        }

        CNA::Content::Cnb::CnbTextureData result;
        result.width = source.width;
        result.height = source.height;
        result.depth = source.depth;
        result.faceCount = source.faceCount;
        result.mipCount = source.mipCount;
        CNA::Content::Cnb::CnbTextureRepresentation representation;
        representation.format = CNA::Content::Cnb::CnbTextureFormat::Rgba8;
        representation.levels.reserve(source.levels.size());

        for (std::uint32_t face = 0u; face < source.faceCount; ++face)
        {
            std::uint32_t width = source.width;
            std::uint32_t height = source.height;
            std::uint32_t depth = source.depth;
            for (std::uint32_t mip = 0u; mip < source.mipCount; ++mip)
            {
                const std::size_t index = static_cast<std::size_t>(face) * source.mipCount + mip;
                representation.levels.push_back(
                    IsDxt(source.surfaceFormat)
                        ? DecompressDxtLevel(
                              source.surfaceFormat, source.levels.at(index), width, height, depth)
                        : source.levels.at(index));
                width = std::max(1u, width / 2u);
                height = std::max(1u, height / 2u);
                depth = std::max(1u, depth / 2u);
            }
        }
        result.representations.push_back(std::move(representation));
        return result;
    }
}

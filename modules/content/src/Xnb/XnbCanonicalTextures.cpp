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
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ArgumentException.hpp"
#include "System/InvalidOperationException.hpp"

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
                              const std::uint32_t minimum)
        {
            const std::uint32_t maximum = MaxMipCount(data.width, data.height, data.depth);
            if (data.mipCount < minimum || data.mipCount > maximum)
            {
                throw ContentLoadException(
                    std::string(readerName) + ": declared mip level count (" +
                    std::to_string(data.mipCount) + ") is outside " + std::to_string(minimum) +
                    ".." + std::to_string(maximum) + ".");
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

        [[nodiscard]] SurfaceFormat Texture2DFormat(const ContentReader& input,
                                                    const std::int32_t value)
        {
            if (input.getVersionProperty() >= 5)
            {
                return static_cast<SurfaceFormat>(value);
            }
            switch (value)
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
            // Refresh's texture readers hand the format to the texture constructor, whose
            // ValidateCreationParameters (Texture2D IL_0034-005d) refuses a format outside the
            // profile's ValidTextureFormats through ProfileCapabilities.ThrowNotSupportedException
            // (IL_0021): NotSupportedException, which ContentReader.ReadAsset does not wrap.
            throw System::NotSupportedException(
                std::string(readerName) +
                ": SurfaceFormat " + std::to_string(ordinal) + " is not supported.");
        }

        // Refresh's texture readers read every header field first (Texture2DReader.Read
        // IL_0000-001e, Texture3DReader.Read IL_0000-0023, TextureCubeReader.Read IL_0000-0016) and
        // then construct the texture, whose creation checks refuse a size of zero or less before the
        // format: ArgumentOutOfRangeException(parameter, ResourcesMustBeGreaterThanZeroSize)
        // (Texture2D.ValidateCreationParameters IL_0000-0027, Texture3D.CreateTexture
        // IL_0013-004f, TextureCube.ValidateCreationParameters IL_0000-0013). ContentReader.ReadAsset
        // wraps only IOException, so Load<T> reports it as is.
        [[nodiscard]] std::uint32_t PositiveDimension(const std::int32_t value,
                                                      const char* parameter)
        {
            if (value <= 0)
            {
                throw System::ArgumentOutOfRangeException(
                    parameter, "Resource size must be greater than zero.");
            }
            return static_cast<std::uint32_t>(value);
        }

        void ReadTextureLevels(ContentReader& input, XnbTextureData& data,
                               const char* readerName, const bool runtimeTexture,
                               const XnbTextureLevelRead& atLevel)
        {
            const auto maximum = MaxMipCount(data.width, data.height, data.depth);
            data.levels.reserve(static_cast<std::size_t>(data.faceCount) *
                                std::min(data.mipCount, maximum));
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
                    // Refresh reads the entire buffer before SetData (2DReader IL_0042-0059,
                    // 3DReader IL_004b-0060, CubeReader IL_003e-0056). GetLevelDesc fails before
                    // size validation (2D.CopyData IL_00f8-010b, 3D IL_00e1-00f1, cube
                    // IL_00fb-010e); D3DERR_INVALIDCALL maps through Helpers.GetExceptionFromResult
                    // (Framework IL_0252-025c) to InvalidOperationException(UnexpectedError).
                    // Do not read the next level.
                    if (runtimeTexture && mip >= maximum)
                    {
                        throw System::InvalidOperationException("An unexpected error has occurred.");
                    }
                    // Refresh Helpers.ValidateCopyParameters IL_002e-0041 refuses zero
                    // elements before ValidateTotalSize (the level lookup above still wins).
                    if (runtimeTexture && byteCount == 0)
                    {
                        throw System::ArgumentOutOfRangeException(
                            "elementCount", "This parameter must be a valid index within the array.");
                    }
                    const std::size_t expected =
                        RawLevelBytes(data.surfaceFormat, width, height, depth);
                    if (bytes.size() != expected)
                    {
                        if (runtimeTexture)
                        {
                            // Refresh Texture.ValidateTotalSize IL_006c-0076 and
                            // Texture3D.CopyData IL_0157-0161: ArgumentException(InvalidTotalSize).
                            throw System::ArgumentException(
                                "The size of the data passed in is too large or too small for this resource.");
                        }
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
                    // AddTrackedObject IL_00e3 -> FireCreatedEvent IL_001a-0031 exposes the
                    // texture before these reads, so earlier uploads can be observed.
                    if (atLevel) { atLevel(data, face, mip, bytes); }
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

    void RequireXnbTextureFormatBeforeConstruction(ContentReader& input, const XnbTextureData& header)
    {
        if (static_cast<std::int32_t>(header.width) <= 0 || static_cast<std::int32_t>(header.height) <= 0)
        {
            return;
        }
        // An XNA ordinal meets the constructor's own profile refusal, with XNA's message. Only an
        // ordinal no XNA SurfaceFormat has -- which CNA's constructors would admit as an extension
        // -- is refused here, as the decoder refuses it after construction (TQ-018, HND-TQ-16).
        const int ordinal = static_cast<int>(header.surfaceFormat);
        if (ordinal >= static_cast<int>(SurfaceFormat::Color) &&
            ordinal <= static_cast<int>(SurfaceFormat::HdrBlendable))
        {
            return;
        }
        const bool cube = header.kind == XnbTextureKind::TextureCube;
        // A version-4 container's legacy format 1 is read as ColorBgraEXT (Texture2DFormat).
        if (!cube && input.getVersionProperty() < 5 && header.surfaceFormat == SurfaceFormat::ColorBgraEXT)
        {
            return;
        }
        throw System::NotSupportedException(
            std::string(cube ? "TextureCubeReader" : "Texture2DReader") + ": SurfaceFormat " +
            std::to_string(ordinal) + " is not supported.");
    }

    XnbTextureData DecodeTexture2DXnbData(
        ContentReader& input, const std::uint32_t maximumDimension,
        const XnbTextureConstruction& atConstruction,
        const XnbTextureLevelRead& atLevel)
    {
        XnbTextureData data;
        data.kind = XnbTextureKind::Texture2D;
        const std::int32_t format = input.ReadInt32();
        const std::int32_t width = input.ReadInt32();
        const std::int32_t height = input.ReadInt32();
        const std::int32_t mipCount = input.ReadInt32();
        data.width = atConstruction ? static_cast<std::uint32_t>(width)
                                    : PositiveDimension(width, "width");
        data.height = atConstruction ? static_cast<std::uint32_t>(height)
                                     : PositiveDimension(height, "height");
        data.surfaceFormat = Texture2DFormat(input, format);
        data.depth = 1u;
        data.faceCount = 1u;
        data.mipCount = mipCount > 0 ? static_cast<std::uint32_t>(mipCount) : 0u;
        data.platform = input.getPlatformProperty();
        // Refresh Texture2DReader.Read IL_001f-002f: device lookup and construction after
        // all four header reads, before constructor validation or any level payload.
        if (atConstruction) { atConstruction(data); }
        data.width = PositiveDimension(width, "width");
        data.height = PositiveDimension(height, "height");
        // ColorBgraEXT is how a version-4 container's legacy format 1 is read, not an XNA 4.0
        // SurfaceFormat: a version-5 container names XNA ordinals only (the writer refuses to put a
        // CNA extension format there, IsXnbWritableSurfaceFormat), and XNA's
        // Texture2D.ValidateCreationParameters (IL_0034-005d) refuses a format outside the
        // profile's list. Refuse it like every other ordinal no XNA format has (TQ-018).
        if (input.getVersionProperty() >= 5 && data.surfaceFormat == SurfaceFormat::ColorBgraEXT)
        {
            // NotSupportedException, as RequireTextureFormat below (HND-TQ-16).
            throw System::NotSupportedException(
                "Texture2DReader: SurfaceFormat " + std::to_string(static_cast<int>(data.surfaceFormat)) +
                " is not supported.");
        }
        RequireTextureFormat("Texture2DReader", data.surfaceFormat, XnbTextureKind::Texture2D);
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
        if (!atConstruction) { ValidateMipCount("Texture2DReader", data, 0u); }
        ReadTextureLevels(input, data, "Texture2DReader", static_cast<bool>(atConstruction), atLevel);
        return data;
    }

    XnbTextureData DecodeTexture3DXnbData(
        ContentReader& input, const XnbTextureConstruction& atConstruction,
        const XnbTextureLevelRead& atLevel)
    {
        XnbTextureData data;
        data.kind = XnbTextureKind::Texture3D;
        data.surfaceFormat = static_cast<SurfaceFormat>(input.ReadInt32());
        const std::int32_t width = input.ReadInt32();
        const std::int32_t height = input.ReadInt32();
        const std::int32_t depth = input.ReadInt32();
        const std::int32_t mipCount = input.ReadInt32();
        data.width = static_cast<std::uint32_t>(width);
        data.height = static_cast<std::uint32_t>(height);
        data.depth = static_cast<std::uint32_t>(depth);
        data.faceCount = 1u;
        data.mipCount = mipCount > 0 ? static_cast<std::uint32_t>(mipCount) : 0u;
        data.platform = input.getPlatformProperty();
        // Refresh Texture3DReader.Read IL_0025-0035 looks up the device and constructs the texture
        // here; Texture3D.CreateTexture asks the profile for volume textures (MaxVolumeExtent,
        // IL_0050-0074) before it checks the format (IL_0079-00a4), and no level is read yet.
        if (atConstruction)
        {
            atConstruction(data);
        }
        data.width = PositiveDimension(width, "width");
        data.height = PositiveDimension(height, "height");
        data.depth = PositiveDimension(depth, "depth");
        RequireTextureFormat("Texture3DReader", data.surfaceFormat, XnbTextureKind::Texture3D);
        input.CheckDecodedByteSize(
            CheckedMultiplyOrThrow(
                {data.width, data.height, data.depth,
                 Microsoft::Xna::Framework::Graphics::Texture::GetFormatSizeEXT(
                     data.surfaceFormat)},
                "Texture3DReader"),
            "Texture3DReader");
        // Like Texture2DReader, Refresh Texture3DReader.Read asks for mipmaps only when the count
        // is above one (IL_002b-0035) and loops `level < count` (IL_0087-008a): zero or less
        // creates a one-level texture and reads no level.
        if (!atConstruction) { ValidateMipCount("Texture3DReader", data, 0u); }
        ReadTextureLevels(input, data, "Texture3DReader", static_cast<bool>(atConstruction), atLevel);
        return data;
    }

    XnbTextureData DecodeTextureCubeXnbData(
        ContentReader& input, const XnbTextureConstruction& atConstruction,
        const XnbTextureLevelRead& atLevel)
    {
        XnbTextureData data;
        data.kind = XnbTextureKind::TextureCube;
        data.surfaceFormat = static_cast<SurfaceFormat>(input.ReadInt32());
        const std::int32_t size = input.ReadInt32();
        const std::int32_t mipCount = input.ReadInt32();
        data.width = static_cast<std::uint32_t>(size);
        data.height = data.width;
        data.depth = 1u;
        data.faceCount = 6u;
        // Refresh TextureCubeReader.Read: mipmaps only above one level (IL_001e-0027), and the
        // per-face loop `level < count` (IL_005f-0062) reads no level for zero or less.
        data.mipCount = mipCount > 0 ? static_cast<std::uint32_t>(mipCount) : 0u;
        data.platform = input.getPlatformProperty();
        // Refresh TextureCubeReader.Read IL_0018-0027 constructs before any face/level.
        if (atConstruction) { atConstruction(data); }
        data.width = PositiveDimension(size, "size");
        RequireTextureFormat("TextureCubeReader", data.surfaceFormat, XnbTextureKind::TextureCube);
        input.CheckDecodedByteSize(
            CheckedMultiplyOrThrow({data.width, data.height, 4u}, "TextureCubeReader"),
            "TextureCubeReader");
        if (!atConstruction) { ValidateMipCount("TextureCubeReader", data, 0u); }
        ReadTextureLevels(input, data, "TextureCubeReader", static_cast<bool>(atConstruction), atLevel);
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

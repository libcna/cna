// SPDX-License-Identifier: MS-PL
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
        constexpr std::uint16_t WaveFormatPcm = 0x0001u;
        constexpr std::uint16_t WaveFormatMsAdpcm = 0x0002u;
        constexpr std::uint16_t WaveFormatIeeeFloat = 0x0003u;
        constexpr std::uint16_t WaveFormatImaAdpcm = 0x0011u;
        constexpr std::uint16_t WaveFormatXma2 = 0x0166u;
        constexpr std::size_t MinRealMsAdpcmExtensionSize = 32u;

        [[nodiscard]] bool IsDxt(const SurfaceFormat format)
        {
            return format == SurfaceFormat::Dxt1 || format == SurfaceFormat::Dxt3 ||
                   format == SurfaceFormat::Dxt5;
        }

        [[nodiscard]] std::uint16_t Swap16(const bool swap, const std::uint16_t value)
        {
            if (!swap) { return value; }
            return static_cast<std::uint16_t>((value >> 8u) | (value << 8u));
        }

        [[nodiscard]] std::uint32_t Swap32(const bool swap, const std::uint32_t value)
        {
            if (!swap) { return value; }
            return ((value >> 24u) & 0x000000FFu) | ((value >> 8u) & 0x0000FF00u) |
                   ((value << 8u) & 0x00FF0000u) | ((value << 24u) & 0xFF000000u);
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
            // Texture3D's established XNA/FNA allocation derives its level count from width and
            // height; depth shrinks per level but does not extend the number of allocated levels.
            const std::uint32_t maximum = MaxMipCount(
                data.width, data.height,
                data.kind == XnbTextureKind::Texture3D ? 1u : data.depth);
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
            const std::size_t bytesPerPixel = format == SurfaceFormat::NormalizedByte2 ? 2u : 4u;
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
                                  const bool texture2D)
        {
            if (format == SurfaceFormat::Color || IsDxt(format) ||
                (texture2D && (format == SurfaceFormat::ColorBgraEXT ||
                               format == SurfaceFormat::NormalizedByte2 ||
                               format == SurfaceFormat::NormalizedByte4)))
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
                    const std::int64_t decodedLevelBytes = CheckedMultiplyOrThrow(
                        {width, height, depth,
                         data.surfaceFormat == SurfaceFormat::NormalizedByte2 ? 2 : 4},
                        readerName);
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

        void RequireReader(const XnbTypeReaderTableEntry& entry, const std::string& expected,
                           const std::string& asset)
        {
            if (entry.normalizedName != expected)
            {
                throw ContentLoadException(
                    "'" + asset + "' requires nested ContentTypeReader '" + expected +
                    "', but the object references '" + entry.normalizedName + "'.");
            }
            if (entry.version != 0)
            {
                throw ContentLoadException(
                    "'" + asset + "' uses reader '" + entry.normalizedName +
                    "' at unsupported version (" + std::to_string(entry.version) + ").");
            }
        }

        template<typename T, typename ReadElement>
        [[nodiscard]] std::vector<T> ReadList(
            ContentReader& input, const std::string& expectedReader, ReadElement readElement)
        {
            RequireReader(XnbCanonicalReaderAccess::ReadReference(input), expectedReader,
                          input.getAssetNameProperty());
            const std::int32_t count = input.ReadInt32();
            input.CheckCollectionElementCount(count, expectedReader);
            std::vector<T> values;
            values.reserve(static_cast<std::size_t>(count));
            for (std::int32_t index = 0; index < count; ++index)
            {
                values.push_back(readElement());
            }
            return values;
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

        [[nodiscard]] std::uint16_t ComputeMsAdpcmSamplesPerBlock(
            const std::uint16_t blockAlign, const std::uint16_t channels)
        {
            const std::uint32_t channelCount = channels == 0u ? 1u : channels;
            const std::uint32_t headerBits = channelCount * 7u * 8u;
            const std::uint32_t blockBits = static_cast<std::uint32_t>(blockAlign) * 8u;
            const std::uint32_t available = blockBits > headerBits ? blockBits - headerBits : 0u;
            return static_cast<std::uint16_t>(available / (4u * channelCount) + 2u);
        }

#ifdef SOUND_ENABLED
        [[nodiscard]] std::vector<std::uint8_t> DecodeWaveToPcm16(
            const XnbSoundEffectData& source, const std::string& origin,
            std::uint32_t& frameCount)
        {
            const bool synthesizeMsAdpcm =
                source.formatTag == WaveFormatMsAdpcm &&
                source.extensionData.size() < MinRealMsAdpcmExtensionSize;
            const std::vector<std::uint8_t> wav = Audio::BuildWavFromWaveFormatEx(
                source.samples.data(), static_cast<std::uint32_t>(source.samples.size()),
                source.formatTag, source.channels, source.sampleRate,
                source.averageBytesPerSecond, source.blockAlign, source.bitsPerSample,
                synthesizeMsAdpcm
                    ? Audio::BuildStandardMsAdpcmExtension(
                          ComputeMsAdpcmSamplesPerBlock(source.blockAlign, source.channels))
                    : source.extensionData,
                0u);

            try
            {
                Audio::DecodedWavPcm16 decoded = Audio::DecodeWavToPcm16(wav, origin);
                if (decoded.sampleRate != source.sampleRate ||
                    decoded.channels != source.channels)
                {
                    throw ContentLoadException(
                        "'" + origin +
                        "': decoded SoundEffect rate/channels disagree with its XNB WAVEFORMATEX.");
                }
                frameCount = decoded.frameCount;
                return std::move(decoded.samples);
            }
            catch (const ContentLoadException&)
            {
                throw;
            }
            catch (const std::exception& inner)
            {
                throw ContentLoadException(
                    "'" + origin + "': headless XNB SoundEffect decode failed.", inner);
            }
        }
#endif

        void ValidateSoundMetadata(const XnbSoundEffectData& source,
                                   const std::uint32_t frameCount,
                                   const std::string& origin)
        {
            if (source.loopStart < 0 || source.loopLength < 0 ||
                static_cast<std::uint64_t>(source.loopStart) +
                        static_cast<std::uint64_t>(source.loopLength) > frameCount)
            {
                throw ContentLoadException(
                    "'" + origin + "': SoundEffect loop region lies outside decoded frames.");
            }
            if (source.storedDurationMs == 0u || frameCount == 0u) { return; }
            const double decodedMs =
                static_cast<double>(frameCount) * 1000.0 / source.sampleRate;
            const double ratio = decodedMs / source.storedDurationMs;
            if (ratio > 2.0 || ratio < 0.5)
            {
                throw ContentLoadException(
                    "'" + origin + "': decoded audio duration (" +
                    std::to_string(decodedMs) +
                    "ms) disagrees drastically with the .xnb's own stored duration (" +
                    std::to_string(source.storedDurationMs) + "ms).");
            }
        }

        [[nodiscard]] std::vector<std::uint8_t> ReadFile(
            const std::filesystem::path& path, const XnbReadLimits& limits)
        {
            std::error_code error;
            const std::uintmax_t size = std::filesystem::file_size(path, error);
            if (error)
            {
                throw ContentLoadException(
                    "cannot inspect XNB source '" + ContentPathToUtf8(path) + "': " +
                    error.message() + ".");
            }
            if (size > static_cast<std::uintmax_t>(limits.maxFileSize))
            {
                throw ContentLoadException(
                    "XNB source '" + ContentPathToUtf8(path) + "' exceeds the maximum file size.");
            }
            std::ifstream stream(path, std::ios::binary);
            if (!stream)
            {
                throw ContentLoadException(
                    "cannot open XNB source '" + ContentPathToUtf8(path) + "'.");
            }
            return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        }

        class CanonicalModelSink
        {
        public:
            CanonicalModelSink(const std::int32_t sharedResourceCount, std::string origin)
                : sharedResourceCount_(sharedResourceCount), origin_(std::move(origin)) {}

            [[nodiscard]] std::string ReadString(ContentReader& input) const
            {
                RequireReader(
                    XnbCanonicalReaderAccess::ReadReference(input),
                    "Microsoft.Xna.Framework.Content.StringReader", origin_);
                return input.ReadString();
            }

            void BeginBones(const std::uint32_t count) { model_.bones.reserve(count); }

            void AddBone(const std::uint32_t index, std::string name,
                         const Microsoft::Xna::Framework::Matrix& transform)
            {
                if (index != model_.bones.size())
                {
                    throw ContentLoadException("XnbImporter: Model bone ordering is inconsistent.");
                }
                model_.bones.push_back({std::move(name), transform, -1, {}});
            }

            void BeginBoneLinks(const std::uint32_t bone, const std::int32_t parent,
                                const std::uint32_t childCount)
            {
                model_.bones.at(bone).parent = parent;
                model_.bones.at(bone).children.reserve(childCount);
            }

            void AddBoneChild(const std::uint32_t bone, const std::int32_t child)
            {
                model_.bones.at(bone).children.push_back(child);
            }

            void EndBoneLinks(const std::uint32_t /*bone*/) {}

            void BeginMeshes(const std::uint32_t count) { model_.meshes.reserve(count); }

            void BeginMesh(const std::uint32_t index, std::string name,
                           const std::int32_t parentBone,
                           const Microsoft::Xna::Framework::BoundingSphere& bounds)
            {
                if (index != model_.meshes.size())
                {
                    throw ContentLoadException("XnbImporter: Model mesh ordering is inconsistent.");
                }
                model_.meshes.push_back({std::move(name), parentBone, bounds, {}});
                currentMesh_ = index;
                currentPart_ = 0u;
            }

            void ReadTag(ContentReader& input, const XnbModelTagKind kind) const
            {
                const XnbTypeReaderTableEntry* reader =
                    XnbCanonicalReaderAccess::ReadOptionalReference(input);
                if (reader == nullptr) { return; }
                std::string location = "Model";
                if (kind == XnbModelTagKind::Mesh)
                {
                    location = "mesh " + std::to_string(currentMesh_);
                }
                else if (kind == XnbModelTagKind::MeshPart)
                {
                    location = "mesh " + std::to_string(currentMesh_) + " part " +
                               std::to_string(currentPart_);
                }
                throw ContentLoadException(
                    "XnbImporter: Model cannot be transcoded losslessly: " + location +
                    " has a non-null Tag using reader '" + reader->normalizedName +
                    "', but Model schema 1 has no Tag representation.");
            }

            void BeginMeshParts(const std::uint32_t count)
            {
                model_.meshes.at(currentMesh_).parts.reserve(count);
            }

            void BeginMeshPart(const std::uint32_t index, const std::int32_t vertexOffset,
                               const std::int32_t vertexCount, const std::int32_t startIndex,
                               const std::int32_t primitiveCount)
            {
                currentPart_ = index;
                model_.meshes.at(currentMesh_).parts.push_back(
                    {vertexOffset, vertexCount, startIndex, primitiveCount, -1, -1, -1});
            }

            void ReadSharedReference(ContentReader& input, const XnbModelSharedKind kind)
            {
                const std::int32_t encoded = input.Read7BitEncodedInt();
                if (encoded <= 0 || encoded > sharedResourceCount_)
                {
                    throw ContentLoadException(
                        "XnbImporter: Model mesh " + std::to_string(currentMesh_) + " part " +
                        std::to_string(currentPart_) +
                        " has a null or out-of-range shared-resource reference.");
                }
                XnbModelPartData& part =
                    model_.meshes.at(currentMesh_).parts.at(currentPart_);
                std::int32_t* destination = &part.effectResource;
                if (kind == XnbModelSharedKind::VertexBuffer)
                {
                    destination = &part.vertexBufferResource;
                }
                else if (kind == XnbModelSharedKind::IndexBuffer)
                {
                    destination = &part.indexBufferResource;
                }
                *destination = encoded - 1;
            }

            void EndMeshPart() {}
            void EndMesh() {}

            [[nodiscard]] XnbModelData Finish(const std::int32_t root)
            {
                model_.rootBone = root;
                return std::move(model_);
            }

        private:
            XnbModelData model_;
            std::int32_t sharedResourceCount_ = 0;
            std::string origin_;
            std::size_t currentMesh_ = 0u;
            std::size_t currentPart_ = 0u;
        };

        [[nodiscard]] std::array<float, 16> MatrixValues(
            const Microsoft::Xna::Framework::Matrix& value)
        {
            return {{value.M11, value.M12, value.M13, value.M14,
                     value.M21, value.M22, value.M23, value.M24,
                     value.M31, value.M32, value.M33, value.M34,
                     value.M41, value.M42, value.M43, value.M44}};
        }

        [[nodiscard]] bool SameFloat(const float left, const float right)
        {
            return std::bit_cast<std::uint32_t>(left) == std::bit_cast<std::uint32_t>(right);
        }

        [[nodiscard]] bool SameSphere(
            const Microsoft::Xna::Framework::BoundingSphere& left,
            const Microsoft::Xna::Framework::BoundingSphere& right)
        {
            return SameFloat(left.Center.X, right.Center.X) &&
                   SameFloat(left.Center.Y, right.Center.Y) &&
                   SameFloat(left.Center.Z, right.Center.Z) &&
                   SameFloat(left.Radius, right.Radius);
        }

        [[nodiscard]] std::string ModelPartContext(
            const XnbModelMeshData& mesh, const std::size_t meshIndex,
            const std::size_t partIndex)
        {
            return "mesh " + std::to_string(meshIndex) + " ('" + mesh.name + "') part " +
                   std::to_string(partIndex);
        }
    }

    XnbTextureData DecodeTexture2DXnbData(
        ContentReader& input, const std::uint32_t maximumDimension)
    {
        XnbTextureData data;
        data.kind = XnbTextureKind::Texture2D;
        data.surfaceFormat = ReadTexture2DFormat(input);
        RequireTextureFormat("Texture2DReader", data.surfaceFormat, true);
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
        RequireTextureFormat("Texture3DReader", data.surfaceFormat, false);
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
                {data.width, data.height, data.depth, 4u}, "Texture3DReader"),
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
        RequireTextureFormat("TextureCubeReader", data.surfaceFormat, false);
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

    XnbSpriteFontData DecodeSpriteFontXnbData(
        ContentReader& input, const std::uint32_t maximumTextureDimension)
    {
        RequireReader(
            XnbCanonicalReaderAccess::ReadReference(input),
            "Microsoft.Xna.Framework.Content.Texture2DReader",
            input.getAssetNameProperty());

        XnbSpriteFontData font;
        font.atlas = DecodeTexture2DXnbData(input, maximumTextureDimension);
        font.glyphs = ReadList<Microsoft::Xna::Framework::Rectangle>(
            input,
            "Microsoft.Xna.Framework.Content.ListReader`1[[Microsoft.Xna.Framework.Rectangle]]",
            [&input]
            {
                const std::int32_t x = input.ReadInt32();
                const std::int32_t y = input.ReadInt32();
                const std::int32_t width = input.ReadInt32();
                const std::int32_t height = input.ReadInt32();
                return Microsoft::Xna::Framework::Rectangle(x, y, width, height);
            });
        font.cropping = ReadList<Microsoft::Xna::Framework::Rectangle>(
            input,
            "Microsoft.Xna.Framework.Content.ListReader`1[[Microsoft.Xna.Framework.Rectangle]]",
            [&input]
            {
                const std::int32_t x = input.ReadInt32();
                const std::int32_t y = input.ReadInt32();
                const std::int32_t width = input.ReadInt32();
                const std::int32_t height = input.ReadInt32();
                return Microsoft::Xna::Framework::Rectangle(x, y, width, height);
            });
        font.characters = ReadList<SharpRuntime::charcs>(
            input, "Microsoft.Xna.Framework.Content.ListReader`1[[System.Char]]",
            [&input] { return input.ReadChar(); });
        font.lineSpacing = input.ReadInt32();
        font.spacing = input.ReadSingle();
        font.kerning = ReadList<Microsoft::Xna::Framework::Vector3>(
            input,
            "Microsoft.Xna.Framework.Content.ListReader`1[[Microsoft.Xna.Framework.Vector3]]",
            [&input] { return input.ReadVector3(); });
        if (input.ReadBoolean()) { font.defaultCharacter = input.ReadChar(); }
        if (font.glyphs.size() != font.cropping.size() ||
            font.glyphs.size() != font.characters.size() ||
            font.glyphs.size() != font.kerning.size())
        {
            throw ContentLoadException(
                "SpriteFontReader: glyph, cropping, character, and kerning counts differ.");
        }
        return font;
    }

    XnbSoundEffectData DecodeSoundEffectXnbData(ContentReader& input)
    {
        XnbSoundEffectData result;
        result.platform = input.getPlatformProperty();
        const bool swap = input.getPlatformProperty() == 'x';
        const std::uint32_t formatLength = input.ReadUInt32();
        if (formatLength < 16u || formatLength >
                static_cast<std::uint32_t>(DefaultXnbReadLimits().maxStringBytes))
        {
            throw ContentLoadException(
                "'" + input.getAssetNameProperty() +
                "': SoundEffectReader has an invalid format block length.");
        }
        result.formatTag = Swap16(swap, input.ReadUInt16());
        result.channels = Swap16(swap, input.ReadUInt16());
        result.sampleRate = Swap32(swap, input.ReadUInt32());
        result.averageBytesPerSecond = Swap32(swap, input.ReadUInt32());
        result.blockAlign = Swap16(swap, input.ReadUInt16());
        result.bitsPerSample = Swap16(swap, input.ReadUInt16());

        if (formatLength > 16u)
        {
            const std::uint16_t extensionSize = Swap16(swap, input.ReadUInt16());
            const std::int64_t remaining = static_cast<std::int64_t>(formatLength) - 18;
            if (remaining < 0 || static_cast<std::uint64_t>(remaining) >
                    static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()))
            {
                throw ContentLoadException(
                    "'" + input.getAssetNameProperty() +
                    "': SoundEffectReader formatLength is too small for its extension.");
            }
            result.extensionData = input.ReadBytesExactOrThrow(
                static_cast<std::int32_t>(remaining), "SoundEffectReader");
            if (result.formatTag != WaveFormatXma2 && extensionSize > result.extensionData.size())
            {
                throw ContentLoadException(
                    "'" + input.getAssetNameProperty() +
                    "': SoundEffectReader cbSize exceeds its format block.");
            }
        }

        result.samples =
            input.ReadBytesExactOrThrow(input.ReadInt32(), "SoundEffectReader");
        result.loopStart = input.ReadInt32();
        result.loopLength = input.ReadInt32();
        result.storedDurationMs = input.ReadUInt32();
        return result;
    }

    Microsoft::Xna::Framework::Curve DecodeCurveXnbData(
        ContentReader& input, std::optional<Microsoft::Xna::Framework::Curve> existing)
    {
        using Microsoft::Xna::Framework::Curve;
        using Microsoft::Xna::Framework::CurveContinuity;
        using Microsoft::Xna::Framework::CurveKey;
        using Microsoft::Xna::Framework::CurveLoopType;

        Curve curve = existing.value_or(Curve{});
        curve.setPreLoopProperty(static_cast<CurveLoopType>(input.ReadInt32()));
        curve.setPostLoopProperty(static_cast<CurveLoopType>(input.ReadInt32()));
        const std::int32_t count = input.ReadInt32();
        input.CheckCollectionElementCount(count, "Microsoft.Xna.Framework.Content.CurveReader");
        for (std::int32_t index = 0; index < count; ++index)
        {
            // Read the wire fields in sequence: C++ does not define function-argument evaluation
            // order, while the XNB representation is strictly position, value, tangents, continuity.
            const float position = input.ReadSingle();
            const float value = input.ReadSingle();
            const float tangentIn = input.ReadSingle();
            const float tangentOut = input.ReadSingle();
            const auto continuity = static_cast<CurveContinuity>(input.ReadInt32());
            curve.getKeysProperty().Add(
                CurveKey(position, value, tangentIn, tangentOut, continuity));
        }
        return curve;
    }

    XnbSongData DecodeSongXnbData(ContentReader& input)
    {
        return {input.ReadString(), input.ReadInt32()};
    }

    XnbVideoData DecodeVideoXnbData(ContentReader& input, const bool objectReferences)
    {
        XnbVideoData result;
        if (!objectReferences)
        {
            // CNA's established runtime reader used direct fields, including in its historical
            // full-container fixtures. Keep that behavior while the pipeline's strict path below
            // accepts FNA's actual ReadObject<T> wire representation.
            result.mediaPath = input.ReadString();
            result.durationMs = input.ReadInt32();
            result.width = input.ReadInt32();
            result.height = input.ReadInt32();
            result.framesPerSecond = input.ReadSingle();
            result.soundtrackType = input.ReadInt32();
            return result;
        }

        RequireReader(
            XnbCanonicalReaderAccess::ReadReference(input),
            "Microsoft.Xna.Framework.Content.StringReader",
            input.getAssetNameProperty());
        result.mediaPath = input.ReadString();
        const auto readInt32Object = [&input]
        {
            RequireReader(
                XnbCanonicalReaderAccess::ReadReference(input),
                "Microsoft.Xna.Framework.Content.Int32Reader",
                input.getAssetNameProperty());
            return input.ReadInt32();
        };
        result.durationMs = readInt32Object();
        result.width = readInt32Object();
        result.height = readInt32Object();
        RequireReader(
            XnbCanonicalReaderAccess::ReadReference(input),
            "Microsoft.Xna.Framework.Content.SingleReader",
            input.getAssetNameProperty());
        result.framesPerSecond = input.ReadSingle();
        result.soundtrackType = readInt32Object();
        return result;
    }

    XnbVertexDeclarationData DecodeVertexDeclarationXnbData(ContentReader& input)
    {
        XnbVertexDeclarationData result;
        result.stride = input.ReadInt32();
        const std::int32_t elementCount = input.ReadInt32();
        input.CheckCollectionElementCount(elementCount, "VertexDeclarationReader elements");
        if (result.stride <= 0)
        {
            throw ContentLoadException("VertexDeclarationReader: vertex stride must be positive.");
        }
        result.elements.reserve(static_cast<std::size_t>(elementCount));
        for (std::int32_t element = 0; element < elementCount; ++element)
        {
            const std::int32_t offset = input.ReadInt32();
            const auto format =
                static_cast<Microsoft::Xna::Framework::Graphics::VertexElementFormat>(
                    input.ReadInt32());
            const auto usage =
                static_cast<Microsoft::Xna::Framework::Graphics::VertexElementUsage>(
                    input.ReadInt32());
            const std::int32_t usageIndex = input.ReadInt32();
            result.elements.emplace_back(
                offset, format, usage, usageIndex);
        }
        return result;
    }

    XnbVertexBufferData DecodeVertexBufferXnbData(ContentReader& input)
    {
        XnbVertexBufferData result;
        result.declaration = DecodeVertexDeclarationXnbData(input);
        result.vertexCount = input.ReadUInt32();
        const std::int64_t byteCount = CheckedMultiplyOrThrow(
            {result.vertexCount, static_cast<std::uint32_t>(result.declaration.stride)},
            "VertexBufferReader");
        input.CheckDecodedByteSize(byteCount, "VertexBufferReader");
        if (byteCount > std::numeric_limits<std::int32_t>::max())
        {
            throw ContentLoadException("VertexBufferReader: vertex payload exceeds Int32 length.");
        }
        result.bytes = input.ReadBytesExactOrThrow(
            static_cast<std::int32_t>(byteCount), "VertexBufferReader");
        return result;
    }

    XnbIndexBufferData DecodeIndexBufferXnbData(ContentReader& input)
    {
        XnbIndexBufferData result;
        result.indexElementSize = input.ReadBoolean() ? 2u : 4u;
        const std::int32_t byteCount = input.ReadInt32();
        if (byteCount < 0 || byteCount % static_cast<std::int32_t>(result.indexElementSize) != 0)
        {
            throw ContentLoadException(
                "IndexBufferReader: payload length is negative or not a whole number of indices.");
        }
        result.bytes = input.ReadBytesExactOrThrow(byteCount, "IndexBufferReader");
        return result;
    }

    XnbBasicEffectData DecodeBasicEffectXnbData(
        ContentReader& input, std::string textureReference)
    {
        XnbBasicEffectData result;
        result.textureReference = std::move(textureReference);
        result.diffuseColor = input.ReadVector3();
        result.emissiveColor = input.ReadVector3();
        result.specularColor = input.ReadVector3();
        result.specularPower = input.ReadSingle();
        result.alpha = input.ReadSingle();
        result.vertexColorEnabled = input.ReadBoolean();
        return result;
    }

    XnbAlphaTestEffectData DecodeAlphaTestEffectXnbData(
        ContentReader& input, std::string textureReference)
    {
        XnbAlphaTestEffectData result;
        result.textureReference = std::move(textureReference);
        result.alphaFunction = input.ReadInt32();
        result.referenceAlpha = input.ReadUInt32();
        result.diffuseColor = input.ReadVector3();
        result.alpha = input.ReadSingle();
        result.vertexColorEnabled = input.ReadBoolean();
        return result;
    }

    XnbDualTextureEffectData DecodeDualTextureEffectXnbData(
        ContentReader& input, std::string textureReference,
        std::string texture2Reference)
    {
        XnbDualTextureEffectData result;
        result.textureReference = std::move(textureReference);
        result.texture2Reference = std::move(texture2Reference);
        result.diffuseColor = input.ReadVector3();
        result.alpha = input.ReadSingle();
        result.vertexColorEnabled = input.ReadBoolean();
        return result;
    }

    XnbEnvironmentMapEffectData DecodeEnvironmentMapEffectXnbData(
        ContentReader& input, std::string textureReference,
        std::string environmentMapReference)
    {
        XnbEnvironmentMapEffectData result;
        result.textureReference = std::move(textureReference);
        result.environmentMapReference = std::move(environmentMapReference);
        result.environmentMapAmount = input.ReadSingle();
        result.environmentMapSpecular = input.ReadVector3();
        result.fresnelFactor = input.ReadSingle();
        result.diffuseColor = input.ReadVector3();
        result.emissiveColor = input.ReadVector3();
        result.alpha = input.ReadSingle();
        return result;
    }

    XnbSkinnedEffectData DecodeSkinnedEffectXnbData(
        ContentReader& input, std::string textureReference)
    {
        XnbSkinnedEffectData result;
        result.textureReference = std::move(textureReference);
        result.weightsPerVertex = input.ReadInt32();
        result.diffuseColor = input.ReadVector3();
        result.emissiveColor = input.ReadVector3();
        result.specularColor = input.ReadVector3();
        result.specularPower = input.ReadSingle();
        result.alpha = input.ReadSingle();
        return result;
    }

    CNA::Content::Cnb::CnbModelData ConvertXnbModelToCnb(
        const XnbModelData& source,
        const std::function<std::string(const std::string&)>& resolveTexture)
    {
        namespace Cnb = CNA::Content::Cnb;
        namespace Fidelity = CNA::Internal::Graphics;
        using Microsoft::Xna::Framework::BoundingSphere;
        using Microsoft::Xna::Framework::Matrix;
        using Microsoft::Xna::Framework::Vector3;

        const auto fail = [](const std::string& detail) -> void
        {
            throw ContentLoadException(
                "XnbImporter: Model cannot be transcoded losslessly: " + detail + ".");
        };

        if (source.bones.empty()) { fail("the Model has no root bone"); }
        if (source.rootBone != 0) { fail("the serialized root bone is not bone 0"); }
        if (!(source.bones.front().transform == Matrix::getIdentityProperty()))
        {
            fail("bone 0 has a non-identity transform which the Model schema-1 runtime does not apply");
        }

        Cnb::CnbModelData result;
        result.bones.reserve(source.bones.size());
        std::vector<std::vector<std::int32_t>> childrenByParent(source.bones.size());
        for (std::size_t bone = 0u; bone < source.bones.size(); ++bone)
        {
            const XnbModelBoneData& inputBone = source.bones[bone];
            if (inputBone.name.empty())
            {
                fail("bone " + std::to_string(bone) +
                     " has an empty name which the Model schema-1 runtime replaces");
            }
            if (bone == 0u)
            {
                if (inputBone.parent != -1) { fail("bone 0 has a parent"); }
            }
            else if (inputBone.parent < 0 ||
                     static_cast<std::size_t>(inputBone.parent) >= bone)
            {
                fail("bone " + std::to_string(bone) +
                     " is not ordered after its valid parent");
            }
            if (inputBone.parent >= 0)
            {
                childrenByParent.at(static_cast<std::size_t>(inputBone.parent)).push_back(
                    static_cast<std::int32_t>(bone));
            }
            result.bones.push_back(
                {inputBone.name, inputBone.parent, MatrixValues(inputBone.transform)});
        }
        for (std::size_t bone = 0u; bone < source.bones.size(); ++bone)
        {
            if (source.bones[bone].children != childrenByParent[bone])
            {
                fail("bone " + std::to_string(bone) +
                     " has child references that do not match the serialized parent graph");
            }
        }

        std::vector<std::uint32_t> resourceUses(source.sharedResources.size(), 0u);
        result.meshes.reserve(source.meshes.size());
        for (std::size_t meshIndex = 0u; meshIndex < source.meshes.size(); ++meshIndex)
        {
            const XnbModelMeshData& mesh = source.meshes[meshIndex];
            if (mesh.parentBone < 0 ||
                static_cast<std::size_t>(mesh.parentBone) >= source.bones.size())
            {
                fail("mesh " + std::to_string(meshIndex) + " has an invalid parent bone");
            }
            Cnb::CnbModelMesh outputMesh;
            outputMesh.name = mesh.name;
            outputMesh.parentBone = mesh.parentBone;
            std::vector<Vector3> meshPositions;

            for (std::size_t partIndex = 0u; partIndex < mesh.parts.size(); ++partIndex)
            {
                const XnbModelPartData& part = mesh.parts[partIndex];
                const std::string where = ModelPartContext(mesh, meshIndex, partIndex);
                const std::int32_t references[] = {
                    part.vertexBufferResource, part.indexBufferResource, part.effectResource};
                for (const std::int32_t reference : references)
                {
                    if (reference < 0 ||
                        static_cast<std::size_t>(reference) >= source.sharedResources.size())
                    {
                        fail(where + " has an invalid shared-resource reference");
                    }
                    ++resourceUses[static_cast<std::size_t>(reference)];
                }

                const auto* vertex = std::get_if<XnbVertexBufferData>(
                    &source.sharedResources[static_cast<std::size_t>(
                        part.vertexBufferResource)].value);
                const auto* index = std::get_if<XnbIndexBufferData>(
                    &source.sharedResources[static_cast<std::size_t>(
                        part.indexBufferResource)].value);
                const auto* effect = std::get_if<XnbBasicEffectData>(
                    &source.sharedResources[static_cast<std::size_t>(part.effectResource)].value);
                if (vertex == nullptr)
                {
                    fail(where + " does not reference a VertexBufferReader resource");
                }
                if (index == nullptr)
                {
                    fail(where + " does not reference an IndexBufferReader resource");
                }
                if (effect == nullptr)
                {
                    fail(where + " uses effect reader '" +
                         source.sharedResources[static_cast<std::size_t>(part.effectResource)].reader +
                         "'; only BasicEffectReader is representable by the initial schema-1 subset");
                }

                const Fidelity::InferredVertexLayout inferred =
                    Fidelity::InferredLayoutForStride(
                        vertex->declaration.stride,
                        Fidelity::UnlistedStrideLayout::RendererRefusesIt);
                if (!inferred.known)
                {
                    fail(where + " uses vertex stride " +
                         std::to_string(vertex->declaration.stride) +
                         " which has no canonical Model schema-1 layout");
                }
                if (vertex->declaration.elements.size() != inferred.count)
                {
                    fail(where + " uses a VertexDeclaration with " +
                         std::to_string(vertex->declaration.elements.size()) +
                         " elements, but stride " +
                         std::to_string(vertex->declaration.stride) + " reconstructs " +
                         std::to_string(inferred.count));
                }
                for (std::size_t element = 0u; element < inferred.count; ++element)
                {
                    const auto& declared = vertex->declaration.elements[element];
                    const auto& canonical = inferred.elements[element];
                    if (declared.getOffsetProperty() != canonical.offset ||
                        declared.getVertexElementFormatProperty() != canonical.format ||
                        declared.getVertexElementUsageProperty() != canonical.usage ||
                        declared.getUsageIndexProperty() != canonical.usageIndex)
                    {
                        fail(where + " uses VertexDeclaration element " +
                             Fidelity::VertexDeclarationFidelityDetail::Describe(
                                 declared.getVertexElementUsageProperty(),
                                 declared.getUsageIndexProperty(), declared.getOffsetProperty(),
                                 declared.getVertexElementFormatProperty()) +
                             ", but stride " +
                             std::to_string(vertex->declaration.stride) +
                             " reconstructs " +
                             Fidelity::VertexDeclarationFidelityDetail::Describe(
                                 canonical.usage, canonical.usageIndex,
                                 canonical.offset, canonical.format));
                    }
                }
                if (part.vertexOffset != 0 || part.startIndex != 0 || part.vertexCount < 0 ||
                    static_cast<std::uint32_t>(part.vertexCount) != vertex->vertexCount)
                {
                    fail(where + " uses VertexOffset = " +
                         std::to_string(part.vertexOffset) + ", StartIndex = " +
                         std::to_string(part.startIndex) + ", and NumVertices = " +
                         std::to_string(part.vertexCount) + " of " +
                         std::to_string(vertex->vertexCount) +
                         "; Model schema 1 preserves only whole buffers at offset zero");
                }
                const std::uint32_t indexCount = static_cast<std::uint32_t>(
                    index->bytes.size() / index->indexElementSize);
                if (part.primitiveCount < 0 || indexCount !=
                        static_cast<std::uint64_t>(part.primitiveCount) * 3u)
                {
                    fail(where + " triangle count does not consume the complete index buffer");
                }
                if (!SameFloat(effect->specularPower, 16.0f))
                {
                    fail(where + " has BasicEffect.SpecularPower = " +
                         std::to_string(effect->specularPower) +
                         ", but Model schema 1 has no SpecularPower field and reconstructs 16");
                }

                Cnb::CnbModelPart outputPart;
                outputPart.name = mesh.name + "/part" + std::to_string(partIndex);
                outputPart.vertexStride =
                    static_cast<std::uint32_t>(vertex->declaration.stride);
                outputPart.vertexCount = vertex->vertexCount;
                outputPart.indexCount = indexCount;
                outputPart.indexElementSize = index->indexElementSize;
                outputPart.primitiveTopology = 4u;
                outputPart.primitiveCount = static_cast<std::uint32_t>(part.primitiveCount);
                outputPart.effectKind = Cnb::CnbEffectKind::BasicEffect;
                outputPart.vertexColorEnabled = effect->vertexColorEnabled;
                outputPart.material.baseColorFactor = {{
                    effect->diffuseColor.X, effect->diffuseColor.Y,
                    effect->diffuseColor.Z, effect->alpha}};
                outputPart.material.emissiveFactor = {{
                    effect->emissiveColor.X, effect->emissiveColor.Y,
                    effect->emissiveColor.Z}};
                outputPart.material.specularColorFactor = {{
                    effect->specularColor.X, effect->specularColor.Y,
                    effect->specularColor.Z}};
                if (!effect->textureReference.empty())
                {
                    outputPart.material.baseColorTexture =
                        resolveTexture(effect->textureReference);
                }
                outputPart.vertexBytes = vertex->bytes;
                outputPart.indexBytes = index->bytes;

                for (std::uint32_t vertexIndex = 0u;
                     vertexIndex < vertex->vertexCount; ++vertexIndex)
                {
                    const std::size_t offset =
                        static_cast<std::size_t>(vertexIndex) *
                        static_cast<std::size_t>(vertex->declaration.stride);
                    float xyz[3];
                    std::memcpy(xyz, vertex->bytes.data() + offset, sizeof(xyz));
                    meshPositions.emplace_back(xyz[0], xyz[1], xyz[2]);
                }

                outputMesh.partIndices.push_back(
                    static_cast<std::uint32_t>(result.parts.size()));
                result.parts.push_back(std::move(outputPart));
            }

            if (meshPositions.empty())
            {
                fail("mesh " + std::to_string(meshIndex) + " has no positions for its bounding sphere");
            }
            const BoundingSphere rebuilt = BoundingSphere::CreateFromPoints(meshPositions);
            if (!SameSphere(mesh.boundingSphere, rebuilt))
            {
                fail("mesh " + std::to_string(meshIndex) +
                     " bounding sphere differs from the value Model schema 1 reconstructs");
            }
            result.meshes.push_back(std::move(outputMesh));
        }

        for (std::size_t resource = 0u; resource < resourceUses.size(); ++resource)
        {
            if (resourceUses[resource] == 0u)
            {
                fail("shared resource " + std::to_string(resource + 1u) + " using reader '" +
                     source.sharedResources[resource].reader +
                     "' is unused; Model schema 1 has no independent resource table");
            }
            if (resourceUses[resource] > 1u)
            {
                fail("shared resource " + std::to_string(resource + 1u) + " using reader '" +
                     source.sharedResources[resource].reader + "' is referenced " +
                     std::to_string(resourceUses[resource]) +
                     " times; Model schema 1 cannot preserve shared-object identity");
            }
        }
        result.hasBoneHierarchy = source.bones.size() > 1u;
        result.appliesGltfLightingPolicy = false;
        return result;
    }

    CNA::Content::Cnb::CnbModelV2Data ConvertXnbModelToCnbV2(
        const XnbModelData& source,
        const std::function<std::string(const std::string&)>& resolveTexture)
    {
        namespace Cnb = CNA::Content::Cnb;
        namespace Graphics = Microsoft::Xna::Framework::Graphics;

        const auto fail = [](const std::string& detail) -> void
        {
            throw ContentLoadException(
                "XnbImporter: Model cannot be transcoded losslessly: " + detail + ".");
        };
        const auto toU32 = [&fail](const std::int32_t value,
                                   const std::string& what) -> std::uint32_t
        {
            if (value < 0) { fail(what + " is negative"); }
            return static_cast<std::uint32_t>(value);
        };
        const auto vector3 = [](const Microsoft::Xna::Framework::Vector3& value)
        {
            return std::array<float, 3>{{value.X, value.Y, value.Z}};
        };
        const auto texture = [&resolveTexture](const std::string& authored)
        {
            return authored.empty() ? std::string{} : resolveTexture(authored);
        };
        const auto format = [&fail](const Graphics::VertexElementFormat value)
        {
            switch (value)
            {
                case Graphics::VertexElementFormat::Single:
                    return Cnb::CnbModelV2VertexFormat::Single;
                case Graphics::VertexElementFormat::Vector2:
                    return Cnb::CnbModelV2VertexFormat::Vector2;
                case Graphics::VertexElementFormat::Vector3:
                    return Cnb::CnbModelV2VertexFormat::Vector3;
                case Graphics::VertexElementFormat::Vector4:
                    return Cnb::CnbModelV2VertexFormat::Vector4;
                case Graphics::VertexElementFormat::Color:
                    return Cnb::CnbModelV2VertexFormat::Color;
                case Graphics::VertexElementFormat::Byte4:
                    return Cnb::CnbModelV2VertexFormat::Byte4;
                case Graphics::VertexElementFormat::Short2:
                    return Cnb::CnbModelV2VertexFormat::Short2;
                case Graphics::VertexElementFormat::Short4:
                    return Cnb::CnbModelV2VertexFormat::Short4;
                case Graphics::VertexElementFormat::NormalizedShort2:
                    return Cnb::CnbModelV2VertexFormat::NormalizedShort2;
                case Graphics::VertexElementFormat::NormalizedShort4:
                    return Cnb::CnbModelV2VertexFormat::NormalizedShort4;
                case Graphics::VertexElementFormat::HalfVector2:
                    return Cnb::CnbModelV2VertexFormat::HalfVector2;
                case Graphics::VertexElementFormat::HalfVector4:
                    return Cnb::CnbModelV2VertexFormat::HalfVector4;
            }
            fail("a VertexDeclaration uses an unknown VertexElementFormat");
            return Cnb::CnbModelV2VertexFormat::Single;
        };
        const auto usage = [&fail](const Graphics::VertexElementUsage value)
        {
            switch (value)
            {
                case Graphics::VertexElementUsage::Position:
                    return Cnb::CnbModelV2VertexUsage::Position;
                case Graphics::VertexElementUsage::Color:
                    return Cnb::CnbModelV2VertexUsage::Color;
                case Graphics::VertexElementUsage::TextureCoordinate:
                    return Cnb::CnbModelV2VertexUsage::TextureCoordinate;
                case Graphics::VertexElementUsage::Normal:
                    return Cnb::CnbModelV2VertexUsage::Normal;
                case Graphics::VertexElementUsage::Binormal:
                    return Cnb::CnbModelV2VertexUsage::Binormal;
                case Graphics::VertexElementUsage::Tangent:
                    return Cnb::CnbModelV2VertexUsage::Tangent;
                case Graphics::VertexElementUsage::BlendIndices:
                    return Cnb::CnbModelV2VertexUsage::BlendIndices;
                case Graphics::VertexElementUsage::BlendWeight:
                    return Cnb::CnbModelV2VertexUsage::BlendWeight;
                case Graphics::VertexElementUsage::Depth:
                    return Cnb::CnbModelV2VertexUsage::Depth;
                case Graphics::VertexElementUsage::Fog:
                    return Cnb::CnbModelV2VertexUsage::Fog;
                case Graphics::VertexElementUsage::PointSize:
                    return Cnb::CnbModelV2VertexUsage::PointSize;
                case Graphics::VertexElementUsage::Sample:
                    return Cnb::CnbModelV2VertexUsage::Sample;
                case Graphics::VertexElementUsage::TessellateFactor:
                    return Cnb::CnbModelV2VertexUsage::TessellateFactor;
            }
            fail("a VertexDeclaration uses an unknown VertexElementUsage");
            return Cnb::CnbModelV2VertexUsage::Position;
        };

        if (source.bones.empty()) { fail("the Model has no root bone"); }
        if (source.rootBone < 0 ||
            static_cast<std::size_t>(source.rootBone) >= source.bones.size())
        {
            fail("the serialized root bone is out of range");
        }

        Cnb::CnbModelV2Data result;
        result.rootBone = static_cast<std::uint32_t>(source.rootBone);
        result.bones.reserve(source.bones.size());
        std::vector<std::vector<std::int32_t>> childrenByParent(source.bones.size());
        for (std::size_t bone = 0u; bone < source.bones.size(); ++bone)
        {
            const XnbModelBoneData& input = source.bones[bone];
            if (input.parent < -1 ||
                (input.parent >= 0 && static_cast<std::size_t>(input.parent) >= bone))
            {
                fail("bone " + std::to_string(bone) +
                     " is not ordered after its valid parent");
            }
            if (input.parent >= 0)
            {
                childrenByParent[static_cast<std::size_t>(input.parent)].push_back(
                    static_cast<std::int32_t>(bone));
            }
            result.bones.push_back({input.name, input.parent, MatrixValues(input.transform)});
        }
        if (source.bones[static_cast<std::size_t>(source.rootBone)].parent != -1)
        {
            fail("the serialized root bone has a parent");
        }
        for (std::size_t bone = 0u; bone < source.bones.size(); ++bone)
        {
            if (source.bones[bone].children != childrenByParent[bone])
            {
                fail("bone " + std::to_string(bone) +
                     " has child references that do not match the serialized parent graph");
            }
        }

        constexpr std::uint32_t Missing = std::numeric_limits<std::uint32_t>::max();
        std::vector<std::uint32_t> vertexResource(source.sharedResources.size(), Missing);
        std::vector<std::uint32_t> indexResource(source.sharedResources.size(), Missing);
        std::vector<std::uint32_t> effectResource(source.sharedResources.size(), Missing);

        const auto findDeclaration =
            [&result, &format, &usage, &toU32](
                const XnbVertexDeclarationData& sourceDeclaration)
        {
            for (std::size_t index = 0u; index < result.vertexDeclarations.size(); ++index)
            {
                const Cnb::CnbModelV2VertexDeclaration& candidate =
                    result.vertexDeclarations[index];
                if (sourceDeclaration.stride < 0 ||
                    candidate.vertexStride !=
                        static_cast<std::uint32_t>(sourceDeclaration.stride) ||
                    candidate.elements.size() != sourceDeclaration.elements.size())
                {
                    continue;
                }
                bool equal = true;
                for (std::size_t element = 0u; element < candidate.elements.size(); ++element)
                {
                    const Graphics::VertexElement& sourceElement =
                        sourceDeclaration.elements[element];
                    const Cnb::CnbModelV2VertexElement& candidateElement =
                        candidate.elements[element];
                    if (sourceElement.getOffsetProperty() < 0 ||
                        sourceElement.getUsageIndexProperty() < 0 ||
                        candidateElement.offset !=
                            static_cast<std::uint32_t>(sourceElement.getOffsetProperty()) ||
                        candidateElement.format != format(
                            sourceElement.getVertexElementFormatProperty()) ||
                        candidateElement.usage != usage(
                            sourceElement.getVertexElementUsageProperty()) ||
                        candidateElement.usageIndex !=
                            static_cast<std::uint32_t>(sourceElement.getUsageIndexProperty()))
                    {
                        equal = false;
                        break;
                    }
                }
                if (equal) { return static_cast<std::uint32_t>(index); }
            }

            Cnb::CnbModelV2VertexDeclaration declaration;
            declaration.vertexStride = toU32(
                sourceDeclaration.stride, "a VertexDeclaration stride");
            declaration.elements.reserve(sourceDeclaration.elements.size());
            for (const Graphics::VertexElement& element : sourceDeclaration.elements)
            {
                declaration.elements.push_back({
                    toU32(element.getOffsetProperty(), "a VertexDeclaration element offset"),
                    format(element.getVertexElementFormatProperty()),
                    usage(element.getVertexElementUsageProperty()),
                    toU32(element.getUsageIndexProperty(),
                          "a VertexDeclaration usage index")});
            }
            result.vertexDeclarations.push_back(std::move(declaration));
            return static_cast<std::uint32_t>(result.vertexDeclarations.size() - 1u);
        };

        for (std::size_t resource = 0u; resource < source.sharedResources.size(); ++resource)
        {
            const auto& value = source.sharedResources[resource].value;
            if (const auto* vertex = std::get_if<XnbVertexBufferData>(&value))
            {
                vertexResource[resource] = static_cast<std::uint32_t>(result.vertexBuffers.size());
                result.vertexBuffers.push_back(
                    {findDeclaration(vertex->declaration), vertex->vertexCount, vertex->bytes});
            }
            else if (const auto* indices = std::get_if<XnbIndexBufferData>(&value))
            {
                indexResource[resource] = static_cast<std::uint32_t>(result.indexBuffers.size());
                if ((indices->indexElementSize != 2u && indices->indexElementSize != 4u) ||
                    indices->bytes.size() % indices->indexElementSize != 0u)
                {
                    fail("shared index resource " + std::to_string(resource + 1u) +
                         " has an invalid element width or byte count");
                }
                result.indexBuffers.push_back({
                    indices->indexElementSize,
                    static_cast<std::uint32_t>(
                        indices->bytes.size() / indices->indexElementSize),
                    indices->bytes});
            }
            else
            {
                Cnb::CnbModelV2Effect output;
                if (const auto* effect = std::get_if<XnbBasicEffectData>(&value))
                {
                    output.kind = Cnb::CnbModelV2EffectKind::BasicEffect;
                    output.primaryTexture = texture(effect->textureReference);
                    output.diffuse = vector3(effect->diffuseColor);
                    output.emissive = vector3(effect->emissiveColor);
                    output.specular = vector3(effect->specularColor);
                    output.specularPower = effect->specularPower;
                    output.alpha = effect->alpha;
                    output.vertexColorEnabled = effect->vertexColorEnabled;
                }
                else if (const auto* effect = std::get_if<XnbAlphaTestEffectData>(&value))
                {
                    if (effect->alphaFunction < 0 || effect->alphaFunction > 7)
                    {
                        fail("shared AlphaTestEffect resource " +
                             std::to_string(resource + 1u) +
                             " has an invalid CompareFunction");
                    }
                    output.kind = Cnb::CnbModelV2EffectKind::AlphaTestEffect;
                    output.primaryTexture = texture(effect->textureReference);
                    output.diffuse = vector3(effect->diffuseColor);
                    output.alpha = effect->alpha;
                    output.alphaFunction = static_cast<std::uint32_t>(effect->alphaFunction);
                    output.referenceAlpha = effect->referenceAlpha;
                    output.vertexColorEnabled = effect->vertexColorEnabled;
                }
                else if (const auto* effect = std::get_if<XnbDualTextureEffectData>(&value))
                {
                    output.kind = Cnb::CnbModelV2EffectKind::DualTextureEffect;
                    output.primaryTexture = texture(effect->textureReference);
                    output.secondaryTexture = texture(effect->texture2Reference);
                    output.diffuse = vector3(effect->diffuseColor);
                    output.alpha = effect->alpha;
                    output.vertexColorEnabled = effect->vertexColorEnabled;
                }
                else if (const auto* effect =
                             std::get_if<XnbEnvironmentMapEffectData>(&value))
                {
                    output.kind = Cnb::CnbModelV2EffectKind::EnvironmentMapEffect;
                    output.primaryTexture = texture(effect->textureReference);
                    output.cubeTexture = texture(effect->environmentMapReference);
                    output.diffuse = vector3(effect->diffuseColor);
                    output.emissive = vector3(effect->emissiveColor);
                    output.specular = vector3(effect->environmentMapSpecular);
                    output.alpha = effect->alpha;
                    output.environmentMapAmount = effect->environmentMapAmount;
                    output.fresnelFactor = effect->fresnelFactor;
                }
                else if (const auto* effect = std::get_if<XnbSkinnedEffectData>(&value))
                {
                    if (effect->weightsPerVertex != 1 && effect->weightsPerVertex != 2 &&
                        effect->weightsPerVertex != 4)
                    {
                        fail("shared SkinnedEffect resource " +
                             std::to_string(resource + 1u) +
                             " has unsupported WeightsPerVertex");
                    }
                    output.kind = Cnb::CnbModelV2EffectKind::SkinnedEffect;
                    output.primaryTexture = texture(effect->textureReference);
                    output.diffuse = vector3(effect->diffuseColor);
                    output.emissive = vector3(effect->emissiveColor);
                    output.specular = vector3(effect->specularColor);
                    output.specularPower = effect->specularPower;
                    output.alpha = effect->alpha;
                    output.weightsPerVertex =
                        static_cast<std::uint32_t>(effect->weightsPerVertex);
                }
                else
                {
                    fail("shared resource " + std::to_string(resource + 1u) +
                         " uses an unsupported resource type");
                }
                effectResource[resource] = static_cast<std::uint32_t>(result.effects.size());
                result.effects.push_back(std::move(output));
            }
        }

        result.meshes.reserve(source.meshes.size());
        for (std::size_t meshIndex = 0u; meshIndex < source.meshes.size(); ++meshIndex)
        {
            const XnbModelMeshData& mesh = source.meshes[meshIndex];
            if (mesh.parentBone < 0 ||
                static_cast<std::size_t>(mesh.parentBone) >= source.bones.size())
            {
                fail("mesh " + std::to_string(meshIndex) + " has an invalid parent bone");
            }
            Cnb::CnbModelV2Mesh output;
            output.name = mesh.name;
            output.parentBone = mesh.parentBone;
            output.boundingSphere = {{mesh.boundingSphere.Center.X,
                                      mesh.boundingSphere.Center.Y,
                                      mesh.boundingSphere.Center.Z,
                                      mesh.boundingSphere.Radius}};
            output.parts.reserve(mesh.parts.size());
            for (std::size_t partIndex = 0u; partIndex < mesh.parts.size(); ++partIndex)
            {
                const XnbModelPartData& part = mesh.parts[partIndex];
                const std::string where = ModelPartContext(mesh, meshIndex, partIndex);
                const auto resourceIndex = [&fail, &source, &where, Missing](
                    const std::int32_t reference,
                    const std::vector<std::uint32_t>& mapping,
                    const char* kind)
                {
                    if (reference < 0 ||
                        static_cast<std::size_t>(reference) >= source.sharedResources.size())
                    {
                        fail(where + " has an invalid shared-resource reference");
                    }
                    const std::uint32_t mapped = mapping[static_cast<std::size_t>(reference)];
                    if (mapped == Missing)
                    {
                        fail(where + " does not reference a " + std::string(kind) + " resource");
                    }
                    return mapped;
                };
                output.parts.push_back({
                    toU32(part.vertexOffset, where + " VertexOffset"),
                    toU32(part.vertexCount, where + " NumVertices"),
                    toU32(part.startIndex, where + " StartIndex"),
                    toU32(part.primitiveCount, where + " PrimitiveCount"),
                    resourceIndex(part.vertexBufferResource, vertexResource, "VertexBufferReader"),
                    resourceIndex(part.indexBufferResource, indexResource, "IndexBufferReader"),
                    resourceIndex(part.effectResource, effectResource, "stock effect")});
            }
            result.meshes.push_back(std::move(output));
        }
        return result;
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

    CNA::Content::Import::ImportedSound ConvertXnbSoundToImportedSound(
        const XnbSoundEffectData& source, const std::string& origin,
        const bool allowXboxPayload)
    {
        if (source.platform == 'x' && !allowXboxPayload)
        {
            throw ContentLoadException(
                "'" + origin +
                "': Xbox 360 SoundEffect sample byte order is not supported for native CNB "
                "transcoding.");
        }
        if (source.channels != 1u && source.channels != 2u)
        {
            throw ContentLoadException(
                "'" + origin + "': unsupported SoundEffect channel count (" +
                std::to_string(source.channels) + "); only mono and stereo are supported.");
        }
        if (source.sampleRate == 0u)
        {
            throw ContentLoadException(
                "'" + origin + "': SoundEffect sample rate must not be zero.");
        }

        CNA::Content::Import::ImportedSound imported;
        imported.sampleRate = source.sampleRate;
        imported.channels = source.channels;
        if (source.formatTag == WaveFormatPcm && source.bitsPerSample == 16u)
        {
            imported.encoding = CNA::Content::Import::ImportedPcmEncoding::Signed16LittleEndian;
            imported.samples = source.samples;
            const std::uint32_t frameBytes = source.channels * 2u;
            if (source.samples.size() % frameBytes != 0u ||
                source.samples.size() / frameBytes > std::numeric_limits<std::uint32_t>::max())
            {
                throw ContentLoadException(
                    "'" + origin + "': PCM16 SoundEffect byte count is not frame-aligned.");
            }
            imported.frameCount =
                static_cast<std::uint32_t>(source.samples.size() / frameBytes);
        }
        else if (source.formatTag == WaveFormatPcm && source.bitsPerSample == 8u)
        {
            imported.encoding = CNA::Content::Import::ImportedPcmEncoding::Unsigned8;
            imported.samples = source.samples;
            if (source.samples.size() % source.channels != 0u ||
                source.samples.size() / source.channels >
                    std::numeric_limits<std::uint32_t>::max())
            {
                throw ContentLoadException(
                    "'" + origin + "': PCM8 SoundEffect byte count is not frame-aligned.");
            }
            imported.frameCount =
                static_cast<std::uint32_t>(source.samples.size() / source.channels);
        }
        else if ((source.formatTag == WaveFormatIeeeFloat && source.bitsPerSample == 32u) ||
                 (source.formatTag == WaveFormatMsAdpcm && source.bitsPerSample == 4u) ||
                 (source.formatTag == WaveFormatImaAdpcm && source.bitsPerSample == 4u))
        {
#ifdef SOUND_ENABLED
            imported.encoding = CNA::Content::Import::ImportedPcmEncoding::Signed16LittleEndian;
            imported.samples = DecodeWaveToPcm16(source, origin, imported.frameCount);
#else
            throw ContentLoadException(
                "'" + origin + "': compressed/float XNB SoundEffect transcoding requires "
                "CNA_AUDIO_PLATFORM=SDL3 because no decoder is available in this build.");
#endif
        }
        else
        {
            throw ContentLoadException(
                "'" + origin + "': unsupported SoundEffect wave format (formatTag=" +
                std::to_string(source.formatTag) + ", bitsPerSample=" +
                std::to_string(source.bitsPerSample) + ", channels=" +
                std::to_string(source.channels) + ", sampleRate=" +
                std::to_string(source.sampleRate) + "). XMA2 and unknown codecs have no "
                "native CNB decode path.");
        }
        imported.loopStart = static_cast<std::uint32_t>(std::max(0, source.loopStart));
        imported.loopLength = static_cast<std::uint32_t>(std::max(0, source.loopLength));
        ValidateSoundMetadata(source, imported.frameCount, origin);
        return imported;
    }

    XnbCanonicalAsset DecodeXnbCanonicalAsset(
        const std::filesystem::path& path, const XnbReadLimits& limits)
    {
        const std::string origin = ContentPathToUtf8(path);
        const std::vector<std::uint8_t> file = ReadFile(path, limits);
        if (file.size() < 10u)
        {
            throw ContentLoadException("'" + origin + "' is truncated before its XNB header.");
        }

        System::IO::MemoryStream headerStream(file.data(), static_cast<std::int32_t>(file.size()));
        System::IO::BinaryReader headerReader(&headerStream, true);
        const XnbHeader header = ParseXnbHeader(headerReader, origin);
        if (header.totalLength != static_cast<std::int32_t>(file.size()))
        {
            throw ContentLoadException(
                "'" + origin + "' declares totalLength " +
                std::to_string(header.totalLength) + ", but the file contains " +
                std::to_string(file.size()) + " bytes.");
        }

        std::vector<std::uint8_t> ownedBody;
        const std::uint8_t* body = nullptr;
        std::size_t bodySize = 0u;
        switch (header.compression)
        {
            case XnbCompression::None:
                body = file.data() + 10u;
                bodySize = file.size() - 10u;
                break;
            case XnbCompression::Lzx:
            {
                if (file.size() < 14u)
                {
                    throw ContentLoadException(
                        "'" + origin + "' is truncated before its decompressed-size field.");
                }
                System::IO::MemoryStream sizeStream(file.data() + 10u, 4);
                System::IO::BinaryReader sizeReader(&sizeStream, true);
                const std::int32_t decompressedSize = sizeReader.ReadInt32();
                ownedBody = DecompressXnbPayload(
                    file.data() + 14u, static_cast<std::int32_t>(file.size() - 14u),
                    decompressedSize, origin, limits);
                body = ownedBody.data();
                bodySize = ownedBody.size();
                break;
            }
            case XnbCompression::Lz4:
            {
                if (file.size() < 14u)
                {
                    throw ContentLoadException(
                        "'" + origin + "' is truncated before its decompressed-size field.");
                }
                System::IO::MemoryStream sizeStream(file.data() + 10u, 4);
                System::IO::BinaryReader sizeReader(&sizeStream, true);
                const std::int32_t decompressedSize = sizeReader.ReadInt32();
                ownedBody = DecompressXnbLz4Payload(
                    file.data() + 14u, static_cast<std::int32_t>(file.size() - 14u),
                    decompressedSize, origin, limits);
                body = ownedBody.data();
                bodySize = ownedBody.size();
                break;
            }
            case XnbCompression::Unknown:
            default:
                throw ContentLoadException(
                    "'" + origin + "' has an unrecognized compression flag combination.");
        }
        if (bodySize > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        {
            throw ContentLoadException("'" + origin + "' has an oversized XNB body.");
        }

        System::IO::MemoryStream bodyStream(body, static_cast<std::int32_t>(bodySize));
        ContentReader reader(
            nullptr, &bodyStream, origin, header.version, header.platform, nullptr, limits);
        XnbCanonicalReaderAccess::Initialize(reader);
        const XnbTypeReaderTableEntry& root =
            XnbCanonicalReaderAccess::ReadReference(reader);
        if (root.version != 0)
        {
            throw ContentLoadException(
                "'" + origin + "' uses root ContentTypeReader '" + root.normalizedName +
                "' at unsupported version (" + std::to_string(root.version) + ").");
        }
        const std::int32_t sharedResourceCount =
            XnbCanonicalReaderAccess::SharedResourceCount(reader);
        const bool modelRoot =
            root.normalizedName == "Microsoft.Xna.Framework.Content.ModelReader";
        if (sharedResourceCount != 0 && !modelRoot)
        {
            throw ContentLoadException(
                "'" + origin + "' root ContentTypeReader '" + root.normalizedName +
                "' uses " +
                std::to_string(sharedResourceCount) +
                " shared resource(s); that reader graph is not supported for native CNB "
                "transcoding.");
        }

        XnbCanonicalAsset result;
        result.rootReader = root.normalizedName;
        result.platform = header.platform;
        result.version = header.version;
        result.compression = header.compression;
        if (root.normalizedName == "Microsoft.Xna.Framework.Content.Texture2DReader")
        {
            result.value = DecodeTexture2DXnbData(reader);
        }
        else if (root.normalizedName == "Microsoft.Xna.Framework.Content.Texture3DReader")
        {
            result.value = DecodeTexture3DXnbData(reader);
        }
        else if (root.normalizedName == "Microsoft.Xna.Framework.Content.TextureCubeReader")
        {
            result.value = DecodeTextureCubeXnbData(reader);
        }
        else if (root.normalizedName == "Microsoft.Xna.Framework.Content.SpriteFontReader")
        {
            result.value = DecodeSpriteFontXnbData(reader);
        }
        else if (root.normalizedName == "Microsoft.Xna.Framework.Content.SoundEffectReader")
        {
            result.value = DecodeSoundEffectXnbData(reader);
        }
        else if (root.normalizedName == "Microsoft.Xna.Framework.Content.CurveReader")
        {
            result.value = DecodeCurveXnbData(reader);
        }
        else if (root.normalizedName == "Microsoft.Xna.Framework.Content.SongReader")
        {
            result.value = DecodeSongXnbData(reader);
        }
        else if (root.normalizedName == "Microsoft.Xna.Framework.Content.VideoReader")
        {
            result.value = DecodeVideoXnbData(reader, true);
        }
        else if (modelRoot)
        {
            CanonicalModelSink sink(sharedResourceCount, origin);
            XnbModelData model = ReadXnbModelGraph(reader, sink);
            model.sharedResources.reserve(static_cast<std::size_t>(sharedResourceCount));
            for (std::int32_t resource = 0; resource < sharedResourceCount; ++resource)
            {
                const XnbTypeReaderTableEntry& resourceReader =
                    XnbCanonicalReaderAccess::ReadReference(reader);
                if (resourceReader.version != 0)
                {
                    throw ContentLoadException(
                        "'" + origin + "' uses shared-resource reader '" +
                        resourceReader.normalizedName + "' at unsupported version (" +
                        std::to_string(resourceReader.version) + ").");
                }
                XnbModelSharedResourceData decoded;
                decoded.reader = resourceReader.normalizedName;
                if (resourceReader.normalizedName ==
                    "Microsoft.Xna.Framework.Content.VertexBufferReader")
                {
                    decoded.value = DecodeVertexBufferXnbData(reader);
                }
                else if (resourceReader.normalizedName ==
                         "Microsoft.Xna.Framework.Content.IndexBufferReader")
                {
                    decoded.value = DecodeIndexBufferXnbData(reader);
                }
                else if (resourceReader.normalizedName ==
                         "Microsoft.Xna.Framework.Content.BasicEffectReader")
                {
                    decoded.value = DecodeBasicEffectXnbData(reader, reader.ReadString());
                }
                else if (resourceReader.normalizedName ==
                         "Microsoft.Xna.Framework.Content.AlphaTestEffectReader")
                {
                    decoded.value = DecodeAlphaTestEffectXnbData(reader, reader.ReadString());
                }
                else if (resourceReader.normalizedName ==
                         "Microsoft.Xna.Framework.Content.DualTextureEffectReader")
                {
                    std::string texture = reader.ReadString();
                    std::string texture2 = reader.ReadString();
                    decoded.value = DecodeDualTextureEffectXnbData(
                        reader, std::move(texture), std::move(texture2));
                }
                else if (resourceReader.normalizedName ==
                         "Microsoft.Xna.Framework.Content.EnvironmentMapEffectReader")
                {
                    std::string texture = reader.ReadString();
                    std::string environmentMap = reader.ReadString();
                    decoded.value = DecodeEnvironmentMapEffectXnbData(
                        reader, std::move(texture), std::move(environmentMap));
                }
                else if (resourceReader.normalizedName ==
                         "Microsoft.Xna.Framework.Content.SkinnedEffectReader")
                {
                    decoded.value = DecodeSkinnedEffectXnbData(reader, reader.ReadString());
                }
                else
                {
                    throw ContentLoadException(
                        "XnbImporter: Model cannot be transcoded losslessly: shared resource " +
                        std::to_string(resource + 1) + " uses reader '" +
                        resourceReader.normalizedName +
                        "'; Model schema 2 supports only VertexBufferReader, IndexBufferReader, "
                        "and the five built-in stock-effect readers.");
                }
                model.sharedResources.push_back(std::move(decoded));
            }
            result.value = std::move(model);
        }
        else
        {
            throw ContentLoadException(
                "'" + origin + "' root ContentTypeReader '" + root.normalizedName +
                "' is not supported for native CNB transcoding.");
        }

        const std::int32_t remaining =
            static_cast<std::int32_t>(bodySize) - bodyStream.getPositionProperty();
        if (remaining > 0 && remaining <= 3)
        {
            for (std::int32_t index = 0; index < remaining; ++index)
            {
                if (reader.ReadByte() != 0u)
                {
                    throw ContentLoadException(
                        "'" + origin + "' has non-zero trailing bytes after its supported root "
                        "object.");
                }
            }
        }
        if (bodyStream.getPositionProperty() != static_cast<std::int32_t>(bodySize))
        {
            throw ContentLoadException(
                "'" + origin + "' has trailing or unconsumed bytes after its supported root "
                "object.");
        }
        return result;
    }
}

// SPDX-License-Identifier: MS-PL
// DecodeXnbCanonicalAsset(): any .xnb file to its canonical data, by its root reader -- what the
// content pipeline's .xnb importer uses. It names every family's decoder, so it is a translation
// unit of its own that only the pipeline links.
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
#include "CNA/Internal/Xnb/XnbContainerBody.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"
#include "System/IO/BinaryReader.hpp"
#include "System/IO/MemoryStream.hpp"
#include "XnbCanonicalShared.hpp"

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Content::ContentLoadException;
    using Microsoft::Xna::Framework::Content::ContentReader;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

    namespace
    {
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
            if (limits.maxFileSize < 0 ||
                size > static_cast<std::uintmax_t>(limits.maxFileSize))
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
                model_.bones.push_back({std::move(name), false, transform, -1, {}});
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
    }

    XnbCanonicalAsset DecodeXnbCanonicalAsset(
        const std::filesystem::path& path, const XnbReadLimits& limits)
    {
        const std::string origin = ContentPathToUtf8(path);
        const std::vector<std::uint8_t> file = ReadFile(path, limits);
        const XnbContainerBody container = DecodeXnbContainerBody(
            std::span<const std::uint8_t>(file.data(), file.size()), origin, limits);
        if (container.header.totalLength != static_cast<std::int32_t>(file.size()))
        {
            throw ContentLoadException(
                "'" + origin + "' declares totalLength " +
                std::to_string(container.header.totalLength) + ", but the file contains " +
                std::to_string(file.size()) + " bytes.");
        }
        const auto body = container.Bytes();
        System::IO::MemoryStream bodyStream(
            body.data(), static_cast<std::int32_t>(body.size()));
        ContentReader reader(
            nullptr, &bodyStream, origin, container.header.version,
            container.header.platform, nullptr, limits);
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
        result.platform = container.header.platform;
        result.version = container.header.version;
        result.compression = container.header.compression;
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
            result.value = DecodeSongXnbData(
                reader, XnbCanonicalReaderAccess::ReaderCount(reader) > 1u);
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
            static_cast<std::int32_t>(body.size()) - bodyStream.getPositionProperty();
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
        if (bodyStream.getPositionProperty() != static_cast<std::int32_t>(body.size()))
        {
            throw ContentLoadException(
                "'" + origin + "' has trailing or unconsumed bytes after its supported root "
                "object.");
        }
        return result;
    }
}

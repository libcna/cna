// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/XnbAssetTypeWriters.hpp"

#include <variant>

#include <limits>
#include <variant>
#include <memory>
#include <utility>

#include "CNA/Internal/Xnb/XnbBuiltInWriters.hpp"
#include "CNA/Internal/Xnb/XnbWriter.hpp"

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

    namespace
    {
        /**
         * @brief Builds a reader identity for a type hosted by `Microsoft.Xna.Framework.Graphics`.
         *
         * @param readerBaseName Assembly-free reader type name.
         * @param targetBaseName Assembly-free target type name.
         * @param evidence How this spelling was established.
         * @return The complete identity.
         */
        [[nodiscard]] XnbReaderIdentity GraphicsIdentity(
            const char* readerBaseName, const char* targetBaseName,
            const XnbNameEvidence evidence)
        {
            XnbReaderIdentity identity;
            identity.readerBaseName = readerBaseName;
            identity.readerAssembly = XnbAssembly::FrameworkGraphics;
            identity.targetBaseName = targetBaseName;
            identity.targetAssembly = XnbAssembly::FrameworkGraphics;
            identity.evidence = evidence;
            return identity;
        }

        /**
         * @brief Builds a reader identity for a type whose reader lives in the core assembly.
         *
         * @param readerBaseName Assembly-free reader type name.
         * @param targetBaseName Assembly-free target type name.
         * @param targetAssembly Assembly hosting the target type.
         * @param evidence How this spelling was established.
         * @return The complete identity.
         */
        [[nodiscard]] XnbReaderIdentity CoreIdentity(
            const char* readerBaseName, const char* targetBaseName,
            const XnbAssembly targetAssembly, const XnbNameEvidence evidence)
        {
            XnbReaderIdentity identity;
            identity.readerBaseName = readerBaseName;
            identity.readerAssembly = XnbAssembly::None;
            identity.targetBaseName = targetBaseName;
            identity.targetAssembly = targetAssembly;
            identity.evidence = evidence;
            return identity;
        }

        /**
         * @brief Encodes a surface format for the container version in force.
         *
         * Container version 5 stores the `SurfaceFormat` ordinal directly. Version 4 stores an
         * earlier, sparser numbering; only the four formats that numbering can express are
         * accepted there, and CNA-only extension formats are refused for both versions because no
         * XNA-compatible runtime knows them.
         *
         * @param output The file being written, consulted for its container version.
         * @param format The canonical surface format.
         * @param readerName Reader name used in the failure message.
         * @return The `Int32` value to serialize.
         * @throws XnbWriteException for a format the selected container version cannot express.
         */
        [[nodiscard]] std::int32_t EncodeSurfaceFormat(
            const XnbWriter& output, const SurfaceFormat format, const char* readerName)
        {
            const XnbContainerVersion version = output.Version() >= 5
                                                    ? XnbContainerVersion::Xna40
                                                    : XnbContainerVersion::Legacy4;
            if (!IsXnbWritableSurfaceFormat(format, version))
            {
                throw XnbWriteException(
                    std::string("'") + output.AssetName() + "': " + readerName +
                    " cannot write SurfaceFormat ordinal " +
                    std::to_string(static_cast<int>(format)) +
                    " into a container-version-" + std::to_string(output.Version()) +
                    " file. Version 5 expresses every XNA 4.0 SurfaceFormat but no CNA extension "
                    "format; version 4 expresses only ColorBgraEXT, Dxt1, Dxt3 and Dxt5.");
            }
            if (version == XnbContainerVersion::Xna40) { return static_cast<std::int32_t>(format); }
            switch (format)
            {
                case SurfaceFormat::ColorBgraEXT: return 1;
                case SurfaceFormat::Dxt1: return 28;
                case SurfaceFormat::Dxt3: return 30;
                case SurfaceFormat::Dxt5: return 32;
                default: break;
            }
            throw XnbWriteException("unreachable: legacy surface-format encoding");
        }

        /**
         * @brief Writes the face-major, mip-major level payloads of a texture.
         *
         * @param output The file being written.
         * @param texture Canonical texture data whose level count must equal faces times mips.
         * @param readerName Reader name used in the failure message.
         * @throws XnbWriteException when the level count disagrees with the declared shape.
         */
        void WriteTextureLevels(XnbWriter& output, const XnbTextureData& texture,
                                const char* readerName)
        {
            const std::size_t expected =
                static_cast<std::size_t>(texture.faceCount) * texture.mipCount;
            if (texture.levels.size() != expected)
            {
                throw XnbWriteException(
                    std::string("'") + output.AssetName() + "': " + readerName + " was given " +
                    std::to_string(texture.levels.size()) + " level payloads but declares " +
                    std::to_string(texture.faceCount) + " face(s) times " +
                    std::to_string(texture.mipCount) + " mip level(s) = " +
                    std::to_string(expected) + ".");
            }
            for (const std::vector<std::uint8_t>& level : texture.levels)
            {
                output.WriteLengthPrefixedBytes(level);
            }
        }

        /**
         * @brief Rejects a texture whose declared shape is unusable.
         *
         * @param output The file being written.
         * @param texture Canonical texture data.
         * @param readerName Reader name used in the failure message.
         * @throws XnbWriteException for a zero dimension or a zero mip count.
         */
        void RequireTextureShape(const XnbWriter& output, const XnbTextureData& texture,
                                 const char* readerName)
        {
            if (texture.width == 0u || texture.height == 0u || texture.depth == 0u ||
                texture.mipCount == 0u || texture.faceCount == 0u)
            {
                throw XnbWriteException(
                    std::string("'") + output.AssetName() + "': " + readerName +
                    " requires positive width, height, depth, face count and mip count.");
            }
            if (texture.width > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) ||
                texture.height > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) ||
                texture.depth > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) ||
                texture.mipCount > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
            {
                throw XnbWriteException(
                    std::string("'") + output.AssetName() + "': " + readerName +
                    " was given a dimension the format's Int32 fields cannot describe.");
            }
        }

        /**
         * @brief Swaps a 16-bit WAVEFORMATEX field when the target platform is big-endian.
         *
         * @param swap Whether the target requires byte swapping.
         * @param value The host-order value.
         * @return The value in target order.
         */
        [[nodiscard]] std::uint16_t Swap16(const bool swap, const std::uint16_t value)
        {
            if (!swap) { return value; }
            return static_cast<std::uint16_t>((value >> 8u) | (value << 8u));
        }

        /**
         * @brief Swaps a 32-bit WAVEFORMATEX field when the target platform is big-endian.
         *
         * @param swap Whether the target requires byte swapping.
         * @param value The host-order value.
         * @return The value in target order.
         */
        [[nodiscard]] std::uint32_t Swap32(const bool swap, const std::uint32_t value)
        {
            if (!swap) { return value; }
            return ((value >> 24u) & 0x000000FFu) | ((value >> 8u) & 0x0000FF00u) |
                   ((value << 8u) & 0x00FF0000u) | ((value << 24u) & 0xFF000000u);
        }

        /**
         * @brief Writes one Model bone reference using the width the bone count selects.
         *
         * @param output The file being written.
         * @param index Zero-based bone index, or -1 for "no bone".
         * @param boneCount Total bone count, which selects a byte or a `UInt32` field.
         * @throws XnbWriteException for an index outside the bone table.
         */
        void WriteBoneReference(XnbWriter& output, const std::int32_t index,
                                const std::uint32_t boneCount)
        {
            if (index < -1 || (index >= 0 && static_cast<std::uint32_t>(index) >= boneCount))
            {
                throw XnbWriteException(
                    "'" + output.AssetName() + "': ModelWriter was given bone reference " +
                    std::to_string(index) + ", which is outside the " +
                    std::to_string(boneCount) + "-bone table.");
            }
            const std::uint32_t encoded =
                index < 0 ? 0u : static_cast<std::uint32_t>(index) + 1u;
            if (boneCount < 255u) { output.WriteByte(static_cast<std::uint8_t>(encoded)); }
            else { output.WriteUInt32(encoded); }
        }

        /**
         * @brief Writes a `VertexDeclaration` payload: stride, element count, then each element.
         *
         * @param output The file being written.
         * @param declaration Canonical declaration data.
         */
        void WriteVertexDeclarationPayload(XnbWriter& output,
                                            const XnbVertexDeclarationData& declaration)
        {
            if (declaration.stride <= 0)
            {
                throw XnbWriteException(
                    "'" + output.AssetName() +
                    "': VertexDeclarationWriter requires a positive vertex stride.");
            }
            output.RequireCollectionCount(declaration.elements.size(),
                                          "VertexDeclarationWriter");
            output.WriteInt32(declaration.stride);
            output.WriteInt32(static_cast<std::int32_t>(declaration.elements.size()));
            for (const auto& element : declaration.elements)
            {
                output.WriteInt32(element.getOffsetProperty());
                output.WriteInt32(
                    static_cast<std::int32_t>(element.getVertexElementFormatProperty()));
                output.WriteInt32(
                    static_cast<std::int32_t>(element.getVertexElementUsageProperty()));
                output.WriteInt32(element.getUsageIndexProperty());
            }
        }

        /**
         * @brief Enqueues one Model shared resource under its own concrete type.
         *
         * @param output The file being written.
         * @param resource The canonical shared resource to enqueue.
         * @return The one-based shared-resource identifier.
         * @throws XnbWriteException for a resource variant no writer covers.
         */
        [[nodiscard]] std::int32_t EnqueueModelSharedResource(
            XnbWriter& output, const XnbModelSharedResourceData& resource)
        {
            return std::visit(
                [&output](const auto& value) { return output.AddSharedResource(value); },
                resource.value);
        }

        /**
         * @brief Writes a complete Model object graph, mirroring ReadXnbModelGraph() field for
         *        field.
         *
         * Shared resources are enqueued up front, in the order the canonical graph lists them, so
         * the identifiers the mesh parts reference match the order the resources are serialized
         * in. Tags are always null: the canonical Model representation has no Tag, and inventing
         * one would change what a game observes.
         *
         * @param output The file being written.
         * @param model The canonical Model graph.
         * @throws XnbWriteException for an out-of-range bone, mesh or shared-resource reference.
         */
        void WriteModelGraph(XnbWriter& output, const XnbModelData& model)
        {
            output.RequireCollectionCount(model.bones.size(), "ModelWriter bones");
            output.RequireCollectionCount(model.meshes.size(), "ModelWriter meshes");
            output.RequireCollectionCount(model.sharedResources.size(),
                                          "ModelWriter shared resources");

            std::vector<std::int32_t> sharedIds;
            sharedIds.reserve(model.sharedResources.size());
            for (const XnbModelSharedResourceData& resource : model.sharedResources)
            {
                sharedIds.push_back(EnqueueModelSharedResource(output, resource));
            }

            const auto boneCount = static_cast<std::uint32_t>(model.bones.size());
            const auto requireSharedId =
                [&output, &sharedIds](const std::int32_t zeroBased, const char* what)
            {
                if (zeroBased < 0 || static_cast<std::size_t>(zeroBased) >= sharedIds.size())
                {
                    throw XnbWriteException(
                        "'" + output.AssetName() + "': ModelWriter mesh part references " +
                        std::string(what) + " shared resource " + std::to_string(zeroBased) +
                        ", which is outside the " + std::to_string(sharedIds.size()) +
                        "-entry shared-resource table. An XNA mesh part must name a vertex "
                        "buffer, an index buffer and an effect.");
                }
                return sharedIds[static_cast<std::size_t>(zeroBased)];
            };

            output.WriteUInt32(boneCount);
            for (const XnbModelBoneData& bone : model.bones)
            {
                output.WriteObject(bone.name);
                output.WriteMatrix(bone.transform);
            }
            for (const XnbModelBoneData& bone : model.bones)
            {
                WriteBoneReference(output, bone.parent, boneCount);
                output.RequireCollectionCount(bone.children.size(), "ModelWriter bone children");
                output.WriteUInt32(static_cast<std::uint32_t>(bone.children.size()));
                for (const std::int32_t child : bone.children)
                {
                    WriteBoneReference(output, child, boneCount);
                }
            }

            output.WriteInt32(static_cast<std::int32_t>(model.meshes.size()));
            for (const XnbModelMeshData& mesh : model.meshes)
            {
                output.WriteObject(mesh.name);
                WriteBoneReference(output, mesh.parentBone, boneCount);
                output.WriteBoundingSphere(mesh.boundingSphere);
                output.WriteNullObject();
                output.RequireCollectionCount(mesh.parts.size(), "ModelWriter mesh parts");
                output.WriteInt32(static_cast<std::int32_t>(mesh.parts.size()));
                for (const XnbModelPartData& part : mesh.parts)
                {
                    output.WriteInt32(part.vertexOffset);
                    output.WriteInt32(part.vertexCount);
                    output.WriteInt32(part.startIndex);
                    output.WriteInt32(part.primitiveCount);
                    output.WriteNullObject();
                    output.WriteSharedResourceReference(
                        requireSharedId(part.vertexBufferResource, "a vertex-buffer"));
                    output.WriteSharedResourceReference(
                        requireSharedId(part.indexBufferResource, "an index-buffer"));
                    output.WriteSharedResourceReference(
                        requireSharedId(part.effectResource, "an effect"));
                }
            }

            WriteBoneReference(output, model.rootBone, boneCount);
            output.WriteNullObject();
        }

        template<typename T>
        void AddWriter(XnbTypeWriterRegistry& registry, XnbReaderIdentity identity,
                       const bool serializedByReference,
                       void (*payload)(XnbWriter&, const T&))
        {
            registry.Register(std::make_shared<const XnbFunctionTypeWriter<T>>(
                std::move(identity), serializedByReference, payload));
        }
    }

    bool IsXnbWritableSurfaceFormat(const SurfaceFormat format,
                                    const XnbContainerVersion version) noexcept
    {
        if (version == XnbContainerVersion::Legacy4)
        {
            return format == SurfaceFormat::ColorBgraEXT || format == SurfaceFormat::Dxt1 ||
                   format == SurfaceFormat::Dxt3 || format == SurfaceFormat::Dxt5;
        }
        switch (format)
        {
            case SurfaceFormat::Color:
            case SurfaceFormat::Bgr565:
            case SurfaceFormat::Bgra5551:
            case SurfaceFormat::Bgra4444:
            case SurfaceFormat::Dxt1:
            case SurfaceFormat::Dxt3:
            case SurfaceFormat::Dxt5:
            case SurfaceFormat::NormalizedByte2:
            case SurfaceFormat::NormalizedByte4:
            case SurfaceFormat::Rgba1010102:
            case SurfaceFormat::Rg32:
            case SurfaceFormat::Rgba64:
            case SurfaceFormat::Alpha8:
            case SurfaceFormat::Single:
            case SurfaceFormat::Vector2:
            case SurfaceFormat::Vector4:
            case SurfaceFormat::HalfSingle:
            case SurfaceFormat::HalfVector2:
            case SurfaceFormat::HalfVector4:
            case SurfaceFormat::HdrBlendable:
                return true;
            default:
                return false;
        }
    }

    XnbReaderIdentity XnbTexture2DReaderIdentity()
    {
        return GraphicsIdentity("Microsoft.Xna.Framework.Content.Texture2DReader",
                                "Microsoft.Xna.Framework.Graphics.Texture2D",
                                XnbNameEvidence::MonoGameFixture);
    }

    XnbReaderIdentity XnbTexture3DReaderIdentity()
    {
        return GraphicsIdentity("Microsoft.Xna.Framework.Content.Texture3DReader",
                                "Microsoft.Xna.Framework.Graphics.Texture3D",
                                XnbNameEvidence::DerivedRule);
    }

    XnbReaderIdentity XnbTextureCubeReaderIdentity()
    {
        return GraphicsIdentity("Microsoft.Xna.Framework.Content.TextureCubeReader",
                                "Microsoft.Xna.Framework.Graphics.TextureCube",
                                XnbNameEvidence::MonoGameFixture);
    }

    void RegisterBuiltInAssetXnbWriters(XnbTypeWriterRegistry& registry)
    {
        AddWriter<XnbTexture2DContent>(registry, XnbTexture2DReaderIdentity(), true,
            [](XnbWriter& output, const XnbTexture2DContent& value)
            {
                const XnbTextureData& texture = value.texture;
                RequireTextureShape(output, texture, "Texture2DWriter");
                if (texture.kind != XnbTextureKind::Texture2D || texture.faceCount != 1u ||
                    texture.depth != 1u)
                {
                    throw XnbWriteException(
                        "'" + output.AssetName() +
                        "': Texture2DWriter requires single-face, single-slice texture data.");
                }
                output.WriteInt32(
                    EncodeSurfaceFormat(output, texture.surfaceFormat, "Texture2DWriter"));
                output.WriteInt32(static_cast<std::int32_t>(texture.width));
                output.WriteInt32(static_cast<std::int32_t>(texture.height));
                output.WriteInt32(static_cast<std::int32_t>(texture.mipCount));
                WriteTextureLevels(output, texture, "Texture2DWriter");
            });

        AddWriter<XnbTexture3DContent>(registry, XnbTexture3DReaderIdentity(), true,
            [](XnbWriter& output, const XnbTexture3DContent& value)
            {
                const XnbTextureData& texture = value.texture;
                RequireTextureShape(output, texture, "Texture3DWriter");
                if (texture.kind != XnbTextureKind::Texture3D || texture.faceCount != 1u)
                {
                    throw XnbWriteException(
                        "'" + output.AssetName() +
                        "': Texture3DWriter requires single-face volume texture data.");
                }
                output.WriteInt32(
                    EncodeSurfaceFormat(output, texture.surfaceFormat, "Texture3DWriter"));
                output.WriteInt32(static_cast<std::int32_t>(texture.width));
                output.WriteInt32(static_cast<std::int32_t>(texture.height));
                output.WriteInt32(static_cast<std::int32_t>(texture.depth));
                output.WriteInt32(static_cast<std::int32_t>(texture.mipCount));
                WriteTextureLevels(output, texture, "Texture3DWriter");
            });

        AddWriter<XnbTextureCubeContent>(registry, XnbTextureCubeReaderIdentity(), true,
            [](XnbWriter& output, const XnbTextureCubeContent& value)
            {
                const XnbTextureData& texture = value.texture;
                RequireTextureShape(output, texture, "TextureCubeWriter");
                if (texture.kind != XnbTextureKind::TextureCube || texture.faceCount != 6u ||
                    texture.depth != 1u || texture.width != texture.height)
                {
                    throw XnbWriteException(
                        "'" + output.AssetName() +
                        "': TextureCubeWriter requires six square faces and a single slice.");
                }
                output.WriteInt32(
                    EncodeSurfaceFormat(output, texture.surfaceFormat, "TextureCubeWriter"));
                output.WriteInt32(static_cast<std::int32_t>(texture.width));
                output.WriteInt32(static_cast<std::int32_t>(texture.mipCount));
                WriteTextureLevels(output, texture, "TextureCubeWriter");
            });

        AddWriter<XnbSpriteFontData>(registry,
            GraphicsIdentity("Microsoft.Xna.Framework.Content.SpriteFontReader",
                             "Microsoft.Xna.Framework.Graphics.SpriteFont",
                             XnbNameEvidence::MonoGameFixture),
            true,
            [](XnbWriter& output, const XnbSpriteFontData& value)
            {
                if (value.glyphs.size() != value.cropping.size() ||
                    value.glyphs.size() != value.characters.size() ||
                    value.glyphs.size() != value.kerning.size())
                {
                    throw XnbWriteException(
                        "'" + output.AssetName() +
                        "': SpriteFontWriter requires equal glyph, cropping, character and "
                        "kerning counts, but was given " + std::to_string(value.glyphs.size()) +
                        "/" + std::to_string(value.cropping.size()) + "/" +
                        std::to_string(value.characters.size()) + "/" +
                        std::to_string(value.kerning.size()) + ".");
                }
                output.WriteObject(XnbTexture2DContent{value.atlas});
                output.WriteObject(value.glyphs);
                output.WriteObject(value.cropping);
                output.WriteObject(value.characters);
                output.WriteInt32(value.lineSpacing);
                output.WriteSingle(value.spacing);
                output.WriteObject(value.kerning);
                output.WriteBoolean(value.defaultCharacter.has_value());
                if (value.defaultCharacter.has_value())
                {
                    output.WriteChar(*value.defaultCharacter);
                }
            });

        AddWriter<XnbSoundEffectData>(registry,
            CoreIdentity("Microsoft.Xna.Framework.Content.SoundEffectReader",
                         "Microsoft.Xna.Framework.Audio.SoundEffect", XnbAssembly::Framework,
                         XnbNameEvidence::MonoGameFixture),
            true,
            [](XnbWriter& output, const XnbSoundEffectData& value)
            {
                const bool swap = output.Platform() == 'x';
                const std::size_t formatLength =
                    value.extensionData.empty() ? 16u : 18u + value.extensionData.size();
                if (formatLength > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
                {
                    throw XnbWriteException(
                        "'" + output.AssetName() +
                        "': SoundEffectWriter format block is too large to describe.");
                }
                output.WriteUInt32(static_cast<std::uint32_t>(formatLength));
                output.WriteUInt16(Swap16(swap, value.formatTag));
                output.WriteUInt16(Swap16(swap, value.channels));
                output.WriteUInt32(Swap32(swap, value.sampleRate));
                output.WriteUInt32(Swap32(swap, value.averageBytesPerSecond));
                output.WriteUInt16(Swap16(swap, value.blockAlign));
                output.WriteUInt16(Swap16(swap, value.bitsPerSample));
                if (!value.extensionData.empty())
                {
                    output.WriteUInt16(
                        Swap16(swap, static_cast<std::uint16_t>(value.extensionData.size())));
                    output.WriteBytes(value.extensionData);
                }
                output.WriteLengthPrefixedBytes(value.samples);
                output.WriteInt32(value.loopStart);
                output.WriteInt32(value.loopLength);
                output.WriteUInt32(value.storedDurationMs);
            });

        AddWriter<XnbSongData>(registry,
            CoreIdentity("Microsoft.Xna.Framework.Content.SongReader",
                         "Microsoft.Xna.Framework.Media.Song", XnbAssembly::Framework,
                         XnbNameEvidence::MonoGameFixture),
            true,
            [](XnbWriter& output, const XnbSongData& value)
            {
                // The media path is a bare length-prefixed string; the duration is dispatched
                // through Int32Reader. Both facts are read straight out of the committed
                // MonoGame Song fixture, whose type-reader table contains Int32Reader precisely
                // because the duration goes through the object protocol.
                output.WriteString(value.mediaPath);
                output.WriteObject(value.durationMs);
            });

        AddWriter<XnbVideoData>(registry,
            CoreIdentity("Microsoft.Xna.Framework.Content.VideoReader",
                         "Microsoft.Xna.Framework.Media.Video", XnbAssembly::Framework,
                         XnbNameEvidence::DerivedRule),
            true,
            [](XnbWriter& output, const XnbVideoData& value)
            {
                output.WriteObject(value.mediaPath);
                output.WriteObject(value.durationMs);
                output.WriteObject(value.width);
                output.WriteObject(value.height);
                output.WriteObject(value.framesPerSecond);
                output.WriteObject(value.soundtrackType);
            });

        AddWriter<XnbVertexDeclarationData>(registry,
            GraphicsIdentity("Microsoft.Xna.Framework.Content.VertexDeclarationReader",
                             "Microsoft.Xna.Framework.Graphics.VertexDeclaration",
                             XnbNameEvidence::MonoGameFixture),
            true,
            [](XnbWriter& output, const XnbVertexDeclarationData& value)
            {
                WriteVertexDeclarationPayload(output, value);
            });

        AddWriter<XnbVertexBufferData>(registry,
            GraphicsIdentity("Microsoft.Xna.Framework.Content.VertexBufferReader",
                             "Microsoft.Xna.Framework.Graphics.VertexBuffer",
                             XnbNameEvidence::MonoGameFixture),
            true,
            [](XnbWriter& output, const XnbVertexBufferData& value)
            {
                // The declaration is written inline, but WriteRawObject still interns
                // VertexDeclarationReader: that is exactly why a real Model .xnb lists a reader it
                // never dispatches to.
                output.WriteRawObject(value.declaration);
                const std::uint64_t expected = static_cast<std::uint64_t>(value.vertexCount) *
                                               static_cast<std::uint64_t>(value.declaration.stride);
                if (expected != value.bytes.size())
                {
                    throw XnbWriteException(
                        "'" + output.AssetName() + "': VertexBufferWriter was given " +
                        std::to_string(value.bytes.size()) + " bytes but " +
                        std::to_string(value.vertexCount) + " vertices of stride " +
                        std::to_string(value.declaration.stride) + " need " +
                        std::to_string(expected) + ".");
                }
                output.WriteUInt32(value.vertexCount);
                output.WriteBytes(value.bytes);
            });

        AddWriter<XnbIndexBufferData>(registry,
            GraphicsIdentity("Microsoft.Xna.Framework.Content.IndexBufferReader",
                             "Microsoft.Xna.Framework.Graphics.IndexBuffer",
                             XnbNameEvidence::MonoGameFixture),
            true,
            [](XnbWriter& output, const XnbIndexBufferData& value)
            {
                if (value.indexElementSize != 2u && value.indexElementSize != 4u)
                {
                    throw XnbWriteException(
                        "'" + output.AssetName() +
                        "': IndexBufferWriter supports 16-bit and 32-bit indices only.");
                }
                if (value.bytes.size() % value.indexElementSize != 0u)
                {
                    throw XnbWriteException(
                        "'" + output.AssetName() + "': IndexBufferWriter was given " +
                        std::to_string(value.bytes.size()) +
                        " bytes, which is not a whole number of " +
                        std::to_string(value.indexElementSize) + "-byte indices.");
                }
                output.WriteBoolean(value.indexElementSize == 2u);
                output.WriteLengthPrefixedBytes(value.bytes);
            });

        AddWriter<XnbBasicEffectData>(registry,
            GraphicsIdentity("Microsoft.Xna.Framework.Content.BasicEffectReader",
                             "Microsoft.Xna.Framework.Graphics.BasicEffect",
                             XnbNameEvidence::MonoGameFixture),
            true,
            [](XnbWriter& output, const XnbBasicEffectData& value)
            {
                output.WriteExternalReference(value.textureReference);
                output.WriteVector3(value.diffuseColor);
                output.WriteVector3(value.emissiveColor);
                output.WriteVector3(value.specularColor);
                output.WriteSingle(value.specularPower);
                output.WriteSingle(value.alpha);
                output.WriteBoolean(value.vertexColorEnabled);
            });

        AddWriter<XnbAlphaTestEffectData>(registry,
            GraphicsIdentity("Microsoft.Xna.Framework.Content.AlphaTestEffectReader",
                             "Microsoft.Xna.Framework.Graphics.AlphaTestEffect",
                             XnbNameEvidence::DerivedRule),
            true,
            [](XnbWriter& output, const XnbAlphaTestEffectData& value)
            {
                output.WriteExternalReference(value.textureReference);
                output.WriteInt32(value.alphaFunction);
                output.WriteUInt32(value.referenceAlpha);
                output.WriteVector3(value.diffuseColor);
                output.WriteSingle(value.alpha);
                output.WriteBoolean(value.vertexColorEnabled);
            });

        AddWriter<XnbDualTextureEffectData>(registry,
            GraphicsIdentity("Microsoft.Xna.Framework.Content.DualTextureEffectReader",
                             "Microsoft.Xna.Framework.Graphics.DualTextureEffect",
                             XnbNameEvidence::DerivedRule),
            true,
            [](XnbWriter& output, const XnbDualTextureEffectData& value)
            {
                output.WriteExternalReference(value.textureReference);
                output.WriteExternalReference(value.texture2Reference);
                output.WriteVector3(value.diffuseColor);
                output.WriteSingle(value.alpha);
                output.WriteBoolean(value.vertexColorEnabled);
            });

        AddWriter<XnbEnvironmentMapEffectData>(registry,
            GraphicsIdentity("Microsoft.Xna.Framework.Content.EnvironmentMapEffectReader",
                             "Microsoft.Xna.Framework.Graphics.EnvironmentMapEffect",
                             XnbNameEvidence::DerivedRule),
            true,
            [](XnbWriter& output, const XnbEnvironmentMapEffectData& value)
            {
                output.WriteExternalReference(value.textureReference);
                output.WriteExternalReference(value.environmentMapReference);
                output.WriteSingle(value.environmentMapAmount);
                output.WriteVector3(value.environmentMapSpecular);
                output.WriteSingle(value.fresnelFactor);
                output.WriteVector3(value.diffuseColor);
                output.WriteVector3(value.emissiveColor);
                output.WriteSingle(value.alpha);
            });

        AddWriter<XnbSkinnedEffectData>(registry,
            GraphicsIdentity("Microsoft.Xna.Framework.Content.SkinnedEffectReader",
                             "Microsoft.Xna.Framework.Graphics.SkinnedEffect",
                             XnbNameEvidence::DerivedRule),
            true,
            [](XnbWriter& output, const XnbSkinnedEffectData& value)
            {
                output.WriteExternalReference(value.textureReference);
                output.WriteInt32(value.weightsPerVertex);
                output.WriteVector3(value.diffuseColor);
                output.WriteVector3(value.emissiveColor);
                output.WriteVector3(value.specularColor);
                output.WriteSingle(value.specularPower);
                output.WriteSingle(value.alpha);
            });

        AddWriter<XnbCompiledEffectContent>(registry,
            GraphicsIdentity("Microsoft.Xna.Framework.Content.EffectReader",
                             "Microsoft.Xna.Framework.Graphics.Effect",
                             XnbNameEvidence::DerivedRule),
            true,
            [](XnbWriter& output, const XnbCompiledEffectContent& value)
            {
                if (value.bytecode.empty())
                {
                    throw XnbWriteException(
                        "'" + output.AssetName() +
                        "': EffectWriter refuses an empty bytecode payload; an Effect asset with "
                        "no compiled program cannot be loaded.");
                }
                output.WriteLengthPrefixedBytes(value.bytecode);
            });

        // XNAP-2B: an external reference stored where the static type is `object`. The reader
        // side is ExternalReferenceReader, whose target type is System.Object, so this entry has
        // to be interned under that name for the dictionary below to resolve it.
        AddWriter<XnbExternalAssetReference>(registry,
            CoreIdentity("Microsoft.Xna.Framework.Content.ExternalReferenceReader",
                         "System.Object", XnbAssembly::Mscorlib, XnbNameEvidence::DerivedRule),
            true,
            [](XnbWriter& output, const XnbExternalAssetReference& value)
            {
                output.WriteExternalReference(value.reference);
            });

        // XNAP-29: Dictionary<String, Object>. Its values are polymorphic -- each carries its own
        // dispatch index -- which is exactly what the homogeneous dictionary writer cannot do.
        AddWriter<XnbEffectParameterTable>(registry,
            [] {
                XnbReaderIdentity key;
                key.targetBaseName = "System.String";
                key.targetAssembly = XnbAssembly::Mscorlib;
                key.evidence = XnbNameEvidence::Xna40Fixture;
                XnbReaderIdentity value;
                value.targetBaseName = "System.Object";
                value.targetAssembly = XnbAssembly::Mscorlib;
                value.evidence = XnbNameEvidence::DerivedRule;
                XnbReaderIdentity identity;
                identity.readerBaseName = "Microsoft.Xna.Framework.Content.DictionaryReader`2";
                identity.readerAssembly = XnbAssembly::None;
                identity.targetBaseName = "System.Collections.Generic.Dictionary`2";
                identity.targetAssembly = XnbAssembly::Mscorlib;
                identity.genericArguments = {key, value};
                identity.evidence = XnbNameEvidence::DerivedRule;
                return identity;
            }(),
            true,
            [](XnbWriter& output, const XnbEffectParameterTable& value)
            {
                output.RequireCollectionCount(value.values.size(), "EffectParameterTableWriter");
                output.WriteInt32(static_cast<std::int32_t>(value.values.size()));
                // std::map already orders by key, which is what makes this deterministic.
                for (const auto& [name, parameter] : value.values)
                {
                    output.WriteObject(name);
                    std::visit([&output](const auto& stored) { output.WriteObject(stored); },
                               parameter);
                }
            });

        AddWriter<XnbEffectMaterialData>(registry,
            GraphicsIdentity("Microsoft.Xna.Framework.Content.EffectMaterialReader",
                             "Microsoft.Xna.Framework.Graphics.EffectMaterial",
                             XnbNameEvidence::DerivedRule),
            true,
            [](XnbWriter& output, const XnbEffectMaterialData& value)
            {
                if (value.effectReference.empty())
                {
                    throw XnbWriteException(
                        "'" + output.AssetName() +
                        "': EffectMaterialWriter needs the effect asset the material clones; a "
                        "material with no effect reference cannot be loaded.");
                }
                // The effect reference sits inline in a field whose type the reader already
                // knows, so it carries no dispatch index. The parameter table does.
                output.WriteExternalReference(value.effectReference);
                output.WriteObject(value.parameters);
            });

        AddWriter<XnbModelData>(registry,
            GraphicsIdentity("Microsoft.Xna.Framework.Content.ModelReader",
                             "Microsoft.Xna.Framework.Graphics.Model",
                             XnbNameEvidence::MonoGameFixture),
            true,
            [](XnbWriter& output, const XnbModelData& value)
            {
                WriteModelGraph(output, value);
            });
    }
}

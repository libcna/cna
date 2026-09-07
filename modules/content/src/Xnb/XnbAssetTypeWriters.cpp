// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/XnbAssetTypeWriters.hpp"

#include <algorithm>

#include <algorithm>
#include <variant>

#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"

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
        /**
         * @brief The exact byte count one mip level of a texture occupies.
         *
         * Block-compressed formats round each dimension up to a whole 4x4 block, which is why a
         * 1x1 DXT5 level still costs sixteen bytes.
         */
        [[nodiscard]] std::uint64_t TextureLevelByteSize(const SurfaceFormat format,
                                                         const std::uint32_t width,
                                                         const std::uint32_t height,
                                                         const std::uint32_t depth)
        {
            const auto unit = static_cast<std::uint64_t>(
                Microsoft::Xna::Framework::Graphics::Texture::GetFormatSizeEXT(format));
            const bool blockCompressed =
                Microsoft::Xna::Framework::Graphics::Texture::GetBlockSizeSquaredEXT(format) > 1;
            if (blockCompressed)
            {
                const std::uint64_t blocksX = (static_cast<std::uint64_t>(width) + 3u) / 4u;
                const std::uint64_t blocksY = (static_cast<std::uint64_t>(height) + 3u) / 4u;
                return blocksX * blocksY * depth * unit;
            }
            return static_cast<std::uint64_t>(width) * height * depth * unit;
        }

        /**
         * @brief The width, in bytes, of the unit the Xbox 360 swaps a texture payload by.
         *
         * The Xbox 360 is big-endian and its texture memory is byte-swapped per component, not per
         * texel and not per level: a 32-bit `Color` texel arrives with its four bytes reversed and
         * a DXT block with each of its 16-bit words reversed. Both are measured against the
         * genuine pipeline -- `xbox/png_texture` reverses `00FF00FF` to `FF00FF00` while leaving
         * the texel order alone, and `xbox/png_texture_dxt` turns `0aca ff79 fdfe` into
         * `ca0a 79ff fefd` (plans/plan_xnapipeline_parity.md XNAPP-252).
         *
         * A format whose components are single bytes needs no swap and answers 1. A format this
         * build has not measured or derived answers 0, and the caller then refuses the platform
         * rather than writing a payload it cannot vouch for.
         *
         * @param format The surface format.
         * @return 1, 2 or 4 for a format that can be converted; 0 for one that cannot.
         */
        [[nodiscard]] std::uint32_t XboxByteSwapUnit(const SurfaceFormat format)
        {
            namespace XnaGraphics = Microsoft::Xna::Framework::Graphics;
            switch (format)
            {
            // Single-byte components: nothing to reverse.
            case SurfaceFormat::Alpha8:
                return 1u;
            // Four bytes per texel, reversed as one unit.
            case SurfaceFormat::Color:
            case SurfaceFormat::Rgba1010102:
            case SurfaceFormat::Rg32:
            case SurfaceFormat::NormalizedByte4:
            case SurfaceFormat::Single:
                return 4u;
            // Two bytes per component or per texel.
            case SurfaceFormat::Bgr565:
            case SurfaceFormat::Bgra5551:
            case SurfaceFormat::Bgra4444:
            case SurfaceFormat::NormalizedByte2:
            case SurfaceFormat::HalfSingle:
            case SurfaceFormat::HalfVector2:
            case SurfaceFormat::HalfVector4:
            case SurfaceFormat::Rgba64:
            case SurfaceFormat::Vector2:
            case SurfaceFormat::Vector4:
            // A block-compressed level is a stream of 16-bit words, and that is the unit.
            case SurfaceFormat::Dxt1:
            case SurfaceFormat::Dxt3:
            case SurfaceFormat::Dxt5:
                return 2u;
            default:
                return 0u;
            }
        }

        /**
         * @brief Reverses each swap unit of one texture level in place.
         *
         * @param level The level's bytes.
         * @param unit 1, 2 or 4; 1 leaves the level untouched.
         */
        void SwapTextureLevelForXbox(std::vector<std::uint8_t>& level, const std::uint32_t unit)
        {
            if (unit < 2u) { return; }
            for (std::size_t at = 0u; at + unit <= level.size(); at += unit)
            {
                std::reverse(level.begin() + static_cast<std::ptrdiff_t>(at),
                             level.begin() + static_cast<std::ptrdiff_t>(at + unit));
            }
        }

        /**
         * @brief The component width one vertex element is byte-swapped by on the Xbox 360.
         *
         * The same rule as a texture payload: the console reads its buffers big-endian, and the
         * unit is the element's own component, not the element. A `Vector3` is three four-byte
         * reversals, a `Color` one, a `Byte4` none, a `Short2` two of two bytes. Measured on the
         * genuine pipeline, which turns the eight `1.0f` and eight `-1.0f` of a model's vertex
         * buffer into their big-endian spellings and leaves the nine floats the container itself
         * writes alone (plans/plan_xnapipeline_parity.md XNAPP-252).
         *
         * @param format The declared element format.
         * @param componentBytes Receives the width; 1 means the element needs no swap.
         * @return false for a format this build has not derived a rule for.
         */
        [[nodiscard]] bool XboxVertexElementSwap(
            const Microsoft::Xna::Framework::Graphics::VertexElementFormat format,
            std::uint32_t& componentBytes, std::size_t& elementBytes)
        {
            using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
            switch (format)
            {
            case VertexElementFormat::Single: componentBytes = 4u; elementBytes = 4u; return true;
            case VertexElementFormat::Vector2: componentBytes = 4u; elementBytes = 8u; return true;
            case VertexElementFormat::Vector3: componentBytes = 4u; elementBytes = 12u; return true;
            case VertexElementFormat::Vector4: componentBytes = 4u; elementBytes = 16u; return true;
            case VertexElementFormat::Color: componentBytes = 4u; elementBytes = 4u; return true;
            case VertexElementFormat::Short2:
            case VertexElementFormat::NormalizedShort2:
            case VertexElementFormat::HalfVector2:
                componentBytes = 2u; elementBytes = 4u; return true;
            case VertexElementFormat::Short4:
            case VertexElementFormat::NormalizedShort4:
            case VertexElementFormat::HalfVector4:
                componentBytes = 2u; elementBytes = 8u; return true;
            case VertexElementFormat::Byte4:
                // Four separate bytes: the console reads them in the order they are written.
                componentBytes = 1u; elementBytes = 4u; return true;
            default:
                return false;
            }
        }

        /**
         * @brief Reverses every component of every vertex in place, for the Xbox 360.
         *
         * @param bytes The whole buffer.
         * @param declaration The declaration that describes it.
         * @return false when an element's format has no derived rule, leaving @p bytes untouched.
         */
        [[nodiscard]] bool SwapVertexBufferForXbox(std::vector<std::uint8_t>& bytes,
                                                   const XnbVertexDeclarationData& declaration)
        {
            const auto stride = static_cast<std::size_t>(declaration.stride);
            if (stride == 0u) { return false; }
            struct Span
            {
                std::size_t offset;
                std::size_t length;
                std::uint32_t unit;
            };
            std::vector<Span> spans;
            for (const Microsoft::Xna::Framework::Graphics::VertexElement& element :
                 declaration.elements)
            {
                std::uint32_t unit = 1u;
                std::size_t length = 0u;
                if (!XboxVertexElementSwap(element.getVertexElementFormatProperty(), unit, length))
                {
                    return false;
                }
                const auto offset = static_cast<std::size_t>(element.getOffsetProperty());
                if (offset + length > stride) { return false; }
                if (unit > 1u) { spans.push_back({offset, length, unit}); }
            }
            for (std::size_t base = 0u; base + stride <= bytes.size(); base += stride)
            {
                for (const Span& span : spans)
                {
                    for (std::size_t at = 0u; at + span.unit <= span.length; at += span.unit)
                    {
                        const auto from =
                            static_cast<std::ptrdiff_t>(base + span.offset + at);
                        std::reverse(bytes.begin() + from,
                                     bytes.begin() + from + static_cast<std::ptrdiff_t>(span.unit));
                    }
                }
            }
            return true;
        }

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
            for (std::size_t index = 0u; index < texture.levels.size(); ++index)
            {
                // plans/plan_xnapipeline.md XNAP-85: a level whose byte count disagrees with the
                // dimensions and format the same file declares is refused here rather than
                // written. The reader would refuse it later, but by then the producer that still
                // held the mismatched data is long gone and the message can only name a file.
                const std::uint32_t mip =
                    static_cast<std::uint32_t>(index % texture.mipCount);
                const std::uint32_t width = std::max(1u, texture.width >> mip);
                const std::uint32_t height = std::max(1u, texture.height >> mip);
                const std::uint32_t depth = std::max(1u, texture.depth >> mip);
                const std::uint64_t needed =
                    TextureLevelByteSize(texture.surfaceFormat, width, height, depth);
                if (texture.levels[index].size() != needed)
                {
                    throw XnbWriteException(
                        std::string("'") + output.AssetName() + "': " + readerName +
                        " level " + std::to_string(index) + " carries " +
                        std::to_string(texture.levels[index].size()) + " bytes, but " +
                        std::to_string(width) + "x" + std::to_string(height) + "x" +
                        std::to_string(depth) + " SurfaceFormat " +
                        std::to_string(static_cast<int>(texture.surfaceFormat)) + " needs " +
                        std::to_string(needed) + ".");
                }
                // The Xbox 360 reads texture memory big-endian, so the payload is reversed by
                // the format's own component width on the way out. Every other field in the file
                // stays little-endian: the container is little-endian on every platform and only
                // the raw payloads are swapped.
                const std::uint32_t unit = output.IsXboxTarget()
                                               ? XboxByteSwapUnit(texture.surfaceFormat)
                                               : 1u;
                if (unit > 1u)
                {
                    std::vector<std::uint8_t> swapped = texture.levels[index];
                    SwapTextureLevelForXbox(swapped, unit);
                    output.WriteLengthPrefixedBytes(swapped);
                }
                else
                {
                    output.WriteLengthPrefixedBytes(texture.levels[index]);
                }
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
            for (const auto& element : declaration.elements)
            {
                // XNA's own VertexDeclaration constructor rejects this, so writing it produces a
                // file that throws on load, and a GPU that accepted it would read past the
                // vertex. Found by the writer-input fuzz corpus
                // (plans/plan_xnapipeline.md XNAP-45).
                const std::int64_t end =
                    static_cast<std::int64_t>(element.getOffsetProperty()) +
                    CNA::Internal::Graphics::VertexDeclarationFidelityDetail::FormatSize(
                        element.getVertexElementFormatProperty());
                if (element.getOffsetProperty() < 0 || end > declaration.stride)
                {
                    throw XnbWriteException(
                        "'" + output.AssetName() +
                        "': VertexDeclarationWriter element at offset " +
                        std::to_string(element.getOffsetProperty()) + " ends at " +
                        std::to_string(end) + ", past the " + std::to_string(declaration.stride) +
                        "-byte vertex stride.");
                }
            }
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

        /** @brief Renders a code point as `U+XXXX`, so U+003F is not read as decimal 63. */
        [[nodiscard]] std::string CodePoint(const SharpRuntime::charcs character)
        {
            static const char* digits = "0123456789ABCDEF";
            const auto value = static_cast<std::uint32_t>(character);
            std::string text = "U+";
            for (int shift = 12; shift >= 0; shift -= 4)
            {
                text.push_back(digits[(value >> shift) & 0xFu]);
            }
            return text;
        }

        /**
         * @brief Refuses a bone hierarchy that contains a cycle
         *        (plans/plan_xnapipeline.md `XNAP-45`).
         *
         * Every bone reference is individually in range, so nothing below notices that following
         * `parent` never reaches a root. An XNA `Model` walks that chain to compute absolute bone
         * transforms; a cycle there is not a bad value, it is a program that does not stop. Found
         * by the writer-input fuzz corpus, which wrote a self-parented bone without complaint.
         *
         * @param output The file being written, for the asset name in the diagnostic.
         * @param model The canonical Model graph.
         * @throws XnbWriteException naming the bone the cycle was found at.
         */
        void RequireAcyclicBoneHierarchy(XnbWriter& output, const XnbModelData& model)
        {
            const std::size_t boneCount = model.bones.size();
            for (std::size_t start = 0u; start < boneCount; ++start)
            {
                std::size_t current = start;
                for (std::size_t steps = 0u; steps <= boneCount; ++steps)
                {
                    const std::int32_t parent = model.bones[current].parent;
                    if (parent < 0 || static_cast<std::size_t>(parent) >= boneCount) { break; }
                    current = static_cast<std::size_t>(parent);
                    if (steps == boneCount)
                    {
                        throw XnbWriteException(
                            "'" + output.AssetName() + "': ModelWriter bone " +
                            std::to_string(start) + " ('" + model.bones[start].name +
                            "') is its own ancestor. An XNA Model walks the parent chain to "
                            "compute absolute bone transforms, so a cycle there does not "
                            "terminate.");
                    }
                }
            }
        }

        /**
         * @brief Refuses a mesh part whose ranges fall outside the buffers it names
         *        (plans/plan_xnapipeline.md `XNAP-45`).
         *
         * `DrawIndexedPrimitives` reads `primitiveCount * 3` indices from `startIndex`, and each
         * index addresses a vertex at `vertexOffset`. XNA validates none of it at load: the part
         * is four integers, and the first draw call reads past the buffer. The writer has both
         * the part and the buffers in hand, so it is the last place this can be caught before it
         * becomes a runtime read out of range.
         *
         * @param output The file being written, for the asset name in the diagnostic.
         * @param model The canonical Model graph.
         * @throws XnbWriteException naming the part and the buffer it overruns.
         */
        void RequireMeshPartsWithinTheirBuffers(XnbWriter& output, const XnbModelData& model)
        {
            const auto resource = [&model](const std::int32_t index)
                -> const XnbModelSharedResourceData*
            {
                if (index < 0 || static_cast<std::size_t>(index) >= model.sharedResources.size())
                {
                    return nullptr;
                }
                return &model.sharedResources[static_cast<std::size_t>(index)];
            };

            for (std::size_t meshIndex = 0u; meshIndex < model.meshes.size(); ++meshIndex)
            {
                const XnbModelMeshData& mesh = model.meshes[meshIndex];
                for (std::size_t partIndex = 0u; partIndex < mesh.parts.size(); ++partIndex)
                {
                    const XnbModelPartData& part = mesh.parts[partIndex];
                    const std::string where =
                        "'" + output.AssetName() + "': ModelWriter mesh " +
                        std::to_string(meshIndex) + " ('" + mesh.name + "') part " +
                        std::to_string(partIndex);
                    if (part.vertexOffset < 0 || part.vertexCount < 0 || part.startIndex < 0 ||
                        part.primitiveCount < 0)
                    {
                        throw XnbWriteException(
                            where + " has a negative vertex offset, vertex count, start index or "
                                    "primitive count.");
                    }

                    if (const XnbModelSharedResourceData* const vertices =
                            resource(part.vertexBufferResource))
                    {
                        if (const auto* buffer =
                                std::get_if<XnbVertexBufferData>(&vertices->value))
                        {
                            const std::uint64_t last =
                                static_cast<std::uint64_t>(part.vertexOffset) +
                                static_cast<std::uint64_t>(part.vertexCount);
                            if (last > buffer->vertexCount)
                            {
                                throw XnbWriteException(
                                    where + " covers vertices " +
                                    std::to_string(part.vertexOffset) + " to " +
                                    std::to_string(last) + " of a " +
                                    std::to_string(buffer->vertexCount) + "-vertex buffer.");
                            }
                        }
                    }

                    const XnbModelSharedResourceData* const indices =
                        resource(part.indexBufferResource);
                    if (indices == nullptr) { continue; }
                    const auto* buffer = std::get_if<XnbIndexBufferData>(&indices->value);
                    if (buffer == nullptr) { continue; }
                    const std::uint32_t elementSize =
                        buffer->indexElementSize == 0u ? 2u : buffer->indexElementSize;
                    const std::uint64_t indexCount = buffer->bytes.size() / elementSize;
                    const std::uint64_t last =
                        static_cast<std::uint64_t>(part.startIndex) +
                        static_cast<std::uint64_t>(part.primitiveCount) * 3u;
                    if (last > indexCount)
                    {
                        throw XnbWriteException(
                            where + " draws " + std::to_string(part.primitiveCount) +
                            " triangles from index " + std::to_string(part.startIndex) +
                            ", reaching index " + std::to_string(last) + " of a " +
                            std::to_string(indexCount) +
                            "-index buffer. DrawIndexedPrimitives would read past it.");
                    }
                }
            }
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
            RequireAcyclicBoneHierarchy(output, model);
            RequireMeshPartsWithinTheirBuffers(output, model);

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
                // A bone with no name is written as a null object, not as an empty string. XNA
                // does that for a source node that carries no name -- an unnamed root frame is the
                // ordinary case in a `.x` file -- and an empty string is a different value: it
                // interns a StringReader and a zero-length string where XNA writes one byte
                // (measured, tests/reference/xna40/differential/model_x_bare_mesh.xnb, whose bone 0
                // is null while every bone of model_x_hierarchy.xnb is named;
                // plans/plan_xnapipeline_parity.md XNAPP-266).
                if (bone.name.empty()) { output.WriteNullObject(); }
                else { output.WriteObject(bone.name); }
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
                // No platform guard: this writer converts its payload for the Xbox 360, and the
                // conversion is measured against the genuine pipeline rather than assumed
                // (plans/plan_xnapipeline_parity.md XNAPP-252). A format whose swap unit is
                // unknown is still refused, below, where the unit is asked for.
                if (output.IsXboxTarget() && XboxByteSwapUnit(texture.surfaceFormat) == 0u)
                {
                    throw XnbWriteException(
                        "'" + output.AssetName() +
                        "': Texture2DWriter has no measured Xbox 360 byte order for surface "
                        "format " +
                        std::to_string(static_cast<int>(texture.surfaceFormat)) +
                        ", and the Xbox 360 is big-endian, so writing the 'x' platform byte over "
                        "this payload would claim a compatibility this build cannot deliver.");
                }
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
                // Converted rather than refused, and proven: `xbox/dds_cube` and
                // `xbox/dds_volume` are byte-identical to the genuine pipeline's output
                // now that a route carries a cube and a volume DDS this far
                // (plans/plan_xnapipeline_parity.md XNAPP-252, XNAPP-255). A surface
                // format with no measured swap unit is still refused.
                if (output.IsXboxTarget() && XboxByteSwapUnit(texture.surfaceFormat) == 0u)
                {
                    throw XnbWriteException(
                        "'" + output.AssetName() +
                        "': Texture3DWriter has no measured Xbox 360 byte order for surface format " +
                        std::to_string(static_cast<int>(texture.surfaceFormat)) +
                        ", and the Xbox 360 is big-endian, so writing the 'x' platform "
                        "byte over this payload would claim a compatibility this build "
                        "cannot deliver.");
                }
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
                // Converted rather than refused, and proven: `xbox/dds_cube` and
                // `xbox/dds_volume` are byte-identical to the genuine pipeline's output
                // now that a route carries a cube and a volume DDS this far
                // (plans/plan_xnapipeline_parity.md XNAPP-252, XNAPP-255). A surface
                // format with no measured swap unit is still refused.
                if (output.IsXboxTarget() && XboxByteSwapUnit(texture.surfaceFormat) == 0u)
                {
                    throw XnbWriteException(
                        "'" + output.AssetName() +
                        "': TextureCubeWriter has no measured Xbox 360 byte order for surface format " +
                        std::to_string(static_cast<int>(texture.surfaceFormat)) +
                        ", and the Xbox 360 is big-endian, so writing the 'x' platform "
                        "byte over this payload would claim a compatibility this build "
                        "cannot deliver.");
                }
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
                // Converted rather than refused. The atlas is a texture and goes through
                // WriteTextureLevels(), which swaps it; everything else a SpriteFont holds --
                // the glyph, cropping and kerning lists -- is written through the container's
                // own object protocol, which stays little-endian on every platform. Both
                // halves are measured: `xbox/font_description`'s kerning triples read
                // little-endian and equal CNA's, and the DXT swap unit comes from XNA's own
                // two builds of the same texture (plans/plan_xnapipeline_parity.md XNAPP-252).
                if (output.IsXboxTarget() &&
                    XboxByteSwapUnit(value.atlas.surfaceFormat) == 0u)
                {
                    throw XnbWriteException(
                        "'" + output.AssetName() +
                        "': SpriteFontWriter has no measured Xbox 360 byte order for its "
                        "atlas's surface format " +
                        std::to_string(static_cast<int>(value.atlas.surfaceFormat)) + ".");
                }
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
                // XNA's SpriteFont binary-searches its character table, so an unsorted one does
                // not fail: it silently returns the wrong glyph, or none. And CNA's own
                // SpriteFont rejects a default character the font cannot render
                // (REMED-GFX-002), so writing one produces an .xnb its own runtime refuses.
                // Both are cheap here and impossible to diagnose from rendered output
                // (plans/plan_xnapipeline.md XNAP-45).
                for (std::size_t index = 1u; index < value.characters.size(); ++index)
                {
                    if (value.characters[index - 1u] < value.characters[index]) { continue; }
                    throw XnbWriteException(
                        "'" + output.AssetName() + "': SpriteFontWriter character table is not "
                        "strictly ascending at index " + std::to_string(index) + " (" +
                        CodePoint(value.characters[index - 1u]) + " then " +
                        CodePoint(value.characters[index]) +
                        "). SpriteFont binary-searches this table, so an unsorted one returns "
                        "the wrong glyph rather than failing.");
                }
                if (value.defaultCharacter.has_value() &&
                    std::find(value.characters.begin(), value.characters.end(),
                              *value.defaultCharacter) == value.characters.end())
                {
                    throw XnbWriteException(
                        "'" + output.AssetName() + "': SpriteFontWriter default character " +
                        CodePoint(*value.defaultCharacter) +
                        " is not in the font's character table, which SpriteFont's own "
                        "constructor rejects.");
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
                output.RequireVerifiedPlatformPayload("SoundEffectWriter", true);
                const bool swap = output.Platform() == 'x';
                // Always the full 18-byte WAVEFORMATEX, with cbSize present and zero when there
                // is no extension -- not the 16-byte PCMWAVEFORMAT that omits cbSize entirely.
                // CNA's reader accepts both, and both occur in the wild, but the only real
                // producer whose output is committed here writes 18, and matching an observed
                // producer beats matching none (plans/plan_xnapipeline.md XNAP-42, which is a
                // byte-exact golden test against exactly that file).
                const std::size_t formatLength = 18u + value.extensionData.size();
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
                output.WriteUInt16(
                    Swap16(swap, static_cast<std::uint16_t>(value.extensionData.size())));
                if (!value.extensionData.empty()) { output.WriteBytes(value.extensionData); }
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
                output.RequireVerifiedPlatformPayload("VertexDeclarationWriter");
                WriteVertexDeclarationPayload(output, value);
            });

        AddWriter<XnbVertexBufferData>(registry,
            GraphicsIdentity("Microsoft.Xna.Framework.Content.VertexBufferReader",
                             "Microsoft.Xna.Framework.Graphics.VertexBuffer",
                             XnbNameEvidence::MonoGameFixture),
            true,
            [](XnbWriter& output, const XnbVertexBufferData& value)
            {
                // No platform guard: this writer converts its own payload for the Xbox 360, and
                // refuses only a declaration whose element formats it has no measured rule for.
                std::vector<std::uint8_t> vertexBytes = value.bytes;
                if (output.IsXboxTarget() && !SwapVertexBufferForXbox(vertexBytes, value.declaration))
                {
                    throw XnbWriteException(
                        "'" + output.AssetName() +
                        "': VertexBufferWriter has no measured Xbox 360 byte order for one of "
                        "this declaration's element formats, and the Xbox 360 is big-endian, so "
                        "writing the 'x' platform byte over this buffer would claim a "
                        "compatibility this build cannot deliver.");
                }
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
                output.WriteBytes(vertexBytes);
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
                // Indices are whole integers, so the swap unit is the index itself.
                if (output.IsXboxTarget())
                {
                    std::vector<std::uint8_t> swapped = value.bytes;
                    SwapTextureLevelForXbox(swapped, value.indexElementSize);
                    output.WriteLengthPrefixedBytes(swapped);
                }
                else
                {
                    output.WriteLengthPrefixedBytes(value.bytes);
                }
            });

        AddWriter<XnbBasicEffectData>(registry,
            GraphicsIdentity("Microsoft.Xna.Framework.Content.BasicEffectReader",
                             "Microsoft.Xna.Framework.Graphics.BasicEffect",
                             XnbNameEvidence::MonoGameFixture),
            true,
            [](XnbWriter& output, const XnbBasicEffectData& value)
            {
                output.RequireVerifiedPlatformPayload("BasicEffectWriter");
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
                output.RequireVerifiedPlatformPayload("EffectWriter");
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
                output.RequireVerifiedPlatformPayload("ModelWriter");
                WriteModelGraph(output, value);
            });
    }
}

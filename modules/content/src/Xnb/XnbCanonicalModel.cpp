// SPDX-License-Identifier: MS-PL
// The canonical .xnb model decoders: vertex declarations and buffers, index buffers, the stock
// effects and the model graph's conversion to .cnb.
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
#include <array>

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Content::ContentLoadException;
    using Microsoft::Xna::Framework::Content::ContentReader;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

    namespace
    {
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
            result.bones.push_back(
                {input.name, input.nameIsNull, input.parent, MatrixValues(input.transform)});
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
}

// SPDX-License-Identifier: MS-PL
// DecodeModelFromCnb(): the reading half of the .cnb model codec (schema 1).
//
// Link boundary: apart from the writer (CnbModelCodec.cpp), so that loading a .cnb model does not
// link CnbWriter (docs/link-size-architecture.md).
#include "CNA/Content/Cnb/CnbModelCodec.hpp"
#include "CnbCodecDetail.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <span>
#include <utility>

#include "CNA/Content/Cnb/CnbAnimationClipCodec.hpp"
#include "CNA/Content/Cnb/CnbArithmetic.hpp"
#include "CNA/Content/Cnb/CnbByteReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Graphics::AnimationClipEXT;
using Microsoft::Xna::Framework::Graphics::BoneTrackEXT;
using Microsoft::Xna::Framework::Graphics::ClipTargetSpaceEXT;
using Microsoft::Xna::Framework::Graphics::KeyframeEXT;

namespace CNA::Content::Cnb
{
    namespace ModelCodecDetail
    {
        /// The primitive count a topology and index count imply. Duplicated here rather than
        /// called through CNA::Internal::GltfImport::PrimitiveCountForTopology so that the codec
        /// stays free of the glTF importer -- and, more to the point, so the cross-check below is
        /// an INDEPENDENT statement of the rule rather than a tautology against the same function
        /// the compiler used to produce the value.
        [[nodiscard]] std::uint64_t DerivePrimitiveCount(std::uint32_t topology,
                                                          std::uint64_t indexCount)
        {
            switch (topology)
            {
                case 0: return indexCount;                              // points
                case 1: return indexCount / 2u;                         // lines
                case 2:                                                  // line loop
                case 3: return indexCount > 0u ? indexCount - 1u : 0u;  // line strip
                case 4: return indexCount / 3u;                         // triangles
                case 5:                                                  // triangle strip
                case 6: return indexCount > 2u ? indexCount - 2u : 0u;  // triangle fan
                default: return 0u;
            }
        }
    }

    namespace
    {
        using namespace ModelCodecDetail;

        std::array<float, 16> ReadMatrix(CnbByteReader& reader)
        {
            std::array<float, 16> matrix{};
            for (float& value : matrix) { value = reader.ReadF32(); }
            return matrix;
        }
        /// Resolves a decoded XREF index back to a logical asset name, or "" for CnbNoIndex.
        std::string ResolveReference(const CnbDocument& document, std::uint32_t index,
                                     const char* what)
        {
            if (index == CnbNoIndex) { return {}; }
            return document.ExternalReferenceAt(index, what).logicalName;
        }
        CnbMaterial ReadMaterial(CnbByteReader& reader, const CnbDocument& document,
                                  std::size_t materialIndex)
        {
            CnbMaterial material;
            const std::array<std::string*, 8> textures{
                &material.baseColorTexture, &material.texture2, &material.normalMap,
                &material.metallicRoughnessMap, &material.emissiveMap, &material.occlusionMap,
                &material.specularMap, &material.specularColorMap};
            const std::string what = "material " + std::to_string(materialIndex) + "'s texture";
            for (std::string* texture : textures)
            {
                *texture = ResolveReference(document, reader.ReadU32(), what.c_str());
            }

            for (float& value : material.baseColorFactor) { value = reader.ReadF32(); }
            for (float& value : material.emissiveFactor) { value = reader.ReadF32(); }
            for (float& value : material.specularColorFactor) { value = reader.ReadF32(); }
            material.metallicFactor = reader.ReadF32();
            material.roughnessFactor = reader.ReadF32();
            material.ior = reader.ReadF32();
            material.specularFactor = reader.ReadF32();
            material.normalScale = reader.ReadF32();
            material.occlusionStrength = reader.ReadF32();
            material.alphaCutoff = reader.ReadF32();
            material.alphaMode = reader.ReadU32();
            // AlphaModeEXT has exactly three enumerators; casting an out-of-range value into it
            // would be undefined behaviour the moment anything switched on the result.
            if (material.alphaMode > 2u)
            {
                reader.Fail("material " + std::to_string(materialIndex) + " has alphaMode " +
                            std::to_string(material.alphaMode) +
                            ", which is not an AlphaModeEXT value (0-2).");
            }
            const std::uint32_t flags = reader.ReadU32();
            if ((flags & ~kMaterialFlagDoubleSided) != 0u)
            {
                reader.Fail("material " + std::to_string(materialIndex) +
                            " sets material flags this build does not define.");
            }
            material.doubleSided = (flags & kMaterialFlagDoubleSided) != 0u;

            for (std::uint8_t& set : material.textureCoordinateSets)
            {
                set = reader.ReadU8();
                if (set > 1u)
                {
                    reader.Fail("material " + std::to_string(materialIndex) +
                                " names texture-coordinate set " + std::to_string(set) +
                                "; CNA's vertex layouts carry at most two (0 or 1).");
                }
            }
            if (reader.ReadU8() != 0u)
            {
                reader.Fail("material " + std::to_string(materialIndex) +
                            " has a non-zero reserved padding byte.");
            }

            for (CnbTextureTransform& transform : material.textureTransforms)
            {
                transform.offsetX = reader.ReadF32();
                transform.offsetY = reader.ReadF32();
                transform.scaleX = reader.ReadF32();
                transform.scaleY = reader.ReadF32();
                transform.rotation = reader.ReadF32();
            }
            for (CnbSamplerState& sampler : material.samplers)
            {
                sampler.filter = reader.ReadU32();
                sampler.addressU = reader.ReadU32();
                sampler.addressV = reader.ReadU32();
                const std::uint32_t declared = reader.ReadU32();
                if (declared > 1u)
                {
                    reader.Fail("material " + std::to_string(materialIndex) +
                                " has a sampler 'declared' field that is neither 0 nor 1.");
                }
                sampler.declared = declared != 0u;
            }
            return material;
        }
        /// Every index must address a vertex the part actually has.
        ///
        /// A ModelMeshPart draws `vertexCount` vertices starting at zero, so an index at or above
        /// that count is out of range by construction -- and it reaches the GPU as an out-of-range
        /// fetch, which is a class of problem a content loader should not be forwarding. One pass
        /// over bytes that are being copied anyway, so the cost is not worth trading for it.
        void RequireIndicesInRange(std::span<const std::uint8_t> indexBytes,
                                   std::uint32_t indexElementSize, std::uint32_t vertexCount,
                                   CnbByteReader& reader, const std::string& where)
        {
            const std::size_t count = indexElementSize == 0u ? 0u : indexBytes.size() / indexElementSize;
            for (std::size_t i = 0; i < count; ++i)
            {
                const std::size_t at = i * indexElementSize;
                std::uint32_t index = 0;
                for (std::uint32_t b = 0; b < indexElementSize; ++b)
                {
                    index |= static_cast<std::uint32_t>(indexBytes[at + b]) << (8u * b);
                }
                if (index >= vertexCount)
                {
                    reader.Fail(where + " index " + std::to_string(i) + " addresses vertex " +
                                std::to_string(index) + ", but the part has only " +
                                std::to_string(vertexCount) + ".");
                }
            }
        }
        CnbMorphData ReadMorph(CnbByteReader& reader, std::uint32_t partVertexCount)
        {
            CnbMorphData morph;
            morph.vertexCount = reader.ReadU32();
            if (morph.vertexCount != 0u && morph.vertexCount != partVertexCount)
            {
                reader.Fail("declares " + std::to_string(morph.vertexCount) +
                            " morphed vertices, but its part has " +
                            std::to_string(partVertexCount) + ".");
            }
            const std::uint32_t flags = reader.ReadU32();
            if ((flags & ~kMorphFlagRecomputeFlatNormals) != 0u)
            {
                reader.Fail("sets morph flags this build does not define.");
            }
            morph.recomputeFlatNormals = (flags & kMorphFlagRecomputeFlatNormals) != 0u;

            // The schema ceiling is applied BEFORE the fit-in-remaining check, not after. A count
            // of two million is refusable on sight; making the refusal wait until the file has
            // actually supplied two million elements' worth of bytes means a hostile count costs
            // whoever is validating it real memory before being rejected, and it also forces any
            // test of the ceiling to allocate that much. Reading with element size 0 asks for the
            // limit check without the fit check; the fit check follows immediately below.
            const std::uint32_t targetCount = reader.ReadCount(0u, "morph targets");
            if (targetCount > kMaxMorphTargets)
            {
                reader.Fail("declares " + std::to_string(targetCount) +
                            " morph targets, above this schema's ceiling of " +
                            std::to_string(kMaxMorphTargets) + ".");
            }
            if (static_cast<std::uint64_t>(targetCount) * 4u >
                static_cast<std::uint64_t>(reader.Remaining()))
            {
                reader.Fail("declares " + std::to_string(targetCount) +
                            " morph targets, more than the chunk has room for.");
            }
            std::vector<std::uint32_t> presence;
            presence.reserve(targetCount);
            for (std::uint32_t t = 0; t < targetCount; ++t)
            {
                const std::uint32_t bits = reader.ReadU32();
                const std::uint32_t known =
                    kMorphTargetHasPositions | kMorphTargetHasNormals | kMorphTargetHasTangents;
                if ((bits & ~known) != 0u)
                {
                    reader.Fail("morph target " + std::to_string(t) +
                                " sets presence bits this build does not define.");
                }
                presence.push_back(bits);
            }

            const std::uint64_t streamFloats =
                static_cast<std::uint64_t>(morph.vertexCount) * 3ull;
            morph.targets.resize(targetCount);
            for (std::uint32_t t = 0; t < targetCount; ++t)
            {
                const auto readStream = [&](std::uint32_t bit, std::vector<float>& out)
                {
                    if ((presence[t] & bit) == 0u) { return; }
                    if (streamFloats > static_cast<std::uint64_t>(reader.Remaining() / 4u))
                    {
                        reader.Fail("morph target " + std::to_string(t) +
                                    " declares more delta bytes than the chunk holds.");
                    }
                    out.resize(static_cast<std::size_t>(streamFloats));
                    for (float& value : out) { value = reader.ReadF32(); }
                };
                readStream(kMorphTargetHasPositions, morph.targets[t].positionDeltas);
                readStream(kMorphTargetHasNormals, morph.targets[t].normalDeltas);
                readStream(kMorphTargetHasTangents, morph.targets[t].tangentDeltas);
            }

            const std::uint32_t weightCount = reader.ReadCount(4u, "morph weights");
            morph.weights.resize(weightCount);
            for (float& weight : morph.weights) { weight = reader.ReadF32(); }

            const std::uint32_t trackFlags = reader.ReadU32();
            if ((trackFlags & ~(kMorphTrackStepInterpolation | kMorphTrackCubicSpline)) != 0u)
            {
                reader.Fail("sets morph weight-track flags this build does not define.");
            }
            morph.weightTrackStepInterpolation = (trackFlags & kMorphTrackStepInterpolation) != 0u;
            morph.weightTrackCubicSpline = (trackFlags & kMorphTrackCubicSpline) != 0u;

            const std::uint32_t keyCount = reader.ReadCount(0u, "morph weight keys");
            if (keyCount > kMaxMorphWeightKeys)
            {
                reader.Fail("declares " + std::to_string(keyCount) +
                            " morph weight keys, above this schema's ceiling of " +
                            std::to_string(kMaxMorphWeightKeys) + ".");
            }
            if (static_cast<std::uint64_t>(keyCount) * kMinMorphWeightKeyBytes >
                static_cast<std::uint64_t>(reader.Remaining()))
            {
                reader.Fail("declares " + std::to_string(keyCount) +
                            " morph weight keys, more than the chunk has room for.");
            }
            morph.weightTrackKeys.resize(keyCount);
            for (std::uint32_t k = 0; k < keyCount; ++k)
            {
                CnbMorphWeightKey& key = morph.weightTrackKeys[k];
                key.timeSeconds = ReadCnbSeconds(reader, "a morph weight key time");
                const auto readStream = [&](std::vector<float>& out)
                {
                    const std::uint32_t count = reader.ReadCount(4u, "morph weight values");
                    out.resize(count);
                    for (float& value : out) { value = reader.ReadF32(); }
                };
                readStream(key.weights);
                readStream(key.inTangent);
                readStream(key.outTangent);
            }
            return morph;
        }
    }

    CnbModelData DecodeModelFromCnb(const CnbDocument& document)
    {
        document.RequireAsset(CnbAssetTypeId::Model, CnbModelSchemaVersion);
        const CnbChunkId known[] = {
            CnbModelChunk::Header, CnbModelChunk::Strings, CnbModelChunk::Bones,
            CnbModelChunk::Meshes, CnbModelChunk::Materials, CnbModelChunk::VertexData,
            CnbModelChunk::IndexData, CnbModelChunk::MorphData, CnbModelChunk::Skeleton,
            CnbModelChunk::Animations, CnbModelChunk::Lights, CnbModelChunk::Cameras};
        document.RequireMandatoryChunksUnderstood(known);

        CnbModelData model;

        CnbByteReader header = document.OpenChunk(document.RequireSingle(CnbModelChunk::Header));
        const std::uint32_t headerFlags = header.ReadU32();
        if ((headerFlags & ~kKnownHeaderFlags) != 0u)
        {
            header.Fail("sets model header flags this build does not define.");
        }
        model.hasBoneHierarchy = (headerFlags & kHeaderFlagHasBoneHierarchy) != 0u;
        model.appliesGltfLightingPolicy = (headerFlags & kHeaderFlagGltfLightingPolicy) != 0u;
        const std::uint32_t boneCount = header.ReadU32();
        const std::uint32_t partCount = header.ReadU32();
        const std::uint32_t meshCount = header.ReadU32();
        const std::uint32_t lightCount = header.ReadU32();
        const std::uint32_t animationCount = header.ReadU32();
        header.RequireExhausted();

        const std::uint32_t maxElements = document.Limits().maxArrayElementCount;
        if (boneCount > maxElements || partCount > maxElements || meshCount > maxElements ||
            lightCount > maxElements || animationCount > maxElements)
        {
            header.Fail("declares more bones, parts, meshes, lights or animations than the "
                        "configured limit allows.");
        }

        // --- MSTR ---------------------------------------------------------------------------
        std::vector<std::string> stringTable;
        {
            CnbByteReader reader = document.OpenChunk(document.RequireSingle(CnbModelChunk::Strings));
            const std::uint32_t count = reader.ReadCount(0u, "strings");
            stringTable.reserve(std::min<std::uint32_t>(count, 65536u));
            for (std::uint32_t i = 0; i < count; ++i) { stringTable.push_back(reader.ReadString()); }
            reader.RequireExhausted();
        }
        const auto lookupString = [&stringTable](CnbByteReader& reader, std::uint32_t index,
                                                  const char* what) -> std::string
        {
            if (index == CnbNoIndex) { return {}; }
            if (static_cast<std::size_t>(index) >= stringTable.size())
            {
                reader.Fail(std::string(what) + " names string " + std::to_string(index) +
                            ", but the string table has " + std::to_string(stringTable.size()) +
                            " entries.");
            }
            return stringTable[index];
        };

        // --- MBON ---------------------------------------------------------------------------
        if (boneCount != 0u)
        {
            CnbByteReader reader = document.OpenChunk(document.RequireSingle(CnbModelChunk::Bones));
            const std::uint64_t expected =
                static_cast<std::uint64_t>(boneCount) * CnbModelBoneStride;
            if (reader.Size() != expected)
            {
                reader.Fail("holds " + std::to_string(reader.Size()) +
                            " byte(s) but the header declares " + std::to_string(boneCount) +
                            " bone(s), which need " + std::to_string(expected) + ".");
            }
            model.bones.resize(boneCount);
            for (std::uint32_t b = 0; b < boneCount; ++b)
            {
                CnbModelBone& bone = model.bones[b];
                bone.name = lookupString(reader, reader.ReadU32(), "a bone");
                bone.parent = reader.ReadI32();
                if (bone.parent < -1 || (bone.parent >= 0 &&
                                          static_cast<std::uint32_t>(bone.parent) >= boneCount))
                {
                    reader.Fail("bone " + std::to_string(b) + " names parent " +
                                std::to_string(bone.parent) + ", which is out of range.");
                }
                // Parent-before-child. A file that violated it would not crash, which is exactly
                // why it has to be refused: Model::CopyAbsoluteBoneTransformsTo would compose that
                // bone against a slot it has not written yet and quietly place the geometry
                // somewhere it does not belong. A cycle is a special case of the same thing and is
                // ruled out by the same check.
                if (bone.parent >= 0 && static_cast<std::uint32_t>(bone.parent) >= b)
                {
                    reader.Fail("bone " + std::to_string(b) + " names parent " +
                                std::to_string(bone.parent) +
                                ", which is not earlier in the table; a bone table must be "
                                "ordered parent-before-child.");
                }
                bone.transform = ReadMatrix(reader);
            }
            reader.RequireExhausted();
        }
        else if (document.FindSingle(CnbModelChunk::Bones).has_value())
        {
            throw ContentLoadException(
                "'" + document.Origin() +
                "' has an MBON chunk but declares zero bones in its header.");
        }

        // --- MMAT ---------------------------------------------------------------------------
        std::vector<CnbMaterial> materials;
        {
            CnbByteReader reader =
                document.OpenChunk(document.RequireSingle(CnbModelChunk::Materials));
            const std::uint32_t count = reader.ReadCount(CnbModelMaterialStride, "materials");
            const std::uint64_t expected =
                static_cast<std::uint64_t>(count) * CnbModelMaterialStride + 4u;
            if (reader.Size() != expected)
            {
                reader.Fail("holds " + std::to_string(reader.Size()) + " byte(s) but declares " +
                            std::to_string(count) + " material(s), which need " +
                            std::to_string(expected) + ".");
            }
            materials.reserve(count);
            for (std::uint32_t m = 0; m < count; ++m)
            {
                materials.push_back(ReadMaterial(reader, document, m));
            }
            reader.RequireExhausted();
        }

        // --- MMSH ---------------------------------------------------------------------------
        const std::vector<std::size_t> vertexChunks = document.FindAll(CnbModelChunk::VertexData);
        const std::vector<std::size_t> indexChunks = document.FindAll(CnbModelChunk::IndexData);
        const std::vector<std::size_t> morphChunks = document.FindAll(CnbModelChunk::MorphData);

        // Checked before anything is allocated per part, not after: it is the strongest statement
        // available about partCount (a chunk count can never exceed the table-of-contents limit),
        // and it is also the correctness check that an unreferenced geometry chunk means the
        // file's tables and its payload disagree.
        if (vertexChunks.size() != partCount || indexChunks.size() != partCount)
        {
            throw ContentLoadException(
                "'" + document.Origin() + "' has " + std::to_string(vertexChunks.size()) +
                " vertex and " + std::to_string(indexChunks.size()) + " index chunk(s) for " +
                std::to_string(partCount) + " part(s).");
        }

        {
            CnbByteReader reader = document.OpenChunk(document.RequireSingle(CnbModelChunk::Meshes));
            const std::uint64_t fixed =
                static_cast<std::uint64_t>(meshCount) * CnbModelMeshStride +
                static_cast<std::uint64_t>(partCount) * CnbModelPartStride + 4u;
            if (reader.Size() < fixed)
            {
                reader.Fail("holds " + std::to_string(reader.Size()) +
                            " byte(s), too few for the " + std::to_string(meshCount) +
                            " mesh row(s) and " + std::to_string(partCount) +
                            " part row(s) the header declares.");
            }

            struct MeshRow
            {
                std::string name;
                std::int32_t parentBone = -1;
                std::uint32_t firstSlot = 0u;
                std::uint32_t slotCount = 0u;
            };
            std::vector<MeshRow> meshRows;
            meshRows.reserve(meshCount);
            for (std::uint32_t m = 0; m < meshCount; ++m)
            {
                MeshRow row;
                row.name = lookupString(reader, reader.ReadU32(), "a mesh");
                row.parentBone = reader.ReadI32();
                row.firstSlot = reader.ReadU32();
                row.slotCount = reader.ReadU32();
                if (row.parentBone < -1 ||
                    (row.parentBone >= 0 && static_cast<std::uint32_t>(row.parentBone) >= boneCount))
                {
                    reader.Fail("mesh " + std::to_string(m) + " names parentBone " +
                                std::to_string(row.parentBone) + ", which is out of range.");
                }
                meshRows.push_back(std::move(row));
            }

            model.parts.resize(partCount);
            for (std::uint32_t p = 0; p < partCount; ++p)
            {
                CnbModelPart& part = model.parts[p];
                part.name = lookupString(reader, reader.ReadU32(), "a part");
                const std::uint32_t vertexOrdinal = reader.ReadU32();
                const std::uint32_t indexOrdinal = reader.ReadU32();
                const std::uint32_t morphOrdinal = reader.ReadU32();
                part.vertexStride = reader.ReadU32();
                part.vertexCount = reader.ReadU32();
                part.indexCount = reader.ReadU32();
                part.indexElementSize = reader.ReadU32();
                part.primitiveTopology = reader.ReadU32();
                part.primitiveCount = reader.ReadU32();
                const std::uint32_t effectKind = reader.ReadU32();
                const std::uint32_t externalEffect = reader.ReadU32();
                const std::uint32_t materialIndex = reader.ReadU32();
                const std::uint32_t flags = reader.ReadU32();

                const std::string where = "part " + std::to_string(p);
                if (part.vertexStride == 0u || part.vertexStride > kMaxVertexStride)
                {
                    reader.Fail(where + " declares an unusable vertex stride of " +
                                std::to_string(part.vertexStride) + ".");
                }
                if (part.indexElementSize != 2u && part.indexElementSize != 4u)
                {
                    reader.Fail(where + " declares an index element size of " +
                                std::to_string(part.indexElementSize) +
                                "; only 2 and 4 are valid.");
                }
                if (part.primitiveTopology > 6u)
                {
                    reader.Fail(where + " names primitive topology " +
                                std::to_string(part.primitiveTopology) +
                                ", which is not a glTF primitive mode (0-6).");
                }
                if (effectKind > CnbMaxEffectKind)
                {
                    reader.Fail(where + " names effect kind " + std::to_string(effectKind) +
                                ", which this build does not define.");
                }
                part.effectKind = static_cast<CnbEffectKind>(effectKind);
                if ((flags & ~(kPartFlagVertexColorEnabled | kPartFlagUnlit)) != 0u)
                {
                    reader.Fail(where + " sets part flags this build does not define.");
                }
                part.vertexColorEnabled = (flags & kPartFlagVertexColorEnabled) != 0u;
                part.unlit = (flags & kPartFlagUnlit) != 0u;

                if (part.effectKind == CnbEffectKind::External)
                {
                    if (externalEffect == CnbNoIndex)
                    {
                        reader.Fail(where +
                                    " uses an external effect but names no external reference.");
                    }
                    part.externalEffect = ResolveReference(document, externalEffect,
                                                            "a part's effect");
                }
                else if (externalEffect != CnbNoIndex)
                {
                    reader.Fail(where +
                                " names an external effect reference but uses a stock effect.");
                }

                if (static_cast<std::size_t>(materialIndex) >= materials.size())
                {
                    reader.Fail(where + " names material " + std::to_string(materialIndex) +
                                ", but the file declares " + std::to_string(materials.size()) + ".");
                }
                part.material = materials[materialIndex];

                if (static_cast<std::size_t>(vertexOrdinal) >= vertexChunks.size())
                {
                    reader.Fail(where + " names vertex chunk " + std::to_string(vertexOrdinal) +
                                ", but the file has " + std::to_string(vertexChunks.size()) + ".");
                }
                if (static_cast<std::size_t>(indexOrdinal) >= indexChunks.size())
                {
                    reader.Fail(where + " names index chunk " + std::to_string(indexOrdinal) +
                                ", but the file has " + std::to_string(indexChunks.size()) + ".");
                }

                const auto vertexBytes = document.ChunkData(vertexChunks[vertexOrdinal]);
                const auto indexBytes = document.ChunkData(indexChunks[indexOrdinal]);
                const std::uint64_t expectedVertexBytes = CheckedMultiply(
                    part.vertexStride, part.vertexCount, reader.Context() + " " + where);
                const std::uint64_t expectedIndexBytes = CheckedMultiply(
                    part.indexElementSize, part.indexCount, reader.Context() + " " + where);
                if (expectedVertexBytes != vertexBytes.size())
                {
                    reader.Fail(where + " declares " + std::to_string(part.vertexCount) +
                                " vertices of " + std::to_string(part.vertexStride) +
                                " bytes, but its vertex chunk holds " +
                                std::to_string(vertexBytes.size()) + " byte(s).");
                }
                if (expectedIndexBytes != indexBytes.size())
                {
                    reader.Fail(where + " declares " + std::to_string(part.indexCount) +
                                " indices of " + std::to_string(part.indexElementSize) +
                                " bytes, but its index chunk holds " +
                                std::to_string(indexBytes.size()) + " byte(s).");
                }
                const std::uint64_t derivedPrimitives =
                    DerivePrimitiveCount(part.primitiveTopology, part.indexCount);
                if (part.primitiveCount != derivedPrimitives)
                {
                    reader.Fail(where + " declares " + std::to_string(part.primitiveCount) +
                                " primitives, but " + std::to_string(part.indexCount) +
                                " indices at topology " + std::to_string(part.primitiveTopology) +
                                " describe " + std::to_string(derivedPrimitives) +
                                "; a part must not draw past its own index buffer.");
                }
                RequireIndicesInRange(indexBytes, part.indexElementSize, part.vertexCount, reader,
                                      where);

                part.vertexBytes.assign(vertexBytes.begin(), vertexBytes.end());
                part.indexBytes.assign(indexBytes.begin(), indexBytes.end());

                if (morphOrdinal != CnbNoIndex)
                {
                    if (static_cast<std::size_t>(morphOrdinal) >= morphChunks.size())
                    {
                        reader.Fail(where + " names morph chunk " + std::to_string(morphOrdinal) +
                                    ", but the file has " + std::to_string(morphChunks.size()) +
                                    ".");
                    }
                    CnbByteReader morphReader = document.OpenChunk(morphChunks[morphOrdinal]);
                    part.morph = ReadMorph(morphReader, part.vertexCount);
                    morphReader.RequireExhausted();
                }
            }

            const std::uint32_t slotCount = reader.ReadCount(4u, "mesh part slots");
            std::vector<std::uint32_t> slots;
            slots.reserve(slotCount);
            for (std::uint32_t i = 0; i < slotCount; ++i)
            {
                const std::uint32_t partIndex = reader.ReadU32();
                if (partIndex >= partCount)
                {
                    reader.Fail("mesh part slot " + std::to_string(i) + " names part " +
                                std::to_string(partIndex) + ", but the file declares " +
                                std::to_string(partCount) + ".");
                }
                slots.push_back(partIndex);
            }
            reader.RequireExhausted();

            model.meshes.reserve(meshCount);
            for (std::size_t m = 0; m < meshRows.size(); ++m)
            {
                const MeshRow& row = meshRows[m];
                const std::uint64_t end =
                    CheckedAdd(row.firstSlot, row.slotCount, reader.Context());
                if (end > slotCount)
                {
                    reader.Fail("mesh " + std::to_string(m) + " names part slots [" +
                                std::to_string(row.firstSlot) + ", " + std::to_string(end) +
                                ") but the file has " + std::to_string(slotCount) + ".");
                }
                CnbModelMesh mesh;
                mesh.name = row.name;
                mesh.parentBone = row.parentBone;
                mesh.partIndices.assign(slots.begin() + static_cast<std::ptrdiff_t>(row.firstSlot),
                                        slots.begin() + static_cast<std::ptrdiff_t>(end));
                model.meshes.push_back(std::move(mesh));
            }
        }

        // --- MSKL ---------------------------------------------------------------------------
        if (const auto index = document.FindSingle(CnbModelChunk::Skeleton); index.has_value())
        {
            CnbByteReader reader = document.OpenChunk(*index);
            const std::uint32_t jointCount = reader.ReadCount(4u, "skeleton joints");
            const std::uint32_t flags = reader.ReadU32();
            if ((flags & ~kSkeletonFlagHasRootPrefix) != 0u)
            {
                reader.Fail("sets skeleton flags this build does not define.");
            }
            const bool hasRootPrefix = (flags & kSkeletonFlagHasRootPrefix) != 0u;

            const std::uint64_t matrixBlocks = hasRootPrefix ? 3ull : 2ull;
            const std::uint64_t expected =
                8ull + static_cast<std::uint64_t>(jointCount) * 4ull +
                static_cast<std::uint64_t>(jointCount) * 64ull * matrixBlocks;
            if (reader.Size() != expected)
            {
                reader.Fail("holds " + std::to_string(reader.Size()) + " byte(s) but declares " +
                            std::to_string(jointCount) + " joint(s)" +
                            (hasRootPrefix ? " with" : " without") +
                            " a root prefix, which need " + std::to_string(expected) + ".");
            }

            CnbModelSkeleton skeleton;
            skeleton.hierarchy.resize(jointCount);
            for (std::uint32_t j = 0; j < jointCount; ++j)
            {
                const std::int32_t parent = reader.ReadI32();
                if (parent < -1 || (parent >= 0 && static_cast<std::uint32_t>(parent) >= jointCount))
                {
                    reader.Fail("joint " + std::to_string(j) + " names parent " +
                                std::to_string(parent) + ", which is out of range.");
                }
                if (parent >= 0 && static_cast<std::uint32_t>(parent) >= j)
                {
                    reader.Fail("joint " + std::to_string(j) + " names parent " +
                                std::to_string(parent) +
                                ", which is not earlier in the table; a skeleton must be ordered "
                                "parent-before-child.");
                }
                skeleton.hierarchy[j] = parent;
            }
            skeleton.bindPose.resize(jointCount);
            for (auto& matrix : skeleton.bindPose) { matrix = ReadMatrix(reader); }
            skeleton.inverseBindPose.resize(jointCount);
            for (auto& matrix : skeleton.inverseBindPose) { matrix = ReadMatrix(reader); }
            if (hasRootPrefix)
            {
                skeleton.rootPrefix.resize(jointCount);
                for (auto& matrix : skeleton.rootPrefix) { matrix = ReadMatrix(reader); }
            }
            reader.RequireExhausted();
            model.skeleton = std::move(skeleton);
        }

        // --- MANM ---------------------------------------------------------------------------
        if (const auto index = document.FindSingle(CnbModelChunk::Animations); index.has_value())
        {
            CnbByteReader reader = document.OpenChunk(*index);
            const std::uint32_t clipCount = reader.ReadCount(24u, "animation clips");
            if (clipCount != animationCount)
            {
                reader.Fail("declares " + std::to_string(clipCount) +
                            " clip(s) but the model header declares " +
                            std::to_string(animationCount) + ".");
            }
            if (reader.ReadU32() != 0u) { reader.Fail("has a non-zero reserved field."); }

            struct ClipRow
            {
                std::string name;
                std::uint32_t targetSpace = 0u;
                std::uint32_t firstTrack = 0u;
                std::uint32_t trackCount = 0u;
                double duration = 0.0;
            };
            std::vector<ClipRow> clipRows;
            clipRows.reserve(clipCount);
            for (std::uint32_t c = 0; c < clipCount; ++c)
            {
                ClipRow row;
                row.name = lookupString(reader, reader.ReadU32(), "an animation clip");
                row.targetSpace = reader.ReadU32();
                if (row.targetSpace > 1u)
                {
                    reader.Fail("clip " + std::to_string(c) + " has target space " +
                                std::to_string(row.targetSpace) +
                                ", which is not a ClipTargetSpaceEXT value (0-1).");
                }
                row.firstTrack = reader.ReadU32();
                row.trackCount = reader.ReadU32();
                row.duration = ReadCnbSeconds(reader, "a clip duration");
                clipRows.push_back(std::move(row));
            }

            const std::uint32_t totalTracks = reader.ReadCount(12u, "animation tracks");
            struct TrackRow
            {
                std::int32_t boneIndex = -1;
                std::uint32_t firstKey = 0u;
                std::uint32_t keyCount = 0u;
            };
            std::vector<TrackRow> trackRows;
            trackRows.reserve(totalTracks);
            for (std::uint32_t t = 0; t < totalTracks; ++t)
            {
                TrackRow row;
                row.boneIndex = reader.ReadI32();
                row.firstKey = reader.ReadU32();
                row.keyCount = reader.ReadU32();
                trackRows.push_back(row);
            }

            const std::uint32_t totalKeys = reader.ReadCount(48u, "animation keyframes");
            std::vector<KeyframeEXT> keys;
            keys.reserve(totalKeys);
            for (std::uint32_t k = 0; k < totalKeys; ++k) { keys.push_back(ReadCnbKeyframe(reader)); }
            reader.RequireExhausted();

            for (std::uint32_t t = 0; t < totalTracks; ++t)
            {
                const std::uint64_t end = CheckedAdd(trackRows[t].firstKey, trackRows[t].keyCount,
                                                      reader.Context());
                if (end > totalKeys)
                {
                    reader.Fail("track " + std::to_string(t) + " names keyframes [" +
                                std::to_string(trackRows[t].firstKey) + ", " +
                                std::to_string(end) + ") but the file has " +
                                std::to_string(totalKeys) + ".");
                }
            }

            model.animations.reserve(clipCount);
            for (const ClipRow& row : clipRows)
            {
                const std::uint64_t end =
                    CheckedAdd(row.firstTrack, row.trackCount, reader.Context());
                if (end > totalTracks)
                {
                    reader.Fail("clip '" + row.name + "' names tracks [" +
                                std::to_string(row.firstTrack) + ", " + std::to_string(end) +
                                ") but the file has " + std::to_string(totalTracks) + ".");
                }

                CnbModelAnimation animation;
                animation.name = row.name;
                animation.clip.Duration = System::TimeSpan::FromSeconds(row.duration);
                animation.clip.TargetSpace = static_cast<ClipTargetSpaceEXT>(row.targetSpace);
                animation.clip.Tracks.reserve(row.trackCount);
                for (std::uint32_t t = row.firstTrack; t < end; ++t)
                {
                    BoneTrackEXT track;
                    track.BoneIndex = trackRows[t].boneIndex;
                    track.Keys.assign(
                        keys.begin() + static_cast<std::ptrdiff_t>(trackRows[t].firstKey),
                        keys.begin() + static_cast<std::ptrdiff_t>(trackRows[t].firstKey) +
                            static_cast<std::ptrdiff_t>(trackRows[t].keyCount));
                    animation.clip.Tracks.push_back(std::move(track));
                }
                model.animations.push_back(std::move(animation));
            }
        }
        else if (animationCount != 0u)
        {
            throw ContentLoadException(
                "'" + document.Origin() + "' declares " + std::to_string(animationCount) +
                " animation(s) but carries no MANM chunk.");
        }

        // --- MLIT ---------------------------------------------------------------------------
        if (const auto index = document.FindSingle(CnbModelChunk::Lights); index.has_value())
        {
            CnbByteReader reader = document.OpenChunk(*index);
            const std::uint32_t count = reader.ReadCount(24u, "lights");
            if (count != lightCount)
            {
                reader.Fail("declares " + std::to_string(count) +
                            " light(s) but the model header declares " +
                            std::to_string(lightCount) + ".");
            }
            model.lights.resize(count);
            for (CnbModelLight& light : model.lights)
            {
                for (float& value : light.direction) { value = reader.ReadF32(); }
                for (float& value : light.diffuseColor) { value = reader.ReadF32(); }
            }
            reader.RequireExhausted();
        }
        else if (lightCount != 0u)
        {
            throw ContentLoadException(
                "'" + document.Origin() + "' declares " + std::to_string(lightCount) +
                " light(s) but carries no MLIT chunk.");
        }

        // --- MCAM ---------------------------------------------------------------------------
        if (const auto index = document.FindSingle(CnbModelChunk::Cameras); index.has_value())
        {
            CnbByteReader reader = document.OpenChunk(*index);
            // Each camera is at least an empty name (4), the node and flag words (8) and 22 floats.
            const std::uint32_t count = reader.ReadCount(4u + 8u + 22u * 4u, "cameras");
            model.cameras.resize(count);
            for (CnbModelCamera& camera : model.cameras)
            {
                camera.name = reader.ReadString();
                camera.sceneNodeIndex = reader.ReadI32();
                if (camera.sceneNodeIndex < -1 ||
                    (camera.sceneNodeIndex >= 0 &&
                     static_cast<std::uint32_t>(camera.sceneNodeIndex) >= boneCount))
                {
                    reader.Fail("names camera node " + std::to_string(camera.sceneNodeIndex) +
                                " of a model with " + std::to_string(boneCount) + " bone(s).");
                }
                const std::uint32_t flags = reader.ReadU32();
                if (flags > 1u) { reader.Fail("sets camera flags this build does not define."); }
                camera.perspective = flags == 1u;
                for (float* value : {&camera.yfov, &camera.aspectRatio, &camera.xmag, &camera.ymag,
                                     &camera.znear, &camera.zfar})
                {
                    *value = reader.ReadF32();
                }
                for (float& value : camera.worldTransform) { value = reader.ReadF32(); }
            }
            reader.RequireExhausted();
        }

        if (morphChunks.size() > partCount)
        {
            throw ContentLoadException(
                "'" + document.Origin() + "' has more morph chunks than parts.");
        }

        return model;
    }
}

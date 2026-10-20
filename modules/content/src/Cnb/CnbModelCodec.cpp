// SPDX-License-Identifier: MS-PL
// EncodeModelToCnb(): the writing half of the .cnb model codec (schema 1).
//
// Link boundary: the reading half is CnbModelDecode.cpp, which every program that loads a .cnb
// model links; this one, which needs CnbWriter, only a content tool does
// (docs/link-size-architecture.md).

#include "CNA/Content/Cnb/CnbModelCodec.hpp"
#include "CnbCodecDetail.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <utility>

#include "CNA/Content/Cnb/CnbAnimationClipCodec.hpp"
#include "CNA/Content/Cnb/CnbArithmetic.hpp"
#include "CNA/Content/Cnb/CnbByteReader.hpp"
#include "CNA/Content/Cnb/CnbByteWriter.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Graphics::AnimationClipEXT;
using Microsoft::Xna::Framework::Graphics::BoneTrackEXT;
using Microsoft::Xna::Framework::Graphics::ClipTargetSpaceEXT;
using Microsoft::Xna::Framework::Graphics::KeyframeEXT;

namespace CNA::Content::Cnb
{
    namespace
    {
        using namespace ModelCodecDetail;

        /// Lexicographically orders encoded records without routing through libstdc++'s
        /// vector comparison, whose optimized byte path triggers GCC's false-positive
        /// `-Wstringop-overread` for the material lookup below.
        struct EncodedBytesLess
        {
            [[nodiscard]] bool operator()(const std::vector<std::uint8_t>& left,
                                          const std::vector<std::uint8_t>& right) const noexcept
            {
                const std::size_t commonSize = std::min(left.size(), right.size());
                for (std::size_t index = 0; index < commonSize; ++index)
                {
                    if (left[index] != right[index]) { return left[index] < right[index]; }
                }
                return left.size() < right.size();
            }
        };

        /// Interns names into one deduplicated table. Deterministic by construction: an index is
        /// assigned the first time a string is seen, in encode order, so the same model always
        /// produces the same table.
        class StringTableBuilder
        {
        public:
            std::uint32_t Intern(const std::string& value)
            {
                const auto existing = indexByValue_.find(value);
                if (existing != indexByValue_.end()) { return existing->second; }
                const auto index = static_cast<std::uint32_t>(values_.size());
                if (values_.size() >= static_cast<std::size_t>(CnbNoIndex))
                {
                    throw ContentLoadException("CNB Model: too many distinct strings to encode.");
                }
                indexByValue_.emplace(value, index);
                values_.push_back(value);
                return index;
            }

            [[nodiscard]] const std::vector<std::string>& Values() const { return values_; }

        private:
            std::map<std::string, std::uint32_t> indexByValue_;
            std::vector<std::string> values_;
        };

        /// Interns external asset references, keyed by (name, expected type) so the same texture
        /// referenced from ten parts becomes one XREF row and one ContentManager load.
        class ExternalReferenceBuilder
        {
        public:
            std::uint32_t Intern(const std::string& logicalName, std::uint32_t expectedAssetTypeId)
            {
                if (logicalName.empty()) { return CnbNoIndex; }
                const auto key = std::make_pair(logicalName, expectedAssetTypeId);
                const auto existing = indexByKey_.find(key);
                if (existing != indexByKey_.end()) { return existing->second; }
                const auto index = static_cast<std::uint32_t>(references_.size());
                if (references_.size() >= static_cast<std::size_t>(CnbNoIndex))
                {
                    throw ContentLoadException(
                        "CNB Model: too many external references to encode.");
                }
                indexByKey_.emplace(key, index);
                references_.push_back(CnbExternalReference{0u, expectedAssetTypeId, logicalName});
                return index;
            }

            [[nodiscard]] std::vector<CnbExternalReference> Take() { return std::move(references_); }

        private:
            std::map<std::pair<std::string, std::uint32_t>, std::uint32_t> indexByKey_;
            std::vector<CnbExternalReference> references_;
        };

        void WriteMatrix(CnbByteWriter& writer, const std::array<float, 16>& matrix)
        {
            for (const float value : matrix) { writer.WriteF32(value); }
        }


        std::uint32_t ToU32(std::size_t value, const char* what)
        {
            if (value > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
            {
                throw ContentLoadException(std::string("CNB Model: ") + what +
                                            " is too large to encode.");
            }
            return static_cast<std::uint32_t>(value);
        }


        void WriteMaterial(CnbByteWriter& writer, const CnbMaterial& material,
                           ExternalReferenceBuilder& references)
        {
            if (material.alphaMode > 2u)
            {
                throw ContentLoadException(
                    "CNB Model: material alphaMode " + std::to_string(material.alphaMode) +
                    " is not an AlphaModeEXT value (0-2).");
            }
            for (const std::uint8_t set : material.textureCoordinateSets)
            {
                if (set > 1u)
                {
                    throw ContentLoadException(
                        "CNB Model: material names texture-coordinate set " +
                        std::to_string(set) + "; only 0 and 1 are supported.");
                }
            }
            const std::array<const std::string*, 8> textures{
                &material.baseColorTexture, &material.texture2, &material.normalMap,
                &material.metallicRoughnessMap, &material.emissiveMap, &material.occlusionMap,
                &material.specularMap, &material.specularColorMap};
            for (const std::string* texture : textures)
            {
                writer.WriteU32(references.Intern(*texture, CnbAssetTypeId::Texture2D));
            }

            for (const float value : material.baseColorFactor) { writer.WriteF32(value); }
            for (const float value : material.emissiveFactor) { writer.WriteF32(value); }
            for (const float value : material.specularColorFactor) { writer.WriteF32(value); }
            writer.WriteF32(material.metallicFactor);
            writer.WriteF32(material.roughnessFactor);
            writer.WriteF32(material.ior);
            writer.WriteF32(material.specularFactor);
            writer.WriteF32(material.normalScale);
            writer.WriteF32(material.occlusionStrength);
            writer.WriteF32(material.alphaCutoff);
            writer.WriteU32(material.alphaMode);
            writer.WriteU32(material.doubleSided ? kMaterialFlagDoubleSided : 0u);
            for (const std::uint8_t set : material.textureCoordinateSets) { writer.WriteU8(set); }
            writer.WriteU8(0u); // pad to a 4-byte boundary; must be zero

            for (const CnbTextureTransform& transform : material.textureTransforms)
            {
                writer.WriteF32(transform.offsetX);
                writer.WriteF32(transform.offsetY);
                writer.WriteF32(transform.scaleX);
                writer.WriteF32(transform.scaleY);
                writer.WriteF32(transform.rotation);
            }
            for (const CnbSamplerState& sampler : material.samplers)
            {
                writer.WriteU32(sampler.filter);
                writer.WriteU32(sampler.addressU);
                writer.WriteU32(sampler.addressV);
                writer.WriteU32(sampler.declared ? 1u : 0u);
            }
        }




        void WriteMorph(CnbByteWriter& writer, const CnbMorphData& morph)
        {
            const std::uint32_t targetCount = ToU32(morph.targets.size(), "a morph target count");
            const std::uint64_t streamFloats =
                static_cast<std::uint64_t>(morph.vertexCount) * 3ull;

            writer.WriteU32(morph.vertexCount);
            writer.WriteU32(morph.recomputeFlatNormals ? kMorphFlagRecomputeFlatNormals : 0u);
            writer.WriteU32(targetCount);

            for (const CnbMorphTarget& target : morph.targets)
            {
                std::uint32_t presence = 0u;
                const auto check = [&](const std::vector<float>& stream, std::uint32_t bit,
                                       const char* which)
                {
                    if (stream.empty()) { return; }
                    if (static_cast<std::uint64_t>(stream.size()) != streamFloats)
                    {
                        throw ContentLoadException(
                            std::string("CNB Model: a morph target's ") + which + " stream has " +
                            std::to_string(stream.size()) + " float(s) but the target covers " +
                            std::to_string(morph.vertexCount) + " vertices.");
                    }
                    presence |= bit;
                };
                check(target.positionDeltas, kMorphTargetHasPositions, "position");
                check(target.normalDeltas, kMorphTargetHasNormals, "normal");
                check(target.tangentDeltas, kMorphTargetHasTangents, "tangent");
                writer.WriteU32(presence);
            }
            for (const CnbMorphTarget& target : morph.targets)
            {
                for (const float value : target.positionDeltas) { writer.WriteF32(value); }
                for (const float value : target.normalDeltas) { writer.WriteF32(value); }
                for (const float value : target.tangentDeltas) { writer.WriteF32(value); }
            }

            writer.WriteU32(ToU32(morph.weights.size(), "a morph weight count"));
            for (const float weight : morph.weights) { writer.WriteF32(weight); }

            std::uint32_t trackFlags = 0u;
            if (morph.weightTrackStepInterpolation) { trackFlags |= kMorphTrackStepInterpolation; }
            if (morph.weightTrackCubicSpline) { trackFlags |= kMorphTrackCubicSpline; }
            writer.WriteU32(trackFlags);
            writer.WriteU32(ToU32(morph.weightTrackKeys.size(), "a morph weight key count"));
            for (const CnbMorphWeightKey& key : morph.weightTrackKeys)
            {
                writer.WriteF64(key.timeSeconds);
                const auto writeStream = [&](const std::vector<float>& stream)
                {
                    writer.WriteU32(ToU32(stream.size(), "a morph weight stream length"));
                    for (const float value : stream) { writer.WriteF32(value); }
                };
                writeStream(key.weights);
                writeStream(key.inTangent);
                writeStream(key.outTangent);
            }
        }

    }

    std::vector<std::uint8_t> EncodeModelToCnb(const CnbModelData& model,
                                                const std::string& contentName)
    {
        StringTableBuilder strings;
        ExternalReferenceBuilder references;

        const std::uint32_t boneCount = ToU32(model.bones.size(), "a bone count");
        const std::uint32_t partCount = ToU32(model.parts.size(), "a part count");
        const std::uint32_t meshCount = ToU32(model.meshes.size(), "a mesh count");
        const std::uint32_t lightCount = ToU32(model.lights.size(), "a light count");
        const std::uint32_t animationCount = ToU32(model.animations.size(), "an animation count");

        // --- MBON ---------------------------------------------------------------------------
        CnbByteWriter bones;
        for (std::size_t boneIndex = 0; boneIndex < model.bones.size(); ++boneIndex)
        {
            const CnbModelBone& bone = model.bones[boneIndex];
            if (bone.parent < -1 ||
                (bone.parent >= 0 && static_cast<std::uint32_t>(bone.parent) >= boneCount))
            {
                throw ContentLoadException(
                    "CNB Model: bone '" + bone.name + "' names an out-of-range parent index.");
            }
            // Parent-before-child, which also makes a cycle structurally impossible. Not an
            // arbitrary tidiness rule: Model::CopyAbsoluteBoneTransformsTo composes world
            // transforms in one ascending pass, reading dest[parentIndex] as it goes, so a bone
            // whose parent comes later reads a slot that has not been written yet and silently
            // produces a wrong world transform rather than failing. Both source formats already
            // specify this order ('.cnj' bones are documented parent-before-child, and
            // SkinningData::SkeletonHierarchy is documented topological), so nothing that can be
            // authored today is refused by stating it here.
            if (bone.parent >= 0 && static_cast<std::size_t>(bone.parent) >= boneIndex)
            {
                throw ContentLoadException(
                    "CNB Model: bone " + std::to_string(boneIndex) + " ('" + bone.name +
                    "') names parent " + std::to_string(bone.parent) +
                    ", which is not earlier in the table. A bone table must be ordered "
                    "parent-before-child.");
            }
            bones.WriteU32(strings.Intern(bone.name));
            bones.WriteI32(bone.parent);
            WriteMatrix(bones, bone.transform);
        }

        // --- MVTX / MIDX / MMRP, and the part rows that address them ------------------------
        std::vector<std::vector<std::uint8_t>> vertexChunks;
        std::vector<std::vector<std::uint8_t>> indexChunks;
        std::vector<std::vector<std::uint8_t>> morphChunks;
        CnbByteWriter partRows;
        CnbByteWriter materialRows;
        std::uint32_t materialCount = 0u;
        // Materials are interned by their encoded bytes. Two parts of one model very often share
        // a material (a multi-primitive mesh split only by topology, for one), and storing 368
        // identical bytes per part instead of an index is exactly the kind of waste a compiled
        // format should not have. Deterministic: the first occurrence, in part order, wins.
        std::map<std::vector<std::uint8_t>, std::uint32_t, EncodedBytesLess>
            materialIndexByBytes;

        for (std::size_t p = 0; p < model.parts.size(); ++p)
        {
            const CnbModelPart& part = model.parts[p];
            const std::string where = "CNB Model: part " + std::to_string(p) + " ('" + part.name + "')";

            if (part.vertexStride == 0u || part.vertexStride > kMaxVertexStride)
            {
                throw ContentLoadException(where + " has an unusable vertex stride of " +
                                            std::to_string(part.vertexStride) + ".");
            }
            if (part.indexElementSize != 2u && part.indexElementSize != 4u)
            {
                throw ContentLoadException(where + " has an index element size of " +
                                            std::to_string(part.indexElementSize) +
                                            "; only 2 and 4 are valid.");
            }
            const std::uint64_t expectedVertexBytes =
                CheckedMultiply(part.vertexStride, part.vertexCount, where);
            if (expectedVertexBytes != part.vertexBytes.size())
            {
                throw ContentLoadException(
                    where + " supplies " + std::to_string(part.vertexBytes.size()) +
                    " vertex byte(s) but declares " + std::to_string(part.vertexCount) +
                    " vertices of " + std::to_string(part.vertexStride) + " bytes.");
            }
            const std::uint64_t expectedIndexBytes =
                CheckedMultiply(part.indexElementSize, part.indexCount, where);
            if (expectedIndexBytes != part.indexBytes.size())
            {
                throw ContentLoadException(
                    where + " supplies " + std::to_string(part.indexBytes.size()) +
                    " index byte(s) but declares " + std::to_string(part.indexCount) +
                    " indices of " + std::to_string(part.indexElementSize) + " bytes.");
            }
            // Validate the payload, not just its size: a well-formed container must
            // not describe a GPU vertex fetch outside this part's vertex buffer.
            CnbByteReader indices(part.indexBytes, where);
            for (std::uint32_t i = 0; i < part.indexCount; ++i)
            {
                const std::uint32_t index = part.indexElementSize == 2u
                    ? indices.ReadU16() : indices.ReadU32();
                if (index >= part.vertexCount)
                {
                    throw ContentLoadException(
                        where + " index " + std::to_string(i) + " addresses vertex " +
                        std::to_string(index) + ", but the part has only " +
                        std::to_string(part.vertexCount) + ".");
                }
            }
            if (static_cast<std::uint32_t>(part.effectKind) > CnbMaxEffectKind)
            {
                throw ContentLoadException(where + " has an unknown effect kind.");
            }
            if (part.primitiveTopology > 6u)
            {
                throw ContentLoadException(where + " has primitive topology " +
                                            std::to_string(part.primitiveTopology) +
                                            ", which is not a glTF primitive mode (0-6).");
            }
            // primitiveCount is derivable from the topology and the index count, so storing it is
            // redundant -- and redundant data in a binary format is only safe if it is
            // cross-checked. A part claiming more primitives than its indices describe would draw
            // past the end of its own index buffer.
            const std::uint64_t derivedPrimitives =
                DerivePrimitiveCount(part.primitiveTopology, part.indexCount);
            if (part.primitiveCount != derivedPrimitives)
            {
                throw ContentLoadException(
                    where + " declares " + std::to_string(part.primitiveCount) +
                    " primitives, but " + std::to_string(part.indexCount) +
                    " indices at topology " + std::to_string(part.primitiveTopology) +
                    " describe " + std::to_string(derivedPrimitives) + ".");
            }

            const auto vertexOrdinal = ToU32(vertexChunks.size(), "a vertex chunk ordinal");
            const auto indexOrdinal = ToU32(indexChunks.size(), "an index chunk ordinal");
            vertexChunks.push_back(part.vertexBytes);
            indexChunks.push_back(part.indexBytes);

            std::uint32_t morphOrdinal = CnbNoIndex;
            if (part.morph.has_value())
            {
                if (part.morph->vertexCount != 0u && part.morph->vertexCount != part.vertexCount)
                {
                    throw ContentLoadException(
                        where + " has morph data covering " +
                        std::to_string(part.morph->vertexCount) +
                        " vertices, but the part has " + std::to_string(part.vertexCount) + ".");
                }
                CnbByteWriter morph;
                WriteMorph(morph, *part.morph);
                morphOrdinal = ToU32(morphChunks.size(), "a morph chunk ordinal");
                morphChunks.push_back(morph.Take());
            }

            std::uint32_t flags = 0u;
            if (part.vertexColorEnabled) { flags |= kPartFlagVertexColorEnabled; }
            if (part.unlit) { flags |= kPartFlagUnlit; }

            partRows.WriteU32(strings.Intern(part.name));
            partRows.WriteU32(vertexOrdinal);
            partRows.WriteU32(indexOrdinal);
            partRows.WriteU32(morphOrdinal);
            partRows.WriteU32(part.vertexStride);
            partRows.WriteU32(part.vertexCount);
            partRows.WriteU32(part.indexCount);
            partRows.WriteU32(part.indexElementSize);
            partRows.WriteU32(part.primitiveTopology);
            partRows.WriteU32(part.primitiveCount);
            partRows.WriteU32(static_cast<std::uint32_t>(part.effectKind));
            partRows.WriteU32(part.effectKind == CnbEffectKind::External
                                  ? references.Intern(part.externalEffect, CnbAssetTypeId::Effect)
                                  : CnbNoIndex);
            CnbByteWriter oneMaterial;
            WriteMaterial(oneMaterial, part.material, references);
            std::vector<std::uint8_t> materialBytes = oneMaterial.Take();
            const auto existing = materialIndexByBytes.find(materialBytes);
            std::uint32_t materialIndex;
            if (existing != materialIndexByBytes.end())
            {
                materialIndex = existing->second;
            }
            else
            {
                materialIndex = materialCount++;
                materialRows.WriteBytes(materialBytes);
                materialIndexByBytes.emplace(std::move(materialBytes), materialIndex);
            }

            partRows.WriteU32(materialIndex);
            partRows.WriteU32(flags);
        }

        // --- MMSH ---------------------------------------------------------------------------
        CnbByteWriter meshRows;
        CnbByteWriter slotValues;
        std::uint32_t slotCount = 0u;
        for (const CnbModelMesh& mesh : model.meshes)
        {
            if (mesh.parentBone < -1 ||
                (mesh.parentBone >= 0 && static_cast<std::uint32_t>(mesh.parentBone) >= boneCount))
            {
                throw ContentLoadException(
                    "CNB Model: mesh '" + mesh.name + "' names an out-of-range parentBone index.");
            }
            meshRows.WriteU32(strings.Intern(mesh.name));
            meshRows.WriteI32(mesh.parentBone);
            meshRows.WriteU32(slotCount);
            meshRows.WriteU32(ToU32(mesh.partIndices.size(), "a mesh part count"));
            for (const std::uint32_t partIndex : mesh.partIndices)
            {
                if (partIndex >= partCount)
                {
                    throw ContentLoadException(
                        "CNB Model: mesh '" + mesh.name + "' names out-of-range part index " +
                        std::to_string(partIndex) + ".");
                }
                slotValues.WriteU32(partIndex);
                ++slotCount;
            }
        }

        CnbByteWriter meshChunk;
        meshChunk.WriteBytes(meshRows.View());
        meshChunk.WriteBytes(partRows.View());
        meshChunk.WriteU32(slotCount);
        meshChunk.WriteBytes(slotValues.View());

        CnbByteWriter materialChunk;
        materialChunk.WriteU32(materialCount);
        materialChunk.WriteBytes(materialRows.View());

        // --- MSKL ---------------------------------------------------------------------------
        CnbByteWriter skeleton;
        if (model.skeleton.has_value())
        {
            const CnbModelSkeleton& s = *model.skeleton;
            const std::uint32_t jointCount = ToU32(s.hierarchy.size(), "a joint count");
            if (s.bindPose.size() != s.hierarchy.size() ||
                s.inverseBindPose.size() != s.hierarchy.size() ||
                (!s.rootPrefix.empty() && s.rootPrefix.size() != s.hierarchy.size()))
            {
                throw ContentLoadException(
                    "CNB Model: the skeleton's matrix arrays disagree with its joint count.");
            }
            skeleton.WriteU32(jointCount);
            skeleton.WriteU32(s.rootPrefix.empty() ? 0u : kSkeletonFlagHasRootPrefix);
            for (std::size_t j = 0; j < s.hierarchy.size(); ++j)
            {
                const std::int32_t parent = s.hierarchy[j];
                // Same rule and the same reason as the bone table above: AnimationPlayer composes
                // joint world transforms in one ascending pass.
                if (parent < -1 || (parent >= 0 && static_cast<std::size_t>(parent) >= j))
                {
                    throw ContentLoadException(
                        "CNB Model: skeleton joint " + std::to_string(j) + " names parent " +
                        std::to_string(parent) +
                        ", which is not earlier in the table. A skeleton must be ordered "
                        "parent-before-child.");
                }
                skeleton.WriteI32(parent);
            }
            for (const auto& matrix : s.bindPose) { WriteMatrix(skeleton, matrix); }
            for (const auto& matrix : s.inverseBindPose) { WriteMatrix(skeleton, matrix); }
            for (const auto& matrix : s.rootPrefix) { WriteMatrix(skeleton, matrix); }
        }

        // --- MANM ---------------------------------------------------------------------------
        CnbByteWriter animations;
        if (!model.animations.empty())
        {
            CnbByteWriter clipRows;
            CnbByteWriter trackRows;
            CnbByteWriter keyRows;
            std::uint32_t trackTotal = 0u;
            std::uint32_t keyTotal = 0u;

            for (const CnbModelAnimation& animation : model.animations)
            {
                clipRows.WriteU32(strings.Intern(animation.name));
                clipRows.WriteU32(static_cast<std::uint32_t>(animation.clip.TargetSpace));
                clipRows.WriteU32(trackTotal);
                clipRows.WriteU32(ToU32(animation.clip.Tracks.size(), "a track count"));
                clipRows.WriteF64(animation.clip.Duration.getTotalSecondsProperty());

                for (const BoneTrackEXT& track : animation.clip.Tracks)
                {
                    trackRows.WriteI32(track.BoneIndex);
                    trackRows.WriteU32(keyTotal);
                    trackRows.WriteU32(ToU32(track.Keys.size(), "a key count"));
                    ++trackTotal;
                    for (const KeyframeEXT& key : track.Keys)
                    {
                        WriteCnbKeyframe(keyRows, key);
                        ++keyTotal;
                    }
                }
            }

            animations.WriteU32(animationCount);
            animations.WriteU32(0u); // reserved, keeps the f64 in each clip row 8-byte aligned
            animations.WriteBytes(clipRows.View());
            animations.WriteU32(trackTotal);
            animations.WriteBytes(trackRows.View());
            animations.WriteU32(keyTotal);
            animations.WriteBytes(keyRows.View());
        }

        // --- MLIT ---------------------------------------------------------------------------
        CnbByteWriter lights;
        if (lightCount != 0u)
        {
            lights.WriteU32(lightCount);
            for (const CnbModelLight& light : model.lights)
            {
                for (const float value : light.direction) { lights.WriteF32(value); }
                for (const float value : light.diffuseColor) { lights.WriteF32(value); }
            }
        }

        // --- MSTR, last, because everything above interned into it --------------------------
        CnbByteWriter stringChunk;
        stringChunk.WriteU32(ToU32(strings.Values().size(), "a string count"));
        for (const std::string& value : strings.Values()) { stringChunk.WriteString(value); }

        CnbByteWriter header;
        std::uint32_t headerFlags = 0u;
        if (model.hasBoneHierarchy) { headerFlags |= kHeaderFlagHasBoneHierarchy; }
        if (model.appliesGltfLightingPolicy) { headerFlags |= kHeaderFlagGltfLightingPolicy; }
        header.WriteU32(headerFlags);
        header.WriteU32(boneCount);
        header.WriteU32(partCount);
        header.WriteU32(meshCount);
        header.WriteU32(lightCount);
        header.WriteU32(animationCount);

        CnbWriter writer(CnbAssetTypeId::Model, CnbModelSchemaVersion);
        writer.SetMetadata("Microsoft.Xna.Framework.Graphics.Model", contentName);
        writer.SetExternalReferences(references.Take());
        writer.AddChunk(CnbModelChunk::Header, header.Take(), CnbChunkFlags::Mandatory, 4u);
        writer.AddChunk(CnbModelChunk::Strings, stringChunk.Take(), CnbChunkFlags::Mandatory, 4u);
        if (boneCount != 0u)
        {
            writer.AddChunk(CnbModelChunk::Bones, bones.Take(), CnbChunkFlags::Mandatory, 4u);
        }
        writer.AddChunk(CnbModelChunk::Meshes, meshChunk.Take(), CnbChunkFlags::Mandatory, 4u);
        writer.AddChunk(CnbModelChunk::Materials, materialChunk.Take(), CnbChunkFlags::Mandatory, 4u);
        // Geometry gets 16-byte alignment: it is the data a future memory-mapped reader would want
        // to address in place, and 16 is the widest vector load any CNA target uses.
        for (std::vector<std::uint8_t>& chunk : vertexChunks)
        {
            writer.AddChunk(CnbModelChunk::VertexData, std::move(chunk), CnbChunkFlags::Mandatory, 16u);
        }
        for (std::vector<std::uint8_t>& chunk : indexChunks)
        {
            writer.AddChunk(CnbModelChunk::IndexData, std::move(chunk), CnbChunkFlags::Mandatory, 16u);
        }
        for (std::vector<std::uint8_t>& chunk : morphChunks)
        {
            writer.AddChunk(CnbModelChunk::MorphData, std::move(chunk), CnbChunkFlags::Mandatory, 8u);
        }
        if (model.skeleton.has_value())
        {
            writer.AddChunk(CnbModelChunk::Skeleton, skeleton.Take(), CnbChunkFlags::Mandatory, 4u);
        }
        if (!model.animations.empty())
        {
            writer.AddChunk(CnbModelChunk::Animations, animations.Take(), CnbChunkFlags::Mandatory, 8u);
        }
        if (lightCount != 0u)
        {
            writer.AddChunk(CnbModelChunk::Lights, lights.Take(), CnbChunkFlags::Mandatory, 4u);
        }
        return writer.Build();
    }
}

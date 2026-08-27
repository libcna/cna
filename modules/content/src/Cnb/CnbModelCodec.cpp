// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Cnb/CnbModelCodec.hpp"

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
        constexpr std::uint32_t kHeaderFlagHasBoneHierarchy = 1u << 0;
        constexpr std::uint32_t kHeaderFlagGltfLightingPolicy = 1u << 1;
        constexpr std::uint32_t kKnownHeaderFlags =
            kHeaderFlagHasBoneHierarchy | kHeaderFlagGltfLightingPolicy;
        constexpr std::uint32_t kPartFlagVertexColorEnabled = 1u << 0;
        constexpr std::uint32_t kPartFlagUnlit = 1u << 1;
        constexpr std::uint32_t kMaterialFlagDoubleSided = 1u << 0;
        constexpr std::uint32_t kSkeletonFlagHasRootPrefix = 1u << 0;
        constexpr std::uint32_t kMorphFlagRecomputeFlatNormals = 1u << 0;
        constexpr std::uint32_t kMorphTargetHasPositions = 1u << 0;
        constexpr std::uint32_t kMorphTargetHasNormals = 1u << 1;
        constexpr std::uint32_t kMorphTargetHasTangents = 1u << 2;
        constexpr std::uint32_t kMorphTrackStepInterpolation = 1u << 0;
        constexpr std::uint32_t kMorphTrackCubicSpline = 1u << 1;

        /// Every vertex layout CNA's importer produces is far below this; the bound exists so a
        /// corrupt stride cannot turn into an enormous per-vertex allocation before anything else
        /// gets a chance to reject it.
        constexpr std::uint32_t kMaxVertexStride = 4096u;

        /// Schema-level ceilings on the two counts whose in-memory footprint is many times their
        /// encoded size. The container's generic array limit alone would let a 64 MB chunk of
        /// four-byte presence words expand into a gigabyte of CnbMorphTarget objects, which is
        /// technically bounded and still not something a content loader should attempt. Both
        /// numbers match the ceilings ContentManager's own .cnj morph/skeleton readers already
        /// apply, so nothing a .cnj can express is refused here.
        constexpr std::uint32_t kMaxMorphTargets = 100000u;
        constexpr std::uint32_t kMaxMorphWeightKeys = 1000000u;

        /// The smallest number of bytes one morph weight key can occupy: an f64 time plus three
        /// zero-length stream counts. Passing the true minimum to ReadCount is what makes its fit
        /// check meaningful for a variable-length record.
        constexpr std::uint32_t kMinMorphWeightKeyBytes = 20u;

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

        std::array<float, 16> ReadMatrix(CnbByteReader& reader)
        {
            std::array<float, 16> matrix{};
            for (float& value : matrix) { value = reader.ReadF32(); }
            return matrix;
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

        /// Resolves a decoded XREF index back to a logical asset name, or "" for CnbNoIndex.
        std::string ResolveReference(const CnbDocument& document, std::uint32_t index,
                                     const char* what)
        {
            if (index == CnbNoIndex) { return {}; }
            return document.ExternalReferenceAt(index, what).logicalName;
        }

        void WriteMaterial(CnbByteWriter& writer, const CnbMaterial& material,
                           ExternalReferenceBuilder& references)
        {
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

            const std::uint32_t targetCount = reader.ReadCount(4u, "morph targets");
            if (targetCount > kMaxMorphTargets)
            {
                reader.Fail("declares " + std::to_string(targetCount) +
                            " morph targets, above this schema's ceiling of " +
                            std::to_string(kMaxMorphTargets) + ".");
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

            const std::uint32_t keyCount =
                reader.ReadCount(kMinMorphWeightKeyBytes, "morph weight keys");
            if (keyCount > kMaxMorphWeightKeys)
            {
                reader.Fail("declares " + std::to_string(keyCount) +
                            " morph weight keys, above this schema's ceiling of " +
                            std::to_string(kMaxMorphWeightKeys) + ".");
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
            if (bone.parent >= 0 && static_cast<std::uint32_t>(bone.parent) >= boneCount)
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
        std::map<std::vector<std::uint8_t>, std::uint32_t> materialIndexByBytes;

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
            if (mesh.parentBone >= 0 && static_cast<std::uint32_t>(mesh.parentBone) >= boneCount)
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

    CnbModelData DecodeModelFromCnb(const CnbDocument& document)
    {
        document.RequireAsset(CnbAssetTypeId::Model, CnbModelSchemaVersion);
        const CnbChunkId known[] = {
            CnbModelChunk::Header, CnbModelChunk::Strings, CnbModelChunk::Bones,
            CnbModelChunk::Meshes, CnbModelChunk::Materials, CnbModelChunk::VertexData,
            CnbModelChunk::IndexData, CnbModelChunk::MorphData, CnbModelChunk::Skeleton,
            CnbModelChunk::Animations, CnbModelChunk::Lights};
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

        if (morphChunks.size() > partCount)
        {
            throw ContentLoadException(
                "'" + document.Origin() + "' has more morph chunks than parts.");
        }

        return model;
    }
}

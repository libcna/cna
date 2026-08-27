// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnb.md CNBF-072 (Phase E tests): the Model asset schema's encode/decode half, which is
// deliberately free of any GraphicsDevice so the whole compiled-Model contract can be tested with
// no display, no renderer and no GPU. Round trip first, then the compiled-shape invariants the
// format exists for (declared geometry sizes, interned strings, deduplicated materials, external
// references kept external), then one negative test per way the tables can disagree.

#include <algorithm>
#include <cstdint>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbByteWriter.hpp"
#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbModelCodec.hpp"
#include "CNA/Content/Cnb/CnbModelData.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

using CNA::Content::Cnb::CnbDocument;
using CNA::Content::Cnb::CnbEffectKind;
using CNA::Content::Cnb::CnbModelAnimation;
using CNA::Content::Cnb::CnbModelBone;
using CNA::Content::Cnb::CnbModelData;
using CNA::Content::Cnb::CnbModelLight;
using CNA::Content::Cnb::CnbModelMesh;
using CNA::Content::Cnb::CnbModelPart;
using CNA::Content::Cnb::CnbModelSkeleton;
using CNA::Content::Cnb::CnbMorphData;
using CNA::Content::Cnb::CnbMorphTarget;
using CNA::Content::Cnb::CnbMorphWeightKey;
using CNA::Content::Cnb::CnbNoIndex;
using CNA::Content::Cnb::CnbWriter;
using CNA::Content::Cnb::DecodeModelFromCnb;
using CNA::Content::Cnb::EncodeModelToCnb;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Graphics::BoneTrackEXT;
using Microsoft::Xna::Framework::Graphics::ClipTargetSpaceEXT;
using Microsoft::Xna::Framework::Graphics::KeyframeEXT;

namespace CnbAssetTypeId = CNA::Content::Cnb::CnbAssetTypeId;
namespace CnbChunkFlags = CNA::Content::Cnb::CnbChunkFlags;
namespace CnbModelChunk = CNA::Content::Cnb::CnbModelChunk;

namespace
{
    std::vector<std::uint8_t> Ramp(std::size_t count, std::uint8_t start = 0u)
    {
        std::vector<std::uint8_t> out(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            out[i] = static_cast<std::uint8_t>(start + i);
        }
        return out;
    }

    CnbModelPart MakePart(const std::string& name, std::uint32_t stride, std::uint32_t vertices,
                          std::uint32_t indices, std::uint32_t indexElementSize)
    {
        CnbModelPart part;
        part.name = name;
        part.vertexStride = stride;
        part.vertexCount = vertices;
        part.indexCount = indices;
        part.indexElementSize = indexElementSize;
        part.primitiveTopology = 4u; // glTF mode 4, TRIANGLES
        part.primitiveCount = indices / 3u;
        part.vertexBytes = Ramp(static_cast<std::size_t>(stride) * vertices, 1u);
        // Real indices, every one addressing a vertex this part actually has. A byte ramp was
        // convenient and wrong: a ModelMeshPart draws `vertices` vertices from zero, so an index
        // at or above that is an out-of-range GPU fetch -- which the codec now refuses, and which
        // this fixture would otherwise have been quietly asserting was fine.
        part.indexBytes.assign(static_cast<std::size_t>(indexElementSize) * indices, 0u);
        for (std::uint32_t i = 0; i < indices; ++i)
        {
            const std::uint32_t value = vertices == 0u ? 0u : i % vertices;
            for (std::uint32_t b = 0; b < indexElementSize; ++b)
            {
                part.indexBytes[static_cast<std::size_t>(i) * indexElementSize + b] =
                    static_cast<std::uint8_t>((value >> (8u * b)) & 0xFFu);
            }
        }
        return part;
    }

    /// A deliberately busy model: two meshes, three parts, two of which share one material, a
    /// bone hierarchy, a skeleton with the optional root-prefix block, two clips, one light, and
    /// one morphed part with a weight track.
    CnbModelData MakeSampleModel()
    {
        CnbModelData model;
        model.hasBoneHierarchy = true;
        model.bones = {
            CnbModelBone{"Root", -1, {}},
            CnbModelBone{"Hips", 0, {}},
            CnbModelBone{"Head", 1, {}},
        };
        model.bones[1].transform[12] = 3.5f;
        model.bones[2].transform[13] = -2.25f;

        CnbModelPart body = MakePart("Body", 32u, 4u, 6u, 2u);
        body.effectKind = CnbEffectKind::BasicEffect;
        body.material.baseColorTexture = "Textures/skin";
        body.material.baseColorFactor = {0.5f, 0.25f, 0.75f, 0.5f};
        body.material.metallicFactor = 0.125f;
        body.material.samplers[0].filter = 2u;
        body.material.samplers[0].declared = true;
        body.material.textureCoordinateSets[0] = 1u;
        body.material.textureTransforms[0].offsetX = 0.1f;
        body.material.textureTransforms[0].scaleY = 2.0f;
        body.vertexColorEnabled = true;

        CnbModelPart trim = MakePart("Trim", 32u, 3u, 3u, 2u);
        trim.effectKind = CnbEffectKind::BasicEffect;
        trim.material = body.material; // shares a material with Body -- exercises interning

        CnbModelPart face = MakePart("Face", 48u, 5u, 9u, 4u);
        face.effectKind = CnbEffectKind::PbrEffect;
        face.material.baseColorTexture = "Textures/face";
        face.material.normalMap = "Textures/face_n";
        face.material.alphaMode = 1u;
        face.material.alphaCutoff = 0.25f;
        face.material.doubleSided = true;
        face.unlit = false;

        CnbMorphData morph;
        morph.vertexCount = 5u;
        morph.recomputeFlatNormals = true;
        CnbMorphTarget smile;
        smile.positionDeltas.assign(15u, 0.5f);
        smile.normalDeltas.assign(15u, -0.25f);
        CnbMorphTarget frown;
        frown.positionDeltas.assign(15u, 1.5f);
        frown.tangentDeltas.assign(15u, 0.75f);
        morph.targets = {smile, frown};
        morph.weights = {0.25f, 0.0f};
        morph.weightTrackStepInterpolation = true;
        CnbMorphWeightKey key;
        key.timeSeconds = 0.5;
        key.weights = {1.0f, 0.0f};
        key.inTangent = {0.1f, 0.2f};
        morph.weightTrackKeys = {key};
        face.morph = morph;

        model.parts = {body, trim, face};
        model.meshes = {
            CnbModelMesh{"Torso", 1, {0u, 1u}},
            CnbModelMesh{"Head", 2, {2u}},
        };

        CnbModelSkeleton skeleton;
        skeleton.hierarchy = {-1, 0};
        skeleton.bindPose.resize(2);
        skeleton.inverseBindPose.resize(2);
        skeleton.rootPrefix.resize(2);
        for (int i = 0; i < 16; ++i)
        {
            skeleton.bindPose[0][static_cast<std::size_t>(i)] = static_cast<float>(i);
            skeleton.inverseBindPose[1][static_cast<std::size_t>(i)] = static_cast<float>(-i);
            skeleton.rootPrefix[1][static_cast<std::size_t>(i)] = static_cast<float>(i) * 0.5f;
        }
        model.skeleton = skeleton;

        CnbModelAnimation walk;
        walk.name = "Walk";
        walk.clip.Duration = System::TimeSpan::FromSeconds(1.5);
        walk.clip.TargetSpace = ClipTargetSpaceEXT::JointPalette;
        BoneTrackEXT track;
        track.BoneIndex = 1;
        KeyframeEXT k0;
        k0.Time = System::TimeSpan::FromSeconds(0.0);
        k0.Translation = Microsoft::Xna::Framework::Vector3(1.0f, 2.0f, 3.0f);
        KeyframeEXT k1;
        k1.Time = System::TimeSpan::FromSeconds(1.5);
        k1.Scale = Microsoft::Xna::Framework::Vector3(2.0f, 2.0f, 2.0f);
        track.Keys = {k0, k1};
        walk.clip.Tracks = {track};

        CnbModelAnimation idle;
        idle.name = "Idle";
        idle.clip.Duration = System::TimeSpan::FromSeconds(4.0);
        idle.clip.TargetSpace = ClipTargetSpaceEXT::SceneNode;

        model.animations = {walk, idle};
        model.lights = {CnbModelLight{{0.0f, -1.0f, 0.0f}, {1.0f, 0.9f, 0.8f}}};
        return model;
    }

    CnbDocument Parse(const std::vector<std::uint8_t>& bytes)
    {
        return CnbDocument::Parse(bytes, "model.cnb");
    }

    void ExpectSamePart(const CnbModelPart& a, const CnbModelPart& b, const char* label)
    {
        EXPECT_EQ(b.name, a.name) << label;
        EXPECT_EQ(b.vertexStride, a.vertexStride) << label;
        EXPECT_EQ(b.vertexCount, a.vertexCount) << label;
        EXPECT_EQ(b.indexCount, a.indexCount) << label;
        EXPECT_EQ(b.indexElementSize, a.indexElementSize) << label;
        EXPECT_EQ(b.primitiveTopology, a.primitiveTopology) << label;
        EXPECT_EQ(b.primitiveCount, a.primitiveCount) << label;
        EXPECT_EQ(b.effectKind, a.effectKind) << label;
        EXPECT_EQ(b.externalEffect, a.externalEffect) << label;
        EXPECT_EQ(b.vertexColorEnabled, a.vertexColorEnabled) << label;
        EXPECT_EQ(b.unlit, a.unlit) << label;
        EXPECT_EQ(b.vertexBytes, a.vertexBytes) << label;
        EXPECT_EQ(b.indexBytes, a.indexBytes) << label;

        EXPECT_EQ(b.material.baseColorTexture, a.material.baseColorTexture) << label;
        EXPECT_EQ(b.material.normalMap, a.material.normalMap) << label;
        EXPECT_EQ(b.material.baseColorFactor, a.material.baseColorFactor) << label;
        EXPECT_FLOAT_EQ(b.material.metallicFactor, a.material.metallicFactor) << label;
        EXPECT_FLOAT_EQ(b.material.alphaCutoff, a.material.alphaCutoff) << label;
        EXPECT_EQ(b.material.alphaMode, a.material.alphaMode) << label;
        EXPECT_EQ(b.material.doubleSided, a.material.doubleSided) << label;
        EXPECT_EQ(b.material.textureCoordinateSets, a.material.textureCoordinateSets) << label;
        EXPECT_FLOAT_EQ(b.material.textureTransforms[0].offsetX,
                        a.material.textureTransforms[0].offsetX) << label;
        EXPECT_FLOAT_EQ(b.material.textureTransforms[0].scaleY,
                        a.material.textureTransforms[0].scaleY) << label;
        EXPECT_EQ(b.material.samplers[0].filter, a.material.samplers[0].filter) << label;
        EXPECT_EQ(b.material.samplers[0].declared, a.material.samplers[0].declared) << label;

        ASSERT_EQ(b.morph.has_value(), a.morph.has_value()) << label;
        if (a.morph.has_value())
        {
            EXPECT_EQ(b.morph->vertexCount, a.morph->vertexCount) << label;
            EXPECT_EQ(b.morph->recomputeFlatNormals, a.morph->recomputeFlatNormals) << label;
            ASSERT_EQ(b.morph->targets.size(), a.morph->targets.size()) << label;
            for (std::size_t t = 0; t < a.morph->targets.size(); ++t)
            {
                EXPECT_EQ(b.morph->targets[t].positionDeltas, a.morph->targets[t].positionDeltas);
                EXPECT_EQ(b.morph->targets[t].normalDeltas, a.morph->targets[t].normalDeltas);
                EXPECT_EQ(b.morph->targets[t].tangentDeltas, a.morph->targets[t].tangentDeltas);
            }
            EXPECT_EQ(b.morph->weights, a.morph->weights) << label;
            EXPECT_EQ(b.morph->weightTrackStepInterpolation,
                      a.morph->weightTrackStepInterpolation) << label;
            ASSERT_EQ(b.morph->weightTrackKeys.size(), a.morph->weightTrackKeys.size()) << label;
            for (std::size_t k = 0; k < a.morph->weightTrackKeys.size(); ++k)
            {
                EXPECT_DOUBLE_EQ(b.morph->weightTrackKeys[k].timeSeconds,
                                 a.morph->weightTrackKeys[k].timeSeconds);
                EXPECT_EQ(b.morph->weightTrackKeys[k].weights, a.morph->weightTrackKeys[k].weights);
                EXPECT_EQ(b.morph->weightTrackKeys[k].inTangent,
                          a.morph->weightTrackKeys[k].inTangent);
                EXPECT_EQ(b.morph->weightTrackKeys[k].outTangent,
                          a.morph->weightTrackKeys[k].outTangent);
            }
        }
    }
}

TEST(CnbModelCodecTest, RoundTripsAFullyPopulatedModel)
{
    const CnbModelData original = MakeSampleModel();
    const CnbModelData decoded = DecodeModelFromCnb(Parse(EncodeModelToCnb(original, "Models/hero")));

    EXPECT_EQ(decoded.hasBoneHierarchy, original.hasBoneHierarchy);
    ASSERT_EQ(decoded.bones.size(), original.bones.size());
    for (std::size_t b = 0; b < original.bones.size(); ++b)
    {
        EXPECT_EQ(decoded.bones[b].name, original.bones[b].name);
        EXPECT_EQ(decoded.bones[b].parent, original.bones[b].parent);
        EXPECT_EQ(decoded.bones[b].transform, original.bones[b].transform);
    }

    ASSERT_EQ(decoded.parts.size(), original.parts.size());
    ExpectSamePart(original.parts[0], decoded.parts[0], "Body");
    ExpectSamePart(original.parts[1], decoded.parts[1], "Trim");
    ExpectSamePart(original.parts[2], decoded.parts[2], "Face");

    ASSERT_EQ(decoded.meshes.size(), original.meshes.size());
    for (std::size_t m = 0; m < original.meshes.size(); ++m)
    {
        EXPECT_EQ(decoded.meshes[m].name, original.meshes[m].name);
        EXPECT_EQ(decoded.meshes[m].parentBone, original.meshes[m].parentBone);
        EXPECT_EQ(decoded.meshes[m].partIndices, original.meshes[m].partIndices);
    }

    ASSERT_TRUE(decoded.skeleton.has_value());
    EXPECT_EQ(decoded.skeleton->hierarchy, original.skeleton->hierarchy);
    EXPECT_EQ(decoded.skeleton->bindPose, original.skeleton->bindPose);
    EXPECT_EQ(decoded.skeleton->inverseBindPose, original.skeleton->inverseBindPose);
    EXPECT_EQ(decoded.skeleton->rootPrefix, original.skeleton->rootPrefix);

    ASSERT_EQ(decoded.animations.size(), 2u);
    EXPECT_EQ(decoded.animations[0].name, "Walk");
    EXPECT_EQ(decoded.animations[0].clip.TargetSpace, ClipTargetSpaceEXT::JointPalette);
    EXPECT_EQ(decoded.animations[0].clip.Duration.getTicksProperty(),
              original.animations[0].clip.Duration.getTicksProperty());
    ASSERT_EQ(decoded.animations[0].clip.Tracks.size(), 1u);
    ASSERT_EQ(decoded.animations[0].clip.Tracks[0].Keys.size(), 2u);
    EXPECT_FLOAT_EQ(decoded.animations[0].clip.Tracks[0].Keys[0].Translation.X, 1.0f);
    EXPECT_FLOAT_EQ(decoded.animations[0].clip.Tracks[0].Keys[1].Scale.Z, 2.0f);
    EXPECT_EQ(decoded.animations[1].name, "Idle");
    EXPECT_EQ(decoded.animations[1].clip.TargetSpace, ClipTargetSpaceEXT::SceneNode);
    EXPECT_TRUE(decoded.animations[1].clip.Tracks.empty());

    ASSERT_EQ(decoded.lights.size(), 1u);
    EXPECT_FLOAT_EQ(decoded.lights[0].direction[1], -1.0f);
    EXPECT_FLOAT_EQ(decoded.lights[0].diffuseColor[2], 0.8f);
}

TEST(CnbModelCodecTest, EncodingIsDeterministic)
{
    const CnbModelData model = MakeSampleModel();
    EXPECT_EQ(EncodeModelToCnb(model, "Models/hero"), EncodeModelToCnb(model, "Models/hero"));
}

TEST(CnbModelCodecTest, GeometryLivesInItsOwnAlignedChunksOnePerPart)
{
    const CnbDocument doc = Parse(EncodeModelToCnb(MakeSampleModel()));

    const std::vector<std::size_t> vertexChunks = doc.FindAll(CnbModelChunk::VertexData);
    const std::vector<std::size_t> indexChunks = doc.FindAll(CnbModelChunk::IndexData);
    ASSERT_EQ(vertexChunks.size(), 3u);
    ASSERT_EQ(indexChunks.size(), 3u);
    EXPECT_EQ(doc.FindAll(CnbModelChunk::MorphData).size(), 1u);

    // Sizes come straight from the declared stride and count, and the offsets are 16-byte
    // aligned so a future memory-mapped reader can address them in place.
    EXPECT_EQ(doc.ChunkData(vertexChunks[0]).size(), 32u * 4u);
    EXPECT_EQ(doc.ChunkData(vertexChunks[2]).size(), 48u * 5u);
    EXPECT_EQ(doc.ChunkData(indexChunks[0]).size(), 2u * 6u);
    EXPECT_EQ(doc.ChunkData(indexChunks[2]).size(), 4u * 9u);
    for (const std::size_t chunk : vertexChunks)
    {
        EXPECT_EQ(doc.ChunkAt(chunk).alignment, 16u);
        EXPECT_EQ(doc.ChunkAt(chunk).offset % 16u, 0u);
    }
}

TEST(CnbModelCodecTest, ExternalAssetsStayExternalAndAreListedOnce)
{
    // Body and Trim share one texture; the reference table must hold it once, so a hundred models
    // sharing a texture still means one ContentManager load, not one copy per part.
    const CnbDocument doc = Parse(EncodeModelToCnb(MakeSampleModel()));
    const std::vector<CNA::Content::Cnb::CnbExternalReference>& refs = doc.ExternalReferences();

    ASSERT_EQ(refs.size(), 3u);
    std::vector<std::string> names;
    for (const auto& ref : refs)
    {
        names.push_back(ref.logicalName);
        EXPECT_EQ(ref.expectedAssetTypeId, CnbAssetTypeId::Texture2D);
    }
    EXPECT_NE(std::find(names.begin(), names.end(), "Textures/skin"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "Textures/face"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "Textures/face_n"), names.end());

    // No texture bytes are embedded: the whole file is descriptor tables plus geometry.
    std::uint64_t geometryBytes = 0;
    for (std::size_t i = 0; i < doc.ChunkCount(); ++i)
    {
        if (doc.ChunkAt(i).type == CnbModelChunk::VertexData ||
            doc.ChunkAt(i).type == CnbModelChunk::IndexData)
        {
            geometryBytes += doc.ChunkAt(i).storedSize;
        }
    }
    EXPECT_EQ(geometryBytes, 32u * 4u + 32u * 3u + 48u * 5u + 2u * 6u + 2u * 3u + 4u * 9u);
}

TEST(CnbModelCodecTest, IdenticalMaterialsAreStoredOnce)
{
    const CnbDocument doc = Parse(EncodeModelToCnb(MakeSampleModel()));
    auto reader = doc.OpenChunk(doc.RequireSingle(CnbModelChunk::Materials));
    // Three parts, two distinct materials.
    EXPECT_EQ(reader.ReadU32(), 2u);
}

TEST(CnbModelCodecTest, AModelWithNoBonesNoSkeletonNoAnimationsAndNoLightsRoundTrips)
{
    CnbModelData model;
    model.parts = {MakePart("Only", 16u, 3u, 3u, 2u)};
    model.meshes = {CnbModelMesh{"Only", -1, {0u}}};

    const CnbDocument doc = Parse(EncodeModelToCnb(model));
    EXPECT_FALSE(doc.FindSingle(CnbModelChunk::Bones).has_value());
    EXPECT_FALSE(doc.FindSingle(CnbModelChunk::Skeleton).has_value());
    EXPECT_FALSE(doc.FindSingle(CnbModelChunk::Animations).has_value());
    EXPECT_FALSE(doc.FindSingle(CnbModelChunk::Lights).has_value());

    const CnbModelData decoded = DecodeModelFromCnb(doc);
    EXPECT_TRUE(decoded.bones.empty());
    EXPECT_FALSE(decoded.hasBoneHierarchy);
    EXPECT_FALSE(decoded.skeleton.has_value());
    ASSERT_EQ(decoded.parts.size(), 1u);
    ASSERT_EQ(decoded.meshes.size(), 1u);
    EXPECT_EQ(decoded.meshes[0].parentBone, -1);
}

TEST(CnbModelCodecTest, ASkeletonWithoutTheOptionalRootPrefixStaysDistinguishableFromATruncatedOne)
{
    // The .skeleton.bin sidecar signalled this block by leftover bytes, so "deliberately absent"
    // and "file truncated" were the same observation. The compiled form states it.
    CnbModelData model;
    model.parts = {MakePart("Only", 16u, 3u, 3u, 2u)};
    model.meshes = {CnbModelMesh{"Only", -1, {0u}}};
    CnbModelSkeleton skeleton;
    skeleton.hierarchy = {-1, 0, 1};
    skeleton.bindPose.resize(3);
    skeleton.inverseBindPose.resize(3);
    model.skeleton = skeleton;

    const CnbModelData decoded = DecodeModelFromCnb(Parse(EncodeModelToCnb(model)));
    ASSERT_TRUE(decoded.skeleton.has_value());
    EXPECT_EQ(decoded.skeleton->hierarchy.size(), 3u);
    EXPECT_TRUE(decoded.skeleton->rootPrefix.empty());
}

TEST(CnbModelCodecTest, AnExternalEffectRoundTripsAsAReference)
{
    CnbModelData model;
    CnbModelPart part = MakePart("Only", 16u, 3u, 3u, 2u);
    part.effectKind = CnbEffectKind::External;
    part.externalEffect = "Effects/water";
    model.parts = {part};
    model.meshes = {CnbModelMesh{"Only", -1, {0u}}};

    const CnbDocument doc = Parse(EncodeModelToCnb(model));
    const CnbModelData decoded = DecodeModelFromCnb(doc);
    EXPECT_EQ(decoded.parts[0].effectKind, CnbEffectKind::External);
    EXPECT_EQ(decoded.parts[0].externalEffect, "Effects/water");

    bool found = false;
    for (const auto& ref : doc.ExternalReferences())
    {
        if (ref.logicalName == "Effects/water")
        {
            EXPECT_EQ(ref.expectedAssetTypeId, CnbAssetTypeId::Effect);
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

// --------------------------------------------------------------------------------------------
// Encoder refusals -- an inconsistent CnbModelData must not be able to produce a valid-looking file
// --------------------------------------------------------------------------------------------

TEST(CnbModelCodecTest, EncoderRefusesGeometryThatDisagreesWithItsDeclaredCounts)
{
    {
        CnbModelData model;
        CnbModelPart part = MakePart("Only", 16u, 3u, 3u, 2u);
        part.vertexBytes.pop_back();
        model.parts = {part};
        model.meshes = {CnbModelMesh{"Only", -1, {0u}}};
        EXPECT_THROW((void)EncodeModelToCnb(model), ContentLoadException);
    }
    {
        CnbModelData model;
        CnbModelPart part = MakePart("Only", 16u, 3u, 3u, 2u);
        part.indexBytes.push_back(0u);
        model.parts = {part};
        model.meshes = {CnbModelMesh{"Only", -1, {0u}}};
        EXPECT_THROW((void)EncodeModelToCnb(model), ContentLoadException);
    }
}

TEST(CnbModelCodecTest, EncoderRefusesUnusableStridesAndIndexWidths)
{
    for (const std::uint32_t stride : {0u, 100000u})
    {
        CnbModelData model;
        CnbModelPart part = MakePart("Only", 16u, 0u, 0u, 2u);
        part.vertexStride = stride;
        model.parts = {part};
        model.meshes = {CnbModelMesh{"Only", -1, {0u}}};
        EXPECT_THROW((void)EncodeModelToCnb(model), ContentLoadException) << "stride " << stride;
    }
    for (const std::uint32_t width : {0u, 1u, 3u, 8u})
    {
        CnbModelData model;
        CnbModelPart part = MakePart("Only", 16u, 3u, 0u, 2u);
        part.indexElementSize = width;
        model.parts = {part};
        model.meshes = {CnbModelMesh{"Only", -1, {0u}}};
        EXPECT_THROW((void)EncodeModelToCnb(model), ContentLoadException) << "width " << width;
    }
}

TEST(CnbModelCodecTest, EncoderRefusesOutOfRangeIndices)
{
    {
        CnbModelData model;
        model.parts = {MakePart("Only", 16u, 3u, 3u, 2u)};
        model.meshes = {CnbModelMesh{"Only", -1, {5u}}};
        EXPECT_THROW((void)EncodeModelToCnb(model), ContentLoadException);
    }
    {
        CnbModelData model;
        model.parts = {MakePart("Only", 16u, 3u, 3u, 2u)};
        model.meshes = {CnbModelMesh{"Only", 4, {0u}}};
        EXPECT_THROW((void)EncodeModelToCnb(model), ContentLoadException);
    }
    {
        CnbModelData model;
        model.bones = {CnbModelBone{"Root", 9, {}}};
        model.parts = {MakePart("Only", 16u, 3u, 3u, 2u)};
        model.meshes = {CnbModelMesh{"Only", -1, {0u}}};
        EXPECT_THROW((void)EncodeModelToCnb(model), ContentLoadException);
    }
}

TEST(CnbModelCodecTest, EncoderRefusesMorphDataThatDoesNotCoverItsPart)
{
    CnbModelData model;
    CnbModelPart part = MakePart("Only", 16u, 3u, 3u, 2u);
    CnbMorphData morph;
    morph.vertexCount = 7u; // not the part's 3
    CnbMorphTarget target;
    target.positionDeltas.assign(21u, 0.0f);
    morph.targets = {target};
    part.morph = morph;
    model.parts = {part};
    model.meshes = {CnbModelMesh{"Only", -1, {0u}}};
    EXPECT_THROW((void)EncodeModelToCnb(model), ContentLoadException);
}

TEST(CnbModelCodecTest, EncoderRefusesASkeletonWhoseArraysDisagree)
{
    CnbModelData model;
    model.parts = {MakePart("Only", 16u, 3u, 3u, 2u)};
    model.meshes = {CnbModelMesh{"Only", -1, {0u}}};
    CnbModelSkeleton skeleton;
    skeleton.hierarchy = {-1, 0};
    skeleton.bindPose.resize(2);
    skeleton.inverseBindPose.resize(1);
    model.skeleton = skeleton;
    EXPECT_THROW((void)EncodeModelToCnb(model), ContentLoadException);
}

// --------------------------------------------------------------------------------------------
// Decoder refusals -- a hand-built file whose tables disagree
// --------------------------------------------------------------------------------------------

namespace
{
    /// Rebuilds a Model .cnb from raw chunk payloads taken out of a valid file, letting a test
    /// change exactly one of them while everything else stays consistent.
    class ModelFileEditor
    {
    public:
        explicit ModelFileEditor(const CnbModelData& model)
            : bytes_(EncodeModelToCnb(model)), document_(Parse(bytes_))
        {
        }

        [[nodiscard]] std::vector<std::uint8_t> Chunk(CNA::Content::Cnb::CnbChunkId type) const
        {
            const auto data = document_.ChunkData(document_.RequireSingle(type));
            return std::vector<std::uint8_t>(data.begin(), data.end());
        }

        /// Rebuilds the file with `replacement` substituted for the first chunk of `type`.
        [[nodiscard]] std::vector<std::uint8_t> Rebuild(
            CNA::Content::Cnb::CnbChunkId type,
            const std::vector<std::uint8_t>& replacement) const
        {
            CnbWriter writer(CnbAssetTypeId::Model, 1u);
            writer.SetExternalReferences(document_.ExternalReferences());
            bool replaced = false;
            for (std::size_t i = 0; i < document_.ChunkCount(); ++i)
            {
                const auto& entry = document_.ChunkAt(i);
                if (entry.type == CNA::Content::Cnb::CnbContainerChunk::Metadata ||
                    entry.type == CNA::Content::Cnb::CnbContainerChunk::ExternalReferences)
                {
                    continue;
                }
                const auto data = document_.ChunkData(i);
                std::vector<std::uint8_t> payload(data.begin(), data.end());
                if (entry.type == type && !replaced)
                {
                    payload = replacement;
                    replaced = true;
                }
                writer.AddChunk(entry.type, std::move(payload), entry.flags, entry.alignment);
            }
            return writer.Build();
        }

    private:
        std::vector<std::uint8_t> bytes_;
        CnbDocument document_;
    };

    CnbModelData MakeSmallModel()
    {
        CnbModelData model;
        model.parts = {MakePart("Only", 16u, 3u, 3u, 2u)};
        model.meshes = {CnbModelMesh{"Only", -1, {0u}}};
        return model;
    }

    void PatchU32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value)
    {
        for (int i = 0; i < 4; ++i)
        {
            bytes[offset + static_cast<std::size_t>(i)] =
                static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu);
        }
    }
}

TEST(CnbModelCodecTest, DecoderRejectsAWrongAssetTypeOrFutureSchemaVersion)
{
    {
        CnbWriter writer(CnbAssetTypeId::Curve, 1u);
        writer.AddChunk(CnbModelChunk::Header, std::vector<std::uint8_t>(24u, 0u),
                        CnbChunkFlags::Mandatory, 4u);
        const CnbDocument doc = CnbDocument::Parse(writer.Build(), "wrong.cnb");
        EXPECT_THROW((void)DecodeModelFromCnb(doc), ContentLoadException);
    }
    {
        CnbWriter writer(CnbAssetTypeId::Model, 2u);
        writer.AddChunk(CnbModelChunk::Header, std::vector<std::uint8_t>(24u, 0u),
                        CnbChunkFlags::Mandatory, 4u);
        const CnbDocument doc = CnbDocument::Parse(writer.Build(), "future.cnb");
        EXPECT_THROW((void)DecodeModelFromCnb(doc), ContentLoadException);
    }
}

TEST(CnbModelCodecTest, DecoderRejectsAHeaderCountThatDisagreesWithTheChunks)
{
    const ModelFileEditor editor{MakeSmallModel()};
    std::vector<std::uint8_t> header = editor.Chunk(CnbModelChunk::Header);

    // partCount says 2, but the file has one part row and one vertex/index chunk.
    PatchU32(header, 8u, 2u);
    const CnbDocument doc = Parse(editor.Rebuild(CnbModelChunk::Header, header));
    EXPECT_THROW((void)DecodeModelFromCnb(doc), ContentLoadException);
}

TEST(CnbModelCodecTest, DecoderRejectsABoneCountThatDisagreesWithTheBoneChunk)
{
    CnbModelData model = MakeSmallModel();
    model.bones = {CnbModelBone{"Root", -1, {}}, CnbModelBone{"Child", 0, {}}};
    model.hasBoneHierarchy = true;
    const ModelFileEditor editor{model};

    std::vector<std::uint8_t> bones = editor.Chunk(CnbModelChunk::Bones);
    bones.resize(bones.size() - 4u);
    const CnbDocument doc = Parse(editor.Rebuild(CnbModelChunk::Bones, bones));
    EXPECT_THROW((void)DecodeModelFromCnb(doc), ContentLoadException);
}

TEST(CnbModelCodecTest, DecoderRejectsAnOutOfRangeStringIndex)
{
    const ModelFileEditor editor{MakeSmallModel()};
    std::vector<std::uint8_t> meshes = editor.Chunk(CnbModelChunk::Meshes);
    PatchU32(meshes, 0u, 9999u); // the mesh row's name index
    const CnbDocument doc = Parse(editor.Rebuild(CnbModelChunk::Meshes, meshes));
    EXPECT_THROW((void)DecodeModelFromCnb(doc), ContentLoadException);
}

TEST(CnbModelCodecTest, DecoderRejectsAPartWhoseGeometryChunkIsTheWrongSize)
{
    CnbModelData model = MakeSmallModel();
    const ModelFileEditor editor{model};
    // The part row's vertexCount field: mesh row (16 bytes) + 5 u32s into the part row.
    std::vector<std::uint8_t> meshes = editor.Chunk(CnbModelChunk::Meshes);
    PatchU32(meshes, 16u + 5u * 4u, 4u); // claims 4 vertices; the chunk holds 3
    const CnbDocument doc = Parse(editor.Rebuild(CnbModelChunk::Meshes, meshes));
    EXPECT_THROW((void)DecodeModelFromCnb(doc), ContentLoadException);
}

TEST(CnbModelCodecTest, DecoderRejectsAnOutOfRangeChunkOrdinal)
{
    const ModelFileEditor editor{MakeSmallModel()};
    std::vector<std::uint8_t> meshes = editor.Chunk(CnbModelChunk::Meshes);
    PatchU32(meshes, 16u + 1u * 4u, 5u); // vertex chunk ordinal 5; the file has one
    const CnbDocument doc = Parse(editor.Rebuild(CnbModelChunk::Meshes, meshes));
    EXPECT_THROW((void)DecodeModelFromCnb(doc), ContentLoadException);
}

TEST(CnbModelCodecTest, DecoderRejectsAnUnknownEffectKindOrOutOfRangeMaterialIndex)
{
    {
        const ModelFileEditor editor{MakeSmallModel()};
        std::vector<std::uint8_t> meshes = editor.Chunk(CnbModelChunk::Meshes);
        PatchU32(meshes, 16u + 10u * 4u, 99u); // effect kind
        const CnbDocument doc = Parse(editor.Rebuild(CnbModelChunk::Meshes, meshes));
        EXPECT_THROW((void)DecodeModelFromCnb(doc), ContentLoadException);
    }
    {
        const ModelFileEditor editor{MakeSmallModel()};
        std::vector<std::uint8_t> meshes = editor.Chunk(CnbModelChunk::Meshes);
        PatchU32(meshes, 16u + 12u * 4u, 7u); // material index
        const CnbDocument doc = Parse(editor.Rebuild(CnbModelChunk::Meshes, meshes));
        EXPECT_THROW((void)DecodeModelFromCnb(doc), ContentLoadException);
    }
}

TEST(CnbModelCodecTest, DecoderRejectsAnOutOfRangeMeshPartSlot)
{
    const ModelFileEditor editor{MakeSmallModel()};
    std::vector<std::uint8_t> meshes = editor.Chunk(CnbModelChunk::Meshes);
    // Last u32 in the chunk is the single slot value.
    PatchU32(meshes, meshes.size() - 4u, 3u);
    const CnbDocument doc = Parse(editor.Rebuild(CnbModelChunk::Meshes, meshes));
    EXPECT_THROW((void)DecodeModelFromCnb(doc), ContentLoadException);
}

TEST(CnbModelCodecTest, DecoderRejectsAMissingMandatoryChunk)
{
    for (const CNA::Content::Cnb::CnbChunkId omit :
         {CnbModelChunk::Header, CnbModelChunk::Strings, CnbModelChunk::Meshes,
          CnbModelChunk::Materials})
    {
        const std::vector<std::uint8_t> bytes = EncodeModelToCnb(MakeSmallModel());
        const CnbDocument source = Parse(bytes);

        CnbWriter writer(CnbAssetTypeId::Model, 1u);
        if (!source.ExternalReferences().empty())
        {
            writer.SetExternalReferences(source.ExternalReferences());
        }
        for (std::size_t i = 0; i < source.ChunkCount(); ++i)
        {
            const auto& entry = source.ChunkAt(i);
            if (entry.type == omit) { continue; }
            if (entry.type == CNA::Content::Cnb::CnbContainerChunk::Metadata ||
                entry.type == CNA::Content::Cnb::CnbContainerChunk::ExternalReferences)
            {
                continue;
            }
            const auto data = source.ChunkData(i);
            writer.AddChunk(entry.type, std::vector<std::uint8_t>(data.begin(), data.end()),
                            entry.flags, entry.alignment);
        }
        const CnbDocument doc = CnbDocument::Parse(writer.Build(), "missing.cnb");
        EXPECT_THROW((void)DecodeModelFromCnb(doc), ContentLoadException)
            << "omitting " << CNA::Content::Cnb::ChunkIdToString(omit);
    }
}

TEST(CnbModelCodecTest, DecoderRejectsAnUnknownMandatoryChunk)
{
    const std::vector<std::uint8_t> bytes = EncodeModelToCnb(MakeSmallModel());
    const CnbDocument source = Parse(bytes);

    CnbWriter writer(CnbAssetTypeId::Model, 1u);
    if (!source.ExternalReferences().empty())
    {
        writer.SetExternalReferences(source.ExternalReferences());
    }
    for (std::size_t i = 0; i < source.ChunkCount(); ++i)
    {
        const auto& entry = source.ChunkAt(i);
        if (entry.type == CNA::Content::Cnb::CnbContainerChunk::Metadata ||
            entry.type == CNA::Content::Cnb::CnbContainerChunk::ExternalReferences)
        {
            continue;
        }
        const auto data = source.ChunkData(i);
        writer.AddChunk(entry.type, std::vector<std::uint8_t>(data.begin(), data.end()),
                        entry.flags, entry.alignment);
    }
    writer.AddChunk(CNA::Content::Cnb::MakeChunkId('m', 'f', 'u', 't'), {1, 2, 3},
                    CnbChunkFlags::Mandatory, 4u);
    const CnbDocument doc = CnbDocument::Parse(writer.Build(), "future.cnb");
    EXPECT_THROW((void)DecodeModelFromCnb(doc), ContentLoadException);
}

TEST(CnbModelCodecTest, DecoderRejectsAnOutOfRangeExternalReferenceIndex)
{
    CnbModelData model = MakeSmallModel();
    model.parts[0].material.baseColorTexture = "Textures/wall";
    const ModelFileEditor editor{model};

    std::vector<std::uint8_t> materials = editor.Chunk(CnbModelChunk::Materials);
    PatchU32(materials, 4u, 42u); // the base-colour texture's XREF index
    const CnbDocument doc = Parse(editor.Rebuild(CnbModelChunk::Materials, materials));
    EXPECT_THROW((void)DecodeModelFromCnb(doc), ContentLoadException);
}

TEST(CnbModelCodecTest, DecoderRejectsAnOutOfRangeAlphaModeOrTexCoordSet)
{
    {
        const ModelFileEditor editor{MakeSmallModel()};
        std::vector<std::uint8_t> materials = editor.Chunk(CnbModelChunk::Materials);
        PatchU32(materials, 4u + 104u, 9u); // alphaMode, 104 bytes into the record
        const CnbDocument doc = Parse(editor.Rebuild(CnbModelChunk::Materials, materials));
        EXPECT_THROW((void)DecodeModelFromCnb(doc), ContentLoadException);
    }
    {
        const ModelFileEditor editor{MakeSmallModel()};
        std::vector<std::uint8_t> materials = editor.Chunk(CnbModelChunk::Materials);
        materials[4u + 108u] = 5u; // first texture-coordinate set byte
        const CnbDocument doc = Parse(editor.Rebuild(CnbModelChunk::Materials, materials));
        EXPECT_THROW((void)DecodeModelFromCnb(doc), ContentLoadException);
    }
    {
        const ModelFileEditor editor{MakeSmallModel()};
        std::vector<std::uint8_t> materials = editor.Chunk(CnbModelChunk::Materials);
        materials[4u + 115u] = 1u; // the reserved padding byte
        const CnbDocument doc = Parse(editor.Rebuild(CnbModelChunk::Materials, materials));
        EXPECT_THROW((void)DecodeModelFromCnb(doc), ContentLoadException);
    }
}

// --------------------------------------------------------------------------------------------
// Schema-level allocation ceilings (plans/plan_cnb.md CNBF-092 review)
// --------------------------------------------------------------------------------------------

TEST(CnbModelCodecTest, DecoderRefusesAMorphTargetCountAboveTheSchemaCeiling)
{
    // The container's generic array limit alone would let a chunk of four-byte presence words
    // expand into orders of magnitude more CnbMorphTarget objects. This ceiling matches the one
    // ContentManager's own .cnj morph reader already applies, so nothing a .cnj can express is
    // refused -- but a crafted .cnb cannot ask for a gigabyte of morph targets either.
    CnbModelData model = MakeSmallModel();
    CnbMorphData morph;
    morph.vertexCount = 0u;
    model.parts[0].morph = morph;
    const ModelFileEditor editor{model};

    // No bytes are supplied for the declared targets on purpose. The ceiling is checked before
    // the fit-in-remaining check, so an absurd count is refused ON SIGHT rather than after the
    // file has made a validator allocate for it -- which is also why this test does not need a
    // multi-megabyte fixture to exercise the ceiling it names.
    std::vector<std::uint8_t> chunk = editor.Chunk(CnbModelChunk::MorphData);
    PatchU32(chunk, 8u, 200000u);           // targetCount, above the 100000 ceiling
    const CnbDocument doc = Parse(editor.Rebuild(CnbModelChunk::MorphData, chunk));
    try
    {
        (void)DecodeModelFromCnb(doc);
        FAIL() << "expected the target-count ceiling to be enforced";
    }
    catch (const ContentLoadException& e)
    {
        EXPECT_NE(std::string(e.what()).find("ceiling"), std::string::npos)
            << "must fail on the ceiling, not merely on running out of bytes: " << e.what();
    }
}

TEST(CnbModelCodecTest, DecoderRefusesAMorphWeightKeyCountAboveTheSchemaCeiling)
{
    CnbModelData model = MakeSmallModel();
    CnbMorphData morph;
    morph.vertexCount = 0u;
    model.parts[0].morph = morph;
    const ModelFileEditor editor{model};

    // Layout with zero targets and zero weights: vertexCount, flags, targetCount, weightCount,
    // trackFlags, keyCount -- six u32s, the last of which is the key count.
    std::vector<std::uint8_t> chunk = editor.Chunk(CnbModelChunk::MorphData);
    ASSERT_EQ(chunk.size(), 24u);
    PatchU32(chunk, 20u, 2000000u);   // keyCount, above the 1000000 ceiling, with no bytes for it
    const CnbDocument doc = Parse(editor.Rebuild(CnbModelChunk::MorphData, chunk));
    try
    {
        (void)DecodeModelFromCnb(doc);
        FAIL() << "expected the weight-key ceiling to be enforced";
    }
    catch (const ContentLoadException& e)
    {
        EXPECT_NE(std::string(e.what()).find("ceiling"), std::string::npos)
            << "must fail on the ceiling, not merely on running out of bytes: " << e.what();
    }
}

TEST(CnbModelCodecTest, DecoderRefusesAPartCountThatDisagreesWithTheGeometryChunkCount)
{
    // Checked before any per-part allocation, so it is both the correctness check for an
    // unreferenced geometry chunk and the real bound on how many parts a file can ask for.
    const ModelFileEditor editor{MakeSmallModel()};
    std::vector<std::uint8_t> header = editor.Chunk(CnbModelChunk::Header);
    PatchU32(header, 8u, 100000u); // partCount, with only one MVTX/MIDX chunk present
    const CnbDocument doc = Parse(editor.Rebuild(CnbModelChunk::Header, header));
    EXPECT_THROW((void)DecodeModelFromCnb(doc), ContentLoadException);
}

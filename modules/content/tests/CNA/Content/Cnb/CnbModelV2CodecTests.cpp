// SPDX-License-Identifier: MS-PL

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbModelV2Codec.hpp"
#include "CNA/Content/Cnb/CnbModelV2Data.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

namespace Cnb = CNA::Content::Cnb;
using Microsoft::Xna::Framework::Content::ContentLoadException;

namespace
{
    void StoreU32(std::vector<std::uint8_t>& bytes, const std::size_t offset,
                  const std::uint32_t value)
    {
        ASSERT_LE(offset + 4u, bytes.size());
        bytes[offset] = static_cast<std::uint8_t>(value);
        bytes[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
        bytes[offset + 2u] = static_cast<std::uint8_t>(value >> 16u);
        bytes[offset + 3u] = static_cast<std::uint8_t>(value >> 24u);
    }

    std::vector<std::uint8_t> RebuildWithChunkMutation(
        const std::vector<std::uint8_t>& bytes, const Cnb::CnbChunkId type,
        const std::size_t ordinal,
        const std::function<void(std::vector<std::uint8_t>&)>& mutate,
        const std::uint32_t replacementFlags = 0xFFFFFFFFu,
        const std::uint32_t replacementAlignment = 0u)
    {
        const Cnb::CnbDocument document = Cnb::CnbDocument::Parse(bytes, "valid-v2.cnb");
        Cnb::CnbWriter writer(document.AssetTypeId(), document.AssetSchemaVersion());
        if (document.Metadata().present)
        {
            writer.SetMetadata(document.Metadata().assetTypeName,
                               document.Metadata().contentName);
        }
        writer.SetExternalReferences(document.ExternalReferences());

        std::size_t seen = 0u;
        bool changed = false;
        for (std::size_t index = 0u; index < document.ChunkCount(); ++index)
        {
            const Cnb::CnbChunkEntry& entry = document.ChunkAt(index);
            if (entry.type == Cnb::CnbContainerChunk::Metadata ||
                entry.type == Cnb::CnbContainerChunk::ExternalReferences)
            {
                continue;
            }
            const std::span<const std::uint8_t> view = document.ChunkData(index);
            std::vector<std::uint8_t> payload(view.begin(), view.end());
            std::uint32_t flags = entry.flags;
            std::uint32_t alignment = entry.alignment;
            if (entry.type == type && seen++ == ordinal)
            {
                mutate(payload);
                if (replacementFlags != 0xFFFFFFFFu) { flags = replacementFlags; }
                if (replacementAlignment != 0u) { alignment = replacementAlignment; }
                changed = true;
            }
            writer.AddChunk(entry.type, std::move(payload), flags, alignment);
        }
        EXPECT_TRUE(changed);
        return writer.Build();
    }

    std::vector<std::uint8_t> RebuildWithTopologyChange(
        const std::vector<std::uint8_t>& bytes, const Cnb::CnbChunkId omitted,
        const Cnb::CnbChunkId duplicated, const bool addUnknownMandatory)
    {
        const Cnb::CnbDocument document = Cnb::CnbDocument::Parse(bytes, "valid-v2.cnb");
        Cnb::CnbWriter writer(document.AssetTypeId(), document.AssetSchemaVersion());
        writer.SetMetadata(document.Metadata().assetTypeName, document.Metadata().contentName);
        writer.SetExternalReferences(document.ExternalReferences());
        for (std::size_t index = 0u; index < document.ChunkCount(); ++index)
        {
            const Cnb::CnbChunkEntry& entry = document.ChunkAt(index);
            if (entry.type == Cnb::CnbContainerChunk::Metadata ||
                entry.type == Cnb::CnbContainerChunk::ExternalReferences ||
                entry.type == omitted)
            {
                continue;
            }
            const std::span<const std::uint8_t> view = document.ChunkData(index);
            std::vector<std::uint8_t> payload(view.begin(), view.end());
            writer.AddChunk(entry.type, payload, entry.flags, entry.alignment);
            if (entry.type == duplicated)
            {
                writer.AddChunk(entry.type, std::move(payload), entry.flags, entry.alignment);
            }
        }
        if (addUnknownMandatory)
        {
            writer.AddChunk(Cnb::MakeChunkId('M', '2', 'X', 'X'), {},
                            Cnb::CnbChunkFlags::Mandatory, 4u);
        }
        return writer.Build();
    }

    std::vector<std::uint8_t> RebuildWithReferences(
        const std::vector<std::uint8_t>& bytes,
        std::vector<Cnb::CnbExternalReference> references)
    {
        const Cnb::CnbDocument document = Cnb::CnbDocument::Parse(bytes, "valid-v2.cnb");
        Cnb::CnbWriter writer(document.AssetTypeId(), document.AssetSchemaVersion());
        writer.SetMetadata(document.Metadata().assetTypeName, document.Metadata().contentName);
        writer.SetExternalReferences(std::move(references));
        for (std::size_t index = 0u; index < document.ChunkCount(); ++index)
        {
            const Cnb::CnbChunkEntry& entry = document.ChunkAt(index);
            if (entry.type == Cnb::CnbContainerChunk::Metadata ||
                entry.type == Cnb::CnbContainerChunk::ExternalReferences)
            {
                continue;
            }
            const std::span<const std::uint8_t> view = document.ChunkData(index);
            writer.AddChunk(entry.type, {view.begin(), view.end()}, entry.flags, entry.alignment);
        }
        return writer.Build();
    }

    void ExpectDecodeRejected(const std::vector<std::uint8_t>& bytes)
    {
        const Cnb::CnbDocument document = Cnb::CnbDocument::Parse(bytes, "mutated-v2.cnb");
        EXPECT_THROW(static_cast<void>(Cnb::DecodeModelV2FromCnb(document)),
                     ContentLoadException);
    }

    void ExpectU32MutationRejected(const std::vector<std::uint8_t>& valid,
                                   const Cnb::CnbChunkId type, const std::size_t ordinal,
                                   const std::size_t offset, const std::uint32_t value)
    {
        ExpectDecodeRejected(RebuildWithChunkMutation(
            valid, type, ordinal, [=](std::vector<std::uint8_t>& payload)
            {
                StoreU32(payload, offset, value);
            }));
    }

    Cnb::CnbModelV2Data MakeModelV2()
    {
        Cnb::CnbModelV2Data model;
        Cnb::CnbModelV2Bone detached;
        detached.name = "Detached";
        Cnb::CnbModelV2Bone root;
        root.name = "Root";
        root.transform[12] = 4.0f;
        model.bones = {detached, root};
        model.rootBone = 1u;

        Cnb::CnbModelV2VertexDeclaration declaration;
        declaration.vertexStride = 24u;
        declaration.elements = {
            {0u, Cnb::CnbModelV2VertexFormat::Vector3,
             Cnb::CnbModelV2VertexUsage::Position, 0u},
            {12u, Cnb::CnbModelV2VertexFormat::Vector3,
             Cnb::CnbModelV2VertexUsage::Normal, 0u}};
        model.vertexDeclarations = {declaration};

        Cnb::CnbModelV2VertexBuffer vertex;
        vertex.declaration = 0u;
        vertex.vertexCount = 5u;
        vertex.bytes.resize(5u * 24u);
        for (std::size_t index = 0u; index < vertex.bytes.size(); ++index)
        {
            vertex.bytes[index] = static_cast<std::uint8_t>(index);
        }
        model.vertexBuffers = {vertex};

        Cnb::CnbModelV2IndexBuffer indices;
        indices.indexElementSize = 2u;
        indices.indexCount = 6u;
        indices.bytes = {0u, 0u, 1u, 0u, 2u, 0u, 1u, 0u, 3u, 0u, 2u, 0u};
        model.indexBuffers = {indices};

        Cnb::CnbModelV2Effect effect;
        effect.kind = Cnb::CnbModelV2EffectKind::BasicEffect;
        effect.primaryTexture = "Textures/checker";
        effect.diffuse = {{0.25f, 0.5f, 0.75f}};
        effect.emissive = {{0.1f, 0.2f, 0.3f}};
        effect.specular = {{0.8f, 0.7f, 0.6f}};
        effect.specularPower = 9.5f;
        effect.alpha = 0.75f;
        effect.vertexColorEnabled = true;
        model.effects = {effect};

        Cnb::CnbModelV2Mesh mesh;
        mesh.name = "SharedGeometry";
        mesh.parentBone = 1;
        mesh.boundingSphere = {{1.0f, 2.0f, 3.0f, 4.0f}};
        mesh.parts = {{1u, 4u, 0u, 1u, 0u, 0u, 0u},
                      {1u, 4u, 3u, 1u, 0u, 0u, 0u}};
        model.meshes = {mesh};
        return model;
    }

    Cnb::CnbModelV2Data MakeEffectsOnlyModel()
    {
        Cnb::CnbModelV2Data model;
        Cnb::CnbModelV2Bone root;
        root.name = "Root";
        model.bones = {root};

        Cnb::CnbModelV2Effect basic;
        basic.kind = Cnb::CnbModelV2EffectKind::BasicEffect;
        basic.primaryTexture = "Textures/basic";
        basic.diffuse = {{1.0f, 0.5f, 0.25f}};
        basic.emissive = {{0.1f, 0.2f, 0.3f}};
        basic.specular = {{0.4f, 0.5f, 0.6f}};
        basic.specularPower = 12.0f;
        basic.alpha = 0.9f;
        basic.vertexColorEnabled = true;

        Cnb::CnbModelV2Effect skinned;
        skinned.kind = Cnb::CnbModelV2EffectKind::SkinnedEffect;
        skinned.primaryTexture = "Textures/skinned";
        skinned.diffuse = {{0.9f, 0.8f, 0.7f}};
        skinned.emissive = {{0.0f, 0.1f, 0.2f}};
        skinned.specular = {{0.3f, 0.4f, 0.5f}};
        skinned.specularPower = 32.0f;
        skinned.alpha = 0.8f;
        skinned.weightsPerVertex = 4u;

        Cnb::CnbModelV2Effect dual;
        dual.kind = Cnb::CnbModelV2EffectKind::DualTextureEffect;
        dual.primaryTexture = "Textures/dual-a";
        dual.secondaryTexture = "Textures/dual-b";
        dual.diffuse = {{0.7f, 0.6f, 0.5f}};
        dual.alpha = 0.7f;
        dual.vertexColorEnabled = true;

        Cnb::CnbModelV2Effect alpha;
        alpha.kind = Cnb::CnbModelV2EffectKind::AlphaTestEffect;
        alpha.primaryTexture = "Textures/cutout";
        alpha.diffuse = {{0.6f, 0.5f, 0.4f}};
        alpha.alpha = 0.6f;
        alpha.alphaFunction = 6u;
        alpha.referenceAlpha = 0xF0000040u;
        alpha.vertexColorEnabled = true;

        Cnb::CnbModelV2Effect environment;
        environment.kind = Cnb::CnbModelV2EffectKind::EnvironmentMapEffect;
        environment.primaryTexture = "Textures/env-base";
        environment.cubeTexture = "Textures/sky";
        environment.diffuse = {{0.5f, 0.4f, 0.3f}};
        environment.emissive = {{0.2f, 0.1f, 0.0f}};
        environment.specular = {{0.8f, 0.9f, 1.0f}};
        environment.environmentMapAmount = 0.75f;
        environment.fresnelFactor = 0.25f;
        environment.alpha = 0.5f;

        model.effects = {basic, skinned, dual, alpha, environment};
        return model;
    }
}

TEST(CnbModelV2CodecTest, RoundTripsExactGraphResourcesAndWindows)
{
    const Cnb::CnbModelV2Data expected = MakeModelV2();
    const std::vector<std::uint8_t> first =
        Cnb::EncodeModelV2ToCnb(expected, "Models/shared");
    const std::vector<std::uint8_t> second =
        Cnb::EncodeModelV2ToCnb(expected, "Models/shared");
    EXPECT_EQ(first, second);

    const Cnb::CnbDocument document = Cnb::CnbDocument::Parse(first, "model-v2.cnb");
    EXPECT_EQ(document.AssetTypeId(), Cnb::CnbAssetTypeId::Model);
    EXPECT_EQ(document.AssetSchemaVersion(), 2u);
    ASSERT_EQ(document.FindAll(Cnb::CnbModelV2Chunk::VertexData).size(), 1u);
    ASSERT_EQ(document.FindAll(Cnb::CnbModelV2Chunk::IndexData).size(), 1u);
    const Cnb::CnbModelV2Data actual = Cnb::DecodeModelV2FromCnb(document);
    EXPECT_EQ(actual, expected);
}

TEST(CnbModelV2CodecTest, RoundTripsAllFiveCompleteStockEffectRecords)
{
    const Cnb::CnbModelV2Data expected = MakeEffectsOnlyModel();
    const Cnb::CnbDocument document = Cnb::CnbDocument::Parse(
        Cnb::EncodeModelV2ToCnb(expected), "effects-v2.cnb");
    const Cnb::CnbModelV2Data actual = Cnb::DecodeModelV2FromCnb(document);
    EXPECT_EQ(actual, expected);
    ASSERT_EQ(document.ExternalReferences().size(), 7u);
    EXPECT_EQ(document.ExternalReferences().back().expectedAssetTypeId,
              Cnb::CnbAssetTypeId::TextureCube);
}

TEST(CnbModelV2CodecTest, SchemaOneAndSchemaTwoDecodersRemainDisjoint)
{
    Cnb::CnbWriter writer(Cnb::CnbAssetTypeId::Model, 1u);
    writer.SetMetadata("Microsoft.Xna.Framework.Graphics.Model", "old");
    writer.AddChunk(Cnb::MakeChunkId('T', 'E', 'S', 'T'), {}, Cnb::CnbChunkFlags::Mandatory);
    const Cnb::CnbDocument old = Cnb::CnbDocument::Parse(writer.Build(), "old-model.cnb");
    EXPECT_THROW(static_cast<void>(Cnb::DecodeModelV2FromCnb(old)), ContentLoadException);
}

TEST(CnbModelV2CodecTest, EncoderRejectsInvalidDeclarationsGraphEffectsAndDrawWindows)
{
    {
        Cnb::CnbModelV2Data model = MakeModelV2();
        model.vertexDeclarations[0].elements[1].offset = 8u;
        EXPECT_THROW(static_cast<void>(Cnb::EncodeModelV2ToCnb(model)), ContentLoadException);
    }
    {
        Cnb::CnbModelV2Data model = MakeModelV2();
        model.meshes[0].parts[0].numVertices = 5u;
        EXPECT_THROW(static_cast<void>(Cnb::EncodeModelV2ToCnb(model)), ContentLoadException);
    }
    {
        Cnb::CnbModelV2Data model = MakeModelV2();
        model.indexBuffers[0].bytes[0] = 4u;
        EXPECT_THROW(static_cast<void>(Cnb::EncodeModelV2ToCnb(model)), ContentLoadException);
    }
    {
        Cnb::CnbModelV2Data model = MakeModelV2();
        model.bones[1].parent = 0;
        EXPECT_THROW(static_cast<void>(Cnb::EncodeModelV2ToCnb(model)), ContentLoadException);
    }
    {
        Cnb::CnbModelV2Data model = MakeModelV2();
        model.effects[0].secondaryTexture = "Textures/hidden";
        EXPECT_THROW(static_cast<void>(Cnb::EncodeModelV2ToCnb(model)), ContentLoadException);
    }
}

TEST(CnbModelV2CodecTest, DecoderRejectsMalformedHeaderGraphAndDrawTables)
{
    const std::vector<std::uint8_t> valid = Cnb::EncodeModelV2ToCnb(MakeModelV2());

    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::Header, 0u, 0u, 1u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::Header, 0u, 4u, 3u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::Header, 0u, 8u, 2u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::Header, 0u, 12u, 3u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::Header, 0u, 16u, 2u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::Header, 0u, 20u, 3u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::Header, 0u, 24u, 2u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::Header, 0u, 28u, 2u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::Header, 0u, 32u, 2u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::Header, 0u, 36u, 2u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::Header, 0u, 40u, 1u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::Strings, 0u, 0u, 0u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::Bones, 0u, 0u, 99u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::Bones, 0u, 76u, 0u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::Bones, 0u, 8u, 0x7FC00000u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::Meshes, 0u, 20u, 0xBF800000u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::Meshes, 0u, 24u, 1u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::Parts, 0u, 4u, 0u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::Parts, 0u, 8u, 5u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::Parts, 0u, 16u, 9u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::Parts, 0u, 28u, 1u);
}

TEST(CnbModelV2CodecTest, DecoderRequiresCanonicalChunkTopologyFlagsAndAlignment)
{
    const std::vector<std::uint8_t> valid = Cnb::EncodeModelV2ToCnb(MakeModelV2());
    ExpectDecodeRejected(RebuildWithTopologyChange(
        valid, Cnb::CnbModelV2Chunk::Effects, Cnb::CnbChunkId{}, false));
    ExpectDecodeRejected(RebuildWithTopologyChange(
        valid, Cnb::CnbChunkId{}, Cnb::CnbModelV2Chunk::Header, false));
    ExpectDecodeRejected(RebuildWithTopologyChange(
        valid, Cnb::CnbChunkId{}, Cnb::CnbChunkId{}, true));
    ExpectDecodeRejected(RebuildWithChunkMutation(
        valid, Cnb::CnbModelV2Chunk::Header, 0u,
        [](std::vector<std::uint8_t>&) {}, Cnb::CnbChunkFlags::None));
    ExpectDecodeRejected(RebuildWithChunkMutation(
        valid, Cnb::CnbModelV2Chunk::Header, 0u,
        [](std::vector<std::uint8_t>&) {}, 0xFFFFFFFFu, 8u));
    ExpectDecodeRejected(RebuildWithChunkMutation(
        valid, Cnb::CnbModelV2Chunk::VertexData, 0u,
        [](std::vector<std::uint8_t>&) {}, 0xFFFFFFFFu, 4u));
}

TEST(CnbModelV2CodecTest, DecoderRejectsMalformedDeclarationAndBufferTables)
{
    const std::vector<std::uint8_t> valid = Cnb::EncodeModelV2ToCnb(MakeModelV2());

    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::VertexDeclarations, 0u, 0u, 0u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::VertexDeclarations, 0u, 4u, 1u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::VertexDeclarations, 0u, 12u, 1u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::VertexDeclarations, 0u, 20u, 99u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::VertexDeclarations, 0u, 24u, 99u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::VertexDeclarations, 0u, 28u, 32u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::VertexDeclarations, 0u, 32u, 1u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::VertexDeclarations, 0u, 36u, 8u);

    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::VertexResources, 0u, 0u, 9u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::VertexResources, 0u, 4u, 0u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::VertexResources, 0u, 8u, 1u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::VertexResources, 0u, 12u, 1u);
    ExpectDecodeRejected(RebuildWithChunkMutation(
        valid, Cnb::CnbModelV2Chunk::VertexData, 0u,
        [](std::vector<std::uint8_t>& payload) { payload.pop_back(); }));

    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::IndexResources, 0u, 0u, 3u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::IndexResources, 0u, 4u, 7u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::IndexResources, 0u, 8u, 1u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::IndexResources, 0u, 12u, 1u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::IndexData, 0u, 0u, 4u);
    ExpectDecodeRejected(RebuildWithChunkMutation(
        valid, Cnb::CnbModelV2Chunk::IndexData, 0u,
        [](std::vector<std::uint8_t>& payload) { payload.pop_back(); }));
}

TEST(CnbModelV2CodecTest, DecoderRejectsMalformedEffectRecordsAndTypedReferences)
{
    const std::vector<std::uint8_t> valid = Cnb::EncodeModelV2ToCnb(MakeModelV2());

    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::Effects, 0u, 0u, 99u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::Effects, 0u, 4u, 2u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::Effects, 0u, 8u, 99u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::Effects, 0u, 28u, 1u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::Effects, 0u, 80u, 0x3F800000u);
    ExpectU32MutationRejected(valid, Cnb::CnbModelV2Chunk::Effects, 0u, 84u, 1u);

    const Cnb::CnbDocument document = Cnb::CnbDocument::Parse(valid, "valid-v2.cnb");
    std::vector<Cnb::CnbExternalReference> references = document.ExternalReferences();
    ASSERT_EQ(references.size(), 1u);
    references[0].expectedAssetTypeId = Cnb::CnbAssetTypeId::TextureCube;
    ExpectDecodeRejected(RebuildWithReferences(valid, std::move(references)));
}

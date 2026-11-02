// SPDX-License-Identifier: MS-PL
#include "CNA/C/cnb_model_v2.h"
#include "CNA/Content/Cnb/CnbModelV2Codec.hpp"
#include "CNA/Content/Cnb/CnbSoundEffectCodec.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Content/Cnb/CnbSourceImport.hpp"
#include "CNA/Content/Cnb/CnbModelFromCnj.hpp"
#include "CNA/Content/Cnb/CnbModelCodec.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <thread>
namespace Cnb = CNA::Content::Cnb;
static_assert(CNA_CNB_MIP_COLOR_SPACE_LINEAR == static_cast<uint32_t>(Cnb::CnbMipColorSpace::Linear));
static_assert(CNA_CNB_MIP_COLOR_SPACE_SRGB == static_cast<uint32_t>(Cnb::CnbMipColorSpace::Srgb));
namespace {
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
static_assert(CNA_CNB_MODEL_V2_SCHEMA_VERSION == Cnb::CnbModelV2SchemaVersion);
static_assert(CNA_CNB_MODEL_V2_HEADER_STRIDE == Cnb::CnbModelV2HeaderStride);
static_assert(CNA_CNB_MODEL_V2_BONE_STRIDE == Cnb::CnbModelV2BoneStride);
static_assert(CNA_CNB_MODEL_V2_MESH_STRIDE == Cnb::CnbModelV2MeshStride);
static_assert(CNA_CNB_MODEL_V2_PART_STRIDE == Cnb::CnbModelV2PartStride);
static_assert(CNA_CNB_MODEL_V2_DECLARATION_STRIDE == Cnb::CnbModelV2DeclarationStride);
static_assert(CNA_CNB_MODEL_V2_ELEMENT_STRIDE == Cnb::CnbModelV2ElementStride);
static_assert(CNA_CNB_MODEL_V2_RESOURCE_STRIDE == Cnb::CnbModelV2ResourceStride);
static_assert(CNA_CNB_MODEL_V2_EFFECT_STRIDE == Cnb::CnbModelV2EffectStride);
static_assert(CNA_CNB_MODEL_V2_NO_INDEX == Cnb::CnbModelV2NoIndex);
static_assert(CNA_CNB_MODEL_V2_CHUNK_HEADER == Cnb::CnbModelV2Chunk::Header.value);
static_assert(CNA_CNB_MODEL_V2_CHUNK_STRINGS == Cnb::CnbModelV2Chunk::Strings.value);
static_assert(CNA_CNB_MODEL_V2_CHUNK_BONES == Cnb::CnbModelV2Chunk::Bones.value);
static_assert(CNA_CNB_MODEL_V2_CHUNK_MESHES == Cnb::CnbModelV2Chunk::Meshes.value);
static_assert(CNA_CNB_MODEL_V2_CHUNK_PARTS == Cnb::CnbModelV2Chunk::Parts.value);
static_assert(CNA_CNB_MODEL_V2_CHUNK_VERTEX_DECLARATIONS == Cnb::CnbModelV2Chunk::VertexDeclarations.value);
static_assert(CNA_CNB_MODEL_V2_CHUNK_VERTEX_RESOURCES == Cnb::CnbModelV2Chunk::VertexResources.value);
static_assert(CNA_CNB_MODEL_V2_CHUNK_VERTEX_DATA == Cnb::CnbModelV2Chunk::VertexData.value);
static_assert(CNA_CNB_MODEL_V2_CHUNK_INDEX_RESOURCES == Cnb::CnbModelV2Chunk::IndexResources.value);
static_assert(CNA_CNB_MODEL_V2_CHUNK_INDEX_DATA == Cnb::CnbModelV2Chunk::IndexData.value);
static_assert(CNA_CNB_MODEL_V2_CHUNK_EFFECTS == Cnb::CnbModelV2Chunk::Effects.value);
static_assert(CNA_CNB_MODEL_V2_EFFECT_BASIC == static_cast<uint32_t>(Cnb::CnbModelV2EffectKind::BasicEffect));
static_assert(CNA_CNB_MODEL_V2_EFFECT_SKINNED == static_cast<uint32_t>(Cnb::CnbModelV2EffectKind::SkinnedEffect));
static_assert(CNA_CNB_MODEL_V2_EFFECT_DUAL_TEXTURE == static_cast<uint32_t>(Cnb::CnbModelV2EffectKind::DualTextureEffect));
static_assert(CNA_CNB_MODEL_V2_EFFECT_ALPHA_TEST == static_cast<uint32_t>(Cnb::CnbModelV2EffectKind::AlphaTestEffect));
static_assert(CNA_CNB_MODEL_V2_EFFECT_ENVIRONMENT_MAP == static_cast<uint32_t>(Cnb::CnbModelV2EffectKind::EnvironmentMapEffect));
static_assert(CNA_VERTEX_ELEMENT_FORMAT_SINGLE == static_cast<uint32_t>(Cnb::CnbModelV2VertexFormat::Single));
static_assert(CNA_VERTEX_ELEMENT_FORMAT_VECTOR2 == static_cast<uint32_t>(Cnb::CnbModelV2VertexFormat::Vector2));
static_assert(CNA_VERTEX_ELEMENT_FORMAT_VECTOR3 == static_cast<uint32_t>(Cnb::CnbModelV2VertexFormat::Vector3));
static_assert(CNA_VERTEX_ELEMENT_FORMAT_VECTOR4 == static_cast<uint32_t>(Cnb::CnbModelV2VertexFormat::Vector4));
static_assert(CNA_VERTEX_ELEMENT_FORMAT_COLOR == static_cast<uint32_t>(Cnb::CnbModelV2VertexFormat::Color));
static_assert(CNA_VERTEX_ELEMENT_FORMAT_BYTE4 == static_cast<uint32_t>(Cnb::CnbModelV2VertexFormat::Byte4));
static_assert(CNA_VERTEX_ELEMENT_FORMAT_SHORT2 == static_cast<uint32_t>(Cnb::CnbModelV2VertexFormat::Short2));
static_assert(CNA_VERTEX_ELEMENT_FORMAT_SHORT4 == static_cast<uint32_t>(Cnb::CnbModelV2VertexFormat::Short4));
static_assert(CNA_VERTEX_ELEMENT_FORMAT_NORMALIZED_SHORT2 == static_cast<uint32_t>(Cnb::CnbModelV2VertexFormat::NormalizedShort2));
static_assert(CNA_VERTEX_ELEMENT_FORMAT_NORMALIZED_SHORT4 == static_cast<uint32_t>(Cnb::CnbModelV2VertexFormat::NormalizedShort4));
static_assert(CNA_VERTEX_ELEMENT_FORMAT_HALF_VECTOR2 == static_cast<uint32_t>(Cnb::CnbModelV2VertexFormat::HalfVector2));
static_assert(CNA_VERTEX_ELEMENT_FORMAT_HALF_VECTOR4 == static_cast<uint32_t>(Cnb::CnbModelV2VertexFormat::HalfVector4));
static_assert(CNA_VERTEX_ELEMENT_USAGE_POSITION == static_cast<uint32_t>(Cnb::CnbModelV2VertexUsage::Position));
static_assert(CNA_VERTEX_ELEMENT_USAGE_COLOR == static_cast<uint32_t>(Cnb::CnbModelV2VertexUsage::Color));
static_assert(CNA_VERTEX_ELEMENT_USAGE_TEXTURE_COORDINATE == static_cast<uint32_t>(Cnb::CnbModelV2VertexUsage::TextureCoordinate));
static_assert(CNA_VERTEX_ELEMENT_USAGE_NORMAL == static_cast<uint32_t>(Cnb::CnbModelV2VertexUsage::Normal));
static_assert(CNA_VERTEX_ELEMENT_USAGE_BINORMAL == static_cast<uint32_t>(Cnb::CnbModelV2VertexUsage::Binormal));
static_assert(CNA_VERTEX_ELEMENT_USAGE_TANGENT == static_cast<uint32_t>(Cnb::CnbModelV2VertexUsage::Tangent));
static_assert(CNA_VERTEX_ELEMENT_USAGE_BLEND_INDICES == static_cast<uint32_t>(Cnb::CnbModelV2VertexUsage::BlendIndices));
static_assert(CNA_VERTEX_ELEMENT_USAGE_BLEND_WEIGHT == static_cast<uint32_t>(Cnb::CnbModelV2VertexUsage::BlendWeight));
static_assert(CNA_VERTEX_ELEMENT_USAGE_DEPTH == static_cast<uint32_t>(Cnb::CnbModelV2VertexUsage::Depth));
static_assert(CNA_VERTEX_ELEMENT_USAGE_FOG == static_cast<uint32_t>(Cnb::CnbModelV2VertexUsage::Fog));
static_assert(CNA_VERTEX_ELEMENT_USAGE_POINT_SIZE == static_cast<uint32_t>(Cnb::CnbModelV2VertexUsage::PointSize));
static_assert(CNA_VERTEX_ELEMENT_USAGE_SAMPLE == static_cast<uint32_t>(Cnb::CnbModelV2VertexUsage::Sample));
static_assert(CNA_VERTEX_ELEMENT_USAGE_TESSELLATE_FACTOR == static_cast<uint32_t>(Cnb::CnbModelV2VertexUsage::TessellateFactor));

extern "C" uint64_t cna_test_model_v2_bytes(unsigned variant, uint8_t* destination, uint64_t capacity)
{
    try {
        auto model = variant == 1 ? MakeEffectsOnlyModel() : MakeModelV2();
        if (variant == 2) {
            model.bones[0].name.clear(); model.bones[0].nameIsNull = true;
        }
        if (variant == 3) {
            model.vertexDeclarations.push_back(model.vertexDeclarations[0]);
            model.vertexBuffers[0].declaration = 1;
            auto& index = model.indexBuffers[0];
            index.indexElementSize = 4;
            index.bytes = {0,0,0,0,1,0,0,0,2,0,0,0,1,0,0,0,3,0,0,0,2,0,0,0};
        }
        const auto bytes = Cnb::EncodeModelV2ToCnb(model, "fixture");
        const auto decoded = Cnb::DecodeModelV2FromCnb(Cnb::CnbDocument::Parse(bytes, "oracle"));
        if (variant != 2 && !(model == decoded)) return 0;
        if (destination && capacity >= bytes.size()) std::memcpy(destination, bytes.data(), bytes.size());
        return bytes.size();
    } catch (...) { return 0; }
}
extern "C" int cna_test_model_v2_worker(int (*function)(void))
{
    int result = 1;
    std::thread worker([&] { result = function(); }); worker.join(); return result;
}
extern "C" uint32_t cna_test_sound_schema(uint32_t format)
{
    return Cnb::CnbSoundEffectSchemaVersionForFormat(static_cast<Cnb::CnbAudioFormat>(format));
}
extern "C" uint64_t cna_test_pcm16(uint32_t format, const uint8_t* bytes, uint64_t size, uint8_t* output)
{
    Cnb::CnbSoundEffectData sound;
    sound.format = static_cast<Cnb::CnbAudioFormat>(format);
    sound.samples.assign(bytes, bytes + size);
    const auto pcm = Cnb::CnbSoundEffectSamplesAsPcm16(sound);
    std::memcpy(output, pcm.data(), pcm.size());
    return pcm.size();
}
extern "C" uint64_t cna_test_mips(uint32_t color, const uint8_t* rgba, uint32_t level, uint8_t* output)
{
    auto texture = Cnb::MakeRgba8Texture2DData(3, 2, std::vector<uint8_t>(rgba, rgba + 24));
    Cnb::GenerateRgba8MipChain(texture, static_cast<Cnb::CnbMipColorSpace>(color));
    const auto& bytes = texture.representations[0].levels[level];
    std::memcpy(output, bytes.data(), bytes.size());
    return bytes.size();
}

extern "C" int cna_test_cnb_write_path(const char* path, const uint8_t* bytes, uint64_t count)
{
    try {
        std::ofstream file(std::filesystem::path(std::u8string(path, path + std::strlen(path))), std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes), static_cast<std::streamsize>(count));
        file.close(); return file.fail() ? 0 : 1;
    } catch (...) { return 0; }
}
extern "C" void cna_test_cnb_remove_path(const char* path)
{
    std::error_code error;
    std::filesystem::remove(std::filesystem::path(std::u8string(path, path + std::strlen(path))), error);
}
extern "C" uint64_t cna_test_cnb_import_path(unsigned kind, const char* path, uint8_t* output, uint64_t capacity)
{
    try {
        const auto nativePath = std::filesystem::path(std::u8string(path, path + std::strlen(path)));
        std::vector<uint8_t> bytes;
        if (kind == 0) bytes = Cnb::EncodeTexture2DToCnb(Cnb::ImportImageAsCnbTexture2D(nativePath), "path");
        else if (kind == 1) bytes = Cnb::EncodeTextureCubeToCnb(Cnb::ImportDdsAsCnbTextureCube(nativePath), "path");
        else if (kind == 2) bytes = Cnb::EncodeSoundEffectToCnb(Cnb::ImportWavAsCnbSoundEffect(nativePath), "path");
        else bytes = Cnb::EncodeModelToCnb(Cnb::BuildCnbModelFromCnj(nativePath, std::filesystem::path(".")).model, "path");
        if (capacity >= bytes.size()) std::memcpy(output, bytes.data(), bytes.size());
        return bytes.size();
    } catch (...) { return 0; }
}

static_assert(CNA_CNB_MODEL_BONE_STRIDE == Cnb::CnbModelV2BoneStride);
static_assert(CNA_CNB_MODEL_CHUNK_VERTEX_DATA == Cnb::CnbModelV2Chunk::VertexData.value);
static_assert(CNA_CNB_MODEL_CHUNK_INDEX_DATA == Cnb::CnbModelV2Chunk::IndexData.value);

extern "C" uint64_t cna_test_model_v2_invalid(unsigned variant, uint8_t* output, uint64_t capacity)
{
    try {
        const auto valid = Cnb::EncodeModelV2ToCnb(MakeModelV2());
        const auto document = Cnb::CnbDocument::Parse(valid, "source");
        Cnb::CnbWriter writer(document.AssetTypeId(), variant == 0 ? 1 : 2);
        writer.SetMetadata(document.Metadata().assetTypeName, document.Metadata().contentName);
        writer.SetExternalReferences(document.ExternalReferences());
        for (size_t i=0; i<document.ChunkCount(); ++i) {
            const auto& entry = document.ChunkAt(i);
            if (entry.type == Cnb::CnbContainerChunk::Metadata || entry.type == Cnb::CnbContainerChunk::ExternalReferences)
                continue;
            if (variant == 1 && entry.type == Cnb::CnbModelV2Chunk::Header) continue;
            const auto source = document.ChunkData(i);
            std::vector<uint8_t> payload(source.begin(), source.end());
            if (variant == 2 && entry.type == Cnb::CnbModelV2Chunk::VertexData) payload.pop_back();
            writer.AddChunk(entry.type, std::move(payload), entry.flags, entry.alignment);
        }
        const auto bytes = writer.Build();
        const auto malformed = Cnb::CnbDocument::Parse(bytes, "malformed");
        bool rejected = false;
        try { (void)Cnb::DecodeModelV2FromCnb(malformed); } catch (...) { rejected = true; }
        if (!rejected || bytes.size() > capacity) return 0;
        std::memcpy(output, bytes.data(), bytes.size());
        return bytes.size();
    } catch (...) { return 0; }
}

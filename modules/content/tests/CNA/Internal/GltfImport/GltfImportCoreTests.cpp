// SPDX-License-Identifier: MS-PL
//
// Direct unit tests for CNA::Internal::GltfImport::GltfImportCore::ExtractMesh() -- calls it
// in-process (cgltf_parse_file + cgltf_load_buffers on a small self-contained fixture written to
// a scratch file, mirroring gltf_to_cnj.cpp's own parse setup) rather than going through
// ContentManager or spawning the CLI tool, since the thing under test here (MeshOut::
// uvSetMismatchedMapsEXT) has no separately-observable effect on either of those higher-level paths --
// the warning it drives (gltf_to_cnj.cpp's own ConvertGroup) is best-effort stdout diagnostics,
// not asserted on elsewhere in this codebase (see the pre-existing morph-target warning, which
// has no matching test either).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "GltfFixtureCorpus.hpp"

using namespace CNA::Internal::GltfImport;

namespace
{
    class ScratchDir
    {
    public:
        ScratchDir()
            : dir_(std::filesystem::temp_directory_path()
                   / ("cna_gltfimportcore_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(dir_);
        }
        ~ScratchDir()
        {
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
        }
        ScratchDir(const ScratchDir&) = delete;
        ScratchDir& operator=(const ScratchDir&) = delete;
        [[nodiscard]] const std::filesystem::path& path() const { return dir_; }

    private:
        std::filesystem::path dir_;
    };

    void WriteFile(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream f(path, std::ios::binary);
        f << text;
    }

    // Single unskinned triangle, base-color texture on TEXCOORD_0 and a normal map on TEXCOORD_1
    // -- a deliberate UV-set mismatch (base color and normal map disagree on which UV channel to
    // sample). TEXCOORD_1's own UV values (0,1 / 1,1 / 0,0) are deliberately different from
    // TEXCOORD_0's (0,0 / 1,0 / 0,1) so the two are not accidentally identical.
    const char* kMismatchedUvGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": {
      "POSITION": 0, "NORMAL": 1, "TANGENT": 2, "TEXCOORD_0": 3, "TEXCOORD_1": 4
  }, "material": 0 } ] } ],
  "materials": [ {
    "pbrMetallicRoughness": { "baseColorTexture": { "index": 0, "texCoord": 0 } },
    "normalTexture": { "index": 1, "texCoord": 1 }
  } ],
  "textures": [ { "source": 0 }, { "source": 1 } ],
  "images": [
    { "bufferView": 5, "mimeType": "image/png" },
    { "bufferView": 6, "mimeType": "image/png" }
  ],
  "buffers": [ {
    "byteLength": 306,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AACAPwAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAgD8AAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAgD8AAIA/AACAPwAAAAAAAAAAiVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4z8AAAAMBAQDJ/pLvAAAAAElFTkSuQmCCiVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4z8AAAAMBAQDJ/pLvAAAAAElFTkSuQmCC"
  } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,   "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 72,  "byteLength": 48 },
    { "buffer": 0, "byteOffset": 120, "byteLength": 24 },
    { "buffer": 0, "byteOffset": 144, "byteLength": 24 },
    { "buffer": 0, "byteOffset": 168, "byteLength": 69 },
    { "buffer": 0, "byteOffset": 237, "byteLength": 69 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 3, "componentType": 5126, "count": 3, "type": "VEC2" },
    { "bufferView": 4, "componentType": 5126, "count": 3, "type": "VEC2" }
  ]
})GLTF";

    // Identical to kMismatchedUvGltf except the normal map is also on TEXCOORD_0 (matching base
    // color) -- the negative case, proving uvSetMismatchedMapsEXT stays empty for the common, non-divergent
    // authoring pattern.
    const char* kMatchedUvGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": {
      "POSITION": 0, "NORMAL": 1, "TANGENT": 2, "TEXCOORD_0": 3, "TEXCOORD_1": 4
  }, "material": 0 } ] } ],
  "materials": [ {
    "pbrMetallicRoughness": { "baseColorTexture": { "index": 0, "texCoord": 0 } },
    "normalTexture": { "index": 1, "texCoord": 0 }
  } ],
  "textures": [ { "source": 0 }, { "source": 1 } ],
  "images": [
    { "bufferView": 5, "mimeType": "image/png" },
    { "bufferView": 6, "mimeType": "image/png" }
  ],
  "buffers": [ {
    "byteLength": 306,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AACAPwAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAgD8AAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAgD8AAIA/AACAPwAAAAAAAAAAiVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4z8AAAAMBAQDJ/pLvAAAAAElFTkSuQmCCiVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4z8AAAAMBAQDJ/pLvAAAAAElFTkSuQmCC"
  } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,   "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 72,  "byteLength": 48 },
    { "buffer": 0, "byteOffset": 120, "byteLength": 24 },
    { "buffer": 0, "byteOffset": 144, "byteLength": 24 },
    { "buffer": 0, "byteOffset": 168, "byteLength": 69 },
    { "buffer": 0, "byteOffset": 237, "byteLength": 69 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 3, "componentType": 5126, "count": 3, "type": "VEC2" },
    { "bufferView": 4, "componentType": 5126, "count": 3, "type": "VEC2" }
  ]
})GLTF";

    MeshOut ExtractPrimitive0(const std::string& gltfJson)
    {
        ScratchDir dir;
        const std::filesystem::path gltfPath = dir.path() / "fixture.gltf";
        WriteFile(gltfPath, gltfJson);

        cgltf_options options{};
        cgltf_data* data = nullptr;
        cgltf_result result = cgltf_parse_file(&options, gltfPath.string().c_str(), &data);
        EXPECT_EQ(result, cgltf_result_success);
        if (result != cgltf_result_success) { return MeshOut{}; }

        result = cgltf_load_buffers(&options, data, gltfPath.string().c_str());
        EXPECT_EQ(result, cgltf_result_success);
        if (result != cgltf_result_success) { cgltf_free(data); return MeshOut{}; }

        MeshOut out = ExtractMesh(data, data->meshes[0].primitives[0], "primitive0", nullptr, 1.0f);
        cgltf_free(data);
        return out;
    }

    // Draco mesh compression decoding (CNB-91, Phase 14F): a single triangle (POSITION at
    // (0,0,0)/(1,0,0)/(0,1,0), a uniform (0,0,1) NORMAL, and TEXCOORD_0 (0,0)/(1,0)/(0,1)) encoded
    // with a real draco::Encoder via draco::TriangleSoupMeshBuilder (not hand-authored bytes --
    // Draco's own bitstream format is not something to fake) using the exact same values as
    // every other real-glTF fixture's own unskinned triangle. The Draco encoder assigns unique
    // attribute IDs 0/1/2 in AddAttribute() call order (POSITION/NORMAL/TEXCOORD_0), confirmed by
    // a standalone encode+decode round-trip during authoring -- hence the
    // "KHR_draco_mesh_compression"."attributes" mapping below.
    const char* kDracoTriangleGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "extensionsUsed": [ "KHR_draco_mesh_compression" ],
  "extensionsRequired": [ "KHR_draco_mesh_compression" ],
  "meshes": [ { "primitives": [ {
      "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 },
      "extensions": {
        "KHR_draco_mesh_compression": {
          "bufferView": 0,
          "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 }
        }
      }
  } ] } ],
  "buffers": [ {
    "byteLength": 156,
    "uri": "data:application/octet-stream;base64,RFJBQ08CAgEBAAAAAwECAQAAAQf/AREBAQABAQAD/wAAAAAAAQAAAQAJAwAAAAEBCQMAAQABAwkCAAIAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AACAPwAAAAAAAAAAAACAPwAAAAAAAAAA"
  } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": 156 }
  ],
  "accessors": [
    { "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "componentType": 5126, "count": 3, "type": "VEC3" },
    { "componentType": 5126, "count": 3, "type": "VEC2" }
  ]
})GLTF";

    // Full MikkTSpace-style tangent generation (CNB-94, Phase 14G): two triangles sharing vertex 0
    // (index 0), with distinctly different UV-gradient tangent directions AND distinctly different
    // interior angles at the shared vertex (90 degrees for triangle A=(0,1,2), ~42.14 degrees for
    // triangle B=(0,3,4)) -- no TANGENT accessor, so ComputeTangentsEXT's fallback runs. Chosen
    // specifically so angle-weighted accumulation produces a genuinely different final tangent
    // direction at vertex 0 than an unweighted (Lengyel's-method) sum would: hand-derived (via an
    // independent Python re-implementation of the same formula, not just captured-and-trusted)
    // weighted result is (0.91369578, 0.40639884, 0) with handedness +1, vs. the unweighted sum's
    // own (0.72499943, 0.68874946, 0) -- a materially different direction, not a rounding-level
    // difference, proving the angle-weighting term itself is what changed the output.
    const char* kAngleWeightedTangentGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": {
      "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2
  }, "indices": 3, "material": 0 } ] } ],
  "materials": [ { "normalTexture": { "index": 0 } } ],
  "textures": [ { "source": 0 } ],
  "images": [ { "bufferView": 4, "mimeType": "image/png" } ],
  "buffers": [ {
    "byteLength": 241,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAACAv83MTD0AAAAAAACAvwAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAgD8AAIA/AACAPwAAAQACAAAAAwAEAIlQTkcNChoKAAAADUlIRFIAAAABAAAAAQgCAAAAkHdT3gAAAAxJREFUeJxj+M/AAAADAQEAyf6S7wAAAABJRU5ErkJggg=="
  } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,   "byteLength": 60 },
    { "buffer": 0, "byteOffset": 60,  "byteLength": 60 },
    { "buffer": 0, "byteOffset": 120, "byteLength": 40 },
    { "buffer": 0, "byteOffset": 160, "byteLength": 12 },
    { "buffer": 0, "byteOffset": 172, "byteLength": 69 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 5, "type": "VEC3", "min": [-1,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 5, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 5, "type": "VEC2" },
    { "bufferView": 3, "componentType": 5123, "count": 6, "type": "SCALAR" }
  ]
})GLTF";

    // GLTF-179: two triangles meet along a data-identical but separately indexed edge. Their UV
    // gradients produce tangents +X and normalize((1,10,0)) respectively. CNA owns one tangent per
    // glTF vertex, so it keeps those two bases separate; reference MikkTSpace internally welds the
    // compatible face corners and returns one shared basis along the edge. This is the smallest
    // input that measures the representation-level divergence instead of merely stating it.
    const char* kMikkWeldDivergenceGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": {
      "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2
  }, "indices": 3, "material": 0 } ] } ],
  "materials": [ { "normalTexture": { "index": 0 } } ],
  "textures": [ { "source": 0 } ],
  "images": [ { "bufferView": 4, "mimeType": "image/png" } ],
  "buffers": [ {
    "byteLength": 273,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAAAAAAAAAAAAgD8AAAAAAACAvwAAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAACAPwAAgL8AACBBAAABAAIAAwAEAAUAiVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4z8AAAAMBAQDJ/pLvAAAAAElFTkSuQmCC"
  } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,   "byteLength": 72 },
    { "buffer": 0, "byteOffset": 72,  "byteLength": 72 },
    { "buffer": 0, "byteOffset": 144, "byteLength": 48 },
    { "buffer": 0, "byteOffset": 192, "byteLength": 12 },
    { "buffer": 0, "byteOffset": 204, "byteLength": 69 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 6, "type": "VEC3", "min": [-1,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 6, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 6, "type": "VEC2" },
    { "bufferView": 3, "componentType": 5123, "count": 6, "type": "SCALAR" }
  ]
})GLTF";

    // glTF extensions (CNB-97, Phase 14H): a single triangle whose material combines
    // KHR_texture_transform (on the base-color texture: offset=[0.1,0.2], scale=[2.0,0.5],
    // rotation=0 -- chosen to avoid trig in hand-verification) and KHR_materials_emissive_strength
    // (emissiveFactor=[0.2,0.3,0.1] * strength=3.0 -> [0.6,0.9,0.3], deliberately > 1 on one
    // channel to prove the multiplier is NOT clamped, unlike ExtractPunctualLightsEXT's own
    // DiffuseColor, since glTF's own emissive-strength extension exists specifically to allow real
    // HDR emissive values beyond [0,1]).
    const char* kTextureTransformAndEmissiveStrengthGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": {
      "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2
  }, "material": 0 } ] } ],
  "materials": [ {
    "pbrMetallicRoughness": {
      "baseColorTexture": { "index": 0, "extensions": {
        "KHR_texture_transform": { "offset": [0.1, 0.2], "scale": [2.0, 0.5] }
      } }
    },
    "normalTexture": { "index": 0 },
    "emissiveFactor": [0.2, 0.3, 0.1],
    "extensions": { "KHR_materials_emissive_strength": { "emissiveStrength": 3.0 } }
  } ],
  "textures": [ { "source": 0 } ],
  "images": [ { "bufferView": 3, "mimeType": "image/png" } ],
  "buffers": [ {
    "byteLength": 165,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4z8AAAAMBAQDJ/pLvAAAAAElFTkSuQmCC"
  } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 36 },
    { "buffer": 0, "byteOffset": 72, "byteLength": 24 },
    { "buffer": 0, "byteOffset": 96, "byteLength": 69 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2" }
  ]
})GLTF";
}

TEST(GltfImportCoreTest, ExtractMeshCarriesTwoDifferentPbrMapUvSetsWithoutAFalseLoss)
{
    const MeshOut out = ExtractPrimitive0(kMismatchedUvGltf);
    ASSERT_TRUE(out.usePbr);
    EXPECT_EQ(60, out.stride);
    EXPECT_TRUE(out.hasSecondTexcoordEXT);
    EXPECT_EQ(0u, out.material.textureCoordinateSetsEXT[
                      static_cast<std::size_t>(TextureSlotEXT::BaseColor)]);
    EXPECT_EQ(1u, out.material.textureCoordinateSetsEXT[
                      static_cast<std::size_t>(TextureSlotEXT::Normal)]);
    EXPECT_TRUE(out.uvSetMismatchedMapsEXT.empty());
}

TEST(GltfImportCoreTest, ExtractMeshDoesNotFlagMatchedPbrMapUvSets)
{
    const MeshOut out = ExtractPrimitive0(kMatchedUvGltf);
    ASSERT_TRUE(out.usePbr);
    EXPECT_TRUE(out.uvSetMismatchedMapsEXT.empty());
}

// DualTextureEffect occlusion brightness fix (CNB-88, Phase 14E). Pixel-value verification
// (the remapped result actually decodes to a halved RGB) is covered end-to-end via Texture2D in
// GltfToCnjToolTests.cpp/RuntimeGltfModelTests.cpp; this file has no GraphicsDevice/Texture2D
// infra, so these two cases stick to what's directly observable here: a valid decode succeeds
// and re-encodes as PNG, and an undecodable input fails gracefully rather than throwing/crashing.
TEST(GltfImportCoreTest, RemapOcclusionImageSucceedsOnAValidPngAndReencodesAsPng)
{
    // The same solid-(255,0,0) 1x1 PNG reused throughout this project's other glTF fixtures
    // (base64 "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4z8AAAAMBAQDJ/pLvAAAAAElFTkSuQmCC"),
    // decoded once to raw bytes to avoid needing a base64 decoder in this test file.
    ExtractedImage input;
    input.bytes = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53,
        0xde, 0x00, 0x00, 0x00, 0x0c, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0x00,
        0x00, 0x03, 0x01, 0x01, 0x00, 0xc9, 0xfe, 0x92, 0xef, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e,
        0x44, 0xae, 0x42, 0x60, 0x82,
    };
    input.extension = "png";

    const auto result = RemapOcclusionImageForDualTextureEXT(input);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->extension, "png");
    EXPECT_FALSE(result->bytes.empty());
    // A real PNG file always starts with this fixed 8-byte signature.
    static const std::uint8_t kPngSignature[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    ASSERT_GE(result->bytes.size(), sizeof(kPngSignature));
    EXPECT_TRUE(std::equal(std::begin(kPngSignature), std::end(kPngSignature), result->bytes.begin()));
}

TEST(GltfImportCoreTest, RemapOcclusionImageReturnsNulloptOnUndecodableInput)
{
    ExtractedImage input;
    input.bytes = {0x00, 0x01, 0x02, 0x03};
    input.extension = "png";

    const auto result = RemapOcclusionImageForDualTextureEXT(input);
    EXPECT_FALSE(result.has_value());
}

TEST(GltfImportCoreTest, DracoUniqueIdsFollowCgltfsFixedUpAttributePointers)
{
    // GLTF-359: IDs are deliberately sparse, out of semantic order and different from the core
    // primitive's accessor mapping. This goes through cgltf_parse itself rather than constructing
    // pointers by hand, so a cgltf upgrade that changes the extension representation breaks this
    // adapter test before it can select the wrong decoded Draco attribute in production.
    const std::string json = R"GLTF({
      "asset": { "version": "2.0" },
      "extensionsUsed": [ "KHR_draco_mesh_compression" ],
      "buffers": [ { "byteLength": 1 } ],
      "bufferViews": [ { "buffer": 0, "byteLength": 1 } ],
      "accessors": [
        { "componentType": 5126, "count": 1, "type": "SCALAR" },
        { "componentType": 5126, "count": 1, "type": "SCALAR" },
        { "componentType": 5126, "count": 1, "type": "SCALAR" },
        { "componentType": 5126, "count": 1, "type": "SCALAR" },
        { "componentType": 5126, "count": 1, "type": "SCALAR" },
        { "componentType": 5126, "count": 1, "type": "SCALAR" },
        { "componentType": 5126, "count": 1, "type": "SCALAR" },
        { "componentType": 5126, "count": 1, "type": "SCALAR" }
      ],
      "meshes": [ { "primitives": [ {
        "attributes": { "POSITION": 0 },
        "extensions": { "KHR_draco_mesh_compression": {
          "bufferView": 0,
          "attributes": { "POSITION": 7, "NORMAL": 2, "TEXCOORD_1": 5 }
        } }
      } ] } ]
    })GLTF";

    cgltf_options options{};
    cgltf_data* data = nullptr;
    ASSERT_EQ(cgltf_result_success, cgltf_parse(&options, json.data(), json.size(), &data));
    ASSERT_NE(nullptr, data);
    ASSERT_EQ(1u, data->meshes_count);
    ASSERT_EQ(1u, data->meshes[0].primitives_count);
    const cgltf_primitive& primitive = data->meshes[0].primitives[0];
    ASSERT_TRUE(primitive.has_draco_mesh_compression);

    EXPECT_EQ(7, FindDracoUniqueIdEXT(primitive, data, cgltf_attribute_type_position, 0));
    EXPECT_EQ(2, FindDracoUniqueIdEXT(primitive, data, cgltf_attribute_type_normal, 0));
    EXPECT_EQ(5, FindDracoUniqueIdEXT(primitive, data, cgltf_attribute_type_texcoord, 1));
    EXPECT_EQ(-1, FindDracoUniqueIdEXT(primitive, data, cgltf_attribute_type_texcoord, 0));
    EXPECT_EQ(-1, FindDracoUniqueIdEXT(primitive, data, cgltf_attribute_type_color, 0));
    EXPECT_EQ(-1, FindDracoUniqueIdEXT(primitive, nullptr, cgltf_attribute_type_position, 0));

    cgltf_free(data);
}

#ifdef CNA_DRACO_AVAILABLE
// Draco mesh compression decoding (CNB-91, Phase 14F). Only compiled when this build actually has
// libdraco support (see CNA_DRACO_AVAILABLE's own doc comment in cmake/CnaLibrary.cmake) --
// mirrors the production code's own #ifdef, so a Draco-less build's test suite has no test to
// skip at all rather than reporting a misleading "SKIPPED".
TEST(GltfImportCoreTest, ExtractMeshDecodesDracoCompressedTriangle)
{
    const MeshOut out = ExtractPrimitive0(kDracoTriangleGltf);

    // glTF's ordinary metallic-roughness material selects the PBR stream even without maps:
    // Position+Normal+generated Tangent+TextureCoordinate (GLTF-215).
    ASSERT_EQ(out.stride, 48);
    ASSERT_FALSE(out.skinned);
    ASSERT_FALSE(out.colored);
    ASSERT_TRUE(out.usePbr);
    ASSERT_EQ(out.vertexBytes.size(), 3u * 48u);

    auto readFloat = [&](std::size_t byteOffset) {
        float v;
        std::memcpy(&v, out.vertexBytes.data() + byteOffset, sizeof(float));
        return v;
    };

    // Vertex 0: Position (0,0,0), Normal (0,0,1), UV (0,0).
    EXPECT_NEAR(readFloat(0), 0.0f, 1e-5f);
    EXPECT_NEAR(readFloat(4), 0.0f, 1e-5f);
    EXPECT_NEAR(readFloat(8), 0.0f, 1e-5f);
    EXPECT_NEAR(readFloat(12), 0.0f, 1e-5f);
    EXPECT_NEAR(readFloat(16), 0.0f, 1e-5f);
    EXPECT_NEAR(readFloat(20), 1.0f, 1e-5f);
    EXPECT_NEAR(readFloat(40), 0.0f, 1e-5f);
    EXPECT_NEAR(readFloat(44), 0.0f, 1e-5f);

    // Vertex 1: Position (1,0,0), UV (1,0).
    EXPECT_NEAR(readFloat(48 + 0), 1.0f, 1e-5f);
    EXPECT_NEAR(readFloat(48 + 4), 0.0f, 1e-5f);
    EXPECT_NEAR(readFloat(48 + 40), 1.0f, 1e-5f);
    EXPECT_NEAR(readFloat(48 + 44), 0.0f, 1e-5f);

    // Vertex 2: Position (0,1,0), UV (0,1).
    EXPECT_NEAR(readFloat(96 + 0), 0.0f, 1e-5f);
    EXPECT_NEAR(readFloat(96 + 4), 1.0f, 1e-5f);
    EXPECT_NEAR(readFloat(96 + 40), 0.0f, 1e-5f);
    EXPECT_NEAR(readFloat(96 + 44), 1.0f, 1e-5f);

    // Draco's own decoded face list drives the index buffer directly (prim.indices has no
    // backing data for a Draco-compressed primitive) -- one triangle, 16-bit indices.
    ASSERT_FALSE(out.use32BitIndices);
    ASSERT_EQ(out.indexBytes.size(), 3u * sizeof(std::uint16_t));
    std::uint16_t i0, i1, i2;
    std::memcpy(&i0, out.indexBytes.data() + 0, 2);
    std::memcpy(&i1, out.indexBytes.data() + 2, 2);
    std::memcpy(&i2, out.indexBytes.data() + 4, 2);
    EXPECT_EQ(i0, 0);
    EXPECT_EQ(i1, 1);
    EXPECT_EQ(i2, 2);
}
#endif

TEST(GltfImportCoreTest, ComputeTangentsEXTAngleWeightsTriangleContributions)
{
    const MeshOut out = ExtractPrimitive0(kAngleWeightedTangentGltf);
    ASSERT_TRUE(out.usePbr);
    ASSERT_EQ(out.stride, 48);
    ASSERT_EQ(out.vertexBytes.size(), 5u * 48u);

    // Vertex 0's own Tangent field: stride 48 = Position(12)+Normal(12)+Tangent(16)+UV(8), vertex
    // 0 is the first 48 bytes, so Tangent starts at byte offset 24.
    float tangent[4];
    std::memcpy(tangent, out.vertexBytes.data() + 24, sizeof(tangent));

    EXPECT_NEAR(tangent[0], 0.91369578f, 1e-4f);
    EXPECT_NEAR(tangent[1], 0.40639884f, 1e-4f);
    EXPECT_NEAR(tangent[2], 0.0f, 1e-4f);
    EXPECT_FLOAT_EQ(tangent[3], 1.0f); // handedness
}

TEST(GltfImportCoreTest, GeneratedTangentsHaveAQuantifiedMikkTSpaceWeldDivergence)
{
    // Reference: upstream mmikk/MikkTSpace commit 3e895b49d05ea07e4c2133156cfa94369e19e409,
    // mikktspace.c SHA-256 de87e74107df766ce68108801262bd8d53899414236b59810509a8fc2a51e288,
    // genTangSpaceDefault() with one callback vertex per face corner. The six values below are its
    // m_setTSpaceBasic results on kMikkWeldDivergenceGltf, not another CNA implementation.
    constexpr std::array<std::array<float, 4>, 6> kMikkReference{{
        {{0.741452575f, 0.671005368f, 0.0f, 1.0f}},
        {{1.000000000f, 0.000000000f, 0.0f, 1.0f}},
        {{0.741452575f, 0.671005368f, 0.0f, 1.0f}},
        {{0.741452575f, 0.671005368f, 0.0f, 1.0f}},
        {{0.741452575f, 0.671005368f, 0.0f, 1.0f}},
        {{0.099503718f, 0.995037138f, 0.0f, 1.0f}},
    }};

    const MeshOut out = ExtractPrimitive0(kMikkWeldDivergenceGltf);
    ASSERT_TRUE(out.usePbr);
    ASSERT_EQ(48, out.stride);
    ASSERT_EQ(6u * 48u, out.vertexBytes.size());

    double squaredDegrees = 0.0;
    double maxDegrees = 0.0;
    for (std::size_t vertex = 0; vertex < kMikkReference.size(); ++vertex)
    {
        std::array<float, 4> actual{};
        std::memcpy(actual.data(), out.vertexBytes.data() + vertex * 48u + 24u,
                    sizeof(actual));

        // Both algorithms still return valid unit frames with the same handedness. The measured
        // difference is the tangent direction Mikk welded across the duplicated shared edge.
        EXPECT_NEAR(1.0f, std::sqrt(actual[0] * actual[0] + actual[1] * actual[1] +
                                    actual[2] * actual[2]), 1e-6f);
        EXPECT_FLOAT_EQ(kMikkReference[vertex][3], actual[3]);
        const float cosine = std::clamp(
            actual[0] * kMikkReference[vertex][0] +
            actual[1] * kMikkReference[vertex][1] +
            actual[2] * kMikkReference[vertex][2], -1.0f, 1.0f);
        const double degrees = std::acos(cosine) * 180.0 / 3.14159265358979323846;
        squaredDegrees += degrees * degrees;
        maxDegrees = std::max(maxDegrees, degrees);
    }

    const double rmsDegrees = std::sqrt(squaredDegrees / kMikkReference.size());
    EXPECT_NEAR(42.1447, maxDegrees, 1e-3);
    EXPECT_NEAR(34.4110, rmsDegrees, 1e-3);
}

#ifdef CNA_DRACO_AVAILABLE
// Regression test for a real bug found while implementing angle-weighted tangent generation
// (CNB-94, Phase 14G): ComputeTangentsEXT's fallback (no TANGENT accessor) used to re-read
// prim.indices directly, which has no backing data for a Draco-compressed primitive (CNB-91's own
// "metadata-only accessor" situation) -- previously an unnoticed correctness gap since 14F's own
// Draco tests all had an explicit TANGENT-free, non-PBR material. This fixture is the same
// Draco-compressed triangle as kDracoTriangleGltf, but with a normalTexture (forcing usePbr=true
// and no TANGENT attribute in the Draco stream), so ComputeTangentsEXT's fallback actually runs
// against Draco-sourced indices.
TEST(GltfImportCoreTest, ComputeTangentsEXTWorksOnADracoCompressedPbrPrimitiveWithNoTangentAccessor)
{
    const char* kDracoPbrNoTangentGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "extensionsUsed": [ "KHR_draco_mesh_compression" ],
  "extensionsRequired": [ "KHR_draco_mesh_compression" ],
  "meshes": [ { "primitives": [ {
      "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 },
      "material": 0,
      "extensions": {
        "KHR_draco_mesh_compression": {
          "bufferView": 0,
          "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 }
        }
      }
  } ] } ],
  "materials": [ { "normalTexture": { "index": 0 } } ],
  "textures": [ { "source": 0 } ],
  "images": [ { "bufferView": 1, "mimeType": "image/png" } ],
  "buffers": [ {
    "byteLength": 225,
    "uri": "data:application/octet-stream;base64,RFJBQ08CAgEBAAAAAwECAQAAAQf/AREBAQABAQAD/wAAAAAAAQAAAQAJAwAAAAEBCQMAAQABAwkCAAIAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AACAPwAAAAAAAAAAAACAPwAAAAAAAAAAiVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4z8AAAAMBAQDJ/pLvAAAAAElFTkSuQmCC"
  } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,   "byteLength": 156 },
    { "buffer": 0, "byteOffset": 156, "byteLength": 69 }
  ],
  "accessors": [
    { "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "componentType": 5126, "count": 3, "type": "VEC3" },
    { "componentType": 5126, "count": 3, "type": "VEC2" }
  ]
})GLTF";

    const MeshOut out = ExtractPrimitive0(kDracoPbrNoTangentGltf);
    ASSERT_TRUE(out.usePbr);
    ASSERT_EQ(out.stride, 48);
    ASSERT_EQ(out.vertexBytes.size(), 3u * 48u);
    ASSERT_EQ(out.indexBytes.size(), 3u * sizeof(std::uint16_t));

    // The exact tangent value isn't the point here (this triangle's own values are already
    // covered byte-for-byte by ExtractMeshDecodesDracoCompressedTriangle) -- this test's own job
    // is proving ComputeTangentsEXT no longer reads through the Draco-compressed primitive's own
    // backing-less prim.indices (previously undefined behavior/garbage indices), by asserting
    // every vertex's Tangent is a finite, genuinely-unit-length-in-plane vector.
    for (int v = 0; v < 3; ++v)
    {
        float tangent[4];
        std::memcpy(tangent, out.vertexBytes.data() + static_cast<std::size_t>(v) * 48 + 24, sizeof(tangent));
        ASSERT_TRUE(std::isfinite(tangent[0]));
        ASSERT_TRUE(std::isfinite(tangent[1]));
        ASSERT_TRUE(std::isfinite(tangent[2]));
        const float lenSq = tangent[0] * tangent[0] + tangent[1] * tangent[1] + tangent[2] * tangent[2];
        EXPECT_NEAR(lenSq, 1.0f, 1e-4f);
        EXPECT_TRUE(tangent[3] == 1.0f || tangent[3] == -1.0f);
    }
}
#endif

TEST(GltfImportCoreTest, ExtractMeshCarriesTextureTransformAndEmissiveStrength)
{
    const MeshOut out = ExtractPrimitive0(kTextureTransformAndEmissiveStrengthGltf);
    ASSERT_TRUE(out.usePbr);
    ASSERT_EQ(out.stride, 48);
    ASSERT_EQ(out.vertexBytes.size(), 3u * 48u);

    // KHR_materials_emissive_strength: [0.2,0.3,0.1] * 3.0 = [0.6,0.9,0.3], not clamped to [0,1]
    // (unlike ExtractPunctualLightsEXT's own DiffuseColor) -- glTF's emissive-strength extension
    // exists specifically to allow real HDR emissive values beyond 1.0.
    EXPECT_NEAR(out.material.emissiveFactor.X, 0.6f, 1e-5f);
    EXPECT_NEAR(out.material.emissiveFactor.Y, 0.9f, 1e-5f);
    EXPECT_NEAR(out.material.emissiveFactor.Z, 0.3f, 1e-5f);

    // GLTF-184: authored UV bytes remain authored; the transform is independent material state so
    // each of the five maps may transform a shared stream differently in the shader.
    auto readUv = [&](std::size_t vertexIndex) {
        float uv[2];
        std::memcpy(uv, out.vertexBytes.data() + vertexIndex * 48 + 40, sizeof(uv));
        return std::pair<float, float>(uv[0], uv[1]);
    };
    EXPECT_EQ(readUv(0), (std::pair<float, float>{0.0f, 0.0f}));
    EXPECT_EQ(readUv(1), (std::pair<float, float>{1.0f, 0.0f}));
    EXPECT_EQ(readUv(2), (std::pair<float, float>{0.0f, 1.0f}));

    const auto& transform = out.material.textureTransformsEXT[
        static_cast<std::size_t>(TextureSlotEXT::BaseColor)];
    EXPECT_FLOAT_EQ(transform.Offset.X, 0.1f);
    EXPECT_FLOAT_EQ(transform.Offset.Y, 0.2f);
    EXPECT_FLOAT_EQ(transform.Scale.X, 2.0f);
    EXPECT_FLOAT_EQ(transform.Scale.Y, 0.5f);
    EXPECT_FLOAT_EQ(transform.Rotation, 0.0f);
}

TEST(GltfImportCoreTest, ExtractPunctualLightsEXTApproximatesDirectionalAndPointLights)
{
    const char* kLightsGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0, 1] } ],
  "nodes": [
    { "name": "DirLight", "translation": [0, 5, 0], "rotation": [-0.7071068, 0, 0, 0.7071068], "extensions": { "KHR_lights_punctual": { "light": 0 } } },
    { "name": "PointLight", "translation": [0, 0, -5], "extensions": { "KHR_lights_punctual": { "light": 1 } } }
  ],
  "extensions": {
    "KHR_lights_punctual": {
      "lights": [
        { "type": "directional", "color": [1, 0, 0], "intensity": 1.0 },
        { "type": "point", "color": [0, 1, 0], "intensity": 1.0 }
      ]
    }
  },
  "extensionsUsed": [ "KHR_lights_punctual" ]
})GLTF";

    ScratchDir dir;
    const std::filesystem::path gltfPath = dir.path() / "lights.gltf";
    WriteFile(gltfPath, kLightsGltf);

    cgltf_options options{};
    cgltf_data* data = nullptr;
    ASSERT_EQ(cgltf_parse_file(&options, gltfPath.string().c_str(), &data), cgltf_result_success);
    ASSERT_EQ(cgltf_load_buffers(&options, data, gltfPath.string().c_str()), cgltf_result_success);

    const auto lights = ExtractPunctualLightsEXT(data);
    ASSERT_EQ(lights.size(), 2u);

    // Light 0: directional, rotated -90 degrees about X from its own local -Z -- glTF's own
    // rotation quaternion (-0.7071068,0,0,0.7071068) applied to (0,0,-1) yields world direction
    // (0,-1,0) (points straight down), independently verified via scipy's own quaternion rotation.
    EXPECT_NEAR(lights[0].direction.X, 0.0f, 1e-4f);
    EXPECT_NEAR(lights[0].direction.Y, -1.0f, 1e-4f);
    EXPECT_NEAR(lights[0].direction.Z, 0.0f, 1e-4f);
    EXPECT_NEAR(lights[0].diffuseColor.X, 1.0f, 1e-5f);
    EXPECT_NEAR(lights[0].diffuseColor.Y, 0.0f, 1e-5f);
    EXPECT_NEAR(lights[0].diffuseColor.Z, 0.0f, 1e-5f);

    // Light 1: point light at world position (0,0,-5) -- approximated as directional pointing from
    // the light toward the scene origin, i.e. direction = normalize(-worldPos) = (0,0,1).
    EXPECT_NEAR(lights[1].direction.X, 0.0f, 1e-4f);
    EXPECT_NEAR(lights[1].direction.Y, 0.0f, 1e-4f);
    EXPECT_NEAR(lights[1].direction.Z, 1.0f, 1e-4f);
    EXPECT_NEAR(lights[1].diffuseColor.X, 0.0f, 1e-5f);
    EXPECT_NEAR(lights[1].diffuseColor.Y, 1.0f, 1e-5f);
    EXPECT_NEAR(lights[1].diffuseColor.Z, 0.0f, 1e-5f);

    cgltf_free(data);
}

// --- plan_gltf.md GLTF-326: what the three-directional-light approximation costs ----------------

namespace
{
    /// A scene of `count` lights built from `entries`, one node each at the given positions.
    std::string LightSceneDocument(const std::string& lightEntries, std::size_t count)
    {
        std::string nodes;
        std::string sceneNodes;
        for (std::size_t i = 0; i < count; ++i)
        {
            if (i != 0) { nodes += ", "; sceneNodes += ", "; }
            nodes += "{ \"name\": \"L" + std::to_string(i) +
                     "\", \"translation\": [0, 0, -5], \"extensions\": { "
                     "\"KHR_lights_punctual\": { \"light\": " + std::to_string(i) + " } } }";
            sceneNodes += std::to_string(i);
        }
        return std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [)GLTF") + sceneNodes + R"GLTF(] } ],
  "nodes": [ )GLTF" + nodes + R"GLTF( ],
  "extensions": { "KHR_lights_punctual": { "lights": [ )GLTF" + lightEntries + R"GLTF( ] } },
  "extensionsUsed": [ "KHR_lights_punctual" ]
})GLTF";
    }

    /// Parses a light-scene document into a guard-owned cgltf_data.
    struct LightScene
    {
        cgltf_data* data = nullptr;
        ~LightScene() { if (data != nullptr) { cgltf_free(data); } }
        LightScene() = default;
        LightScene(const LightScene&) = delete;
        LightScene& operator=(const LightScene&) = delete;
    };

    bool ParseLightScene(LightScene& out, const std::string& json)
    {
        cgltf_options options{};
        return cgltf_parse(&options, json.data(), json.size(), &out.data) == cgltf_result_success;
    }
}

TEST(GltfImportCoreTest, LightReportNamesEveryLightBeyondTheThreeXnaCanBind)
{
    // "3 lights imported" is not actionable and "3 of 6 imported" is. Counting the drop means the
    // loop can no longer stop at three, which is the one behavioural change GLTF-326 makes.
    std::string entries;
    for (int i = 0; i < 6; ++i)
    {
        if (i != 0) { entries += ", "; }
        entries += R"({ "type": "directional", "color": [1, 1, 1], "intensity": 1.0 })";
    }
    LightScene scene;
    ASSERT_TRUE(ParseLightScene(scene, LightSceneDocument(entries, 6)));

    LightReportEXT report;
    const std::vector<LightOut> lights = ExtractPunctualLightsEXT(scene.data, report);
    EXPECT_EQ(3u, lights.size()) << "the three-light cap itself must not have changed";
    EXPECT_EQ(3u, report.droppedLightCount);
    EXPECT_TRUE(report.AnythingLost());
}

TEST(GltfImportCoreTest, LightReportCountsPointAndSpotApproximationsApart)
{
    // A point light loses its falloff; a spot loses its falloff AND its cone. The approximation is
    // materially worse for one than the other, so one combined "approximated" count would hide the
    // difference an author most needs to see.
    LightScene scene;
    ASSERT_TRUE(ParseLightScene(scene, LightSceneDocument(
        R"({ "type": "point", "color": [1, 1, 1], "intensity": 1.0 },
            { "type": "spot", "color": [1, 1, 1], "intensity": 1.0,
              "spot": { "innerConeAngle": 0.2, "outerConeAngle": 0.5 } },
            { "type": "directional", "color": [1, 1, 1], "intensity": 1.0 })", 3)));

    LightReportEXT report;
    const std::vector<LightOut> lights = ExtractPunctualLightsEXT(scene.data, report);
    ASSERT_EQ(3u, lights.size());
    EXPECT_EQ(1u, report.approximatedPointLightCount);
    EXPECT_EQ(1u, report.approximatedSpotLightCount);
    EXPECT_EQ(0u, report.droppedLightCount) << "three lights fit; nothing was dropped";
}

TEST(GltfImportCoreTest, LightReportNamesAnIntensityClampedOutOfGamut)
{
    // glTF intensity is photometric and unbounded -- lux for a directional light, candela for the
    // others -- while DiffuseColor is a [0,1] colour. 683 lm/W is the luminous efficacy constant
    // and turns up in real files; it imports as plain white, which is not a bug and is absolutely
    // something an author comparing renders deserves to be told.
    LightScene scene;
    ASSERT_TRUE(ParseLightScene(scene, LightSceneDocument(
        R"({ "type": "directional", "color": [1, 0.5, 0.25], "intensity": 683.0 },
            { "type": "directional", "color": [1, 1, 1], "intensity": 1.0 })", 2)));

    LightReportEXT report;
    const std::vector<LightOut> lights = ExtractPunctualLightsEXT(scene.data, report);
    ASSERT_EQ(2u, lights.size());
    EXPECT_EQ(1u, report.clampedIntensityLightCount) << "only the out-of-gamut light counts";
    EXPECT_NEAR(683.0f, report.worstPreClampChannelEXT, 1e-3f)
        << "the worst pre-clamp channel is what says whether the clamp was marginal or total";

    // And the clamp itself still happens -- the report describes the behaviour, it does not
    // replace it.
    EXPECT_NEAR(1.0f, lights[0].diffuseColor.X, 1e-5f);
    EXPECT_NEAR(1.0f, lights[0].diffuseColor.Y, 1e-5f);
    EXPECT_NEAR(1.0f, lights[0].diffuseColor.Z, 1e-5f);
}

TEST(GltfImportCoreTest, LightReportIsEmptyForAFileAlreadyInsideXnasLightingModel)
{
    // The control. Without it, a report that fired on every file would pass all three tests above
    // and turn every ordinary import into a warning nobody reads.
    LightScene scene;
    ASSERT_TRUE(ParseLightScene(scene, LightSceneDocument(
        R"({ "type": "directional", "color": [1, 1, 1], "intensity": 1.0 },
            { "type": "directional", "color": [0.5, 0.5, 0.5], "intensity": 1.0 })", 2)));

    LightReportEXT report;
    const std::vector<LightOut> lights = ExtractPunctualLightsEXT(scene.data, report);
    EXPECT_EQ(2u, lights.size());
    EXPECT_FALSE(report.AnythingLost())
        << "a file XNA can light exactly must produce no diagnostic at all";
}

TEST(GltfImportCoreTest, BothLightOverloadsExtractIdenticalLights)
{
    // The reporting overload must be the same extraction with a second output, not a second
    // implementation that can drift from the one every existing caller uses.
    LightScene scene;
    ASSERT_TRUE(ParseLightScene(scene, LightSceneDocument(
        R"({ "type": "point", "color": [1, 0, 0], "intensity": 1.0 },
            { "type": "directional", "color": [0, 1, 0], "intensity": 2.0 },
            { "type": "spot", "color": [0, 0, 1], "intensity": 0.5,
              "spot": { "outerConeAngle": 0.5 } },
            { "type": "directional", "color": [1, 1, 1], "intensity": 1.0 })", 4)));

    LightReportEXT report;
    const std::vector<LightOut> reported = ExtractPunctualLightsEXT(scene.data, report);
    const std::vector<LightOut> plain = ExtractPunctualLightsEXT(scene.data);
    ASSERT_EQ(plain.size(), reported.size());
    for (std::size_t i = 0; i < plain.size(); ++i)
    {
        SCOPED_TRACE("light " + std::to_string(i));
        EXPECT_EQ(plain[i].direction, reported[i].direction);
        EXPECT_EQ(plain[i].diffuseColor, reported[i].diffuseColor);
    }
    EXPECT_EQ(1u, report.droppedLightCount);
}

TEST(GltfImportCoreTest, ExtractPunctualLightsEXTCapsAtThreeLights)
{
    const char* kFourLightsGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0, 1, 2, 3] } ],
  "nodes": [
    { "name": "L0", "extensions": { "KHR_lights_punctual": { "light": 0 } } },
    { "name": "L1", "extensions": { "KHR_lights_punctual": { "light": 1 } } },
    { "name": "L2", "extensions": { "KHR_lights_punctual": { "light": 2 } } },
    { "name": "L3", "extensions": { "KHR_lights_punctual": { "light": 3 } } }
  ],
  "extensions": {
    "KHR_lights_punctual": {
      "lights": [
        { "type": "directional", "color": [1, 1, 1], "intensity": 1.0 },
        { "type": "directional", "color": [1, 1, 1], "intensity": 1.0 },
        { "type": "directional", "color": [1, 1, 1], "intensity": 1.0 },
        { "type": "directional", "color": [1, 1, 1], "intensity": 1.0 }
      ]
    }
  },
  "extensionsUsed": [ "KHR_lights_punctual" ]
})GLTF";

    ScratchDir dir;
    const std::filesystem::path gltfPath = dir.path() / "fourlights.gltf";
    WriteFile(gltfPath, kFourLightsGltf);

    cgltf_options options{};
    cgltf_data* data = nullptr;
    ASSERT_EQ(cgltf_parse_file(&options, gltfPath.string().c_str(), &data), cgltf_result_success);
    ASSERT_EQ(cgltf_load_buffers(&options, data, gltfPath.string().c_str()), cgltf_result_success);

    ASSERT_EQ(data->lights_count, 4u);
    const auto lights = ExtractPunctualLightsEXT(data);
    EXPECT_EQ(lights.size(), 3u); // capped, not all 4 -- matches every CNA stock effect's own MaxLights=3.

    cgltf_free(data);
}

// --- plan_gltf.md GLTF-173: normals for a primitive that authors none ---------------------------

namespace
{
    /// A single-primitive document over the given positions and indices, with no NORMAL attribute.
    /// Positions are written as a base64 data: URI so the fixture needs no sidecar.
    std::string NormalLessDocument(const std::vector<float>& positions,
                                    const std::vector<std::uint16_t>& indices)
    {
        std::vector<std::uint8_t> buffer;
        for (const float value : positions)
        {
            std::uint8_t bytes[4];
            std::memcpy(bytes, &value, 4);
            buffer.insert(buffer.end(), bytes, bytes + 4);
        }
        const std::size_t indexOffset = buffer.size();
        for (const std::uint16_t value : indices)
        {
            buffer.push_back(static_cast<std::uint8_t>(value & 0xFF));
            buffer.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
        }
        while (buffer.size() % 4 != 0) { buffer.push_back(0); }

        static const char* kAlphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string base64;
        for (std::size_t i = 0; i < buffer.size(); i += 3)
        {
            const std::uint32_t chunk =
                (static_cast<std::uint32_t>(buffer[i]) << 16) |
                (i + 1 < buffer.size() ? static_cast<std::uint32_t>(buffer[i + 1]) << 8 : 0u) |
                (i + 2 < buffer.size() ? static_cast<std::uint32_t>(buffer[i + 2]) : 0u);
            base64 += kAlphabet[(chunk >> 18) & 0x3F];
            base64 += kAlphabet[(chunk >> 12) & 0x3F];
            base64 += (i + 1 < buffer.size()) ? kAlphabet[(chunk >> 6) & 0x3F] : '=';
            base64 += (i + 2 < buffer.size()) ? kAlphabet[chunk & 0x3F] : '=';
        }

        return std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ { "primitives": [ {
      "attributes": { "POSITION": 0 }, "indices": 1, "mode": 4
  } ] } ],
  "buffers": [ { "byteLength": )GLTF") + std::to_string(buffer.size()) +
               R"GLTF(, "uri": "data:application/octet-stream;base64,)GLTF" + base64 + R"GLTF(" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": )GLTF" + std::to_string(indexOffset) + R"GLTF( },
    { "buffer": 0, "byteOffset": )GLTF" + std::to_string(indexOffset) +
               R"GLTF(, "byteLength": )GLTF" + std::to_string(indices.size() * 2) + R"GLTF( }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": )GLTF" +
               std::to_string(positions.size() / 3) + R"GLTF(, "type": "VEC3" },
    { "bufferView": 1, "componentType": 5123, "count": )GLTF" +
               std::to_string(indices.size()) + R"GLTF(, "type": "SCALAR" }
  ]
})GLTF";
    }

    MeshOut ExtractNormalLess(const std::vector<float>& positions,
                              const std::vector<std::uint16_t>& indices)
    {
        const std::string json = NormalLessDocument(positions, indices);
        cgltf_options options{};
        cgltf_data* data = nullptr;
        EXPECT_EQ(cgltf_result_success, cgltf_parse(&options, json.data(), json.size(), &data));
        if (data == nullptr) { return MeshOut{}; }
        EXPECT_EQ(cgltf_result_success, cgltf_load_buffers(&options, data, "."));
        MeshOut out = ExtractMesh(data, data->meshes[0].primitives[0], "probe", nullptr, 1.0f);
        cgltf_free(data);
        return out;
    }

    /// The Normal element of a packed vertex, read through the canonical stride table rather than
    /// a hardcoded offset -- the lesson of GLTF-278.
    std::array<float, 3> NormalOfVertex(const MeshOut& mesh, std::size_t vertex)
    {
        const CNA::Internal::Graphics::InferredVertexLayout layout =
            CNA::Internal::Graphics::InferredLayoutForStride(
                mesh.stride, CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt);
        EXPECT_TRUE(layout.known) << "stride " << mesh.stride << " is not in the canonical table";
        int offset = -1;
        for (std::size_t i = 0; layout.known && i < layout.count; ++i)
        {
            if (layout.elements[i].usage ==
                    Microsoft::Xna::Framework::Graphics::VertexElementUsage::Normal &&
                layout.elements[i].usageIndex == 0)
            {
                offset = layout.elements[i].offset;
            }
        }
        EXPECT_GE(offset, 0);
        std::array<float, 3> normal{};
        if (offset >= 0)
        {
            std::memcpy(normal.data(),
                        mesh.vertexBytes.data() +
                            vertex * static_cast<std::size_t>(mesh.stride) +
                            static_cast<std::size_t>(offset),
                        sizeof(normal));
        }
        return normal;
    }
}

TEST(GltfImportCoreTest, AbsentNormalsAreComputedFromTheFaceRatherThanFabricatedAsPlusZ)
{
    // §3.7.2.1 makes calculating flat normals a MUST. CNA wrote (0,0,1) on every vertex instead --
    // a surface facing +Z regardless of where it points. The triangle is tilted out of the XY plane
    // precisely so the two answers differ: cross((1,0,0),(0,1,1)) = (0,-1,1).
    const MeshOut mesh = ExtractNormalLess({0, 0, 0, 1, 0, 0, 0, 1, 1}, {0, 1, 2});
    ASSERT_EQ(3u, mesh.vertexBytes.size() / static_cast<std::size_t>(mesh.stride));
    EXPECT_TRUE(mesh.generatedNormalsEXT);
    EXPECT_EQ(0u, mesh.flatNormalDuplicatedVertexCountEXT)
        << "one triangle shares no vertex, so the flat normal is exact and nothing had to split";
    EXPECT_EQ(0u, mesh.flatNormalMergedVertexCountEXT);

    const float invSqrt2 = 1.0f / std::sqrt(2.0f);
    for (std::size_t v = 0; v < 3; ++v)
    {
        SCOPED_TRACE("vertex " + std::to_string(v));
        const std::array<float, 3> normal = NormalOfVertex(mesh, v);
        EXPECT_NEAR(0.0f, normal[0], 1e-5f);
        EXPECT_NEAR(-invSqrt2, normal[1], 1e-5f);
        EXPECT_NEAR(invSqrt2, normal[2], 1e-5f);
    }
}

TEST(GltfImportCoreTest, AnAuthoredPlanarTriangleStillGetsExactlyPlusZ)
{
    // The control, and the reason the rest of the corpus did not move: a planar CCW triangle's own
    // face normal IS (0,0,1), so every other normal-less fixture reads identically under the old
    // behaviour and the new one. Without this test that agreement looks like the change not having
    // taken effect.
    const MeshOut mesh = ExtractNormalLess({0, 0, 0, 1, 0, 0, 0, 1, 0}, {0, 1, 2});
    EXPECT_TRUE(mesh.generatedNormalsEXT);
    const std::array<float, 3> normal = NormalOfVertex(mesh, 0);
    EXPECT_NEAR(0.0f, normal[0], 1e-6f);
    EXPECT_NEAR(0.0f, normal[1], 1e-6f);
    EXPECT_NEAR(1.0f, normal[2], 1e-6f);
}

namespace
{
    /// The element with the given usage on a packed vertex, read through the canonical stride table.
    std::array<float, 4> ElementOfVertex(const MeshOut& mesh, std::size_t vertex,
                                         Microsoft::Xna::Framework::Graphics::VertexElementUsage usage,
                                         std::size_t floats)
    {
        const CNA::Internal::Graphics::InferredVertexLayout layout =
            CNA::Internal::Graphics::InferredLayoutForStride(
                mesh.stride, CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt);
        EXPECT_TRUE(layout.known) << "stride " << mesh.stride << " is not in the canonical table";
        int offset = -1;
        for (std::size_t i = 0; layout.known && i < layout.count; ++i)
        {
            if (layout.elements[i].usage == usage && layout.elements[i].usageIndex == 0)
            {
                offset = layout.elements[i].offset;
            }
        }
        EXPECT_GE(offset, 0);
        std::array<float, 4> value{};
        if (offset >= 0)
        {
            std::memcpy(value.data(),
                        mesh.vertexBytes.data() +
                            vertex * static_cast<std::size_t>(mesh.stride) +
                            static_cast<std::size_t>(offset),
                        floats * sizeof(float));
        }
        return value;
    }

    std::array<float, 3> PositionOfVertex(const MeshOut& mesh, std::size_t vertex)
    {
        const std::array<float, 4> p = ElementOfVertex(
            mesh, vertex, Microsoft::Xna::Framework::Graphics::VertexElementUsage::Position, 3);
        return {p[0], p[1], p[2]};
    }

    std::size_t VertexCountOf(const MeshOut& mesh)
    {
        return mesh.stride > 0 ? mesh.vertexBytes.size() / static_cast<std::size_t>(mesh.stride) : 0u;
    }

    std::vector<std::uint32_t> IndicesOf(const MeshOut& mesh)
    {
        std::vector<std::uint32_t> indices;
        if (mesh.use32BitIndices)
        {
            for (std::size_t i = 0; i + 3 < mesh.indexBytes.size(); i += 4)
            {
                std::uint32_t value = 0;
                std::memcpy(&value, mesh.indexBytes.data() + i, 4);
                indices.push_back(value);
            }
            return indices;
        }
        for (std::size_t i = 0; i + 1 < mesh.indexBytes.size(); i += 2)
        {
            std::uint16_t value = 0;
            std::memcpy(&value, mesh.indexBytes.data() + i, 2);
            indices.push_back(value);
        }
        return indices;
    }

    /// Asserts §3.7.2.1's own definition of flat shading, remap-independently: every corner of
    /// every emitted triangle carries that triangle's own geometric normal.
    ///
    /// Deliberately derived from the EMITTED positions and indices rather than from a hand-computed
    /// vertex table, so the assertion cannot be satisfied by reproducing whatever numbering the
    /// split happened to choose. An averaging implementation fails it on any folded mesh; a
    /// per-face-duplicating one passes whatever order it hands the new vertices out in.
    void ExpectEveryFaceIsFlat(const MeshOut& mesh)
    {
        const std::vector<std::uint32_t> indices = IndicesOf(mesh);
        const std::size_t vertices = VertexCountOf(mesh);
        ASSERT_GE(indices.size(), 3u);
        for (std::size_t f = 0; f + 2 < indices.size(); f += 3)
        {
            SCOPED_TRACE("triangle at index " + std::to_string(f));
            ASSERT_LT(indices[f], vertices);
            ASSERT_LT(indices[f + 1], vertices);
            ASSERT_LT(indices[f + 2], vertices);
            const std::array<float, 3> a = PositionOfVertex(mesh, indices[f]);
            const std::array<float, 3> b = PositionOfVertex(mesh, indices[f + 1]);
            const std::array<float, 3> c = PositionOfVertex(mesh, indices[f + 2]);
            const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
            const float e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
            const float cross[3] = {e1[1] * e2[2] - e1[2] * e2[1],
                                    e1[2] * e2[0] - e1[0] * e2[2],
                                    e1[0] * e2[1] - e1[1] * e2[0]};
            const float length =
                std::sqrt(cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2]);
            ASSERT_GT(length, 1e-6f) << "degenerate fixture triangle proves nothing";
            const std::array<float, 3> expected{cross[0] / length, cross[1] / length,
                                                cross[2] / length};
            for (std::size_t k = 0; k < 3; ++k)
            {
                SCOPED_TRACE("corner " + std::to_string(k));
                const std::array<float, 3> normal = NormalOfVertex(mesh, indices[f + k]);
                EXPECT_NEAR(expected[0], normal[0], 1e-5f);
                EXPECT_NEAR(expected[1], normal[1], 1e-5f);
                EXPECT_NEAR(expected[2], normal[2], 1e-5f);
            }
        }
    }
}

TEST(GltfImportCoreTest, AVertexSharedAcrossFacesOfDifferentOrientationIsSplitSoEveryFaceIsFlat)
{
    // plan_gltf.md GLTF-461. Four triangles: (0,1,2) and (0,2,3) are a quad in the XY plane, and
    // (3,2,4) and (2,5,4) fold up out of it. Vertices 2 and 3 belong to faces of BOTH orientations,
    // so flat shading needs each of them duplicated once per orientation. GLTF-173 averaged them
    // instead and counted the approximation; this asserts the split.
    //
    // The discrimination is in the two face normals: +Z and (0,-1/sqrt2,1/sqrt2). Their
    // area-weighted average is neither, so the old behaviour cannot pass ExpectEveryFaceIsFlat by
    // accident -- and the vertex count it produced (6) is not the one asserted here.
    const MeshOut mesh = ExtractNormalLess(
        {0, 0, 0,   1, 0, 0,   1, 1, 0,   0, 1, 0,   0, 2, 1,   1, 2, 1},
        {0, 1, 2,  0, 2, 3,  3, 2, 4,  2, 5, 4});
    EXPECT_TRUE(mesh.generatedNormalsEXT);
    EXPECT_EQ(2u, mesh.flatNormalDuplicatedVertexCountEXT)
        << "exactly the two vertices on the fold need a second copy; a larger number means the "
           "split is not minimal and a smaller one means it did not happen";
    EXPECT_EQ(8u, VertexCountOf(mesh));
    EXPECT_EQ(0u, mesh.flatNormalMergedVertexCountEXT)
        << "no two faces here are parallel-but-not-identical, so nothing may be averaged at all";
    EXPECT_FALSE(mesh.morphedFlatNormalsEXT) << "there are no morph targets to recompute for";

    ExpectEveryFaceIsFlat(mesh);

    // Every duplicate carries its source vertex's own position: the split renumbers, it does not
    // move geometry. Vertices 2/3 appear twice, at (1,1,0) and (0,1,0) respectively.
    int atSharedEdge = 0;
    for (std::size_t v = 0; v < VertexCountOf(mesh); ++v)
    {
        const std::array<float, 3> p = PositionOfVertex(mesh, v);
        if ((p[0] == 1.0f && p[1] == 1.0f && p[2] == 0.0f) ||
            (p[0] == 0.0f && p[1] == 1.0f && p[2] == 0.0f))
        {
            ++atSharedEdge;
        }
    }
    EXPECT_EQ(4, atSharedEdge) << "the two fold vertices should each appear exactly twice";
}

TEST(GltfImportCoreTest, CoplanarSharedVerticesAreNotDuplicated)
{
    // The minimality control, and the reason the generated corpus barely moved: a quad split into
    // two coplanar triangles shares two vertices, and both faces want the SAME normal, so no
    // duplication is needed. An implementation that simply de-indexed every primitive into a
    // triangle soup would return six vertices here and pass every flat-normal assertion while
    // inflating every normal-less mesh in the corpus by 50%.
    const MeshOut mesh = ExtractNormalLess(
        {0, 0, 0,   1, 0, 0,   1, 1, 0,   0, 1, 0},
        {0, 1, 2,  0, 2, 3});
    EXPECT_TRUE(mesh.generatedNormalsEXT);
    EXPECT_EQ(0u, mesh.flatNormalDuplicatedVertexCountEXT);
    EXPECT_EQ(4u, VertexCountOf(mesh));
    const std::vector<std::uint32_t> indices = IndicesOf(mesh);
    EXPECT_EQ(std::vector<std::uint32_t>({0, 1, 2, 0, 2, 3}), indices)
        << "an unsplit primitive must keep its own numbering, so its bytes are unchanged";
    ExpectEveryFaceIsFlat(mesh);
}

TEST(GltfImportCoreTest, AVertexNoFaceTouchesKeepsItsSlotAndThePlaceholderNormal)
{
    // A fourth position no index references. glTF states no normal for such a vertex, and dropping
    // it would renumber every index for no gain -- so the slot survives and carries the packing
    // loop's own placeholder, exactly as it did before the split existed.
    const MeshOut mesh = ExtractNormalLess(
        {0, 0, 0,   1, 0, 0,   0, 1, 1,   9, 9, 9},
        {0, 1, 2});
    EXPECT_EQ(4u, VertexCountOf(mesh));
    EXPECT_EQ(0u, mesh.flatNormalDuplicatedVertexCountEXT);
    const std::array<float, 3> orphan = NormalOfVertex(mesh, 3);
    EXPECT_NEAR(0.0f, orphan[0], 1e-6f);
    EXPECT_NEAR(0.0f, orphan[1], 1e-6f);
    EXPECT_NEAR(1.0f, orphan[2], 1e-6f);
    const std::array<float, 3> position = PositionOfVertex(mesh, 3);
    EXPECT_FLOAT_EQ(9.0f, position[0]);
}

TEST(GltfImportCoreTest, ADegenerateFaceNeverForcesADuplicate)
{
    // A zero-area triangle has no orientation to disagree with, so it must not open a second group
    // on a vertex it shares with a real face -- which would duplicate vertices and hand one copy a
    // fabricated normal. Triangle (0,1,2) is real; (0,1,1) is degenerate and shares two of its
    // vertices.
    const MeshOut mesh = ExtractNormalLess(
        {0, 0, 0,   1, 0, 0,   0, 1, 1},
        {0, 1, 2,  0, 1, 1});
    EXPECT_TRUE(mesh.generatedNormalsEXT);
    EXPECT_EQ(0u, mesh.flatNormalDuplicatedVertexCountEXT);
    EXPECT_EQ(3u, VertexCountOf(mesh));
    const float invSqrt2 = 1.0f / std::sqrt(2.0f);
    for (std::size_t v = 0; v < 3; ++v)
    {
        SCOPED_TRACE("vertex " + std::to_string(v));
        const std::array<float, 3> normal = NormalOfVertex(mesh, v);
        EXPECT_NEAR(0.0f, normal[0], 1e-5f);
        EXPECT_NEAR(-invSqrt2, normal[1], 1e-5f);
        EXPECT_NEAR(invSqrt2, normal[2], 1e-5f);
    }
}


TEST(GltfImportCoreTest, AnAuthoredTangentIsIgnoredWhenTheFileAuthorsNoNormal)
{
    // plan_gltf.md GLTF-464: this was an inline document until the corpus could grow. It is a
    // conformance statement about the format -- §3.7.2.1's "the provided tangents (if present) MUST
    // be ignored" -- so `docs/gltf-conformance.md` §3.8 puts it in `tools/gltf_fixtures/`, where the
    // L3 expectation is derived independently and the four L7 policies render it.
    //
    // The fixture's authored tangent is (0,1,0,+1) and the tangent §3.7.2.1 requires a reader to
    // regenerate is (1,0,0,+1): no shared component, so honouring the authored one is a different
    // vector rather than a near miss.
    const std::filesystem::path gltfPath =
        CnaTest::GltfOracle::CorpusDirectory() / "tangent-without-normal.gltf";
    ASSERT_TRUE(std::filesystem::is_regular_file(gltfPath)) << gltfPath;

    cgltf_options options{};
    cgltf_data* data = nullptr;
    ASSERT_EQ(cgltf_result_success,
              cgltf_parse_file(&options, gltfPath.string().c_str(), &data));
    ASSERT_NE(nullptr, data);
    ASSERT_EQ(cgltf_result_success,
              cgltf_load_buffers(&options, data, gltfPath.string().c_str()));

    const MeshOut mesh = ExtractMesh(data, data->meshes[0].primitives[0], "probe", nullptr, 1.0f);
    cgltf_free(data);

    ASSERT_EQ(48, mesh.stride);
    EXPECT_TRUE(mesh.generatedNormalsEXT);
    EXPECT_TRUE(mesh.ignoredTangentForGeneratedNormalsEXT)
        << "§3.7.2.1 requires the authored tangent to be ignored, and dropping it silently is what "
           "this flag exists to prevent";

    const float invSqrt2 = 1.0f / std::sqrt(2.0f);
    for (std::size_t v = 0; v < 3; ++v)
    {
        SCOPED_TRACE("vertex " + std::to_string(v));
        const std::array<float, 3> n = NormalOfVertex(mesh, v);
        EXPECT_NEAR(0.0f, n[0], 1e-5f);
        EXPECT_NEAR(-invSqrt2, n[1], 1e-5f);
        EXPECT_NEAR(invSqrt2, n[2], 1e-5f);

        const std::array<float, 4> t = ElementOfVertex(
            mesh, v, Microsoft::Xna::Framework::Graphics::VertexElementUsage::Tangent, 4);
        // The generated basis, closed-form from these UVs: (+X, +1). The authored (0,1,0,+1) shares
        // no component with it, so "used the authored tangent" fails on the first two.
        EXPECT_NEAR(1.0f, t[0], 1e-5f);
        EXPECT_NEAR(0.0f, t[1], 1e-5f);
        EXPECT_NEAR(0.0f, t[2], 1e-5f);
        EXPECT_FLOAT_EQ(1.0f, t[3]);
        EXPECT_NEAR(0.0f, t[0] * n[0] + t[1] * n[1] + t[2] * n[2], 1e-5f)
            << "the authored tangent is not perpendicular to the computed normal, so keeping it "
               "would show up exactly here";
    }
}

TEST(GltfImportCoreTest, TheSplitRewritesTheGeneratedTangentBasisTogetherWithTheNormals)
{
    // The split renumbers every per-vertex stream, and the tangent basis is generated AFTER it from
    // the split arrays -- so each copy's tangent belongs to its own face. The assertion is the
    // property tangent-space normal mapping depends on: T is unit length and perpendicular to that
    // copy's N. Reading the tangent through the pre-split numbering would leave the fold's copies
    // sharing one basis, and the dot product would not be zero on at least one of them.
    const MeshOut mesh = ExtractNormalLess(
        {0, 0, 0,   1, 0, 0,   1, 1, 0,   0, 1, 0,   0, 2, 1,   1, 2, 1},
        {0, 1, 2,  0, 2, 3,  3, 2, 4,  2, 5, 4});
    ASSERT_EQ(48, mesh.stride) << "a materialless primitive is glTF's default metallic-roughness";
    for (std::size_t v = 0; v < VertexCountOf(mesh); ++v)
    {
        SCOPED_TRACE("vertex " + std::to_string(v));
        const std::array<float, 3> n = NormalOfVertex(mesh, v);
        const std::array<float, 4> t = ElementOfVertex(
            mesh, v, Microsoft::Xna::Framework::Graphics::VertexElementUsage::Tangent, 4);
        EXPECT_NEAR(1.0f, std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]), 1e-4f);
        EXPECT_NEAR(0.0f, t[0] * n[0] + t[1] * n[1] + t[2] * n[2], 1e-4f);
        EXPECT_TRUE(t[3] == 1.0f || t[3] == -1.0f);
    }
}

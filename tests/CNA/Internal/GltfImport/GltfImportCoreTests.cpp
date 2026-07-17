// SPDX-License-Identifier: MS-PL
//
// Direct unit tests for CNA::Internal::GltfImport::GltfImportCore::ExtractMesh() -- calls it
// in-process (cgltf_parse_file + cgltf_load_buffers on a small self-contained fixture written to
// a scratch file, mirroring gltf_to_cnj.cpp's own parse setup) rather than going through
// ContentManager or spawning the CLI tool, since the thing under test here (MeshOut::
// pbrUv2Mismatch) has no separately-observable effect on either of those higher-level paths --
// the warning it drives (gltf_to_cnj.cpp's own ConvertGroup) is best-effort stdout diagnostics,
// not asserted on elsewhere in this codebase (see the pre-existing morph-target warning, which
// has no matching test either).

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"

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
    // color) -- the negative case, proving pbrUv2Mismatch stays false for the common, non-divergent
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
}

TEST(GltfImportCoreTest, ExtractMeshDetectsMismatchedPbrMapUvSets)
{
    const MeshOut out = ExtractPrimitive0(kMismatchedUvGltf);
    ASSERT_TRUE(out.usePbr);
    EXPECT_TRUE(out.pbrUv2Mismatch);
}

TEST(GltfImportCoreTest, ExtractMeshDoesNotFlagMatchedPbrMapUvSets)
{
    const MeshOut out = ExtractPrimitive0(kMatchedUvGltf);
    ASSERT_TRUE(out.usePbr);
    EXPECT_FALSE(out.pbrUv2Mismatch);
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

#ifdef CNA_DRACO_AVAILABLE
// Draco mesh compression decoding (CNB-91, Phase 14F). Only compiled when this build actually has
// libdraco support (see CNA_DRACO_AVAILABLE's own doc comment in cmake/CnaLibrary.cmake) --
// mirrors the production code's own #ifdef, so a Draco-less build's test suite has no test to
// skip at all rather than reporting a misleading "SKIPPED".
TEST(GltfImportCoreTest, ExtractMeshDecodesDracoCompressedTriangle)
{
    const MeshOut out = ExtractPrimitive0(kDracoTriangleGltf);

    // Unskinned, uncolored, no PBR maps -> stride 32 (Position+Normal+TextureCoordinate).
    ASSERT_EQ(out.stride, 32);
    ASSERT_FALSE(out.skinned);
    ASSERT_FALSE(out.colored);
    ASSERT_FALSE(out.usePbr);
    ASSERT_EQ(out.vertexBytes.size(), 3u * 32u);

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
    EXPECT_NEAR(readFloat(24), 0.0f, 1e-5f);
    EXPECT_NEAR(readFloat(28), 0.0f, 1e-5f);

    // Vertex 1: Position (1,0,0), UV (1,0).
    EXPECT_NEAR(readFloat(32 + 0), 1.0f, 1e-5f);
    EXPECT_NEAR(readFloat(32 + 4), 0.0f, 1e-5f);
    EXPECT_NEAR(readFloat(32 + 24), 1.0f, 1e-5f);
    EXPECT_NEAR(readFloat(32 + 28), 0.0f, 1e-5f);

    // Vertex 2: Position (0,1,0), UV (0,1).
    EXPECT_NEAR(readFloat(64 + 0), 0.0f, 1e-5f);
    EXPECT_NEAR(readFloat(64 + 4), 1.0f, 1e-5f);
    EXPECT_NEAR(readFloat(64 + 24), 0.0f, 1e-5f);
    EXPECT_NEAR(readFloat(64 + 28), 1.0f, 1e-5f);

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

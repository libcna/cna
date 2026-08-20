// SPDX-License-Identifier: MS-PL
//
// plans/plan_modern.md MOD-2076: the material extensions beyond glTF core, carried out of the importer.
//
// cgltf has parsed clearcoat, sheen, transmission, volume and iridescence for a long time; what was
// missing was anyone copying the values into MaterialOut, so an application could not see them at
// all. These tests drive real documents through the real extraction path, because the failure mode
// of "a field nobody copied" is silence -- the material imports, looks ordinary, and nothing says
// the file asked for anything else.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"

using namespace CNA::Internal::GltfImport;

namespace
{
    struct Parsed
    {
        cgltf_data* data = nullptr;
        ~Parsed() { if (data != nullptr) { cgltf_free(data); } }
        Parsed() = default;
        Parsed(const Parsed&) = delete;
        Parsed& operator=(const Parsed&) = delete;
    };

    std::string Base64(const std::vector<std::uint8_t>& bytes)
    {
        static const char* kAlphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        for (std::size_t i = 0; i < bytes.size(); i += 3)
        {
            const std::uint32_t chunk =
                (static_cast<std::uint32_t>(bytes[i]) << 16) |
                (i + 1 < bytes.size() ? static_cast<std::uint32_t>(bytes[i + 1]) << 8 : 0u) |
                (i + 2 < bytes.size() ? static_cast<std::uint32_t>(bytes[i + 2]) : 0u);
            out += kAlphabet[(chunk >> 18) & 0x3F];
            out += kAlphabet[(chunk >> 12) & 0x3F];
            out += (i + 1 < bytes.size()) ? kAlphabet[(chunk >> 6) & 0x3F] : '=';
            out += (i + 2 < bytes.size()) ? kAlphabet[chunk & 0x3F] : '=';
        }
        return out;
    }

    /// One triangle with one material, whose `extensions` object is whatever the caller passes and
    /// whose `extensionsUsed` names whatever it declares.
    std::string DocumentWith(const std::string& extensionsObject, const std::string& used)
    {
        std::vector<std::uint8_t> buffer;
        const std::vector<float> positions = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
        for (const float value : positions)
        {
            std::uint8_t bytes[4];
            std::memcpy(bytes, &value, 4);
            buffer.insert(buffer.end(), bytes, bytes + 4);
        }

        return std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "extensionsUsed": [ )GLTF") + used + R"GLTF( ],
  "meshes": [ { "primitives": [ {
      "attributes": { "POSITION": 0 }, "material": 0, "mode": 4
  } ] } ],
  "materials": [ { "pbrMetallicRoughness": { }, "extensions": )GLTF" + extensionsObject +
               R"GLTF( } ],
  "buffers": [ { "byteLength": )GLTF" + std::to_string(buffer.size()) +
               R"GLTF(, "uri": "data:application/octet-stream;base64,)GLTF" + Base64(buffer) +
               R"GLTF(" } ],
  "bufferViews": [ { "buffer": 0, "byteOffset": 0, "byteLength": 36 } ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
      "min": [0,0,0], "max": [1,1,0] }
  ]
})GLTF";
    }

    MaterialOut MaterialOf(const std::string& json)
    {
        Parsed parsed;
        cgltf_options options{};
        if (cgltf_parse(&options, json.data(), json.size(), &parsed.data) != cgltf_result_success)
        {
            ADD_FAILURE() << "the fixture document did not parse";
            return MaterialOut{};
        }
        if (cgltf_load_buffers(&options, parsed.data, nullptr) != cgltf_result_success)
        {
            ADD_FAILURE() << "the fixture buffers did not load";
            return MaterialOut{};
        }
        if (parsed.data->meshes_count == 0) { ADD_FAILURE() << "no mesh"; return MaterialOut{}; }
        return ExtractMesh(parsed.data, parsed.data->meshes[0].primitives[0], "probe", nullptr,
                           1.0f).material;
    }
}

TEST(GltfMaterialExtensionsTest, AMaterialDeclaringNoneOfThemKeepsEveryExtensionDefault)
{
    // The property that makes reading these unconditional safe: a file that says nothing produces
    // exactly the values the extensions specify as their own defaults, so nothing downstream can
    // tell "absent" from "present and neutral" -- because for these extensions they are the same.
    const MaterialOut material = MaterialOf(DocumentWith("{ }", "\"KHR_texture_transform\""));

    EXPECT_FLOAT_EQ(material.clearcoatFactorEXT, 0.0f);
    EXPECT_FLOAT_EQ(material.clearcoatRoughnessFactorEXT, 0.0f);
    EXPECT_FLOAT_EQ(material.sheenColorFactorEXT.X, 0.0f);
    EXPECT_FLOAT_EQ(material.sheenRoughnessFactorEXT, 0.0f);
    EXPECT_FLOAT_EQ(material.transmissionFactorEXT, 0.0f);
    EXPECT_FLOAT_EQ(material.thicknessFactorEXT, 0.0f);
    EXPECT_FLOAT_EQ(material.attenuationDistanceEXT, 0.0f);
    EXPECT_FLOAT_EQ(material.attenuationColorEXT.X, 1.0f);
    EXPECT_FLOAT_EQ(material.iridescenceFactorEXT, 0.0f);
    EXPECT_FLOAT_EQ(material.iridescenceIorEXT, 1.3f);
    EXPECT_FLOAT_EQ(material.iridescenceThicknessMinimumEXT, 100.0f);
    EXPECT_FLOAT_EQ(material.iridescenceThicknessMaximumEXT, 400.0f);
}

TEST(GltfMaterialExtensionsTest, ClearcoatSheenTransmissionVolumeAndIridescenceAllArrive)
{
    const MaterialOut material = MaterialOf(DocumentWith(R"({
      "KHR_materials_clearcoat": { "clearcoatFactor": 0.8, "clearcoatRoughnessFactor": 0.3 },
      "KHR_materials_sheen": { "sheenColorFactor": [0.6, 0.5, 0.4],
                               "sheenRoughnessFactor": 0.7 },
      "KHR_materials_transmission": { "transmissionFactor": 0.9 },
      "KHR_materials_volume": { "thicknessFactor": 2.5, "attenuationDistance": 4.5,
                                "attenuationColor": [0.3, 0.6, 0.9] },
      "KHR_materials_iridescence": { "iridescenceFactor": 0.65, "iridescenceIor": 1.75,
                                     "iridescenceThicknessMinimum": 80,
                                     "iridescenceThicknessMaximum": 720 }
    })",
        "\"KHR_materials_clearcoat\", \"KHR_materials_sheen\", \"KHR_materials_transmission\", "
        "\"KHR_materials_volume\", \"KHR_materials_iridescence\""));

    EXPECT_FLOAT_EQ(material.clearcoatFactorEXT, 0.8f);
    EXPECT_FLOAT_EQ(material.clearcoatRoughnessFactorEXT, 0.3f);
    EXPECT_FLOAT_EQ(material.sheenColorFactorEXT.X, 0.6f);
    EXPECT_FLOAT_EQ(material.sheenColorFactorEXT.Z, 0.4f);
    EXPECT_FLOAT_EQ(material.sheenRoughnessFactorEXT, 0.7f);
    EXPECT_FLOAT_EQ(material.transmissionFactorEXT, 0.9f);
    EXPECT_FLOAT_EQ(material.thicknessFactorEXT, 2.5f);
    EXPECT_FLOAT_EQ(material.attenuationDistanceEXT, 4.5f);
    EXPECT_FLOAT_EQ(material.attenuationColorEXT.Y, 0.6f);
    EXPECT_FLOAT_EQ(material.iridescenceFactorEXT, 0.65f);
    EXPECT_FLOAT_EQ(material.iridescenceIorEXT, 1.75f);
    EXPECT_FLOAT_EQ(material.iridescenceThicknessMinimumEXT, 80.0f);
    EXPECT_FLOAT_EQ(material.iridescenceThicknessMaximumEXT, 720.0f);
}

TEST(GltfMaterialExtensionsTest, AnInfiniteAttenuationDistanceArrivesAsTheValueMeaningInfinite)
{
    // glTF spells "the medium absorbs nothing" as +Infinity, which is not a number a shader uniform
    // can carry. It has to arrive as the zero that PbrMaterialExtensions uses for the same thing,
    // or every volume with the extension's default would absorb everything after one unit.
    const MaterialOut material = MaterialOf(DocumentWith(
        R"({ "KHR_materials_volume": { "thicknessFactor": 1.0 } })",
        "\"KHR_materials_volume\""));

    EXPECT_FLOAT_EQ(material.thicknessFactorEXT, 1.0f);
    EXPECT_FLOAT_EQ(material.attenuationDistanceEXT, 0.0f)
        << "an absent attenuation distance is infinite, and infinity has to arrive as zero here";
    EXPECT_TRUE(std::isfinite(material.attenuationDistanceEXT));
}

TEST(GltfMaterialExtensionsTest, AnUnknownExtensionIsStillIgnoredRatherThanFailingTheImport)
{
    // The other half of the row, and the one a file in the wild is most likely to exercise: an
    // extension nothing in CNA has heard of must not stop the material from importing.
    const MaterialOut material = MaterialOf(DocumentWith(R"({
      "KHR_materials_clearcoat": { "clearcoatFactor": 0.5 },
      "VENDOR_materials_something_nobody_implements": { "wibble": [1, 2, 3] }
    })",
        "\"KHR_materials_clearcoat\", \"VENDOR_materials_something_nobody_implements\""));

    EXPECT_FLOAT_EQ(material.clearcoatFactorEXT, 0.5f)
        << "an unknown neighbour stopped a known extension from being read";
}

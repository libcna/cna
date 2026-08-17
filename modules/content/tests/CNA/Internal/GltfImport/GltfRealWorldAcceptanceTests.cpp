// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-407: opt-in acceptance for the exact real-world watch that exposed D1-D3.
// The asset remains outside the repository; point CNA_GLTF_CHRONOGRAPH_WATCH at its pinned GLB.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "System/Security/Cryptography/SHA256.hpp"

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "GltfOracleEXT.hpp"

using namespace CnaTest::GltfOracle;
using namespace CNA::Internal::GltfImport;

namespace
{
    struct ParsedGltf
    {
        cgltf_data* data = nullptr;
        ~ParsedGltf() { if (data != nullptr) { cgltf_free(data); } }
    };

    struct Bounds
    {
        std::array<float, 3> min{
            std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()};
        std::array<float, 3> max{
            std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest()};
        bool any = false;
    };

    Bounds BoundsForNode(const WorldPositions& world, const std::string& nodeName)
    {
        Bounds bounds;
        for (const WorldInstance& instance : world.instances)
        {
            if (instance.nodeName != nodeName) { continue; }
            for (const std::array<float, 3>& position : instance.worldPositions)
            {
                bounds.any = true;
                for (std::size_t c = 0; c < 3; ++c)
                {
                    bounds.min[c] = std::min(bounds.min[c], position[c]);
                    bounds.max[c] = std::max(bounds.max[c], position[c]);
                }
            }
        }
        return bounds;
    }

    std::string Sha256(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) { return {}; }
        const std::vector<std::uint8_t> bytes{
            std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        System::Security::Cryptography::SHA256 sha;
        const std::vector<std::uint8_t> digest = sha.ComputeHash(bytes);
        std::ostringstream out;
        out << std::hex << std::setfill('0');
        for (const std::uint8_t byte : digest)
        {
            out << std::setw(2) << static_cast<unsigned>(byte);
        }
        return out.str();
    }

    bool UsesExtension(const cgltf_data& data, const std::string& name)
    {
        for (cgltf_size i = 0; i < data.extensions_used_count; ++i)
        {
            if (data.extensions_used[i] != nullptr && name == data.extensions_used[i]) { return true; }
        }
        return false;
    }

    void ExpectSameBounds(const Bounds& expected, const Bounds& actual, const std::string& name)
    {
        ASSERT_TRUE(expected.any) << name << " has no specification-side positions";
        ASSERT_TRUE(actual.any) << name << " has no CNA-imported positions";
        for (std::size_t c = 0; c < 3; ++c)
        {
            EXPECT_NEAR(expected.min[c], actual.min[c], 1e-4f) << name << " min[" << c << "]";
            EXPECT_NEAR(expected.max[c], actual.max[c], 1e-4f) << name << " max[" << c << "]";
        }
    }
}

TEST(GltfRealWorldAcceptanceL4, ChronographWatchMatchesItsRecordedGeometryMaterialsAndAnimation)
{
    const char* assetPath = std::getenv("CNA_GLTF_CHRONOGRAPH_WATCH");
    if (assetPath == nullptr || *assetPath == '\0')
    {
        GTEST_SKIP() << "set CNA_GLTF_CHRONOGRAPH_WATCH to the pinned ChronographWatch.glb";
    }

    ASSERT_EQ("8e875fcd83efb433afed9ef1c18b2c2b2e075e2bf48371cadfd2a3cf529f1aef",
              Sha256(assetPath)) << "this is not plan_gltf.md §4.4's exact asset";

    ParsedGltf parsed;
    cgltf_options options{};
    ASSERT_EQ(cgltf_result_success, cgltf_parse_file(&options, assetPath, &parsed.data));
    ASSERT_NE(nullptr, parsed.data);
    ASSERT_EQ(cgltf_result_success, cgltf_load_buffers(&options, parsed.data, assetPath));
    ASSERT_EQ(cgltf_result_success, cgltf_validate(parsed.data));
    const cgltf_data& data = *parsed.data;

    EXPECT_EQ(14u, data.nodes_count);
    EXPECT_EQ(13u, data.meshes_count);
    EXPECT_EQ(29u, data.materials_count);
    EXPECT_EQ(8u, data.textures_count);
    EXPECT_EQ(1u, data.animations_count);
    std::size_t primitiveCount = 0;
    std::size_t variantMappingCount = 0;
    for (cgltf_size m = 0; m < data.meshes_count; ++m)
    {
        primitiveCount += data.meshes[m].primitives_count;
        for (cgltf_size p = 0; p < data.meshes[m].primitives_count; ++p)
        {
            variantMappingCount += data.meshes[m].primitives[p].mappings_count;
        }
    }
    EXPECT_EQ(19u, primitiveCount);
    EXPECT_EQ(28u, variantMappingCount);
    EXPECT_TRUE(UsesExtension(data, "KHR_materials_transmission"));
    EXPECT_TRUE(UsesExtension(data, "KHR_materials_variants"));
    EXPECT_TRUE(UsesExtension(data, "KHR_texture_transform"));

    ASSERT_EQ(4u, data.variants_count);
    const std::array<std::string, 4> variantNames = {
        "Surgical White", "Midnight Gold", "Commerce Green", "Khronos Red"};
    for (std::size_t i = 0; i < variantNames.size(); ++i)
    {
        ASSERT_NE(nullptr, data.variants[i].name);
        EXPECT_EQ(variantNames[i], data.variants[i].name);
    }

    std::size_t transmissionMaterials = 0;
    for (cgltf_size i = 0; i < data.materials_count; ++i)
    {
        if (data.materials[i].has_transmission == 0) { continue; }
        ++transmissionMaterials;
        EXPECT_STREQ("Glass Face", data.materials[i].name);
        EXPECT_FLOAT_EQ(1.0f, data.materials[i].transmission.transmission_factor);
    }
    EXPECT_EQ(1u, transmissionMaterials);

    // Exercise CNA's semantic extraction too: recognizing extension records in cgltf is not the
    // acceptance. The default glass must reach MeshOut as CNA's documented blend approximation,
    // and all 28 authored variant mappings must survive the same production extraction path.
    std::size_t extractedVariantMappings = 0;
    std::size_t extractedGlassMaterials = 0;
    for (cgltf_size m = 0; m < data.meshes_count; ++m)
    {
        for (cgltf_size p = 0; p < data.meshes[m].primitives_count; ++p)
        {
            const cgltf_primitive& primitive = data.meshes[m].primitives[p];
            const MeshOut mesh = ExtractMesh(&data, primitive, data.meshes[m].name, nullptr, 1.0f);
            extractedVariantMappings += ExtractMaterialVariantsEXT(
                &data, primitive, data.meshes[m].name, nullptr, 1.0f).size();
            if (primitive.material == nullptr || primitive.material->name == nullptr ||
                std::string(primitive.material->name) != "Glass Face")
            {
                continue;
            }
            ++extractedGlassMaterials;
            EXPECT_FLOAT_EQ(1.0f, mesh.transmissionFactorEXT);
            EXPECT_TRUE(mesh.transmissionApproximatedEXT);
            EXPECT_FLOAT_EQ(0.0f, mesh.material.baseColorFactor.W);
        }
    }
    EXPECT_EQ(28u, extractedVariantMappings);
    EXPECT_EQ(1u, extractedGlassMaterials);

    const cgltf_animation& animation = data.animations[0];
    ASSERT_EQ(1u, animation.channels_count);
    ASSERT_EQ(1u, animation.samplers_count);
    EXPECT_STREQ("Anim_0", animation.name);
    const cgltf_animation_channel& channel = animation.channels[0];
    ASSERT_NE(nullptr, channel.target_node);
    EXPECT_STREQ("Hand Seconds", channel.target_node->name);
    EXPECT_EQ(cgltf_animation_path_type_rotation, channel.target_path);
    ASSERT_NE(nullptr, channel.sampler);
    ASSERT_NE(nullptr, channel.sampler->input);
    EXPECT_EQ(121u, channel.sampler->input->count);
    ASSERT_TRUE(channel.sampler->input->has_min);
    ASSERT_TRUE(channel.sampler->input->has_max);
    EXPECT_FLOAT_EQ(0.0f, channel.sampler->input->min[0]);
    EXPECT_FLOAT_EQ(60.0f, channel.sampler->input->max[0]);

    const SceneGraphOut scene = BuildSceneGraph(&data);
    std::vector<std::string> animationWarnings;
    AnimationReportEXT animationReport;
    const std::vector<ClipOut> clips = ExtractSceneNodeClips(
        &data, scene, 1.0f, animationWarnings, &animationReport);
    ASSERT_TRUE(animationWarnings.empty());
    ASSERT_EQ(1u, clips.size());
    EXPECT_EQ("Anim_0", clips[0].name);
    EXPECT_DOUBLE_EQ(60.0, clips[0].duration);
    EXPECT_EQ(ClipTargetSpace::SceneNode, clips[0].targetSpace);
    ASSERT_EQ(1u, clips[0].tracks.size());
    ASSERT_GE(clips[0].tracks[0].boneIndex, 0);
    ASSERT_LT(static_cast<std::size_t>(clips[0].tracks[0].boneIndex), scene.nodes.size());
    EXPECT_EQ("Hand Seconds", scene.nodes[clips[0].tracks[0].boneIndex].name);
    EXPECT_EQ(121u, clips[0].tracks[0].keys.size());
    EXPECT_EQ(0, animationReport.skippedOutOfSceneChannels);
    EXPECT_EQ(0, animationReport.skippedUnsupportedPathChannels);

    const WorldPositions expected = EvaluateWorldPositionsEXT(data);
    const WorldPositions actual = EvaluateCnaWorldPositionsEXT(data);
    ASSERT_TRUE(expected.selfCheckPassed);
    ASSERT_EQ(19u, expected.instances.size());
    ASSERT_EQ(expected.instances.size(), actual.instances.size());
    for (const std::string& node : {"Backplate Khronos", "Hand Hours", "Hand Minutes"})
    {
        ExpectSameBounds(BoundsForNode(expected, node), BoundsForNode(actual, node), node);
    }

    const Bounds backplate = BoundsForNode(actual, "Backplate Khronos");
    EXPECT_NEAR(-1.9022f, backplate.min[1], 1e-4f);
    EXPECT_NEAR(1.9834f, backplate.max[1], 1e-4f);
    EXPECT_NEAR(0.0166f, backplate.min[2], 1e-4f);
    EXPECT_NEAR(0.0810f, backplate.max[2], 1e-4f);
    const Bounds hours = BoundsForNode(actual, "Hand Hours");
    EXPECT_NEAR(-0.2314f, hours.min[1], 1e-4f);
    EXPECT_NEAR(0.3759f, hours.max[1], 1e-4f);
    EXPECT_NEAR(0.7371f, hours.min[2], 1e-4f);
    EXPECT_NEAR(0.7676f, hours.max[2], 1e-4f);
    const Bounds minutes = BoundsForNode(actual, "Hand Minutes");
    EXPECT_NEAR(-0.2629f, minutes.min[1], 1e-4f);
    EXPECT_NEAR(0.6765f, minutes.max[1], 1e-4f);
    EXPECT_NEAR(0.7740f, minutes.min[2], 1e-4f);
    EXPECT_NEAR(0.8056f, minutes.max[2], 1e-4f);
}

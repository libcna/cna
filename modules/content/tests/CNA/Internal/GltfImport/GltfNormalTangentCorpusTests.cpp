// SPDX-License-Identifier: MS-PL
//
// plans/plan_gltf.md GLTF-170/GLTF-175/GLTF-176/GLTF-399: the discriminating assertions behind the
// normals/tangents corpus group. L2/L3/L4/L5 already sweep the values and bytes generically; these
// tests prove each newly added fixture still has the shape that makes those comparisons useful.

#include <array>
#include <cmath>
#include <cstdint>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "GltfFixtureCorpus.hpp"
#include "GltfOracleEXT.hpp"

using namespace CNA::Internal::GltfImport;
using CnaTest::GltfOracle::DumpMeshOutEXT;
using CnaTest::GltfOracle::LoadedFixture;
using CnaTest::GltfOracle::MeshOutDump;

namespace
{
    constexpr float kTolerance = 1e-5f;

    std::array<float, 3> Cross(const std::array<float, 3>& a,
                               const std::array<float, 3>& b)
    {
        return {a[1] * b[2] - a[2] * b[1],
                a[2] * b[0] - a[0] * b[2],
                a[0] * b[1] - a[1] * b[0]};
    }

    float Determinant3x3(const Microsoft::Xna::Framework::Matrix& m)
    {
        return m.M11 * (m.M22 * m.M33 - m.M23 * m.M32)
             - m.M12 * (m.M21 * m.M33 - m.M23 * m.M31)
             + m.M13 * (m.M21 * m.M32 - m.M22 * m.M31);
    }

    std::vector<MeshInstanceOut> AllInstances(const cgltf_data& data)
    {
        std::vector<MeshInstanceOut> out;
        for (const MeshGroup& group : CollectMeshGroups(&data))
        {
            for (const MeshInstanceOut& instance : group.instances) { out.push_back(instance); }
        }
        return out;
    }
}

TEST(GltfNormalTangentCorpus, OppositeAuthoredHandednessReconstructsOppositeBitangents)
{
    const LoadedFixture fixture("tangent-handedness");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    ASSERT_EQ(1u, fixture.Data().meshes_count);
    ASSERT_EQ(2u, fixture.Data().meshes[0].primitives_count);

    for (std::size_t primitive = 0; primitive < 2; ++primitive)
    {
        SCOPED_TRACE("primitive " + std::to_string(primitive));
        const MeshOut mesh = ExtractMesh(&fixture.Data(),
                                         fixture.Data().meshes[0].primitives[primitive],
                                         "probe", nullptr, 1.0f);
        const MeshOutDump dump = DumpMeshOutEXT(mesh);
        ASSERT_EQ(48, dump.stride) << "the fixture must use the layout carrying TANGENT.w";
        ASSERT_EQ(3u, dump.normals.size());
        ASSERT_EQ(3u, dump.tangents.size());
        const float expectedSign = primitive == 0 ? 1.0f : -1.0f;
        for (std::size_t vertex = 0; vertex < 3; ++vertex)
        {
            EXPECT_NEAR(1.0f, dump.tangents[vertex][0], kTolerance);
            EXPECT_NEAR(0.0f, dump.tangents[vertex][1], kTolerance);
            EXPECT_NEAR(0.0f, dump.tangents[vertex][2], kTolerance);
            EXPECT_NEAR(expectedSign, dump.tangents[vertex][3], kTolerance);
            std::array<float, 3> tangent = {
                dump.tangents[vertex][0], dump.tangents[vertex][1], dump.tangents[vertex][2]};
            std::array<float, 3> bitangent = Cross(dump.normals[vertex], tangent);
            for (float& component : bitangent) { component *= dump.tangents[vertex][3]; }
            EXPECT_NEAR(0.0f, bitangent[0], kTolerance);
            EXPECT_NEAR(expectedSign, bitangent[1], kTolerance)
                << "dropping w makes the fixture's two halves reconstruct the same frame";
            EXPECT_NEAR(0.0f, bitangent[2], kTolerance);
        }
    }
}

TEST(GltfNormalTangentCorpus, MissingTangentGeneratesTheClosedFormUnitBasis)
{
    const LoadedFixture fixture("tangent-absent-generated");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    ASSERT_EQ(1u, fixture.Data().meshes_count);
    ASSERT_EQ(1u, fixture.Data().meshes[0].primitives_count);
    const cgltf_primitive& primitive = fixture.Data().meshes[0].primitives[0];
    EXPECT_EQ(nullptr, cgltf_find_accessor(&primitive, cgltf_attribute_type_tangent, 0))
        << "the fixture stopped exercising generation and now authors its answer";
    EXPECT_NE(nullptr, cgltf_find_accessor(&primitive, cgltf_attribute_type_texcoord, 0))
        << "without UVs the fallback would be the degenerate +X default, not a solved basis";

    const MeshOut mesh = ExtractMesh(&fixture.Data(), primitive, "probe", nullptr, 1.0f);
    const MeshOutDump dump = DumpMeshOutEXT(mesh);
    ASSERT_EQ(48, dump.stride);
    ASSERT_EQ(3u, dump.normals.size());
    ASSERT_EQ(3u, dump.tangents.size());
    for (std::size_t vertex = 0; vertex < dump.tangents.size(); ++vertex)
    {
        const auto& tangent = dump.tangents[vertex];
        EXPECT_NEAR(1.0f, tangent[0], kTolerance);
        EXPECT_NEAR(0.0f, tangent[1], kTolerance);
        EXPECT_NEAR(0.0f, tangent[2], kTolerance);
        EXPECT_NEAR(1.0f, tangent[3], kTolerance);
        const float length = std::sqrt(tangent[0] * tangent[0] + tangent[1] * tangent[1] +
                                       tangent[2] * tangent[2]);
        EXPECT_NEAR(1.0f, length, kTolerance) << "Gram-Schmidt output was not renormalised";
        const float dot = dump.normals[vertex][0] * tangent[0] +
                          dump.normals[vertex][1] * tangent[1] +
                          dump.normals[vertex][2] * tangent[2];
        EXPECT_NEAR(0.0f, dot, kTolerance);
    }
}

TEST(GltfNormalTangentCorpus, MirroringKeepsTheBufferSharedAndMakesHandednessAPerDrawSign)
{
    const LoadedFixture fixture("tangent-mirrored");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const std::vector<MeshInstanceOut> instances = AllInstances(fixture.Data());
    ASSERT_EQ(2u, instances.size());
    EXPECT_EQ(instances[0].mesh, instances[1].mesh)
        << "the per-draw requirement disappears if the fixture no longer shares one mesh";

    const MeshInstanceOut* ordinary = nullptr;
    const MeshInstanceOut* mirrored = nullptr;
    for (const MeshInstanceOut& instance : instances)
    {
        const std::string name = instance.node != nullptr && instance.node->name != nullptr
            ? instance.node->name : "";
        if (name == "Ordinary") { ordinary = &instance; }
        if (name == "Mirrored") { mirrored = &instance; }
    }
    ASSERT_NE(nullptr, ordinary);
    ASSERT_NE(nullptr, mirrored);
    EXPECT_FALSE(ordinary->mirroredEXT);
    EXPECT_TRUE(mirrored->mirroredEXT);
    EXPECT_GT(Determinant3x3(ordinary->worldTransform), 0.0f);
    EXPECT_LT(Determinant3x3(mirrored->worldTransform), 0.0f);

    // The one shared buffer necessarily carries the authored local-space +1 on both placements.
    const MeshOut mesh = ExtractMesh(&fixture.Data(), fixture.Data().meshes[0].primitives[0],
                                     "probe", nullptr, 1.0f);
    const MeshOutDump dump = DumpMeshOutEXT(mesh);
    ASSERT_EQ(3u, dump.tangents.size());
    for (const auto& tangent : dump.tangents) { EXPECT_FLOAT_EQ(1.0f, tangent[3]); }

    // Under S(-1,1,1), T becomes -X and cross(+Z,-X) is -Y. Multiplying the authored w by
    // sign(det(world)) restores +Y, exactly matching the transformed object-space bitangent.
    const float worldSign = dump.tangents[0][3] *
        (Determinant3x3(mirrored->worldTransform) < 0.0f ? -1.0f : 1.0f);
    EXPECT_FLOAT_EQ(-1.0f, worldSign);
    const std::array<float, 3> worldNormal = {0.0f, 0.0f, 1.0f};
    const std::array<float, 3> worldTangent = {-1.0f, 0.0f, 0.0f};
    std::array<float, 3> worldBitangent = Cross(worldNormal, worldTangent);
    for (float& component : worldBitangent) { component *= worldSign; }
    EXPECT_EQ((std::array<float, 3>{0.0f, 1.0f, 0.0f}), worldBitangent);
}

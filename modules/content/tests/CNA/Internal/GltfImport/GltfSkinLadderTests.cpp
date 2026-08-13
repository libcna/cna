// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-399 / §15.4: the skinning ladder, evaluated rather than described.
//
// §15.4 lists a ladder of skin fixtures with an exactly computable answer for each. Six of them
// owned a defect and got a hand-written test when that defect was closed; the rest stated their
// expected world position in the plan and in the manifest, and nothing read it. A ladder nobody
// climbs is a table of claims.
//
// This file climbs it. For every corpus fixture whose manifest carries an `l4.skin` block, it runs
// the specification's own skin equation (§3.7.3.3)
//
//     skinnedPosition = sum_i  weight_i * jointMatrix[joint_i] * position
//
// using CNA's real bone palette -- the matrices `AnimationPlayer` hands a renderer -- and compares
// the result against the manifest. Two properties make that comparison meaningful rather than
// circular:
//
//   * The palette is indexed in CNA's OWN bone order, not the file's joint order, and the two
//     differ by `SkeletonResult::oldToNew` (§15.1.2 -- the bone order is the scene graph's and is
//     deliberately not a skin palette's). The mapping is read from the importer rather than
//     assumed, so a reordering regression surfaces as a wrong position rather than being absorbed.
//   * The expected positions are computed by the generator from the authored transforms, in
//     Python, without reference to any C++ code. Nothing here can agree with itself by accident.
//
// The sweep is total over the corpus and asserts a floor, so a fixture that stops declaring a skin
// expectation cannot quietly leave the ladder.

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "GltfFixtureCorpus.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/AnimationPlayer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using CnaTest::GltfOracle::CorpusDirectory;
using CnaTest::GltfOracle::CorpusFixtureIds;
using CnaTest::GltfOracle::IsRejectionFixture;
using CnaTest::GltfOracle::LoadedFixture;
using CnaTest::GltfOracle::Member;
using CnaTest::GltfOracle::Numbers;
using CnaTest::GltfOracle::Path;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::AnimationPlayer;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Model;
using Microsoft::Xna::Framework::Graphics::SkinningData;

namespace
{
    /// Generous next to the values under test -- the smallest difference any of these fixtures is
    /// shaped to produce is 0.1 of a unit, and the largest is 100. This is a float-error budget,
    /// not a fudge factor.
    constexpr float kTolerance = 1e-4f;

    /// A fixture's authored joint bindings, read from its L3 expectation: the file's own
    /// JOINTS_0/WEIGHTS_0 in the file's own joint index space.
    struct Bindings
    {
        std::vector<std::array<int, 4>> joints;
        std::vector<std::array<float, 4>> weights;
        std::vector<Vector3> positions;
    };

    bool ReadBindings(const LoadedFixture& fixture, Bindings& out, std::string& why)
    {
        const CNA::Internal::JsonValue& primitives = Path(fixture.Expected(), "l3.primitives");
        if (primitives.type != CNA::Internal::JsonType::Array || primitives.arrayValue.empty())
        {
            why = "the fixture declares an l4.skin block but no L3 primitive to apply it to";
            return false;
        }
        const CNA::Internal::JsonValue& primitive = primitives.arrayValue.front();
        const CNA::Internal::JsonValue& joints = Member(primitive, "joints");
        const CNA::Internal::JsonValue& weights = Member(primitive, "weights");
        const CNA::Internal::JsonValue& positions = Member(primitive, "positions");
        if (joints.type != CNA::Internal::JsonType::Array ||
            weights.type != CNA::Internal::JsonType::Array ||
            positions.type != CNA::Internal::JsonType::Array)
        {
            why = "the L3 primitive is missing joints, weights or positions";
            return false;
        }
        if (joints.arrayValue.size() != positions.arrayValue.size() ||
            weights.arrayValue.size() != positions.arrayValue.size())
        {
            why = "the L3 primitive's joints, weights and positions disagree in length";
            return false;
        }

        for (std::size_t v = 0; v < positions.arrayValue.size(); ++v)
        {
            const std::vector<double> p = Numbers(positions.arrayValue[v]);
            const std::vector<double> j = Numbers(joints.arrayValue[v]);
            const std::vector<double> w = Numbers(weights.arrayValue[v]);
            if (p.size() != 3 || j.size() != 4 || w.size() != 4)
            {
                why = "an L3 vertex is not VEC3 position with VEC4 joints and weights";
                return false;
            }
            out.positions.emplace_back(static_cast<float>(p[0]), static_cast<float>(p[1]),
                                       static_cast<float>(p[2]));
            out.joints.push_back({static_cast<int>(j[0]), static_cast<int>(j[1]),
                                  static_cast<int>(j[2]), static_cast<int>(j[3])});
            out.weights.push_back({static_cast<float>(w[0]), static_cast<float>(w[1]),
                                   static_cast<float>(w[2]), static_cast<float>(w[3])});
        }
        return true;
    }

    /// The file's joint index space mapped into CNA's bone index space, straight from the importer.
    /// §15.1.2 keeps the two deliberately distinct, so this must be read and never assumed.
    bool ReadJointToBone(const LoadedFixture& fixture, std::vector<int>& out, std::string& why)
    {
        using namespace CNA::Internal::GltfImport;
        for (const MeshGroup& group : CollectMeshGroups(&fixture.Data()))
        {
            if (group.skin == nullptr) { continue; }
            out = BuildSkeleton(group.skin, 1.0f).oldToNew;
            return true;
        }
        why = "the fixture declares an l4.skin block but the importer found no skinned mesh group";
        return false;
    }
}

TEST(GltfSkinLadder, EverySkinFixtureSkinsItsVerticesWhereTheSpecificationSaysItDoes)
{
    GraphicsDevice gd;
    int covered = 0;

    for (const std::string& id : CorpusFixtureIds())
    {
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        if (IsRejectionFixture(fixture.Expected())) { continue; }

        const CNA::Internal::JsonValue& skin = Path(fixture.Expected(), "l4.skin");
        const CNA::Internal::JsonValue& expectedPositions = Member(skin, "skinnedPositions");
        if (expectedPositions.type != CNA::Internal::JsonType::Array) { continue; }
        ++covered;
        SCOPED_TRACE(id);

        Bindings bindings;
        std::string why;
        ASSERT_TRUE(ReadBindings(fixture, bindings, why)) << why;
        std::vector<int> jointToBone;
        ASSERT_TRUE(ReadJointToBone(fixture, jointToBone, why)) << why;
        ASSERT_EQ(bindings.positions.size(), expectedPositions.arrayValue.size())
            << "the manifest states a different number of skinned positions than the mesh has "
               "vertices";

        ContentManager cm(nullptr, CorpusDirectory().string());
        cm.setGraphicsDevice(gd);
        Model model = cm.Load<Model>(id);
        auto* skinning = dynamic_cast<SkinningData*>(model.getTagProperty());
        ASSERT_NE(nullptr, skinning) << "a skinned fixture imported without any SkinningData";

        AnimationPlayer player(*skinning);
        const std::vector<Matrix>& palette = player.GetSkinTransforms();
        EXPECT_EQ(static_cast<std::size_t>(
                      CnaTest::GltfOracle::NumberOr(skin, "jointCount", -1.0)),
                  palette.size())
            << "the bone palette does not have one entry per authored joint";

        for (std::size_t v = 0; v < bindings.positions.size(); ++v)
        {
            // §3.7.3.3's equation, verbatim: the weighted sum of the joint matrices, each applied
            // to the same mesh-local position. Weight zero contributes nothing and is skipped
            // rather than multiplied, so a joint index parked in an unused slot -- which glTF
            // permits and exporters emit -- cannot fault the lookup.
            Vector3 skinned(0.0f, 0.0f, 0.0f);
            for (int i = 0; i < 4; ++i)
            {
                const float w = bindings.weights[v][i];
                if (w == 0.0f) { continue; }
                const int joint = bindings.joints[v][i];
                ASSERT_GE(joint, 0);
                ASSERT_LT(static_cast<std::size_t>(joint), jointToBone.size())
                    << "vertex " << v << " names joint " << joint << ", which the skin has not";
                const int bone = jointToBone[static_cast<std::size_t>(joint)];
                ASSERT_GE(bone, 0);
                ASSERT_LT(static_cast<std::size_t>(bone), palette.size());
                const Vector3 contribution =
                    Vector3::Transform(bindings.positions[v], palette[static_cast<std::size_t>(bone)]);
                skinned = skinned + w * contribution;
            }

            const std::vector<double> expected = Numbers(expectedPositions.arrayValue[v]);
            ASSERT_EQ(3u, expected.size());
            EXPECT_NEAR(expected[0], skinned.X, kTolerance) << "vertex " << v << ".x";
            EXPECT_NEAR(expected[1], skinned.Y, kTolerance) << "vertex " << v << ".y";
            EXPECT_NEAR(expected[2], skinned.Z, kTolerance) << "vertex " << v << ".z";
        }
    }

    // The floor. §15.4's ladder is what this number tracks; a fixture dropping its `l4.skin` block
    // would otherwise remove itself from the sweep and still report success.
    EXPECT_GE(covered, 8) << "fewer skin fixtures carry an evaluable expectation than §15.4's "
                             "ladder has rungs -- the sweep shrank rather than the corpus";
}

TEST(GltfSkinLadder, TheLadderCoversTheDistinctPropertiesSection154Enumerates)
{
    // A count alone would pass with eight copies of the same fixture. What §15.4 is a ladder OF is
    // the properties: blending, four influences, an absent bind matrix, a non-uniform joint scale,
    // a joint chain, and an index component type that is not a byte. Each is named by a feature
    // string in the manifest, so this asserts the ladder's *shape* rather than its length.
    const std::vector<std::string> required = {
        "two-joint weight blending",
        "four-influence weight blending",
        "skin without inverseBindMatrices",
        "non-uniform joint scale",
        "joint parented to joint",
        "UNSIGNED_SHORT JOINTS_0",
    };

    std::vector<std::string> missing = required;
    for (const std::string& id : CorpusFixtureIds())
    {
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        for (const std::string& feature :
             CnaTest::GltfOracle::Strings(Path(fixture.Expected(), "inventory.features")))
        {
            missing.erase(std::remove(missing.begin(), missing.end(), feature), missing.end());
        }
    }

    for (const std::string& absent : missing)
    {
        ADD_FAILURE() << "no corpus fixture declares the property \"" << absent
                      << "\" -- §15.4's ladder has a rung with nothing standing on it";
    }
}

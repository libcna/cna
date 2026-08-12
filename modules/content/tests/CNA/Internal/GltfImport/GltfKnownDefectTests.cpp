// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-004: the eight proven defects D1-D8, as executable tests.
//
// EVERY TEST IN THIS FILE IS GREEN WHILE CNA IS BROKEN. That inversion is deliberate and is the
// mechanism that makes a known defect visible and reproducible without turning the ordinary suite
// red for months. Each test asserts two things:
//
//   1. the spec-derived expectation is still NOT met -- the defect is still present; and
//   2. the divergence is EXACTLY the one the forensic audit recorded -- CNA is broken in the
//      documented way, not in some new way.
//
// Assertion 2 is what gives these tests their real value. When the owning remediation task lands,
// it fails here with the fixture and the task id named, so the implementer cannot mistake a
// partial fix for a complete one. Converting a case to passing is a manifest change in
// tools/gltf_fixtures (set the defect's status to "fixed" and drop its divergentFields) plus
// deleting the test below -- never a change to the fixture or to its expected values. The record
// itself is never deleted: a remediated defect stays in the corpus as the regression witness, and
// the ledger test at the end of this file asserts both directions of that bookkeeping.
//
// Current state: D1, D2, D3 and D4 are FIXED (GLTF-113/114/115 and GLTF-063 respectively). D5 is
// PARTIALLY REMEDIATED -- GLTF-071 landed, GLTF-072 still owns the topology conversion itself --
// so its tests assert the new, explicitly rejected behaviour rather than the old silent
// reinterpretation, and the audit's original measurement stays on record under priorActual.
// D8 is FIXED too (GLTF-245/247/248/260), in a separate batch from D1-D3 and deliberately so:
// the node-transform work only parked a skinned mesh on the identity root, and the joint
// ancestry and mesh-space cancellation were resolved afterwards on their own fixtures.
// D6 and D7 are untouched.

#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Matrix.hpp"

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"

#include "GltfFixtureCorpus.hpp"
#include "GltfOracleEXT.hpp"

using namespace CnaTest::GltfOracle;
using CNA::Internal::JsonType;
using CNA::Internal::JsonValue;
using Microsoft::Xna::Framework::Matrix;

namespace
{
    constexpr double kTolerance = 1e-5;

    /// The `defects[]` record with the given id, so every assertion below is checked against the
    /// generated manifest rather than against numbers re-typed into this file.
    const JsonValue& DefectRecord(const LoadedFixture& fixture, const std::string& defectId)
    {
        const JsonValue& defects = Member(fixture.Expected(), "defects");
        if (defects.type == JsonType::Array)
        {
            for (const JsonValue& defect : defects.arrayValue)
            {
                if (StringOr(defect, "id", "") == defectId) { return defect; }
            }
        }
        return JsonNull();
    }

    /// Fails when the defect has been marked fixed but its known-defect test still exists, which
    /// is the one way this file could start lying about the state of the code. A
    /// partially-remediated defect is still open: one of its owning tasks landed, the behaviour
    /// changed, and the test below asserts the new behaviour rather than the original one.
    void RequireStillOpen(const JsonValue& defect, const std::string& defectId)
    {
        ASSERT_EQ(JsonType::Object, defect.type) << defectId << " has no record in the manifest";
        ASSERT_TRUE(IsOpenDefect(defect))
            << defectId << " is no longer open in tools/gltf_fixtures (status '"
            << StringOr(defect, "status", "") << "'), but this known-defect test still exists. "
            << "Delete the test and move the fixture's layer into the GltfConformance suite.";
    }

    const JsonValue& CurrentActual(const JsonValue& defect)
    {
        return Member(defect, "currentActual");
    }

    void ExpectVector(const std::vector<double>& expected, const std::array<float, 3>& actual,
                      const std::string& what)
    {
        ASSERT_EQ(3u, expected.size()) << what << ": manifest value is not a 3-vector";
        for (std::size_t i = 0; i < 3; ++i)
        {
            EXPECT_NEAR(expected[i], static_cast<double>(actual[i]), kTolerance)
                << what << "[" << i << "]";
        }
    }

    bool IsIdentity(const GltfMatrix& m)
    {
        const GltfMatrix identity = IdentityMatrix();
        for (std::size_t i = 0; i < 16; ++i)
        {
            if (std::fabs(m[i] - identity[i]) > kTolerance) { return false; }
        }
        return true;
    }

    /// Shared body for D1/D2/D3 while they were open. Retained only as history in the file
    /// header; the live assertions now live in GltfConformanceL4. Deliberately unused.
    [[maybe_unused]] void ExpectNodeTransformDiscarded_Historical(const std::string& fixtureId,
                                                                   const std::string& defectId)
    {
        const LoadedFixture fixture(fixtureId);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        const JsonValue& defect = DefectRecord(fixture, defectId);
        ASSERT_NO_FATAL_FAILURE(RequireStillOpen(defect, defectId));
        const JsonValue& actualRecord = CurrentActual(defect);

        const WorldPositions expectedWorld = EvaluateWorldPositionsEXT(fixture.Data());
        ASSERT_TRUE(expectedWorld.selfCheckPassed);
        const WorldPositions cnaWorld = EvaluateCnaWorldPositionsEXT(fixture.Data());

        // (1) The defect is still present: CNA's world geometry does not match the specification's.
        ASSERT_TRUE(expectedWorld.hasBounds);
        ASSERT_TRUE(cnaWorld.hasBounds);
        bool boundsDiffer = false;
        for (std::size_t c = 0; c < 3; ++c)
        {
            boundsDiffer = boundsDiffer ||
                std::fabs(expectedWorld.min[c] - cnaWorld.min[c]) > kTolerance ||
                std::fabs(expectedWorld.max[c] - cnaWorld.max[c]) > kTolerance;
        }
        EXPECT_TRUE(boundsDiffer)
            << defectId << " no longer reproduces on " << fixtureId << ": CNA's world bounds now "
            << "match the specification. If the fix landed, mark the defect fixed in "
            << "tools/gltf_fixtures and delete this test.";

        // (2) It is broken in exactly the documented way: every instance comes back with an
        // identity world transform, so the geometry stays in mesh-local space.
        EXPECT_EQ(static_cast<std::size_t>(NumberOr(actualRecord, "instanceCount", -1)),
                  cnaWorld.instances.size());
        const bool expectAllIdentity =
            BoolOr(actualRecord, "instanceWorldMatricesAreAllIdentity", false);
        for (const WorldInstance& instance : cnaWorld.instances)
        {
            EXPECT_EQ(expectAllIdentity, IsIdentity(instance.worldMatrix));
            EXPECT_EQ(-1, instance.node) << "MeshGroup gained a node -- Phase 5 may have landed";
        }
        ExpectVector(Numbers(Path(actualRecord, "worldBounds.min")), cnaWorld.min,
                     "currentActual.worldBounds.min");
        ExpectVector(Numbers(Path(actualRecord, "worldBounds.max")), cnaWorld.max,
                     "currentActual.worldBounds.max");
    }

}

// --- D1/D2/D3: the node transform pipeline ----------------------------------------------------
//
// REMEDIATED by GLTF-103 -> GLTF-113 -> GLTF-114 -> GLTF-115. There are deliberately no
// known-defect tests here any more, for the same reason D4 has none: with all three records marked
// fixed and their divergentFields empty, GltfConformanceL4 asserts xf-shared-mesh, xf-parent-child
// and xf-matrix-node's world geometry in full, so any of D1-D3 reappearing fails an ordinary green
// test rather than needing an inverted one. The records stay in the corpus as regression witnesses,
// with the audit's original measurements preserved under priorActual, and the ledger test at the
// end of this file asserts that bookkeeping in both directions.

// --- D4: the index path --------------------------------------------------------------------------
//
// REMEDIATED by GLTF-063. There is deliberately no known-defect test here any more: with the
// defect record marked fixed and its divergentFields empty, GltfConformanceL3 asserts
// sparse-indices' index list in full, so D4 reappearing fails an ordinary green test. The
// corresponding regression assertions live in GltfIndexDecodeTests.cpp, and the record itself
// stays in the corpus as the witness (see the ledger test at the end of this file).

// --- D5: primitive topology ----------------------------------------------------------------------

// --- D5: primitive topology ----------------------------------------------------------------------
//
// REMEDIATED by GLTF-071 -> GLTF-072 -> GLTF-073/GLTF-076/GLTF-078. There are deliberately no
// known-defect tests here any more. All seven glTF modes import: the three triangle modes as a
// triangle list (converted where needed, winding preserved), a LINE_LOOP as a LINE_STRIP carrying
// the closing segment glTF leaves implicit in the mode, and the rest as themselves with a real
// PrimitiveType on the part and a §12.3 primitive count. GltfConformanceL3/L5 and
// GltfPrimitiveTopology assert all seven in full, so any of it regressing fails an ordinary green
// test. The record stays in the corpus as the regression witness with the audit's original
// measurement under priorActual.

// --- D6: rigid node animation ---------------------------------------------------------------------

TEST(GltfKnownDefect, D6_RigidNodeAnimationIsImportedButNotYetSerialised)
{
    // Owned by GLTF-103 -> GLTF-113 -> GLTF-114 -> GLTF-293, with GLTF-294 still outstanding.
    //
    // The claim has moved on with the code. It is no longer "the animation disappears"; both
    // mechanisms that made it disappear are gone. What is asserted now is the remaining, explicit
    // limitation: the clip is extracted, correct and reported, and is not written to the .cnj
    // because the clip schema cannot yet say which index space a track targets (§15.1.2).
    using namespace CNA::Internal::GltfImport;

    const LoadedFixture fixture("anim-rigid-node");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const JsonValue& defect = DefectRecord(fixture, "D6");
    ASSERT_NO_FATAL_FAILURE(RequireStillOpen(defect, "D6"));
    const JsonValue& actualRecord = CurrentActual(defect);

    // The file really does carry the animation, and really has no skin.
    ASSERT_EQ(1u, static_cast<std::size_t>(fixture.Data().animations_count));
    ASSERT_EQ(1u, static_cast<std::size_t>(fixture.Data().animations[0].channels_count));
    ASSERT_EQ(0u, static_cast<std::size_t>(fixture.Data().skins_count));

    const std::vector<MeshGroup> groups = CollectMeshGroups(&fixture.Data());
    ASSERT_EQ(1u, groups.size());
    EXPECT_EQ(nullptr, groups[0].skin);
    // Mechanism one is gone: extraction no longer depends on the group having a skin.
    EXPECT_FALSE(BoolOr(actualRecord, "clipExtractionGatedOnSkin", true));

    // Mechanism two is gone: a non-joint target resolves against the scene graph instead of being
    // skipped, and the resulting track drives that node's own bone.
    const SceneGraphOut scene = BuildSceneGraph(&fixture.Data());
    std::vector<std::string> warnings;
    const std::vector<ClipOut> clips =
        ExtractSceneNodeClips(&fixture.Data(), scene, 1.0f, warnings);
    EXPECT_EQ(static_cast<std::size_t>(NumberOr(actualRecord, "importedClipCount", -1)),
              clips.size());
    std::size_t trackCount = 0;
    for (const ClipOut& clip : clips)
    {
        trackCount += clip.tracks.size();
        EXPECT_EQ(ClipTargetSpace::SceneNode, clip.targetSpace);
    }
    EXPECT_EQ(static_cast<std::size_t>(NumberOr(actualRecord, "importedTrackCount", -1)),
              trackCount);
    EXPECT_GT(trackCount, 0u) << "the rigid channel produced no track -- D6 has come back";

    // The old joint-palette path still skips it, and that is correct rather than a bug: a file
    // with no skin has no palette, so there is nothing there for the channel to target.
    SkeletonResult emptySkeleton;
    std::vector<std::string> paletteWarnings;
    const std::vector<ClipOut> paletteClips =
        ExtractClips(&fixture.Data(), emptySkeleton, 1.0f, paletteWarnings);
    std::size_t paletteTracks = 0;
    for (const ClipOut& clip : paletteClips) { paletteTracks += clip.tracks.size(); }
    EXPECT_EQ(0u, paletteTracks);

    // What remains, stated as the thing it is: the clip is reported, not dropped. The converter
    // emits a warning naming it; serialisation waits for GLTF-294.
    EXPECT_FALSE(BoolOr(actualRecord, "serialisedToCnj", true));
    const std::vector<std::string> remaining = Strings(Member(defect, "remainingTasks"));
    EXPECT_NE(remaining.end(), std::find(remaining.begin(), remaining.end(), "GLTF-294"))
        << "D6 no longer names GLTF-294 as outstanding; if the .cnj carries rigid clips now, mark "
           "the defect fixed in tools/gltf_fixtures and delete this test";
}

// --- D7: factor-only PBR material ------------------------------------------------------------------

// --- D7: material state --------------------------------------------------------------------------
//
// REMEDIATED by GLTF-215 -> GLTF-216/217/219/221 -> GLTF-228/229/231. There is deliberately no
// known-defect test here any more: with the record marked fixed and its divergentFields empty,
// GltfConformanceL3 asserts mat-factor-only-gold's material in full, so any of it regressing fails
// an ordinary green test. GltfMaterialState carries the end-to-end assertions.


// --- D8: the skin ancestor chain ---------------------------------------------------------------------

// --- D8: the skin coordinate spaces -----------------------------------------------------------
//
// REMEDIATED by GLTF-245 -> GLTF-247 -> GLTF-248 -> GLTF-260. No known-defect test here any more,
// for the same reason D1-D4 have none: GltfSkinSpaces asserts the joint matrix and the resulting
// skinned position through the real loader on skin-armature-ancestor (the full scene ancestry above
// the joint set) and on skin-mesh-node-transform (the mesh-space cancellation, applied exactly
// once), so D8 reappearing fails an ordinary green test. The audit's original measurement --
// joint matrix translate(0,-100,0) -- is preserved under the record's priorActual.

// --- Ledger completeness -------------------------------------------------------------------------

TEST(GltfKnownDefect, EveryOpenDefectInTheCorpusLedgerHasAnExecutableTestHere)
{
    // Adding a defect fixture to the generator without an executable test would leave the defect
    // documented but unproven, which is exactly the failure mode this batch exists to remove. The
    // converse matters just as much: a defect the corpus records as remediated must NOT still have
    // a "still broken" test here, or the file would start lying about the state of the code.
    const std::set<std::string> open = {"D6"};
    // Remediated defects, and the task that closed each. Their records stay in the corpus as
    // regression witnesses and their fixtures are asserted by the ordinary conformance suites.
    const std::map<std::string, std::string> remediated = {
        {"D1", "GLTF-114"}, {"D2", "GLTF-114"}, {"D3", "GLTF-114"}, {"D4", "GLTF-063"},
        {"D5", "GLTF-073"}, {"D7", "GLTF-228"}, {"D8", "GLTF-247"}};

    const JsonValue& ledger = Member(CorpusManifest(), "defectLedger");
    ASSERT_EQ(JsonType::Array, ledger.type);
    std::set<std::string> recorded;
    for (const JsonValue& defect : ledger.arrayValue)
    {
        const std::string id = StringOr(defect, "id", "");
        ASSERT_FALSE(id.empty());
        recorded.insert(id);
        EXPECT_FALSE(Strings(Member(defect, "owningTasks")).empty())
            << id << " names no remediation task";
        EXPECT_FALSE(Member(defect, "fixtures").arrayValue.empty())
            << id << " is not reproduced by any fixture";

        const std::string status = StringOr(defect, "status", "");
        const auto closed = remediated.find(id);
        if (closed != remediated.end())
        {
            EXPECT_EQ("fixed", status)
                << id << " is listed here as remediated but the corpus still calls it " << status;
            const std::vector<std::string> closedTasks = Strings(Member(defect, "closedTasks"));
            EXPECT_NE(closedTasks.end(),
                      std::find(closedTasks.begin(), closedTasks.end(), closed->second))
                << id << " does not name " << closed->second << " as the task that closed it";
            continue;
        }
        EXPECT_TRUE(open.count(id) != 0)
            << id << " is recorded in the corpus but has no known-defect test in this file";
        EXPECT_NE("fixed", status)
            << id << " is marked fixed in the corpus but this file still asserts it is broken. "
                     "Delete its known-defect test and move it into the remediated list here.";
    }
    for (const std::string& id : open)
    {
        EXPECT_TRUE(recorded.count(id) != 0)
            << id << " has a test here but is no longer recorded in the corpus ledger";
    }
    for (const auto& [id, task] : remediated)
    {
        EXPECT_TRUE(recorded.count(id) != 0)
            << id << " was remediated by " << task << " but its record was deleted from the "
            << "corpus ledger -- a remediated defect stays as the regression witness";
    }
}

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

    /// Shared body for the two D5 fixtures, as they stand after GLTF-071 and before GLTF-072.
    ///
    /// The claim being asserted has moved on with the code: it is no longer "the topology is
    /// silently reinterpreted" but "the topology is read, classified and explicitly rejected, and
    /// nothing reaches the triangle-list path". The fixture and its spec-derived expectation are
    /// untouched -- what a conforming importer must eventually produce has not changed.
    void ExpectTopologyClassifiedAndRejected(const std::string& fixtureId)
    {
        using CNA::Internal::GltfImport::ClassifyPrimitiveTopology;
        using CNA::Internal::GltfImport::IsPrimitiveTopologySupported;
        using CNA::Internal::GltfImport::PrimitiveTopologyMode;
        using CNA::Internal::GltfImport::PrimitiveTopologyName;

        const LoadedFixture fixture(fixtureId);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        const JsonValue& defect = DefectRecord(fixture, "D5");
        ASSERT_NO_FATAL_FAILURE(RequireStillOpen(defect, "D5"));
        const JsonValue& actualRecord = CurrentActual(defect);

        // (0) The file really does declare a non-TRIANGLES mode, and GLTF-072 is still what owns
        // finishing it. If that task closes, this test is what must be revisited.
        const JsonValue& expectedPrimitive = Path(fixture.Expected(), "l3.primitives").arrayValue.at(0);
        const int expectedMode = static_cast<int>(NumberOr(expectedPrimitive, "mode", -1));
        EXPECT_NE(4, expectedMode) << "this fixture is supposed to declare a non-TRIANGLES mode";
        const std::vector<std::string> remaining = Strings(Member(defect, "remainingTasks"));
        EXPECT_NE(remaining.end(), std::find(remaining.begin(), remaining.end(), "GLTF-073"))
            << "D5 no longer names GLTF-073 as outstanding; once ModelMeshPart carries a real "
               "PrimitiveType these topologies have a draw path, and this test and the fixture's "
               "defect record both need to move on";

        // (1) prim.type is genuinely read: the classifier returns the file's own mode, by number
        // and by specification name, and reports it as one CNA cannot yet honour.
        ASSERT_EQ(1u, static_cast<std::size_t>(fixture.Data().meshes_count));
        ASSERT_EQ(1u, static_cast<std::size_t>(fixture.Data().meshes[0].primitives_count));
        const auto topology =
            ClassifyPrimitiveTopology(fixture.Data().meshes[0].primitives[0], fixtureId);
        EXPECT_EQ(expectedMode, PrimitiveTopologyMode(topology));
        EXPECT_EQ(static_cast<int>(NumberOr(actualRecord, "classifiedMode", -1)),
                  PrimitiveTopologyMode(topology));
        EXPECT_EQ(StringOr(actualRecord, "classifiedModeName", ""),
                  std::string(PrimitiveTopologyName(topology)));
        EXPECT_FALSE(IsPrimitiveTopologySupported(topology));

        // (2) The import is rejected, and the mode reaches the diagnostic by name and by number --
        // a caller reading only the error message can still tell exactly what the file declared.
        const std::vector<ExtractedPrimitive> extracted = ExtractSceneMeshesEXT(fixture.Data());
        ASSERT_EQ(1u, extracted.size());
        EXPECT_TRUE(BoolOr(actualRecord, "importRejected", false));
        ASSERT_FALSE(extracted[0].extracted)
            << "a topology with no draw path imported successfully -- if GLTF-073/GLTF-077 landed, "
               "update D5's record in tools/gltf_fixtures and delete this case";
        for (const std::string& fragment : Strings(Member(actualRecord, "errorContains")))
        {
            EXPECT_NE(std::string::npos, extracted[0].error.find(fragment))
                << "the rejection does not name '" << fragment << "': " << extracted[0].error;
        }

        // (3) Nothing reaches the numIndices/3 triangle-list path. This is the assertion that
        // distinguishes "explicitly rejected" from "silently corrupted": there is no index list to
        // divide by three at all, where before there was one that produced a single wrong triangle.
        const MeshOutDump& dump = extracted[0].dump;
        EXPECT_TRUE(dump.indices.empty()) << "an index list survived a rejected import";
        EXPECT_EQ(0u, dump.indices.size() / 3);
        EXPECT_EQ(Member(actualRecord, "triangles").arrayValue.size(), dump.indices.size() / 3);
        // ...and what the audit measured before GLTF-071 is still on record, unchanged.
        const JsonValue& prior = Member(defect, "priorActual");
        if (prior.type == JsonType::Object)
        {
            EXPECT_EQ(1u, Member(prior, "triangles").arrayValue.size())
                << "the pre-GLTF-071 measurement (one silently reinterpreted triangle) was lost";
            EXPECT_FALSE(BoolOr(prior, "topologyCarried", true));
        }
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

// GLTF-071 closed the reading half and GLTF-072 the conversion half, so the triangle topologies
// are no longer here: mode-triangle-strip and mode-triangle-fan import, and GltfConformanceL3/L5
// assert their converted index lists in full. What is left is the four topologies that decode
// correctly and have nowhere to be drawn -- a draw-path gap owned by GLTF-073/GLTF-077/GLTF-078,
// not a decoding one. Each stays here until it has one.

TEST(GltfKnownDefect, D5_NonIndexedPointsAreClassifiedAndRejectedPendingASupportDecision)
{
    ExpectTopologyClassifiedAndRejected("mode-points");
}

TEST(GltfKnownDefect, D5_LineListIsClassifiedAndRejectedPendingAPrimitiveType)
{
    ExpectTopologyClassifiedAndRejected("mode-lines");
}

TEST(GltfKnownDefect, D5_LineStripIsClassifiedAndRejectedPendingAPrimitiveType)
{
    ExpectTopologyClassifiedAndRejected("mode-line-strip");
}

TEST(GltfKnownDefect, D5_LineLoopIsClassifiedAndRejectedPendingItsClosingSegmentConversion)
{
    ExpectTopologyClassifiedAndRejected("mode-line-loop");
}

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

TEST(GltfKnownDefect, D7_FactorOnlyPbrMaterialKeepsItsFactorsButNotItsAlphaState)
{
    // Owned by GLTF-215/216/217/219/221 (landed) and GLTF-228/229/231 (outstanding).
    //
    // The claim has moved on with the code. The material is no longer downgraded and its factors
    // are no longer lost; what is still lost is the alpha and sidedness state, which has nowhere
    // to go -- MeshOut has no field for it and PbrEffect has no parameter for it.
    const LoadedFixture fixture("mat-factor-only-gold");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const JsonValue& defect = DefectRecord(fixture, "D7");
    ASSERT_NO_FATAL_FAILURE(RequireStillOpen(defect, "D7"));
    const JsonValue& actualRecord = CurrentActual(defect);

    // The file authors a real, non-default material with no texture map of any kind.
    ASSERT_EQ(1u, static_cast<std::size_t>(fixture.Data().materials_count));
    const cgltf_material& material = fixture.Data().materials[0];
    ASSERT_NE(0, material.has_pbr_metallic_roughness);
    EXPECT_NE(cgltf_alpha_mode_opaque, material.alpha_mode);
    EXPECT_NE(0, material.double_sided);

    const std::vector<ExtractedPrimitive> extracted = ExtractSceneMeshesEXT(fixture.Data());
    ASSERT_EQ(1u, extracted.size());
    ASSERT_TRUE(extracted[0].extracted) << extracted[0].error;
    const MeshOutDump& dump = extracted[0].dump;
    const JsonValue& expectedMaterial =
        Member(Path(fixture.Expected(), "l3.primitives").arrayValue.at(0), "material");
    ASSERT_EQ(JsonType::Object, expectedMaterial.type);

    // GLTF-215: the material MODEL now selects the effect, so a factor-only metallic-roughness
    // material reaches PbrEffect -- with no map of any kind, which is exactly what it could never
    // do before. GLTF-216's stride follows from that.
    EXPECT_TRUE(dump.usePbr)
        << "a factor-only material is downgraded again -- GLTF-215's rule was reverted";
    EXPECT_EQ(BoolOr(actualRecord, "usePbr", false), dump.usePbr);
    EXPECT_EQ(static_cast<int>(NumberOr(actualRecord, "stride", -1)), dump.stride);
    EXPECT_FALSE(dump.hasBaseColorImage) << "the fixture authors no texture at all";

    // Every authored FACTOR now survives, compared against the fixture's own spec-derived
    // expectation rather than against a number re-typed here.
    const std::vector<double> expectedBaseColor = Numbers(Member(expectedMaterial, "baseColorFactor"));
    ASSERT_EQ(4u, expectedBaseColor.size());
    for (std::size_t c = 0; c < 4; ++c)
    {
        EXPECT_NEAR(expectedBaseColor[c], static_cast<double>(dump.baseColorFactor[c]), kTolerance)
            << "baseColorFactor[" << c << "] -- GLTF-216";
    }
    EXPECT_NEAR(NumberOr(expectedMaterial, "metallicFactor", -1),
                static_cast<double>(dump.metallicFactor), kTolerance) << "GLTF-219";
    EXPECT_NEAR(NumberOr(expectedMaterial, "roughnessFactor", -1),
                static_cast<double>(dump.roughnessFactor), kTolerance) << "GLTF-219";
    const std::vector<double> expectedEmissive = Numbers(Member(expectedMaterial, "emissiveFactor"));
    ASSERT_EQ(3u, expectedEmissive.size());
    for (std::size_t c = 0; c < 3; ++c)
    {
        EXPECT_NEAR(expectedEmissive[c], static_cast<double>(dump.emissiveFactor[c]), kTolerance)
            << "emissiveFactor[" << c << "] -- GLTF-221";
    }

    // ...and the ledger agrees with the code about which fields those are, in both directions, so
    // the record cannot drift away from what the test actually measured.
    const std::vector<std::string> carried = Strings(Member(actualRecord, "carriedFields"));
    EXPECT_NE(carried.end(), std::find(carried.begin(), carried.end(), "baseColorFactor"));
    const std::vector<std::string> lost = Strings(Member(actualRecord, "lostFields"));
    EXPECT_EQ(lost.end(), std::find(lost.begin(), lost.end(), "baseColorFactor"));

    // What remains: MeshOut has no field for the alpha and sidedness state at all, so there is
    // nothing to read back here -- its absence IS the defect, and it stays named until
    // GLTF-228/229/231 give it somewhere to live.
    for (const char* field : {"alphaMode", "alphaCutoff", "doubleSided"})
    {
        EXPECT_NE(lost.end(), std::find(lost.begin(), lost.end(), field))
            << field << " is no longer recorded as lost -- if it now survives, update D7's record";
    }
    const std::vector<std::string> remaining = Strings(Member(defect, "remainingTasks"));
    EXPECT_NE(remaining.end(), std::find(remaining.begin(), remaining.end(), "GLTF-228"))
        << "D7 no longer names GLTF-228 as outstanding; if alphaMode landed, mark the defect fixed "
           "in tools/gltf_fixtures and delete this test";
}

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
    const std::set<std::string> open = {"D5", "D6", "D7"};
    // Remediated defects, and the task that closed each. Their records stay in the corpus as
    // regression witnesses and their fixtures are asserted by the ordinary conformance suites.
    const std::map<std::string, std::string> remediated = {
        {"D1", "GLTF-114"}, {"D2", "GLTF-114"}, {"D3", "GLTF-114"}, {"D4", "GLTF-063"},
        {"D8", "GLTF-247"}};

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

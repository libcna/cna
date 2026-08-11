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
        EXPECT_NE(remaining.end(), std::find(remaining.begin(), remaining.end(), "GLTF-072"))
            << "D5 no longer names GLTF-072 as outstanding; if the conversion landed, this test "
               "and the fixture's defect record both need to move to fixed";

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
            << "a non-TRIANGLES primitive imported successfully -- if GLTF-072 landed, mark D5 "
               "fixed in tools/gltf_fixtures and delete this test";
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
        EXPECT_EQ(1u, Member(prior, "triangles").arrayValue.size())
            << "the pre-GLTF-071 measurement (one silently reinterpreted triangle) was lost";
        EXPECT_FALSE(BoolOr(prior, "topologyCarried", true));
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

TEST(GltfKnownDefect, D5_TriangleStripIsClassifiedAndRejectedPendingConversion)
{
    // GLTF-071 closed the reading half; GLTF-072 owns the strip -> triangle-list conversion.
    ExpectTopologyClassifiedAndRejected("mode-triangle-strip");
}

TEST(GltfKnownDefect, D5_NonIndexedPointsAreClassifiedAndRejectedPendingASupportDecision)
{
    // GLTF-071 closed the reading half; GLTF-072/GLTF-077 own what a point primitive becomes.
    ExpectTopologyClassifiedAndRejected("mode-points");
}

// --- D6: rigid node animation ---------------------------------------------------------------------

TEST(GltfKnownDefect, D6_RigidNodeAnimationIsSilentlyDropped)
{
    // Owned by GLTF-103 -> GLTF-113 -> GLTF-114 -> GLTF-284.
    using namespace CNA::Internal::GltfImport;

    const LoadedFixture fixture("anim-rigid-node");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const JsonValue& defect = DefectRecord(fixture, "D6");
    ASSERT_NO_FATAL_FAILURE(RequireStillOpen(defect, "D6"));
    const JsonValue& actualRecord = CurrentActual(defect);

    // The file really does carry the animation -- the loss is entirely on CNA's side.
    ASSERT_EQ(1u, static_cast<std::size_t>(fixture.Data().animations_count));
    ASSERT_EQ(1u, static_cast<std::size_t>(fixture.Data().animations[0].channels_count));
    ASSERT_EQ(0u, static_cast<std::size_t>(fixture.Data().skins_count));

    // Mechanism one: clip extraction is gated on the group having a skin, and this file has none,
    // so the converter never calls ExtractClips at all and emits no "animations" key.
    const std::vector<MeshGroup> groups = CollectMeshGroups(&fixture.Data());
    ASSERT_EQ(1u, groups.size());
    EXPECT_EQ(nullptr, groups[0].skin);
    EXPECT_TRUE(BoolOr(actualRecord, "clipExtractionGatedOnSkin", false));

    // Mechanism two: even when called, ExtractClips resolves each channel target against the
    // joint set, so a channel targeting an ordinary mesh node is skipped -- silently, with no
    // warning. Both mechanisms have to be addressed for D6 to be fixed.
    SkeletonResult emptySkeleton;
    std::vector<std::string> warnings;
    const std::vector<ClipOut> clips =
        ExtractClips(&fixture.Data(), emptySkeleton, 1.0f, warnings);
    EXPECT_EQ(static_cast<std::size_t>(NumberOr(actualRecord, "clipCountIfExtractClipsWereCalled", -1)),
              clips.size());
    std::size_t trackCount = 0;
    for (const ClipOut& clip : clips) { trackCount += clip.tracks.size(); }
    EXPECT_EQ(static_cast<std::size_t>(NumberOr(actualRecord, "trackCountIfExtractClipsWereCalled", -1)),
              trackCount);
    EXPECT_EQ(0u, trackCount)
        << "a rigid node channel now produces a track -- if GLTF-284 landed, mark D6 fixed in "
           "tools/gltf_fixtures and delete this test";
    EXPECT_EQ(BoolOr(actualRecord, "warningEmitted", true), !warnings.empty())
        << "the drop is supposed to be silent; a warning appearing is itself a change";
}

// --- D7: factor-only PBR material ------------------------------------------------------------------

TEST(GltfKnownDefect, D7_FactorOnlyPbrMaterialIsDowngradedAndItsStateLost)
{
    // Owned by GLTF-217 / GLTF-228 / GLTF-229.
    const LoadedFixture fixture("mat-factor-only-gold");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const JsonValue& defect = DefectRecord(fixture, "D7");
    ASSERT_NO_FATAL_FAILURE(RequireStillOpen(defect, "D7"));
    const JsonValue& actualRecord = CurrentActual(defect);

    // The file authors a real, non-default material.
    ASSERT_EQ(1u, static_cast<std::size_t>(fixture.Data().materials_count));
    const cgltf_material& material = fixture.Data().materials[0];
    ASSERT_NE(0, material.has_pbr_metallic_roughness);
    EXPECT_NE(cgltf_alpha_mode_opaque, material.alpha_mode);
    EXPECT_NE(0, material.double_sided);

    const std::vector<ExtractedPrimitive> extracted = ExtractSceneMeshesEXT(fixture.Data());
    ASSERT_EQ(1u, extracted.size());
    ASSERT_TRUE(extracted[0].extracted) << extracted[0].error;
    const MeshOutDump& dump = extracted[0].dump;

    // With no normal map and no metallic-roughness map, the PBR path can never be selected, so the
    // surface falls all the way back to BasicEffect's stride-32 layout.
    EXPECT_EQ(BoolOr(actualRecord, "usePbr", true), dump.usePbr);
    EXPECT_FALSE(dump.usePbr)
        << "a factor-only material now selects the PBR path -- if GLTF-217 landed, mark D7 fixed "
           "in tools/gltf_fixtures and delete this test";
    EXPECT_EQ(static_cast<int>(NumberOr(actualRecord, "stride", -1)), dump.stride);

    // Not one material property survives. MeshOut has no field at all for baseColorFactor,
    // alphaMode, alphaCutoff or doubleSided; and the three factor fields it does have are
    // assigned only inside ExtractMesh's `if (usePbr)` guard, so they are left at MeshOut's own
    // defaults rather than the file's values. This is plan_gltf.md §1.1's "zero material fields
    // emitted", confirmed field by field.
    const JsonValue& expectedMaterial =
        Member(Path(fixture.Expected(), "l3.primitives").arrayValue.at(0), "material");
    ASSERT_EQ(JsonType::Object, expectedMaterial.type);

    EXPECT_NEAR(NumberOr(actualRecord, "metallicFactor", -1),
                static_cast<double>(dump.metallicFactor), kTolerance);
    EXPECT_NEAR(NumberOr(actualRecord, "roughnessFactor", -1),
                static_cast<double>(dump.roughnessFactor), kTolerance);
    EXPECT_GT(std::fabs(NumberOr(expectedMaterial, "metallicFactor", -1) -
                        static_cast<double>(dump.metallicFactor)), kTolerance)
        << "metallicFactor now reaches MeshOut for a factor-only material -- if GLTF-217 landed, "
           "mark D7 fixed in tools/gltf_fixtures and delete this test";
    EXPECT_GT(std::fabs(NumberOr(expectedMaterial, "roughnessFactor", -1) -
                        static_cast<double>(dump.roughnessFactor)), kTolerance)
        << "roughnessFactor now reaches MeshOut for a factor-only material";

    const std::vector<double> expectedEmissive = Numbers(Member(expectedMaterial, "emissiveFactor"));
    const std::vector<double> actualEmissive = Numbers(Member(actualRecord, "emissiveFactor"));
    ASSERT_EQ(3u, expectedEmissive.size());
    ASSERT_EQ(3u, actualEmissive.size());
    double emissiveDelta = 0.0;
    for (std::size_t c = 0; c < 3; ++c)
    {
        EXPECT_NEAR(actualEmissive[c], static_cast<double>(dump.emissiveFactor[c]), kTolerance);
        emissiveDelta += std::fabs(expectedEmissive[c] - static_cast<double>(dump.emissiveFactor[c]));
    }
    EXPECT_GT(emissiveDelta, kTolerance) << "emissiveFactor now reaches MeshOut";

    EXPECT_TRUE(Strings(Member(actualRecord, "carriedFields")).empty())
        << "the ledger claims a material field survives; none does";
    EXPECT_FALSE(dump.hasBaseColorImage);
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

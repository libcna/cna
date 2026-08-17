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
// Current state: ALL EIGHT audit defects are FIXED, so this file contains no known-defect tests at
// all -- only the ledger test at the end, which asserts the bookkeeping in both directions and is
// what will catch the next defect being recorded without one. Each remediated defect's record stays
// in the corpus as its regression witness, with the audit's original measurement under priorActual,
// and its behaviour is asserted by ordinary green tests in the GltfConformance, GltfPrimitiveTopology,
// GltfSkinSpaces, GltfRigidAnimation, GltfMaterialState and GltfDrawTopology suites.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <gtest/gtest.h>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Matrix.hpp"

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"

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

// --- D6: rigid node animation ---------------------------------------------------------------------
//
// REMEDIATED by GLTF-103 -> GLTF-113 -> GLTF-114 -> GLTF-293 -> GLTF-294. There is deliberately no
// known-defect test here any more: the clip is extracted, serialised, read back and playable, and
// GltfRigidAnimation asserts all four ends of that. The record stays in the corpus as the
// regression witness with the audit's original measurement under priorActual.


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
    // Every audit defect (D1..D8) is remediated, and so is GLTF-241, the one later non-audit defect
    // the ledger recorded the same way -- GLTF-462 carried the combination instead of continuing to
    // report it, so its "still broken" test below became a fix witness. The ledger is currently
    // empty of open defects, and that is a claim this test keeps honest in both directions: a new
    // divergence declared in the corpus immediately demands an executable test here.
    const std::set<std::string> open = {};
    // Remediated defects, and the task that closed each. Their records stay in the corpus as
    // regression witnesses and their fixtures are asserted by the ordinary conformance suites.
    const std::map<std::string, std::string> remediated = {
        {"D1", "GLTF-114"}, {"D2", "GLTF-114"}, {"D3", "GLTF-114"}, {"D4", "GLTF-063"},
        {"D5", "GLTF-073"}, {"D6", "GLTF-294"}, {"D7", "GLTF-228"}, {"D8", "GLTF-247"},
        {"GLTF-241", "GLTF-462"}};

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

// --- plan_gltf.md GLTF-241/GLTF-462: vertex-coloured PBR, carried rather than reported ----------

// This used to be the one material combination CNA could not import as the file asks.
// `mat-vertex-color-pbr` carries COLOR_0 and a metallic-roughness material, and GLTF-241 chose the
// second outcome its acceptance allowed: import through BasicEffect, name the loss. The loss was
// large -- stride 24 has no Normal slot, so the authored normals went with the material and the
// primitive could not be lit at all, let alone shaded.
//
// GLTF-462 removes it. §3.7.2.1 makes COLOR_0 "an additional linear multiplier to base color",
// which is a TERM in the metallic-roughness product rather than a reason to leave the model, and
// stride 60 already reserved four bytes purely to stay distinct from stride 56 -- so the colour has
// somewhere to live without a stride any renderer's input layout would have to learn. This test is
// now the regression witness for the fix, and it asserts every part of the file that used to be
// dropped: the material model, the authored normals, the tangent basis and the colour, together.
TEST(GltfKnownDefect, GLTF241_VertexColouredPbrKeepsItsMaterialItsNormalsAndItsColour)
{
    const LoadedFixture fixture("mat-vertex-color-pbr");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    const CNA::Internal::GltfImport::MeshOut extracted = CNA::Internal::GltfImport::ExtractMesh(
        &fixture.Data(), fixture.Data().meshes[0].primitives[0], "ColoredMetalTri", nullptr, 1.0f);

    EXPECT_TRUE(extracted.colored);
    EXPECT_TRUE(extracted.usePbr)
        << "vertex colour is a multiplier on base colour, not a different material model";
    EXPECT_EQ(60, extracted.stride);
    EXPECT_TRUE(extracted.unsupportedMaterialModelEXT.empty())
        << "nothing is dropped any more, so nothing may be reported as dropped -- a report that "
           "keeps firing after its cause is gone is how a fixed defect looks open forever";
    EXPECT_FALSE(extracted.droppedNormalForStrideEXT)
        << "stride 60 has a Normal slot, so the authored normals are carried";
    EXPECT_FALSE(extracted.droppedTangentForStrideEXT);

    // The three streams that used to be mutually exclusive, all present in one record. Read through
    // the canonical stride table rather than at hardcoded offsets (GLTF-278).
    const CNA::Internal::Graphics::InferredVertexLayout layout =
        CNA::Internal::Graphics::InferredLayoutForStride(
            extracted.stride, CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt);
    ASSERT_TRUE(layout.known);
    int normalOffset = -1, tangentOffset = -1, colorOffset = -1;
    for (std::size_t e = 0; e < layout.count; ++e)
    {
        if (layout.elements[e].usageIndex != 0) { continue; }
        using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
        if (layout.elements[e].usage == VertexElementUsage::Normal)
        { normalOffset = layout.elements[e].offset; }
        else if (layout.elements[e].usage == VertexElementUsage::Tangent)
        { tangentOffset = layout.elements[e].offset; }
        else if (layout.elements[e].usage == VertexElementUsage::Color)
        { colorOffset = layout.elements[e].offset; }
    }
    ASSERT_GE(normalOffset, 0);
    ASSERT_GE(tangentOffset, 0);
    ASSERT_GE(colorOffset, 0);

    const CNA::Internal::JsonValue& primitives = Path(fixture.Expected(), "l3.primitives");
    ASSERT_EQ(CNA::Internal::JsonType::Array, primitives.type);
    ASSERT_FALSE(primitives.arrayValue.empty());
    const std::vector<double> expectedNormals =
        Numbers(Member(primitives.arrayValue.front(), "normals"));
    const std::vector<double> expectedColors =
        Numbers(Member(primitives.arrayValue.front(), "colors"));
    ASSERT_FALSE(expectedNormals.empty())
        << "the fixture stopped authoring normals, so it can no longer show they are carried";
    ASSERT_FALSE(expectedColors.empty());

    const std::size_t vertices =
        extracted.vertexBytes.size() / static_cast<std::size_t>(extracted.stride);
    ASSERT_EQ(expectedNormals.size(), vertices * 3);
    ASSERT_EQ(expectedColors.size(), vertices * 4);
    for (std::size_t v = 0; v < vertices; ++v)
    {
        SCOPED_TRACE("vertex " + std::to_string(v));
        const std::size_t base = v * static_cast<std::size_t>(extracted.stride);
        float normal[3];
        std::memcpy(normal, extracted.vertexBytes.data() + base +
                                static_cast<std::size_t>(normalOffset), sizeof(normal));
        for (std::size_t c = 0; c < 3; ++c)
        {
            EXPECT_NEAR(expectedNormals[v * 3 + c], normal[c], 1e-5)
                << "the authored normal did not survive";
        }
        float tangent[4];
        std::memcpy(tangent, extracted.vertexBytes.data() + base +
                                 static_cast<std::size_t>(tangentOffset), sizeof(tangent));
        EXPECT_NEAR(1.0f, std::sqrt(tangent[0] * tangent[0] + tangent[1] * tangent[1] +
                                    tangent[2] * tangent[2]), 1e-4f)
            << "a PBR layout with no usable tangent basis cannot normal-map";
        std::uint8_t color[4];
        std::memcpy(color, extracted.vertexBytes.data() + base +
                               static_cast<std::size_t>(colorOffset), sizeof(color));
        for (std::size_t c = 0; c < 4; ++c)
        {
            const auto expectedByte = static_cast<int>(
                std::clamp(expectedColors[v * 4 + c], 0.0, 1.0) * 255.0 + 0.5);
            EXPECT_EQ(expectedByte, static_cast<int>(color[c]))
                << "COLOR_0 component " << c;
        }
    }

    // And the material's own factors, which GLTF-219/221 already carried into MeshOut while no
    // effect would consume them. Now one does.
    EXPECT_NEAR(0.85f, extracted.material.metallicFactor, 1e-5f);
    EXPECT_NEAR(0.15f, extracted.material.roughnessFactor, 1e-5f);
}

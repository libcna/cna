// SPDX-License-Identifier: MS-PL
//
// plans/plan_gltf.md GLTF-327 / GLTF-329 / GLTF-330 / GLTF-331 / GLTF-332: the corpus half of the
// `KHR_lights_punctual` approximation.
//
// `GltfClipAndLight` already locks the per-light *rules* on scratch documents that can vary one
// thing at a time. This file works from committed corpus fixtures instead, and asserts the two
// things a scratch document cannot: which lights survive when a file declares more than XNA can
// bind, and that the surviving three are chosen by an ordering the FILE defines rather than by one
// the importer happens to walk in.
//
// None of it is visible in the imported result. A scene lit by five ranged lights imports as three
// unbounded ones and simply renders differently, which is why every assertion here is paired with
// one about what the importer reports.

#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "GltfFixtureCorpus.hpp"

using CNA::Internal::JsonType;
using CNA::Internal::JsonValue;
using CnaTest::GltfOracle::BoolOr;
using CnaTest::GltfOracle::LoadedFixture;
using CnaTest::GltfOracle::Member;
using CnaTest::GltfOracle::Numbers;
using CnaTest::GltfOracle::NumberOr;
using CnaTest::GltfOracle::Path;
using CnaTest::GltfOracle::StringOr;

namespace
{
    constexpr float kTolerance = 1e-4f;

    /// The `l4.lights` block of a fixture that declares one, or a null value.
    const JsonValue& LightExpectation(const LoadedFixture& fixture)
    {
        return Path(fixture.Expected(), "l4.lights");
    }

    /// Every `lights-*` fixture in the corpus, in its own stable order.
    std::vector<std::string> LightFixtureIds()
    {
        std::vector<std::string> ids;
        for (const std::string& id : CnaTest::GltfOracle::CorpusFixtureIds())
        {
            if (id.rfind("lights-", 0) == 0) { ids.push_back(id); }
        }
        return ids;
    }
}

// --- GLTF-327: range and cone angles ------------------------------------------------------------

TEST(GltfLightBudget, RangeAndConeAnglesAreIgnoredAndCountedWhereTheyAreLost)
{
    // Both bound a light's REACH, and the directional light each is approximated by has no bounds
    // at all -- so a lamp the author scoped to one room lights the whole scene. The error grows
    // with distance from the light, which is exactly where an author placing it will not see it.
    //
    // Counted apart from the kind approximation above them: that one is about what the light IS,
    // this one about how far it goes, and a report that merged them could not say which.
    using namespace CNA::Internal::GltfImport;

    const LoadedFixture fixture("lights-kinds-and-reach");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const JsonValue& expected = LightExpectation(fixture);
    ASSERT_EQ(JsonType::Object, expected.type) << "the fixture states no light expectation";

    LightReportEXT report;
    const std::vector<LightOut> lights = ExtractPunctualLightsEXT(&fixture.Data(), report);

    EXPECT_EQ(static_cast<std::size_t>(NumberOr(expected, "importedCount", -1)), lights.size());
    EXPECT_EQ(static_cast<std::size_t>(NumberOr(expected, "ignoredRangeCount", -1)),
              report.ignoredRangeCount);
    EXPECT_EQ(static_cast<std::size_t>(NumberOr(expected, "ignoredConeAngleCount", -1)),
              report.ignoredConeAngleCount);

    // The kind approximation is reported separately and still correctly, so the two counts cannot
    // have been wired to the same source.
    EXPECT_EQ(static_cast<std::size_t>(NumberOr(expected, "approximatedPointCount", -1)),
              report.approximatedPointLightCount);
    EXPECT_EQ(static_cast<std::size_t>(NumberOr(expected, "approximatedSpotCount", -1)),
              report.approximatedSpotLightCount);

    // Nothing here is bright, so the clamp must stay silent -- otherwise "something was lost"
    // would be true for reasons this fixture is not about.
    EXPECT_EQ(0u, report.clampedIntensityLightCount);
    EXPECT_EQ(0u, report.droppedLightCount);
    EXPECT_TRUE(report.AnythingLost());
}

TEST(GltfLightBudget, ADirectionalLightWithNoRangeReportsNothingLost)
{
    // The control for every count above. A directional light has no range and no cone in the first
    // place, so a report that incremented on light *presence* rather than on an authored bound
    // would be indistinguishable from a correct one without this.
    using namespace CNA::Internal::GltfImport;

    const LoadedFixture fixture("lights-kinds-and-reach");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    // The fixture's first light is the directional control; the counts above already prove the
    // other two are what drive the totals. Here the assertion is on the arithmetic: three lights,
    // two ranges, one cone -- never three of each.
    const JsonValue& expected = LightExpectation(fixture);
    const JsonValue& entries = Member(expected, "entries");
    ASSERT_EQ(JsonType::Array, entries.type);

    std::size_t exactLights = 0;
    for (const JsonValue& row : entries.arrayValue)
    {
        if (BoolOr(row, "exact", false)) { ++exactLights; }
    }
    EXPECT_EQ(1u, exactLights) << "the fixture must carry exactly one exactly-mapped light";

    LightReportEXT report;
    (void)ExtractPunctualLightsEXT(&fixture.Data(), report);
    EXPECT_LT(report.ignoredRangeCount, entries.arrayValue.size())
        << "every light was counted as ranged, including the one that declares no range";
    EXPECT_LT(report.ignoredConeAngleCount, entries.arrayValue.size());
}

// --- GLTF-329: which three survive, and why -----------------------------------------------------

TEST(GltfLightBudget, TheThreeImportedLightsAreTheFirstThreeInNodeOrderNotInLightArrayOrder)
{
    // XNA binds DirectionalLight0..2 and nothing else, so a file with five lights loses two. WHICH
    // two must come from an ordering the file itself defines over light *placements*, and the only
    // such ordering is the node array restricted to the default scene.
    //
    // The extension's own `lights` array is an ordering over light *definitions*, which is a
    // different thing -- one definition may be instanced by several nodes, and a definition no node
    // references is not in the scene at all. The fixture authors it in exactly reverse order, so a
    // reader that walked it keeps a different three and the colours say so immediately.
    using namespace CNA::Internal::GltfImport;

    const LoadedFixture fixture("lights-over-budget");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const JsonValue& expected = LightExpectation(fixture);
    ASSERT_EQ(JsonType::Object, expected.type);

    LightReportEXT report;
    const std::vector<LightOut> lights = ExtractPunctualLightsEXT(&fixture.Data(), report);

    ASSERT_EQ(static_cast<std::size_t>(NumberOr(expected, "importedCount", -1)), lights.size());
    EXPECT_EQ(static_cast<std::size_t>(NumberOr(expected, "droppedCount", -1)),
              report.droppedLightCount);

    const JsonValue& colours = Member(expected, "importedDiffuseColors");
    ASSERT_EQ(JsonType::Array, colours.type);
    ASSERT_EQ(lights.size(), colours.arrayValue.size());
    for (std::size_t i = 0; i < lights.size(); ++i)
    {
        SCOPED_TRACE("light " + std::to_string(i));
        const std::vector<double> rgb = Numbers(colours.arrayValue[i]);
        ASSERT_EQ(3u, rgb.size());
        EXPECT_NEAR(static_cast<float>(rgb[0]), lights[i].diffuseColor.X, kTolerance);
        EXPECT_NEAR(static_cast<float>(rgb[1]), lights[i].diffuseColor.Y, kTolerance);
        EXPECT_NEAR(static_cast<float>(rgb[2]), lights[i].diffuseColor.Z, kTolerance);
    }
}

TEST(GltfLightBudget, ExtractionKeepsWalkingPastTheThirdLightSoItCanCountWhatItDrops)
{
    // "3 of 5 imported" is actionable and "3 imported" is not. Stopping at three would produce the
    // same three lights and a dropped count of zero, which is a report that says a lossy import was
    // lossless -- worse than no report, because it is believed.
    using namespace CNA::Internal::GltfImport;

    const LoadedFixture fixture("lights-over-budget");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    LightReportEXT report;
    const std::vector<LightOut> lights = ExtractPunctualLightsEXT(&fixture.Data(), report);
    EXPECT_EQ(3u, lights.size());
    EXPECT_GT(report.droppedLightCount, 0u);
    EXPECT_EQ(static_cast<std::size_t>(NumberOr(LightExpectation(fixture), "declaredCount", -1)),
              lights.size() + report.droppedLightCount)
        << "imported + dropped must account for every light in the scene, or the count is a guess";
}

TEST(GltfLightBudget, ExtractionIsDeterministicAcrossRepeatedRuns)
{
    // The ordering is only a property if it is stable. Asserted by extracting twice from one parse
    // rather than by trusting a single run: a reader that walked an unordered container would very
    // likely produce the same answer once and a different one on a rehash.
    using namespace CNA::Internal::GltfImport;

    const LoadedFixture fixture("lights-over-budget");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    LightReportEXT firstReport;
    const std::vector<LightOut> first = ExtractPunctualLightsEXT(&fixture.Data(), firstReport);
    LightReportEXT secondReport;
    const std::vector<LightOut> second = ExtractPunctualLightsEXT(&fixture.Data(), secondReport);

    ASSERT_EQ(first.size(), second.size());
    for (std::size_t i = 0; i < first.size(); ++i)
    {
        SCOPED_TRACE("light " + std::to_string(i));
        EXPECT_FLOAT_EQ(first[i].diffuseColor.X, second[i].diffuseColor.X);
        EXPECT_FLOAT_EQ(first[i].diffuseColor.Y, second[i].diffuseColor.Y);
        EXPECT_FLOAT_EQ(first[i].diffuseColor.Z, second[i].diffuseColor.Z);
        EXPECT_FLOAT_EQ(first[i].direction.X, second[i].direction.X);
        EXPECT_FLOAT_EQ(first[i].direction.Y, second[i].direction.Y);
        EXPECT_FLOAT_EQ(first[i].direction.Z, second[i].direction.Z);
    }
    EXPECT_EQ(firstReport.droppedLightCount, secondReport.droppedLightCount);
}

// --- GLTF-330: the photometric clamp -------------------------------------------------------------

TEST(GltfLightBudget, AnUnboundedPhotometricIntensityClampsToWhiteAndSaysSo)
{
    // glTF intensity is photometric and unbounded -- lux for a directional light -- while
    // DiffuseColor is a [0,1] colour. 683 is not an absurd authored value; it is the
    // lumens-per-watt constant, and a real exporter emits numbers of that magnitude. It imports as
    // plain white, which cannot be fixed inside XNA's lighting model and must therefore be said
    // out loud rather than left for an author to deduce from a render that looks blown out.
    using namespace CNA::Internal::GltfImport;

    const LoadedFixture fixture("lights-over-budget");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const JsonValue& expected = LightExpectation(fixture);

    LightReportEXT report;
    const std::vector<LightOut> lights = ExtractPunctualLightsEXT(&fixture.Data(), report);

    EXPECT_EQ(static_cast<std::size_t>(NumberOr(expected, "clampedIntensityCount", -1)),
              report.clampedIntensityLightCount);
    EXPECT_NEAR(static_cast<float>(NumberOr(expected, "worstPreClampChannel", -1.0)),
                report.worstPreClampChannelEXT, 1e-2f)
        << "the pre-clamp magnitude is what tells an author how far out of gamut they were; "
           "without it the report says only that something was clipped";

    // Every imported channel is in gamut, whatever the file asked for.
    for (const LightOut& light : lights)
    {
        EXPECT_LE(light.diffuseColor.X, 1.0f);
        EXPECT_LE(light.diffuseColor.Y, 1.0f);
        EXPECT_LE(light.diffuseColor.Z, 1.0f);
        EXPECT_GE(light.diffuseColor.X, 0.0f);
    }
}

// --- GLTF-332: the light fixtures, swept ---------------------------------------------------------

TEST(GltfLightBudget, EveryLightFixtureMatchesItsManifestOnEveryReportedLoss)
{
    // The regression sweep. Each fixture states all six counts, so a change that fixed one loss and
    // silently introduced another fails here rather than in whichever single test happened to cover
    // the one that changed.
    using namespace CNA::Internal::GltfImport;

    const std::vector<std::string> ids = LightFixtureIds();
    ASSERT_FALSE(ids.empty()) << "no lights-* fixtures found -- the sweep proved nothing";

    for (const std::string& id : ids)
    {
        SCOPED_TRACE(id);
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        const JsonValue& expected = LightExpectation(fixture);
        ASSERT_EQ(JsonType::Object, expected.type) << "a lights-* fixture states no expectation";

        LightReportEXT report;
        const std::vector<LightOut> lights = ExtractPunctualLightsEXT(&fixture.Data(), report);

        EXPECT_EQ(static_cast<std::size_t>(NumberOr(expected, "importedCount", -1)), lights.size());
        EXPECT_EQ(static_cast<std::size_t>(NumberOr(expected, "droppedCount", -1)),
                  report.droppedLightCount);
        EXPECT_EQ(static_cast<std::size_t>(NumberOr(expected, "approximatedPointCount", -1)),
                  report.approximatedPointLightCount);
        EXPECT_EQ(static_cast<std::size_t>(NumberOr(expected, "approximatedSpotCount", -1)),
                  report.approximatedSpotLightCount);
        EXPECT_EQ(static_cast<std::size_t>(NumberOr(expected, "clampedIntensityCount", -1)),
                  report.clampedIntensityLightCount);
        EXPECT_EQ(static_cast<std::size_t>(NumberOr(expected, "ignoredRangeCount", -1)),
                  report.ignoredRangeCount);
        EXPECT_EQ(static_cast<std::size_t>(NumberOr(expected, "ignoredConeAngleCount", -1)),
                  report.ignoredConeAngleCount);

        // XNA's hard limit, restated per fixture: whatever the file declares, three is the most
        // that can ever arrive.
        EXPECT_LE(lights.size(), 3u);
        for (const LightOut& light : lights)
        {
            EXPECT_NEAR(1.0f, light.direction.Length(), 1e-3f)
                << "an imported direction must be normalised -- an effect binds it unchanged";
        }
    }
}

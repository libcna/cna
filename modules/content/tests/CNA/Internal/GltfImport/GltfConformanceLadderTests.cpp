// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-010: `ctest -L gltf-conformance` runs the whole ladder and names the failing
// layer.
//
// The label is assembled in cmake/UnitTests.cmake, one CTest entry per rung, so CTest's own
// per-test result is what names the layer. That arrangement has exactly one failure mode worth
// guarding: a new Gltf* suite that matches no rung's filter. It would still run under a plain
// `ctest`, so nothing would look broken -- but it would be absent from the conformance label, and
// the label would quietly stop meaning "the whole ladder". This file closes that hole by parsing
// the rung list out of the CMake file that defines it and checking the partition against the
// suites gtest actually registered in this binary.

#include <algorithm>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "GltfFixtureCorpus.hpp"

namespace
{
    /// The repository root, reached the same way the corpus tests reach tools/gltf_fixtures.
    std::filesystem::path RepositoryRoot()
    {
        const std::filesystem::path corpus = CnaTest::GltfOracle::CorpusDirectory();
        if (corpus.empty()) { return {}; }
        return corpus.parent_path().parent_path().parent_path();
    }

    /// Every `backticked` token in one markdown table cell, in order.
    std::vector<std::string> BacktickedTokens(const std::string& cell)
    {
        std::vector<std::string> out;
        std::string::size_type at = 0;
        while ((at = cell.find('`', at)) != std::string::npos)
        {
            const auto end = cell.find('`', at + 1);
            if (end == std::string::npos) { break; }
            out.push_back(cell.substr(at + 1, end - at - 1));
            at = end + 1;
        }
        return out;
    }

    struct Rung
    {
        std::string layer;
        std::vector<std::string> suitePrefixes;
    };

    /// Parses `set(CNA_GLTF_CONFORMANCE_RUNGS "L0|A.*:B.*" ...)` out of cmake/UnitTests.cmake.
    ///
    /// Reading the build file rather than restating its contents is the point: a duplicated list
    /// would drift, and a drifted list would assert nothing.
    std::vector<Rung> ParseRungs(std::string& error)
    {
        const std::filesystem::path cmakeFile = RepositoryRoot() / "cmake" / "UnitTests.cmake";
        std::ifstream file(cmakeFile);
        if (!file)
        {
            error = "cannot open " + cmakeFile.string();
            return {};
        }

        std::vector<Rung> rungs;
        std::string line;
        bool inList = false;
        while (std::getline(file, line))
        {
            if (!inList)
            {
                if (line.find("set(CNA_GLTF_CONFORMANCE_RUNGS") != std::string::npos)
                {
                    inList = true;
                }
                continue;
            }

            const std::size_t open = line.find('"');
            if (open == std::string::npos) { break; }
            const std::size_t close = line.find('"', open + 1);
            if (close == std::string::npos) { break; }
            const std::string entry = line.substr(open + 1, close - open - 1);

            const std::size_t bar = entry.find('|');
            if (bar == std::string::npos)
            {
                error = "rung entry without a '|' separator: " + entry;
                return {};
            }

            Rung rung;
            rung.layer = entry.substr(0, bar);
            std::string filters = entry.substr(bar + 1);
            std::size_t start = 0;
            while (start <= filters.size())
            {
                const std::size_t colon = filters.find(':', start);
                std::string token = filters.substr(
                    start, colon == std::string::npos ? std::string::npos : colon - start);
                // Every rung filter is a "<Suite>.*" pattern; the suite name is what matters here.
                if (token.size() > 2 && token.compare(token.size() - 2, 2, ".*") == 0)
                {
                    token.erase(token.size() - 2);
                }
                if (!token.empty()) { rung.suitePrefixes.push_back(token); }
                if (colon == std::string::npos) { break; }
                start = colon + 1;
            }
            rungs.push_back(std::move(rung));

            if (line.find(')') != std::string::npos) { break; }
        }

        if (rungs.empty() && error.empty())
        {
            error = "CNA_GLTF_CONFORMANCE_RUNGS not found in " + cmakeFile.string();
        }
        return rungs;
    }

    /// Every gtest suite in this binary whose name begins with "Gltf".
    std::vector<std::string> RegisteredGltfSuites()
    {
        std::vector<std::string> names;
        const ::testing::UnitTest& unitTest = *::testing::UnitTest::GetInstance();
        for (int i = 0; i < unitTest.total_test_suite_count(); ++i)
        {
            const std::string name = unitTest.GetTestSuite(i)->name();
            if (name.rfind("Gltf", 0) == 0) { names.push_back(name); }
        }
        std::sort(names.begin(), names.end());
        return names;
    }
}

// The rung list must be readable at all. If this fails, every assertion below is vacuous, so it is
// a separate case rather than a guard inside the real one.
TEST(GltfConformanceLadder, TheRungListIsReadableFromTheBuildFile)
{
    std::string error;
    const std::vector<Rung> rungs = ParseRungs(error);
    ASSERT_TRUE(error.empty()) << error;
    ASSERT_FALSE(rungs.empty());

    // The ladder's own rungs, in order, plus the two entries that ride the same label without
    // being layers. Naming them here means a rung that silently disappears from the build file is
    // caught, not just one that gains an unmatched suite.
    const std::vector<std::string> expectedLayers = {"L0", "L1", "L2", "L3", "L4", "L5", "L6",
                                                     "Ledger", "Tool"};
    std::vector<std::string> actualLayers;
    for (const Rung& rung : rungs) { actualLayers.push_back(rung.layer); }
    EXPECT_EQ(expectedLayers, actualLayers)
        << "the conformance label's rungs changed; update this expectation deliberately";
}

// The partition itself: every Gltf* suite in the binary is claimed by exactly one rung.
TEST(GltfConformanceLadder, EveryGltfSuiteBelongsToExactlyOneRung)
{
    std::string error;
    const std::vector<Rung> rungs = ParseRungs(error);
    ASSERT_TRUE(error.empty()) << error;
    ASSERT_FALSE(rungs.empty());

    const std::vector<std::string> suites = RegisteredGltfSuites();
    ASSERT_FALSE(suites.empty()) << "no Gltf test suite is registered at all";

    for (const std::string& suite : suites)
    {
        std::vector<std::string> claimedBy;
        for (const Rung& rung : rungs)
        {
            const bool claims = std::find(rung.suitePrefixes.begin(), rung.suitePrefixes.end(),
                                          suite) != rung.suitePrefixes.end();
            if (claims) { claimedBy.push_back(rung.layer); }
        }
        EXPECT_EQ(1u, claimedBy.size())
            << "suite '" << suite << "' is claimed by " << claimedBy.size()
            << " rungs of CNA_GLTF_CONFORMANCE_RUNGS in cmake/UnitTests.cmake; every glTF suite "
               "must belong to exactly one, or `ctest -L gltf-conformance` stops meaning "
               "\"the whole ladder\"";
    }

    // And the converse: a rung must not name a suite that no longer exists, which would make the
    // ctest entry silently run nothing.
    for (const Rung& rung : rungs)
    {
        for (const std::string& prefix : rung.suitePrefixes)
        {
            EXPECT_NE(suites.end(), std::find(suites.begin(), suites.end(), prefix))
                << "rung " << rung.layer << " names suite '" << prefix
                << "', which is not registered -- that CTest entry runs zero tests";
        }
    }
}

// --- plan_gltf.md GLTF-403 / GLTF-413: §27.1's evidence must exist ------------------------------

TEST(GltfConformanceLadder, EverySection271RowIsTraceableToFixturesAndTestsThatExist)
{
    // §27.1 is the CORE 2.0 CORRECT milestone's checklist, and §27.1.1 maps each of its 20 rows to
    // the fixtures that exercise it and the suites that assert it. A milestone citing evidence is
    // only worth as much as the evidence being real, so this checks exactly that: every row
    // present, every fixture named actually in the corpus, every suite named actually registered
    // in this binary. A renamed suite or a deleted fixture then fails a test rather than leaving
    // the declaration pointing at nothing.
    //
    // What it deliberately does NOT check is the State column. Whether a row is green is the
    // judgement `GLTF-458` has to make from the tests themselves; a test asserting its own
    // milestone would be circular.
    std::ifstream plan(RepositoryRoot() / "plan_gltf.md");
    ASSERT_TRUE(plan.is_open()) << "cannot open plan_gltf.md";

    std::set<std::string> registeredSuites;
    const ::testing::UnitTest& unitTest = *::testing::UnitTest::GetInstance();
    for (int i = 0; i < unitTest.total_test_suite_count(); ++i)
    {
        registeredSuites.insert(unitTest.GetTestSuite(i)->name());
    }
    ASSERT_FALSE(registeredSuites.empty());

    std::set<std::string> corpusIds;
    for (const std::string& id : CnaTest::GltfOracle::CorpusFixtureIds()) { corpusIds.insert(id); }
    ASSERT_FALSE(corpusIds.empty());

    std::set<int> rowsSeen;
    std::size_t fixturesChecked = 0;
    std::size_t suitesChecked = 0;
    std::string line;
    bool inSection = false;
    while (std::getline(plan, line))
    {
        if (line.rfind("#### 27.1.1 ", 0) == 0) { inSection = true; continue; }
        if (inSection && line.rfind("#", 0) == 0) { break; }
        if (!inSection || line.empty() || line.front() != '|') { continue; }
        if (line.find("---") != std::string::npos) { continue; }

        std::vector<std::string> cells;
        std::string cell;
        std::istringstream stream(line);
        while (std::getline(stream, cell, '|')) { cells.push_back(cell); }
        if (!cells.empty() && cells.front().find_first_not_of(" \t") == std::string::npos)
        {
            cells.erase(cells.begin());
        }
        if (cells.size() < 4) { continue; }

        const std::string number = cells[0].substr(cells[0].find_first_not_of(" \t"));
        if (number.rfind("#", 0) == 0) { continue; }  // the header row
        int row = 0;
        try { row = std::stoi(number); } catch (const std::exception&) { continue; }
        SCOPED_TRACE("§27.1 row " + std::to_string(row));
        rowsSeen.insert(row);

        // Every backticked token in the fixtures cell that is not a wildcard or a task id must be
        // a real corpus asset. A wildcard (`morph-*`) names a family the corpus test suites sweep,
        // and pinning each member here would duplicate the manifest.
        for (const std::string& token : BacktickedTokens(cells[1]))
        {
            if (token.find('*') != std::string::npos) { continue; }
            if (token.rfind("GLTF-", 0) == 0) { continue; }
            if (token.rfind("l5.", 0) == 0) { continue; }
            EXPECT_NE(corpusIds.end(), corpusIds.find(token))
                << "§27.1.1 row " << row << " cites fixture '" << token
                << "', which is not in the corpus";
            ++fixturesChecked;
        }

        // Every backticked token in the test cell must be a registered suite. This is the half
        // that rots fastest: a suite renamed in a refactor leaves a milestone citing a test that
        // no longer runs, and nothing else would notice.
        for (const std::string& token : BacktickedTokens(cells[2]))
        {
            if (token.rfind("GLTF-", 0) == 0) { continue; }
            if (token.find('(') != std::string::npos) { continue; }  // a named case, not a suite
            EXPECT_NE(registeredSuites.end(), registeredSuites.find(token))
                << "§27.1.1 row " << row << " cites test suite '" << token
                << "', which is not registered in this binary";
            ++suitesChecked;
        }
    }

    for (int row = 1; row <= 20; ++row)
    {
        EXPECT_NE(rowsSeen.end(), rowsSeen.find(row))
            << "§27.1 row " << row << " has no entry in §27.1.1, so the milestone would be "
               "declared with nothing said about it";
    }
    EXPECT_GT(fixturesChecked, 40u) << "too few fixtures cited for this to be traceability";
    EXPECT_GT(suitesChecked, 40u) << "too few suites cited for this to be traceability";
}

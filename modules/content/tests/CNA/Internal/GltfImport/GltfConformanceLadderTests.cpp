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

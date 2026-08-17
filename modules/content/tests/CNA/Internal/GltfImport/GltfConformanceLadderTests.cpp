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
#include <iterator>
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

    /// Every gtest suite in this binary whose name CONTAINS "Gltf".
    ///
    /// Contains, not begins-with, and the difference was a real hole: `RuntimeGltfModelTest` is a
    /// glTF suite by any reading -- it loads `.gltf` through `ContentManager` end to end -- and a
    /// prefix match left it outside the ladder entirely *and* outside the `--gtest_filter='Gltf*'`
    /// the sanitizer CI job runs. It was therefore neither rung-checked nor sanitised, and four of
    /// its cases had been failing on any renderer with a 3D pipeline since `GLTF-215` changed
    /// effect selection (`GLTF-383`).
    std::vector<std::string> RegisteredGltfSuites()
    {
        std::vector<std::string> names;
        const ::testing::UnitTest& unitTest = *::testing::UnitTest::GetInstance();
        for (int i = 0; i < unitTest.total_test_suite_count(); ++i)
        {
            const std::string name = unitTest.GetTestSuite(i)->name();
            if (name.find("Gltf") != std::string::npos) { names.push_back(name); }
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    /// Suites deliberately omitted from CnaTests on this target by cmake/UnitTests.cmake.
    ///
    /// GltfToCnjToolTest launches the converter through POSIX process APIs, so it cannot be linked
    /// into Windows, Emscripten or Android builds. It still belongs to the Tool rung and remains
    /// valid traceability evidence; the source-presence check below prevents this exception from
    /// hiding a removed or renamed suite on one of those targets.
    std::vector<std::string> PlatformExcludedGltfSuites()
    {
        std::vector<std::string> excluded;
#if defined(_WIN32) || defined(__EMSCRIPTEN__) || defined(__ANDROID__)
        excluded.push_back("GltfToCnjToolTest");
#endif
#ifndef CNA_DRACO_AVAILABLE
        // These suites contain a test-only encoder and semantic decoder parity checks, so they are
        // deliberately not registered in the CNA_ENABLE_DRACO=OFF configuration. Their source
        // presence is still verified below, just like a platform-excluded process suite.
        excluded.push_back("GltfDracoEncoderPin");
        excluded.push_back("GltfDracoParity");
#endif
        return excluded;
    }

    bool SourceDeclaresPlatformExcludedGltfSuite(const std::string& suite)
    {
        std::filesystem::path source;
        if (suite == "GltfToCnjToolTest")
        {
            source = RepositoryRoot() / "modules" / "content" / "tests" / "Microsoft" /
                "Xna" / "Framework" / "Content" / "GltfToCnjToolTests.cpp";
        }
        else if (suite == "GltfDracoEncoderPin" || suite == "GltfDracoParity")
        {
            source = RepositoryRoot() / "modules" / "content" / "tests" / "CNA" /
                "Internal" / "GltfImport" / "GltfDracoCorpusTests.cpp";
        }
        else { return false; }
        std::ifstream file(source);
        if (!file) { return false; }
        const std::string text((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
        return text.find("TEST(" + suite + ",") != std::string::npos;
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

    // The ladder's own rungs, in order, plus the three entries that ride the same label without
    // being layers. Naming them here means a rung that silently disappears from the build file is
    // caught, not just one that gains an unmatched suite.
    //
    // `Perf` is Phase 22's measurements (plan_gltf.md GLTF-433 … GLTF-443). It is on the label
    // rather than beside it because its assertions are conformance assertions in the campaign's
    // sense -- "a cached load still costs less than a parse", "a copied Model still shares its
    // buffers" -- and because a measurement nobody runs stops being a measurement.
    const std::vector<std::string> expectedLayers = {"L0", "L1", "L2", "L3", "L4", "L5", "L6",
                                                     "Perf", "Ledger", "Tool"};
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
    const std::vector<std::string> platformExcluded = PlatformExcludedGltfSuites();
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
            if (std::find(platformExcluded.begin(), platformExcluded.end(), prefix) !=
                platformExcluded.end())
            {
                EXPECT_TRUE(SourceDeclaresPlatformExcludedGltfSuite(prefix))
                    << "platform-excluded suite '" << prefix
                    << "' is no longer declared by its source file";
                continue;
            }
            EXPECT_NE(suites.end(), std::find(suites.begin(), suites.end(), prefix))
                << "rung " << rung.layer << " names suite '" << prefix
                << "', which is not registered -- that CTest entry runs zero tests";
        }
    }
}

TEST(GltfConformanceLadder, RendererParityIncludesSuitesWhoseNameContainsGltf)
{
    const std::filesystem::path script =
        RepositoryRoot() / "scripts" / "gltf-renderer-parity.sh";
    std::ifstream file(script);
    ASSERT_TRUE(file.is_open()) << "cannot open " << script;

    const std::string source((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
    // RuntimeGltfModelTest does not begin with `Gltf`. Checking both invocations pins the actual
    // comparison rather than a nearby comment that could say the right thing while the script
    // still omits the suite on either side.
    EXPECT_NE(std::string::npos, source.find("run \"$A\" '*Gltf*' > \"$tmp_a\""));
    EXPECT_NE(std::string::npos, source.find("run \"$B\" '*Gltf*' > \"$tmp_b\""));
}

TEST(GltfConformanceLadder, RequiredCiRunsEveryCampaignRendererForEveryCommitAndCannotIgnoreFailure)
{
    const std::filesystem::path workflow =
        RepositoryRoot() / ".github" / "workflows" / "gltf-renderer-stride-ci.yml";
    std::ifstream file(workflow);
    ASSERT_TRUE(file.is_open()) << "cannot open " << workflow;
    const std::string source((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());

    // Exact matrix ownership: omitting the non-rasterising controls or either real API turns a
    // differential gate into a single-backend smoke test. Vulkan remains useful extra evidence;
    // GLTF-398's minimum is the first three entries.
    EXPECT_NE(std::string::npos,
              source.find("renderer: [STUB, HEADLESS, OPENGLES3, VULKAN]"));
    EXPECT_NE(std::string::npos,
              source.find("xvfb-run -a ctest --test-dir build -L gltf-conformance "
                          "--output-on-failure"));
    EXPECT_NE(std::string::npos, source.find("fail-fast: false"));

    // A docs-only plan change is still a commit, and cancelling the prior SHA would leave it with
    // no result. Every pushed branch is included too: the workflow deliberately pays the cost
    // rather than redefining "per commit" as only a few long-lived branches.
    EXPECT_NE(std::string::npos, source.find("  push: {}\n"));
    EXPECT_EQ(std::string::npos, source.find("paths-ignore:"));
    EXPECT_NE(std::string::npos,
              source.find("group: gltf-renderer-conformance-${{ github.sha }}"));
    EXPECT_NE(std::string::npos, source.find("cancel-in-progress: false"));

    // GLTF-009/390: the application-owned L7 rung is just as required as the lower renderer
    // matrix. Pinning the viewer commit prevents a mutable develop head from changing the camera
    // or presentation policy underneath an otherwise unchanged CNA commit.
    EXPECT_NE(std::string::npos, source.find("  l7-corpus:\n"));
    EXPECT_NE(std::string::npos,
              source.find("f32d1f136c74ed1508d4887952bf5a7a3d3b8b40"));
    EXPECT_NE(std::string::npos, source.find("python3-pil"));
    EXPECT_NE(std::string::npos, source.find("--target cna_gltf_viewer --parallel 3"));
    EXPECT_NE(std::string::npos, source.find("-R '^CnaGltfConformanceL7$'"));

    // GitHub Actions fails a run step by default. Pin the absence of both escape hatches so a
    // future YAML cleanup cannot turn the required check green after a failed rung.
    EXPECT_EQ(std::string::npos, source.find("continue-on-error:"));
    EXPECT_EQ(std::string::npos, source.find("|| true"));
}

// --- plan_gltf.md GLTF-403 / GLTF-413: §27.1's evidence must exist ------------------------------

TEST(GltfConformanceLadder, EverySection271RowIsTraceableToFixturesAndTestsThatExist)
{
    // §27.1 is the CORE 2.0 CORRECT milestone's checklist, and §27.1.1 maps each of its 20 rows to
    // the fixtures that exercise it and the suites that assert it. A milestone citing evidence is
    // only worth as much as the evidence being real, so this checks exactly that: every row
    // present, every fixture named actually in the corpus, and every suite name backed either by
    // gtest in this binary or by an exact renderer CTest registration. Renderer framebuffer tests
    // cannot be members of a STUB CnaTests binary, so requiring only the former would make the
    // sanitizer build reject valid L7 evidence. A renamed suite or a deleted fixture still fails
    // rather than leaving the declaration pointing at nothing.
    //
    // What it deliberately does NOT check is the State column. Whether a row is green is the
    // judgement `GLTF-458` has to make from the tests themselves; a test asserting its own
    // milestone would be circular.
    std::ifstream plan(RepositoryRoot() / "plan_gltf.md");
    ASSERT_TRUE(plan.is_open()) << "cannot open plan_gltf.md";

    std::set<std::string> registeredEvidenceNames;
    const ::testing::UnitTest& unitTest = *::testing::UnitTest::GetInstance();
    for (int i = 0; i < unitTest.total_test_suite_count(); ++i)
    {
        registeredEvidenceNames.insert(unitTest.GetTestSuite(i)->name());
    }
    ASSERT_FALSE(registeredEvidenceNames.empty());

    // A platform may intentionally omit a suite whose implementation needs unavailable process
    // APIs. It remains evidence only while its real source still declares that suite; this is not
    // a free-form allow-list for stale plan references.
    for (const std::string& suite : PlatformExcludedGltfSuites())
    {
        EXPECT_TRUE(SourceDeclaresPlatformExcludedGltfSuite(suite))
            << "platform-excluded evidence suite '" << suite
            << "' is no longer declared by its source file";
        registeredEvidenceNames.insert(suite);
    }

    // EasyGL L7 checks are standalone executables registered with CTest, not gtest suites linked
    // into CnaTests. Read their one authoritative CMake registration rather than maintaining a
    // second allow-list here. Exact-token extraction still catches a rename or removed test.
    const std::filesystem::path easyGlTests =
        RepositoryRoot() / "modules" / "renderers" / "easygl" / "examples" / "CMakeLists.txt";
    std::ifstream easyGlFile(easyGlTests);
    ASSERT_TRUE(easyGlFile.is_open()) << "cannot open " << easyGlTests;
    std::string easyGlSource;
    std::string cmakeLine;
    while (std::getline(easyGlFile, cmakeLine))
    {
        // A removed registration retained as a comment is not evidence. Strip CMake comments
        // before matching, including trailing comments after an active command.
        const std::size_t comment = cmakeLine.find('#');
        if (comment != std::string::npos) { cmakeLine.resize(comment); }
        easyGlSource += cmakeLine;
        easyGlSource.push_back('\n');
    }
    const std::regex rendererRegistration(
        R"(cna_register_renderer_test\s*\(\s*NAME\s+([A-Za-z0-9_]+))");
    std::size_t rendererTestsFound = 0;
    for (std::sregex_iterator it(easyGlSource.begin(), easyGlSource.end(), rendererRegistration),
                              end;
         it != end; ++it)
    {
        registeredEvidenceNames.insert((*it)[1].str());
        ++rendererTestsFound;
    }
    ASSERT_GT(rendererTestsFound, 0u) << "no renderer CTest registrations found in " << easyGlTests;

    // The corpus-wide L7 executable is owned by the separately versioned production viewer, so
    // its CMake registration is not present in a clean CNA checkout.  The required CNA workflow
    // pins that viewer commit and selects the exact CTest name.  Extract exact anchored selectors
    // from the workflow instead of adding a free-form exception here: renaming/removing the CI
    // rung then makes the plan citation stale and fails this test.
    const std::filesystem::path workflow =
        RepositoryRoot() / ".github" / "workflows" / "gltf-renderer-stride-ci.yml";
    std::ifstream workflowFile(workflow);
    ASSERT_TRUE(workflowFile.is_open()) << "cannot open " << workflow;
    const std::string workflowSource((std::istreambuf_iterator<char>(workflowFile)),
                                     std::istreambuf_iterator<char>());
    const std::regex exactCtestSelector(R"(-R\s+['"]?\^([A-Za-z0-9_]+)\$['"]?)");
    std::size_t workflowTestsFound = 0;
    for (std::sregex_iterator it(workflowSource.begin(), workflowSource.end(), exactCtestSelector),
                              end;
         it != end; ++it)
    {
        registeredEvidenceNames.insert((*it)[1].str());
        ++workflowTestsFound;
    }
    ASSERT_GT(workflowTestsFound, 0u)
        << "no exact CTest selector found in the glTF conformance workflow";

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

        // Every backticked token in the test cell must be a registered gtest suite or standalone
        // renderer CTest. This is the half that rots fastest: a suite renamed in a refactor leaves
        // a milestone citing a test that no longer runs, and nothing else would notice.
        for (const std::string& token : BacktickedTokens(cells[2]))
        {
            if (token.rfind("GLTF-", 0) == 0) { continue; }
            if (token.find('(') != std::string::npos) { continue; }  // a named case, not a suite
            EXPECT_NE(registeredEvidenceNames.end(), registeredEvidenceNames.find(token))
                << "§27.1.1 row " << row << " cites test suite '" << token
                << "', which is neither registered in this binary nor as an EasyGL CTest";
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

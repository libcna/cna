// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-003 / GLTF-004: the generated conformance corpus and the L1-L4 ladder over it.
//
// This suite is unusual on purpose, and the shape is the point.
//
// `GltfConformance*` are ordinary green regression tests: for every fixture and every layer, each
// field of the spec-derived expectation is asserted, EXCEPT the specific fields a still-open
// defect is recorded as breaking. Nothing is skipped wholesale -- D7 loses a material and nothing
// else, so mat-factor-only-gold's positions, normals and indices stay fully asserted.
//
// `GltfKnownDefect*` are also green, and assert the opposite: that each proven defect is STILL
// PRESENT and still wrong in exactly the way the audit recorded. They exist so the defects are
// visible and executable rather than merely documented, and so that the remediation task which
// fixes one FAILS here -- loudly, with the fixture and the owning task named -- instead of
// silently passing. Flipping such a case to passing is a manifest edit in tools/gltf_fixtures
// (defect status -> "fixed"), never an edit to the fixture or to its expectation.
//
// The expectation manifests never contain CNA's wrong output as an expectation. Where a wrong
// value appears at all it is under `defects[].currentActual`, which is dated evidence, and the
// conformance assertions never read it.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <gtest/gtest.h>
#include <iomanip>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "System/Security/Cryptography/SHA256.hpp"

#include "CNA/Internal/Graphics/ImageLoader.hpp"

#include "GltfFixtureCorpus.hpp"
#include "GltfOracleEXT.hpp"

using namespace CnaTest::GltfOracle;
using CNA::Internal::JsonType;
using CNA::Internal::JsonValue;

namespace
{
    /// Every fixture value in this corpus is exactly representable in binary32, so the tolerance
    /// only absorbs the decode arithmetic itself (a normalized byte divided by 255, a matrix
    /// product), never authored imprecision.
    constexpr double kTolerance = 1e-5;

    void ExpectNear(double expected, double actual, const std::string& what)
    {
        EXPECT_NEAR(expected, actual, kTolerance) << what;
    }

    /// Compares a flat expected component list against a flat actual one, naming the first
    /// divergent component rather than dumping both arrays.
    void ExpectComponents(const std::vector<double>& expected, const std::vector<float>& actual,
                          const std::string& what)
    {
        ASSERT_EQ(expected.size(), actual.size())
            << what << ": component count differs (expected " << expected.size()
            << ", got " << actual.size() << ")";
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            ExpectNear(expected[i], static_cast<double>(actual[i]),
                       what + ": component[" + std::to_string(i) + "]");
        }
    }

    template <std::size_t N>
    std::vector<float> Flatten(const std::vector<std::array<float, N>>& values)
    {
        std::vector<float> out;
        out.reserve(values.size() * N);
        for (const std::array<float, N>& value : values)
        {
            out.insert(out.end(), value.begin(), value.end());
        }
        return out;
    }

    std::string HexDigest(const std::vector<std::uint8_t>& bytes)
    {
        std::ostringstream out;
        out << std::hex << std::setfill('0');
        for (const std::uint8_t b : bytes) { out << std::setw(2) << static_cast<unsigned>(b); }
        return out.str();
    }

    std::vector<std::uint8_t> ReadAllBytes(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file),
                                         std::istreambuf_iterator<char>());
    }

    const JsonValue& ExpectedPrimitives(const LoadedFixture& fixture)
    {
        return Path(fixture.Expected(), "l3.primitives");
    }

    /// True when the manifest's import policy names @p field as a stream CNA's chosen vertex
    /// layout has no slot for.
    bool IsDroppedAttribute(const JsonValue& expectedPrimitive, const std::string& field)
    {
        const JsonValue& dropped = Path(expectedPrimitive, "importPolicy.droppedAttributes");
        if (dropped.type != JsonType::Array) { return false; }
        for (const JsonValue& entry : dropped.arrayValue)
        {
            if (entry.type == JsonType::String && entry.stringValue == field) { return true; }
        }
        return false;
    }

    /// The extracted primitive matching a (mesh, primitive) pair, or nullptr when the import path
    /// produced none -- which is itself a meaningful answer for an exclusion fixture.
    const ExtractedPrimitive* FindExtracted(const std::vector<ExtractedPrimitive>& extracted,
                                            int mesh, int primitive)
    {
        for (const ExtractedPrimitive& entry : extracted)
        {
            if (entry.mesh == mesh && entry.primitive == primitive) { return &entry; }
        }
        return nullptr;
    }
}

// --- Corpus integrity -----------------------------------------------------------------------

TEST(GltfFixtureCorpus, CorpusDirectoryIsPresentAndParsed)
{
    ASSERT_FALSE(CorpusDirectory().empty())
        << "tests/assets/gltf was not found. Run the suite from the repository root, or "
           "regenerate it: PYTHONPATH=tools python3 -m gltf_fixtures --out tests/assets/gltf";
    ASSERT_EQ(JsonType::Object, CorpusManifest().type) << "tests/assets/gltf/manifest.json is "
                                                          "missing or malformed";
}

TEST(GltfFixtureCorpus, ManifestMatchesEveryCommittedFileByteForByte)
{
    // The generator is the source of truth and its output is committed. Verifying each file
    // against the digest the generator recorded proves the committed tree is exactly what
    // regeneration produces, without needing a Python interpreter at test time.
    const JsonValue& files = Member(CorpusManifest(), "files");
    ASSERT_EQ(JsonType::Array, files.type);
    ASSERT_FALSE(files.arrayValue.empty());

    std::set<std::string> listed;
    for (const JsonValue& entry : files.arrayValue)
    {
        const std::string path = StringOr(entry, "path", "");
        ASSERT_FALSE(path.empty()) << "manifest files[] entry has no path";
        listed.insert(path);
        if (path == "manifest.json") { continue; }  // the manifest cannot record its own digest

        const std::vector<std::uint8_t> bytes = ReadAllBytes(CorpusDirectory() / path);
        EXPECT_EQ(static_cast<std::size_t>(NumberOr(entry, "bytes", -1)), bytes.size())
            << path << ": size differs from the manifest";

        System::Security::Cryptography::SHA256 sha;
        EXPECT_EQ(StringOr(entry, "sha256", ""), HexDigest(sha.ComputeHash(bytes)))
            << path << ": content differs from the generator output. Regenerate with "
                       "'PYTHONPATH=tools python3 -m gltf_fixtures --out tests/assets/gltf' and "
                       "commit the result -- never hand-edit a generated fixture.";
    }

    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(CorpusDirectory()))
    {
        if (!entry.is_regular_file()) { continue; }
        const std::string name = entry.path().filename().string();
        // The manifest is written after the file list is computed, so it cannot list itself.
        if (name == "manifest.json") { continue; }
        EXPECT_TRUE(listed.count(name) != 0)
            << name << " is committed but not listed in manifest.json -- a stale file the "
               "generator no longer produces";
    }
}

TEST(GltfFixtureCorpus, TheGeneratorPackageIsCompleteInTheCheckout)
{
    // The corpus is only meaningful because a committed generator can reproduce it. That
    // guarantee failed silently once: .gitignore carried an unanchored 'build*', which matched
    // tools/gltf_fixtures/builder.py, so the generator's core module was never committed and
    // 'python3 -m gltf_fixtures' died with ModuleNotFoundError on every clean checkout. Every
    // other corpus test still passed, because the committed files and the committed manifest
    // agreed with each other -- neither of them needs the generator to exist.
    //
    // This closes that hole without needing a Python interpreter at test time: every module the
    // generator's own sources import from within the package must be present on disk. A module
    // that goes missing again fails here, naming the file and the importer.
    const std::filesystem::path corpus = CorpusDirectory();
    ASSERT_FALSE(corpus.empty());
    const std::filesystem::path generator =
        corpus.parent_path().parent_path().parent_path() / "tools" / "gltf_fixtures";
    ASSERT_TRUE(std::filesystem::is_directory(generator))
        << "tools/gltf_fixtures is missing -- the corpus has no generator to reproduce it";

    std::vector<std::filesystem::path> sources;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::recursive_directory_iterator(generator))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".py")
        {
            sources.push_back(entry.path());
        }
    }
    ASSERT_FALSE(sources.empty()) << "tools/gltf_fixtures contains no Python sources";

    for (const std::filesystem::path& source : sources)
    {
        std::ifstream file(source);
        ASSERT_TRUE(file.is_open()) << source.string();
        std::string line;
        while (std::getline(file, line))
        {
            // Only intra-package relative imports are checked: "from .x import" resolves against
            // the importing module's own directory, "from ..x import" against its parent.
            const std::size_t from = line.find("from .");
            if (from == std::string::npos || line.find("import") == std::string::npos) { continue; }
            // The leading dot means "this package", i.e. the importing module's own directory;
            // each dot after it climbs one level.
            std::size_t cursor = from + 5;
            std::filesystem::path base = source.parent_path();
            ++cursor;
            while (cursor < line.size() && line[cursor] == '.')
            {
                base = base.parent_path();
                ++cursor;
            }
            std::string module;
            while (cursor < line.size() && (std::isalnum(static_cast<unsigned char>(line[cursor]))
                                            || line[cursor] == '_'))
            {
                module += line[cursor++];
            }
            if (module.empty()) { continue; }  // "from . import x" -- the package itself

            const std::filesystem::path asModule = base / (module + ".py");
            const std::filesystem::path asPackage = base / module / "__init__.py";
            EXPECT_TRUE(std::filesystem::exists(asModule) || std::filesystem::exists(asPackage))
                << source.filename().string() << " imports '" << module
                << "', but neither " << asModule.string() << " nor " << asPackage.string()
                << " exists. The generator cannot run, so the corpus cannot be regenerated -- "
                   "check .gitignore is not swallowing the file (plan_gltf.md GLTF-003).";
        }
    }
}

TEST(GltfFixtureCorpus, ExternalReferencePinsAreImmutableOptionalAndMapOnlyExistingFixtures)
{
    // GLTF-013/014/016: the three Khronos inputs are developer references, not a hidden network
    // dependency. The Python runner additionally reads the two explicitly supplied upstream root
    // manifests and expands every fileName; this checkout-only test guards the committed side of
    // that contract without requiring Python, Git, npm, or network access.
    const std::filesystem::path pins =
        CorpusDirectory().parent_path().parent_path().parent_path()
        / "tools" / "gltf_fixtures" / "reference-pins.json";
    std::ifstream file(pins, std::ios::binary);
    ASSERT_TRUE(file.is_open()) << pins.string();
    const std::string text((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    JsonValue document;
    ASSERT_NO_THROW(document = CNA::Internal::ParseJson(text));
    ASSERT_EQ(JsonType::Object, document.type);
    EXPECT_DOUBLE_EQ(1.0, NumberOr(document, "schemaVersion", -1));

    const std::map<std::string, std::pair<std::string, std::string>> expectedSources = {
        {"sample-assets", {"GLTF-013",
                           "https://github.com/KhronosGroup/glTF-Sample-Assets"}},
        {"asset-generator", {"GLTF-014",
                              "https://github.com/KhronosGroup/glTF-Asset-Generator"}},
        {"sample-renderer", {"GLTF-016",
                             "https://github.com/KhronosGroup/glTF-Sample-Renderer"}},
    };
    const JsonValue& sources = Member(document, "sources");
    ASSERT_EQ(JsonType::Array, sources.type);
    ASSERT_EQ(expectedSources.size(), sources.arrayValue.size());
    std::set<std::string> seenSources;
    for (const JsonValue& source : sources.arrayValue)
    {
        ASSERT_EQ(JsonType::Object, source.type);
        const std::string id = StringOr(source, "id", "");
        const auto expected = expectedSources.find(id);
        ASSERT_NE(expectedSources.end(), expected) << "unknown reference source " << id;
        EXPECT_TRUE(seenSources.insert(id).second) << "duplicate reference source " << id;
        EXPECT_EQ(expected->second.first, StringOr(source, "task", ""));
        EXPECT_EQ(expected->second.second, StringOr(source, "repository", ""));
        EXPECT_FALSE(BoolOr(source, "runtimeDependency", true));
        EXPECT_FALSE(BoolOr(source, "ciDependency", true));

        const std::string revision = StringOr(source, "revision", "");
        EXPECT_EQ(40u, revision.size()) << id << ": pin must be a full commit";
        EXPECT_TRUE(std::all_of(revision.begin(), revision.end(), [](const char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        })) << id << ": pin must be lowercase hexadecimal";
        EXPECT_FALSE(StringOr(Member(source, "license"), "summary", "").empty())
            << id << ": pin has no licence summary";
    }

    const JsonValue& manifestMap = Member(document, "assetGeneratorManifest");
    ASSERT_EQ(JsonType::Object, manifestMap.type);
    EXPECT_EQ("group-semantic-overlap", StringOr(manifestMap, "mappingScope", ""));
    const JsonValue& paths = Member(manifestMap, "paths");
    ASSERT_EQ(JsonType::Object, paths.type);
    EXPECT_EQ("Output/Positive/Manifest.json", StringOr(paths, "positive", ""));
    EXPECT_EQ("Output/Negative/Manifest.json", StringOr(paths, "negative", ""));
    const JsonValue& manifestDigests = Member(manifestMap, "sha256");
    ASSERT_EQ(JsonType::Object, manifestDigests.type);
    EXPECT_EQ("100ccab87d7f9a072532ccc3f3cd998e234365c03404b080d1fef96db8096330",
              StringOr(manifestDigests, "positive", ""));
    EXPECT_EQ("6502a9724d1ec90ff6e55ae8db99b1c8185927df14d9f6831e275fe27555ec94",
              StringOr(manifestDigests, "negative", ""));

    const JsonValue& mappings = Member(manifestMap, "groupMappings");
    ASSERT_EQ(JsonType::Array, mappings.type);
    EXPECT_EQ(28u, mappings.arrayValue.size())
        << "the pinned Asset Generator revision has 28 root-manifest groups";
    const std::vector<std::string> fixtureIds = CorpusFixtureIds();
    const std::set<std::string> knownFixtures(fixtureIds.begin(), fixtureIds.end());
    std::set<std::string> groups;
    std::set<int> upstreamIds;
    std::size_t overlaps = 0;
    std::size_t gaps = 0;
    for (const JsonValue& mapping : mappings.arrayValue)
    {
        ASSERT_EQ(JsonType::Object, mapping.type);
        const std::string suite = StringOr(mapping, "suite", "");
        const std::string folder = StringOr(mapping, "folder", "");
        const std::string relationship = StringOr(mapping, "relationship", "");
        const std::string identity = suite + "/" + folder;
        EXPECT_TRUE(suite == "positive" || suite == "negative") << identity;
        EXPECT_TRUE(groups.insert(identity).second) << "duplicate mapping " << identity;
        const int upstreamId = static_cast<int>(NumberOr(mapping, "id", -1));
        EXPECT_GE(upstreamId, 0) << identity;
        EXPECT_TRUE(upstreamIds.insert(upstreamId).second) << "duplicate upstream id " << upstreamId;

        const std::vector<std::string> mapped = Strings(Member(mapping, "cnaFixtureIds"));
        if (relationship == "overlap")
        {
            ++overlaps;
            EXPECT_FALSE(mapped.empty()) << identity << ": overlap names no CNA fixture";
        }
        else if (relationship == "gap")
        {
            ++gaps;
            EXPECT_TRUE(mapped.empty()) << identity << ": gap claims CNA coverage";
        }
        else
        {
            ADD_FAILURE() << identity << ": unknown relationship " << relationship;
        }
        for (const std::string& fixtureId : mapped)
        {
            EXPECT_NE(knownFixtures.end(), knownFixtures.find(fixtureId))
                << identity << " maps to absent CNA fixture " << fixtureId;
        }
        EXPECT_FALSE(StringOr(mapping, "note", "").empty()) << identity << ": mapping has no note";
    }
    EXPECT_EQ(26u, overlaps);
    EXPECT_EQ(2u, gaps);
}

TEST(GltfFixtureCorpus, SampleAssetsFetcherIsPinnedSparseExplicitAndDeveloperOnly)
{
    // GLTF-405: executing the fetch-on-demand decision means more than documenting a URL. Keep
    // the opt-in tool bound to GLTF-013's exact pin and make the non-destructive sparse policy
    // checkout state, without invoking Git or requiring network access in this test.
    const std::filesystem::path script =
        CorpusDirectory().parent_path().parent_path().parent_path()
        / "scripts" / "fetch-gltf-sample-assets.sh";
    std::ifstream file(script, std::ios::binary);
    ASSERT_TRUE(file.is_open()) << script.string();
    const std::string text((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());

    EXPECT_NE(std::string::npos, text.find(
        "https://github.com/KhronosGroup/glTF-Sample-Assets"));
    EXPECT_NE(std::string::npos, text.find(
        "2bac6f8c57bf471df0d2a1e8a8ec023c7801dddf"));
    EXPECT_NE(std::string::npos, text.find("--filter=blob:none"));
    EXPECT_NE(std::string::npos, text.find("sparse-checkout init --cone"));
    EXPECT_NE(std::string::npos, text.find("sparse-checkout set"));
    EXPECT_NE(std::string::npos,
              text.find("checkout --quiet --detach \"$SAMPLE_ASSETS_REVISION\""));
    EXPECT_NE(std::string::npos,
              text.find("[[ -e \"$destination\" || -L \"$destination\" ]]"));
    EXPECT_NE(std::string::npos, text.find("^[A-Za-z0-9._-]+$"));
    EXPECT_NE(std::string::npos, text.find("\"$model\" == \".\" || \"$model\" == \"..\""))
        << "dot paths canonicalise outside Models/<name> and must be rejected before Git runs";
    EXPECT_NE(std::string::npos, text.find("No model licence was reviewed by this fetch"));
    EXPECT_EQ(std::string::npos, text.find("rm -"))
        << "the fetcher must never erase an existing or partial checkout";
}

TEST(GltfFixtureCorpus, ViewerRetakeMatrixIsPinnedCompleteAndStrict)
{
    // GLTF-429 is an opt-in system retake, but its definition is permanent conformance state.
    // Check the matrix and protocol without a display, browser, network or third-party checkout;
    // the real harness separately proves that all of these cases pass.
    const std::filesystem::path script =
        CorpusDirectory().parent_path().parent_path().parent_path()
        / "scripts" / "gltf-viewer-retake.py";
    std::ifstream file(script, std::ios::binary);
    ASSERT_TRUE(file.is_open()) << script.string();
    const std::string text((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());

    EXPECT_NE(std::string::npos, text.find(
        "863b981fb755359063e370ff7b6e956bda0716e2"));
    EXPECT_NE(std::string::npos, text.find(
        "2bac6f8c57bf471df0d2a1e8a8ec023c7801dddf"));
    EXPECT_NE(std::string::npos, text.find("MINIMUM_MASK_IOU = 0.99"));
    EXPECT_NE(std::string::npos, text.find("MAXIMUM_RGB_MAE = 100.0"));
    EXPECT_NE(std::string::npos, text.find("twoProcessPngByteIdentical"));
    EXPECT_NE(std::string::npos, text.find("fixed animation equals bind/time-zero"));
    EXPECT_NE(std::string::npos, text.find("footprint[\"bytes\"] < 50 * 1024 * 1024"));
    EXPECT_NE(std::string::npos, text.find("rows != list(range(1, 15))"));
    EXPECT_NE(std::string::npos, text.find("len(results) != 15"));

    std::set<std::string> caseIds;
    std::set<int> rows;
    const std::regex declaration(
        R"(RetakeCase\(([0-9]+),\s*\"([a-z0-9-]+)\")");
    for (std::sregex_iterator it(text.begin(), text.end(), declaration), end; it != end; ++it)
    {
        rows.insert(std::stoi((*it)[1].str()));
        EXPECT_TRUE(caseIds.insert((*it)[2].str()).second)
            << "duplicate retake case " << (*it)[2].str();
    }
    EXPECT_EQ(15u, caseIds.size());
    EXPECT_EQ(14u, rows.size());
    for (int row = 1; row <= 14; ++row)
    {
        EXPECT_NE(rows.end(), rows.find(row)) << "Gate C row " << row << " is absent";
    }
    EXPECT_NE(caseIds.end(), caseIds.find("sparse-attribute"));
    EXPECT_NE(caseIds.end(), caseIds.find("sparse-index"));
}

TEST(GltfFixtureCorpus, VulkanMaterialL7ReportIsCompleteExactAndReproducible)
{
    // GLTF-244: keep the fast 14-fixture material subset independently reviewable even though the
    // later GLTF-385 campaign now gives Vulkan its own complete corpus oracle too.
    const std::filesystem::path repository =
        CorpusDirectory().parent_path().parent_path().parent_path();
    const std::filesystem::path reportPath =
        repository / "docs" / "gltf-l7-vulkan-materials-report.json";
    const std::vector<std::uint8_t> reportBytes = ReadAllBytes(reportPath);
    const std::string text(reportBytes.begin(), reportBytes.end());
    JsonValue report;
    ASSERT_NO_THROW(report = CNA::Internal::ParseJson(text));
    ASSERT_EQ(JsonType::Object, report.type);
    EXPECT_EQ("VULKAN", StringOr(report, "renderer", ""));
    EXPECT_NE(std::string::npos, StringOr(report, "scope", "").find("not a claim of whole-corpus"));
    EXPECT_EQ(0.0, NumberOr(Member(report, "goldenComparison"), "rgbTolerance", -1.0));
    EXPECT_EQ(0.0, NumberOr(Member(report, "goldenComparison"), "alphaTolerance", -1.0));

    std::set<std::string> expectedIds;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(CorpusDirectory()))
    {
        const std::string name = entry.path().filename().string();
        if (entry.is_regular_file() && name.starts_with("mat-") && name.ends_with(".gltf"))
        {
            expectedIds.insert(entry.path().stem().string());
        }
    }
    ASSERT_EQ(14u, expectedIds.size());

    const JsonValue& results = Member(report, "results");
    ASSERT_EQ(JsonType::Array, results.type);
    ASSERT_EQ(expectedIds.size(), results.arrayValue.size());
    std::set<std::string> seen;
    System::Security::Cryptography::SHA256 sha;
    for (const JsonValue& result : results.arrayValue)
    {
        const std::string id = StringOr(result, "id", "");
        ASSERT_NE(expectedIds.end(), expectedIds.find(id)) << "unknown material result " << id;
        EXPECT_TRUE(seen.insert(id).second) << "duplicate material result " << id;
        EXPECT_TRUE(BoolOr(result, "twoProcessPngByteIdentical", false)) << id;
        EXPECT_GT(NumberOr(result, "nonClearPixelCount", 0.0), 0.0) << id;

        const std::filesystem::path source = repository / StringOr(result, "source", "");
        const std::filesystem::path golden = repository / StringOr(result, "golden", "");
        ASSERT_TRUE(std::filesystem::is_regular_file(source)) << source.string();
        ASSERT_TRUE(std::filesystem::is_regular_file(golden)) << golden.string();
        EXPECT_EQ(StringOr(result, "sourceSha256", ""),
                  HexDigest(sha.ComputeHash(ReadAllBytes(source)))) << id;
        const std::string goldenDigest = HexDigest(sha.ComputeHash(ReadAllBytes(golden)));
        EXPECT_EQ(StringOr(result, "goldenPngSha256", ""), goldenDigest) << id;
        EXPECT_EQ(StringOr(result, "twoProcessPngSha256", ""), goldenDigest) << id;
    }
    EXPECT_EQ(expectedIds, seen);

    const std::filesystem::path script = repository / "scripts" / "gltf-l7-vulkan-materials.py";
    const std::vector<std::uint8_t> scriptBytes = ReadAllBytes(script);
    const std::string scriptText(scriptBytes.begin(), scriptBytes.end());
    EXPECT_NE(std::string::npos, scriptText.find("sorted(corpus.glob(\"mat-*.gltf\"))"));
    EXPECT_NE(std::string::npos, scriptText.find("if len(assets) != 14"));
    EXPECT_NE(std::string::npos, scriptText.find("require_equal(first, second"));
    EXPECT_NE(std::string::npos, scriptText.find("maximum != 0"));
    EXPECT_NE(std::string::npos, scriptText.find("[Vulkan] GPU:"));
}

TEST(GltfFixtureCorpus, VulkanCorpusL7ReportIsCompleteExactAndReproducible)
{
    // GLTF-385/390/391: verify the permanent evidence without requiring Vulkan, SDL or a display.
    // The system retake proves pixels; this integrity gate proves that its claimed 146 inputs,
    // exact 138 renderer-owned goldens, eight safe rejections and zero-delta policy are the bytes
    // actually present in this checkout.
    const std::filesystem::path repository =
        CorpusDirectory().parent_path().parent_path().parent_path();
    const std::filesystem::path policyPath =
        repository / "tests" / "gltf-l7" / "vulkan-policy.json";
    const std::filesystem::path reportPath =
        repository / "docs" / "gltf-l7-vulkan-corpus-report.json";

    JsonValue policy;
    const std::vector<std::uint8_t> policyBytes = ReadAllBytes(policyPath);
    ASSERT_NO_THROW(policy = CNA::Internal::ParseJson(
        std::string(policyBytes.begin(), policyBytes.end())));
    ASSERT_EQ(JsonType::Object, policy.type);
    EXPECT_EQ("VULKAN", StringOr(policy, "renderer", ""));
    EXPECT_EQ(0.0, NumberOr(Member(policy, "goldenComparison"), "rgbTolerance", -1.0));
    EXPECT_EQ(0.0, NumberOr(Member(policy, "goldenComparison"), "alphaTolerance", -1.0));
    const std::vector<std::string> tasks = Strings(Member(policy, "tasks"));
    EXPECT_NE(tasks.end(), std::find(tasks.begin(), tasks.end(), "GLTF-385"));
    EXPECT_NE(tasks.end(), std::find(tasks.begin(), tasks.end(), "GLTF-390"));
    EXPECT_NE(tasks.end(), std::find(tasks.begin(), tasks.end(), "GLTF-391"));

    JsonValue report;
    const std::vector<std::uint8_t> reportBytes = ReadAllBytes(reportPath);
    ASSERT_NO_THROW(report = CNA::Internal::ParseJson(
        std::string(reportBytes.begin(), reportBytes.end())));
    ASSERT_EQ(JsonType::Object, report.type);
    EXPECT_EQ("VULKAN", StringOr(report, "renderer", ""));
    EXPECT_EQ(148.0, NumberOr(report, "distinctAssetCount", -1.0));
    EXPECT_EQ(140.0, NumberOr(report, "capturedAssetCount", -1.0));
    EXPECT_EQ(8.0, NumberOr(report, "rejectedAssetCount", -1.0));
    EXPECT_EQ(2.0, NumberOr(Member(report, "capture"), "processesPerAsset", -1.0));
    EXPECT_EQ(0.0, NumberOr(Member(report, "goldenComparison"), "rgbTolerance", -1.0));
    EXPECT_EQ(0.0, NumberOr(Member(report, "goldenComparison"), "alphaTolerance", -1.0));
    EXPECT_FALSE(StringOr(Member(Member(report, "capture"), "environment"),
                          "VK_DRIVER_FILES", "").empty());
    const JsonValue& rendererEnvironment = Member(report, "rendererEnvironment");
    ASSERT_EQ(JsonType::Array, rendererEnvironment.type);
    ASSERT_EQ(1u, rendererEnvironment.arrayValue.size());
    EXPECT_NE(std::string::npos, rendererEnvironment.arrayValue.front().stringValue.find("llvmpipe"));
    const JsonValue& activeDivergences =
        Member(Member(report, "classification"), "activeGoldenDivergences");
    ASSERT_EQ(JsonType::Array, activeDivergences.type);
    EXPECT_TRUE(activeDivergences.arrayValue.empty());

    const std::vector<std::string> fixtureIds = CorpusFixtureIds();
    ASSERT_EQ(148u, fixtureIds.size());
    const std::set<std::string> expectedIds(fixtureIds.begin(), fixtureIds.end());
    const JsonValue& assets = Member(report, "assets");
    ASSERT_EQ(JsonType::Array, assets.type);
    ASSERT_EQ(expectedIds.size(), assets.arrayValue.size());

    std::set<std::string> seen;
    std::set<std::string> capturedIds;
    std::size_t rejected = 0;
    System::Security::Cryptography::SHA256 sha;
    for (const JsonValue& result : assets.arrayValue)
    {
        const std::string id = StringOr(result, "id", "");
        ASSERT_NE(expectedIds.end(), expectedIds.find(id)) << "unknown Vulkan L7 asset " << id;
        EXPECT_TRUE(seen.insert(id).second) << "duplicate Vulkan L7 result " << id;

        const std::filesystem::path source =
            CorpusDirectory() / StringOr(result, "source", "");
        ASSERT_TRUE(std::filesystem::is_regular_file(source)) << source.string();
        EXPECT_EQ(StringOr(result, "sourceSha256", ""),
                  HexDigest(sha.ComputeHash(ReadAllBytes(source)))) << id;

        const std::string disposition = StringOr(result, "disposition", "");
        if (disposition == "capture")
        {
            capturedIds.insert(id);
            const std::filesystem::path golden =
                repository / StringOr(result, "golden", "");
            ASSERT_TRUE(std::filesystem::is_regular_file(golden)) << golden.string();
            const std::string digest = HexDigest(sha.ComputeHash(ReadAllBytes(golden)));
            EXPECT_EQ(StringOr(result, "goldenPngSha256", ""), digest) << id;
            EXPECT_EQ(StringOr(result, "twoProcessPngSha256", ""), digest) << id;
            EXPECT_TRUE(BoolOr(result, "twoProcessPngByteIdentical", false)) << id;
            EXPECT_EQ(0.0, NumberOr(Member(result, "comparison"), "maximumRgbDelta", -1.0)) << id;
            EXPECT_EQ(0.0, NumberOr(Member(result, "comparison"), "maximumAlphaDelta", -1.0)) << id;
            EXPECT_TRUE(NumberOr(result, "nonClearPixelCount", 0.0) > 0.0
                        || BoolOr(result, "emptyForegroundAllowed", false)) << id;
        }
        else if (disposition == "reject")
        {
            ++rejected;
            EXPECT_TRUE(BoolOr(result, "twoProcessDispositionIdentical", false)) << id;
            EXPECT_FALSE(StringOr(result, "owningTask", "").empty()) << id;
            EXPECT_FALSE(StringOr(result, "errorContains", "").empty()) << id;
        }
        else
        {
            ADD_FAILURE() << id << ": unknown Vulkan L7 disposition " << disposition;
        }
    }
    EXPECT_EQ(expectedIds, seen);
    EXPECT_EQ(140u, capturedIds.size());
    EXPECT_EQ(8u, rejected);

    std::set<std::string> goldenIds;
    const std::filesystem::path goldenRoot = repository / "tests" / "gltf-l7" / "vulkan";
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(goldenRoot))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".png")
        {
            goldenIds.insert(entry.path().stem().string());
        }
    }
    EXPECT_EQ(capturedIds, goldenIds)
        << "the Vulkan L7 golden directory has a missing or stale PNG";

    const std::filesystem::path script = repository / "scripts" / "gltf-l7-corpus.py";
    const std::vector<std::uint8_t> scriptBytes = ReadAllBytes(script);
    const std::string scriptText(scriptBytes.begin(), scriptBytes.end());
    EXPECT_NE(std::string::npos, scriptText.find("tests/gltf-l7/vulkan"));
    EXPECT_NE(std::string::npos, scriptText.find("[Vulkan] GPU: "));
    EXPECT_NE(std::string::npos, scriptText.find("VK_DRIVER_FILES"));
    EXPECT_NE(std::string::npos, scriptText.find("if len(renderers) != 1"));
}

TEST(GltfFixtureCorpus, DirectX11CorpusL7ReportIsCompleteExactAndReproducible)
{
    // GLTF-386/390/391: the production-viewer retake proves D3D11 pixels through DXVK. This
    // ordinary no-display gate proves that the report still names every input and exact golden,
    // records the translation layer plus virtual desktop, and carries no active divergence.
    const std::filesystem::path repository =
        CorpusDirectory().parent_path().parent_path().parent_path();
    const std::filesystem::path policyPath =
        repository / "tests" / "gltf-l7" / "directx11-policy.json";
    const std::filesystem::path reportPath =
        repository / "docs" / "gltf-l7-directx11-report.json";

    JsonValue policy;
    const std::vector<std::uint8_t> policyBytes = ReadAllBytes(policyPath);
    ASSERT_NO_THROW(policy = CNA::Internal::ParseJson(
        std::string(policyBytes.begin(), policyBytes.end())));
    ASSERT_EQ(JsonType::Object, policy.type);
    EXPECT_EQ("DIRECTX11/DXVK", StringOr(policy, "renderer", ""));
    EXPECT_EQ(0.0, NumberOr(Member(policy, "goldenComparison"), "rgbTolerance", -1.0));
    EXPECT_EQ(0.0, NumberOr(Member(policy, "goldenComparison"), "alphaTolerance", -1.0));
    const std::vector<std::string> tasks = Strings(Member(policy, "tasks"));
    EXPECT_NE(tasks.end(), std::find(tasks.begin(), tasks.end(), "GLTF-386"));
    EXPECT_NE(tasks.end(), std::find(tasks.begin(), tasks.end(), "GLTF-390"));
    EXPECT_NE(tasks.end(), std::find(tasks.begin(), tasks.end(), "GLTF-391"));

    JsonValue report;
    const std::vector<std::uint8_t> reportBytes = ReadAllBytes(reportPath);
    ASSERT_NO_THROW(report = CNA::Internal::ParseJson(
        std::string(reportBytes.begin(), reportBytes.end())));
    ASSERT_EQ(JsonType::Object, report.type);
    EXPECT_EQ("DIRECTX11/DXVK", StringOr(report, "renderer", ""));
    EXPECT_EQ(148.0, NumberOr(report, "distinctAssetCount", -1.0));
    EXPECT_EQ(140.0, NumberOr(report, "capturedAssetCount", -1.0));
    EXPECT_EQ(8.0, NumberOr(report, "rejectedAssetCount", -1.0));
    EXPECT_EQ(2.0, NumberOr(Member(report, "capture"), "processesPerAsset", -1.0));
    EXPECT_EQ(0.0, NumberOr(Member(report, "goldenComparison"), "rgbTolerance", -1.0));
    EXPECT_EQ(0.0, NumberOr(Member(report, "goldenComparison"), "alphaTolerance", -1.0));
    EXPECT_EQ("Wine Z: drive", StringOr(Member(report, "capture"), "windowsPathConvention", ""));

    const JsonValue& environment = Member(Member(report, "capture"), "environment");
    EXPECT_EQ("dummy", StringOr(environment, "SDL_AUDIODRIVER", ""));
    EXPECT_EQ("info", StringOr(environment, "DXVK_LOG_LEVEL", ""));
    EXPECT_EQ("CNA,1280x1024", StringOr(environment, "CNA_D3D11_VIRTUAL_DESKTOP", ""));
    const JsonValue& rendererEnvironment = Member(report, "rendererEnvironment");
    ASSERT_EQ(JsonType::Array, rendererEnvironment.type);
    ASSERT_EQ(1u, rendererEnvironment.arrayValue.size());
    EXPECT_EQ("DIRECTX11", rendererEnvironment.arrayValue.front().stringValue);
    const JsonValue& translationEnvironment = Member(report, "translationLayerEnvironment");
    ASSERT_EQ(JsonType::Array, translationEnvironment.type);
    ASSERT_EQ(1u, translationEnvironment.arrayValue.size());
    // plan_gltf.md GLTF-471: the pin is on the RELEASE, and the two strings below are the two
    // spellings the same release logs. Upstream DXVK 2.6 prints "DXVK: v2.6"; Debian's `dxvk`
    // 2.6+ds-1 prints "DXVK: 2.6.0". The committed report was captured against the former and this
    // revision's re-capture against the latter, and both are DXVK 2.6 -- so accepting either keeps
    // the assertion's actual job (a pinned DXVK release handled the run, never a silent WineD3D
    // fallback, and exactly one layer version across both processes) while not failing on a distro
    // packaging difference. Adding a THIRD spelling here would mean a different release; do not.
    const std::string translationLayer = translationEnvironment.arrayValue.front().stringValue;
    EXPECT_TRUE(translationLayer == "DXVK v2.6" || translationLayer == "DXVK 2.6.0")
        << "unexpected translation-layer version '" << translationLayer
        << "': the DirectX11 L7 goldens are pinned to DXVK 2.6";
    const JsonValue& classification = Member(report, "classification");
    const JsonValue& activeDivergences = Member(classification, "activeGoldenDivergences");
    ASSERT_EQ(JsonType::Array, activeDivergences.type);
    EXPECT_TRUE(activeDivergences.arrayValue.empty());
    const JsonValue& resolvedDivergences = Member(classification, "resolvedDivergences");
    ASSERT_EQ(JsonType::Array, resolvedDivergences.type);
    EXPECT_EQ(2u, resolvedDivergences.arrayValue.size());

    System::Security::Cryptography::SHA256 sha;
    EXPECT_EQ(StringOr(report, "corpusManifestSha256", ""),
              HexDigest(sha.ComputeHash(ReadAllBytes(CorpusDirectory() / "manifest.json"))));
    EXPECT_EQ(StringOr(report, "viewerRunnerSha256", ""),
              HexDigest(sha.ComputeHash(ReadAllBytes(repository / "scripts" / "run-wine-dxvk.sh"))));
    const std::vector<std::string> fixtureIds = CorpusFixtureIds();
    ASSERT_EQ(148u, fixtureIds.size());
    const std::set<std::string> expectedIds(fixtureIds.begin(), fixtureIds.end());
    const JsonValue& assets = Member(report, "assets");
    ASSERT_EQ(JsonType::Array, assets.type);
    ASSERT_EQ(expectedIds.size(), assets.arrayValue.size());

    std::set<std::string> seen;
    std::set<std::string> capturedIds;
    std::size_t rejected = 0;
    for (const JsonValue& result : assets.arrayValue)
    {
        const std::string id = StringOr(result, "id", "");
        ASSERT_NE(expectedIds.end(), expectedIds.find(id)) << "unknown DirectX11 L7 asset " << id;
        EXPECT_TRUE(seen.insert(id).second) << "duplicate DirectX11 L7 result " << id;

        const std::filesystem::path source =
            CorpusDirectory() / StringOr(result, "source", "");
        ASSERT_TRUE(std::filesystem::is_regular_file(source)) << source.string();
        EXPECT_EQ(StringOr(result, "sourceSha256", ""),
                  HexDigest(sha.ComputeHash(ReadAllBytes(source)))) << id;

        const std::string disposition = StringOr(result, "disposition", "");
        if (disposition == "capture")
        {
            capturedIds.insert(id);
            const std::filesystem::path golden =
                repository / StringOr(result, "golden", "");
            ASSERT_TRUE(std::filesystem::is_regular_file(golden)) << golden.string();
            const std::string digest = HexDigest(sha.ComputeHash(ReadAllBytes(golden)));
            EXPECT_EQ(StringOr(result, "goldenPngSha256", ""), digest) << id;
            EXPECT_EQ(StringOr(result, "twoProcessPngSha256", ""), digest) << id;
            EXPECT_TRUE(BoolOr(result, "twoProcessPngByteIdentical", false)) << id;
            EXPECT_EQ(0.0, NumberOr(Member(result, "comparison"), "maximumRgbDelta", -1.0)) << id;
            EXPECT_EQ(0.0, NumberOr(Member(result, "comparison"), "maximumAlphaDelta", -1.0)) << id;
            EXPECT_TRUE(NumberOr(result, "nonClearPixelCount", 0.0) > 0.0
                        || BoolOr(result, "emptyForegroundAllowed", false)) << id;
        }
        else if (disposition == "reject")
        {
            ++rejected;
            EXPECT_TRUE(BoolOr(result, "twoProcessDispositionIdentical", false)) << id;
            EXPECT_FALSE(StringOr(result, "owningTask", "").empty()) << id;
            EXPECT_FALSE(StringOr(result, "errorContains", "").empty()) << id;
        }
        else
        {
            ADD_FAILURE() << id << ": unknown DirectX11 L7 disposition " << disposition;
        }
    }
    EXPECT_EQ(expectedIds, seen);
    EXPECT_EQ(140u, capturedIds.size());
    EXPECT_EQ(8u, rejected);

    std::set<std::string> goldenIds;
    const std::filesystem::path goldenRoot = repository / "tests" / "gltf-l7" / "directx11";
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(goldenRoot))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".png")
            goldenIds.insert(entry.path().stem().string());
    }
    EXPECT_EQ(capturedIds, goldenIds)
        << "the DirectX11 L7 golden directory has a missing or stale PNG";

    const std::filesystem::path script = repository / "scripts" / "gltf-l7-corpus.py";
    const std::vector<std::uint8_t> scriptBytes = ReadAllBytes(script);
    const std::string scriptText(scriptBytes.begin(), scriptBytes.end());
    EXPECT_NE(std::string::npos, scriptText.find("tests/gltf-l7/directx11"));
    EXPECT_NE(std::string::npos, scriptText.find("CNA_D3D11_VIRTUAL_DESKTOP"));
    EXPECT_NE(std::string::npos, scriptText.find("DXVK_LOG_LEVEL"));
    EXPECT_NE(std::string::npos, scriptText.find("if windows_paths and len(translation_layers) != 1"));
}

TEST(GltfFixtureCorpus, SoftwareCorpusL7ReportIsCompleteExactAndReproducible)
{
    // GLTF-387/390/391: the production-viewer retake proves CPU-rendered pixels. This ordinary
    // no-display integrity test proves that the report still names every canonical input, the
    // exact 138 SOFTWARE-owned goldens, all eight safe rejections and a zero-delta comparison.
    const std::filesystem::path repository =
        CorpusDirectory().parent_path().parent_path().parent_path();
    const std::filesystem::path policyPath =
        repository / "tests" / "gltf-l7" / "software-policy.json";
    const std::filesystem::path reportPath =
        repository / "docs" / "gltf-l7-software-corpus-report.json";

    JsonValue policy;
    const std::vector<std::uint8_t> policyBytes = ReadAllBytes(policyPath);
    ASSERT_NO_THROW(policy = CNA::Internal::ParseJson(
        std::string(policyBytes.begin(), policyBytes.end())));
    ASSERT_EQ(JsonType::Object, policy.type);
    EXPECT_EQ("SOFTWARE", StringOr(policy, "renderer", ""));
    EXPECT_EQ(0.0, NumberOr(Member(policy, "goldenComparison"), "rgbTolerance", -1.0));
    EXPECT_EQ(0.0, NumberOr(Member(policy, "goldenComparison"), "alphaTolerance", -1.0));
    const std::vector<std::string> tasks = Strings(Member(policy, "tasks"));
    EXPECT_NE(tasks.end(), std::find(tasks.begin(), tasks.end(), "GLTF-387"));
    EXPECT_NE(tasks.end(), std::find(tasks.begin(), tasks.end(), "GLTF-390"));
    EXPECT_NE(tasks.end(), std::find(tasks.begin(), tasks.end(), "GLTF-391"));

    JsonValue report;
    const std::vector<std::uint8_t> reportBytes = ReadAllBytes(reportPath);
    ASSERT_NO_THROW(report = CNA::Internal::ParseJson(
        std::string(reportBytes.begin(), reportBytes.end())));
    ASSERT_EQ(JsonType::Object, report.type);
    EXPECT_EQ("SOFTWARE", StringOr(report, "renderer", ""));
    EXPECT_EQ(148.0, NumberOr(report, "distinctAssetCount", -1.0));
    EXPECT_EQ(140.0, NumberOr(report, "capturedAssetCount", -1.0));
    EXPECT_EQ(8.0, NumberOr(report, "rejectedAssetCount", -1.0));
    EXPECT_EQ(2.0, NumberOr(Member(report, "capture"), "processesPerAsset", -1.0));
    EXPECT_EQ(0.0, NumberOr(Member(report, "goldenComparison"), "rgbTolerance", -1.0));
    EXPECT_EQ(0.0, NumberOr(Member(report, "goldenComparison"), "alphaTolerance", -1.0));
    const JsonValue& environment = Member(Member(report, "capture"), "environment");
    EXPECT_EQ("x11", StringOr(environment, "SDL_VIDEODRIVER", ""));
    EXPECT_EQ("dummy", StringOr(environment, "SDL_AUDIODRIVER", ""));
    const JsonValue& rendererEnvironment = Member(report, "rendererEnvironment");
    ASSERT_EQ(JsonType::Array, rendererEnvironment.type);
    ASSERT_EQ(1u, rendererEnvironment.arrayValue.size());
    EXPECT_EQ("SOFTWARE", rendererEnvironment.arrayValue.front().stringValue);
    const JsonValue& activeDivergences =
        Member(Member(report, "classification"), "activeGoldenDivergences");
    ASSERT_EQ(JsonType::Array, activeDivergences.type);
    EXPECT_TRUE(activeDivergences.arrayValue.empty());

    System::Security::Cryptography::SHA256 sha;
    EXPECT_EQ(StringOr(report, "corpusManifestSha256", ""),
              HexDigest(sha.ComputeHash(ReadAllBytes(CorpusDirectory() / "manifest.json"))));
    const std::vector<std::string> fixtureIds = CorpusFixtureIds();
    ASSERT_EQ(148u, fixtureIds.size());
    const std::set<std::string> expectedIds(fixtureIds.begin(), fixtureIds.end());
    const JsonValue& assets = Member(report, "assets");
    ASSERT_EQ(JsonType::Array, assets.type);
    ASSERT_EQ(expectedIds.size(), assets.arrayValue.size());

    std::set<std::string> seen;
    std::set<std::string> capturedIds;
    std::size_t rejected = 0;
    for (const JsonValue& result : assets.arrayValue)
    {
        const std::string id = StringOr(result, "id", "");
        ASSERT_NE(expectedIds.end(), expectedIds.find(id)) << "unknown SOFTWARE L7 asset " << id;
        EXPECT_TRUE(seen.insert(id).second) << "duplicate SOFTWARE L7 result " << id;

        const std::filesystem::path source =
            CorpusDirectory() / StringOr(result, "source", "");
        ASSERT_TRUE(std::filesystem::is_regular_file(source)) << source.string();
        EXPECT_EQ(StringOr(result, "sourceSha256", ""),
                  HexDigest(sha.ComputeHash(ReadAllBytes(source)))) << id;

        const std::string disposition = StringOr(result, "disposition", "");
        if (disposition == "capture")
        {
            capturedIds.insert(id);
            const std::filesystem::path golden =
                repository / StringOr(result, "golden", "");
            ASSERT_TRUE(std::filesystem::is_regular_file(golden)) << golden.string();
            const std::string digest = HexDigest(sha.ComputeHash(ReadAllBytes(golden)));
            EXPECT_EQ(StringOr(result, "goldenPngSha256", ""), digest) << id;
            EXPECT_EQ(StringOr(result, "twoProcessPngSha256", ""), digest) << id;
            EXPECT_TRUE(BoolOr(result, "twoProcessPngByteIdentical", false)) << id;
            EXPECT_EQ(0.0, NumberOr(Member(result, "comparison"), "maximumRgbDelta", -1.0)) << id;
            EXPECT_EQ(0.0, NumberOr(Member(result, "comparison"), "maximumAlphaDelta", -1.0)) << id;
            EXPECT_TRUE(NumberOr(result, "nonClearPixelCount", 0.0) > 0.0
                        || BoolOr(result, "emptyForegroundAllowed", false)) << id;
        }
        else if (disposition == "reject")
        {
            ++rejected;
            EXPECT_TRUE(BoolOr(result, "twoProcessDispositionIdentical", false)) << id;
            EXPECT_FALSE(StringOr(result, "owningTask", "").empty()) << id;
            EXPECT_FALSE(StringOr(result, "errorContains", "").empty()) << id;
        }
        else
        {
            ADD_FAILURE() << id << ": unknown SOFTWARE L7 disposition " << disposition;
        }
    }
    EXPECT_EQ(expectedIds, seen);
    EXPECT_EQ(140u, capturedIds.size());
    EXPECT_EQ(8u, rejected);

    std::set<std::string> goldenIds;
    const std::filesystem::path goldenRoot = repository / "tests" / "gltf-l7" / "software";
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(goldenRoot))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".png")
            goldenIds.insert(entry.path().stem().string());
    }
    EXPECT_EQ(capturedIds, goldenIds)
        << "the SOFTWARE L7 golden directory has a missing or stale PNG";

    const std::filesystem::path script = repository / "scripts" / "gltf-l7-corpus.py";
    const std::vector<std::uint8_t> scriptBytes = ReadAllBytes(script);
    const std::string scriptText(scriptBytes.begin(), scriptBytes.end());
    EXPECT_NE(std::string::npos, scriptText.find("tests/gltf-l7/software"));
    EXPECT_NE(std::string::npos, scriptText.find("CNA: graphics renderer: "));
    EXPECT_NE(std::string::npos, scriptText.find("SDL_VIDEODRIVER"));
    EXPECT_NE(std::string::npos, scriptText.find("if len(renderers) != 1"));
}

TEST(GltfFixtureCorpus, EveryL7GoldenCarriesTheVertexColourAlphaProductRatherThanTheWhiteIdentity)
{
    // plan_gltf.md GLTF-465. §3.9.2/§3.7.2.1: COLOR_0 is "an additional linear multiplier to base
    // color" -- ITS ALPHA INCLUDED. Every other check in this suite proves the colour reaches the
    // renderer; this one proves the renderer USED it, from the committed pixels, with no display and
    // no shader-source string matching.
    //
    // It works because base-colour ALPHA is the one part of the product with no view dependence at
    // all: baseColorTexture.a x baseColorFactor.a x COLOR_0.a, interpolated across the triangle.
    // Specular highlights, normal mapping and lighting all vary spatially and could mask an RGB
    // regression, but alpha cannot -- so a BLEND-mode PBR primitive WITHOUT a COLOR_0 must capture
    // ONE alpha value everywhere, and the same primitive WITH one must capture a spread that matches
    // the authored per-vertex alphas.
    //
    // `mat-factor-only-gold` is the control: PBR, alphaMode BLEND, baseColorFactor alpha 0.5, no
    // COLOR_0. `skin-vertex-color-pbr` is GLTF-463's witness: baseColorFactor alpha 0.6 and authored
    // COLOR_0 alphas 255/191/128. The rig's own alpha composite is CALIBRATED from the control rather
    // than assumed, so a legitimate change to how the viewer composites (one draw instead of two, a
    // different clear) moves both readings together and this test keeps its meaning; what it can
    // never absorb is the colour being dropped, because that collapses the spread to a single value.
    const std::filesystem::path repository =
        CorpusDirectory().parent_path().parent_path().parent_path();

    struct GoldenPolicy
    {
        const char* directory;
        const char* renderer;
    };
    constexpr std::array<GoldenPolicy, 4> policies{{
        {"easygl", "OPENGLES3/EasyGL"},
        {"vulkan", "VULKAN"},
        {"software", "SOFTWARE"},
        {"directx11", "DIRECTX11/DXVK"},
    }};

    // The authored numbers, from tools/gltf_fixtures/defs/skinning.py and the mat-* material set.
    constexpr double kControlFactorAlpha = 0.5;
    constexpr double kWitnessFactorAlpha = 0.6;
    constexpr double kWitnessLowestVertexAlpha = 128.0 / 255.0;
    constexpr double kWitnessHighestVertexAlpha = 255.0 / 255.0;

    for (const GoldenPolicy& policy : policies)
    {
        SCOPED_TRACE(policy.renderer);
        const std::filesystem::path goldens = repository / "tests" / "gltf-l7" / policy.directory;
        const std::filesystem::path controlPath = goldens / "mat-factor-only-gold.png";
        const std::filesystem::path witnessPath = goldens / "skin-vertex-color-pbr.png";
        ASSERT_TRUE(std::filesystem::is_regular_file(controlPath)) << controlPath;
        ASSERT_TRUE(std::filesystem::is_regular_file(witnessPath)) << witnessPath;

        const auto foregroundAlphas = [](const std::filesystem::path& path) {
            const CNA::Internal::Graphics::ImageData image =
                CNA::Internal::Graphics::ImageLoader::Load(path.string());
            std::vector<int> alphas;
            std::vector<int> reds;
            for (std::size_t at = 0; at + 3 < image.pixels.size(); at += 4)
            {
                if (image.pixels[at + 3] == 0) { continue; }
                alphas.push_back(static_cast<int>(image.pixels[at + 3]));
                reds.push_back(static_cast<int>(image.pixels[at]));
            }
            return std::pair<std::vector<int>, std::vector<int>>(std::move(alphas), std::move(reds));
        };

        const auto [controlAlphas, controlReds] = foregroundAlphas(controlPath);
        const auto [witnessAlphas, witnessReds] = foregroundAlphas(witnessPath);
        ASSERT_FALSE(controlAlphas.empty()) << "the control capture has no foreground at all";
        ASSERT_FALSE(witnessAlphas.empty()) << "the witness capture has no foreground at all";

        const std::set<int> controlDistinct(controlAlphas.begin(), controlAlphas.end());
        EXPECT_EQ(1u, controlDistinct.size())
            << "a BLEND-mode PBR primitive with no COLOR_0 must have exactly one alpha everywhere; "
               "more than one means something other than the base colour product is moving it";

        const int witnessLow = *std::min_element(witnessAlphas.begin(), witnessAlphas.end());
        const int witnessHigh = *std::max_element(witnessAlphas.begin(), witnessAlphas.end());
        const std::set<int> witnessDistinct(witnessAlphas.begin(), witnessAlphas.end());
        EXPECT_GT(witnessDistinct.size(), 8u)
            << "the vertex-coloured capture has a nearly flat alpha, which is what substituting the "
               "opaque-white identity for COLOR_0 looks like";

        // Calibrate the rig's own alpha composite from the control: it maps an authored alpha to a
        // captured one, and one sample plus the assumption that it is a fixed power is enough.
        const double controlCaptured = static_cast<double>(*controlDistinct.begin()) / 255.0;
        ASSERT_GT(controlCaptured, 0.0);
        const double exponent = std::log(controlCaptured) / std::log(kControlFactorAlpha);
        const auto composite = [exponent](double authored) {
            return std::pow(authored, exponent) * 255.0;
        };
        const double expectedLow = composite(kWitnessFactorAlpha * kWitnessLowestVertexAlpha);
        const double expectedHigh = composite(kWitnessFactorAlpha * kWitnessHighestVertexAlpha);

        // Two units of slack for the renderer's own rounding of an 8-bit channel; the values being
        // separated by roughly a factor of four is the whole point, so this cannot pass by accident.
        EXPECT_NEAR(expectedLow, static_cast<double>(witnessLow), 2.0)
            << "the darkest captured alpha does not match baseColorFactor.a x the lowest COLOR_0.a";
        EXPECT_NEAR(expectedHigh, static_cast<double>(witnessHigh), 2.0)
            << "the brightest captured alpha does not match baseColorFactor.a x the highest COLOR_0.a";

        // And the RGB product moved too: with the identity substituted, the only remaining spatial
        // variation on this flat, flat-normal-mapped triangle would be the specular term.
        const int redLow = *std::min_element(witnessReds.begin(), witnessReds.end());
        const int redHigh = *std::max_element(witnessReds.begin(), witnessReds.end());
        EXPECT_GT(redHigh - redLow, 5)
            << "the vertex-coloured capture's base colour is spatially flat";

        // The RIGID path needs its own witness, because `skin-vertex-color-pbr` proves stride 80 and
        // says nothing about stride 60. `mat-vertex-color-pbr` is opaque, so alpha cannot carry it --
        // but its three `COLOR_0` values are pure red, green and blue, and its emissive factor
        // (0.05, 0, 0.2) has NO green in it. So at its red corner every green contribution to the
        // surface is zero except the specular term, and the capture must actually contain a green
        // channel of 0. Under the opaque-white identity the albedo's green would be
        // `baseColorFactor.g` = 0.4 everywhere, lit and never zero -- the one thing this cannot be
        // confused with. The upper bound then proves the channel is genuinely present elsewhere
        // rather than the whole surface being black.
        const std::filesystem::path rigidPath = goldens / "mat-vertex-color-pbr.png";
        ASSERT_TRUE(std::filesystem::is_regular_file(rigidPath)) << rigidPath;
        const CNA::Internal::Graphics::ImageData rigid =
            CNA::Internal::Graphics::ImageLoader::Load(rigidPath.string());
        int greenLow = 256;
        int greenHigh = -1;
        std::size_t rigidForeground = 0;
        for (std::size_t at = 0; at + 3 < rigid.pixels.size(); at += 4)
        {
            if (rigid.pixels[at + 3] == 0) { continue; }
            ++rigidForeground;
            const int green = static_cast<int>(rigid.pixels[at + 1]);
            greenLow = std::min(greenLow, green);
            greenHigh = std::max(greenHigh, green);
        }
        ASSERT_GT(rigidForeground, 0u) << "the rigid vertex-colour capture has no foreground";
        EXPECT_EQ(0, greenLow)
            << "no pixel has a zero green channel, so COLOR_0's own green is not multiplying the "
               "base colour on the rigid stride-60 path";
        EXPECT_GT(greenHigh, 30)
            << "the whole rigid capture is green-free, which is a black surface rather than a product";
    }
}

TEST(GltfFixtureCorpus, KhronosValidatorPinIsImmutableAndNotARuntimeDependency)
{
    // GLTF-015: the CI tool is external, but which tool CI trusts is checkout state. Pin the exact
    // official release archive and digest here without downloading or executing anything.
    const std::filesystem::path pin =
        CorpusDirectory().parent_path().parent_path().parent_path()
        / "tools" / "gltf_fixtures" / "validator-pin.json";
    std::ifstream file(pin, std::ios::binary);
    ASSERT_TRUE(file.is_open()) << pin.string();
    const std::string text((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    JsonValue document;
    ASSERT_NO_THROW(document = CNA::Internal::ParseJson(text));
    ASSERT_EQ(JsonType::Object, document.type);
    EXPECT_EQ("GLTF-015", StringOr(document, "task", ""));
    EXPECT_EQ("https://github.com/KhronosGroup/glTF-Validator",
              StringOr(document, "repository", ""));
    EXPECT_EQ("2.0.0-dev.3.10", StringOr(document, "releaseVersion", ""));
    EXPECT_FALSE(BoolOr(document, "runtimeDependency", true));
    EXPECT_TRUE(BoolOr(document, "ciDependency", false));
    EXPECT_EQ("Apache-2.0", StringOr(Member(document, "license"), "spdx", ""));

    const JsonValue& artifact = Member(document, "ciArtifact");
    ASSERT_EQ(JsonType::Object, artifact.type);
    EXPECT_EQ("linux-x86_64", StringOr(artifact, "platform", ""));
    EXPECT_EQ("gltf_validator", StringOr(artifact, "executable", ""));
    EXPECT_EQ("168eba887964125abe17ae97899b38d0b3cfd73c266c78424c194929ddcbc522",
              StringOr(artifact, "sha256", ""));
    EXPECT_EQ("https://github.com/KhronosGroup/glTF-Validator/releases/download/"
              "2.0.0-dev.3.10/gltf_validator-2.0.0-dev.3.10-linux64.tar.xz",
              StringOr(artifact, "url", ""));
}

TEST(GltfFixtureCorpus, ValidatorErrorsAreExactNamedMalformedFixtureOracles)
{
    // An empty list is implicit and means zero errors. A non-empty list is not a blanket skip:
    // both .gltf and .glb must produce exactly these distinct severity-0 codes in CI.
    std::size_t exceptions = 0;
    for (const std::string& id : CorpusFixtureIds())
    {
        SCOPED_TRACE(id);
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        const JsonValue& inventory = Member(fixture.Expected(), "inventory");
        const std::vector<std::string> codes =
            Strings(Member(inventory, "validatorExpectedErrorCodes"));
        if (codes.empty())
        {
            EXPECT_TRUE(StringOr(inventory, "validatorExceptionReason", "").empty());
            EXPECT_NE(0u, id.rfind("bad-", 0))
                << "a bad-* fixture must state which Validator error proves its malformedness";
            continue;
        }
        ++exceptions;
        EXPECT_FALSE(StringOr(inventory, "validatorExceptionReason", "").empty());
        const std::set<std::string> unique(codes.begin(), codes.end());
        EXPECT_EQ(codes.size(), unique.size()) << "duplicate Validator error code";
        for (const std::string& code : codes)
        {
            EXPECT_FALSE(code.empty());
            EXPECT_TRUE(std::all_of(code.begin(), code.end(), [](const char character) {
                return (character >= 'A' && character <= 'Z') || character == '_'
                       || (character >= '0' && character <= '9');
            })) << "invalid Validator error code " << code;
        }
    }
    EXPECT_EQ(10u, exceptions);
}

TEST(GltfFixtureCorpus, DistinctAssetCountEqualsTheSumOfOwningGroupCounts)
{
    // plan_gltf.md §24.1: one asset has exactly one owning group, so the distinct-asset total is
    // the sum of the owning-group counts. Referencing a fixture from another group never
    // re-counts it.
    const JsonValue& manifest = CorpusManifest();
    const JsonValue& groups = Member(manifest, "owningGroupCounts");
    ASSERT_EQ(JsonType::Object, groups.type);

    double sum = 0.0;
    for (const auto& [name, count] : groups.objectValue)
    {
        ASSERT_EQ(JsonType::Number, count.type) << "owningGroupCounts." << name;
        sum += count.numberValue;
    }
    EXPECT_DOUBLE_EQ(NumberOr(manifest, "distinctAssetCount", -1), sum);
    EXPECT_EQ(CorpusFixtureIds().size(), static_cast<std::size_t>(sum));

    std::set<std::string> uniqueIds;
    for (const std::string& id : CorpusFixtureIds()) { uniqueIds.insert(id); }
    EXPECT_EQ(uniqueIds.size(), CorpusFixtureIds().size()) << "duplicate canonical fixture id";
}

TEST(GltfFixtureCorpus, FinalTargetAccountsForEveryGeneratedAndMissingAssetExactlyOnce)
{
    // GLTF-399 used three incompatible totals (135, 136 and 141) because later fixture additions
    // updated prose and arithmetic independently. The generator now owns one exact final inventory:
    // current assets plus named missing assets must equal its target, per group and globally.
    const JsonValue& manifest = CorpusManifest();
    const JsonValue& currentGroups = Member(manifest, "owningGroupCounts");
    const JsonValue& targetGroups = Member(manifest, "targetOwningGroupCounts");
    const JsonValue& missing = Member(manifest, "missingAssets");
    ASSERT_EQ(JsonType::Object, currentGroups.type);
    ASSERT_EQ(JsonType::Object, targetGroups.type);
    ASSERT_EQ(JsonType::Array, missing.type);

    std::map<std::string, std::size_t> missingByGroup;
    const std::vector<std::string> generatedIds = CorpusFixtureIds();
    std::set<std::string> accountedIds(generatedIds.begin(), generatedIds.end());
    ASSERT_EQ(accountedIds.size(), generatedIds.size());
    for (const JsonValue& asset : missing.arrayValue)
    {
        const std::string id = StringOr(asset, "id", "");
        const std::string group = StringOr(asset, "owningGroup", "");
        ASSERT_FALSE(id.empty()) << "missingAssets entry has no id";
        ASSERT_EQ(JsonType::Number, Member(targetGroups, group).type)
            << id << " names unknown target owning group '" << group << "'";
        EXPECT_TRUE(accountedIds.insert(id).second)
            << id << " is generated and missing, or appears twice in missingAssets";
        ++missingByGroup[group];
    }

    std::size_t targetSum = 0;
    std::size_t currentSum = 0;
    for (const auto& [group, targetValue] : targetGroups.objectValue)
    {
        ASSERT_EQ(JsonType::Number, targetValue.type) << group;
        const JsonValue& currentValue = Member(currentGroups, group);
        ASSERT_EQ(JsonType::Number, currentValue.type)
            << "target group '" << group << "' has no current count (zero must be explicit)";

        const auto target = static_cast<std::size_t>(targetValue.numberValue);
        const auto current = static_cast<std::size_t>(currentValue.numberValue);
        EXPECT_EQ(target, current + missingByGroup[group]) << group;
        targetSum += target;
        currentSum += current;
    }

    EXPECT_EQ(static_cast<std::size_t>(NumberOr(manifest, "targetDistinctAssetCount", -1)),
              targetSum);
    EXPECT_EQ(static_cast<std::size_t>(NumberOr(manifest, "distinctAssetCount", -1)), currentSum);
    EXPECT_EQ(targetSum, accountedIds.size());
    EXPECT_EQ(targetSum, currentSum + missing.arrayValue.size());

    // The human plan names the generated target rather than carrying a fourth independent number.
    const std::filesystem::path plan =
        CorpusDirectory().parent_path().parent_path().parent_path() / "plan_gltf.md";
    std::ifstream file(plan);
    ASSERT_TRUE(file.is_open()) << plan.string();
    const std::string text((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    const std::string heading = "### 24.2 Target corpus — " + std::to_string(targetSum) +
                                " distinct synthetic assets";
    EXPECT_NE(std::string::npos, text.find(heading))
        << "plan_gltf.md must use the generator's target: " << heading;
}

TEST(GltfFixtureCorpus, AllFourteenForensicAuditFixturesArePromoted)
{
    // plan_gltf.md §24.2: f1...f14 are promoted, not re-invented, and each maps to exactly one
    // canonical corpus id.
    const JsonValue& promoted = Member(CorpusManifest(), "promotedAuditFixtures");
    ASSERT_EQ(JsonType::Object, promoted.type);
    EXPECT_EQ(14u, promoted.objectValue.size());

    std::set<std::string> canonical;
    for (int i = 1; i <= 14; ++i)
    {
        const std::string key = "f" + std::to_string(i);
        const std::string id = StringOr(promoted, key, "");
        EXPECT_FALSE(id.empty()) << "audit fixture " << key << " has no canonical corpus id";
        EXPECT_TRUE(canonical.insert(id).second)
            << id << " is claimed by more than one audit fixture";
        const std::vector<std::string> ids = CorpusFixtureIds();
        EXPECT_NE(ids.end(), std::find(ids.begin(), ids.end(), id))
            << key << " maps to '" << id << "', which is not in the corpus";
    }
}

TEST(GltfFixtureCorpus, EveryFixtureParsesAndLoadsItsBuffers)
{
    for (const std::string& id : CorpusFixtureIds())
    {
        SCOPED_TRACE(id);
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    }
}

TEST(GltfFixtureCorpus, EveryGlbTwinIsAValidContainerCarryingTheSameAsset)
{
    // The generator emits each asset in both containers from one source of truth. This checks the
    // generator, not CNA's GLB support: a `.glb` whose header or chunk layout were wrong would be
    // committed silently otherwise. Container-level conformance itself is GLTF-026's, and
    // asserting the twins produce identical L3 output is GLTF-400's.
    for (const std::string& id : CorpusFixtureIds())
    {
        SCOPED_TRACE(id);
        const std::filesystem::path glb = CorpusDirectory() / (id + ".glb");
        ASSERT_TRUE(std::filesystem::exists(glb)) << "missing GLB twin";

        const std::vector<std::uint8_t> bytes = ReadAllBytes(glb);
        ASSERT_GE(bytes.size(), 12u) << "GLB shorter than its own header";
        EXPECT_EQ(0u, bytes.size() % 4) << "GLB length must stay 4-byte aligned";
        EXPECT_EQ('g', bytes[0]);
        EXPECT_EQ('l', bytes[1]);
        EXPECT_EQ('T', bytes[2]);
        EXPECT_EQ('F', bytes[3]);
        std::uint32_t version = 0;
        std::uint32_t declaredLength = 0;
        std::memcpy(&version, bytes.data() + 4, sizeof(version));
        std::memcpy(&declaredLength, bytes.data() + 8, sizeof(declaredLength));
        EXPECT_EQ(2u, version);
        EXPECT_EQ(bytes.size(), static_cast<std::size_t>(declaredLength))
            << "the GLB header's total length disagrees with the file size";

        cgltf_options options{};
        cgltf_data* data = nullptr;
        const std::string path = glb.string();
        ASSERT_EQ(cgltf_result_success, cgltf_parse_file(&options, path.c_str(), &data))
            << "the generated GLB does not parse";
        EXPECT_EQ(cgltf_result_success, cgltf_load_buffers(&options, data, path.c_str()))
            << "the generated GLB's BIN chunk does not load";

        // The same asset in another container, so the structural counts must match the .gltf.
        const LoadedFixture textForm(id);
        ASSERT_TRUE(textForm.Ok()) << textForm.Error();
        EXPECT_EQ(textForm.Data().meshes_count, data->meshes_count);
        EXPECT_EQ(textForm.Data().nodes_count, data->nodes_count);
        EXPECT_EQ(textForm.Data().accessors_count, data->accessors_count);
        ASSERT_EQ(1u, static_cast<std::size_t>(data->buffers_count));
        EXPECT_EQ(textForm.Data().buffers[0].size, data->buffers[0].size);
        cgltf_free(data);
    }
}

// plan_gltf.md GLTF-131: the transform ladder in `.glb` form too.
//
// The test above proves the two containers hold the same *counts*. This one asks the question the
// row is actually about: does CNA place the geometry identically out of either container? A GLB
// reaches the importer through a different path -- the JSON is a chunk rather than a file, and
// every buffer resolves to the BIN chunk instead of a URI -- and the world transform is composed
// from the JSON the same way in both cases only if that JSON really did survive the repackaging.
// Byte-for-byte equality is the right bar here: the two files describe the same numbers, so any
// difference at all is a container bug rather than an accumulated rounding difference.
TEST(GltfConformanceL4, TheGlbAndGltfTwinsPlaceTheSameGeometry)
{
    for (const std::string& id : CorpusFixtureIds())
    {
        SCOPED_TRACE(id);
        const LoadedFixture textForm(id);
        ASSERT_TRUE(textForm.Ok()) << textForm.Error();
        if (IsRejectionFixture(textForm.Expected())) { continue; }
        if (IsKnownDefectField(textForm.Expected(), "L4", "worldPositions")) { continue; }

        cgltf_options options{};
        cgltf_data* binaryForm = nullptr;
        const std::string glbPath = (CorpusDirectory() / (id + ".glb")).string();
        ASSERT_EQ(cgltf_result_success,
                  cgltf_parse_file(&options, glbPath.c_str(), &binaryForm));
        ASSERT_EQ(cgltf_result_success,
                  cgltf_load_buffers(&options, binaryForm, glbPath.c_str()));

        const WorldPositions fromText = EvaluateCnaWorldPositionsEXT(textForm.Data());
        const WorldPositions fromBinary = EvaluateCnaWorldPositionsEXT(*binaryForm);
        cgltf_free(binaryForm);

        ASSERT_EQ(fromText.instances.size(), fromBinary.instances.size())
            << "the two containers produced a different number of placements";
        for (std::size_t i = 0; i < fromText.instances.size(); ++i)
        {
            SCOPED_TRACE("instance " + std::to_string(i));
            EXPECT_EQ(fromText.instances[i].node, fromBinary.instances[i].node);
            EXPECT_EQ(fromText.instances[i].mesh, fromBinary.instances[i].mesh);
            EXPECT_EQ(fromText.instances[i].primitive, fromBinary.instances[i].primitive);
            EXPECT_EQ(fromText.instances[i].worldMatrix, fromBinary.instances[i].worldMatrix)
                << "the composed world transform differs between the .gltf and its .glb twin";
            ASSERT_EQ(fromText.instances[i].worldPositions.size(),
                      fromBinary.instances[i].worldPositions.size());
            EXPECT_EQ(fromText.instances[i].worldPositions,
                      fromBinary.instances[i].worldPositions)
                << "the placed vertices differ between the .gltf and its .glb twin";
        }
    }
}

// --- L1: container / parser structure --------------------------------------------------------

TEST(GltfConformanceL1, ContainerStructureMatchesTheManifest)
{
    for (const std::string& id : CorpusFixtureIds())
    {
        SCOPED_TRACE(id);
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        const cgltf_data& data = fixture.Data();
        const JsonValue& l1 = Member(fixture.Expected(), "l1");
        ASSERT_EQ(JsonType::Object, l1.type) << "fixture has no l1 expectation";

        EXPECT_EQ(std::string("2.0"), std::string(data.asset.version != nullptr
                                                  ? data.asset.version : ""));
        EXPECT_EQ(static_cast<std::size_t>(NumberOr(l1, "sceneCount", -1)),
                  static_cast<std::size_t>(data.scenes_count));
        EXPECT_EQ(static_cast<std::size_t>(NumberOr(l1, "nodeCount", -1)),
                  static_cast<std::size_t>(data.nodes_count));
        EXPECT_EQ(static_cast<std::size_t>(NumberOr(l1, "meshCount", -1)),
                  static_cast<std::size_t>(data.meshes_count));
        EXPECT_EQ(static_cast<std::size_t>(NumberOr(l1, "materialCount", -1)),
                  static_cast<std::size_t>(data.materials_count));
        EXPECT_EQ(static_cast<std::size_t>(NumberOr(l1, "skinCount", -1)),
                  static_cast<std::size_t>(data.skins_count));
        EXPECT_EQ(static_cast<std::size_t>(NumberOr(l1, "animationCount", -1)),
                  static_cast<std::size_t>(data.animations_count));
        EXPECT_EQ(static_cast<std::size_t>(NumberOr(l1, "accessorCount", -1)),
                  static_cast<std::size_t>(data.accessors_count));
        EXPECT_EQ(static_cast<std::size_t>(NumberOr(l1, "bufferViewCount", -1)),
                  static_cast<std::size_t>(data.buffer_views_count));
        EXPECT_EQ(static_cast<std::size_t>(NumberOr(l1, "bufferCount", -1)),
                  static_cast<std::size_t>(data.buffers_count));
        ASSERT_EQ(1u, static_cast<std::size_t>(data.buffers_count));
        EXPECT_EQ(static_cast<std::size_t>(NumberOr(l1, "bufferByteLength", -1)),
                  static_cast<std::size_t>(data.buffers[0].size));

        // The default scene is the one the importer must use; getting it wrong is how a decoy
        // mesh gets imported.
        // §3.5 permits a file with no `scenes` array at all, and `scene-no-scenes` is the fixture
        // for it -- so "-1" is a legitimate expectation here rather than a fixture that forgot to
        // state one. What must not happen is a file declaring a scene index and not having it.
        const long long expectedScene = static_cast<long long>(NumberOr(l1, "defaultScene", -1));
        if (expectedScene < 0)
        {
            EXPECT_EQ(nullptr, data.scene)
                << "the manifest states no default scene and the file has one";
            EXPECT_EQ(0u, data.scenes_count);
        }
        else
        {
            ASSERT_NE(nullptr, data.scene) << "file declares no default scene";
            EXPECT_EQ(expectedScene, static_cast<long long>(data.scene - data.scenes));
        }

        EXPECT_EQ(Strings(Member(l1, "extensionsRequired")).size(),
                  static_cast<std::size_t>(data.extensions_required_count));
        EXPECT_EQ(Strings(Member(l1, "extensionsUsed")).size(),
                  static_cast<std::size_t>(data.extensions_used_count));
    }
}

// --- L2: decoded accessor arrays --------------------------------------------------------------

TEST(GltfConformanceL2, DecodedAccessorsMatchTheManifest)
{
    for (const std::string& id : CorpusFixtureIds())
    {
        SCOPED_TRACE(id);
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        // A fixture the importer must refuse has no expectation at this layer at all; the refusal
        // is asserted in full by GltfContainerValidation.
        if (IsRejectionFixture(fixture.Expected())) { continue; }

        const JsonValue& accessors = Path(fixture.Expected(), "l2.accessors");
        ASSERT_EQ(JsonType::Array, accessors.type);
        ASSERT_EQ(accessors.arrayValue.size(), static_cast<std::size_t>(fixture.Data().accessors_count));

        for (const JsonValue& expected : accessors.arrayValue)
        {
            const auto index = static_cast<std::size_t>(NumberOr(expected, "index", -1));
            const std::string usage = StringOr(expected, "usage", "?");
            SCOPED_TRACE("accessor " + std::to_string(index) + " (" + usage + ")");

            const AccessorDump dump = DumpAccessorEXT(fixture.Data(), index);
            const std::string encodedWith = StringOr(expected, "encodedWith", "");
            if (encodedWith.empty())
            {
                ASSERT_TRUE(dump.decoded) << dump.error;
            }
            else
            {
                // KHR_draco_mesh_compression deliberately leaves its attribute accessors with no
                // bufferView: those objects carry count/type/bounds metadata while the values live
                // in the extension's own compressed bufferView. cgltf_accessor_unpack_floats
                // represents that absent ordinary base as a successful all-zero placeholder; it
                // does not and cannot expose the compressed values. Assert that exact source shape
                // here; the L3 sweep checks the real values after the pinned decoder has run.
                EXPECT_EQ("KHR_draco_mesh_compression", encodedWith);
                EXPECT_TRUE(dump.decoded) << dump.error;
                EXPECT_TRUE(dump.error.empty());
                EXPECT_TRUE(std::all_of(dump.values.begin(), dump.values.end(),
                                        [](float value) { return value == 0.0f; }))
                    << "an extension-backed accessor unexpectedly acquired ordinary values";
            }

            EXPECT_EQ(StringOr(expected, "type", ""), dump.type);
            EXPECT_EQ(static_cast<int>(NumberOr(expected, "componentType", -1)), dump.componentType);
            EXPECT_EQ(StringOr(expected, "componentTypeName", ""), dump.componentTypeName);
            EXPECT_EQ(static_cast<std::size_t>(NumberOr(expected, "count", -1)), dump.count);
            EXPECT_EQ(static_cast<std::size_t>(NumberOr(expected, "componentsPerElement", -1)),
                      dump.componentsPerElement);
            EXPECT_EQ(BoolOr(expected, "normalized", false), dump.normalized);
            EXPECT_EQ(BoolOr(expected, "sparse", false), dump.sparse);
            EXPECT_EQ(static_cast<std::size_t>(NumberOr(expected, "byteOffset", 0)), dump.byteOffset);

            // A null bufferView is not a defect: a sparse accessor may legitimately have none.
            const JsonValue& expectedView = Member(expected, "bufferView");
            EXPECT_EQ(expectedView.type == JsonType::Number
                          ? static_cast<int>(expectedView.numberValue) : -1,
                      dump.bufferView);
            const JsonValue& expectedViewOffset = Member(expected, "bufferViewByteOffset");
            EXPECT_EQ(expectedViewOffset.type == JsonType::Number
                          ? static_cast<long long>(expectedViewOffset.numberValue) : -1,
                      dump.bufferViewByteOffset);
            const JsonValue& expectedStride = Member(expected, "bufferViewByteStride");
            EXPECT_EQ(expectedStride.type == JsonType::Number
                          ? static_cast<long long>(expectedStride.numberValue) : -1,
                      dump.bufferViewByteStride);

            if (!encodedWith.empty())
            {
                EXPECT_EQ(-1, dump.bufferView);
                EXPECT_EQ(dump.count * dump.componentsPerElement, dump.values.size());
                EXPECT_EQ(JsonType::Array, Member(expected, "values").type)
                    << "the post-decompression L3 truth still has to be stated";
                continue;
            }
            ExpectComponents(Numbers(Member(expected, "values")), dump.values, "decoded values");
        }
    }
}

// --- L3: semantic mesh -------------------------------------------------------------------------

TEST(GltfConformanceL3, SemanticMeshStreamsMatchTheManifest)
{
    for (const std::string& id : CorpusFixtureIds())
    {
        SCOPED_TRACE(id);
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        // A fixture the importer must refuse has no expectation at this layer at all; the refusal
        // is asserted in full by GltfContainerValidation.
        if (IsRejectionFixture(fixture.Expected()) ||
            RequiresUnavailableDraco(fixture.Expected())) { continue; }

        const std::vector<ExtractedPrimitive> extracted = ExtractSceneMeshesEXT(fixture.Data());
        const JsonValue& primitives = ExpectedPrimitives(fixture);
        ASSERT_EQ(JsonType::Array, primitives.type);

        for (const JsonValue& expected : primitives.arrayValue)
        {
            const int mesh = static_cast<int>(NumberOr(expected, "mesh", -1));
            const int primitive = static_cast<int>(NumberOr(expected, "primitive", -1));
            SCOPED_TRACE("mesh " + std::to_string(mesh) + " primitive " + std::to_string(primitive));

            const ExtractedPrimitive* actual = FindExtracted(extracted, mesh, primitive);
            ASSERT_NE(nullptr, actual) << "the import path produced no such primitive";

            // A defect that stops this primitive being imported at all -- today, a topology CNA
            // classifies and rejects rather than reinterpreting (D5) -- leaves no semantic mesh to
            // compare field by field. The rejection itself is asserted by the GltfKnownDefect
            // suite, including that it names the mode; here it only has to not be mistaken for a
            // conformance failure.
            if (IsKnownDefectField(fixture.Expected(), "L3", "import"))
            {
                EXPECT_FALSE(actual->extracted)
                    << "the manifest records this primitive as not importable, but it imported. "
                       "If the owning task landed, update its defect record.";
                continue;
            }
            ASSERT_TRUE(actual->extracted) << "ExtractMesh threw: " << actual->error;
            const MeshOutDump& dump = actual->dump;

            // The topology the file declares reaches L3 and is not assumed (GLTF-071). This is the
            // SOURCE mode, and it survives any import-time conversion -- which is exactly what
            // makes the conversion below checkable rather than merely asserted against itself.
            EXPECT_TRUE(dump.topologyCarried);
            EXPECT_EQ(static_cast<int>(NumberOr(expected, "mode", -1)), dump.topologyMode);
            EXPECT_EQ(StringOr(expected, "modeName", ""), dump.topologyName);

            // GLTF-072: what CNA's own documented per-mode policy (plan_gltf.md §10.1) must turn
            // the primitive into. Kept separate from the spec-derived fields above because it is a
            // CNA decision -- converting strips and fans at import rather than plumbing new
            // topologies through every renderer.
            const JsonValue& policy = Member(expected, "importPolicy");
            ASSERT_EQ(JsonType::Object, policy.type)
                << "the manifest has no importPolicy block -- a generator change dropped it";
            EXPECT_EQ(static_cast<int>(NumberOr(policy, "topologyMode", -1)),
                      dump.importedTopologyMode)
                << "the emitted index list is not in the topology the import policy requires";
            EXPECT_EQ(StringOr(policy, "topologyName", ""), dump.importedTopologyName);

            EXPECT_EQ(static_cast<std::size_t>(NumberOr(expected, "vertexCount", -1)),
                      dump.vertexCount);

            // Only the streams the fixture actually authors are asserted. What an importer packs
            // into a slot whose attribute the file omits is not something the specification
            // constrains, and asserting CNA's fill value here would silently promote an
            // implementation choice to a requirement.
            // plan_gltf.md GLTF-178: tangents are compared here too, but only where the fixture
            // AUTHORS them. A generated tangent basis is CNA's own algorithm rather than anything
            // §3.7.2.1 prescribes, so the manifest states none and this comparison skips it --
            // asserting a generated basis would promote an implementation choice to a conformance
            // requirement, which is the one thing this corpus must not do.
            for (const auto& [field, actualValues] : {
                     std::pair{std::string("positions"), Flatten(dump.positions)},
                     std::pair{std::string("normals"), Flatten(dump.normals)},
                     std::pair{std::string("tangents"), Flatten(dump.tangents)},
                     std::pair{std::string("texcoords"), Flatten(dump.texcoords)},
                     std::pair{std::string("weights"), Flatten(dump.weights)}})
            {
                const JsonValue& expectedField = Member(expected, field);
                ASSERT_EQ(JsonType::Array, expectedField.type)
                    << field << ": the manifest has no such array -- a generator change dropped it";
                // An empty array means the fixture does not author this attribute at all. What an
                // importer packs into a layout slot whose attribute the file omits is not
                // constrained by the specification, so nothing is asserted about it.
                if (expectedField.arrayValue.empty()) { continue; }
                if (IsKnownDefectField(fixture.Expected(), "L3", field)) { continue; }
                // A stream the file authors and CNA's chosen vertex layout has no slot for. The
                // manifest states it at full value -- that is what the file means -- and names it
                // here as dropped, with the reason. What is asserted is that it really is DROPPED:
                // "documented limitation" must not become cover for "present and wrong".
                if (IsDroppedAttribute(expected, field))
                {
                    EXPECT_TRUE(actualValues.empty())
                        << field << " is declared dropped for this primitive's layout, but the "
                           "importer produced values for it";
                    continue;
                }
                const std::vector<double> expectedValues = Numbers(expectedField);
                ASSERT_FALSE(expectedValues.empty()) << field << ": manifest values are not numeric";
                ExpectComponents(expectedValues, actualValues, field);
            }

            // GLTF-182: only dual-UV fixtures carry this optional L3 field, keeping the other
            // generated expectations byte-stable while still comparing the second packed stream
            // component-for-component where the file genuinely samples it.
            const JsonValue& expectedTexcoords1 = Member(expected, "texcoords1");
            if (expectedTexcoords1.type == JsonType::Array &&
                !expectedTexcoords1.arrayValue.empty() &&
                !IsKnownDefectField(fixture.Expected(), "L3", "texcoords1"))
            {
                const std::vector<double> expectedValues = Numbers(expectedTexcoords1);
                ASSERT_FALSE(expectedValues.empty())
                    << "texcoords1: manifest values are not numeric";
                ExpectComponents(expectedValues, Flatten(dump.texcoords1), "texcoords1");
            }

            const std::vector<double> expectedColors = Numbers(Member(expected, "colors"));
            if (!expectedColors.empty() && !IsKnownDefectField(fixture.Expected(), "L3", "colors"))
            {
                ASSERT_EQ(expectedColors.size(), dump.colors.size() * 4)
                    << "colors: component count differs";
                for (std::size_t i = 0; i < expectedColors.size(); ++i)
                {
                    // The expectation is the decoded [0,1] float; CNA repacks to a byte, so the
                    // comparison is against the specification's own round-trip of that float.
                    const auto expectedByte = static_cast<int>(
                        std::clamp(expectedColors[i], 0.0, 1.0) * 255.0 + 0.5);
                    EXPECT_EQ(expectedByte, static_cast<int>(dump.colors[i / 4][i % 4]))
                        << "colors: component[" << i << "]";
                }
            }

            const std::vector<double> expectedJoints = Numbers(Member(expected, "joints"));
            if (!expectedJoints.empty() && !IsKnownDefectField(fixture.Expected(), "L3", "joints"))
            {
                ASSERT_EQ(expectedJoints.size(), dump.joints.size() * 4);
                for (std::size_t i = 0; i < expectedJoints.size(); ++i)
                {
                    EXPECT_EQ(static_cast<int>(expectedJoints[i]),
                              static_cast<int>(dump.joints[i / 4][i % 4]))
                        << "joints: component[" << i << "]";
                }
            }

            if (!IsKnownDefectField(fixture.Expected(), "L3", "indices"))
            {
                // The index list CNA emits is the import policy's, not the file's: for a strip or
                // fan the two differ, and comparing against the authored run would assert that no
                // conversion happened. For a TRIANGLES primitive the generator writes the same
                // list into both, so nothing about the existing fixtures is weakened.
                const std::vector<double> expectedIndices = Numbers(Member(policy, "indices"));
                ASSERT_EQ(expectedIndices.size(), dump.indices.size()) << "indices: count differs";
                for (std::size_t i = 0; i < expectedIndices.size(); ++i)
                {
                    EXPECT_EQ(static_cast<std::uint32_t>(expectedIndices[i]), dump.indices[i])
                        << "indices[" << i << "]";
                }

                // The converted list must describe exactly the triangles §3.7.2.1 derives from the
                // authored one. Asserted from `triangles` rather than from `importPolicy.indices`,
                // so a generator bug that got the conversion rule wrong in BOTH places still fails.
                const JsonValue& triangles = Member(expected, "triangles");
                if (triangles.type == JsonType::Array && !triangles.arrayValue.empty())
                {
                    ASSERT_EQ(triangles.arrayValue.size() * 3, dump.indices.size())
                        << "the emitted index list does not describe the expected triangle count";
                    // plan_gltf.md GLTF-461: §3.7.2.1's flat-normal split renumbers, so an emitted
                    // index is compared through the manifest's own independently computed remap
                    // rather than against the authored number directly. `triangles` stays in
                    // AUTHORED numbering because it is the spec's own expansion of the file, which
                    // is exactly what makes this a second opinion on the conversion rule.
                    const std::vector<double> splitSource =
                        Numbers(Path(policy, "flatNormalSplit.sourceVertex"));
                    const auto authoredCorner = [&](std::uint32_t emitted) -> std::uint32_t {
                        if (splitSource.empty()) { return emitted; }
                        return emitted < splitSource.size()
                            ? static_cast<std::uint32_t>(splitSource[emitted])
                            : emitted;
                    };
                    for (std::size_t t = 0; t < triangles.arrayValue.size(); ++t)
                    {
                        const std::vector<double> corners = Numbers(triangles.arrayValue[t]);
                        ASSERT_EQ(3u, corners.size()) << "triangles[" << t << "]";
                        for (std::size_t c = 0; c < 3; ++c)
                        {
                            EXPECT_EQ(static_cast<std::uint32_t>(corners[c]),
                                      authoredCorner(dump.indices[t * 3 + c]))
                                << "triangles[" << t << "][" << c << "] -- winding is preserved "
                                   "only if a strip's odd triangle swaps its first two corners";
                        }
                    }
                }
            }
        }

        // The converse of "every expected primitive is imported": nothing outside the default
        // scene may be. This is what makes scene-default-selection meaningful.
        const JsonValue& excluded = Path(fixture.Expected(), "l3.excludedMeshes");
        for (const double meshIndex : Numbers(excluded))
        {
            EXPECT_EQ(nullptr, FindExtracted(extracted, static_cast<int>(meshIndex), 0))
                << "mesh " << static_cast<int>(meshIndex)
                << " is outside the default scene but was imported anyway";
        }
    }
}

TEST(GltfConformanceL3, IorAndSpecularFactorsMatchTheManifest)
{
    const LoadedFixture fixture("mat-factor-only-gold");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    const std::vector<ExtractedPrimitive> extracted = ExtractSceneMeshesEXT(fixture.Data());
    const ExtractedPrimitive* actual = FindExtracted(extracted, 0, 0);
    ASSERT_NE(nullptr, actual);
    ASSERT_TRUE(actual->extracted) << actual->error;

    const JsonValue& expectedPrimitive = ExpectedPrimitives(fixture).arrayValue.at(0);
    const JsonValue& material = Member(expectedPrimitive, "material");
    ASSERT_EQ(JsonType::Object, material.type);

    const MeshOutDump& dump = actual->dump;
    EXPECT_NEAR(NumberOr(material, "ior", -1.0), static_cast<double>(dump.ior), 1e-6);
    EXPECT_NEAR(NumberOr(material, "specularFactor", -1.0),
                static_cast<double>(dump.specularFactor), 1e-6);
    ExpectComponents(Numbers(Member(material, "specularColorFactor")),
                     std::vector<float>(dump.specularColorFactor.begin(),
                                        dump.specularColorFactor.end()),
                     "specularColorFactor");
}

// --- L4: world-space geometry ------------------------------------------------------------------

TEST(GltfConformanceL4, ExpectedWorldPositionsMatchTheManifest)
{
    // The oracle half of L4: proves the C++ world-transform evaluator agrees with the generator
    // that produced the manifest, and with cgltf's independent composition. This says nothing
    // about CNA -- it is the harness proving itself trustworthy before it judges anything.
    for (const std::string& id : CorpusFixtureIds())
    {
        SCOPED_TRACE(id);
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        // A fixture the importer must refuse has no expectation at this layer at all; the refusal
        // is asserted in full by GltfContainerValidation.
        if (IsRejectionFixture(fixture.Expected()) ||
            RequiresUnavailableDraco(fixture.Expected())) { continue; }

        const WorldPositions expectedWorld = EvaluateWorldPositionsEXT(fixture.Data());
        EXPECT_TRUE(expectedWorld.selfCheckPassed)
            << "the oracle's own node composition disagreed with cgltf_node_transform_world -- the "
               "harness is broken, not CNA";

        const JsonValue& instances = Path(fixture.Expected(), "l4.instances");
        ASSERT_EQ(JsonType::Array, instances.type);
        ASSERT_EQ(instances.arrayValue.size(), expectedWorld.instances.size())
            << "instance count differs from the manifest";

        for (std::size_t i = 0; i < instances.arrayValue.size(); ++i)
        {
            const JsonValue& expected = instances.arrayValue[i];
            const WorldInstance& actual = expectedWorld.instances[i];
            SCOPED_TRACE("instance " + std::to_string(i) + " (" + actual.nodeName + ")");

            EXPECT_EQ(static_cast<int>(NumberOr(expected, "node", -1)), actual.node);
            EXPECT_EQ(static_cast<int>(NumberOr(expected, "mesh", -1)), actual.mesh);
            EXPECT_EQ(static_cast<int>(NumberOr(expected, "primitive", 0)), actual.primitive);
            EXPECT_EQ(StringOr(expected, "nodeName", ""), actual.nodeName);
            ExpectComponents(Numbers(Member(expected, "worldMatrixColumnMajor")),
                             std::vector<float>(actual.worldMatrix.begin(), actual.worldMatrix.end()),
                             "worldMatrixColumnMajor");
            ExpectComponents(Numbers(Member(expected, "worldPositions")),
                             Flatten(actual.worldPositions), "worldPositions");
        }

        const std::vector<double> expectedMin = Numbers(Path(fixture.Expected(), "l4.worldBounds.min"));
        if (!expectedMin.empty())
        {
            ASSERT_TRUE(expectedWorld.hasBounds);
            ExpectComponents(expectedMin,
                             std::vector<float>(expectedWorld.min.begin(), expectedWorld.min.end()),
                             "worldBounds.min");
            ExpectComponents(Numbers(Path(fixture.Expected(), "l4.worldBounds.max")),
                             std::vector<float>(expectedWorld.max.begin(), expectedWorld.max.end()),
                             "worldBounds.max");
        }
    }
}

TEST(GltfConformanceL4, CnaWorldPositionsMatchTheExpectedGeometry)
{
    // The judgement half of L4: what CNA's real import path places in world space, compared
    // against the oracle. Fixtures whose world geometry a recorded defect breaks are covered by
    // the GltfKnownDefect suite instead, so this stays a clean green regression test.
    for (const std::string& id : CorpusFixtureIds())
    {
        SCOPED_TRACE(id);
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        // A fixture the importer must refuse has no expectation at this layer at all; the refusal
        // is asserted in full by GltfContainerValidation.
        if (IsRejectionFixture(fixture.Expected()) ||
            RequiresUnavailableDraco(fixture.Expected())) { continue; }
        // A defect that stops a primitive being imported at all contributes no world geometry
        // either, which is why such a record declares its L4 fields as well as its L3 ones. The
        // fixture's L4 expectation is untouched -- it is simply not reachable while it is open.
        if (IsKnownDefectField(fixture.Expected(), "L4", "worldPositions")) { continue; }

        const WorldPositions expectedWorld = EvaluateWorldPositionsEXT(fixture.Data());
        const WorldPositions cnaWorld = EvaluateCnaWorldPositionsEXT(fixture.Data());

        ASSERT_EQ(expectedWorld.instances.size(), cnaWorld.instances.size())
            << "CNA imported a different number of mesh instances than the default scene contains";

        // Instances are paired by the node and primitive, not by position in the list. The two
        // sides enumerate in different, equally legitimate orders -- the oracle walks the scene
        // depth-first, CNA walks the glTF node array (a deliberate ordering CollectMeshGroups
        // documents) -- and those coincide for a file whose node array happens to be authored in
        // traversal order. Comparing positionally would turn any fixture where they diverge into a
        // wall of numeric failures that says nothing about the geometry, which is exactly the
        // wrong signal: instance ORDER is not a glTF-specified property. The node index is.
        using PlacementKey = std::pair<int, int>;  // node, primitive
        std::map<PlacementKey, const WorldInstance*> expectedByPlacement;
        std::map<PlacementKey, const WorldInstance*> cnaByPlacement;
        for (const WorldInstance& instance : expectedWorld.instances)
        {
            EXPECT_TRUE(expectedByPlacement.emplace(
                PlacementKey{instance.node, instance.primitive}, &instance).second)
                << "oracle emitted the same node/primitive placement twice";
        }
        for (const WorldInstance& instance : cnaWorld.instances)
        {
            EXPECT_TRUE(cnaByPlacement.emplace(
                PlacementKey{instance.node, instance.primitive}, &instance).second)
                << "CNA emitted the same node/primitive placement twice";
        }
        ASSERT_EQ(expectedByPlacement.size(), cnaByPlacement.size())
            << "CNA placed a different set of node/primitive pairs than the default scene does";

        for (const auto& [key, expectedInstance] : expectedByPlacement)
        {
            SCOPED_TRACE("node " + std::to_string(key.first) + " primitive " +
                         std::to_string(key.second));
            const auto found = cnaByPlacement.find(key);
            ASSERT_NE(cnaByPlacement.end(), found)
                << "CNA imported no instance for this node/primitive pair";
            EXPECT_EQ(expectedInstance->mesh, found->second->mesh);
            // plan_gltf.md GLTF-461: §3.7.2.1's flat-normal split duplicates a vertex shared
            // between differently oriented faces, so CNA can emit more vertices than the file
            // declares. The invariant is that the split DUPLICATES and never MOVES: every emitted
            // vertex must sit exactly where its source vertex does. Mapping through the remap keeps
            // that exact -- collapsing the copies instead would hide a copy that had moved.
            const std::vector<std::uint32_t>& remap = found->second->vertexSource;
            std::vector<std::array<float, 3>> expectedForCna;
            if (remap.empty())
            {
                expectedForCna = expectedInstance->worldPositions;
            }
            else
            {
                EXPECT_EQ(remap.size(), found->second->worldPositions.size())
                    << "the split remap does not describe every emitted vertex";
                for (const std::uint32_t source : remap)
                {
                    ASSERT_LT(source, expectedInstance->worldPositions.size())
                        << "the split remap names a vertex the file does not declare";
                    expectedForCna.push_back(expectedInstance->worldPositions[source]);
                }
            }
            const std::vector<float> expectedPositions = Flatten(expectedForCna);
            ExpectComponents(std::vector<double>(expectedPositions.begin(), expectedPositions.end()),
                             Flatten(found->second->worldPositions), "worldPositions");
        }
    }
}

// --- plan_gltf.md GLTF-401: the manifest describes every asset, completely ------------------------

TEST(GltfFixtureCorpus, EveryAssetDeclaresItsGroupItsLayersAndAnExpectationForEachOfThem)
{
    // §24.1's inventory is only worth having if it is total. The count check above proves the
    // owning groups add up; this proves each entry actually says what it claims to validate, and
    // -- the part that matters -- that the fixture carries an expectation for every layer it
    // names. A fixture declaring "L4" with an empty `l4` block is compared against nothing at that
    // layer while the inventory reports coverage, which is worse than declaring nothing at all.
    const JsonValue& manifest = CorpusManifest();
    const JsonValue& assets = Member(manifest, "assets");
    ASSERT_EQ(JsonType::Array, assets.type);
    ASSERT_FALSE(assets.arrayValue.empty());

    std::set<std::string> knownGroups;
    for (const auto& [name, unused] : Member(manifest, "owningGroupCounts").objectValue)
    {
        (void)unused;
        knownGroups.insert(name);
    }
    ASSERT_FALSE(knownGroups.empty());

    const std::set<std::string> layerNames{"L1", "L2", "L3", "L4", "L5", "L6"};
    std::size_t declaredLayers = 0;
    for (const JsonValue& asset : assets.arrayValue)
    {
        const std::string id = StringOr(asset, "id", "");
        SCOPED_TRACE(id);
        ASSERT_FALSE(id.empty()) << "an inventory entry has no id";

        const std::string owning = StringOr(asset, "owningGroup", "");
        EXPECT_NE(knownGroups.end(), knownGroups.find(owning))
            << "owningGroup '" << owning << "' is not one of the counted groups";

        // A referencing group is a *different* group that uses the asset. Naming its own owning
        // group would double-count it in exactly the way §24.1's ownership model exists to prevent.
        const JsonValue& referencing = Member(asset, "referencingGroups");
        ASSERT_EQ(JsonType::Array, referencing.type) << "referencingGroups is not an array";
        for (const JsonValue& group : referencing.arrayValue)
        {
            ASSERT_EQ(JsonType::String, group.type);
            EXPECT_NE(owning, group.stringValue)
                << "an asset lists its own owning group as a referencing group";
            EXPECT_NE(knownGroups.end(), knownGroups.find(group.stringValue))
                << "referencingGroups names '" << group.stringValue << "', which is not a group";
        }

        const JsonValue& layers = Member(asset, "validatedLayers");
        ASSERT_EQ(JsonType::Array, layers.type);
        EXPECT_FALSE(layers.arrayValue.empty())
            << "the asset validates no layer at all, so nothing compares it to anything";

        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        for (const JsonValue& layer : layers.arrayValue)
        {
            ASSERT_EQ(JsonType::String, layer.type);
            const std::string name = layer.stringValue;
            EXPECT_NE(layerNames.end(), layerNames.find(name)) << "unknown layer '" << name << "'";
            ++declaredLayers;

            // L1 is the container itself -- the file parsing at all is its expectation, and the
            // manifest's `l1` block carries the container facts rather than a comparison.
            if (name == "L1") { continue; }
            const std::string key = "l" + name.substr(1);
            const JsonValue& block = Member(fixture.Expected(), key);
            EXPECT_NE(JsonType::Null, block.type)
                << "the inventory says this asset validates " << name
                << ", and its expectation has no '" << key << "' block to validate against";
        }
    }
    EXPECT_GT(declaredLayers, 100u)
        << "too few declared layers across the corpus for this to be a completeness check";
}

// --- plan_gltf.md GLTF-419: the corpus stays small enough to read --------------------------------

TEST(GltfFixtureCorpus, EveryAssetFitsTheSizeBudgetOrStatesWhyItCannot)
{
    // A conformance fixture is evidence a human reads. One that has grown to hundreds of kilobytes
    // is a blob nobody checks, and a corpus of them is a build-time cost paid on every clone.
    //
    // The budget is on the ASSETS -- the .gltf and its .glb twin -- rather than on their
    // expectations, which are allowed to be long because they are the stated answers, not the
    // input. An asset over budget is not automatically wrong, but it must carry a REASON in the
    // manifest: an exemption list living in this test is how a budget quietly stops binding.
    constexpr std::uintmax_t kPerAssetBudget = 8 * 1024;
    constexpr std::uintmax_t kCorpusBudget = 2 * 1024 * 1024;

    std::map<std::string, std::string> exemptions;
    for (const JsonValue& asset : Member(CorpusManifest(), "assets").arrayValue)
    {
        const std::string reason = StringOr(asset, "sizeExemptionReason", "");
        if (!reason.empty()) { exemptions.emplace(StringOr(asset, "id", ""), reason); }
    }

    std::uintmax_t corpusBytes = 0;
    std::uintmax_t largestAsset = 0;
    std::string largestName;
    std::size_t checked = 0;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(CorpusDirectory()))
    {
        if (!entry.is_regular_file()) { continue; }
        const std::uintmax_t size = entry.file_size();
        corpusBytes += size;

        const std::string name = entry.path().filename().string();
        const std::string extension = entry.path().extension().string();
        if (extension != ".gltf" && extension != ".glb") { continue; }
        ++checked;
        const std::string id = name.substr(0, name.size() - extension.size());
        if (size > largestAsset) { largestAsset = size; largestName = name; }

        const auto exemption = exemptions.find(id);
        if (exemption != exemptions.end())
        {
            EXPECT_GT(size, kPerAssetBudget / 2)
                << id << " declares a size exemption it does not need (" << size
                << " bytes) -- an exemption nobody needs is one nobody notices going stale";
            continue;
        }
        EXPECT_LE(size, kPerAssetBudget)
            << name << " is " << size << " bytes, over the " << kPerAssetBudget
            << "-byte per-asset budget, and its manifest entry states no sizeExemptionReason";
    }

    EXPECT_GT(checked, 100u) << "too few assets measured -- the .gltf/.glb pairs were not found";
    EXPECT_LE(corpusBytes, kCorpusBudget)
        << "the whole corpus is " << corpusBytes << " bytes, over the stated "
        << kCorpusBudget << "-byte ceiling";
    RecordProperty("corpusBytes", static_cast<int>(corpusBytes));
    RecordProperty("largestAssetBytes", static_cast<int>(largestAsset));
    RecordProperty("largestAsset", largestName);
}

// --- plan_gltf.md GLTF-416: the corpus documentation is generated, not maintained ----------------

TEST(GltfFixtureCorpus, TheConformanceDocListsEveryFixtureTheManifestDeclares)
{
    // A corpus inventory written by hand is stale the first time a fixture is added, and a stale
    // inventory is worse than none: it reads as a coverage claim nobody checked. So
    // docs/gltf-conformance.md §7 is generated (`--fixture-table`) and compared here, the same rule
    // §19's extension table lives under -- with the corrected table printed on failure, so fixing
    // the document is a paste rather than a reading exercise.
    const std::filesystem::path doc =
        CorpusDirectory().parent_path().parent_path().parent_path() / "docs" / "gltf-conformance.md";
    std::ifstream file(doc);
    ASSERT_TRUE(file.is_open()) << "cannot open " << doc;

    std::map<std::string, std::string> documented;  // fixture id -> owning group
    std::string line;
    bool inSection = false;
    while (std::getline(file, line))
    {
        if (line.rfind("## 7. The corpus, fixture by fixture", 0) == 0) { inSection = true; continue; }
        if (inSection && line.rfind("## ", 0) == 0) { break; }
        if (!inSection || line.empty() || line.front() != '|') { continue; }
        if (line.find("---") != std::string::npos) { continue; }

        std::vector<std::string> cells;
        std::string cell;
        std::istringstream stream(line);
        while (std::getline(stream, cell, '|'))
        {
            std::string::size_type at;
            while ((at = cell.find('`')) != std::string::npos) { cell.erase(at, 1); }
            const auto first = cell.find_first_not_of(" \t");
            cells.push_back(first == std::string::npos
                                ? std::string()
                                : cell.substr(first, cell.find_last_not_of(" \t") - first + 1));
        }
        if (!cells.empty() && cells.front().empty()) { cells.erase(cells.begin()); }
        if (cells.size() < 4 || cells[0] == "Fixture") { continue; }
        EXPECT_TRUE(documented.emplace(cells[0], cells[1]).second)
            << "§7 lists " << cells[0] << " twice";
    }
    ASSERT_FALSE(documented.empty()) << "§7's table parsed to nothing -- the heading moved?";

    std::ostringstream corrected;
    corrected << "\n  regenerate with: python3 -m gltf_fixtures --fixture-table\n";

    std::size_t compared = 0;
    for (const JsonValue& asset : Member(CorpusManifest(), "assets").arrayValue)
    {
        const std::string id = StringOr(asset, "id", "");
        SCOPED_TRACE(id);
        const auto found = documented.find(id);
        ASSERT_NE(documented.end(), found)
            << "the corpus contains " << id << " and docs/gltf-conformance.md §7 does not list it, "
               "so the inventory claims coverage it does not have." << corrected.str();
        EXPECT_EQ(StringOr(asset, "owningGroup", ""), found->second)
            << "§7 and the manifest disagree about which group owns " << id << "."
            << corrected.str();
        documented.erase(found);
        ++compared;
    }
    for (const auto& [id, group] : documented)
    {
        ADD_FAILURE() << "§7 lists " << id << " (" << group
                      << "), which is not in the corpus -- a fixture was removed and its row was "
                         "left behind." << corrected.str();
    }
    EXPECT_EQ(CorpusFixtureIds().size(), compared);
}

// --- plan_gltf.md GLTF-414: inline documents stay a deliberate choice ----------------------------

TEST(GltfFixtureCorpus, InlineGltfDocumentsDoNotGrowWithoutADecision)
{
    // The suite holds a few hundred glTF documents written as C++ string literals, and
    // docs/gltf-conformance.md §3.8 records why they are not corpus fixtures: they are negative
    // one-offs, probes of loader machinery rather than of glTF semantics, or mutations. Each is a
    // defensible choice, and the risk is that the choice stops being made -- an inline document is
    // invisible to every corpus sweep, so it asserts only what its own test asserts.
    //
    // Hence a ceiling rather than a ban. Adding one is fine; raising this number is the deliberate
    // act that says so, and the commit that raises it is where the reason goes.
    // GLTF-179 adds one loader-algorithm probe, not a corpus conformance asset: it exists only to
    // compare ExtractMesh's generated tangent bytes with six outputs measured from the external
    // MikkTSpace reference implementation. The corpus cannot derive those reference values itself.
    //
    // 261 -> 263 buys no new document. GltfUvChannelTests' per-map transform builder splices
    // `extensionExtras` and `materialExtras` into the middle of one document, and each splice
    // closes and reopens the raw literal -- so three literal openings there are one glTF file,
    // and the count below exceeds the document count by exactly that much. It is raised rather
    // than the counter taught to see through concatenation, because the ratchet's job is to make
    // the next addition a deliberate act, and it still does that from here.
    //
    // 263 -> 265 for GLTF-461's two spec-rule probes, then 277 -> 275 when GLTF-464 REMOVED them
    // again on 2026-08-18. Both were conformance statements about the format, so §3.8's rule put
    // them in tools/gltf_fixtures/ as `tangent-without-normal` and `morph-normalless-quad`; they had
    // been inline only because the corpus asset count is pinned by four committed L7 provenance
    // reports that enumerate every asset, and the recorded blocker -- that `directx11` could not be
    // re-captured here -- had already been disproved by GLTF-471. The corpus is 148 now and all four
    // policies were re-captured. This is the only entry in this list that subtracts, and it is what
    // the ratchet is for: the ceiling records decisions in both directions.
    //
    // 265 -> 267 for GLTF-468's two storage-form probes, and both are the same story: §3.7.2.1's
    // attribute table allows `COLOR_n` as VEC3 or VEC4 and `TEXCOORD_n` as float, unsigned byte
    // normalized or unsigned short normalized, and the corpus had an asset for exactly one form of
    // each. Neither found a bug -- CNA decodes all of them -- so what they add is the assertion, and
    // an unasserted rule is how the four §27.1.2 divergences survived. GLTF-464 owns promoting them.
    //
    // 267 -> 276 buys no further document, for the reason this test already records above:
    // GLTF-468's `ColorAndTexcoordFormDocument` is PARAMETERISED (colour type and texcoord component
    // type), so it splices `std::to_string` calls into the middle of one file and each splice closes
    // and reopens the raw literal. Ten literal openings there are one glTF document. The ratchet is
    // still doing its job from here -- its purpose is to make the next ADDITION deliberate, not to
    // count string fragments, and teaching the counter to see through concatenation would trade a
    // reliable prompt for a fragile parser.
    //
    // 276 -> 277 for GLTF-470's sparse animation sampler: one document, written with literal offsets
    // so it needs no splices at all. It crosses two features the corpus covers separately and never
    // together, which is a gap §24's own per-feature inventory is structurally unable to see.
    //
    // Note for whoever edits this comment: the scan counts the opening delimiter anywhere in a
    // .cpp, comments included, so spelling it here would raise the very number it explains.
    constexpr int kCeiling = 275;

    int found = 0;
    std::map<std::string, int> perFile;
    const std::filesystem::path tests =
        CorpusDirectory().parent_path().parent_path().parent_path() / "modules" / "content" / "tests";
    ASSERT_TRUE(std::filesystem::is_directory(tests)) << "cannot find " << tests;

    for (const std::filesystem::directory_entry& entry :
         std::filesystem::recursive_directory_iterator(tests))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".cpp") { continue; }
        std::ifstream file(entry.path());
        const std::string source((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());
        int count = 0;
        for (std::string::size_type at = source.find("R\"GLTF("); at != std::string::npos;
             at = source.find("R\"GLTF(", at + 1))
        {
            ++count;
        }
        if (count == 0) { continue; }
        found += count;
        perFile[entry.path().filename().string()] = count;
    }

    RecordProperty("inlineGltfDocuments", found);
    RecordProperty("filesWithInlineDocuments", static_cast<int>(perFile.size()));

    // Where they live is part of the rule: an inline glTF document inside a suite that is not a
    // glTF suite is a test asserting glTF behaviour from outside every glTF gate.
    for (const auto& [name, count] : perFile)
    {
        EXPECT_TRUE(name.find("Gltf") != std::string::npos)
            << name << " holds " << count << " inline glTF document(s) and is not a glTF suite, so "
               "they sit outside the conformance label and the sanitizer job's filter";
    }

    EXPECT_LE(found, kCeiling)
        << "there are now " << found << " inline glTF documents, over the recorded " << kCeiling
        << ". That is not a failure of the new test -- it is the decision GLTF-414 asks for. If the "
           "document is an asset whose correct import is a conformance statement, it belongs in "
           "tools/gltf_fixtures/ instead (docs/gltf-conformance.md §3.7). If it is a negative "
           "one-off, a loader-machinery probe or a mutation, raise this ceiling and say which in "
           "the commit message.";
}

// --- plan_gltf.md GLTF-066: the L2 dump shows the sparse override APPLIED -------------------------

TEST(GltfConformanceL2, ASparseOverrideIsVisibleInTheDumpAndNotJustPlausible)
{
    // "The dump shows the override applied" is only checkable against what the dump would have
    // said WITHOUT it. Each sparse fixture therefore states its base array -- the values a decoder
    // that ignored the `sparse` block would produce -- and this asserts three things in order:
    // the decoded values match the effective expectation, they differ from the base, and they
    // differ at the elements the override names. Without the middle one, a fixture whose base
    // happened to equal its overridden values would pass while proving nothing, which is exactly
    // how D4 hid: `cgltf_accessor_read_index` returned 0 for every element, and 0 was a legal
    // index.
    std::size_t checkedAccessors = 0;
    for (const std::string& id : CorpusFixtureIds())
    {
        SCOPED_TRACE(id);
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        if (IsRejectionFixture(fixture.Expected())) { continue; }

        for (const JsonValue& expected : Path(fixture.Expected(), "l2.accessors").arrayValue)
        {
            const JsonValue& base = Member(expected, "baseValuesIfSparseIgnored");
            if (base.type != JsonType::Array) { continue; }
            const auto index = static_cast<std::size_t>(NumberOr(expected, "index", -1));
            SCOPED_TRACE("accessor " + std::to_string(index) + " (" +
                         StringOr(expected, "usage", "?") + ")");
            ++checkedAccessors;

            EXPECT_TRUE(CnaTest::GltfOracle::BoolOr(expected, "sparse", false))
                << "an accessor states a sparse base but is not marked sparse";

            const AccessorDump dump = DumpAccessorEXT(fixture.Data(), index);
            ASSERT_TRUE(dump.decoded) << dump.error;

            const std::vector<double> effective = Numbers(Member(expected, "values"));
            const std::vector<double> ignored = Numbers(base);
            ASSERT_EQ(effective.size(), ignored.size())
                << "the base array and the effective values have different lengths";
            ASSERT_EQ(effective.size(), dump.values.size());

            std::size_t differingComponents = 0;
            for (std::size_t i = 0; i < effective.size(); ++i)
            {
                EXPECT_NEAR(effective[i], static_cast<double>(dump.values[i]), 1e-6)
                    << "component " << i << " does not match the effective expectation";
                if (std::fabs(effective[i] - ignored[i]) > 1e-6) { ++differingComponents; }
            }
            EXPECT_GT(differingComponents, 0u)
                << "this fixture's sparse override changes nothing: its base array already equals "
                   "its effective values, so a decoder that skipped the override entirely would "
                   "pass every assertion above";
        }
    }
    EXPECT_GE(checkedAccessors, 3u)
        << "fewer than three sparse accessors state a base array -- the control has shrunk";
}

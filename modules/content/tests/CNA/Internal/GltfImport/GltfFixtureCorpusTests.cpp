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
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <gtest/gtest.h>
#include <iomanip>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "System/Security/Cryptography/SHA256.hpp"

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
        const long long expectedScene = static_cast<long long>(NumberOr(l1, "defaultScene", -1));
        ASSERT_NE(nullptr, data.scene) << "file declares no default scene";
        EXPECT_EQ(expectedScene, static_cast<long long>(data.scene - data.scenes));

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
            ASSERT_TRUE(dump.decoded) << dump.error;

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
        if (IsRejectionFixture(fixture.Expected())) { continue; }

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
            for (const auto& [field, actualValues] : {
                     std::pair{std::string("positions"), Flatten(dump.positions)},
                     std::pair{std::string("normals"), Flatten(dump.normals)},
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
                const std::vector<double> expectedValues = Numbers(expectedField);
                ASSERT_FALSE(expectedValues.empty()) << field << ": manifest values are not numeric";
                ExpectComponents(expectedValues, actualValues, field);
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
                    for (std::size_t t = 0; t < triangles.arrayValue.size(); ++t)
                    {
                        const std::vector<double> corners = Numbers(triangles.arrayValue[t]);
                        ASSERT_EQ(3u, corners.size()) << "triangles[" << t << "]";
                        for (std::size_t c = 0; c < 3; ++c)
                        {
                            EXPECT_EQ(static_cast<std::uint32_t>(corners[c]),
                                      dump.indices[t * 3 + c])
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
        if (IsRejectionFixture(fixture.Expected())) { continue; }

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
        if (IsRejectionFixture(fixture.Expected())) { continue; }
        // A defect that stops a primitive being imported at all contributes no world geometry
        // either, which is why such a record declares its L4 fields as well as its L3 ones. The
        // fixture's L4 expectation is untouched -- it is simply not reachable while it is open.
        if (IsKnownDefectField(fixture.Expected(), "L4", "worldPositions")) { continue; }

        const WorldPositions expectedWorld = EvaluateWorldPositionsEXT(fixture.Data());
        const WorldPositions cnaWorld = EvaluateCnaWorldPositionsEXT(fixture.Data());

        ASSERT_EQ(expectedWorld.instances.size(), cnaWorld.instances.size())
            << "CNA imported a different number of mesh instances than the default scene contains";

        // Instances are paired by the node that places them, not by position in the list. The two
        // sides enumerate in different, equally legitimate orders -- the oracle walks the scene
        // depth-first, CNA walks the glTF node array (a deliberate ordering CollectMeshGroups
        // documents) -- and those coincide for a file whose node array happens to be authored in
        // traversal order. Comparing positionally would turn any fixture where they diverge into a
        // wall of numeric failures that says nothing about the geometry, which is exactly the
        // wrong signal: instance ORDER is not a glTF-specified property. The node index is.
        std::map<int, std::vector<const WorldInstance*>> expectedByNode;
        std::map<int, std::vector<const WorldInstance*>> cnaByNode;
        for (const WorldInstance& instance : expectedWorld.instances)
        {
            expectedByNode[instance.node].push_back(&instance);
        }
        for (const WorldInstance& instance : cnaWorld.instances)
        {
            cnaByNode[instance.node].push_back(&instance);
        }
        ASSERT_EQ(expectedByNode.size(), cnaByNode.size())
            << "CNA placed meshes on a different set of nodes than the default scene does";

        for (const auto& [node, expectedInstances] : expectedByNode)
        {
            SCOPED_TRACE("node " + std::to_string(node));
            const auto found = cnaByNode.find(node);
            ASSERT_NE(cnaByNode.end(), found) << "CNA imported no mesh instance for this node";
            ASSERT_EQ(expectedInstances.size(), found->second.size())
                << "this node's primitive count differs";
            for (std::size_t i = 0; i < expectedInstances.size(); ++i)
            {
                SCOPED_TRACE("primitive " + std::to_string(i));
                EXPECT_EQ(expectedInstances[i]->mesh, found->second[i]->mesh);
                const std::vector<float> expectedPositions =
                    Flatten(expectedInstances[i]->worldPositions);
                ExpectComponents(
                    std::vector<double>(expectedPositions.begin(), expectedPositions.end()),
                    Flatten(found->second[i]->worldPositions), "worldPositions");
            }
        }
    }
}

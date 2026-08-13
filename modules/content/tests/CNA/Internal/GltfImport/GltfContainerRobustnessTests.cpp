// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-026 / GLTF-027 / GLTF-031 / GLTF-048: the container's own rules, and what
// happens when a file breaks them.
//
// Everything above this layer assumes the bytes are where the header says they are. §4.4's GLB
// layout is what makes that true -- chunk order, 4-byte alignment, a total length the header
// declares -- and a reader that trusts a malformed one indexes into whatever follows. None of the
// malformed cases below is hypothetical: a truncated download, a chunk length written before the
// payload was finished, a `byteLength` left over from an earlier export.
//
// The rule for every one of them is the same, and it is the only rule that makes a container
// robust: refuse by name, do not repair, and never read past what the file actually contains.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "GltfFixtureCorpus.hpp"
#include "GltfOracleEXT.hpp"

using namespace CNA::Internal::GltfImport;
using CnaTest::GltfOracle::CorpusDirectory;
using CnaTest::GltfOracle::CorpusFixtureIds;

namespace
{
    struct Parsed
    {
        cgltf_data* data = nullptr;
        ~Parsed() { if (data != nullptr) { cgltf_free(data); } }
        Parsed() = default;
        Parsed(const Parsed&) = delete;
        Parsed& operator=(const Parsed&) = delete;
    };

    bool ParseText(Parsed& out, const std::string& json)
    {
        cgltf_options options{};
        if (cgltf_parse(&options, json.data(), json.size(), &out.data) != cgltf_result_success)
        {
            return false;
        }
        return cgltf_load_buffers(&options, out.data, ".") == cgltf_result_success;
    }

    std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file),
                                          std::istreambuf_iterator<char>());
    }

    std::uint32_t ReadU32(const std::vector<std::uint8_t>& bytes, std::size_t offset)
    {
        std::uint32_t value = 0;
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        return value;
    }

    void WriteU32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value)
    {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    }

    /// True when cgltf accepts these bytes as a GLB *and* can load their buffers.
    bool AcceptsGlb(const std::vector<std::uint8_t>& bytes)
    {
        cgltf_options options{};
        cgltf_data* data = nullptr;
        if (cgltf_parse(&options, bytes.data(), bytes.size(), &data) != cgltf_result_success)
        {
            return false;
        }
        const bool loaded = cgltf_load_buffers(&options, data, ".") == cgltf_result_success;
        cgltf_free(data);
        return loaded;
    }

    /// A committed, valid GLB to mutate. Every case below starts from one so a refusal is
    /// attributable to the mutation rather than to anything else about the file.
    std::vector<std::uint8_t> ValidGlb()
    {
        return ReadBytes(CorpusDirectory() / "xf-identity.glb");
    }
}

// --- GLTF-026: the container's own layout ---------------------------------------------------------

TEST(GltfContainerRobustness, EveryCommittedGlbFollowsTheChunkLayoutSection44Requires)
{
    // §4.4.3: the JSON chunk comes first and is mandatory; the BIN chunk, if present, comes
    // second; each chunk's length is a multiple of 4 and each is padded to that -- JSON with
    // **spaces** and BIN with **zeros**, because the padding of the JSON chunk is inside the
    // document text a parser will read.
    //
    // Checked over the whole corpus rather than one file, because these are properties of the
    // emitter and a single asset could satisfy them by accident of size.
    for (const std::string& id : CorpusFixtureIds())
    {
        SCOPED_TRACE(id);
        const std::vector<std::uint8_t> bytes = ReadBytes(CorpusDirectory() / (id + ".glb"));
        ASSERT_GE(bytes.size(), 20u) << "shorter than a header plus one chunk header";
        ASSERT_EQ(0u, bytes.size() % 4) << "the file itself is not 4-byte aligned";
        EXPECT_EQ(bytes.size(), static_cast<std::size_t>(ReadU32(bytes, 8)))
            << "the header's total length disagrees with the file size";

        std::size_t offset = 12;
        int chunkIndex = 0;
        bool sawJson = false;
        bool sawBin = false;
        while (offset + 8 <= bytes.size())
        {
            const std::uint32_t chunkLength = ReadU32(bytes, offset);
            const std::uint32_t chunkType = ReadU32(bytes, offset + 4);
            SCOPED_TRACE("chunk " + std::to_string(chunkIndex));
            EXPECT_EQ(0u, chunkLength % 4) << "a chunk length must be a multiple of 4";
            ASSERT_LE(offset + 8 + chunkLength, bytes.size()) << "a chunk runs past the file";

            if (chunkIndex == 0)
            {
                EXPECT_EQ(0x4E4F534Au, chunkType) << "the first chunk must be JSON";
                sawJson = true;
                // Trailing padding is spaces: the chunk is handed to a JSON parser whole, and a
                // zero byte inside it is not whitespace.
                for (std::size_t i = offset + 8 + chunkLength; i-- > offset + 8;)
                {
                    if (bytes[i] == ' ') { continue; }
                    EXPECT_EQ('}', static_cast<char>(bytes[i]))
                        << "the JSON chunk is padded with something other than spaces";
                    break;
                }
            }
            else
            {
                EXPECT_EQ(0x004E4942u, chunkType) << "only a BIN chunk may follow the JSON one";
                sawBin = true;
            }
            offset += 8 + chunkLength;
            ++chunkIndex;
        }
        EXPECT_TRUE(sawJson);
        EXPECT_EQ(bytes.size(), offset) << "there are bytes after the last chunk";
        // Every corpus asset has a buffer, so every one has a BIN chunk -- if that ever stops
        // being true this assertion is the place to relax it deliberately.
        EXPECT_TRUE(sawBin) << "no BIN chunk, yet the asset declares a buffer";
    }
}

// --- GLTF-027: malformed GLB ----------------------------------------------------------------------

TEST(GltfContainerRobustness, AGlbWithABadMagicOrVersionIsRefused)
{
    // The two cheapest checks in the format, and the ones that decide whether anything else is
    // even worth attempting. A reader that skipped them would go on to interpret an arbitrary
    // file's bytes as chunk headers.
    {
        std::vector<std::uint8_t> bytes = ValidGlb();
        ASSERT_FALSE(bytes.empty());
        bytes[0] = 'X';
        EXPECT_FALSE(AcceptsGlb(bytes)) << "a file that is not a GLB at all was accepted";
    }
    {
        std::vector<std::uint8_t> bytes = ValidGlb();
        WriteU32(bytes, 4, 3);  // §4.4.1 pins the container version at 2
        EXPECT_FALSE(AcceptsGlb(bytes)) << "an unknown container version was accepted";
    }
}

TEST(GltfContainerRobustness, AGlbWhoseDeclaredLengthsDisagreeWithItsBytesIsRefused)
{
    // Three ways the same lie is told, and each one, believed, produces a read past the end of the
    // buffer: a total length longer than the file, a JSON chunk length longer than what follows,
    // and a file truncated after its header.
    {
        std::vector<std::uint8_t> bytes = ValidGlb();
        WriteU32(bytes, 8, static_cast<std::uint32_t>(bytes.size() + 64));
        EXPECT_FALSE(AcceptsGlb(bytes)) << "a total length past the end of the file was accepted";
    }
    {
        std::vector<std::uint8_t> bytes = ValidGlb();
        WriteU32(bytes, 12, static_cast<std::uint32_t>(bytes.size()));
        EXPECT_FALSE(AcceptsGlb(bytes)) << "a chunk length past the end of the file was accepted";
    }
    {
        std::vector<std::uint8_t> bytes = ValidGlb();
        bytes.resize(16);
        EXPECT_FALSE(AcceptsGlb(bytes)) << "a truncated file was accepted";
    }
}

TEST(GltfContainerRobustness, AValidGlbIsStillAcceptedAfterAllThatMutating)
{
    // The control every refusal test needs: the unmutated file must load, or "refused" above
    // would prove only that the reader refuses everything.
    EXPECT_TRUE(AcceptsGlb(ValidGlb()));
}

// --- GLTF-031: buffer.byteLength ------------------------------------------------------------------

TEST(GltfContainerRobustness, ABufferShorterThanItsOwnViewsIsRefusedRatherThanReadPast)
{
    // §5.9: `byteLength` is the buffer's real size, and a bufferView must fit inside it. A view
    // that runs past is the most direct out-of-bounds read a glTF file can ask for -- and the
    // accessor built on it decodes silently, because the bytes it reads are simply whatever
    // follows the allocation.
    // Asserted through `ValidateGltfEXT`, which is what both loaders run, rather than through the
    // parser alone: cgltf's parse step resolves indices to pointers and does not bound-check the
    // views, so "the file parsed" is not the question this row asks.
    Parsed doc;
    ASSERT_TRUE(ParseText(doc, R"GLTF({
  "asset": { "version": "2.0" },
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 } } ] } ],
  "buffers": [ { "byteLength": 12, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAA" } ],
  "bufferViews": [ { "buffer": 0, "byteOffset": 0, "byteLength": 36 } ],
  "accessors": [ { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3" } ]
})GLTF"));
    std::vector<std::string> warnings;
    EXPECT_THROW(ValidateGltfEXT(doc.data, "short-buffer.gltf", warnings), std::runtime_error)
        << "a bufferView longer than its own buffer was accepted";
}

TEST(GltfContainerRobustness, ABufferViewStartingPastTheEndOfItsBufferIsRefused)
{
    // The same lie told through the offset rather than the length. Worth its own case because an
    // implementation that checks `byteOffset + byteLength <= buffer.byteLength` in 32-bit
    // arithmetic can be talked past it by an offset near the type's maximum.
    Parsed doc;
    ASSERT_TRUE(ParseText(doc, R"GLTF({
  "asset": { "version": "2.0" },
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 } } ] } ],
  "buffers": [ { "byteLength": 36, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" } ],
  "bufferViews": [ { "buffer": 0, "byteOffset": 4294967280, "byteLength": 36 } ],
  "accessors": [ { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3" } ]
})GLTF"));
    std::vector<std::string> warnings;
    EXPECT_THROW(ValidateGltfEXT(doc.data, "far-offset.gltf", warnings), std::runtime_error)
        << "a bufferView starting past the end of its buffer was accepted";
}

// plan_gltf.md GLTF-039. The offset test above is the reachable half of the same class; this is
// the half no .gltf file can express, and it is asserted directly rather than left unexercised.
//
// cgltf parses every JSON integer through `atoll`, so a value above `LLONG_MAX` saturates and two
// saturated values still sum to `2^64 - 2` -- a file cannot make `byteOffset + byteLength` wrap.
// The guard is not about what one parser accepts, though: it is about the computation being safe
// for any caller, and a check nothing exercises is a check that has already stopped working. So
// the range is set on a parsed document, which is exactly the state ValidateGltfEXT is handed.
TEST(GltfContainerRobustness, ValidationRejectsABufferViewWhoseRangeWraps)
{
    Parsed doc;
    ASSERT_TRUE(ParseText(doc, R"GLTF({
  "asset": { "version": "2.0" },
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 } } ] } ],
  "buffers": [ { "byteLength": 36, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" } ],
  "bufferViews": [ { "buffer": 0, "byteOffset": 0, "byteLength": 36 } ],
  "accessors": [ { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3" } ]
})GLTF"));
    std::vector<std::string> warnings;
    // The control: as parsed, the document is sound, so a failure below is the wrap and not the
    // file.
    ASSERT_NO_THROW(ValidateGltfEXT(doc.data, "wrapping-view.gltf", warnings));

    // 36 bytes starting 36 short of the address space: the sum is exactly 0 in size_t, so every
    // unchecked `offset + size <= buffer.size` test in existence passes.
    //
    // The offset is chosen 4-ALIGNED as well, and that is not incidental: §3.6.2.4's alignment
    // check now runs before this one (GLTF-040's second finding), so a misaligned offset would be
    // refused for the wrong reason and this test would pass without exercising the wrap guard at
    // all -- which is what it did on the first run after the reorder.
    doc.data->buffer_views[0].offset = std::numeric_limits<std::size_t>::max() - 35;
    doc.data->buffer_views[0].size = 36;
    try
    {
        ValidateGltfEXT(doc.data, "wrapping-view.gltf", warnings);
        ADD_FAILURE() << "a bufferView whose byteOffset + byteLength wraps was accepted";
    }
    catch (const std::runtime_error& e)
    {
        EXPECT_NE(std::string::npos, std::string(e.what()).find("overflow"))
            << "the refusal does not say what is wrong: " << e.what();
    }
    // Left in the wrapped state deliberately -- cgltf_free walks the buffers, not the views, so
    // nothing here reads the range again.
}

// --- GLTF-048: two primitives sharing one bufferView ----------------------------------------------

TEST(GltfContainerRobustness, TwoPrimitivesReadingOneBufferViewEachGetTheirOwnData)
{
    // Sharing a bufferView between accessors is ordinary glTF -- an exporter interleaves or packs
    // two meshes into one view and distinguishes them by `byteOffset`. The failure worth testing
    // is a decoder that caches by *view* rather than by accessor: the second primitive then gets
    // the first one's vertices, which is a mesh that renders, in the wrong shape.
    //
    // The two accessors below start 36 bytes apart in one 72-byte view, and their positions have
    // no value in common.
    Parsed doc;
    ASSERT_TRUE(ParseText(doc, R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "mesh": 0 } ],
  "meshes": [ { "primitives": [
    { "attributes": { "POSITION": 0 } },
    { "attributes": { "POSITION": 1 } }
  ] } ],
  "buffers": [ { "byteLength": 72, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAACgQAAAAAAAAAAAAADAQAAAAAAAAAAAAACgQAAAgD8AAAAA" } ],
  "bufferViews": [ { "buffer": 0, "byteOffset": 0, "byteLength": 72 } ],
  "accessors": [
    { "bufferView": 0, "byteOffset": 0,  "componentType": 5126, "count": 3, "type": "VEC3",
      "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 0, "byteOffset": 36, "componentType": 5126, "count": 3, "type": "VEC3",
      "min": [5,0,0], "max": [6,1,0] }
  ]
})GLTF"));

    const MeshOut first =
        ExtractMesh(doc.data, doc.data->meshes[0].primitives[0], "first", nullptr, 1.0f);
    const MeshOut second =
        ExtractMesh(doc.data, doc.data->meshes[0].primitives[1], "second", nullptr, 1.0f);

    float firstX = 0.0f;
    float secondX = 0.0f;
    std::memcpy(&firstX, first.vertexBytes.data() + static_cast<std::size_t>(first.stride),
                sizeof(firstX));
    std::memcpy(&secondX, second.vertexBytes.data() + static_cast<std::size_t>(second.stride),
                sizeof(secondX));
    EXPECT_FLOAT_EQ(1.0f, firstX);
    EXPECT_FLOAT_EQ(6.0f, secondX)
        << "the second primitive got the first one's vertices -- something is keyed on the "
           "bufferView rather than the accessor";
}

// --- GLTF-351: EXT_meshopt_compression, refused rather than read as undefined bytes -------------

TEST(GltfContainerRobustness, AMeshoptCompressedBufferViewIsRefusedRatherThanReadUndecoded)
{
    // The trap this closes is that cgltf makes the extension look supported. It parses the
    // compression block and validates its metadata thoroughly -- stride, count, mode, filter, and
    // that the compressed range fits its buffer -- so `cgltf_parse` and `cgltf_validate` both
    // succeed. What it does not do is DECODE: that needs meshopt_decodeVertexBuffer and friends,
    // supplied by the caller, which CNA does not provide.
    //
    // Without a decoder `cgltf_buffer_view_data` falls through to `buffer->data + view->offset`,
    // so every accessor over such a view reads whatever bytes happen to be there. Not an error,
    // not empty -- undefined geometry that renders mangled or invisible with nothing to say why,
    // which is exactly the outcome this row's acceptance forbids.
    //
    // Declared as *used* rather than *required* on purpose: this is the one place the GLTF-024
    // "an ignorable extension is a warning" rule does not apply, because an ignorable extension is
    // one whose absence leaves the file readable, and this one relocates the geometry.
    static const char* kMeshoptGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "extensionsUsed": [ "EXT_meshopt_compression" ],
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ { "name": "CompressedTri", "primitives": [
      { "attributes": { "POSITION": 0 }, "mode": 4 } ] } ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
      "min": [0, 0, 0], "max": [1, 1, 0] }
  ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": 36, "byteStride": 12,
      "extensions": { "EXT_meshopt_compression": {
          "buffer": 0, "byteOffset": 0, "byteLength": 36,
          "byteStride": 12, "count": 3, "mode": "ATTRIBUTES" } } }
  ],
  "buffers": [ { "byteLength": 36, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA==" } ]
})GLTF";

    Parsed parsed;
    ASSERT_TRUE(ParseText(parsed, kMeshoptGltf))
        << "the file must PARSE -- if it did not, the refusal below would prove nothing about "
           "meshopt and everything about a malformed document";

    std::vector<std::string> warnings;
    try
    {
        CNA::Internal::GltfImport::ValidateGltfEXT(parsed.data, "meshopt-probe", warnings);
        FAIL() << "a meshopt-compressed bufferView was accepted; every accessor over it would read "
                  "undefined bytes";
    }
    catch (const std::runtime_error& e)
    {
        const std::string what = e.what();
        EXPECT_NE(std::string::npos, what.find("EXT_meshopt_compression")) << what;
        // Actionable: which bufferView, and why refusing beats importing.
        EXPECT_NE(std::string::npos, what.find("bufferView")) << what;
        EXPECT_NE(std::string::npos, what.find("undefined bytes")) << what;
    }
}

TEST(GltfContainerRobustness, AnOrdinaryBufferViewIsNotMistakenForACompressedOne)
{
    // The control, and not a formality: the check walks every bufferView in the file, so a
    // predicate reading the wrong field would refuse the entire corpus. Asserted over the whole
    // corpus rather than one document, since that is the blast radius.
    std::size_t validated = 0;
    for (const std::string& id : CorpusFixtureIds())
    {
        const CnaTest::GltfOracle::LoadedFixture fixture(id);
        if (!fixture.Ok()) { continue; }
        std::vector<std::string> warnings;
        try
        {
            CNA::Internal::GltfImport::ValidateGltfEXT(&fixture.Data(), id, warnings);
            ++validated;
        }
        catch (const std::runtime_error& e)
        {
            EXPECT_EQ(std::string::npos, std::string(e.what()).find("meshopt"))
                << id << " was refused as meshopt-compressed: " << e.what();
        }
    }
    EXPECT_GT(validated, 0u) << "no fixture validated at all -- the control proved nothing";
}

// --- plan_gltf.md GLTF-040: the container/buffer layer under mutation ------------------------------

namespace
{
    /// The GLB seeds the fuzz starts from: the whole `bad-*` group, plus one valid asset.
    ///
    /// The malformed group is the seed the row asks for, and it is the right one -- those files
    /// already sit on the boundary the checks defend. On its own it would be a weak fuzz, though:
    /// a mutation of a file that is already refused is usually still refused, and never reaches
    /// the code past validation. Valid assets are seeded alongside them -- and weighted, because
    /// they are where the mutants that get *deep* come from -- with three different shapes, so
    /// what survives to extraction is not always the same one primitive.
    std::vector<std::vector<std::uint8_t>> FuzzSeeds()
    {
        std::vector<std::vector<std::uint8_t>> seeds;
        for (const std::string& id : CorpusFixtureIds())
        {
            if (id.rfind("bad-", 0) != 0) { continue; }
            seeds.push_back(ReadBytes(CorpusDirectory() / (id + ".glb")));
        }
        for (const char* id : {"xf-identity", "skin-mesh-node-transform", "mat-alpha-mask-cutoff"})
        {
            std::vector<std::uint8_t> valid = ReadBytes(CorpusDirectory() / (std::string(id) + ".glb"));
            seeds.push_back(valid);
            seeds.push_back(std::move(valid));
        }
        return seeds;
    }

    /// The u32 values a length or offset field is worth being set to: the boundaries an unchecked
    /// `+`/`*` behaves differently at, rather than uniform noise, which almost never lands on one.
    constexpr std::uint32_t kInterestingU32[] = {
        0u, 1u, 3u, 4u, 12u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFCu, 0xFFFFFFFFu};

    /// Applies one structured mutation in place. Every kind targets something a reader must
    /// decide about -- a length, an alignment, a chunk boundary, the JSON text itself -- because a
    /// mutation that only perturbs vertex data proves nothing about the container.
    void MutateOnce(std::vector<std::uint8_t>& bytes, std::mt19937& rng)
    {
        if (bytes.empty()) { return; }
        switch (rng() % 5)
        {
            case 0:  // one bit, anywhere
                bytes[rng() % bytes.size()] ^= static_cast<std::uint8_t>(1u << (rng() % 8));
                break;
            case 1:  // truncation, the commonest real-world corruption
                bytes.resize(rng() % bytes.size());
                break;
            case 2:  // a length or offset field, set to a boundary value
            {
                if (bytes.size() < 4) { break; }
                const std::size_t offset = (rng() % (bytes.size() / 4)) * 4;
                WriteU32(bytes, offset,
                          kInterestingU32[rng() % (sizeof(kInterestingU32) / sizeof(*kInterestingU32))]);
                break;
            }
            case 3:  // shift everything after a point out of alignment
            {
                const std::size_t at = rng() % bytes.size();
                bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(at),
                              static_cast<std::uint8_t>(rng() % 256));
                break;
            }
            default:  // a JSON-significant byte, to reach the parser's own error paths
            {
                static const char kJsonBytes[] = {'{', '}', '[', ']', '"', ',', ':', '0', '\\'};
                bytes[rng() % bytes.size()] =
                    static_cast<std::uint8_t>(kJsonBytes[rng() % sizeof(kJsonBytes)]);
                break;
            }
        }
    }
}

// A mutated container must be refused by name or handled, never crash and never throw something a
// caller cannot catch deliberately.
//
// The property is deliberately not "every mutant is rejected": a bit flipped in a vertex's
// mantissa produces a perfectly valid file with slightly different geometry, and demanding
// rejection would be demanding that CNA reject files it should load. What is asserted is the
// contract a caller can actually rely on -- the loader either succeeds or throws a
// `std::runtime_error` naming the problem. `std::length_error` from inside an allocator, which is
// what an unguarded `count * stride` produces (`GLTF-039`), is a failure of that contract even
// though it is technically an exception.
//
// Seeded from a stated constant, because a fuzz whose failures cannot be reproduced is a flake
// generator. This is also the test `GLTF-036`'s sanitizer CI job exercises for free: it runs
// `--gtest_filter='Gltf*'` under ASan and UBSan, so a mutation that reads out of bounds or
// overflows a signed integer is reported there rather than only when it happens to segfault.
TEST(GltfContainerRobustness, MutatedContainersAreRefusedByNameRatherThanCrashing)
{
    using namespace CNA::Internal::GltfImport;

    const std::vector<std::vector<std::uint8_t>> seeds = FuzzSeeds();
    ASSERT_GE(seeds.size(), 2u) << "the corpus has no bad-* group to seed from";

    std::mt19937 rng(20260812u);
    std::size_t parsed = 0;
    std::size_t validated = 0;
    std::size_t extracted = 0;
    std::size_t namedRefusals = 0;

    for (int iteration = 0; iteration < 3000; ++iteration)
    {
        std::vector<std::uint8_t> bytes = seeds[static_cast<std::size_t>(rng()) % seeds.size()];
        const int mutations = 1 + static_cast<int>(rng() % 3);
        for (int m = 0; m < mutations; ++m) { MutateOnce(bytes, rng); }
        if (bytes.empty()) { continue; }
        SCOPED_TRACE("iteration " + std::to_string(iteration) + ", " +
                      std::to_string(bytes.size()) + " bytes");

        cgltf_options options{};
        cgltf_data* data = nullptr;
        if (cgltf_parse(&options, bytes.data(), bytes.size(), &data) != cgltf_result_success)
        {
            continue;  // Refused by the parser, which is a refusal by name.
        }
        struct Free { cgltf_data* d; ~Free() { cgltf_free(d); } } freeGuard{data};
        if (cgltf_load_buffers(&options, data, ".") != cgltf_result_success) { continue; }
        ++parsed;

        try
        {
            std::vector<std::string> warnings;
            ValidateGltfEXT(data, "fuzz.glb", warnings);
        }
        catch (const std::runtime_error&)
        {
            ++namedRefusals;
            continue;
        }
        catch (const std::exception& e)
        {
            ADD_FAILURE() << "validation threw something other than a runtime_error, which a "
                              "caller cannot distinguish from a bug in itself: " << e.what();
            continue;
        }
        ++validated;

        // Past validation, every remaining stage must hold the same contract. Extraction is where
        // the counts and strides are actually used, so it is the half a container mutation reaches
        // last and damages most quietly.
        try
        {
            const SceneGraphOut scene = BuildSceneGraph(data);
            (void)scene;
            for (cgltf_size m = 0; m < data->meshes_count; ++m)
            {
                for (cgltf_size pi = 0; pi < data->meshes[m].primitives_count; ++pi)
                {
                    (void)ExtractMesh(data, data->meshes[m].primitives[pi], "fuzz", nullptr, 1.0f);
                }
            }
            ++extracted;
        }
        catch (const std::runtime_error&)
        {
            ++namedRefusals;
        }
        catch (const std::exception& e)
        {
            ADD_FAILURE() << "extraction threw something other than a runtime_error: " << e.what();
        }
    }

    // How far the run actually got, recorded rather than described: a fuzz that reports only
    // "passed" cannot be told apart from one whose mutations all died at the front door.
    RecordProperty("mutants", 3000);
    RecordProperty("parsed", static_cast<int>(parsed));
    RecordProperty("validated", static_cast<int>(validated));
    RecordProperty("extracted", static_cast<int>(extracted));
    RecordProperty("namedRefusals", static_cast<int>(namedRefusals));

    // Floors, so a fuzz that stopped reaching the code cannot report success. A mutation set that
    // never produced a parseable document would exercise nothing but cgltf's front door, and one
    // that never produced a refusal would mean the checks are not being reached at all.
    EXPECT_GT(parsed, 200u) << "almost nothing survived parsing -- the mutations are too coarse to "
                               "reach anything past cgltf's front door";
    EXPECT_GT(namedRefusals, 50u)
        << "too few mutants were refused by name, so this run says little about the checks";
    EXPECT_GT(extracted, 20u)
        << "too few mutants reached extraction, so the deepest half went unexercised";
}

// The regression test for what the fuzz above found on its first widened run, kept as its own
// case so the finding does not depend on a seed continuing to reach it.
//
// §3.6.2.1 lets an accessor omit its `bufferView`, in which case it reads as zeros. Nothing in the
// file then bounds its count -- unlike `bad-accessor-count-overflow`, whose span guard has a view
// to check against -- so a count of 2^62 asks the reader to materialise 55 exabytes of zeros that
// the document never shipped. The allocation failed as `std::length_error` from inside the
// allocator, which names neither the file nor the accessor and is not what any caller of an
// importer catches.
TEST(GltfContainerRobustness, AnAccessorWithNoBufferViewAndAnAbsurdCountIsRefusedByName)
{
    Parsed doc;
    ASSERT_TRUE(ParseText(doc, R"GLTF({
  "asset": { "version": "2.0" },
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 } } ] } ],
  "buffers": [ { "byteLength": 36, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" } ],
  "bufferViews": [ { "buffer": 0, "byteOffset": 0, "byteLength": 36 } ],
  "accessors": [ { "componentType": 5126, "count": 4611686018427387904, "type": "VEC3" } ]
})GLTF"));

    try
    {
        (void)CNA::Internal::GltfImport::ExtractMesh(
            doc.data, doc.data->meshes[0].primitives[0], "zero-base", nullptr, 1.0f);
        ADD_FAILURE() << "an accessor demanding 55 exabytes of implicit zeros was extracted";
    }
    catch (const std::runtime_error& e)
    {
        const std::string message = e.what();
        EXPECT_NE(std::string::npos, message.find("zero-base"))
            << "the refusal does not name what failed: " << message;
        EXPECT_NE(std::string::npos, message.find("4611686018427387904"))
            << "the refusal does not name the count it refused: " << message;
    }
    catch (const std::exception& e)
    {
        ADD_FAILURE() << "refused with an exception a caller cannot catch deliberately: "
                      << e.what();
    }
}

// --- the version gate, asserted where it actually happens ----------------------------------------

TEST(GltfContainerRobustness, AGltf10DocumentIsRefusedAtParseByTheVendoredReader)
{
    // glTF 1.0 is not a subset of 2.0: different material model, different animation encoding, and
    // shader-based techniques 2.0 removed entirely. A reader that ignores `asset.version` and
    // parses what it recognises imports a real 1.0 asset as a PARTIAL model with no diagnostic --
    // which is worse than refusing a file it cannot read.
    //
    // This lives here rather than in the corpus because the vendored cgltf refuses such a file at
    // *parse*, and a corpus rejection fixture must parse: one that does not proves nothing about
    // CNA's own checks. The body below is otherwise valid glTF 2.0 on purpose, so what is refused
    // is unambiguously the version and not the content.
    Parsed doc;
    EXPECT_FALSE(ParseText(doc, R"GLTF({
  "asset": { "version": "1.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 } } ] } ],
  "buffers": [ { "byteLength": 36, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAACAPwAAAAA=" } ],
  "bufferViews": [ { "buffer": 0, "byteOffset": 0, "byteLength": 36 } ],
  "accessors": [ { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3" } ]
})GLTF"))
        << "a document declaring asset.version 1.0 was parsed as glTF 2.0";

    // The control: the identical document at version 2.0 parses, so the refusal above is the
    // version rather than anything else about the file.
    Parsed modern;
    EXPECT_TRUE(ParseText(modern, R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 } } ] } ],
  "buffers": [ { "byteLength": 36, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAACAPwAAAAA=" } ],
  "bufferViews": [ { "buffer": 0, "byteOffset": 0, "byteLength": 36 } ],
  "accessors": [ { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3" } ]
})GLTF"))
        << "the 2.0 control does not parse either, so the version test above proves nothing";
}

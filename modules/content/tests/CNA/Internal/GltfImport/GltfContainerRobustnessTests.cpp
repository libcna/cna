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
#include <string>
#include <vector>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "GltfFixtureCorpus.hpp"

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

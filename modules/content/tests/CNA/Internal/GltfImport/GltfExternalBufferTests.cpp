// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-028 / GLTF-030: a glTF file whose buffer lives in a separate `.bin`.
//
// This is the shape most exported glTF actually has -- a `.gltf` next to a `.bin`, sometimes next
// to a `textures/` directory. `gltf-external-bin`, `gltf-data-uri-bin`, and
// `gltf-uri-percent-encoded` are the committed positive witnesses, all generated from the same
// geometry shape with their sidecars hashed into the corpus manifest.
//
// `GltfUriContainment` tests the resolver directly, which is where the containment *decision* is
// made. What it cannot show is that the decision is reached at all on the ordinary path: a loader
// that resolved the URI correctly and then read the file from the wrong directory, or never read
// it, would satisfy every one of those tests. The scratch trees remain for negative and
// deliberately nested cases; the committed fixtures make the positive source forms participate in
// every L1-L5 corpus sweep and in the `.gltf`/`.glb` twin checks.

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "GltfFixtureCorpus.hpp"

using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Model;
using CnaTest::GltfOracle::CorpusDirectory;
using CnaTest::GltfOracle::LoadedFixture;
using CnaTest::GltfOracle::Path;
using CnaTest::GltfOracle::StringOr;

namespace
{
    /// A scratch content directory that deletes itself.
    class ScratchContent
    {
    public:
        ScratchContent()
            : root_(std::filesystem::temp_directory_path() /
                    ("cna_gltf_external_bin_" +
                     std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(root_);
        }
        ~ScratchContent()
        {
            std::error_code ec;
            std::filesystem::remove_all(root_, ec);
        }
        ScratchContent(const ScratchContent&) = delete;
        ScratchContent& operator=(const ScratchContent&) = delete;

        [[nodiscard]] const std::filesystem::path& Root() const { return root_; }

    private:
        std::filesystem::path root_;
    };

    /// The buffer every document below points at: three positions and three 16-bit indices.
    ///
    /// The positions are distinct and non-trivial so "the buffer was read" is distinguishable from
    /// "a zeroed buffer of the right size was produced", which is what a resolver that found no
    /// file but did not say so would leave behind.
    std::vector<std::uint8_t> BufferBytes()
    {
        const float positions[9] = {0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 3.0f, 0.0f};
        const std::uint16_t indices[3] = {0, 1, 2};
        std::vector<std::uint8_t> bytes(sizeof(positions) + sizeof(indices));
        std::memcpy(bytes.data(), positions, sizeof(positions));
        std::memcpy(bytes.data() + sizeof(positions), indices, sizeof(indices));
        return bytes;
    }

    /// A `.gltf` document whose single buffer is the external file named by @p bufferUri.
    std::string DocumentWithExternalBuffer(const std::string& bufferUri)
    {
        return std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ { "name": "ExternalTri", "primitives": [
      { "attributes": { "POSITION": 0 }, "indices": 1, "mode": 4 } ] } ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
      "min": [0, 0, 0], "max": [2, 3, 0] },
    { "bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR" }
  ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 6 }
  ],
  "buffers": [ { "byteLength": 42, "uri": ")GLTF") + bufferUri + R"GLTF(" } ]
})GLTF";
    }

    void WriteText(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream f(path, std::ios::binary);
        f << text;
    }

    void WriteBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    }

    /// Loads @p asset from @p root, returning the error message or "" on success.
    std::string LoadError(const std::filesystem::path& root, const std::string& asset)
    {
        GraphicsDevice gd;
        ContentManager cm(nullptr, root.string());
        cm.setGraphicsDevice(gd);
        try
        {
            Model model = cm.Load<Model>(asset);
            if (model.getMeshesProperty().getCountProperty() == 0) { return "no meshes"; }
            return {};
        }
        catch (const std::exception& e)
        {
            return e.what();
        }
    }
}

// --- GLTF-028: a relative URI resolves against the .gltf's own directory ------------------------

TEST(GltfExternalBuffer, ARelativeBufferUriResolvesAgainstTheGltfDirectoryAndItsDataArrives)
{
    // Against the *document's* directory, not the process's working directory and not the
    // ContentManager's root -- those coincide in almost every test, which is why the buffer here
    // lives in a subdirectory and the asset is loaded by a path that reaches into it.
    ScratchContent content;
    const std::filesystem::path assetDir = content.Root() / "models";
    std::filesystem::create_directories(assetDir);
    WriteText(assetDir / "external.gltf", DocumentWithExternalBuffer("geometry.bin"));
    WriteBytes(assetDir / "geometry.bin", BufferBytes());

    GraphicsDevice gd;
    ContentManager cm(nullptr, content.Root().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("models/external");

    ASSERT_EQ(1, model.getMeshesProperty().getCountProperty());
    const auto& parts = model.getMeshesProperty()[0]->getMeshPartsProperty();
    ASSERT_EQ(1, parts.getCountProperty());
    // The data arrived, not merely a buffer of the right size: a resolver that found nothing and
    // did not say so leaves a zeroed buffer whose vertex count is still three.
    EXPECT_EQ(3, parts[0]->getNumVerticesProperty());
    EXPECT_EQ(1, parts[0]->getPrimitiveCountProperty());
}

TEST(GltfExternalBuffer, CommittedExternalInlineAndEncodedBuffersCarryIdenticalBytes)
{
    const LoadedFixture external("gltf-external-bin");
    const LoadedFixture inlineData("gltf-data-uri-bin");
    const LoadedFixture encoded("gltf-uri-percent-encoded");
    ASSERT_TRUE(external.Ok()) << external.Error();
    ASSERT_TRUE(inlineData.Ok()) << inlineData.Error();
    ASSERT_TRUE(encoded.Ok()) << encoded.Error();
    ASSERT_EQ(1u, static_cast<std::size_t>(external.Data().buffers_count));
    ASSERT_EQ(1u, static_cast<std::size_t>(inlineData.Data().buffers_count));
    ASSERT_EQ(1u, static_cast<std::size_t>(encoded.Data().buffers_count));

    const cgltf_buffer& fromFile = external.Data().buffers[0];
    const cgltf_buffer& fromData = inlineData.Data().buffers[0];
    const cgltf_buffer& fromEncoded = encoded.Data().buffers[0];
    ASSERT_NE(nullptr, fromFile.uri);
    ASSERT_NE(nullptr, fromData.uri);
    ASSERT_NE(nullptr, fromEncoded.uri);
    EXPECT_STREQ("gltf-external-bin.geometry.bin", fromFile.uri);
    EXPECT_EQ(0u, std::string(fromData.uri).find("data:application/octet-stream;base64,"));
    EXPECT_STREQ("gltf-uri-percent-encoded%20geometry.bin", fromEncoded.uri);
    ASSERT_EQ(79u, static_cast<std::size_t>(fromFile.size));
    ASSERT_EQ(fromFile.size, fromData.size);
    ASSERT_EQ(fromFile.size, fromEncoded.size);
    ASSERT_NE(nullptr, fromFile.data);
    ASSERT_NE(nullptr, fromData.data);
    ASSERT_NE(nullptr, fromEncoded.data);
    EXPECT_EQ(0, std::memcmp(fromFile.data, fromData.data, fromFile.size));
    EXPECT_EQ(0, std::memcmp(fromFile.data, fromEncoded.data, fromFile.size));

    const auto& externalContract = Path(external.Expected(), "container.gltf.buffer");
    const auto& dataContract = Path(inlineData.Expected(), "container.gltf.buffer");
    const auto& encodedContract = Path(encoded.Expected(), "container.gltf.buffer");
    EXPECT_EQ("external", StringOr(externalContract, "source", ""));
    EXPECT_EQ("data-uri", StringOr(dataContract, "source", ""));
    EXPECT_EQ("external", StringOr(encodedContract, "source", ""));
    EXPECT_TRUE(std::filesystem::is_regular_file(
        CorpusDirectory() / "gltf-external-bin.geometry.bin"));
    EXPECT_TRUE(std::filesystem::is_regular_file(
        CorpusDirectory() / "gltf-uri-percent-encoded geometry.bin"));
    EXPECT_FALSE(std::filesystem::exists(
        CorpusDirectory() / "gltf-uri-percent-encoded%20geometry.bin"))
        << "the encoded spelling must not exist, or a loader that skips URI decoding can pass";

    // And exercise the ordinary CNA ContentManager path, not just cgltf's test helper. Exact
    // vertex/index bytes for these same fixtures are asserted independently by the L5 sweep.
    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model externalModel = cm.Load<Model>("gltf-external-bin");
    Model dataModel = cm.Load<Model>("gltf-data-uri-bin");
    Model encodedModel = cm.Load<Model>("gltf-uri-percent-encoded");
    ASSERT_EQ(1, externalModel.getMeshesProperty().getCountProperty());
    ASSERT_EQ(1, dataModel.getMeshesProperty().getCountProperty());
    ASSERT_EQ(1, encodedModel.getMeshesProperty().getCountProperty());
    EXPECT_EQ(externalModel.getMeshesProperty()[0]->getMeshPartsProperty()[0]->
                  getNumVerticesProperty(),
              dataModel.getMeshesProperty()[0]->getMeshPartsProperty()[0]->
                  getNumVerticesProperty());
    EXPECT_EQ(externalModel.getMeshesProperty()[0]->getMeshPartsProperty()[0]->
                  getPrimitiveCountProperty(),
              encodedModel.getMeshesProperty()[0]->getMeshPartsProperty()[0]->
                  getPrimitiveCountProperty());
}

TEST(GltfExternalBuffer, AMissingBufferFileFailsWithAMessageNamingIt)
{
    // The failure mode worth guarding is not the exception -- it is a load that "succeeds" with an
    // empty buffer, producing a model whose geometry is silently all zeros. Whatever the loader
    // does here, it must not be that.
    ScratchContent content;
    WriteText(content.Root() / "missing.gltf", DocumentWithExternalBuffer("nowhere.bin"));

    const std::string error = LoadError(content.Root(), "missing");
    ASSERT_FALSE(error.empty())
        << "a document whose buffer file does not exist loaded without complaint";
    // Actionable: an author needs to know it was the buffer, and which one.
    EXPECT_NE(std::string::npos, error.find("missing"))
        << "the diagnostic does not name the asset: " << error;
}

TEST(GltfExternalBuffer, ABufferUriEscapingTheAssetDirectoryIsRefusedOnTheOrdinaryLoadPath)
{
    // GltfUriContainment proves the resolver refuses a traversal. This proves the ordinary load
    // path actually reaches the resolver -- a loader that resolved paths itself and only consulted
    // the containment rule for images would pass every test in that file.
    ScratchContent content;
    const std::filesystem::path assetDir = content.Root() / "models";
    std::filesystem::create_directories(assetDir);
    WriteBytes(content.Root() / "outside.bin", BufferBytes());
    WriteText(assetDir / "escaping.gltf", DocumentWithExternalBuffer("../outside.bin"));

    const std::string error = LoadError(content.Root(), "models/escaping");
    EXPECT_FALSE(error.empty())
        << "a buffer URI reaching outside the asset directory was loaded -- the file it read is "
           "real and readable, which is exactly why nothing else would have noticed";
}

// --- GLTF-030: percent-encoding, for buffers and not only images --------------------------------

TEST(GltfExternalBuffer, APercentEncodedBufferUriResolvesToTheDecodedFileName)
{
    // §3.6.1.1 says a URI is a URI: a space is `%20`, and a file whose name contains one is
    // ordinary on every desktop platform. cgltf decodes image URIs itself, which is exactly the
    // kind of asymmetry that leaves buffers reading a file called "my%20geometry.bin" -- a name
    // that does not exist, from a loader that then reports the file as missing rather than as
    // wrongly spelled.
    ScratchContent content;
    WriteText(content.Root() / "encoded.gltf", DocumentWithExternalBuffer("my%20geometry.bin"));
    WriteBytes(content.Root() / "my geometry.bin", BufferBytes());

    GraphicsDevice gd;
    ContentManager cm(nullptr, content.Root().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("encoded");

    ASSERT_EQ(1, model.getMeshesProperty().getCountProperty());
    const auto& parts = model.getMeshesProperty()[0]->getMeshPartsProperty();
    ASSERT_EQ(1, parts.getCountProperty());
    EXPECT_EQ(3, parts[0]->getNumVerticesProperty());
}

TEST(GltfExternalBuffer, TheUndecodedSpellingIsNotAlsoAccepted)
{
    // The control, and the reason the test above is not satisfied by a loader that ignores
    // percent-encoding entirely: with the file actually named `my%20geometry.bin`, a
    // non-decoding loader would succeed and a decoding one must fail. Asserting the decode
    // direction alone cannot tell those apart.
    ScratchContent content;
    WriteText(content.Root() / "literal.gltf", DocumentWithExternalBuffer("my%20geometry.bin"));
    WriteBytes(content.Root() / "my%20geometry.bin", BufferBytes());

    EXPECT_FALSE(LoadError(content.Root(), "literal").empty())
        << "the URI was used verbatim rather than decoded, so a file whose real name contains a "
           "space would never be found";
}

TEST(GltfExternalBuffer, TheSameGeometryLoadsIdenticallyFromAnExternalBufferAndADataUri)
{
    // Where the buffer lives is a container decision and must not change a single vertex. Asserted
    // because the two take genuinely different code paths -- one reads a file, one decodes base64
    // -- and a divergence between them would show up as geometry that differs depending on how the
    // exporter happened to be configured.
    ScratchContent content;
    WriteText(content.Root() / "external.gltf", DocumentWithExternalBuffer("geometry.bin"));
    WriteBytes(content.Root() / "geometry.bin", BufferBytes());

    // The same bytes, inline. Base64 of the 42-byte buffer above.
    const std::vector<std::uint8_t> bytes = BufferBytes();
    static const char* kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string base64;
    for (std::size_t i = 0; i < bytes.size(); i += 3)
    {
        const std::uint32_t a = bytes[i];
        const std::uint32_t b = (i + 1 < bytes.size()) ? bytes[i + 1] : 0;
        const std::uint32_t c = (i + 2 < bytes.size()) ? bytes[i + 2] : 0;
        const std::uint32_t triple = (a << 16) | (b << 8) | c;
        base64 += kAlphabet[(triple >> 18) & 0x3F];
        base64 += kAlphabet[(triple >> 12) & 0x3F];
        base64 += (i + 1 < bytes.size()) ? kAlphabet[(triple >> 6) & 0x3F] : '=';
        base64 += (i + 2 < bytes.size()) ? kAlphabet[triple & 0x3F] : '=';
    }
    WriteText(content.Root() / "inline.gltf",
              DocumentWithExternalBuffer("data:application/octet-stream;base64," + base64));

    GraphicsDevice gd;
    ContentManager cm(nullptr, content.Root().string());
    cm.setGraphicsDevice(gd);
    Model fromFile = cm.Load<Model>("external");
    Model fromData = cm.Load<Model>("inline");

    ASSERT_EQ(fromFile.getMeshesProperty().getCountProperty(),
              fromData.getMeshesProperty().getCountProperty());
    const auto& filePart = fromFile.getMeshesProperty()[0]->getMeshPartsProperty()[0];
    const auto& dataPart = fromData.getMeshesProperty()[0]->getMeshPartsProperty()[0];
    EXPECT_EQ(filePart->getNumVerticesProperty(), dataPart->getNumVerticesProperty());
    EXPECT_EQ(filePart->getPrimitiveCountProperty(), dataPart->getPrimitiveCountProperty());
    EXPECT_EQ(filePart->getVertexOffsetProperty(), dataPart->getVertexOffsetProperty());
    EXPECT_EQ(filePart->getStartIndexProperty(), dataPart->getStartIndexProperty());
}

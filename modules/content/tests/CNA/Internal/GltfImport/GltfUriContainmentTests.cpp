// SPDX-License-Identifier: MS-PL
//
// plans/plan_gltf.md GLTF-032 / GLTF-198: a glTF file may only reference files inside its own directory.
//
// These fixtures are written to a scratch directory rather than generated into the corpus, and
// that is deliberate rather than a shortcut. What is under test is *a path on disk* -- a URI that
// escapes only means something relative to a real directory with a real file outside it, and one
// of the cases needs a symbolic link, which no `.gltf` document can express. The corpus generator
// emits self-contained, byte-reproducible assets; a fixture whose whole point is what lies outside
// the asset does not belong in it.
//
// The tests are written against `ResolveExternalUriEXT`/`ValidateExternalUriContainmentEXT`
// because that is where the decision is made. All three entry points -- ContentManager's
// `ReadGltfModel`, the offline `gltf_to_cnj` tool, and `ExtractImage` -- call into it, and none of
// them adds a rule of its own.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <system_error>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"

using namespace CNA::Internal::GltfImport;

namespace
{
    /// A scratch tree shaped like a real asset drop: an `asset/` directory holding the `.gltf`,
    /// a `secret.bin` OUTSIDE it that nothing in the asset may reach, and a sibling `asset-evil/`
    /// whose name shares `asset`'s string prefix without being inside it.
    class ContainmentScratch
    {
    public:
        ContainmentScratch()
            : root_(std::filesystem::temp_directory_path()
                    / ("cna_gltf_uri_containment_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(root_ / "asset" / "textures");
            std::filesystem::create_directories(root_ / "asset-evil");
            Write(root_ / "secret.bin", "outside");
            Write(root_ / "asset-evil" / "secret.bin", "sibling");
            Write(root_ / "asset" / "textures" / "base.png", "inside");
        }
        ~ContainmentScratch()
        {
            std::error_code ec;
            std::filesystem::remove_all(root_, ec);
        }
        ContainmentScratch(const ContainmentScratch&) = delete;
        ContainmentScratch& operator=(const ContainmentScratch&) = delete;

        [[nodiscard]] const std::filesystem::path& Root() const { return root_; }
        [[nodiscard]] std::filesystem::path AssetDir() const { return root_ / "asset"; }

        static void Write(const std::filesystem::path& path, const std::string& text)
        {
            std::ofstream f(path, std::ios::binary);
            f << text;
        }

    private:
        std::filesystem::path root_;
    };

    /// Runs the resolver and returns the message it threw, or an empty string if it accepted.
    std::string RefusalFor(const std::filesystem::path& dir, const std::string& uri,
                           const char* what = "buffer")
    {
        try
        {
            ResolveExternalUriEXT(dir, uri, what);
            return {};
        }
        catch (const std::exception& e)
        {
            return e.what();
        }
    }

    /// Parses a `.gltf` document written into the scratch asset directory. The caller owns nothing:
    /// the returned pointer is freed by the guard the test holds.
    struct ParsedGltf
    {
        cgltf_data* data = nullptr;
        ~ParsedGltf() { if (data != nullptr) { cgltf_free(data); } }
        ParsedGltf() = default;
        ParsedGltf(const ParsedGltf&) = delete;
        ParsedGltf& operator=(const ParsedGltf&) = delete;
    };

    bool ParseInto(ParsedGltf& out, const std::filesystem::path& assetDir, const std::string& json)
    {
        const std::filesystem::path gltfPath = assetDir / "fixture.gltf";
        ContainmentScratch::Write(gltfPath, json);
        cgltf_options options{};
        return cgltf_parse_file(&options, gltfPath.string().c_str(), &out.data) ==
               cgltf_result_success;
    }

    /// A minimal but structurally valid document whose single buffer is external, so the URI under
    /// test is the only thing that varies between cases.
    std::string DocumentWithBufferUri(const std::string& uri)
    {
        return std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "buffers": [ { "byteLength": 7, "uri": ")GLTF") + uri + R"GLTF(" } ]
})GLTF";
    }

    std::string DocumentWithImageUri(const std::string& uri)
    {
        return std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "images": [ { "uri": ")GLTF") + uri + R"GLTF(" } ]
})GLTF";
    }
}

// --- What must still work --------------------------------------------------------------------

TEST(GltfUriContainment, AnOrdinaryRelativeUriResolvesInsideTheAssetDirectory)
{
    const ContainmentScratch scratch;
    const std::filesystem::path resolved =
        ResolveExternalUriEXT(scratch.AssetDir(), "textures/base.png", "image");
    EXPECT_EQ(scratch.AssetDir() / "textures" / "base.png", resolved);
}

TEST(GltfUriContainment, PercentEncodingIsDecodedForAContainedUri)
{
    // A space in a file name is percent-encoded per §3.1's URI rules, and decoding it is what
    // GLTF-030 asks for -- containment must not come at the cost of no longer finding the file.
    const ContainmentScratch scratch;
    ContainmentScratch::Write(scratch.AssetDir() / "textures" / "base color.png", "inside");
    const std::filesystem::path resolved =
        ResolveExternalUriEXT(scratch.AssetDir(), "textures/base%20color.png", "image");
    EXPECT_EQ(scratch.AssetDir() / "textures" / "base color.png", resolved);
    EXPECT_TRUE(std::filesystem::exists(resolved));
}

TEST(GltfUriContainment, ATraversalThatComesBackInsideIsAccepted)
{
    // 'textures/../textures/base.png' never leaves the directory, so there is nothing to refuse.
    // Refusing every '..' outright would be a cheaper check and a wrong one.
    const ContainmentScratch scratch;
    const std::filesystem::path resolved =
        ResolveExternalUriEXT(scratch.AssetDir(), "textures/../textures/base.png", "image");
    EXPECT_EQ(scratch.AssetDir() / "textures" / "base.png", resolved);
}

// --- What must be refused ----------------------------------------------------------------------

TEST(GltfUriContainment, AParentTraversalIsRefused)
{
    const ContainmentScratch scratch;
    const std::string message = RefusalFor(scratch.AssetDir(), "../secret.bin");
    ASSERT_FALSE(message.empty()) << "a URI reaching the parent directory was accepted";
    EXPECT_NE(message.find("outside the asset's own directory"), std::string::npos) << message;
    EXPECT_NE(message.find("../secret.bin"), std::string::npos)
        << "the message must name the URI it refused: " << message;
}

TEST(GltfUriContainment, ATraversalHiddenMidPathIsRefused)
{
    // No single component of 'textures/../../secret.bin' looks wrong; only the whole path does,
    // which is why the check is lexical normalisation rather than a component scan.
    const ContainmentScratch scratch;
    EXPECT_FALSE(RefusalFor(scratch.AssetDir(), "textures/../../secret.bin").empty());
}

TEST(GltfUriContainment, ADeepTraversalIsRefused)
{
    const ContainmentScratch scratch;
    EXPECT_FALSE(RefusalFor(scratch.AssetDir(), "../../../../etc/passwd").empty());
}

TEST(GltfUriContainment, APercentEncodedTraversalIsRefused)
{
    // The decisive ordering test: percent-decoding happens BEFORE containment, so '%2e%2e%2f' is
    // seen for what it is. A check that ran the other way round would let this straight through.
    const ContainmentScratch scratch;
    EXPECT_FALSE(RefusalFor(scratch.AssetDir(), "%2e%2e%2fsecret.bin").empty());
    EXPECT_FALSE(RefusalFor(scratch.AssetDir(), "%2E%2E/secret.bin").empty());
}

TEST(GltfUriContainment, AnAbsolutePathIsRefusedAndSaysSo)
{
    const ContainmentScratch scratch;
    // URI '/' remains absolute under Windows path semantics too; a percent-encoded slash must be
    // classified after decoding, and '\\' is the equivalent Windows root-relative spelling.
    for (const char* uri : {"/etc/passwd", "%2Fetc/passwd", "\\Windows\\win.ini"})
    {
        SCOPED_TRACE(uri);
        const std::string message = RefusalFor(scratch.AssetDir(), uri);
        ASSERT_FALSE(message.empty());
        EXPECT_NE(message.find("absolute path"), std::string::npos) << message;
    }
}

TEST(GltfUriContainment, ADriveAbsolutePathIsRefusedOnEveryPlatform)
{
    // 'C:/Windows/...' is absolute on Windows and a relative path named 'C:' on Linux. The answer
    // must not depend on which std::filesystem is compiled in, so it is decided before the path
    // type is consulted at all.
    const ContainmentScratch scratch;
    const std::string message = RefusalFor(scratch.AssetDir(), "C:/Windows/win.ini");
    ASSERT_FALSE(message.empty());
    EXPECT_NE(message.find("drive-absolute"), std::string::npos) << message;
}

TEST(GltfUriContainment, ANetworkSchemeIsRefusedAsUnsupportedRatherThanTreatedAsAFileName)
{
    // Joining 'http://example.com/x.bin' onto the asset directory would produce a path that simply
    // does not exist, and "cannot open file" is a misleading way to say "CNA does not fetch URLs".
    const ContainmentScratch scratch;
    for (const char* uri : {"http://example.com/x.bin", "https://example.com/x.bin",
                            "file:///etc/passwd", "ftp://example.com/x.bin"})
    {
        SCOPED_TRACE(uri);
        const std::string message = RefusalFor(scratch.AssetDir(), uri);
        ASSERT_FALSE(message.empty());
        EXPECT_NE(message.find("relative file paths"), std::string::npos) << message;
    }
}

TEST(GltfUriContainment, ContainmentIsComponentWiseNotAStringPrefix)
{
    // '<root>/asset-evil' begins with the characters of '<root>/asset' and is not inside it. A
    // containment check written as a string comparison passes every other test in this file and
    // fails this one.
    const ContainmentScratch scratch;
    const std::string message = RefusalFor(scratch.AssetDir(), "../asset-evil/secret.bin");
    ASSERT_FALSE(message.empty()) << "a sibling directory sharing a name prefix was accepted";
    EXPECT_NE(message.find("outside the asset's own directory"), std::string::npos) << message;
}

TEST(GltfUriContainment, ASymlinkPointingOutsideTheAssetDirectoryIsRefused)
{
    // Lexical normalisation cannot see through a link: '<asset>/escape/secret.bin' is lexically
    // inside the directory and physically is not. This is the case that makes the second,
    // filesystem-resolving pass necessary rather than belt-and-braces.
    const ContainmentScratch scratch;
    std::error_code ec;
    std::filesystem::create_directory_symlink(scratch.Root(), scratch.AssetDir() / "escape", ec);
    if (ec)
    {
        GTEST_SKIP() << "this filesystem does not allow creating symbolic links: " << ec.message();
    }

    const std::string message = RefusalFor(scratch.AssetDir(), "escape/secret.bin");
    ASSERT_FALSE(message.empty()) << "a symlink out of the asset directory was accepted";
    EXPECT_NE(message.find("symbolic link"), std::string::npos) << message;
}

// --- The whole-file sweep ----------------------------------------------------------------------

TEST(GltfUriContainment, TheSweepRefusesATraversingBufferUri)
{
    // The sweep is what stands in front of cgltf_load_buffers, which resolves external buffer URIs
    // itself and offers no hook to veto one. If this stopped running, a traversal would be read
    // before any CNA code saw the file.
    const ContainmentScratch scratch;
    ParsedGltf parsed;
    ASSERT_TRUE(ParseInto(parsed, scratch.AssetDir(), DocumentWithBufferUri("../secret.bin")));

    try
    {
        ValidateExternalUriContainmentEXT(parsed.data, scratch.AssetDir());
        FAIL() << "the sweep accepted a traversing buffer URI";
    }
    catch (const std::exception& e)
    {
        EXPECT_NE(std::string(e.what()).find("buffer URI"), std::string::npos) << e.what();
    }
}

TEST(GltfUriContainment, TheSweepRefusesATraversingImageUri)
{
    const ContainmentScratch scratch;
    ParsedGltf parsed;
    ASSERT_TRUE(ParseInto(parsed, scratch.AssetDir(), DocumentWithImageUri("../secret.bin")));

    try
    {
        ValidateExternalUriContainmentEXT(parsed.data, scratch.AssetDir());
        FAIL() << "the sweep accepted a traversing image URI";
    }
    catch (const std::exception& e)
    {
        EXPECT_NE(std::string(e.what()).find("image URI"), std::string::npos) << e.what();
    }
}

TEST(GltfUriContainment, TheSweepAcceptsDataUrisAndContainedRelativeUris)
{
    // A data: URI carries its bytes inline and an absent URI is a GLB's own BIN chunk -- neither
    // is a path, and refusing them would break every fixture in the corpus.
    const ContainmentScratch scratch;
    ParsedGltf inlineBuffer;
    ASSERT_TRUE(ParseInto(inlineBuffer, scratch.AssetDir(),
                          DocumentWithBufferUri("data:application/octet-stream;base64,AAAAAA==")));
    EXPECT_NO_THROW(ValidateExternalUriContainmentEXT(inlineBuffer.data, scratch.AssetDir()));

    ParsedGltf relativeImage;
    ASSERT_TRUE(ParseInto(relativeImage, scratch.AssetDir(),
                          DocumentWithImageUri("textures/base.png")));
    EXPECT_NO_THROW(ValidateExternalUriContainmentEXT(relativeImage.data, scratch.AssetDir()));
}

TEST(GltfUriContainment, TheSweepIsTotalOverBothUriBearingArrays)
{
    // Buffers and images are the only two places a glTF 2.0 file can name an external file. If a
    // future extension adds a third, this test is where the omission should become visible: it
    // asserts that a file whose ONLY traversal is in the images array is still refused, which is
    // the failure mode of a sweep that walks buffers and stops.
    const ContainmentScratch scratch;
    ParsedGltf parsed;
    ASSERT_TRUE(ParseInto(parsed, scratch.AssetDir(), std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "buffers": [ { "byteLength": 6, "uri": "data:application/octet-stream;base64,AAAAAA==" } ],
  "images": [ { "uri": "textures/base.png" }, { "uri": "../secret.bin" } ]
})GLTF")));

    EXPECT_THROW(ValidateExternalUriContainmentEXT(parsed.data, scratch.AssetDir()),
                 std::runtime_error);
}

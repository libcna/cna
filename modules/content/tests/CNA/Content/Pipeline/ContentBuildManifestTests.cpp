// SPDX-License-Identifier: MS-PL

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Content/Pipeline/ContentBuildManifest.hpp"
#include "CNA/Internal/ContentPath.hpp"

namespace Pipeline = CNA::Content::Pipeline;

namespace
{
    class ScratchDirectory
    {
    public:
        explicit ScratchDirectory(const std::string& tag)
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_content_manifest_" + tag + "_" +
                     std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(path_);
        }

        ~ScratchDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }

        ScratchDirectory(const ScratchDirectory&) = delete;
        ScratchDirectory& operator=(const ScratchDirectory&) = delete;

        [[nodiscard]] const std::filesystem::path& Path() const
        {
            return path_;
        }

    private:
        std::filesystem::path path_;
    };

    void WriteBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }

    Pipeline::ContentBuildManifestEntry MakeEntry(const std::filesystem::path& sourceRoot)
    {
        WriteBytes(sourceRoot / "asset.bin", {1u, 2u, 3u});
        WriteBytes(sourceRoot / "shared" / "table.bin", {4u, 5u, 6u});

        Pipeline::ContentBuildManifestEntry entry;
        entry.logicalName = "Data/asset";
        entry.source = "asset.bin";
        entry.output = "Data/asset.cnb";
        entry.importer = {"test.Importer", "1"};
        entry.processor = {"test.Processor", "2"};
        entry.writer = {"test.Writer", "3"};
        entry.parameters.Set("bool", true);
        entry.parameters.Set("f64", 1.25);
        entry.parameters.Set("i64", std::int64_t{-7});
        entry.parameters.Set("string", std::string("value"));
        entry.parameters.Set("u64", std::uint64_t{9u});
        entry.dependencies = {
            {Pipeline::ContentDependencyKind::PrimarySource, "asset.bin"},
            {Pipeline::ContentDependencyKind::SourceFile, "shared/table.bin"},
        };
        entry.runtimeReferences = {{"Textures/reference", 1u}};
        entry.assetTypeId = 42u;
        entry.fingerprint = Pipeline::ComputeContentBuildFingerprint(entry, sourceRoot);
        entry.outputSha256 = Pipeline::ContentSha256({7u, 8u, 9u});
        return entry;
    }
} // namespace

TEST(ContentBuildManifestTest, UsesTheKnownSha256Digest)
{
    EXPECT_EQ(Pipeline::ContentSha256({'a', 'b', 'c'}),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(ContentBuildManifestTest, PersistentPathTextRoundTripsNativeNonAsciiNamesAsUtf8)
{
    const std::filesystem::path native =
        std::filesystem::path(u8"Textury") / std::filesystem::path(u8"žluťoučký_壁.png");
    const std::string persistent = CNA::Internal::ContentPathToUtf8(native);

    EXPECT_EQ(persistent, "Textury/žluťoučký_壁.png");
    EXPECT_EQ(CNA::Internal::ContentPathFromUtf8(persistent), native);
}

TEST(ContentBuildManifestTest, RoundTripsEveryStableFieldDeterministically)
{
    ScratchDirectory scratch("roundtrip");
    Pipeline::ContentBuildManifest manifest;
    const Pipeline::ContentBuildManifestEntry entry = MakeEntry(scratch.Path());
    manifest.Set(entry);

    const std::string first = manifest.Serialize();
    const Pipeline::ContentBuildManifest parsed = Pipeline::ContentBuildManifest::Parse(first);
    ASSERT_NE(parsed.Find("Data/asset"), nullptr);
    EXPECT_EQ(*parsed.Find("Data/asset"), entry);
    EXPECT_EQ(parsed.Serialize(), first);
    EXPECT_NE(first.find("CNA.ContentPipeline.Manifest"), std::string::npos);
    EXPECT_NE(first.find("source-file"), std::string::npos);
    EXPECT_NE(first.find("runtimeReferences"), std::string::npos);
}

TEST(ContentBuildManifestTest, FingerprintUsesBytesNotModificationTimes)
{
    ScratchDirectory scratch("bytes");
    Pipeline::ContentBuildManifestEntry entry = MakeEntry(scratch.Path());
    const std::string original = entry.fingerprint;

    std::error_code error;
    std::filesystem::last_write_time(
        scratch.Path() / "asset.bin",
        std::filesystem::last_write_time(scratch.Path() / "asset.bin") + std::chrono::seconds(10),
        error);
    ASSERT_FALSE(error);
    EXPECT_EQ(Pipeline::ComputeContentBuildFingerprint(entry, scratch.Path()), original);

    WriteBytes(scratch.Path() / "shared" / "table.bin", {4u, 5u, 7u});
    EXPECT_NE(Pipeline::ComputeContentBuildFingerprint(entry, scratch.Path()), original);
}

TEST(ContentBuildManifestTest, FingerprintInvalidatesEveryDeclaredBuildIdentity)
{
    ScratchDirectory scratch("identity");
    const Pipeline::ContentBuildManifestEntry original = MakeEntry(scratch.Path());

    Pipeline::ContentBuildManifestEntry changed = original;
    changed.logicalName = "Data/renamed";
    EXPECT_NE(Pipeline::ComputeContentBuildFingerprint(changed, scratch.Path()),
              original.fingerprint);

    changed = original;
    changed.importer.version = "2";
    EXPECT_NE(Pipeline::ComputeContentBuildFingerprint(changed, scratch.Path()),
              original.fingerprint);

    changed = original;
    changed.processor.version = "3";
    EXPECT_NE(Pipeline::ComputeContentBuildFingerprint(changed, scratch.Path()),
              original.fingerprint);

    changed = original;
    changed.writer.version = "4";
    EXPECT_NE(Pipeline::ComputeContentBuildFingerprint(changed, scratch.Path()),
              original.fingerprint);

    changed = original;
    changed.parameters.Set("u64", std::uint64_t{10u});
    EXPECT_NE(Pipeline::ComputeContentBuildFingerprint(changed, scratch.Path()),
              original.fingerprint);

    changed = original;
    changed.assetTypeId = 43u;
    EXPECT_NE(Pipeline::ComputeContentBuildFingerprint(changed, scratch.Path()),
              original.fingerprint);
}

TEST(ContentBuildManifestTest, ContentBuildDependencyRequiresAndUsesAnEffectiveFingerprint)
{
    ScratchDirectory scratch("build_dependency");
    Pipeline::ContentBuildManifestEntry entry = MakeEntry(scratch.Path());
    entry.dependencies.push_back(
        {Pipeline::ContentDependencyKind::ContentBuild, "Shared/material"});

    EXPECT_THROW((void)Pipeline::ComputeContentBuildFingerprint(entry, scratch.Path()),
                 std::runtime_error);
    const std::string first = Pipeline::ComputeContentBuildFingerprint(
        entry, scratch.Path(), {{"Shared/material", std::string(64u, '1')}});
    const std::string second = Pipeline::ComputeContentBuildFingerprint(
        entry, scratch.Path(), {{"Shared/material", std::string(64u, '2')}});
    EXPECT_NE(first, second);
}

TEST(ContentBuildManifestTest, RejectsTraversalAndSymlinkEscapes)
{
    ScratchDirectory scratch("containment");
    Pipeline::ContentBuildManifestEntry entry = MakeEntry(scratch.Path());
    entry.dependencies.push_back({Pipeline::ContentDependencyKind::SourceFile, "../outside.bin"});
    entry.fingerprint = std::string(64u, '1');
    Pipeline::ContentBuildManifest manifest;
    EXPECT_THROW(manifest.Set(entry), std::invalid_argument);

#if !defined(_WIN32)
    ScratchDirectory outside("outside");
    WriteBytes(outside.Path() / "secret.bin", {9u});
    std::error_code error;
    std::filesystem::create_symlink(outside.Path() / "secret.bin", scratch.Path() / "escape.bin",
                                    error);
    ASSERT_FALSE(error);
    entry = MakeEntry(scratch.Path());
    entry.dependencies.push_back({Pipeline::ContentDependencyKind::SourceFile, "escape.bin"});
    EXPECT_THROW((void)Pipeline::ComputeContentBuildFingerprint(entry, scratch.Path()),
                 std::runtime_error);
#endif
}

// SPDX-License-Identifier: MS-PL
//
// XNB-65/65A/66/67/61a: unit tests for ContentManager's content-manifest scan.

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "CNA/Internal/PathUtf8.hpp"
#include "CNA/Internal/TitlePath.hpp"
#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/PlatformFactory.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentManifestEntry;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

namespace
{
    void WriteBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    void WriteFile(const std::filesystem::path& path, const std::string& text)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream f(path, std::ios::binary);
        f << text;
    }

    class ScratchContentRoot
    {
    public:
        ScratchContentRoot()
            : dir_(std::filesystem::temp_directory_path()
                   / ("cna_manifest_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(dir_);
        }

        ~ScratchContentRoot()
        {
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
        }

        ScratchContentRoot(const ScratchContentRoot&) = delete;
        ScratchContentRoot& operator=(const ScratchContentRoot&) = delete;

        [[nodiscard]] const std::filesystem::path& path() const { return dir_; }

    private:
        std::filesystem::path dir_;
    };

    std::vector<std::uint8_t> BuildXnbFile(const std::string& readerName, int32_t value)
    {
        System::IO::MemoryStream bodyMs;
        System::IO::BinaryWriter bodyWriter(&bodyMs, true);
        bodyWriter.Write7BitEncodedInt(1);
        bodyWriter.Write(readerName);
        bodyWriter.Write((int32_t)0);
        bodyWriter.Write7BitEncodedInt(0);
        bodyWriter.Write7BitEncodedInt(1);
        bodyWriter.Write(value);
        bodyWriter.Flush();
        auto bodyBytes = bodyMs.ToArray();

        System::IO::MemoryStream fileMs;
        System::IO::BinaryWriter fileWriter(&fileMs, true);
        fileWriter.Write((uint8_t)'X'); fileWriter.Write((uint8_t)'N'); fileWriter.Write((uint8_t)'B');
        fileWriter.Write((uint8_t)'w');
        fileWriter.Write((uint8_t)5);
        fileWriter.Write((uint8_t)0);
        fileWriter.Write((int32_t)(10 + (int32_t)bodyBytes.size()));
        fileWriter.Write(bodyBytes.data(), 0, (int32_t)bodyBytes.size());
        fileWriter.Flush();

        auto fileBytes = fileMs.ToArray();
        return std::vector<std::uint8_t>(fileBytes.begin(), fileBytes.end());
    }

    const ContentManifestEntry* FindEntry(
        const std::vector<ContentManifestEntry>& manifest, const std::string& relativePath)
    {
        for (const auto& e : manifest)
        {
            if (e.relativePath == relativePath) return &e;
        }
        return nullptr;
    }

    class ContentManagerManifestTest : public ::testing::Test
    {
    protected:
        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }
    };
}

TEST_F(ContentManagerManifestTest, ManifestFindsNativeCnjAndXnbFiles)
{
    ScratchContentRoot root;
    WriteBytes(root.path() / "player.png", {0x89, 'P', 'N', 'G'});
    WriteFile(root.path() / "font.cnj", R"({"cnjVersion": 1, "type": "SpriteFont"})");
    WriteBytes(root.path() / "tex.xnb", BuildXnbFile("Microsoft.Xna.Framework.Content.Texture2DReader", 1));

    ContentManager cm(nullptr, root.path().string());
    const auto& manifest = cm.GetContentManifest();

    const auto* player = FindEntry(manifest, "player");
    ASSERT_NE(player, nullptr);
    ASSERT_EQ(player->nativeExtensions.size(), 1u);
    EXPECT_EQ(player->nativeExtensions[0], ".png");
    EXPECT_FALSE(player->hasCnj);
    EXPECT_FALSE(player->hasXnb);

    const auto* font = FindEntry(manifest, "font");
    ASSERT_NE(font, nullptr);
    EXPECT_TRUE(font->hasCnj);

    const auto* tex = FindEntry(manifest, "tex");
    ASSERT_NE(tex, nullptr);
    EXPECT_TRUE(tex->hasXnb);
}

TEST_F(ContentManagerManifestTest, ManifestGroupsMultipleExtensionsUnderOneLogicalName)
{
    ScratchContentRoot root;
    WriteBytes(root.path() / "player.xnb", BuildXnbFile("Microsoft.Xna.Framework.Content.Texture2DReader", 1));
    WriteFile(root.path() / "player.cnj", R"({"cnjVersion": 1, "type": "Texture2D"})");
    WriteBytes(root.path() / "player.png", {0x89, 'P', 'N', 'G'});

    ContentManager cm(nullptr, root.path().string());
    const auto& manifest = cm.GetContentManifest();

    const auto* player = FindEntry(manifest, "player");
    ASSERT_NE(player, nullptr);
    EXPECT_TRUE(player->hasXnb);
    EXPECT_TRUE(player->hasCnj);
    ASSERT_EQ(player->nativeExtensions.size(), 1u);
    EXPECT_EQ(player->nativeExtensions[0], ".png");
}

TEST_F(ContentManagerManifestTest, XnbEntryCarriesReaderNameInventory)
{
    ScratchContentRoot root;
    WriteBytes(root.path() / "tex.xnb",
               BuildXnbFile("Microsoft.Xna.Framework.Content.Texture2DReader, Microsoft.Xna.Framework.Graphics, "
                            "Version=4.0.0.0",
                            1));

    ContentManager cm(nullptr, root.path().string());
    const auto& manifest = cm.GetContentManifest();

    const auto* tex = FindEntry(manifest, "tex");
    ASSERT_NE(tex, nullptr);
    ASSERT_EQ(tex->xnbReaderNames.size(), 1u);
    EXPECT_EQ(tex->xnbReaderNames[0], "Microsoft.Xna.Framework.Content.Texture2DReader");
}

TEST_F(ContentManagerManifestTest, MalformedXnbDoesNotAbortTheWholeScan)
{
    ScratchContentRoot root;
    WriteBytes(root.path() / "broken.xnb", {'X', 'N', 'X', 'w', 5, 0, 0, 0, 0, 0}); // bad magic
    WriteBytes(root.path() / "good.xnb", BuildXnbFile("Microsoft.Xna.Framework.Content.Texture2DReader", 1));

    ContentManager cm(nullptr, root.path().string());
    const auto& manifest = cm.GetContentManifest();

    const auto* broken = FindEntry(manifest, "broken");
    ASSERT_NE(broken, nullptr);
    EXPECT_TRUE(broken->hasXnb);
    EXPECT_TRUE(broken->xnbReaderNames.empty()); // best-effort: malformed -> empty inventory

    const auto* good = FindEntry(manifest, "good");
    ASSERT_NE(good, nullptr);
    EXPECT_EQ(good->xnbReaderNames.size(), 1u);
}

TEST_F(ContentManagerManifestTest, RefreshContentManifestPicksUpNewlyAddedFiles)
{
    ScratchContentRoot root;
    ContentManager cm(nullptr, root.path().string());

    EXPECT_EQ(cm.GetContentManifest().size(), 0u);

    WriteBytes(root.path() / "late.xnb", BuildXnbFile("Microsoft.Xna.Framework.Content.Texture2DReader", 1));
    EXPECT_EQ(cm.GetContentManifest().size(), 0u); // stale snapshot, matches XNB-65A's documented behavior

    cm.RefreshContentManifest();
    EXPECT_EQ(cm.GetContentManifest().size(), 1u);
}

TEST_F(ContentManagerManifestTest, ReaderUsageSummaryAggregatesAcrossFilesAndReportsRegistration)
{
    ScratchContentRoot root;
    WriteBytes(root.path() / "a.xnb", BuildXnbFile("Microsoft.Xna.Framework.Content.Texture2DReader", 1));
    WriteBytes(root.path() / "b.xnb", BuildXnbFile("Microsoft.Xna.Framework.Content.Texture2DReader", 2));
    WriteBytes(root.path() / "c.xnb", BuildXnbFile("Microsoft.Xna.Framework.Content.SpriteFontReader", 3));

    ContentTypeReaderManager::AddTypeCreator("Microsoft.Xna.Framework.Content.Texture2DReader", nullptr);

    ContentManager cm(nullptr, root.path().string());
    const auto summary = cm.GetXnbReaderUsageSummary();

    ASSERT_EQ(summary.size(), 2u);
    for (const auto& usage : summary)
    {
        if (usage.readerName == "Microsoft.Xna.Framework.Content.Texture2DReader")
        {
            EXPECT_EQ(usage.fileCount, 2);
            EXPECT_TRUE(usage.isRegistered);
        }
        else if (usage.readerName == "Microsoft.Xna.Framework.Content.SpriteFontReader")
        {
            EXPECT_EQ(usage.fileCount, 1);
            EXPECT_FALSE(usage.isRegistered);
        }
        else
        {
            FAIL() << "Unexpected reader name in summary: " << usage.readerName;
        }
    }
}

TEST_F(ContentManagerManifestTest, PipelineBookkeepingIsExcludedWithoutHidingLookalikeAssets)
{
    ScratchContentRoot root;
    for (const char* name : {".cna-content.lock", ".cna-content-manifest.json",
                            "nested/.cna-content.lock", "nested/.cna-content-manifest.json",
                            ".cna-contentproj/cna-buildcontent.json",
                            "nested/.cna-contentproj/cache.bin"})
    {
        WriteFile(root.path() / name, "bookkeeping");
    }
    for (const char* name : {"good.png", ".private.png", "user.lock",
                            ".cna-content.lock.png", ".cna-content-manifest.json.cnj",
                            ".cna-contentproj-extra/visible.png",
                            "ordinary/.cna-contentproj", "directory/.cna-content.lock/asset.png"})
    {
        WriteFile(root.path() / name, "asset");
    }
    ContentManager manager(nullptr, root.path().string());
    const auto& manifest = manager.GetContentManifest();
    ASSERT_EQ(manifest.size(), 8u);
    for (const char* name : {".cna-content", ".cna-content-manifest",
                            "nested/.cna-content", "nested/.cna-content-manifest",
                            ".cna-contentproj/cna-buildcontent", "nested/.cna-contentproj/cache"})
    {
        EXPECT_EQ(FindEntry(manifest, name), nullptr) << name;
    }
    for (const char* name : {"good", ".private", "user", ".cna-content.lock",
                            ".cna-content-manifest.json", ".cna-contentproj-extra/visible",
                            "ordinary/.cna-contentproj", "directory/.cna-content.lock/asset"})
    {
        EXPECT_NE(FindEntry(manifest, name), nullptr) << name;
    }
}

TEST_F(ContentManagerManifestTest, RefreshExcludesNewBookkeepingAndRetainsTheSnapshotContract)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "first.png", "asset");
    ContentManager manager(nullptr, root.path().string());
    ASSERT_EQ(manager.GetContentManifest().size(), 1u);
    WriteFile(root.path() / "late.cnj", "asset");
    WriteFile(root.path() / "new/.cna-content.lock", "bookkeeping");
    WriteFile(root.path() / "new/.cna-content-manifest.json", "bookkeeping");
    WriteFile(root.path() / "new/.cna-contentproj/intermediate.json", "bookkeeping");
    EXPECT_EQ(manager.GetContentManifest().size(), 1u);
    EXPECT_EQ(FindEntry(manager.GetContentManifest(), "late"), nullptr);
    manager.RefreshContentManifest();
    const auto& refreshed = manager.GetContentManifest();
    ASSERT_EQ(refreshed.size(), 2u);
    EXPECT_NE(FindEntry(refreshed, "first"), nullptr);
    EXPECT_NE(FindEntry(refreshed, "late"), nullptr);
    EXPECT_EQ(FindEntry(refreshed, "new/.cna-content"), nullptr);
    EXPECT_EQ(FindEntry(refreshed, "new/.cna-content-manifest"), nullptr);
    EXPECT_EQ(FindEntry(refreshed, "new/.cna-contentproj/intermediate"), nullptr);
}

namespace
{
    void CheckColdContentRoot()
    {
        namespace fs = std::filesystem;
        ASSERT_FALSE(CNA::Platform::HasCurrentPlatform());
        ASSERT_FALSE(CNA::Internal::TryGetTitlePath().has_value());
        // An independently owned platform supplies the executable path without installing
        // an ambient platform. This keeps the regression portable across SDL platforms.
        const auto independent = CNA::Platform::PlatformFactory::Create();
        const fs::path title = CNA::Internal::PathFromUtf8(independent->GetFileSystem()->GetBasePath());
        ASSERT_FALSE(CNA::Platform::HasCurrentPlatform());
        ScratchContentRoot working;
        const fs::path relative = working.path().filename();
        const fs::path stagedTitle = title / relative;
        ASSERT_TRUE(fs::create_directory(stagedTitle));
        struct RemoveTitle
        {
            fs::path path;
            ~RemoveTitle() { std::error_code ignored; fs::remove_all(path, ignored); }
        } removeTitle{stagedTitle};
        WriteFile(stagedTitle / "title-only.png", "title asset");
        WriteFile(working.path() / relative / "cwd-only.png", "decoy");
        struct RestoreDirectory
        {
            fs::path path = fs::current_path();
            ~RestoreDirectory() { std::error_code ignored; fs::current_path(path, ignored); }
        } restore;
        fs::current_path(working.path());
        ContentManager absolute(nullptr, CNA::Internal::PathToGenericUtf8(working.path() / relative));
        EXPECT_NE(FindEntry(absolute.GetContentManifest(), "cwd-only"), nullptr);
        EXPECT_FALSE(CNA::Platform::HasCurrentPlatform());
        ContentManager manager(nullptr, CNA::Internal::PathToGenericUtf8(relative));
        const auto& cold = manager.GetContentManifest();
        ASSERT_EQ(cold.size(), 1u);
        EXPECT_NE(FindEntry(cold, "title-only"), nullptr);
        EXPECT_EQ(FindEntry(cold, "cwd-only"), nullptr);
        EXPECT_EQ(manager.getRootDirectoryProperty(), CNA::Internal::PathToGenericUtf8(relative));
        const std::string detected = CNA::Platform::GetCurrentPlatform().GetFileSystem()->GetBasePath();
        EXPECT_TRUE(fs::equivalent(CNA::Internal::PathFromUtf8(detected), title));
        CNA::Internal::SetTitlePath(detected);
        manager.RefreshContentManifest();
        ASSERT_EQ(manager.GetContentManifest().size(), 1u);
        EXPECT_NE(FindEntry(manager.GetContentManifest(), "title-only"), nullptr);
        CNA::Internal::SetTitlePath(CNA::Internal::PathToGenericUtf8(working.path()));
        ContentManager overridden(nullptr, CNA::Internal::PathToGenericUtf8(relative));
        ASSERT_EQ(overridden.GetContentManifest().size(), 1u);
        EXPECT_NE(FindEntry(overridden.GetContentManifest(), "cwd-only"), nullptr);
        // An explicit title override does not implicitly refresh an existing snapshot.
        EXPECT_NE(FindEntry(manager.GetContentManifest(), "title-only"), nullptr);
    }
}

TEST(ContentManagerColdRootTest, RelativeRootDoesNotDependOnAmbientPlatformInitialization)
{
#if GTEST_HAS_DEATH_TEST && !defined(__ANDROID__)
    if (CNA::Platform::PlatformFactory::GetDefaultName() != "SDL3")
        GTEST_SKIP() << "Executable-root regression requires the SDL desktop filesystem contract";
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_EXIT(
        {
            CheckColdContentRoot();
            std::exit(::testing::Test::HasFailure() ? 1 : 0);
        },
        ::testing::ExitedWithCode(0), "");
#else
    GTEST_SKIP() << "Fresh-process title-root regression requires desktop death-test support";
#endif
}

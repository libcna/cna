// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnb.md XNB-65/65A/66/67/61a: unit tests for ContentManager's content-manifest scan.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
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

// SPDX-License-Identifier: MS-PL
//
// plans/plan_windows_portability.md WINPORT-0011. Content loading from a content root whose own
// directories and asset names are non-ASCII.
//
// These are deliberately independent of the thirteen CnaContentTests failures that predate this
// workstream on both platforms: nothing here loads a real texture, model or effect, because those
// suites already fail for reasons that have nothing to do with paths and a Unicode test that
// inherited their red would prove nothing. What is asserted is the part F30 actually broke -- that
// the resolver finds the file, that the bytes it reads are the bytes that were written, and that
// the identity it hands back names the same asset.
//
// Asset names are spelled with hex escapes so this source file's own encoding is not under test.
// The concatenation breaks are required: a C++ hex escape is maximal-munch.

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <system_error>
#include <vector>

#include "CNA/Internal/PathUtf8.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"

namespace fs = std::filesystem;

using CNA::Internal::PathFromUtf8;
using CNA::Internal::PathToGenericUtf8;
using CNA::Internal::PathToUtf8;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentManifestEntry;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

namespace
{
    constexpr const char* kCzech = "\xc5\xbe" "lu\xc5\xa5" "ou\xc4\x8d" "k\xc3\xbd";
    constexpr const char* kJapanese = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e";
    constexpr const char* kCyrillic = "\xd0\xba\xd0\xb8\xd1\x80\xd0\xb8\xd0\xbb\xd0\xbb\xd0\xb8\xd1\x86\xd0\xb0";
    constexpr const char* kEmoji = "emoji-\xf0\x9f\x98\x80";

    /// A content root whose own directory name is non-ASCII, removed on destruction.
    class UnicodeContentRoot
    {
    public:
        UnicodeContentRoot()
        {
            std::error_code ec;
            dir_ = fs::temp_directory_path()
                   / PathFromUtf8(std::string("cna-uniroot-") + kCzech + "-"
                                  + std::to_string(counter_++));
            fs::remove_all(dir_, ec);
            fs::create_directories(dir_, ec);
        }

        ~UnicodeContentRoot()
        {
            std::error_code ec;
            fs::remove_all(dir_, ec);
        }

        UnicodeContentRoot(const UnicodeContentRoot&) = delete;
        UnicodeContentRoot& operator=(const UnicodeContentRoot&) = delete;

        [[nodiscard]] const fs::path& Path() const { return dir_; }

        /// The root as ContentManager::RootDirectory takes it: UTF-8.
        [[nodiscard]] std::string Utf8() const { return PathToGenericUtf8(dir_); }

        /// Writes @p text at root/<relativeUtf8>, creating parents, staying native throughout.
        fs::path Write(const std::string& relativeUtf8, const std::string& text) const
        {
            const fs::path file = dir_ / PathFromUtf8(relativeUtf8);
            std::error_code ec;
            fs::create_directories(file.parent_path(), ec);
            std::ofstream out(file, std::ios::binary);
            EXPECT_TRUE(out.is_open()) << relativeUtf8;
            out << text;
            return file;
        }

    private:
        fs::path dir_;
        static inline int counter_ = 0;
    };

    /// A game-defined type with no CNA reader, so these tests assert about paths rather than
    /// about any particular asset format's decoder.
    struct Marker
    {
        std::string payload;
    };

    Marker MarkerFromJson(const std::string& json, ContentManager&)
    {
        Marker m;
        const std::size_t at = json.find("\"payload\"");
        if (at != std::string::npos)
        {
            const std::size_t open = json.find('"', json.find(':', at) + 1);
            const std::size_t close = json.find('"', open + 1);
            if (open != std::string::npos && close != std::string::npos)
            {
                m.payload = json.substr(open + 1, close - open - 1);
            }
        }
        return m;
    }

    const ContentManifestEntry* FindEntry(const std::vector<ContentManifestEntry>& manifest,
                                          const std::string& relativePath)
    {
        for (const ContentManifestEntry& e : manifest)
        {
            if (e.relativePath == relativePath) { return &e; }
        }
        return nullptr;
    }

    class UnicodeContentRootTest : public ::testing::Test
    {
    protected:
        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }
    };
}

// ---------------------------------------------------------------------------------------------
// The public loading path, end to end. ContentManager::Load<T>() composes the root and the asset
// name, walks the tree case-insensitively, and reads the file -- which is the whole of the chain
// F30 broke. RegisterCnjLoader<T> is used so the assertion is about paths and not about any
// particular asset format's decoder.
// ---------------------------------------------------------------------------------------------

TEST_F(UnicodeContentRootTest, LoadsAnAssetFromANonAsciiContentRoot)
{
    const UnicodeContentRoot root;
    root.Write("hero.cnj", R"({"cnjVersion": 1, "type": "Marker", "payload": "ROOT-OK"})");

    ContentManager cm(nullptr, root.Utf8());
    cm.RegisterCnjLoader<Marker>("Marker", MarkerFromJson);

    EXPECT_EQ(cm.Load<Marker>("hero").payload, "ROOT-OK");
}

TEST_F(UnicodeContentRootTest, LoadsAnAssetWhoseOwnNameIsNonAscii)
{
    const UnicodeContentRoot root;
    root.Write(std::string(kCzech) + ".cnj",
               R"({"cnjVersion": 1, "type": "Marker", "payload": "NAME-OK"})");

    ContentManager cm(nullptr, root.Utf8());
    cm.RegisterCnjLoader<Marker>("Marker", MarkerFromJson);

    EXPECT_EQ(cm.Load<Marker>(kCzech).payload, "NAME-OK");
}

TEST_F(UnicodeContentRootTest, LoadsAnAssetWhoseEveryPathComponentIsADifferentScript)
{
    const UnicodeContentRoot root;
    const std::string name = std::string(kCyrillic) + "/" + kJapanese + "/" + kEmoji + "/" + kCzech;
    root.Write(name + ".cnj", R"({"cnjVersion": 1, "type": "Marker", "payload": "MIXED-OK"})");

    ContentManager cm(nullptr, root.Utf8());
    cm.RegisterCnjLoader<Marker>("Marker", MarkerFromJson);

    EXPECT_EQ(cm.Load<Marker>(name).payload, "MIXED-OK");
}

TEST_F(UnicodeContentRootTest, LoadsANonAsciiAssetSpelledWithTheWrongAsciiCase)
{
    // Only the ASCII letters fold; the non-ASCII part must match exactly. This is the
    // case-insensitive walker, which used to convert every directory entry it enumerated.
    const UnicodeContentRoot root;
    root.Write(std::string("Levels/") + kCzech + "_LEVEL.cnj",
               R"({"cnjVersion": 1, "type": "Marker", "payload": "CASE-OK"})");

    ContentManager cm(nullptr, root.Utf8());
    cm.RegisterCnjLoader<Marker>("Marker", MarkerFromJson);

    EXPECT_EQ(cm.Load<Marker>(std::string("levels/") + kCzech + "_level").payload, "CASE-OK");
}

TEST_F(UnicodeContentRootTest, ANonAsciiSiblingDoesNotBreakTheLookupOfAnAsciiAsset)
{
    // The failure mode all three copies of the walker had: they narrowed every entry they
    // enumerated, not only the one they wanted, so one unrepresentable sibling aborted the search.
    const UnicodeContentRoot root;
    root.Write("Assets/Texture.cnj", R"({"cnjVersion": 1, "type": "Marker", "payload": "ASCII-OK"})");
    root.Write(std::string("Assets/") + kJapanese + ".cnj",
               R"({"cnjVersion": 1, "type": "Marker", "payload": "decoy"})");
    root.Write(std::string("Assets/") + kEmoji + ".cnj",
               R"({"cnjVersion": 1, "type": "Marker", "payload": "decoy"})");

    ContentManager cm(nullptr, root.Utf8());
    cm.RegisterCnjLoader<Marker>("Marker", MarkerFromJson);

    EXPECT_EQ(cm.Load<Marker>("assets/texture").payload, "ASCII-OK");
}

TEST_F(UnicodeContentRootTest, ABackslashSpellingStillResolvesWithNonAsciiComponents)
{
    // XNA asset names are spelled with backslashes; separator normalisation must survive the
    // encoding work, and must not be confused with it.
    const UnicodeContentRoot root;
    root.Write(std::string(kJapanese) + "/" + kCzech + ".cnj",
               R"({"cnjVersion": 1, "type": "Marker", "payload": "SEP-OK"})");

    ContentManager cm(nullptr, root.Utf8());
    cm.RegisterCnjLoader<Marker>("Marker", MarkerFromJson);

    EXPECT_EQ(cm.Load<Marker>(std::string(kJapanese) + "\\" + kCzech).payload, "SEP-OK");
}

TEST_F(UnicodeContentRootTest, TheSameAssetLoadedTwiceComesBackFromTheCacheAsTheSameAsset)
{
    // The cache key is derived from the asset name; if the identity spelling were unstable under
    // conversion, a second Load would silently re-read rather than hit, or worse, collide.
    const UnicodeContentRoot root;
    const std::string name = std::string(kCyrillic) + "/" + kEmoji;
    root.Write(name + ".cnj", R"({"cnjVersion": 1, "type": "Marker", "payload": "CACHE-OK"})");

    ContentManager cm(nullptr, root.Utf8());
    cm.RegisterCnjLoader<Marker>("Marker", MarkerFromJson);

    EXPECT_EQ(cm.Load<Marker>(name).payload, "CACHE-OK");
    EXPECT_EQ(cm.Load<Marker>(name).payload, "CACHE-OK");
}

TEST_F(UnicodeContentRootTest, AMissingNonAsciiAssetStillRefusesTheOrdinaryWay)
{
    const UnicodeContentRoot root;
    ContentManager cm(nullptr, root.Utf8());
    cm.RegisterCnjLoader<Marker>("Marker", MarkerFromJson);

    // A miss, not a filesystem_error escaping from a conversion.
    EXPECT_THROW((void)cm.Load<Marker>(std::string(kEmoji) + "/absent"), ContentLoadException);
}

// ---------------------------------------------------------------------------------------------
// The manifest. Its relativePath is both a public value and the key assets are grouped by, so it
// has to be generic-form UTF-8 -- a backslash spelling would not match anything produced elsewhere.
// ---------------------------------------------------------------------------------------------

TEST_F(UnicodeContentRootTest, ManifestNamesNonAsciiAssetsInGenericUtf8)
{
    const UnicodeContentRoot root;
    root.Write(std::string(kJapanese) + "/" + kCzech + ".png", "\x89PNG");
    root.Write(std::string(kCyrillic) + ".cnj", R"({"cnjVersion": 1, "type": "SpriteFont"})");

    ContentManager cm(nullptr, root.Utf8());
    const std::vector<ContentManifestEntry>& manifest = cm.GetContentManifest();

    const ContentManifestEntry* nested = FindEntry(manifest, std::string(kJapanese) + "/" + kCzech);
    ASSERT_NE(nested, nullptr) << "a nested non-ASCII asset is missing from the manifest";
    ASSERT_EQ(nested->nativeExtensions.size(), 1u);
    EXPECT_EQ(nested->nativeExtensions[0], ".png");

    const ContentManifestEntry* cnj = FindEntry(manifest, kCyrillic);
    ASSERT_NE(cnj, nullptr);
    EXPECT_TRUE(cnj->hasCnj);

    for (const ContentManifestEntry& e : manifest)
    {
        EXPECT_EQ(e.relativePath.find('\\'), std::string::npos)
            << "manifest identities are generic form on every platform: " << e.relativePath;
    }
}

TEST_F(UnicodeContentRootTest, EveryManifestIdentityLoadsBackThroughThePublicApi)
{
    // The manifest is only useful if the identities it reports are the ones Load<T>() accepts.
    // That is the round trip the ANSI conversion broke: the scan produced one spelling and the
    // loader composed another.
    const UnicodeContentRoot root;
    root.Write(std::string(kCzech) + ".cnj",
               R"({"cnjVersion": 1, "type": "Marker", "payload": "one"})");
    root.Write(std::string(kJapanese) + "/" + kEmoji + ".cnj",
               R"({"cnjVersion": 1, "type": "Marker", "payload": "two"})");
    root.Write(std::string(kCyrillic) + "/" + kCzech + "/" + kJapanese + ".cnj",
               R"({"cnjVersion": 1, "type": "Marker", "payload": "three"})");

    ContentManager cm(nullptr, root.Utf8());
    cm.RegisterCnjLoader<Marker>("Marker", MarkerFromJson);

    const std::vector<ContentManifestEntry> manifest = cm.GetContentManifest();
    ASSERT_EQ(manifest.size(), 3u);

    std::vector<std::string> loaded;
    for (const ContentManifestEntry& e : manifest)
    {
        EXPECT_TRUE(e.hasCnj) << e.relativePath;
        loaded.push_back(cm.Load<Marker>(e.relativePath).payload);
    }
    std::sort(loaded.begin(), loaded.end());
    EXPECT_EQ(loaded, (std::vector<std::string>{"one", "three", "two"}));
}

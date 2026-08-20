// SPDX-License-Identifier: MS-PL

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include "CNA/Internal/Media/SavedPictureStore.hpp"
#include "CNA/Internal/Graphics/ImageLoader.hpp"

using CNA::Internal::Media::SavedPictureStore;

namespace
{
    class SavedPictureStoreTest : public ::testing::Test
    {
    protected:
        std::string root = "tests/assets/media/.saved_picture_store_test_fixture";

        void SetUp() override
        {
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
        }

        void TearDown() override
        {
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
        }
    };
}

TEST_F(SavedPictureStoreTest, GetSavedPicturesDirectoryCreatesItIfMissing)
{
    std::string dir = SavedPictureStore::GetSavedPicturesDirectory(root);
    ASSERT_FALSE(dir.empty());
    EXPECT_TRUE(std::filesystem::is_directory(dir));
    EXPECT_EQ(std::filesystem::path(dir).filename().string(), "Saved Pictures");
}

// plans/plan_media.md MEDIA-59: a real file is written and readable back with correct content, using
// a real PNG loaded via the same ImageLoader PictureLibraryIndex uses.
TEST_F(SavedPictureStoreTest, SavePictureWritesARealReadableFile)
{
    // Reuse an existing real fixture PNG's bytes rather than hand-crafting one.
    std::ifstream src("tests/assets/media/pictures/Family/portrait.png", std::ios::binary);
    ASSERT_TRUE(src.is_open());
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(src)), std::istreambuf_iterator<char>());
    ASSERT_FALSE(bytes.empty());

    std::string savedPath = SavedPictureStore::SavePicture(root, "my_picture", bytes);
    ASSERT_FALSE(savedPath.empty());
    EXPECT_TRUE(std::filesystem::exists(savedPath));
    EXPECT_EQ(std::filesystem::path(savedPath).extension().string(), ".png");

    CNA::Internal::Graphics::ImageData img = CNA::Internal::Graphics::ImageLoader::Load(savedPath);
    EXPECT_EQ(img.width, 100);
    EXPECT_EQ(img.height, 80);
}

TEST_F(SavedPictureStoreTest, SavePictureFailsGracefullyWithEmptyPicturesRoot)
{
    std::vector<uint8_t> bytes = {1, 2, 3};
    EXPECT_TRUE(SavedPictureStore::SavePicture("", "name", bytes).empty());
}

// Security regression: `name` is caller-supplied and untrusted -- a relative traversal, an
// absolute path, or a Windows-style backslash traversal must never let the written file escape
// the real Saved Pictures directory. Found by external code review, not by inspection.
TEST_F(SavedPictureStoreTest, SavePictureRejectsPathTraversalInName)
{
    std::vector<uint8_t> bytes = {1, 2, 3};

    std::string savedPath = SavedPictureStore::SavePicture(root, "../../../../tmp/evil", bytes);
    ASSERT_FALSE(savedPath.empty());
    std::string savedDir = SavedPictureStore::GetSavedPicturesDirectory(root);
    EXPECT_EQ(std::filesystem::path(savedPath).parent_path().lexically_normal(),
              std::filesystem::path(savedDir).lexically_normal());
    std::error_code ec;
    EXPECT_FALSE(std::filesystem::exists("/tmp/evil.png", ec));
}

TEST_F(SavedPictureStoreTest, SavePictureRejectsAbsolutePathInName)
{
    std::vector<uint8_t> bytes = {1, 2, 3};

    std::string savedPath = SavedPictureStore::SavePicture(root, "/etc/cron.d/evil", bytes);
    ASSERT_FALSE(savedPath.empty());
    std::string savedDir = SavedPictureStore::GetSavedPicturesDirectory(root);
    EXPECT_EQ(std::filesystem::path(savedPath).parent_path().lexically_normal(),
              std::filesystem::path(savedDir).lexically_normal());
}

TEST_F(SavedPictureStoreTest, SavePictureRejectsWindowsStyleBackslashTraversalInName)
{
    std::vector<uint8_t> bytes = {1, 2, 3};

    std::string savedPath = SavedPictureStore::SavePicture(root, "..\\..\\..\\..\\tmp\\evil2", bytes);
    ASSERT_FALSE(savedPath.empty());
    std::string savedDir = SavedPictureStore::GetSavedPicturesDirectory(root);
    EXPECT_EQ(std::filesystem::path(savedPath).parent_path().lexically_normal(),
              std::filesystem::path(savedDir).lexically_normal());
    std::error_code ec;
    EXPECT_FALSE(std::filesystem::exists("/tmp/evil2.png", ec));
}

TEST_F(SavedPictureStoreTest, SavePictureFallsBackToASafeNameForDotOrDotDot)
{
    std::vector<uint8_t> bytes = {1, 2, 3};

    std::string savedPath = SavedPictureStore::SavePicture(root, "..", bytes);
    ASSERT_FALSE(savedPath.empty());
    EXPECT_TRUE(std::filesystem::exists(savedPath));
    EXPECT_EQ(std::filesystem::path(savedPath).stem().string(), "picture");
}

// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "CNA/Internal/Media/MediaLibraryPaths.hpp"
#include "CNA/Platform/PlatformTestDecorator.hpp"

#include <cstdint>
#include <string>
#include <vector>

using CNA::Internal::Media::MediaLibraryPaths;

namespace
{
    class CannedFileSystem final : public CNA::Platform::IPlatformFileSystem
    {
    public:
        std::string music;
        std::string pictures;

        [[nodiscard]] std::string GetBasePath() const override { return {}; }
        [[nodiscard]] std::string GetPreferencesPath(
            const std::string&, const std::string&) const override { return {}; }
        [[nodiscard]] std::string GetUserFolder(
            const CNA::Platform::UserFolder folder) const override
        {
            return folder == CNA::Platform::UserFolder::Music ? music : pictures;
        }
        [[nodiscard]] bool TryLoadFile(
            const std::string&, std::vector<std::uint8_t>&) const override { return false; }
        [[nodiscard]] bool TryLoadFileIgnoringCase(
            const std::string&, std::vector<std::uint8_t>&) const override { return false; }
        void CreateDirectory(const std::string&) override {}
    };

    class MediaPathPlatform final : public CNA::Platform::Testing::PlatformTestDecorator
    {
    public:
        [[nodiscard]] CNA::Platform::IPlatformFileSystem* GetFileSystem() override
        {
            return &fileSystem;
        }

        CannedFileSystem fileSystem;
    };

    class MediaLibraryPathsTest : public ::testing::Test
    {
    protected:
        void TearDown() override
        {
            MediaLibraryPaths::SetMusicRootOverride("");
            MediaLibraryPaths::SetPictureRootOverride("");
        }
    };
}

TEST_F(MediaLibraryPathsTest, OverrideRedirectsMusicRoot)
{
    MediaLibraryPaths::SetMusicRootOverride("tests/assets/media/music");
    EXPECT_EQ(MediaLibraryPaths::GetMusicRoot(), "tests/assets/media/music");
}

TEST_F(MediaLibraryPathsTest, OverrideRedirectsPictureRoot)
{
    MediaLibraryPaths::SetPictureRootOverride("tests/assets/media/pictures");
    EXPECT_EQ(MediaLibraryPaths::GetPictureRoot(), "tests/assets/media/pictures");
}

TEST_F(MediaLibraryPathsTest, NoOverrideResolvesToARealNonEmptyPathOrGracefullyEmpty)
{
    // Real per-OS folder resolution -- either a genuine existing path (the common case) or a
    // documented graceful empty-string fallback if the platform can't determine one in this
    // environment (e.g. a minimal container with no XDG user-dirs configuration at all).
    std::string music = MediaLibraryPaths::GetMusicRoot();
    if (!music.empty())
    {
        EXPECT_NE(music.back(), '/');
    }
}

TEST_F(MediaLibraryPathsTest, UsesAmbientPlatformFoldersAndStripsTrailingSeparators)
{
    MediaPathPlatform platform;
    platform.fileSystem.music = "/virtual/user/Music/";
    platform.fileSystem.pictures = "C:\\virtual\\Pictures\\";
    const CNA::Platform::Testing::ScopedCurrentPlatform current(platform);

    EXPECT_EQ(MediaLibraryPaths::GetMusicRoot(), "/virtual/user/Music");
    EXPECT_EQ(MediaLibraryPaths::GetPictureRoot(), "C:\\virtual\\Pictures");
}

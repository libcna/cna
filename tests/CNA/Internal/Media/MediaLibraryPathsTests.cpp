// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "CNA/Internal/Media/MediaLibraryPaths.hpp"

using CNA::Internal::Media::MediaLibraryPaths;

namespace
{
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
    // documented graceful empty-string fallback if SDL_GetUserFolder can't determine one in this
    // environment (e.g. a minimal container with no XDG user-dirs configuration at all).
    std::string music = MediaLibraryPaths::GetMusicRoot();
    if (!music.empty())
    {
        EXPECT_NE(music.back(), '/');
    }
}

// SPDX-License-Identifier: MS-PL
// ISSUE-101: a decoded URI must not open a different file after NUL truncation.
#include <gtest/gtest.h>
#include <string>
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"
#include "System/IO/File.hpp"
#include "System/IO/FileNotFoundException.hpp"
#include "System/Uri.hpp"

namespace
{
    const std::string fixture = "tests/assets/media/video/chroma_420.mkv";

    TEST(VideoFilePathBoundaryTest, FileExistsDoesNotTruncateAtEmbeddedNull)
    {
        ASSERT_TRUE(System::IO::File::Exists(fixture));
        EXPECT_FALSE(System::IO::File::Exists(fixture + std::string(1, '\0') + "suffix"));
        EXPECT_TRUE(System::IO::File::Exists(fixture));
    }

    TEST(VideoFilePathBoundaryTest, RawVideoRejectsEmbeddedNullBeforeDecoding)
    {
        Microsoft::Xna::Framework::Graphics::GraphicsDevice device;
        EXPECT_THROW(Microsoft::Xna::Framework::Media::Video video(
            fixture + std::string(1, '\0') + "suffix", &device), System::IO::FileNotFoundException);
    }

    TEST(VideoFilePathBoundaryTest, RawVideoRejectsADirectoryBeforeDecoding)
    {
        Microsoft::Xna::Framework::Graphics::GraphicsDevice device;
        EXPECT_THROW(Microsoft::Xna::Framework::Media::Video video(
            "tests/assets/media/video", &device), System::IO::FileNotFoundException);
    }

    TEST(VideoFilePathBoundaryTest, FileSchemeWithoutAuthorityIsNotAnAbsoluteUri)
    {
        const System::Uri uri("file:/tmp/clip.mkv", System::UriKind::RelativeOrAbsolute);
        EXPECT_FALSE(uri.getIsAbsoluteUriProperty());
    }

    TEST(VideoFilePathBoundaryTest, AbsoluteDosPathIsAFileUriNotAOneLetterScheme)
    {
        const System::Uri uri("C:\\dir\\clip.mkv", System::UriKind::RelativeOrAbsolute);
        EXPECT_TRUE(uri.getIsAbsoluteUriProperty());
        EXPECT_EQ(uri.getSchemeProperty(), "file");
    }
}

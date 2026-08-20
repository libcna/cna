// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "System/IO/FileNotFoundException.hpp"

using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Media::Video;
using Microsoft::Xna::Framework::Media::VideoSoundtrackType;

namespace
{
    constexpr const char* kFixture = "tests/assets/media/video/chroma_420.mkv";
}

// plans/plan_media.md MEDIA-44: the raw-file constructor now throws FileNotFoundException for a
// missing file, matching FNA's own File.Exists check, instead of silently leaving
// width_/height_/duration_ at 0 with no exception at all.
TEST(VideoTest, RawFileConstructorThrowsFileNotFoundExceptionForMissingFile)
{
    GraphicsDevice gd;
    EXPECT_THROW(
        Video video("tests/assets/media/video/does-not-exist.mkv", &gd),
        System::IO::FileNotFoundException);
}

TEST(VideoTest, RawFileConstructorProbesRealDimensionsAndFps)
{
    GraphicsDevice gd;
    Video video(kFixture, &gd);
    EXPECT_EQ(video.getWidthProperty(), 160);
    EXPECT_EQ(video.getHeightProperty(), 90);
    EXPECT_NEAR(video.getFramesPerSecondProperty(), 25.0f, 0.5f);
    EXPECT_GT(video.getDurationProperty().getTotalMillisecondsProperty(), 0.0);
}

// The XNB-sourced (7-arg) constructor trusts the caller's metadata and never touches the file at
// construction time (matches FNA's "wait until VideoPlayer tries to load this" design) -- a
// nonexistent path must NOT throw here, unlike the raw-file constructor above.
TEST(VideoTest, XnbConstructorDoesNotTouchTheFileAtConstructionTime)
{
    GraphicsDevice gd;
    EXPECT_NO_THROW({
        Video video("tests/assets/media/video/does-not-exist.mkv", &gd, 2000, 160, 90, 25.0f,
                     VideoSoundtrackType::MusicAndDialog);
    });
}

TEST(VideoTest, XnbConstructorUsesSuppliedMetadataVerbatim)
{
    GraphicsDevice gd;
    Video video(kFixture, &gd, 2000, 160, 90, 25.0f, VideoSoundtrackType::Music);
    EXPECT_EQ(video.getWidthProperty(), 160);
    EXPECT_EQ(video.getHeightProperty(), 90);
    EXPECT_FLOAT_EQ(video.getFramesPerSecondProperty(), 25.0f);
    EXPECT_EQ(video.getVideoSoundtrackTypeProperty(), VideoSoundtrackType::Music);
    EXPECT_EQ(video.getDurationProperty(), System::TimeSpan::FromMilliseconds(2000));
}

TEST(VideoTest, FromUriEXTConstructsFromLocalPath)
{
    GraphicsDevice gd;
    Video* video = Video::FromUriEXT(kFixture, &gd);
    ASSERT_NE(video, nullptr);
    EXPECT_EQ(video->getWidthProperty(), 160);
    delete video;
}

TEST(VideoTest, GetTypeNameIsFullyQualified)
{
    GraphicsDevice gd;
    Video video(kFixture, &gd);
    EXPECT_EQ(video.GetTypeName(), "Microsoft.Xna.Framework.Media.Video");
}

// plans/plan_media.md MEDIA-86: Video's own SetAudioTrackEXT/SetVideoTrackEXT (distinct from
// VideoPlayer's identically-named methods -- these only record the selected track index and
// forward to an attached VideoPlayer, which is nullptr for a standalone Video like this one, so
// "doesn't throw and other fields stay unaffected" is what's actually observable at this level).
TEST(VideoTest, SetAudioTrackEXTAndSetVideoTrackEXTDoNotThrowOrAffectOtherFields)
{
    GraphicsDevice gd;
    Video video(kFixture, &gd, 2000, 160, 90, 25.0f, VideoSoundtrackType::MusicAndDialog);

    EXPECT_NO_THROW(video.SetAudioTrackEXT(1));
    EXPECT_NO_THROW(video.SetVideoTrackEXT(1));
    EXPECT_NO_THROW(video.SetAudioTrackEXT(-1)); // "no explicit selection" sentinel

    EXPECT_EQ(video.getWidthProperty(), 160);
    EXPECT_EQ(video.getHeightProperty(), 90);
    EXPECT_EQ(video.getDurationProperty(), System::TimeSpan::FromMilliseconds(2000));
}

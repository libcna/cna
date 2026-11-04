// SPDX-License-Identifier: MS-PL
#include <cmath>
#include <limits>
#include <memory>
#include <typeinfo>
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"
#include "System/IO/FileNotFoundException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"

using Microsoft::Xna::Framework::Media::Video;
using Microsoft::Xna::Framework::Media::VideoSoundtrackType;

TEST(VideoMetadataContractTest, MetadataConstructorPreservesZeroNegativeAndExtremeValues)
{
    // Refresh Video::.ctor IL_0014..003f directly stores metadata, without clamps,
    // file checks, enum validation or native decoder access. 1 ms = 10,000 ticks.
    struct Case { int ms, width, height; float fps; VideoSoundtrackType soundtrack; long long ticks; };
    const Case cases[] = {
        {0, 0, 0, 0.0f, VideoSoundtrackType::Music, 0},
        {-7, -2, -3, -0.5f, VideoSoundtrackType::Dialog, -70000},
        {1234, 321, 123, 29.75f, VideoSoundtrackType::MusicAndDialog, 12340000},
        {2147483647, 2147483647, 1, 0.5f, static_cast<VideoSoundtrackType>(99), 21474836470000LL},
        {-2147483647 - 1, 1, -2147483647 - 1, -0.0f, VideoSoundtrackType::Music, -21474836480000LL}
    };
    for (const auto& value : cases)
    {
        SCOPED_TRACE(value.ms);
        Video video("no-tq055-video.mkv", nullptr, value.ms, value.width, value.height,
                    value.fps, value.soundtrack);
        EXPECT_EQ(video.getWidthProperty(), value.width);
        EXPECT_EQ(video.getHeightProperty(), value.height);
        // All these binary fractions are exact float32; no computation or tolerance needed.
        EXPECT_EQ(video.getFramesPerSecondProperty(), value.fps);
        EXPECT_EQ(std::signbit(video.getFramesPerSecondProperty()), std::signbit(value.fps));
        EXPECT_EQ(video.getVideoSoundtrackTypeProperty(), value.soundtrack);
        EXPECT_EQ(video.getDurationProperty().getTicksProperty(), value.ticks);
        EXPECT_EQ(video.getFileNameProperty(), "no-tq055-video.mkv");
        EXPECT_EQ(video.getGraphicsDeviceProperty(), nullptr);
    }
}

TEST(VideoMetadataContractTest, FramesPerSecondPreservesNonfiniteValues)
{
    // Refresh ctor IL_0034..0037 stores Single directly; get_FramesPerSecond IL_0000..0006.
    for (const float fps : {std::numeric_limits<float>::infinity(),
                            -std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::quiet_NaN()})
    {
        Video video("", nullptr, 0, 0, 0, fps, VideoSoundtrackType::Music);
        if (std::isnan(fps)) EXPECT_TRUE(std::isnan(video.getFramesPerSecondProperty()));
        else EXPECT_EQ(video.getFramesPerSecondProperty(), fps);
    }
}

TEST(VideoMetadataContractTest, DurationExtensionStoresTicksWithoutChangingOtherMetadata)
{
    Video video("", nullptr, 1234, 321, 123, 29.75f, VideoSoundtrackType::Dialog);
    // CNA extension documented by Video.hpp: store TimeSpan verbatim, including sub-ms ticks.
    for (const long long ticks : {1234567LL, -1LL, 0LL})
    {
        video.setDurationProperty(System::TimeSpan(ticks));
        EXPECT_EQ(video.getDurationProperty().getTicksProperty(), ticks);
        EXPECT_EQ(video.getWidthProperty(), 321);
        EXPECT_EQ(video.getHeightProperty(), 123);
        EXPECT_EQ(video.getFramesPerSecondProperty(), 29.75f);
        EXPECT_EQ(video.getVideoSoundtrackTypeProperty(), VideoSoundtrackType::Dialog);
    }
}

TEST(VideoMetadataContractTest, RawFileRefusalsPreserveExactTypeAndRequestedFile)
{
    // FNA src/Media/Video/Video.cs:90..93 File.Exists; CNA FileName mapping.
    const std::string paths[] = {"", "tests/assets/media/video", "no-tq055-video.mkv",
        std::string("tests/assets/media/video/chroma_420.mkv") + std::string(1, '\0') + "suffix"};
    for (const auto& path : paths)
    {
        SCOPED_TRACE(path);
        try { Video video(path, nullptr); ADD_FAILURE() << "Non-file accepted"; }
        catch (const System::IO::FileNotFoundException& error)
        {
            EXPECT_EQ(typeid(error), typeid(System::IO::FileNotFoundException));
            EXPECT_EQ(error.getFileNameProperty(), path);
        }
        catch (const std::exception& error) { ADD_FAILURE() << error.what(); }
    }
}

TEST(VideoMetadataContractTest, UriRejectsNonFileSchemeBeforeDecoderAccess)
{
    // FNA src/Media/Video/Video.cs:151..163 rejects non-file URI schemes.
    try
    {
        std::unique_ptr<Video> video(Video::FromUriEXT("https://example.invalid/tq055.mkv", nullptr));
        ADD_FAILURE() << "Non-file scheme accepted";
    }
    catch (const System::InvalidOperationException& error)
    { EXPECT_EQ(typeid(error), typeid(System::InvalidOperationException)); }
    catch (const std::exception& error) { ADD_FAILURE() << error.what(); }
}

#ifndef CNA_VIDEO_AVAILABLE
TEST(VideoMetadataContractTest, DecoderFreeBuildRefusesExistingFileWithExactNotSupported)
{
    // CNA's explicit decoder-free profile: existence check precedes backend refusal.
    try
    {
        Video video("tests/assets/media/video/chroma_420.mkv", nullptr);
        ADD_FAILURE() << "Decoder-free raw construction succeeded";
    }
    catch (const System::NotSupportedException& error)
    { EXPECT_EQ(typeid(error), typeid(System::NotSupportedException)); }
    catch (const std::exception& error) { ADD_FAILURE() << error.what(); }
}
#endif

#ifdef CNA_VIDEO_AVAILABLE
TEST(VideoMetadataContractTest, FailedProbeLeavesDocumentedCnaDefaults)
{
    // CNA raw-file extension policy (also documented in C VideoSmoke): an existing
    // non-video file has no metadata. This is not XNA's metadata-only constructor,
    // nor FNA's TimeSpan.MaxValue duration hack. JSON bytes are not a media container.
    Video video("tests/assets/media/video/manifest.json", nullptr);
    EXPECT_EQ(video.getWidthProperty(), 0);
    EXPECT_EQ(video.getHeightProperty(), 0);
    EXPECT_EQ(video.getFramesPerSecondProperty(), 0.0f);
    EXPECT_EQ(video.getDurationProperty().getTicksProperty(), 0);
    // FNA raw Video ctor (Video.cs:81..105) never assigns this enum; CLR default is Music=0.
    EXPECT_EQ(video.getVideoSoundtrackTypeProperty(), VideoSoundtrackType::Music);
    EXPECT_EQ(video.getGraphicsDeviceProperty(), nullptr);
}
#endif

// SPDX-License-Identifier: MS-PL
// ISSUE-099: measured managed guard subset of VideoPlayerGuardsOracle.cs.
// No decoder, graphics device or playback is created by these cases.
#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <typeinfo>
#include "Microsoft/Xna/Framework/Media/Video/VideoPlayer.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

namespace
{
    using Microsoft::Xna::Framework::Media::VideoPlayer;

    template<class Exception, class Action>
    void ExactRefusal(Action&& action)
    {
        try
        {
            action();
            FAIL() << "Expected the measured XNA refusal";
        }
        catch (const Exception& error)
        {
            EXPECT_EQ(typeid(error), typeid(Exception));
        }
        catch (const std::exception& error)
        {
            FAIL() << "Wrong exception family: " << error.what();
        }
    }

    template<class Action>
    void DisposedRefusal(Action&& action)
    {
        try
        {
            action();
            FAIL() << "Disposed player must refuse the operation";
        }
        catch (const System::ObjectDisposedException& error)
        {
            EXPECT_EQ(typeid(error), typeid(System::ObjectDisposedException));
            EXPECT_EQ(error.getObjectNameProperty(), "VideoPlayer");
        }
        catch (const std::exception& error)
        {
            FAIL() << "Wrong disposed exception family: " << error.what();
        }
    }

    TEST(VideoPlayerValidationTest, FreshTextureIsRefusedWithoutAnActiveVideo)
    {
        VideoPlayer player;
        ExactRefusal<System::InvalidOperationException>([&] { (void)player.GetTexture(); });
    }

    TEST(VideoPlayerValidationTest, NullVideoIsRefusedByName)
    {
        VideoPlayer player;
        try
        {
            player.Play(nullptr);
            FAIL() << "Null video must be refused";
        }
        catch (const System::ArgumentNullException& error)
        {
            EXPECT_EQ(typeid(error), typeid(System::ArgumentNullException));
            EXPECT_EQ(error.getParamNameProperty(), "video");
        }
    }

    TEST(VideoPlayerValidationTest, OutOfRangeVolumePreservesThePriorValue)
    {
        VideoPlayer player;
        // XNA VideoPlayer.set_Volume IL_0020..003a: the ordered guard rejects infinities as well as
        // finite out-of-range values; an unsuccessful setter must preserve the old value.
        for (const float value : {-0.1f, 1.1f, -std::numeric_limits<float>::infinity(),
                                 std::numeric_limits<float>::infinity()})
        {
            player.setVolumeProperty(0.25f);
            try
            {
                player.setVolumeProperty(value);
                ADD_FAILURE() << "Out-of-range volume must be refused";
            }
            catch (const System::ArgumentOutOfRangeException& error)
            {
                EXPECT_EQ(typeid(error), typeid(System::ArgumentOutOfRangeException));
                EXPECT_EQ(error.getParamNameProperty(), "value");
            }
            EXPECT_FLOAT_EQ(player.getVolumeProperty(), 0.25f);
        }
    }

    TEST(VideoPlayerValidationTest, BoundaryAndNaNVolumesFollowTheManagedGuard)
    {
        VideoPlayer player;
        for (const float value : {0.0f, 1.0f})
        {
            ASSERT_NO_THROW(player.setVolumeProperty(value));
            EXPECT_FLOAT_EQ(player.getVolumeProperty(), value);
        }
        ASSERT_NO_THROW(player.setVolumeProperty(std::numeric_limits<float>::quiet_NaN()));
        EXPECT_TRUE(std::isnan(player.getVolumeProperty()));
    }

    TEST(VideoPlayerValidationTest, DisposedConfigurationGettersRemainReadable)
    {
        VideoPlayer player;
        player.setVolumeProperty(0.25f);
        player.setIsLoopedProperty(true);
        player.setIsMutedProperty(true);
        player.Dispose();
        EXPECT_FLOAT_EQ(player.getVolumeProperty(), 0.25f);
        EXPECT_TRUE(player.getIsLoopedProperty());
        EXPECT_TRUE(player.getIsMutedProperty());
    }

    TEST(VideoPlayerValidationTest, DisposedStateAndPositionAreRefused)
    {
        VideoPlayer player;
        player.Dispose();
        DisposedRefusal([&] { (void)player.getStateProperty(); });
        DisposedRefusal([&] { (void)player.getPlayPositionProperty(); });
    }

    TEST(VideoPlayerValidationTest, DisposalPrecedesSetterValidationAndMutation)
    {
        VideoPlayer player;
        player.setVolumeProperty(0.25f);
        player.setIsLoopedProperty(true);
        player.setIsMutedProperty(true);
        player.Dispose();
        DisposedRefusal([&] { player.setIsLoopedProperty(false); });
        DisposedRefusal([&] { player.setIsMutedProperty(false); });
        for (const float value : {0.25f, -1.0f, std::numeric_limits<float>::quiet_NaN()})
            DisposedRefusal([&] { player.setVolumeProperty(value); });
        EXPECT_FLOAT_EQ(player.getVolumeProperty(), 0.25f);
        EXPECT_TRUE(player.getIsLoopedProperty());
        EXPECT_TRUE(player.getIsMutedProperty());
    }

    TEST(VideoPlayerValidationTest, ExistingDisposedTransportAndTextureGuardsRemain)
    {
        VideoPlayer player;
        player.Dispose();
        DisposedRefusal([&] { player.Play(nullptr); });
        DisposedRefusal([&] { player.Stop(); });
        DisposedRefusal([&] { player.Pause(); });
        DisposedRefusal([&] { player.Resume(); });
        DisposedRefusal([&] { (void)player.GetTexture(); });
    }

    TEST(VideoPlayerValidationTest, DisposeIsIdempotent)
    {
        VideoPlayer player;
        player.Dispose();
        ASSERT_NO_THROW(player.Dispose());
        EXPECT_TRUE(player.getIsDisposedProperty());
        EXPECT_EQ(player.getVideoProperty(), nullptr);
    }
}

TEST(VideoPlayerValidationTest, Tq023MuteAndLoopSettingsSurviveTransportAndRejectedVolume)
{
    using Microsoft::Xna::Framework::Media::MediaState;
    VideoPlayer player;
    // XNA VideoPlayer .ctor defaults and independent muted/volume/looping fields; set_Volume
    // IL_0020..003a validates before storing. FNA applies mute to gain, not stored volume.
    EXPECT_FALSE(player.getIsLoopedProperty());
    EXPECT_FALSE(player.getIsMutedProperty());
    EXPECT_FLOAT_EQ(player.getVolumeProperty(), 1.0f);
    player.setVolumeProperty(0.375f);
    player.setIsMutedProperty(true);
    player.setIsLoopedProperty(true);
    EXPECT_FLOAT_EQ(player.getVolumeProperty(), 0.375f);
    player.setVolumeProperty(0.625f);
    EXPECT_TRUE(player.getIsMutedProperty());
    EXPECT_THROW(player.setVolumeProperty(-1.0f), System::ArgumentOutOfRangeException);
    player.Pause();
    player.Resume();
    player.Stop();
    EXPECT_EQ(player.getStateProperty(), MediaState::Stopped);
    EXPECT_EQ(player.getPlayPositionProperty(), System::TimeSpan::Zero);
    EXPECT_EQ(player.getVideoProperty(), nullptr);
    EXPECT_TRUE(player.getIsLoopedProperty());
    EXPECT_TRUE(player.getIsMutedProperty());
    EXPECT_FLOAT_EQ(player.getVolumeProperty(), 0.625f);
    player.setIsMutedProperty(false);
    EXPECT_FLOAT_EQ(player.getVolumeProperty(), 0.625f);
}

#ifdef CNA_FFMPEG_AVAILABLE
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"
#include "VideoPlayerTestAccess.hpp"

TEST(VideoPlayerValidationTest, Tq023PausedClockAndNullPlayPreserveRealDecoderPlayback)
{
    using namespace Microsoft::Xna::Framework::Media;
    // CNA's device-less Video extension opens the real decoder without allocating a GPU texture.
    // FNA VideoPlayer Pause stops its clock, Resume starts it, Stop resets it. XNA Play
    // IL_0011..0024 rejects null before replacing activeVideo; Stop retains that identity.
    Video video("tests/assets/media/video/chroma_420.mkv", nullptr);
    VideoPlayer player;
    player.Play(&video);
    ASSERT_EQ(player.getStateProperty(), MediaState::Playing);
    ASSERT_EQ(player.getVideoProperty(), &video);
    VideoPlayerTestAccess::SetElapsedSeconds(player, 0.625);
    player.Pause();
    ASSERT_EQ(player.getStateProperty(), MediaState::Paused);
    const auto paused = player.getPlayPositionProperty();
    EXPECT_GE(paused, System::TimeSpan::FromMilliseconds(625));
    EXPECT_THROW(player.Play(nullptr), System::ArgumentNullException);
    player.Pause();
    EXPECT_EQ(player.getStateProperty(), MediaState::Paused);
    EXPECT_EQ(player.getPlayPositionProperty(), paused);
    EXPECT_EQ(player.getVideoProperty(), &video);
    player.Resume();
    EXPECT_EQ(player.getStateProperty(), MediaState::Playing);
    EXPECT_GE(player.getPlayPositionProperty(), paused);
    player.Pause();
    EXPECT_GE(player.getPlayPositionProperty(), paused);
    player.Stop();
    player.Stop();
    player.Resume();
    EXPECT_EQ(player.getStateProperty(), MediaState::Stopped);
    EXPECT_EQ(player.getPlayPositionProperty(), System::TimeSpan::Zero);
    EXPECT_EQ(player.getVideoProperty(), &video);
    player.Dispose();
    EXPECT_TRUE(player.getIsDisposedProperty());
    EXPECT_EQ(player.getVideoProperty(), &video);
    EXPECT_THROW((void)player.GetTexture(), System::ObjectDisposedException);
}
#endif

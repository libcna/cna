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
        for (const float value : {-0.1f, 1.1f})
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

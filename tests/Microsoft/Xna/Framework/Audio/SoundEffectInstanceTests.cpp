// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <vector>

#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioChannels.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioListener.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEmitter.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundState.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "SoundEffectInstanceTestAccess.hpp"

#include <SDL3_mixer/SDL_mixer.h>

using Microsoft::Xna::Framework::Audio::AudioChannels;
using Microsoft::Xna::Framework::Audio::AudioEmitter;
using Microsoft::Xna::Framework::Audio::AudioListener;
using Microsoft::Xna::Framework::Audio::SoundEffect;
using Microsoft::Xna::Framework::Audio::SoundEffectInstance;
using Microsoft::Xna::Framework::Audio::SoundEffectInstanceTestAccess;
using Microsoft::Xna::Framework::Audio::SoundState;

// All SoundEffectInstance tests need a SoundEffect, whose construction opens the
// (dummy) audio device. The fixture skips every test if no device is available.
class SoundEffectInstanceTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ::setenv("SDL_AUDIODRIVER", "dummy", 1);
        try
        {
            std::vector<unsigned char> pcm(4 * 2048, 0); // 2048 stereo S16 frames
            effect_ = std::make_unique<SoundEffect>(pcm, 44100, AudioChannels::Stereo);
        }
        catch (...)
        {
            effect_.reset();
        }
    }

    SoundEffectInstance instance() { return effect_->CreateInstance(); }
    bool haveDevice() const { return effect_ != nullptr; }

    std::unique_ptr<SoundEffect> effect_;
};

#define REQUIRE_DEVICE() do { if (!haveDevice()) GTEST_SKIP() << "no audio device"; } while (0)

TEST_F(SoundEffectInstanceTest, DefaultState)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    EXPECT_FALSE(inst.getIsDisposedProperty());
    EXPECT_EQ(inst.getStateProperty(), SoundState::Stopped);
    EXPECT_FALSE(inst.getIsLoopedProperty());
    EXPECT_FLOAT_EQ(inst.getVolumeProperty(), 1.0f);
    EXPECT_FLOAT_EQ(inst.getPanProperty(), 0.0f);
    EXPECT_FLOAT_EQ(inst.getPitchProperty(), 0.0f);
}

TEST_F(SoundEffectInstanceTest, VolumePassesThroughUnclamped)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.setVolumeProperty(2.0f); // FNA does not clamp
    EXPECT_FLOAT_EQ(inst.getVolumeProperty(), 2.0f);
    inst.setVolumeProperty(-0.5f);
    EXPECT_FLOAT_EQ(inst.getVolumeProperty(), -0.5f);
    inst.setVolumeProperty(0.3f); // move overload
    EXPECT_FLOAT_EQ(inst.getVolumeProperty(), 0.3f);
}

TEST_F(SoundEffectInstanceTest, PanRangeAndDisposed)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.setPanProperty(0.5f);
    EXPECT_FLOAT_EQ(inst.getPanProperty(), 0.5f);
    inst.setPanProperty(-1.0f); // move overload, boundary
    EXPECT_FLOAT_EQ(inst.getPanProperty(), -1.0f);
    EXPECT_THROW(inst.setPanProperty(1.5f), System::ArgumentOutOfRangeException);
    EXPECT_THROW(inst.setPanProperty(-1.5f), System::ArgumentOutOfRangeException);
    inst.Dispose();
    EXPECT_THROW(inst.setPanProperty(0.0f), System::ObjectDisposedException);
}

TEST_F(SoundEffectInstanceTest, PitchClampsToRange)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.setPitchProperty(0.5f);
    EXPECT_FLOAT_EQ(inst.getPitchProperty(), 0.5f);
    inst.setPitchProperty(2.0f);
    EXPECT_FLOAT_EQ(inst.getPitchProperty(), 1.0f);
    inst.setPitchProperty(-2.0f); // move overload
    EXPECT_FLOAT_EQ(inst.getPitchProperty(), -1.0f);
}

TEST_F(SoundEffectInstanceTest, IsLoopedBeforePlay)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.setIsLoopedProperty(true);
    EXPECT_TRUE(inst.getIsLoopedProperty());
    inst.setIsLoopedProperty(false);
    EXPECT_FALSE(inst.getIsLoopedProperty());
}

TEST_F(SoundEffectInstanceTest, IsLoopedAfterPlayThrows)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    EXPECT_THROW(inst.setIsLoopedProperty(true), System::InvalidOperationException);
}

TEST_F(SoundEffectInstanceTest, PlayStopTransitions)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    EXPECT_EQ(inst.getStateProperty(), SoundState::Playing);
    inst.Stop();
    EXPECT_EQ(inst.getStateProperty(), SoundState::Stopped);
}

TEST_F(SoundEffectInstanceTest, StopFalseDoesNotCutOffLoopedPlaybackImmediately)
{
    // Non-immediate Stop must not cut playback off right away -- it only removes the loop so
    // the track finishes its current pass and stops naturally afterward (CP-13).
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.setIsLoopedProperty(true);
    inst.Play();
    ASSERT_EQ(inst.getStateProperty(), SoundState::Playing);

    inst.Stop(false);
    EXPECT_EQ(inst.getStateProperty(), SoundState::Playing);
}

TEST_F(SoundEffectInstanceTest, RepeatedPlayWhileAlreadyPlayingDoesNotRestartTrack)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    ASSERT_EQ(inst.getStateProperty(), SoundState::Playing);

    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(inst);
    ASSERT_NE(track, nullptr);

    // Simulate playback having progressed partway through (the fixture's PCM buffer is 2048
    // stereo S16 frames, so 500 is comfortably in bounds), then call Play() again while still
    // Playing. FNA no-ops in this case; a naive re-Play would call MIX_PlayTrack again, which
    // (per SDL_mixer docs) restarts mixing from MIX_PROP_PLAY_START_FRAME_NUMBER's default of 0.
    ASSERT_TRUE(MIX_SetTrackPlaybackPosition(track, 500));
    inst.Play();

    EXPECT_EQ(inst.getStateProperty(), SoundState::Playing);
    EXPECT_GE(MIX_GetTrackPlaybackPosition(track), 500);
}

TEST_F(SoundEffectInstanceTest, MoveConstructorTransfersTrackAndProperties)
{
    REQUIRE_DEVICE();
    SoundEffectInstance src = instance();
    src.setVolumeProperty(0.5f);
    src.Play();
    ASSERT_EQ(src.getStateProperty(), SoundState::Playing);
    MIX_Track* originalTrack = SoundEffectInstanceTestAccess::GetTrack(src);
    ASSERT_NE(originalTrack, nullptr);

    SoundEffectInstance dst(std::move(src));

    EXPECT_EQ(SoundEffectInstanceTestAccess::GetTrack(dst), originalTrack);
    EXPECT_FLOAT_EQ(dst.getVolumeProperty(), 0.5f);
    EXPECT_EQ(dst.getStateProperty(), SoundState::Playing);

    // The moved-from instance must look disposed so its destructor (which no-ops when
    // isDisposed_ is already true) doesn't try to destroy the track dst now owns.
    EXPECT_TRUE(src.getIsDisposedProperty());
    EXPECT_EQ(SoundEffectInstanceTestAccess::GetTrack(src), nullptr);
}

TEST_F(SoundEffectInstanceTest, MoveAssignmentTransfersTrackAndDestroysPreviousOne)
{
    REQUIRE_DEVICE();
    SoundEffectInstance src = instance();
    src.setPanProperty(0.25f);
    src.Play();
    MIX_Track* originalTrack = SoundEffectInstanceTestAccess::GetTrack(src);
    ASSERT_NE(originalTrack, nullptr);

    SoundEffectInstance dst = instance();
    dst.Play();
    ASSERT_NE(SoundEffectInstanceTestAccess::GetTrack(dst), nullptr);

    dst = std::move(src);

    EXPECT_EQ(SoundEffectInstanceTestAccess::GetTrack(dst), originalTrack);
    EXPECT_FLOAT_EQ(dst.getPanProperty(), 0.25f);
    EXPECT_EQ(dst.getStateProperty(), SoundState::Playing);

    EXPECT_TRUE(src.getIsDisposedProperty());
    EXPECT_EQ(SoundEffectInstanceTestAccess::GetTrack(src), nullptr);
}

TEST_F(SoundEffectInstanceTest, PauseResume)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    inst.Pause();
    EXPECT_EQ(inst.getStateProperty(), SoundState::Paused);
    inst.Resume();
    EXPECT_EQ(inst.getStateProperty(), SoundState::Playing);
    inst.Stop(true);
    EXPECT_EQ(inst.getStateProperty(), SoundState::Stopped);
}

TEST_F(SoundEffectInstanceTest, Apply3DSingleListener)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    AudioListener listener;
    AudioEmitter emitter;
    emitter.setPositionProperty({10.0f, 0.0f, 0.0f});
    EXPECT_NO_THROW(inst.Apply3D(listener, emitter));
}

TEST_F(SoundEffectInstanceTest, Apply3DArrayOverload)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    AudioListener listeners[2];
    AudioEmitter emitter;
    EXPECT_NO_THROW(inst.Apply3D(listeners, 1, emitter));
    EXPECT_THROW(inst.Apply3D(listeners, 2, emitter), System::NotSupportedException);
    EXPECT_THROW(inst.Apply3D(nullptr, 1, emitter), System::ArgumentNullException);
}

TEST_F(SoundEffectInstanceTest, Apply3DDoesNotModifyVolumeOrPanProperties)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.setVolumeProperty(0.42f);
    inst.setPanProperty(-0.75f);

    AudioListener listener;
    AudioEmitter emitter;
    emitter.setPositionProperty({10.0f, 0.0f, 0.0f}); // off to one side, well outside DistanceScale
    inst.Apply3D(listener, emitter);

    EXPECT_FLOAT_EQ(inst.getVolumeProperty(), 0.42f);
    EXPECT_FLOAT_EQ(inst.getPanProperty(), -0.75f);
}

TEST_F(SoundEffectInstanceTest, Apply3DAfterDisposeThrows)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Dispose();
    AudioListener listener;
    AudioEmitter emitter;
    EXPECT_THROW(inst.Apply3D(listener, emitter), System::ObjectDisposedException);
}

TEST_F(SoundEffectInstanceTest, DisposeIsIdempotent)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Dispose();
    EXPECT_TRUE(inst.getIsDisposedProperty());
    EXPECT_NO_THROW(inst.Dispose());
}

TEST_F(SoundEffectInstanceTest, PlayAfterDisposeThrows)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Dispose();
    EXPECT_THROW(inst.Play(), System::ObjectDisposedException);
}

TEST_F(SoundEffectInstanceTest, GetTypeName)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    EXPECT_EQ(inst.GetTypeName(), "Microsoft.Xna.Framework.Audio.SoundEffectInstance");
}

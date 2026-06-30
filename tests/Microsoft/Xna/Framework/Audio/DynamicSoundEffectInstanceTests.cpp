// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include <cstdlib>
#include <vector>

#include "Microsoft/Xna/Framework/Audio/DynamicSoundEffectInstance.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioChannels.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundState.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "System/EventArgs.hpp"

using Microsoft::Xna::Framework::Audio::AudioChannels;
using Microsoft::Xna::Framework::Audio::DynamicSoundEffectInstance;
using Microsoft::Xna::Framework::Audio::SoundEffectInstance;
using Microsoft::Xna::Framework::Audio::SoundState;

namespace
{
    // Tries to start playback under the SDL "dummy" audio driver so device-dependent
    // behaviour can be exercised headlessly. Returns true if the instance reached a
    // non-Stopped state; otherwise the caller should skip the assertion.
    bool tryStartHeadless(DynamicSoundEffectInstance& d)
    {
        ::setenv("SDL_AUDIODRIVER", "dummy", 1);
        std::vector<unsigned char> pcm(4 * 256, 0); // 256 stereo S16 frames of silence
        d.SubmitBuffer(pcm);
        try
        {
            d.Play();
        }
        catch (...)
        {
            return false;
        }
        return d.getStateProperty() != SoundState::Stopped;
    }
}

// ---------------------------------------------------------------------------
// Headless-safe tests (no audio device required)
// ---------------------------------------------------------------------------

TEST(DynamicSoundEffectInstanceTest, ConstructionDefaultState)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    EXPECT_EQ(d.getPendingBufferCountProperty(), 0);
    EXPECT_FALSE(d.getIsDisposedProperty());
    EXPECT_EQ(d.getStateProperty(), SoundState::Stopped);
    EXPECT_FALSE(d.getIsLoopedProperty());
}

TEST(DynamicSoundEffectInstanceTest, IsLoopedSetterIsNoOpDirect)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Mono);
    EXPECT_NO_THROW(d.setIsLoopedProperty(true));
    EXPECT_FALSE(d.getIsLoopedProperty());
}

// Regression for the override bug (T-2A): going through a SoundEffectInstance&
// must dispatch to the dynamic no-op override, not the base setter.
TEST(DynamicSoundEffectInstanceTest, IsLoopedSetterIsNoOpViaBaseRefWhenStopped)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Mono);
    SoundEffectInstance& base = d;
    bool lvalue = true;
    EXPECT_NO_THROW(base.setIsLoopedProperty(lvalue)); // const bool& overload
    EXPECT_NO_THROW(base.setIsLoopedProperty(true));   // bool&& overload
    EXPECT_FALSE(d.getIsLoopedProperty());
}

TEST(DynamicSoundEffectInstanceTest, SampleDurationRoundTrip)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    // 1 second of 16-bit stereo @ 44100 Hz = 44100 * 2ch * 2bytes = 176400 bytes.
    const int bytes = d.GetSampleSizeInBytes(System::TimeSpan::FromSeconds(1.0));
    EXPECT_EQ(bytes, 176400);
    EXPECT_NEAR(d.GetSampleDuration(176400).getTotalSecondsProperty(), 1.0, 1e-9);
}

TEST(DynamicSoundEffectInstanceTest, SubmitBufferQueuesWhileStopped)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    std::vector<unsigned char> pcm(64, 0);
    d.SubmitBuffer(pcm);
    EXPECT_EQ(d.getPendingBufferCountProperty(), 1);
}

TEST(DynamicSoundEffectInstanceTest, SubmitBufferRangeThrows)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    std::vector<unsigned char> pcm(16, 0);
    EXPECT_THROW(d.SubmitBuffer(pcm, -1, 4), System::ArgumentOutOfRangeException);
    EXPECT_THROW(d.SubmitBuffer(pcm, 0, -1), System::ArgumentOutOfRangeException);
    EXPECT_THROW(d.SubmitBuffer(pcm, 8, 16), System::ArgumentOutOfRangeException);
}

TEST(DynamicSoundEffectInstanceTest, SubmitFloatBufferRangeThrows)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    std::vector<float> buf(16, 0.0f);
    EXPECT_THROW(d.SubmitFloatBufferEXT(buf, -1, 4), System::ArgumentOutOfRangeException);
    EXPECT_THROW(d.SubmitFloatBufferEXT(buf, 0, 32), System::ArgumentOutOfRangeException);
}

TEST(DynamicSoundEffectInstanceTest, SubmitFloatBufferBeforePlayingIsAllowed)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    std::vector<float> buf(32, 0.25f);
    EXPECT_NO_THROW(d.SubmitFloatBufferEXT(buf));
    EXPECT_EQ(d.getPendingBufferCountProperty(), 1);
}

TEST(DynamicSoundEffectInstanceTest, DisposeMarksDisposedAndIsIdempotent)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    d.Dispose();
    EXPECT_TRUE(d.getIsDisposedProperty());
    EXPECT_NO_THROW(d.Dispose()); // idempotent
}

TEST(DynamicSoundEffectInstanceTest, PlayAfterDisposeThrowsObjectDisposed)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    d.Dispose();
    EXPECT_THROW(d.Play(), System::ObjectDisposedException);
}

TEST(DynamicSoundEffectInstanceTest, StopWhileStoppedIsSafe)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    EXPECT_NO_THROW(d.Stop());
    EXPECT_EQ(d.getStateProperty(), SoundState::Stopped);
}

TEST(DynamicSoundEffectInstanceTest, GetTypeNameIsFullyQualified)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    EXPECT_EQ(d.GetTypeName(), "Microsoft.Xna.Framework.Audio.DynamicSoundEffectInstance");
}

// ---------------------------------------------------------------------------
// Device-dependent tests (run under the SDL dummy driver; skipped if unavailable)
// ---------------------------------------------------------------------------

// T-2C: submitting a float buffer to an int-format instance after it starts must throw.
TEST(DynamicSoundEffectInstanceTest, SubmitFloatAfterPlayingThrowsInvalidOperation)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    if (!tryStartHeadless(d))
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
    std::vector<float> buf(32, 0.0f);
    EXPECT_THROW(d.SubmitFloatBufferEXT(buf), System::InvalidOperationException);
}

// T-2A: setting IsLooped through a base reference on a playing dynamic instance must
// remain a no-op (the base setter would otherwise throw "cannot change while playing").
TEST(DynamicSoundEffectInstanceTest, IsLoopedViaBaseRefWhilePlayingDoesNotThrow)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    if (!tryStartHeadless(d))
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
    SoundEffectInstance& base = d;
    EXPECT_NO_THROW(base.setIsLoopedProperty(true));
    EXPECT_FALSE(d.getIsLoopedProperty());
}

// BufferNeeded must fire while the queue is starved during Update().
TEST(DynamicSoundEffectInstanceTest, BufferNeededFiresWhenStarved)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    if (!tryStartHeadless(d))
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
    int fired = 0;
    d.BufferNeeded += [&fired](System::Object*, const System::EventArgs&) { ++fired; };
    d.Update();
    EXPECT_GT(fired, 0);
}

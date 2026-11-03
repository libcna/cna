// SPDX-License-Identifier: MS-PL
//
// NPV-0105: SoundEffect.Duration is a property of the
// sample data, not of an output device -- FNA computes it from the sample count when the effect
// is created. So it must be reported whatever CNA_AUDIO_PLATFORM is, including NULL,
// which have no mixer to ask. It used to be zero there for every effect, which a game timing
// anything by Duration would never notice until it shipped an SDL-free build.
//
// Deliberately separate from SoundEffectTests.cpp: that suite exercises the SDL3_mixer engine and
// is compiled only for CNA_AUDIO_PLATFORM=SDL3 (cmake/UnitTests.cmake), which is exactly why this
// gap went unseen.

#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Audio/NoAudioHardwareException.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/TimeSpan.hpp"

#include <vector>

namespace {

using Microsoft::Xna::Framework::Audio::AudioChannels;
using Microsoft::Xna::Framework::Audio::NoAudioHardwareException;
using Microsoft::Xna::Framework::Audio::SoundEffect;

TEST(SoundEffectDurationTest, ARawPcmEffectReportsItsDurationOnEveryAudioPlatform)
{
    // 8000 frames of mono 16-bit PCM at 8 kHz: exactly one second.
    const std::vector<unsigned char> pcm(16000, 0);
    try
    {
        SoundEffect effect(pcm, 8000, AudioChannels::Mono);
        EXPECT_NEAR(effect.getDurationProperty().getTotalSecondsProperty(), 1.0, 1e-6);
    }
    catch (const NoAudioHardwareException&)
    {
        GTEST_SKIP() << "the selected audio platform needs a device and none is available";
    }
}

TEST(SoundEffectDurationTest, ARangeCountsWholeStereoFrames)
{
    // 400 bytes of stereo 16-bit PCM is 100 whole frames. A stray trailing byte is no longer
    // truncated here: XNA refuses a buffer that is not whole frames (cna-killer KF-12).
    const std::vector<unsigned char> pcm(400, 0);
    try
    {
        SoundEffect effect(pcm, 0, static_cast<int>(pcm.size()), 44100, AudioChannels::Stereo, 0, 0);
        // XNA: AudioFormat.DurationFromSize(400) = FromMilliseconds(100 * 1000f / 44100f) =
        // FromMilliseconds(2.2676), which .NET Framework rounds to 2 ms (FromBuffer
        // IL_00fe-IL_0105; mscorlib TimeSpan.Interval IL_001d-IL_0070).
        EXPECT_EQ(effect.getDurationProperty().getTicksProperty(), 2 * System::TimeSpan::TicksPerMillisecond);
    }
    catch (const NoAudioHardwareException&)
    {
        GTEST_SKIP() << "the selected audio platform needs a device and none is available";
    }
}

// XNA never creates an empty effect to ask for its duration: the three-argument constructor
// refuses a null or empty buffer with ArgumentException before FromBuffer looks at the rate
// (.ctor(byte[], int, AudioChannels) IL_0039-IL_004b). So an empty buffer is refused on every
// audio platform, and with an invalid rate too -- not ArgumentOutOfRangeException("sampleRate").
// This replaced AnEmptyBufferHasZeroDuration, which skipped whenever the constructor refused.
TEST(SoundEffectDurationTest, AnEmptyBufferIsRefusedBeforeTheRateIsChecked)
{
    const std::vector<unsigned char> pcm;
    for (const int rate : {22050, 1})
    {
        SCOPED_TRACE(rate);
        try
        {
            SoundEffect effect(pcm, rate, AudioChannels::Mono);
            FAIL() << "an empty buffer was accepted";
        }
        catch (const System::ArgumentOutOfRangeException&)
        {
            FAIL() << "the rate was checked before the empty buffer";
        }
        catch (const System::ArgumentException&)
        {
        }
    }
}

} // namespace

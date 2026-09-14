// SPDX-License-Identifier: MS-PL
//
// plans/plan_native_platform_validation.md NPV-0105: SoundEffect.Duration is a property of the
// sample data, not of an output device -- FNA computes it from the sample count when the effect
// is created. So it must be reported whatever CNA_AUDIO_PLATFORM is, including NULL and SDL2,
// which have no mixer to ask. It used to be zero there for every effect, which a game timing
// anything by Duration would never notice until it shipped an SDL-free build.
//
// Deliberately separate from SoundEffectTests.cpp: that suite exercises the SDL3_mixer engine and
// is compiled only for CNA_AUDIO_PLATFORM=SDL3 (cmake/UnitTests.cmake), which is exactly why this
// gap went unseen.

#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Audio/NoAudioHardwareException.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"

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

TEST(SoundEffectDurationTest, ARangeCountsWholeStereoFramesOnly)
{
    // 401 bytes of stereo 16-bit PCM is 100 whole frames and one stray byte; the stray byte is not
    // a frame, on any platform (SoundEffectTests.cpp locks the same truncation for SDL3).
    const std::vector<unsigned char> pcm(401, 0);
    try
    {
        SoundEffect effect(pcm, 0, static_cast<int>(pcm.size()), 44100, AudioChannels::Stereo, 0, 0);
        EXPECT_NEAR(effect.getDurationProperty().getTotalSecondsProperty(), 100.0 / 44100.0, 1e-6);  // TimeSpan resolves to 100 ns ticks
    }
    catch (const NoAudioHardwareException&)
    {
        GTEST_SKIP() << "the selected audio platform needs a device and none is available";
    }
}

TEST(SoundEffectDurationTest, AnEmptyBufferHasZeroDuration)
{
    const std::vector<unsigned char> pcm;
    try
    {
        SoundEffect effect(pcm, 22050, AudioChannels::Mono);
        EXPECT_EQ(effect.getDurationProperty().getTotalSecondsProperty(), 0.0);
    }
    catch (const std::exception&)
    {
        // An SDL3 mixer may refuse an empty buffer outright; that is its own suite's concern.
        GTEST_SKIP() << "the selected audio platform refuses an empty buffer";
    }
}

} // namespace

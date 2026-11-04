// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <type_traits>
#include <typeinfo>
#include <vector>

#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioChannels.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundState.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/Environment.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "System/TimeSpan.hpp"
#include "SoundEffectInstanceTestAccess.hpp"
#include "AudioTestWait.hpp"
#include "CNA/Internal/Audio/AudioMixer.hpp"
#include "CNA/Logger.hpp"

#include <SDL3_mixer/SDL_mixer.h>
#include "System/Environment.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "SoundEffectTestAccess.hpp"
#include "Microsoft/Xna/Framework/Audio/NoAudioHardwareException.hpp"

using Microsoft::Xna::Framework::Audio::AudioChannels;
using Microsoft::Xna::Framework::Audio::SoundEffect;
using Microsoft::Xna::Framework::Audio::SoundEffectInstance;
using Microsoft::Xna::Framework::Audio::SoundEffectInstanceTestAccess;
using Microsoft::Xna::Framework::Audio::SoundEffectTestAccess;
using Microsoft::Xna::Framework::Audio::SoundState;

// T-3G: SoundEffect must be move-only -- a single, unambiguous owner per underlying resource
// is what makes Dispose()'s instance-tracking cascade below unambiguous (see SoundEffect.hpp).
static_assert(!std::is_copy_constructible_v<SoundEffect>, "SoundEffect must not be copy-constructible (T-3G)");
static_assert(!std::is_copy_assignable_v<SoundEffect>, "SoundEffect must not be copy-assignable (T-3G)");
static_assert(std::is_move_constructible_v<SoundEffect>, "SoundEffect must remain move-constructible");
static_assert(std::is_move_assignable_v<SoundEffect>, "SoundEffect must remain move-assignable");

namespace
{
    // Builds a SoundEffect from a small silent PCM buffer under the SDL dummy driver.
    // Returns nullptr if no audio device can be opened (the caller should skip).
    // Selects the window-free dummy audio driver before a test first reaches the mixer.
    void UseDummyAudioDriver()
    {
        System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    }

    std::unique_ptr<SoundEffect> makeEffect(const std::size_t frames = 1024)
    {
        UseDummyAudioDriver();
        try
        {
            std::vector<unsigned char> pcm(4 * frames, 0); // stereo S16 frames
            return std::make_unique<SoundEffect>(pcm, 44100, AudioChannels::Stereo);
        }
        catch (...)
        {
            return nullptr;
        }
    }

    void w16(std::vector<uint8_t>& v, uint16_t x)
    {
        v.push_back(static_cast<uint8_t>(x));
        v.push_back(static_cast<uint8_t>(x >> 8));
    }

    void w32(std::vector<uint8_t>& v, uint32_t x)
    {
        v.push_back(static_cast<uint8_t>(x));
        v.push_back(static_cast<uint8_t>(x >> 8));
        v.push_back(static_cast<uint8_t>(x >> 16));
        v.push_back(static_cast<uint8_t>(x >> 24));
    }

    void tag(std::vector<uint8_t>& v, const char* t)
    {
        v.insert(v.end(), t, t + 4);
    }

    // Minimal valid 16-bit mono PCM WAV: 0.1s of silence @ 44100Hz -- enough for FromStream to
    // successfully decode and report a nonzero Duration (regression fixture for CP-11).
    std::vector<uint8_t> BuildMinimalWavBytes()
    {
        constexpr uint16_t channels      = 1;
        constexpr uint32_t sampleRate    = 44100;
        constexpr uint8_t  bitsPerSample = 16;
        constexpr uint32_t frameCount    = 4410; // 0.1s
        const uint32_t audioLen = frameCount * channels * (bitsPerSample / 8);

        const uint16_t blockAlign     = static_cast<uint16_t>(channels * (bitsPerSample / 8));
        const uint32_t avgBytesPerSec = sampleRate * blockAlign;
        const uint32_t riffPayload    = 4 + (8 + 16) + (8 + audioLen);

        std::vector<uint8_t> wav;
        tag(wav, "RIFF"); w32(wav, riffPayload);
        tag(wav, "WAVE");
        tag(wav, "fmt "); w32(wav, 16);
        w16(wav, 1); w16(wav, channels);
        w32(wav, sampleRate); w32(wav, avgBytesPerSec);
        w16(wav, blockAlign); w16(wav, bitsPerSample);
        tag(wav, "data"); w32(wav, audioLen);
        wav.resize(wav.size() + audioLen, 0); // silence

        return wav;
    }

    // Same as BuildMinimalWavBytes, but with "fmt " chunk's audioFormat set to an unsupported
    // format tag (0x2000, a reserved/unused value -- not PCM(1), IEEE float(3), or WAVE_FORMAT_
    // EXTENSIBLE(0xFFFE)) -- P10-SE-002 fixture for FromStream's unsupported-codec path.
    std::vector<uint8_t> BuildWavBytesWithUnsupportedFormatTag()
    {
        std::vector<uint8_t> wav = BuildMinimalWavBytes();
        constexpr std::size_t audioFormatOffset = 20; // "RIFF"+size+"WAVE"+"fmt "+size = 20
        constexpr uint16_t unsupportedFormatTag  = 0x2000;
        std::memcpy(wav.data() + audioFormatOffset, &unsupportedFormatTag, 2);
        return wav;
    }

    // Same as BuildMinimalWavBytes, but the "fmt " chunk is truncated: its declared chunk size
    // (16) is left unchanged, but the file itself ends partway through those 16 bytes, with no
    // "data" chunk at all -- P10-SE-002 fixture for a malformed/truncated fmt chunk.
    std::vector<uint8_t> BuildWavBytesWithTruncatedFmtChunk()
    {
        std::vector<uint8_t> wav;
        tag(wav, "RIFF"); w32(wav, 4 + 8 + 16);
        tag(wav, "WAVE");
        tag(wav, "fmt "); w32(wav, 16); // declares 16 bytes of fmt payload
        w16(wav, 1); w16(wav, 1); // audioFormat=PCM, channels=1 -- only 4 of the 16 bytes follow
        return wav;
    }

    // Same as BuildMinimalWavBytes, but the "data" chunk's declared size (audioLen) is far larger
    // than the number of sample bytes actually present in the file -- P10-SE-002 fixture for a
    // malformed/truncated data chunk.
    std::vector<uint8_t> BuildWavBytesWithTruncatedDataChunk()
    {
        std::vector<uint8_t> wav = BuildMinimalWavBytes();
        constexpr uint32_t claimedAudioLen = 4410 * 100; // wildly exceeds what's really present
        constexpr std::size_t dataSizeOffset = 12 + 8 + 16 + 4; // offset of "data"'s size field
        std::memcpy(wav.data() + dataSizeOffset, &claimedAudioLen, 4);
        return wav;
    }

    // Same as BuildMinimalWavBytes, but with a trailing "smpl" chunk (one sample loop) after the
    // "data" chunk -- regression fixture for CP-17's FromStream loop-point parsing.
    std::vector<uint8_t> BuildWavBytesWithSmplChunk(uint32_t loopStartSample, uint32_t loopEndSample)
    {
        std::vector<uint8_t> wav = BuildMinimalWavBytes();

        std::vector<uint8_t> smpl;
        w32(smpl, 0); // Manufacturer
        w32(smpl, 0); // Product
        w32(smpl, 0); // Sample Period
        w32(smpl, 0); // MIDI Unity Note
        w32(smpl, 0); // MIDI Pitch Fraction
        w32(smpl, 0); // SMPTE Format
        w32(smpl, 0); // SMPTE Offset
        w32(smpl, 1); // numSampleLoops
        w32(smpl, 0); // samplerData
        w32(smpl, 0);              // Cue Point ID
        w32(smpl, 0);              // Type
        w32(smpl, loopStartSample);
        w32(smpl, loopEndSample);
        w32(smpl, 0); // Fraction
        w32(smpl, 0); // Play Count

        tag(wav, "smpl");
        w32(wav, static_cast<uint32_t>(smpl.size()));
        wav.insert(wav.end(), smpl.begin(), smpl.end());

        // Fix up the RIFF chunk size to include the appended smpl chunk.
        const uint32_t riffPayload = static_cast<uint32_t>(wav.size()) - 8;
        std::memcpy(wav.data() + 4, &riffPayload, 4);

        return wav;
    }
}

// ===================== static sample math (headless) =====================

TEST(SoundEffectTest, GetSampleSizeInBytesOneSecond)
{
    // 1 s of 16-bit stereo @ 44100 = 44100 * 2 * 2 = 176400 bytes.
    EXPECT_EQ(SoundEffect::GetSampleSizeInBytes(
                  System::TimeSpan::FromSeconds(1.0), 44100, AudioChannels::Stereo),
              176400);
}

TEST(SoundEffectTest, GetSampleDurationOneSecond)
{
    const System::TimeSpan d =
        SoundEffect::GetSampleDuration(176400, 44100, AudioChannels::Stereo);
    EXPECT_NEAR(d.getTotalSecondsProperty(), 1.0, 1e-9);
}

TEST(SoundEffectTest, GetSampleDurationRoundsToTheNearestMillisecondAsXnaDoes)
{
    // 1000 stereo frames = 4000 bytes; 1000/44100 s = 22.6757 ms. XNA's TimeSpan.FromMilliseconds
    // (.NET Framework) rounds to 23 ms; FNA truncates to 22 (cna-killer KF-7).
    EXPECT_EQ(SoundEffect::GetSampleDuration(4000, 44100, AudioChannels::Stereo).getTicksProperty(),
              23 * System::TimeSpan::TicksPerMillisecond);
    // 559 stereo frames at 31184 Hz = 17.926 ms -> 18.
    EXPECT_EQ(SoundEffect::GetSampleDuration(2236, 31184, AudioChannels::Stereo).getTicksProperty(),
              18 * System::TimeSpan::TicksPerMillisecond);
    // Below the half: 1000 mono frames at 44100 Hz = 22.676 ms -> 23; 441 frames = 10.0 ms -> 10.
    EXPECT_EQ(SoundEffect::GetSampleDuration(882, 44100, AudioChannels::Mono).getTicksProperty(),
              10 * System::TimeSpan::TicksPerMillisecond);
}

TEST(SoundEffectTest, GetSampleSizeInBytesIsAlwaysWholeFramesAsXnaComputesThem)
{
    // XNA: (int)(ms * (double)((float)rate / 1000f)) frames, plus frames % channels, times the
    // block alignment -- never a partial frame (cna-killer KF-8).
    EXPECT_EQ(SoundEffect::GetSampleSizeInBytes(System::TimeSpan::FromMilliseconds(83), 40622,
                                                AudioChannels::Mono),
              6742);
    EXPECT_EQ(SoundEffect::GetSampleSizeInBytes(System::TimeSpan::FromMilliseconds(73), 33191,
                                                AudioChannels::Stereo),
              9688);
    // 44100f / 1000f is 44.0999985 in float, so one second is 44099 frames: 88198 bytes of mono.
    // Stereo gets the frame back through the frames % channels term: 176400.
    EXPECT_EQ(SoundEffect::GetSampleSizeInBytes(System::TimeSpan::FromSeconds(1.0), 44100,
                                                AudioChannels::Mono),
              88198);
}

// XNA SoundEffect.GetSampleDuration (IL_0000-IL_0045): a negative size is
// ArgumentException("sizeInBytes"), then a rate outside 8000..48000 and a channel count other than
// 1 or 2 are ArgumentOutOfRangeException, and only then does a zero size answer TimeSpan.Zero.
// This replaces GetSampleDurationZeroForBadFormat, which pinned FNA's zero for a zero rate.
TEST(SoundEffectTest, GetSampleDurationRefusesWhatXnaRefusesInXnasOrder)
{
    auto paramOf = [](auto&& call) -> std::string {
        try { call(); }
        catch (const System::ArgumentException& error) { return error.getParamNameProperty(); }
        return "<no exception>";
    };
    try { (void)SoundEffect::GetSampleDuration(-2, 44100, AudioChannels::Stereo); FAIL(); }
    catch (const System::ArgumentOutOfRangeException&) { FAIL() << "not an out-of-range refusal"; }
    catch (const System::ArgumentException& error) { EXPECT_EQ(error.getParamNameProperty(), "sizeInBytes"); }
    EXPECT_THROW((void)SoundEffect::GetSampleDuration(1000, 0, AudioChannels::Stereo),
                 System::ArgumentOutOfRangeException);
    EXPECT_EQ(paramOf([] { (void)SoundEffect::GetSampleDuration(1000, 7999, AudioChannels::Mono); }), "sampleRate");
    EXPECT_EQ(paramOf([] { (void)SoundEffect::GetSampleDuration(1000, 48001, AudioChannels::Mono); }), "sampleRate");
    EXPECT_EQ(paramOf([] { (void)SoundEffect::GetSampleDuration(1000, 44100, static_cast<AudioChannels>(3)); }), "channels");
    EXPECT_EQ(paramOf([] { (void)SoundEffect::GetSampleDuration(1000, 44100, static_cast<AudioChannels>(0)); }), "channels");
    // Order: the size is checked before the rate, the rate before the channels, and the zero
    // shortcut only after all three.
    EXPECT_EQ(paramOf([] { (void)SoundEffect::GetSampleDuration(-1, 0, static_cast<AudioChannels>(0)); }), "sizeInBytes");
    EXPECT_EQ(paramOf([] { (void)SoundEffect::GetSampleDuration(0, 0, static_cast<AudioChannels>(0)); }), "sampleRate");
    EXPECT_EQ(paramOf([] { (void)SoundEffect::GetSampleDuration(0, 8000, static_cast<AudioChannels>(0)); }), "channels");
    // Both ends of the rate range are accepted: 16000 mono bytes at 8000 Hz are 1000 ms, at
    // 48000 Hz 166.67 ms -> 167 ms.
    EXPECT_EQ(SoundEffect::GetSampleDuration(16000, 8000, AudioChannels::Mono).getTicksProperty(),
              1000 * System::TimeSpan::TicksPerMillisecond);
    EXPECT_EQ(SoundEffect::GetSampleDuration(16000, 48000, AudioChannels::Mono).getTicksProperty(),
              167 * System::TimeSpan::TicksPerMillisecond);
    EXPECT_EQ(SoundEffect::GetSampleDuration(0, 44100, AudioChannels::Stereo), System::TimeSpan::Zero);
    // A trailing partial frame is dropped before the division: 3 stereo bytes are no frame at all.
    EXPECT_EQ(SoundEffect::GetSampleDuration(3, 8000, AudioChannels::Stereo), System::TimeSpan::Zero);
}

// XNA SoundEffect.GetSampleSizeInBytes (IL_0000-IL_0087): a duration below zero or above
// int.MaxValue milliseconds, a rate outside 8000..48000 and a bad channel count are
// ArgumentOutOfRangeException, in that order; TimeSpan.Zero answers 0; and an OverflowException
// from AudioFormat.SizeFromDuration's checked arithmetic is turned into
// ArgumentOutOfRangeException("duration") rather than wrapping.
TEST(SoundEffectTest, GetSampleSizeInBytesRefusesWhatXnaRefusesInXnasOrder)
{
    auto paramOf = [](auto&& call) -> std::string {
        try { call(); }
        catch (const System::ArgumentOutOfRangeException& error) { return error.getParamNameProperty(); }
        return "<no exception>";
    };
    using System::TimeSpan;
    EXPECT_EQ(paramOf([] { (void)SoundEffect::GetSampleSizeInBytes(TimeSpan::FromTicks(-1), 44100, AudioChannels::Mono); }), "duration");
    // One millisecond past int.MaxValue fails the bound check itself.
    EXPECT_EQ(paramOf([] { (void)SoundEffect::GetSampleSizeInBytes(TimeSpan::FromMilliseconds(2147483648.0), 8000, AudioChannels::Mono); }), "duration");
    EXPECT_EQ(paramOf([] { (void)SoundEffect::GetSampleSizeInBytes(TimeSpan::FromSeconds(1.0), 7999, AudioChannels::Mono); }), "sampleRate");
    EXPECT_EQ(paramOf([] { (void)SoundEffect::GetSampleSizeInBytes(TimeSpan::FromSeconds(1.0), 48001, AudioChannels::Mono); }), "sampleRate");
    EXPECT_EQ(paramOf([] { (void)SoundEffect::GetSampleSizeInBytes(TimeSpan::FromSeconds(1.0), 44100, static_cast<AudioChannels>(3)); }), "channels");
    // Order: duration before rate before channels, and the zero shortcut after all three.
    EXPECT_EQ(paramOf([] { (void)SoundEffect::GetSampleSizeInBytes(TimeSpan::FromTicks(-1), 0, static_cast<AudioChannels>(0)); }), "duration");
    EXPECT_EQ(paramOf([] { (void)SoundEffect::GetSampleSizeInBytes(TimeSpan::Zero, 0, static_cast<AudioChannels>(0)); }), "sampleRate");
    EXPECT_EQ(paramOf([] { (void)SoundEffect::GetSampleSizeInBytes(TimeSpan::Zero, 8000, static_cast<AudioChannels>(0)); }), "channels");
    EXPECT_EQ(SoundEffect::GetSampleSizeInBytes(TimeSpan::Zero, 48000, AudioChannels::Stereo), 0);
    // Overflow, hand-derived: 12 h = 43,200,000 ms at 48 kHz is 43,200,000 * 48.0 = 2,073,600,000
    // frames (fits int32), times the stereo block alignment 4 = 8,294,400,000 bytes: mul.ovf throws.
    EXPECT_EQ(paramOf([] { (void)SoundEffect::GetSampleSizeInBytes(TimeSpan::FromHours(12.0), 48000, AudioChannels::Stereo); }), "duration");
    // 24 h at 48 kHz is 4,147,200,000 frames, past int32 already at conv.ovf.i4.
    EXPECT_EQ(paramOf([] { (void)SoundEffect::GetSampleSizeInBytes(TimeSpan::FromHours(24.0), 48000, AudioChannels::Mono); }), "duration");
    // The largest mono size that still fits: 6 h at 48 kHz = 1,036,800,000 frames * 2 bytes.
    EXPECT_EQ(SoundEffect::GetSampleSizeInBytes(TimeSpan::FromHours(6.0), 48000, AudioChannels::Mono),
              2073600000);
}

// TQ-032: AudioFormat.SizeFromDuration IL_0000-0028 uses TotalMilliseconds
// without rounding it first: frames = trunc(ms * (8000f / 1000f)); then
// (frames + frames % channels) * (2 * channels). One frame is 1250 ticks.
TEST(SoundEffectTest, SampleSizeUsesFractionalMillisecondsBeforeFrameRounding)
{
    struct Case { long long ticks; int mono; int stereo; };
    for (const Case c : {Case{1249, 0, 0}, {1250, 2, 8}, {2499, 2, 8},
                         {2500, 4, 8}, {3750, 6, 16}})
    {
        SCOPED_TRACE(c.ticks);
        const auto duration = System::TimeSpan::FromTicks(c.ticks);
        EXPECT_EQ(SoundEffect::GetSampleSizeInBytes(duration, 8000, AudioChannels::Mono), c.mono);
        EXPECT_EQ(SoundEffect::GetSampleSizeInBytes(duration, 8000, AudioChannels::Stereo), c.stereo);
    }
}

// ===================== static properties (headless) =====================

TEST(SoundEffectTest, MasterVolumeTakesZeroToOneAndRefusesTheRest)
{
    // CP-16: the getter/setter round-trip through SDL3_mixer's real master gain, so this needs a
    // (dummy) audio device. KF-12: XNA refuses NaN and anything outside 0..1 with
    // ArgumentOutOfRangeException, where FNA passed the value through.
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    float saved = 1.0f;
    try
    {
        saved = SoundEffect::getMasterVolumeProperty();
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
    SoundEffect::setMasterVolumeProperty(0.5f);
    EXPECT_FLOAT_EQ(SoundEffect::getMasterVolumeProperty(), 0.5f);
    EXPECT_THROW(SoundEffect::setMasterVolumeProperty(2.0f), System::ArgumentOutOfRangeException);
    EXPECT_THROW(SoundEffect::setMasterVolumeProperty(-0.1f), System::ArgumentOutOfRangeException);
    EXPECT_THROW(SoundEffect::setMasterVolumeProperty(std::numeric_limits<float>::quiet_NaN()),
                 System::ArgumentOutOfRangeException);
    EXPECT_FLOAT_EQ(SoundEffect::getMasterVolumeProperty(), 0.5f);
    SoundEffect::setMasterVolumeProperty(1.0f); // move overload
    EXPECT_FLOAT_EQ(SoundEffect::getMasterVolumeProperty(), 1.0f);
    SoundEffect::setMasterVolumeProperty(saved);
}

// CP-16: MasterVolume must retroactively affect already-playing sounds, not just future Play()
// calls -- SDL3_mixer's MIX_SetMixerGain is a real global stage applied at mix time, so a track's
// own gain (Volume_ only) must stay constant while the mixer's gain is what actually changes.
TEST(SoundEffectTest, MasterVolumeAffectsAlreadyPlayingInstanceViaMixerGainNotTrackGain)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    auto effect = makeEffect();
    if (!effect)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }

    const float savedMaster = SoundEffect::getMasterVolumeProperty();
    try
    {
        SoundEffect::setMasterVolumeProperty(1.0f);
        SoundEffectInstance instance = effect->CreateInstance();
        instance.setVolumeProperty(0.8f);
        instance.Play();
        if (instance.getStateProperty() != SoundState::Playing)
        {
            SoundEffect::setMasterVolumeProperty(savedMaster);
            GTEST_SKIP() << "no audio device (dummy driver unavailable)";
        }

        MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(instance);
        ASSERT_NE(track, nullptr);
        const float trackGainBefore = MIX_GetTrackGain(track);

        SoundEffect::setMasterVolumeProperty(0.25f);

        // The real discriminator: query SDL3_mixer's own mixer-level gain directly (not just
        // SoundEffect::getMasterVolumeProperty(), which pre-fix round-tripped through a plain
        // static field regardless of the mixer -- that alone wouldn't prove anything reached the
        // mixer). This is the actual live mechanism that must reflect the new value while the
        // instance keeps playing, with no per-instance re-application needed.
        EXPECT_FLOAT_EQ(MIX_GetMixerGain(CNA::Internal::Audio::GetMixer()), 0.25f);
        // The track's own gain must NOT have been touched by the MasterVolume change (it still
        // reflects only Volume_) -- otherwise master volume would be double-applied once mixed
        // with the mixer's own gain.
        EXPECT_FLOAT_EQ(MIX_GetTrackGain(track), trackGainBefore);
        EXPECT_FLOAT_EQ(MIX_GetTrackGain(track), 0.8f);
    }
    catch (...)
    {
        SoundEffect::setMasterVolumeProperty(savedMaster);
        throw;
    }
    SoundEffect::setMasterVolumeProperty(savedMaster);
}

// XNA refuses a negative DistanceScale and turns zero into float.Epsilon (KF-12).
TEST(SoundEffectTest, DistanceScaleRejectsNegativeAndLiftsZeroToEpsilon)
{
    const float saved = SoundEffect::getDistanceScaleProperty();
    SoundEffect::setDistanceScaleProperty(2.0f);
    EXPECT_FLOAT_EQ(SoundEffect::getDistanceScaleProperty(), 2.0f);
    SoundEffect::setDistanceScaleProperty(0.0f);
    EXPECT_EQ(SoundEffect::getDistanceScaleProperty(), std::numeric_limits<float>::denorm_min());
    EXPECT_THROW(SoundEffect::setDistanceScaleProperty(-1.0f), System::ArgumentOutOfRangeException);
    SoundEffect::setDistanceScaleProperty(saved);
}

// XNA: SpeedOfSound must be positive, DopplerScale non-negative, NaN refused by both (KF-12).
TEST(SoundEffectTest, SpeedOfSoundAndDopplerScaleRefuseWhatXnaRefuses)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_THROW(SoundEffect::setSpeedOfSoundProperty(0.0f), System::ArgumentOutOfRangeException);
    EXPECT_THROW(SoundEffect::setSpeedOfSoundProperty(nan), System::ArgumentOutOfRangeException);
    EXPECT_THROW(SoundEffect::setDopplerScaleProperty(-0.5f), System::ArgumentOutOfRangeException);
    EXPECT_THROW(SoundEffect::setDopplerScaleProperty(nan), System::ArgumentOutOfRangeException);
    const float speed = SoundEffect::getSpeedOfSoundProperty();
    SoundEffect::setSpeedOfSoundProperty(300.0f);
    EXPECT_FLOAT_EQ(SoundEffect::getSpeedOfSoundProperty(), 300.0f);
    SoundEffect::setSpeedOfSoundProperty(speed);
}

TEST(SoundEffectTest, DopplerScaleRejectsNegative)
{
    const float saved = SoundEffect::getDopplerScaleProperty();
    SoundEffect::setDopplerScaleProperty(0.0f);
    EXPECT_FLOAT_EQ(SoundEffect::getDopplerScaleProperty(), 0.0f);
    EXPECT_THROW(SoundEffect::setDopplerScaleProperty(-0.1f), System::ArgumentOutOfRangeException);
    SoundEffect::setDopplerScaleProperty(saved);
}

TEST(SoundEffectTest, SpeedOfSoundRoundTrip)
{
    const float saved = SoundEffect::getSpeedOfSoundProperty();
    SoundEffect::setSpeedOfSoundProperty(300.0f);
    EXPECT_FLOAT_EQ(SoundEffect::getSpeedOfSoundProperty(), 300.0f);
    SoundEffect::setSpeedOfSoundProperty(saved);
}

// ===================== buffer-range constructor (headless) =====================

// TQ-032: SoundEffect.FromBuffer IL_0051-005b, 006f-0079, 0081-008c,
// 00a2-00ac, 00b6-00c1 and 00db-00e5 use ArgumentException(message), without
// ParamName. The short constructor does likewise at IL_0041-004b.
TEST(SoundEffectTest, BufferFailuresHaveXnasExactExceptionAndNoParameterName)
{
    const auto plainArgument = [](auto&& call) {
        try { call(); ADD_FAILURE() << "expected ArgumentException"; }
        catch (const System::ArgumentException& error)
        {
            EXPECT_EQ(typeid(error), typeid(System::ArgumentException));
            EXPECT_TRUE(error.getParamNameProperty().empty());
        }
    };
    const std::vector<unsigned char> pcm(16, 0);
    plainArgument([&] { (void)SoundEffect(std::vector<unsigned char>{}, 0, AudioChannels::Stereo); });
    plainArgument([&] { (void)SoundEffect(std::vector<unsigned char>(15), 0, 4, 8000, AudioChannels::Stereo, 0, 0); });
    for (const int offset : {-4, 2, 16})
        plainArgument([&] { (void)SoundEffect(pcm, offset, 4, 8000, AudioChannels::Stereo, 0, 0); });
    for (const int count : {-4, 0, 6, 16, 2147483644})
    {
        // offset 4 is valid; the final count is aligned and 4 + 2147483644
        // exceeds INT32_MAX. This reaches the count guard, unlike an invalid huge offset.
        plainArgument([&] { (void)SoundEffect(pcm, 4, count, 8000, AudioChannels::Stereo, 0, 0); });
    }
    for (const auto [start, length] : {std::pair{-1, 1}, {0, -1}, {1, 4}, {2147483647, 1}})
        plainArgument([&] { (void)SoundEffect(pcm, 0, 16, 8000, AudioChannels::Stereo, start, length); });
    // Rate and channel failures remain named, derived exceptions (IL_0012-0031).
    const auto namedRange = [&](int rate, AudioChannels channels, const char* name) {
        try { (void)SoundEffect(pcm, -1, 0, rate, channels, -1, -1); ADD_FAILURE(); }
        catch (const System::ArgumentException& error)
        {
            EXPECT_EQ(typeid(error), typeid(System::ArgumentOutOfRangeException));
            EXPECT_EQ(error.getParamNameProperty(), name);
        }
    };
    namedRange(7999, static_cast<AudioChannels>(0), "sampleRate");
    namedRange(8000, static_cast<AudioChannels>(0), "channels");
}

TEST(SoundEffectTest, BufferRangeConstructorThrowsOnBadRange)
{
    std::vector<unsigned char> pcm(16, 0);
    // Range check runs before any device access, so this is safe headlessly.
    EXPECT_THROW(SoundEffect(pcm, 8, 16, 44100, AudioChannels::Stereo, 0, 0), System::ArgumentException);
    EXPECT_THROW(SoundEffect(pcm, -4, 4, 44100, AudioChannels::Stereo, 0, 0), System::ArgumentException);
    EXPECT_THROW(SoundEffect(pcm, 0, 6, 44100, AudioChannels::Stereo, 0, 0), System::ArgumentException);
    EXPECT_THROW(SoundEffect(std::vector<unsigned char>(6, 0), 44100, AudioChannels::Stereo),
                 System::ArgumentException); // not whole stereo frames
    EXPECT_THROW(SoundEffect(std::vector<unsigned char>{}, 44100, AudioChannels::Mono),
                 System::ArgumentException);
}

// P9-VALIDATION-003: offset+count must be checked without computing the (possibly overflowing)
// sum in 32 bits -- two individually-plausible int32 values can overflow, and on a typical
// two's-complement wraparound the result can come out small/negative, silently passing a naive
// "offset + count > buffer.size()" check while still reading far out of bounds via
// buffer.data() + offset. XNA refuses it with ArgumentException.
TEST(SoundEffectTest, BufferRangeConstructorRejectsOffsetCountIntegerOverflow)
{
    std::vector<unsigned char> pcm(16, 0);
    constexpr int hugeOffset = 2000000000;
    constexpr int hugeCount  = 2000000000; // offset+count overflows int32 (INT32_MAX ~2.147e9)
    EXPECT_THROW(SoundEffect(pcm, hugeOffset, hugeCount, 44100, AudioChannels::Stereo, 0, 0),
                 System::ArgumentException);
}

// cna-killer KF-12 (was AUD-05-001/002): XNA's FromBuffer refuses a sample rate outside
// 8000..48000 Hz and a channel count other than mono or stereo with ArgumentOutOfRangeException,
// before anything reaches the audio backend. FNA left both to the backend, which reported
// NotSupportedException here; since 2026-09-04 XNA decides.
TEST(SoundEffectTest, BufferRangeConstructorRefusesTheSampleRatesAndChannelsXnaRefuses)
{
    std::vector<unsigned char> pcm(16, 0);
    const int size = static_cast<int>(pcm.size());
    for (const int rate : {0, -1, 7999, 48001})
    {
        EXPECT_THROW(SoundEffect(pcm, 0, size, rate, AudioChannels::Stereo, 0, 0),
                     System::ArgumentOutOfRangeException) << rate;
    }
    EXPECT_THROW(SoundEffect(pcm, 0, size, 44100, static_cast<AudioChannels>(0), 0, 0),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW(SoundEffect(pcm, 0, size, 44100, static_cast<AudioChannels>(3), 0, 0),
                 System::ArgumentOutOfRangeException);
}

TEST(SoundEffectTest, BufferRangeConstructorRefusesMisalignedCountInAnAlignedBuffer)
{
    // Keep the whole buffer aligned so refusal exercises count, not buffer validation.
    std::vector<unsigned char> pcm(404, 0);
    EXPECT_THROW(SoundEffect(pcm, 0, 401, 44100, AudioChannels::Stereo, 0, 0),
                 System::ArgumentException);
}

TEST(SoundEffectTest, BufferRangeConstructorCopiesOnlyTheSelectedPcmFrames)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    // S16LE: the selected 0.2 seconds is +0.25; prefix and suffix are -0.5.
    std::vector<unsigned char> pcm(2 * 17640);
    for (int frame = 0; frame < 17640; ++frame)
    {
        pcm[2 * frame] = 0;
        pcm[2 * frame + 1] = frame >= 4410 && frame < 13230 ? 0x20 : 0xc0;
    }
    SoundEffect effect(pcm, 8820, 17640, 44100, AudioChannels::Mono, 0, 0);
    EXPECT_EQ(effect.getDurationProperty(), System::TimeSpan::FromMilliseconds(200));
    std::fill(pcm.begin(), pcm.end(), 0);

    struct Capture
    {
        std::atomic<int> samples{0};
        std::atomic<bool> unexpected{false};
        static void SDLCALL Read(void* data, MIX_Track*, const SDL_AudioSpec*, float* values, int count)
        {
            auto& capture = *static_cast<Capture*>(data);
            for (int i = 0; i < count; ++i)
                if (std::abs(values[i] - 0.25f) > 1e-6f) capture.unexpected.store(true);
            capture.samples.fetch_add(count);
        }
    } capture;
    // Capture outlives the track, including assertion failures and stack unwinding.
    auto instance = effect.CreateInstance();
    instance.setIsLoopedProperty(true);
    instance.Play();
    auto* track = SoundEffectInstanceTestAccess::GetTrack(instance);
    ASSERT_NE(track, nullptr);
    MIX_SetTrackRawCallback(track, Capture::Read, &capture);
    const bool observed = CnaAudioTest::WaitUntil([&] { return capture.samples.load() >= 8820; });
    instance.Stop();
    MIX_SetTrackRawCallback(track, nullptr, nullptr);
    EXPECT_TRUE(observed) << "no complete selected buffer reached the mixer";
    EXPECT_FALSE(capture.unexpected.load()) << "mixed samples differ from the selected +0.25 PCM";
}

// CP-17/CP-23: a nonzero loop region given to the buffer-range constructor must reach the
// SoundEffectInstance it creates (SDL3_mixer exposes no way to read back the loop-start/
// max-frame play options actually passed to MIX_PlayTrack, so this checks the values that feed
// that call rather than the mixed audio output itself -- see SoundEffectInstanceTestAccess).
TEST(SoundEffectTest, BufferRangeConstructorPropagatesLoopRegionToInstance)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    std::vector<unsigned char> pcm(4 * 1000, 0); // 1000 stereo S16 frames
    SoundEffect effect(pcm, 0, static_cast<int>(pcm.size()), 44100, AudioChannels::Stereo,
                       100, 400);

    SoundEffectInstance instance = effect.CreateInstance();
    EXPECT_EQ(SoundEffectInstanceTestAccess::LoopStart(instance), 100u);
    EXPECT_EQ(SoundEffectInstanceTestAccess::LoopLength(instance), 400u);

    instance.setIsLoopedProperty(true);
    instance.Play(); // must not throw/crash with a real loop region applied
}

// P10-LOOP-005: loop region covering the entire sound (loopStart==0, loopLength==full frame
// count) is a distinct edge case from the default all-zero "loop everything" region -- confirms
// an explicitly-authored full-length region also propagates correctly and doesn't crash Play().
TEST(SoundEffectTest, BufferRangeConstructorPropagatesLoopRegionCoveringEntireSound)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    std::vector<unsigned char> pcm(4 * 1000, 0); // 1000 stereo S16 frames
    SoundEffect effect(pcm, 0, static_cast<int>(pcm.size()), 44100, AudioChannels::Stereo,
                       0, 1000);

    SoundEffectInstance instance = effect.CreateInstance();
    EXPECT_EQ(SoundEffectInstanceTestAccess::LoopStart(instance), 0u);
    EXPECT_EQ(SoundEffectInstanceTestAccess::LoopLength(instance), 1000u);

    instance.setIsLoopedProperty(true);
    instance.Play();
}

// P10-LOOP-005: loopStart+loopLength landing exactly at the sample's full length (with a nonzero
// start) is distinct from both the all-zero default and the start==0 full-cover case above.
TEST(SoundEffectTest, BufferRangeConstructorPropagatesLoopRegionEndingExactlyAtFullLength)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    std::vector<unsigned char> pcm(4 * 1000, 0); // 1000 stereo S16 frames
    SoundEffect effect(pcm, 0, static_cast<int>(pcm.size()), 44100, AudioChannels::Stereo,
                       200, 800); // 200 + 800 == 1000, the full frame count

    SoundEffectInstance instance = effect.CreateInstance();
    EXPECT_EQ(SoundEffectInstanceTestAccess::LoopStart(instance), 200u);
    EXPECT_EQ(SoundEffectInstanceTestAccess::LoopLength(instance), 800u);

    instance.setIsLoopedProperty(true);
    instance.Play();
}

// cna-killer KF-12: XNA's FromBuffer refuses a loop region outside the sound -- a negative start
// or length, or start + length past the frame count -- with ArgumentException. FNA stored the
// values unvalidated (P9-VALIDATION-002). A region that fits is kept exactly.
TEST(SoundEffectTest, BufferRangeConstructorRefusesALoopRegionOutsideTheSound)
{
    std::vector<unsigned char> pcm(4 * 1000, 0); // 1000 stereo S16 frames
    const int size = static_cast<int>(pcm.size());
    EXPECT_THROW(SoundEffect(pcm, 0, size, 44100, AudioChannels::Stereo, 900, 5000), System::ArgumentException);
    EXPECT_THROW(SoundEffect(pcm, 0, size, 44100, AudioChannels::Stereo, -1, 10), System::ArgumentException);
    EXPECT_THROW(SoundEffect(pcm, 0, size, 44100, AudioChannels::Stereo, 0, -1), System::ArgumentException);
    try
    {
        System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
        SoundEffect effect(pcm, 0, size, 44100, AudioChannels::Stereo, 900, 100);
        SoundEffectInstance instance = effect.CreateInstance();
        EXPECT_EQ(SoundEffectInstanceTestAccess::LoopStart(instance), 900u);
        EXPECT_EQ(SoundEffectInstanceTestAccess::LoopLength(instance), 100u);
    }
    catch (const Microsoft::Xna::Framework::Audio::NoAudioHardwareException&)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
}

// ===================== FromStream (headless) =====================

TEST(SoundEffectTest, FromStreamEmptyThrowsNotSupported)
{
    std::istringstream empty;
    EXPECT_THROW((void)SoundEffect::FromStream(empty), System::NotSupportedException);
}

TEST(SoundEffectTest, FromStreamGarbageThrowsNotSupported)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy"); // FromStream reaches the mixer for non-empty data
    std::istringstream garbage("this is not audio data at all");
    try
    {
        SoundEffect* loaded = SoundEffect::FromStream(garbage);
        delete loaded; // unreachable on the expected throw; avoids a leak otherwise
        FAIL() << "expected an exception";
    }
    catch (const System::NotSupportedException&)
    {
        SUCCEED();
    }
    catch (...)
    {
        GTEST_SKIP() << "audio device unavailable; could not exercise the decode path";
    }
}

TEST(SoundEffectTest, FromStreamWaveWithoutChannelsIsRefusedInsteadOfEndingTheProcess)
{
    // A corrupted WAVE (cna-killer) declaring zero channels made SDL_mixer divide by a zero frame
    // size: SIGFPE, the whole process gone.
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    auto bytes = BuildMinimalWavBytes();
    bytes[22] = 0; // "fmt " channels
    bytes[23] = 0;
    std::istringstream stream(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    try
    {
        SoundEffect* loaded = SoundEffect::FromStream(stream);
        delete loaded;
        FAIL() << "a WAVE file without channels loaded";
    }
    catch (const System::NotSupportedException& error)
    {
        EXPECT_NE(std::string(error.what()).find("no channels"), std::string::npos) << error.what();
    }
    catch (const Microsoft::Xna::Framework::Audio::NoAudioHardwareException&)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
}

TEST(SoundEffectTest, FromStreamValidWavSucceedsAndReportsNonzeroDuration)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    auto bytes = BuildMinimalWavBytes();
    std::string s(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::istringstream valid(s);

    std::unique_ptr<SoundEffect> fx;
    try
    {
        fx.reset(SoundEffect::FromStream(valid));
    }
    catch (...)
    {
        GTEST_SKIP() << "audio device unavailable; could not exercise the decode path";
    }

    ASSERT_TRUE(fx != nullptr);
    EXPECT_FALSE(fx->getIsDisposedProperty());
    // P11-TEST-001: exact value, not just non-zero -- BuildMinimalWavBytes()'s 4410 frames at
    // 44100Hz is exactly 0.1 seconds.
    EXPECT_NEAR(fx->getDurationProperty().getTotalSecondsProperty(), 0.1, 1e-6);
}

// CP-17: FromStream must parse a WAV's "smpl" chunk into a real loop region, matching FNA's own
// hand-rolled WAV parser (which scans for exactly this chunk after the "data" chunk).
TEST(SoundEffectTest, FromStreamParsesSmplChunkIntoLoopRegion)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    auto bytes = BuildWavBytesWithSmplChunk(500, 3000);
    std::string s(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::istringstream withLoop(s);

    std::unique_ptr<SoundEffect> fx;
    try
    {
        fx.reset(SoundEffect::FromStream(withLoop));
    }
    catch (...)
    {
        GTEST_SKIP() << "audio device unavailable; could not exercise the decode path";
    }
    ASSERT_TRUE(fx != nullptr);

    SoundEffectInstance instance = fx->CreateInstance();
    EXPECT_EQ(SoundEffectInstanceTestAccess::LoopStart(instance), 500u);
    EXPECT_EQ(SoundEffectInstanceTestAccess::LoopLength(instance), 2500u); // 3000 - 500
}

// A WAV with no "smpl" chunk (the common case) must leave the loop region at its default
// (0, 0) -- i.e. "loop the entire track" once IsLooped is set, not some stale/garbage value.
TEST(SoundEffectTest, FromStreamWithoutSmplChunkLeavesLoopRegionAtZero)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    auto bytes = BuildMinimalWavBytes();
    std::string s(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::istringstream noLoop(s);

    std::unique_ptr<SoundEffect> fx;
    try
    {
        fx.reset(SoundEffect::FromStream(noLoop));
    }
    catch (...)
    {
        GTEST_SKIP() << "audio device unavailable; could not exercise the decode path";
    }
    ASSERT_TRUE(fx != nullptr);

    SoundEffectInstance instance = fx->CreateInstance();
    EXPECT_EQ(SoundEffectInstanceTestAccess::LoopStart(instance), 0u);
    EXPECT_EQ(SoundEffectInstanceTestAccess::LoopLength(instance), 0u);
}

// A "smpl" chunk that declares a sample loop but is truncated before the loop-entry bytes must
// not crash/overread -- just leave the loop region at its default (0, 0).
TEST(SoundEffectTest, FromStreamWithTruncatedSmplChunkDoesNotCrash)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    auto bytes = BuildMinimalWavBytes();

    std::vector<uint8_t> smpl;
    w32(smpl, 0); w32(smpl, 0); w32(smpl, 0); w32(smpl, 0);
    w32(smpl, 0); w32(smpl, 0); w32(smpl, 0);
    w32(smpl, 1); // numSampleLoops = 1
    w32(smpl, 0); // samplerData
    // No loop-entry bytes follow, even though numSampleLoops claims one.

    tag(bytes, "smpl");
    w32(bytes, static_cast<uint32_t>(smpl.size()));
    bytes.insert(bytes.end(), smpl.begin(), smpl.end());
    const uint32_t riffPayload = static_cast<uint32_t>(bytes.size()) - 8;
    std::memcpy(bytes.data() + 4, &riffPayload, 4);

    std::string s(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::istringstream truncated(s);

    std::unique_ptr<SoundEffect> fx;
    try
    {
        fx.reset(SoundEffect::FromStream(truncated));
    }
    catch (...)
    {
        GTEST_SKIP() << "audio device unavailable; could not exercise the decode path";
    }
    ASSERT_TRUE(fx != nullptr);

    SoundEffectInstance instance = fx->CreateInstance();
    EXPECT_EQ(SoundEffectInstanceTestAccess::LoopStart(instance), 0u);
    EXPECT_EQ(SoundEffectInstanceTestAccess::LoopLength(instance), 0u);
}

// P10-SE-002: a WAV whose "fmt " chunk declares an audioFormat tag the underlying decoder does
// not support must be rejected the same way plain garbage bytes are, not silently misread.
TEST(SoundEffectTest, FromStreamUnsupportedFormatTagThrowsNotSupported)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    auto bytes = BuildWavBytesWithUnsupportedFormatTag();
    std::string s(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::istringstream stream(s);

    try
    {
        SoundEffect* loaded = SoundEffect::FromStream(stream);
        delete loaded;
        FAIL() << "expected an exception for an unsupported WAV format tag";
    }
    catch (const System::NotSupportedException&)
    {
        SUCCEED();
    }
    catch (...)
    {
        GTEST_SKIP() << "audio device unavailable; could not exercise the decode path";
    }
}

// P10-SE-002: a WAV whose "fmt " chunk is truncated (declares 16 bytes of payload but the file
// ends partway through them, with no "data" chunk at all) must be rejected, not overread.
TEST(SoundEffectTest, FromStreamTruncatedFmtChunkThrowsNotSupported)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    auto bytes = BuildWavBytesWithTruncatedFmtChunk();
    std::string s(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::istringstream stream(s);

    try
    {
        SoundEffect* loaded = SoundEffect::FromStream(stream);
        delete loaded;
        FAIL() << "expected an exception for a truncated fmt chunk";
    }
    catch (const System::NotSupportedException&)
    {
        SUCCEED();
    }
    catch (...)
    {
        GTEST_SKIP() << "audio device unavailable; could not exercise the decode path";
    }
}

// P10-SE-002: a WAV whose "data" chunk declares far more audio bytes than are actually present
// in the file must be rejected, not read past the real end of the buffer.
TEST(SoundEffectTest, FromStreamTruncatedDataChunkThrowsNotSupported)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    auto bytes = BuildWavBytesWithTruncatedDataChunk();
    std::string s(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::istringstream stream(s);

    try
    {
        SoundEffect* loaded = SoundEffect::FromStream(stream);
        delete loaded;
        FAIL() << "expected an exception for a truncated data chunk";
    }
    catch (const System::NotSupportedException&)
    {
        SUCCEED();
    }
    catch (...)
    {
        GTEST_SKIP() << "audio device unavailable; could not exercise the decode path";
    }
}

// ===================== path constructor (CNAEXT) =====================

TEST(SoundEffectTest, ConstructFromEmptyPathIsNoOp)
{
    // Empty path is a documented no-op (Impl stays default-constructed, never touches the
    // mixer), so this is safe to run without any audio device.
    SoundEffect fx("");
    EXPECT_FALSE(fx.getIsDisposedProperty());
    EXPECT_EQ(fx.getDurationProperty().getTotalSecondsProperty(), 0.0);
}

TEST(SoundEffectTest, ConstructFromNonexistentPathThrowsNotSupported)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy"); // a non-empty path reaches the mixer
    try
    {
        SoundEffect fx("/nonexistent/cna_test_path/does_not_exist_12345.wav");
        FAIL() << "expected an exception";
    }
    catch (const System::NotSupportedException&)
    {
        SUCCEED();
    }
    catch (...)
    {
        GTEST_SKIP() << "audio device unavailable; could not exercise the load path";
    }
}

// ===================== instance methods (need audio device) =====================

TEST(SoundEffectTest, ConstructFromBufferAndProperties)
{
    auto fx = makeEffect();
    if (!fx) GTEST_SKIP() << "no audio device";
    EXPECT_FALSE(fx->getIsDisposedProperty());
    // Exact value: XNA's FromBuffer stores AudioFormat.DurationFromSize(count) (FromBuffer
    // IL_00fe-IL_0105), whose TimeSpan.FromMilliseconds rounds to whole milliseconds on .NET
    // Framework (mscorlib TimeSpan.Interval IL_001d-IL_0070). makeEffect()'s 1024 stereo frames
    // at 44100 Hz are 23.22 ms -> 23 ms, not the frame-exact 1024/44100 s FNA reports.
    EXPECT_EQ(fx->getDurationProperty().getTicksProperty(), 23 * System::TimeSpan::TicksPerMillisecond);
}

TEST(SoundEffectTest, NameGetSet)
{
    auto fx = makeEffect();
    if (!fx) GTEST_SKIP() << "no audio device";
    fx->setNameProperty("explosion");
    EXPECT_EQ(fx->getNameProperty(), "explosion");
    fx->setNameProperty(std::string("boom")); // move overload
    EXPECT_EQ(fx->getNameProperty(), "boom");
}

TEST(SoundEffectTest, CreateInstanceProducesBoundInstance)
{
    auto fx = makeEffect();
    if (!fx) GTEST_SKIP() << "no audio device";
    SoundEffectInstance inst = fx->CreateInstance();
    EXPECT_FALSE(inst.getIsDisposedProperty());
}

// cna-killer KF-12: XNA's CreateInstance refuses a disposed SoundEffect with
// ObjectDisposedException; FNA created an inert instance (P9-VALIDATION-012/013).
TEST(SoundEffectTest, CreateInstanceOnDisposedSoundEffectThrowsObjectDisposed)
{
    auto fx = makeEffect();
    if (!fx) GTEST_SKIP() << "no audio device";

    fx->Dispose();
    EXPECT_THROW((void)fx->CreateInstance(), System::ObjectDisposedException);
}

// CP-22: the move-only static_asserts above only prove move-constructibility is possible, not
// that a moved-to SoundEffect's underlying resource actually still works -- mirrors CP-12's
// analogous SoundEffectInstance move tests.
TEST(SoundEffectTest, MoveConstructedEffectStillCreatesAWorkingInstance)
{
    auto fx = makeEffect();
    if (!fx) GTEST_SKIP() << "no audio device";

    SoundEffect moved(std::move(*fx));
    EXPECT_FALSE(moved.getIsDisposedProperty());

    SoundEffectInstance inst = moved.CreateInstance();
    inst.Play();
    EXPECT_EQ(inst.getStateProperty(), SoundState::Playing);
}

TEST(SoundEffectTest, MoveAssignedEffectStillCreatesAWorkingInstance)
{
    auto fx = makeEffect();
    auto other = makeEffect();
    if (!fx || !other) GTEST_SKIP() << "no audio device";

    *other = std::move(*fx);
    EXPECT_FALSE(other->getIsDisposedProperty());

    SoundEffectInstance inst = other->CreateInstance();
    inst.Play();
    EXPECT_EQ(inst.getStateProperty(), SoundState::Playing);
}

TEST(SoundEffectTest, PlayReturnsTrue)
{
    auto fx = makeEffect();
    if (!fx) GTEST_SKIP() << "no audio device";
    EXPECT_TRUE(fx->Play());
    EXPECT_TRUE(fx->Play(0.5f, 0.0f, 0.25f));
}

// P11-PAN-002 (RFC-1 for the fire-and-forget path): hard pan (pan = ±1) is exactly the case that
// used to hard-eliminate the opposite channel outright (CHECKLIST.md CP-19) before this fix
// registered a real crossfeed-matrix cooked callback on the fire-and-forget track (matching
// P11-PAN-001's design for SoundEffectInstance -- SoundEffect::Play() computes the matrix once,
// via the same SoundEffectInstance::INTERNAL_calculatePanCrossfeedMatrix already unit-tested by
// P11-PAN-001's SoundEffectInstanceFilterMathTest suite). This is a smoke/non-crash test, not a
// sample-level verification -- the fire-and-forget path exposes no way for a test to reach the
// MIX_Track it internally creates and destroys (same limitation P12-PITCH-001 already noted for
// this exact call path), so the underlying matrix math's correctness is what P11-PAN-001's own
// pure-math tests already establish; this only proves the new cooked-callback registration/
// stereo-forcing plumbing doesn't crash across the full pan range, including both hard extremes.
TEST(SoundEffectTest, PlayWithHardPanDoesNotCrash)
{
    auto fx = makeEffect();
    if (!fx) GTEST_SKIP() << "no audio device";
    EXPECT_TRUE(fx->Play(1.0f, 0.0f, 1.0f));
    EXPECT_TRUE(fx->Play(1.0f, 0.0f, -1.0f));
}

// Regression for a TSan-confirmed race between reclaiming a finished fire-and-forget pan state
// in the next Play() and SDL3_mixer's final cooked callback still reading that state. Very short
// sounds plus repeated Play() calls exercise the stopped -> final-cooked callback window; under
// TSan this used to report a delete/read race in PendingPanStateCleanup/FireAndForgetPanCallback.
TEST(SoundEffectTest, RepeatedShortFireAndForgetPlaySafelyReclaimsPanState)
{
    auto fx = makeEffect(64); // 64 stereo S16 frames (~1.5 ms)
    if (!fx) GTEST_SKIP() << "no audio device (dummy driver unavailable)";

    for (int i = 0; i < 200; ++i)
    {
        ASSERT_TRUE(fx->Play(1.0f, 0.0f, (i & 1) ? 1.0f : -1.0f));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Let the last short track stop, then make one final Play() reclaim its queued pan state.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_TRUE(fx->Play());
}

TEST(SoundEffectTest, PlayThrowsOnPanOutOfRange)
{
    // Matches SoundEffectInstance::setPanProperty's validation (CP-2): Pan is range-checked,
    // not clamped.
    auto fx = makeEffect();
    if (!fx) GTEST_SKIP() << "no audio device";
    EXPECT_THROW(fx->Play(1.0f, 0.0f, 1.5f), System::ArgumentOutOfRangeException);
    EXPECT_THROW(fx->Play(1.0f, 0.0f, -1.5f), System::ArgumentOutOfRangeException);
}

// XNA's Play(float, float, float) assigns the three values through a fresh instance's Volume,
// Pitch and Pan setters, in that order (Play IL_00a8-IL_00b8), so it refuses exactly what they
// refuse, NaN included (set_Volume/set_Pitch/set_Pan IL_002f-IL_0063: blt.un/bgt.un), each as
// ArgumentOutOfRangeException("value"). This replaces PlayClampsPitchInsteadOfThrowing, which
// pinned FNA's old clamp.
TEST(SoundEffectTest, PlayRefusesWhatTheInstanceSettersRefuse)
{
    auto fx = makeEffect();
    if (!fx) GTEST_SKIP() << "no audio device";
    const float nan = std::numeric_limits<float>::quiet_NaN();
    auto paramOf = [&](float volume, float pitch, float pan) -> std::string {
        try { (void)fx->Play(volume, pitch, pan); }
        catch (const System::ArgumentOutOfRangeException& error) { return error.getParamNameProperty(); }
        return "<no exception>";
    };
    EXPECT_EQ(paramOf(1.0f, 5.0f, 0.0f), "value");
    EXPECT_EQ(paramOf(1.0f, -1.0001f, 0.0f), "value");
    EXPECT_EQ(paramOf(1.0f, nan, 0.0f), "value");
    EXPECT_EQ(paramOf(1.0001f, 0.0f, 0.0f), "value");
    EXPECT_EQ(paramOf(-0.0001f, 0.0f, 0.0f), "value");
    EXPECT_EQ(paramOf(nan, 0.0f, 0.0f), "value");
    EXPECT_EQ(paramOf(1.0f, 0.0f, nan), "value");
    // Every boundary value is inside the range and plays.
    EXPECT_TRUE(fx->Play(0.0f, -1.0f, -1.0f));
    EXPECT_TRUE(fx->Play(1.0f, 1.0f, 1.0f));
}

// XNA's Play(float, float, float) refuses a disposed effect with ObjectDisposedException before
// it validates anything (Play IL_0035-IL_0052); FNA's old Play returned false. Dispose itself is
// idempotent (Dispose(bool) IL_0013-IL_0019 skips an already-disposed effect).
TEST(SoundEffectTest, DisposeIsIdempotentAndPlayRefusesTheDisposedEffect)
{
    auto fx = makeEffect();
    if (!fx) GTEST_SKIP() << "no audio device";
    EXPECT_FALSE(fx->getIsDisposedProperty());
    fx->Dispose();
    EXPECT_TRUE(fx->getIsDisposedProperty());
    EXPECT_NO_THROW(fx->Dispose());
    EXPECT_TRUE(fx->getIsDisposedProperty());
    EXPECT_THROW((void)fx->Play(), System::ObjectDisposedException);
    // Disposal is checked before the arguments, so even out-of-range values report disposal.
    EXPECT_THROW((void)fx->Play(2.0f, 2.0f, 2.0f), System::ObjectDisposedException);
}

TEST(SoundEffectTest, GetTypeNameIsDottedXnaName)
{
    auto fx = makeEffect();
    if (!fx) GTEST_SKIP() << "no audio device";
    EXPECT_EQ(fx->GetTypeName(), "Microsoft.Xna.Framework.Audio.SoundEffect");
}

// CP-7: a common chaining pattern is SoundEffect(...).CreateInstance() -- the SoundEffect
// temporary is destroyed at the end of the full expression, before the returned
// SoundEffectInstance is ever Play()ed. Before the CP-7 fix, Play() dereferenced a raw
// `const SoundEffect*` pointing at that already-destroyed temporary (heap-use-after-free).
TEST(SoundEffectTest, PlaySucceedsAfterOriginatingSoundEffectTemporaryIsDestroyed)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    std::vector<unsigned char> pcm(4 * 1024, 0);

    try
    {
        SoundEffectInstance inst = SoundEffect(pcm, 44100, AudioChannels::Stereo).CreateInstance();
        // The temporary SoundEffect above has already been destroyed by this point.
        inst.Play();
        EXPECT_EQ(inst.getStateProperty(), SoundState::Playing);
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device";
    }
}

// ===================== Instance-tracking + Dispose cascade (T-3G) =====================

TEST(SoundEffectTest, DisposeStopsAndDisposesLiveInstanceFromCreateInstance)
{
    auto fx = makeEffect();
    if (!fx) GTEST_SKIP() << "no audio device";

    SoundEffectInstance inst = fx->CreateInstance();
    inst.Play();
    ASSERT_EQ(inst.getStateProperty(), SoundState::Playing);
    ASSERT_FALSE(inst.getIsDisposedProperty());

    fx->Dispose();

    EXPECT_TRUE(inst.getIsDisposedProperty());
    // XNA's State refuses a disposed instance (get_State IL_0012-IL_002f) rather than answering
    // Stopped, so the cascade is observed through the refusal and the torn-down track.
    EXPECT_THROW((void)inst.getStateProperty(), System::ObjectDisposedException);
    EXPECT_EQ(SoundEffectInstanceTestAccess::GetTrack(inst), nullptr);
    // The cascaded instance must be genuinely inert afterward, not just flagged.
    EXPECT_THROW(inst.Play(), System::ObjectDisposedException);
}

TEST(SoundEffectTest, DisposeCascadesToEveryLiveInstance)
{
    auto fx = makeEffect();
    if (!fx) GTEST_SKIP() << "no audio device";

    SoundEffectInstance a = fx->CreateInstance();
    SoundEffectInstance b = fx->CreateInstance();
    a.Play();
    b.Play();
    ASSERT_FALSE(a.getIsDisposedProperty());
    ASSERT_FALSE(b.getIsDisposedProperty());

    fx->Dispose();

    EXPECT_TRUE(a.getIsDisposedProperty());
    EXPECT_TRUE(b.getIsDisposedProperty());
}

TEST(SoundEffectTest, DisposeSkipsAlreadyDisposedInstanceWithoutThrowing)
{
    auto fx = makeEffect();
    if (!fx) GTEST_SKIP() << "no audio device";

    SoundEffectInstance inst = fx->CreateInstance();
    inst.Play();
    inst.Dispose(); // unregisters itself before the SoundEffect ever gets to cascade to it

    EXPECT_NO_THROW(fx->Dispose());
    EXPECT_TRUE(inst.getIsDisposedProperty());
}

// Regression check for the CP-7-style dangling-pointer hazard this feature could reintroduce:
// once `inst` has been moved-to (dst), the SoundEffect's cascade tracking must follow the move
// and target &dst, not the (possibly since-reused) address of the original `src`.
TEST(SoundEffectTest, DisposeAfterInstanceMovedOutOfScopeDisposesTheMovedToInstance)
{
    auto fx = makeEffect();
    if (!fx) GTEST_SKIP() << "no audio device";

    SoundEffectInstance dst = fx->CreateInstance();
    {
        SoundEffectInstance src = fx->CreateInstance();
        src.Play();
        dst = std::move(src);
        // src goes out of scope here; its stack slot may be reused by later code.
    }
    ASSERT_FALSE(dst.getIsDisposedProperty());

    fx->Dispose();

    EXPECT_TRUE(dst.getIsDisposedProperty());
    EXPECT_THROW((void)dst.getStateProperty(), System::ObjectDisposedException); // get_State IL_0012-IL_002f
    EXPECT_EQ(SoundEffectInstanceTestAccess::GetTrack(dst), nullptr);
}

// Same regression as above, but for the move constructor specifically (operator= and the
// converting constructor re-point tracking via separate code paths in SoundEffectInstance.cpp).
TEST(SoundEffectTest, DisposeAfterInstanceMoveConstructedOutOfScopeDisposesTheMovedToInstance)
{
    auto fx = makeEffect();
    if (!fx) GTEST_SKIP() << "no audio device";

    std::optional<SoundEffectInstance> dst;
    {
        SoundEffectInstance src = fx->CreateInstance();
        src.Play();
        dst.emplace(std::move(src));
        // src goes out of scope here; its stack slot may be reused by later code.
    }
    ASSERT_TRUE(dst.has_value());
    ASSERT_FALSE(dst->getIsDisposedProperty());

    fx->Dispose();

    EXPECT_TRUE(dst->getIsDisposedProperty());
    EXPECT_THROW((void)dst->getStateProperty(), System::ObjectDisposedException); // get_State IL_0012-IL_002f
    EXPECT_EQ(SoundEffectInstanceTestAccess::GetTrack(*dst), nullptr);
}

// ---------------------------------------------------------------------------
// AUD-05-006: passing whole-file container bytes (WAV/Ogg/MP3/XNB) to a raw PCM16LE constructor
// is a common misuse this constructor can't reject outright (the container header still "looks
// like" valid 16-bit samples to the backend) -- these tests confirm it's at least diagnosed via
// stderr, never thrown (matches the constructor's doc comment, AUD-05-005: the fix for real
// misuse is the file-path constructor instead, not a rejection here).
// ---------------------------------------------------------------------------

TEST(SoundEffectTest, RawBufferStartingWithRiffSignatureEmitsDiagnosticWithoutThrowing)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    std::vector<unsigned char> pcm;
    const char riff[] = "RIFF\x24\x08\x00\x00WAVEfmt ";
    pcm.insert(pcm.end(), riff, riff + sizeof(riff) - 1);
    pcm.resize(pcm.size() + 4 * 256, 0); // pad to a plausible sample-frame count

    testing::internal::CaptureStderr();
    std::unique_ptr<SoundEffect> fx;
    try
    {
        fx = std::make_unique<SoundEffect>(pcm, 44100, AudioChannels::Stereo);
    }
    catch (...)
    {
        testing::internal::GetCapturedStderr();
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
    std::string captured = testing::internal::GetCapturedStderr();

    ASSERT_NE(fx, nullptr);
    EXPECT_FALSE(fx->getIsDisposedProperty()); // never throws/rejects -- advisory only
    EXPECT_NE(captured.find("RIFF"), std::string::npos) << "captured stderr: " << captured;
}

TEST(SoundEffectTest, RawBufferStartingWithXnbSignatureEmitsDiagnosticWithoutThrowing)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    std::vector<unsigned char> pcm{'X', 'N', 'B', 'w'};
    pcm.resize(pcm.size() + 4 * 256, 0);

    testing::internal::CaptureStderr();
    std::unique_ptr<SoundEffect> fx;
    try
    {
        fx = std::make_unique<SoundEffect>(pcm, 44100, AudioChannels::Stereo);
    }
    catch (...)
    {
        testing::internal::GetCapturedStderr();
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
    std::string captured = testing::internal::GetCapturedStderr();

    ASSERT_NE(fx, nullptr);
    EXPECT_NE(captured.find("XNB"), std::string::npos) << "captured stderr: " << captured;
}

// The diagnostic is a CNA log line, not a raw stderr write: a consumer that installed a sink
// (cna_logger_set_sink_ext) receives it there, and nothing reaches stderr behind the sink.
TEST(SoundEffectTest, RawBufferDiagnosticReachesAnInstalledLoggerSinkAndNotStderr)
{
    auto opened = makeEffect(); // open the mixer first, so only the diagnostic is observed
    if (!opened) GTEST_SKIP() << "no audio device";

    std::vector<unsigned char> pcm;
    const char riff[] = "RIFF\x24\x08\x00\x00WAVEfmt ";
    pcm.insert(pcm.end(), riff, riff + sizeof(riff) - 1);
    pcm.resize(pcm.size() + 4 * 256, 0);

    std::vector<std::pair<CNA::LogLevel, std::string>> audioLines;
    CNA::Logger::SetSink([&audioLines](const CNA::LogLevel level, const CNA::LogCategory category,
                                       const std::string_view message) {
        if (category == CNA::LogCategory::AUDIO)
            audioLines.emplace_back(level, std::string(message));
    });
    testing::internal::CaptureStderr();
    auto fx = std::make_unique<SoundEffect>(pcm, 44100, AudioChannels::Stereo);
    const std::string captured = testing::internal::GetCapturedStderr();
    CNA::Logger::ResetSink();

    EXPECT_TRUE(captured.empty()) << "captured stderr: " << captured;
    EXPECT_TRUE(std::ranges::any_of(audioLines, [](const auto& line) {
        return line.first == CNA::LogLevel::WARN && line.second.find("RIFF") != std::string::npos;
    }));
}

TEST(SoundEffectTest, RawBufferWithoutKnownSignatureEmitsNoDiagnostic)
{
    auto fx = makeEffect(); // all-zero PCM, no container signature
    if (!fx) GTEST_SKIP() << "no audio device";

    testing::internal::CaptureStderr();
    auto second = std::make_unique<SoundEffect>(
        std::vector<unsigned char>(4 * 256, 0), 44100, AudioChannels::Stereo);
    std::string captured = testing::internal::GetCapturedStderr();

    EXPECT_TRUE(captured.empty()) << "captured stderr: " << captured;
}

// ---------------------------------------------------------------------------
// AUD-05-007: raw PCM16 statistics that look implausible for real audio (e.g. compressed data
// without a recognizable container signature, or float32 samples byte-reinterpreted as PCM16)
// should also be diagnosed -- via byte-level entropy, since compressed/encoded bitstreams
// approach maximum entropy while real quantized audio essentially never does.
// ---------------------------------------------------------------------------

TEST(SoundEffectTest, RawBufferWithHighEntropyRandomDataEmitsDiagnosticWithoutThrowing)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    // Deterministic PRNG (fixed seed) -- genuinely high-entropy bytes, a reasonable proxy for
    // compressed/encoded data's own near-uniform byte distribution.
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> dist(0, 255);
    std::vector<unsigned char> pcm(4 * 1024);
    for (auto& b : pcm) b = static_cast<unsigned char>(dist(rng));

    testing::internal::CaptureStderr();
    std::unique_ptr<SoundEffect> fx;
    try
    {
        fx = std::make_unique<SoundEffect>(pcm, 44100, AudioChannels::Stereo);
    }
    catch (...)
    {
        testing::internal::GetCapturedStderr();
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
    std::string captured = testing::internal::GetCapturedStderr();

    ASSERT_NE(fx, nullptr);
    EXPECT_FALSE(fx->getIsDisposedProperty()); // never throws/rejects -- advisory only
    EXPECT_NE(captured.find("entropy"), std::string::npos) << "captured stderr: " << captured;
}

TEST(SoundEffectTest, RawBufferWithRealSineWaveEmitsNoEntropyDiagnostic)
{
    auto fx = makeEffect(); // sanity: confirm a device is available before the real assertion
    if (!fx) GTEST_SKIP() << "no audio device";

    // A real, continuous 440 Hz tone -- correlated sample-to-sample, nowhere near the entropy
    // of compressed/random data. Must not false-positive.
    const int frames = 1024;
    std::vector<unsigned char> pcm(static_cast<std::size_t>(frames) * 4); // stereo S16
    auto* samples = reinterpret_cast<int16_t*>(pcm.data());
    for (int i = 0; i < frames; ++i)
    {
        const double t = static_cast<double>(i) / 44100.0;
        const auto s = static_cast<int16_t>(30000.0 * std::sin(2.0 * 3.14159265358979 * 440.0 * t));
        samples[i * 2 + 0] = s;
        samples[i * 2 + 1] = s;
    }

    testing::internal::CaptureStderr();
    auto second = std::make_unique<SoundEffect>(pcm, 44100, AudioChannels::Stereo);
    std::string captured = testing::internal::GetCapturedStderr();

    EXPECT_TRUE(captured.empty()) << "captured stderr: " << captured;
}

TEST(SoundEffectTest, ZeroLengthRawBufferIsRefused)
{
    EXPECT_THROW(SoundEffect(std::vector<unsigned char>{}, 44100, AudioChannels::Stereo),
                 System::ArgumentException);
}

// XNA reports a FromBuffer effect's Duration in whole milliseconds (FromBuffer IL_00fe-IL_0105,
// AudioFormat.DurationFromSize, .NET Framework TimeSpan.FromMilliseconds rounding). One stereo
// frame at 44100 Hz is 0.0227 ms and rounds to zero; this replaced a frame-exact expectation that
// came from the mixer, not from XNA.
TEST(SoundEffectTest, OneFrameRawBufferRoundsItsDurationToZeroMilliseconds)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    try
    {
        // Exactly one stereo S16 frame: 2 channels * 2 bytes = 4 bytes.
        SoundEffect fx(std::vector<unsigned char>(4, 0), 44100, AudioChannels::Stereo);
        EXPECT_EQ(fx.getDurationProperty(), System::TimeSpan::Zero);
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
}

// The half-millisecond boundary, hand-derived: 22 mono frames at 44100 Hz are 0.4989 ms -> 0 ms,
// 23 frames 0.5215 ms -> 1 ms (round half away from zero, mscorlib TimeSpan.Interval).
TEST(SoundEffectTest, ShortRawBufferDurationsRoundAtHalfAMillisecond)
{
    UseDummyAudioDriver();
    try
    {
        SoundEffect below(std::vector<unsigned char>(44, 0), 44100, AudioChannels::Mono);
        SoundEffect above(std::vector<unsigned char>(46, 0), 44100, AudioChannels::Mono);
        EXPECT_EQ(below.getDurationProperty(), System::TimeSpan::Zero);
        EXPECT_EQ(above.getDurationProperty().getTicksProperty(), System::TimeSpan::TicksPerMillisecond);
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
}

// AUD-05-013: MIX_LoadRawAudio (unlike the separate MIX_LoadRawAudioNoCopy, which CNA does not
// use) eagerly copies the source bytes via SDL_LoadFile_IO into its own buffer -- confirmed by
// reading third_party/SDL_mixer/src/SDL_mixer.c's MIX_LoadAudioWithProperties: the
// `else if (!ondemand)` precache branch, which MIX_LoadRawAudio's call chain always takes since
// it never sets MIX_PROP_AUDIO_LOAD_ONDEMAND_BOOLEAN (only MIX_LoadRawAudioNoCopy does). This
// empirically locks that source-reading down: the source buffer is overwritten then destroyed
// immediately after construction, before the SoundEffect is used again.
TEST(SoundEffectTest, RawBufferLifetimeIsIndependentOfSourceMemoryAfterConstruction)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");

    std::unique_ptr<SoundEffect> fx;
    try
    {
        auto source = std::make_unique<std::vector<unsigned char>>(4 * 1024, 0x00);
        fx = std::make_unique<SoundEffect>(*source, 44100, AudioChannels::Stereo);

        // Overwrite then free the source buffer -- if MIX_LoadRawAudio held a pointer into it
        // instead of copying, this would corrupt or crash on the next SoundEffect access.
        std::fill(source->begin(), source->end(), static_cast<unsigned char>(0xCD));
        source.reset();
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }

    ASSERT_NE(fx, nullptr);
    // 1024 stereo frames at 44100 Hz: 23.22 ms, reported in whole milliseconds as XNA does.
    EXPECT_EQ(fx->getDurationProperty().getTicksProperty(), 23 * System::TimeSpan::TicksPerMillisecond);
    EXPECT_NO_THROW(fx->CreateInstance().Play());
}

TEST(SoundEffectTest, SimpleBufferConstructorRefusesUndefinedChannelCounts)
{
    const std::vector<unsigned char> pcm(4 * 256, 0);
    for (const int channels : {-1, 0, 3, 5})
    {
        SCOPED_TRACE(channels);
        EXPECT_THROW(SoundEffect(pcm, 44100, static_cast<AudioChannels>(channels)),
                     System::ArgumentOutOfRangeException);
    }
}

// The three-argument constructor is FromBuffer(buffer, 0, buffer.Length, sampleRate, channels,
// 0, 0) (.ctor(byte[], int, AudioChannels) IL_004c-IL_0056). Hand-derived for 3307 mono S16
// frames at 22050 Hz:
// - Duration is AudioFormat.DurationFromSize(6614) = TimeSpan.FromMilliseconds(3307 * 1000f /
//   22050f = 149.977) = 150 ms (FromBuffer IL_00fe-IL_0105; .NET Framework rounds to whole
//   milliseconds, so neither 149 ms by truncation nor FNA's frame-exact 0.149977 s);
// - the zero loop length becomes the whole sound, (0, 3307) (FromBuffer IL_00e6-IL_00ee);
// - every frame of the buffer is the sound, in order: the first 1654 frames are S16LE 0x2000
//   (+0.25) and the last 1653 are 0xC000 (-0.5), so a looped track repeats runs of exactly 1654
//   and 1653 samples. A dropped, repeated or shifted frame changes a run length.
// Only a missing device skips; any other exception is a failure.
TEST(SoundEffectTest, SimpleBufferConstructorSubmitsTheWholeBufferWithXnasDurationAndLoop)
{
    UseDummyAudioDriver();
    constexpr int frames = 3307;
    constexpr int firstHalf = 1654;
    std::vector<unsigned char> pcm(2 * frames);
    for (int frame = 0; frame < frames; ++frame)
    {
        pcm[2 * frame] = 0x00;
        pcm[2 * frame + 1] = frame < firstHalf ? 0x20 : 0xc0;
    }
    std::unique_ptr<SoundEffect> effect;
    try
    {
        effect = std::make_unique<SoundEffect>(pcm, 22050, AudioChannels::Mono);
    }
    catch (const Microsoft::Xna::Framework::Audio::NoAudioHardwareException&)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
    EXPECT_EQ(effect->getDurationProperty().getTicksProperty(), 150 * System::TimeSpan::TicksPerMillisecond);
    std::fill(pcm.begin(), pcm.end(), 0); // the effect must own a copy

    struct Capture
    {
        std::vector<float> values = std::vector<float>(3 * 3307);
        std::atomic<int> count{0};
        static void SDLCALL Read(void* data, MIX_Track*, const SDL_AudioSpec*, float* samples, int n)
        {
            auto& capture = *static_cast<Capture*>(data);
            int at = capture.count.load();
            for (int i = 0; i < n && at < static_cast<int>(capture.values.size()); ++i, ++at)
                capture.values[static_cast<std::size_t>(at)] = samples[i];
            capture.count.store(at);
        }
    } capture;
    // Capture outlives the track, including assertion failures and stack unwinding.
    auto instance = effect->CreateInstance();
    EXPECT_EQ(SoundEffectInstanceTestAccess::LoopStart(instance), 0u);
    EXPECT_EQ(SoundEffectInstanceTestAccess::LoopLength(instance), 3307u);
    instance.setIsLoopedProperty(true);
    instance.Play();
    auto* track = SoundEffectInstanceTestAccess::GetTrack(instance);
    ASSERT_NE(track, nullptr);
    MIX_SetTrackRawCallback(track, Capture::Read, &capture);
    const bool observed = CnaAudioTest::WaitUntil(
        [&] { return capture.count.load() >= static_cast<int>(capture.values.size()); });
    instance.Stop();
    MIX_SetTrackRawCallback(track, nullptr, nullptr);
    ASSERT_TRUE(observed) << "fewer than three loop periods reached the mixer";

    // Run lengths, phase-independent: the first and last runs are partial, every other run is a
    // whole half of the buffer.
    std::vector<std::pair<float, int>> runs;
    for (const float value : capture.values)
    {
        ASSERT_TRUE(value == 0.25f || value == -0.5f) << value;
        if (runs.empty() || runs.back().first != value) runs.emplace_back(value, 0);
        ++runs.back().second;
    }
    ASSERT_GE(runs.size(), 5u);
    for (std::size_t i = 1; i + 1 < runs.size(); ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_EQ(runs[i].second, runs[i].first == 0.25f ? firstHalf : frames - firstHalf);
    }
}

// ---------------------------------------------------------------------------
// AUD-05-016: a claimed offset/count near the int32 (SharpRuntime::intcs) ceiling, checked
// against a genuinely small real buffer, must be rejected by P9-VALIDATION-003's unsigned-
// arithmetic bounds check (`off > buffer.size() || cnt > buffer.size() - off`) before ever
// computing `buffer.data() + offset` or reaching the backend -- no huge real allocation is
// needed to prove this, since the check itself never depends on how large the real buffer is,
// only on the (small) real buffer failing to contain the (huge) claimed range.
// ---------------------------------------------------------------------------

TEST(SoundEffectTest, HugeCountAgainstSmallBufferThrowsBeforeReachingBackend)
{
    std::vector<unsigned char> buffer(16, 0);
    EXPECT_THROW(
        SoundEffect(buffer, 0, std::numeric_limits<SharpRuntime::intcs>::max(),
                    44100, AudioChannels::Stereo, 0, 0),
        System::ArgumentException);
}

TEST(SoundEffectTest, HugeOffsetNearIntMaxThrowsBeforeReachingBackend)
{
    std::vector<unsigned char> buffer(16, 0);
    EXPECT_THROW(
        SoundEffect(buffer, std::numeric_limits<SharpRuntime::intcs>::max(), 4,
                    44100, AudioChannels::Stereo, 0, 0),
        System::ArgumentException);
}

// The exact scenario P9-VALIDATION-003 exists for: offset + count individually look plausible
// but would overflow a plain int32 sum (undefined behavior) if computed naively instead of via
// the unsigned-arithmetic pattern.
TEST(SoundEffectTest, OffsetPlusCountThatWouldOverflowInt32ThrowsCleanly)
{
    // XNA computes offset + count checked and turns the overflow into
    // ArgumentException(InvalidOffsetCountLength); an offset past the buffer is refused before
    // that, with ArgumentException(InvalidAudioBufferOffset).
    std::vector<unsigned char> buffer(16, 0);
    const auto nearMax = std::numeric_limits<SharpRuntime::intcs>::max() - 3;
    EXPECT_THROW(SoundEffect(buffer, 4, nearMax, 44100, AudioChannels::Stereo, 0, 0),
                 System::ArgumentException);
    EXPECT_THROW(SoundEffect(buffer, nearMax, 4, 44100, AudioChannels::Stereo, 0, 0),
                 System::ArgumentException);
}

TEST(SoundEffectTest, Safe002StreamExtensionMutationsRefuseAndRecover)
{
    const auto seed = BuildMinimalWavBytes();
    const auto load = [](const std::vector<std::uint8_t>& bytes)
    {
        std::istringstream stream(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        return std::unique_ptr<SoundEffect>(SoundEffect::FromStream(stream));
    };
    const auto recover = [&]
    {
        const auto sound = load(seed);
        ASSERT_NE(sound, nullptr);
        EXPECT_EQ(sound->getDurationProperty().getTicksProperty(), 1000000);
        EXPECT_FALSE(sound->getIsDisposedProperty());
    };
    recover();
    for (const std::size_t length : {0u, 11u, 19u, 35u, 43u, 44u})
    {
        SCOPED_TRACE(length);
        const std::vector<std::uint8_t> bytes(seed.begin(), seed.begin()+length);
        EXPECT_THROW((void)load(bytes), System::NotSupportedException);
        recover();
    }
    for (const auto [offset, value] : std::vector<std::pair<std::size_t, std::uint32_t>>{
             {20,0x2000}, {22,0}, {24,0}, {32,0}, {34,65535}})
    {
        SCOPED_TRACE(offset);
            SCOPED_TRACE(value);
        auto bytes = seed;
        const unsigned width = offset == 24 ? 4 : 2;
        for (unsigned i=0; i<width; ++i) bytes[offset+i] = static_cast<std::uint8_t>(value >> (8u*i));
        EXPECT_THROW((void)load(bytes), System::NotSupportedException);
        recover();
    }
    // The SDL3 mixer extension refuses the truncated declaration that the internal decoder accepts.
    auto oversized = seed;
    for (std::size_t i=40; i<44; ++i) oversized[i]=255;
    EXPECT_THROW((void)load(oversized), System::NotSupportedException);
    recover();
}

TEST(SoundEffectTest, Safe002StreamSamplerDoesNotReadALoopFromOutsideItsChunk)
{
    auto bytes = BuildWavBytesWithSmplChunk(1, 5);
    const auto offset = BuildMinimalWavBytes().size();
    bytes[offset+4]=36; // the loop entry is physically present, but outside the declared smpl chunk
    std::istringstream stream(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    std::unique_ptr<SoundEffect> sound(SoundEffect::FromStream(stream));
    ASSERT_NE(sound, nullptr);
    auto instance = sound->CreateInstance();
    EXPECT_EQ(SoundEffectInstanceTestAccess::LoopStart(instance), 0u);
    EXPECT_EQ(SoundEffectInstanceTestAccess::LoopLength(instance), 0u);
    const auto seed = BuildMinimalWavBytes();
    std::istringstream valid(std::string(reinterpret_cast<const char*>(seed.data()), seed.size()));
    std::unique_ptr<SoundEffect> recovered(SoundEffect::FromStream(valid));
    ASSERT_NE(recovered, nullptr);
    EXPECT_EQ(recovered->getDurationProperty().getTicksProperty(), 1000000);
}

// ===================== TQ-005: XNA contracts with an IL oracle =====================

// XNA's FromBuffer rewrites a zero loop length to the whole sound and discards the start
// (FromBuffer IL_00e6-IL_00ee: `if (loopLength == 0) { loopStart = 0; loopLength = count /
// BlockAlign; }`). Without that, CNA looped [500, end) for (500, 0) where XNA loops [0, end).
TEST(SoundEffectTest, BufferConstructorTurnsAZeroLoopLengthIntoTheWholeSound)
{
    UseDummyAudioDriver();
    std::vector<unsigned char> pcm(4 * 1000, 0); // 1000 stereo S16 frames
    try
    {
        SoundEffect startOnly(pcm, 0, static_cast<int>(pcm.size()), 44100, AudioChannels::Stereo, 500, 0);
        SoundEffectInstance a = startOnly.CreateInstance();
        EXPECT_EQ(SoundEffectInstanceTestAccess::LoopStart(a), 0u);
        EXPECT_EQ(SoundEffectInstanceTestAccess::LoopLength(a), 1000u);

        // The frame count comes from count, not from the buffer: 600 bytes from offset 400 are
        // 150 frames.
        SoundEffect range(pcm, 400, 600, 44100, AudioChannels::Stereo, 0, 0);
        SoundEffectInstance b = range.CreateInstance();
        EXPECT_EQ(SoundEffectInstanceTestAccess::LoopStart(b), 0u);
        EXPECT_EQ(SoundEffectInstanceTestAccess::LoopLength(b), 150u);

        SoundEffect simple(pcm, 22050, AudioChannels::Mono); // 2000 mono frames
        SoundEffectInstance c = simple.CreateInstance();
        EXPECT_EQ(SoundEffectInstanceTestAccess::LoopStart(c), 0u);
        EXPECT_EQ(SoundEffectInstanceTestAccess::LoopLength(c), 2000u);
    }
    catch (const Microsoft::Xna::Framework::Audio::NoAudioHardwareException&)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
}

// The 7-argument constructor's Duration is AudioFormat.DurationFromSize(count) (FromBuffer
// IL_00fe-IL_0105): only the selected range counts, and it is rounded to whole milliseconds.
// Hand-derived: 2236 bytes are 559 stereo frames; 559 * 1000f / 31184f = 17.926 ms -> 18 ms. The
// 4000-byte buffer around it (1000 frames, 32.07 ms) must not leak in.
TEST(SoundEffectTest, BufferRangeDurationCountsOnlyTheRangeInWholeMilliseconds)
{
    UseDummyAudioDriver();
    std::vector<unsigned char> pcm(4000, 0);
    try
    {
        SoundEffect effect(pcm, 400, 2236, 31184, AudioChannels::Stereo, 0, 0);
        EXPECT_EQ(effect.getDurationProperty().getTicksProperty(), 18 * System::TimeSpan::TicksPerMillisecond);
        EXPECT_EQ(effect.getDurationProperty(), SoundEffect::GetSampleDuration(2236, 31184, AudioChannels::Stereo));
    }
    catch (const Microsoft::Xna::Framework::Audio::NoAudioHardwareException&)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
}

// FromBuffer's own validation order (IL_0000-IL_00db): rate, channels, buffer, offset, count,
// loop. A call that is wrong in several ways reports the first: a bad rate wins over a bad
// buffer, which only the three-argument constructor checks earlier.
TEST(SoundEffectTest, BufferRangeConstructorChecksTheRateBeforeTheBuffer)
{
    const std::vector<unsigned char> odd(7, 0);
    EXPECT_THROW(SoundEffect(odd, 0, 7, 100, AudioChannels::Stereo, 0, 0), System::ArgumentOutOfRangeException);
    EXPECT_THROW(SoundEffect(odd, 0, 7, 44100, static_cast<AudioChannels>(5), 0, 0), System::ArgumentOutOfRangeException);
    // The offset must be strictly inside the buffer: offset == size is refused even with count 0.
    const std::vector<unsigned char> pcm(16, 0);
    try { (void)SoundEffect(pcm, 16, 0, 44100, AudioChannels::Stereo, 0, 0); FAIL(); }
    catch (const System::ArgumentOutOfRangeException&) { FAIL() << "range errors are plain ArgumentException"; }
    catch (const System::ArgumentException&) {}
    // A misaligned offset with an aligned count, and a zero count.
    EXPECT_THROW(SoundEffect(pcm, 2, 4, 44100, AudioChannels::Stereo, 0, 0), System::ArgumentException);
    EXPECT_THROW(SoundEffect(pcm, 0, 0, 44100, AudioChannels::Stereo, 0, 0), System::ArgumentException);
    // loopStart + loopLength overflowing int32 is refused, not wrapped into range (IL_00ae-IL_00c1).
    EXPECT_THROW(SoundEffect(pcm, 0, 16, 44100, AudioChannels::Stereo, 2147483647, 1), System::ArgumentException);
    // The loop end may equal the frame count but not pass it: 16 stereo bytes are 4 frames.
    EXPECT_THROW(SoundEffect(pcm, 0, 16, 44100, AudioChannels::Stereo, 1, 4), System::ArgumentException);
}

// XNA's static defaults (SoundEffect..cctor IL_0000-IL_002d): SpeedOfSound 343.5, DopplerScale 1,
// DistanceScale 1, MasterVolume 1. Every test that changes one restores it, so a fresh process
// sees these; a wrong default (e.g. 343 m/s) moves every Doppler calculation.
TEST(SoundEffectTest, StaticPropertiesStartAtXnasDefaults)
{
    EXPECT_EQ(SoundEffect::getSpeedOfSoundProperty(), 343.5f);
    EXPECT_EQ(SoundEffect::getDopplerScaleProperty(), 1.0f);
    EXPECT_EQ(SoundEffect::getDistanceScaleProperty(), 1.0f);
    UseDummyAudioDriver();
    try
    {
        EXPECT_EQ(SoundEffect::getMasterVolumeProperty(), 1.0f);
    }
    catch (const Microsoft::Xna::Framework::Audio::NoAudioHardwareException&)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
}

// set_DistanceScale (IL_0000-IL_0023): `bge.un` lets NaN and -0.0 past the negative check, then
// anything <= float.Epsilon (1.401298E-45, the smallest denormal) becomes float.Epsilon. So -0.0
// is lifted to Epsilon, NaN is stored as NaN, and the smallest negative denormal is refused.
TEST(SoundEffectTest, DistanceScaleBoundariesFollowXnasUnorderedComparisons)
{
    const float saved = SoundEffect::getDistanceScaleProperty();
    const float epsilon = std::numeric_limits<float>::denorm_min();
    SoundEffect::setDistanceScaleProperty(-0.0f);
    EXPECT_EQ(SoundEffect::getDistanceScaleProperty(), epsilon);
    SoundEffect::setDistanceScaleProperty(epsilon);
    EXPECT_EQ(SoundEffect::getDistanceScaleProperty(), epsilon);
    SoundEffect::setDistanceScaleProperty(2.0f * epsilon); // above Epsilon: kept as given
    EXPECT_EQ(SoundEffect::getDistanceScaleProperty(), 2.0f * epsilon);
    SoundEffect::setDistanceScaleProperty(std::numeric_limits<float>::infinity());
    EXPECT_EQ(SoundEffect::getDistanceScaleProperty(), std::numeric_limits<float>::infinity());
    SoundEffect::setDistanceScaleProperty(std::numeric_limits<float>::quiet_NaN());
    EXPECT_TRUE(std::isnan(SoundEffect::getDistanceScaleProperty()));
    SoundEffect::setDistanceScaleProperty(3.0f);
    EXPECT_THROW(SoundEffect::setDistanceScaleProperty(-epsilon), System::ArgumentOutOfRangeException);
    EXPECT_EQ(SoundEffect::getDistanceScaleProperty(), 3.0f); // a refusal keeps the old value
    SoundEffect::setDistanceScaleProperty(saved);
}

// set_SpeedOfSound refuses `value <= 0` and NaN (`ble.un`, IL_0000-IL_0006); set_DopplerScale
// refuses `value < 0` and NaN (`blt.un`), so -0.0 is a valid DopplerScale but not a valid
// SpeedOfSound. A refusal leaves the stored value alone.
TEST(SoundEffectTest, SpeedOfSoundAndDopplerScaleBoundaries)
{
    const float speed = SoundEffect::getSpeedOfSoundProperty();
    const float doppler = SoundEffect::getDopplerScaleProperty();
    const float epsilon = std::numeric_limits<float>::denorm_min();

    SoundEffect::setSpeedOfSoundProperty(epsilon);
    EXPECT_EQ(SoundEffect::getSpeedOfSoundProperty(), epsilon);
    SoundEffect::setSpeedOfSoundProperty(std::numeric_limits<float>::infinity());
    EXPECT_EQ(SoundEffect::getSpeedOfSoundProperty(), std::numeric_limits<float>::infinity());
    SoundEffect::setSpeedOfSoundProperty(340.0f);
    EXPECT_THROW(SoundEffect::setSpeedOfSoundProperty(-0.0f), System::ArgumentOutOfRangeException);
    EXPECT_THROW(SoundEffect::setSpeedOfSoundProperty(-340.0f), System::ArgumentOutOfRangeException);
    EXPECT_EQ(SoundEffect::getSpeedOfSoundProperty(), 340.0f);

    SoundEffect::setDopplerScaleProperty(-0.0f);
    EXPECT_EQ(SoundEffect::getDopplerScaleProperty(), 0.0f);
    SoundEffect::setDopplerScaleProperty(4.5f);
    EXPECT_THROW(SoundEffect::setDopplerScaleProperty(-epsilon), System::ArgumentOutOfRangeException);
    EXPECT_EQ(SoundEffect::getDopplerScaleProperty(), 4.5f);

    SoundEffect::setSpeedOfSoundProperty(speed);
    SoundEffect::setDopplerScaleProperty(doppler);
}

// set_MasterVolume (IL_0000-IL_002c): both ends of 0..1 are accepted and stored exactly; the
// smallest step past either end is refused and the previous value kept.
TEST(SoundEffectTest, MasterVolumeAcceptsBothEndsAndRefusesTheNextStepOut)
{
    UseDummyAudioDriver();
    float saved = 1.0f;
    try { saved = SoundEffect::getMasterVolumeProperty(); }
    catch (...) { GTEST_SKIP() << "no audio device (dummy driver unavailable)"; }
    SoundEffect::setMasterVolumeProperty(0.0f);
    EXPECT_EQ(SoundEffect::getMasterVolumeProperty(), 0.0f);
    SoundEffect::setMasterVolumeProperty(1.0f);
    EXPECT_EQ(SoundEffect::getMasterVolumeProperty(), 1.0f);
    EXPECT_THROW(SoundEffect::setMasterVolumeProperty(std::nextafter(1.0f, 2.0f)), System::ArgumentOutOfRangeException);
    EXPECT_THROW(SoundEffect::setMasterVolumeProperty(-std::numeric_limits<float>::denorm_min()), System::ArgumentOutOfRangeException);
    EXPECT_EQ(SoundEffect::getMasterVolumeProperty(), 1.0f);
    SoundEffect::setMasterVolumeProperty(saved);
}

// A new effect's Name is String.Empty (every constructor's IL_0001-IL_0006). set_Name refuses a
// disposed effect first, then a null or empty name with ArgumentNullException("value")
// (IL_0000-IL_0030); a refusal keeps the old name.
TEST(SoundEffectTest, NameStartsEmptyAndRefusesEmptyAndDisposed)
{
    auto fx = makeEffect();
    if (!fx) GTEST_SKIP() << "no audio device";
    EXPECT_EQ(fx->getNameProperty(), "");
    fx->setNameProperty("laser");
    try { fx->setNameProperty(std::string()); FAIL(); }
    catch (const System::ArgumentNullException& error) { EXPECT_EQ(error.getParamNameProperty(), "value"); }
    const std::string empty;
    EXPECT_THROW(fx->setNameProperty(empty), System::ArgumentNullException); // const& overload
    EXPECT_EQ(fx->getNameProperty(), "laser");
    fx->Dispose();
    EXPECT_THROW(fx->setNameProperty("after"), System::ObjectDisposedException);
    // Disposal wins over the empty-name check.
    EXPECT_THROW(fx->setNameProperty(std::string()), System::ObjectDisposedException);
    EXPECT_EQ(fx->getNameProperty(), "laser");
}

// XNA's CreateInstance makes a fresh, independent instance each time (CreateInstance
// IL_0031-IL_0056: a new SoundEffectInstance(this, false) added to the effect's children), whose
// constructor sets Volume 1, Pitch 0, Pan 0, IsLooped false (SoundEffectInstance..ctor
// IL_0052-IL_0075); and Dispose of the effect disposes every child (Dispose(bool)).
TEST(SoundEffectTest, CreateInstanceReturnsIndependentBoundChildren)
{
    auto fx = makeEffect();
    if (!fx) GTEST_SKIP() << "no audio device";
    ASSERT_EQ(SoundEffectTestAccess::GetLiveInstanceCount(*fx), 0u);
    SoundEffectInstance first = fx->CreateInstance();
    SoundEffectInstance second = fx->CreateInstance();
    EXPECT_EQ(SoundEffectTestAccess::GetLiveInstanceCount(*fx), 2u);

    first.setVolumeProperty(0.25f);
    first.setIsLoopedProperty(true);
    first.Play();
    EXPECT_EQ(first.getStateProperty(), SoundState::Playing);
    // The second instance shares none of the first one's state.
    EXPECT_EQ(second.getStateProperty(), SoundState::Stopped);
    EXPECT_EQ(second.getVolumeProperty(), 1.0f);
    EXPECT_EQ(second.getPitchProperty(), 0.0f);
    EXPECT_EQ(second.getPanProperty(), 0.0f);
    EXPECT_FALSE(second.getIsLoopedProperty());
    second.Play();
    EXPECT_EQ(second.getStateProperty(), SoundState::Playing);
    EXPECT_NE(SoundEffectInstanceTestAccess::GetTrack(first), SoundEffectInstanceTestAccess::GetTrack(second));

    fx->Dispose();
    EXPECT_TRUE(first.getIsDisposedProperty());
    EXPECT_TRUE(second.getIsDisposedProperty());
}

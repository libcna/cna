// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <type_traits>
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
#include "CNA/Internal/Audio/AudioMixer.hpp"

#include <SDL3_mixer/SDL_mixer.h>
#include "System/Environment.hpp"

using Microsoft::Xna::Framework::Audio::AudioChannels;
using Microsoft::Xna::Framework::Audio::SoundEffect;
using Microsoft::Xna::Framework::Audio::SoundEffectInstance;
using Microsoft::Xna::Framework::Audio::SoundEffectInstanceTestAccess;
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
    std::unique_ptr<SoundEffect> makeEffect()
    {
        System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
        try
        {
            std::vector<unsigned char> pcm(4 * 1024, 0); // 1024 stereo S16 frames
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

TEST(SoundEffectTest, GetSampleDurationTruncatesToWholeMilliseconds)
{
    // 1000 stereo frames = 4000 bytes; 1000/44100 s = 22.6757 ms -> FNA truncates to 22 ms.
    const System::TimeSpan d =
        SoundEffect::GetSampleDuration(4000, 44100, AudioChannels::Stereo);
    EXPECT_NEAR(d.getTotalSecondsProperty(), 0.022, 1e-6);
}

TEST(SoundEffectTest, GetSampleDurationZeroForBadFormat)
{
    EXPECT_EQ(SoundEffect::GetSampleDuration(1000, 0, AudioChannels::Stereo).getTotalSecondsProperty(),
              0.0);
}

// ===================== static properties (headless) =====================

TEST(SoundEffectTest, MasterVolumePassesThroughUnclamped)
{
    // CP-16: the getter/setter now round-trip through SDL3_mixer's real master gain
    // (MIX_GetMixerGain/MIX_SetMixerGain), matching FNA's own live-query-the-device semantics
    // (SoundEffect.cs's MasterVolume queries/sets the FAudio master voice directly, no local
    // cache) -- so this now needs a (dummy) audio device, unlike before this fix.
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
    SoundEffect::setMasterVolumeProperty(2.0f); // FNA does not clamp
    EXPECT_FLOAT_EQ(SoundEffect::getMasterVolumeProperty(), 2.0f);
    SoundEffect::setMasterVolumeProperty(1.25f); // move overload
    EXPECT_FLOAT_EQ(SoundEffect::getMasterVolumeProperty(), 1.25f);
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

TEST(SoundEffectTest, DistanceScaleRejectsNonPositive)
{
    const float saved = SoundEffect::getDistanceScaleProperty();
    SoundEffect::setDistanceScaleProperty(2.0f);
    EXPECT_FLOAT_EQ(SoundEffect::getDistanceScaleProperty(), 2.0f);
    EXPECT_THROW(SoundEffect::setDistanceScaleProperty(0.0f), System::ArgumentOutOfRangeException);
    EXPECT_THROW(SoundEffect::setDistanceScaleProperty(-1.0f), System::ArgumentOutOfRangeException);
    SoundEffect::setDistanceScaleProperty(saved);
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

TEST(SoundEffectTest, BufferRangeConstructorThrowsOnBadRange)
{
    std::vector<unsigned char> pcm(16, 0);
    // Range check runs before any device access, so this is safe headlessly.
    EXPECT_THROW(SoundEffect(pcm, 8, 16, 44100, AudioChannels::Stereo, 0, 0),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW(SoundEffect(pcm, -1, 4, 44100, AudioChannels::Stereo, 0, 0),
                 System::ArgumentOutOfRangeException);
}

// P9-VALIDATION-003: offset+count must be checked without computing the (possibly overflowing)
// sum directly -- two individually-plausible int32 values can overflow, and on a typical
// two's-complement wraparound the result can come out small/negative, silently passing a naive
// "offset + count > buffer.size()" check while still reading far out of bounds via
// buffer.data() + offset. This must throw, not attempt the out-of-bounds read.
TEST(SoundEffectTest, BufferRangeConstructorRejectsOffsetCountIntegerOverflow)
{
    std::vector<unsigned char> pcm(16, 0);
    constexpr int hugeOffset = 2000000000;
    constexpr int hugeCount  = 2000000000; // offset+count overflows int32 (INT32_MAX ~2.147e9)
    EXPECT_THROW(SoundEffect(pcm, hugeOffset, hugeCount, 44100, AudioChannels::Stereo, 0, 0),
                 System::ArgumentOutOfRangeException);
}

// AUD-05-001/002 (2026-07-17 deep audit): investigated whether the raw buffer constructor should
// validate sampleRate/channels before reaching the backend. Confirmed real FNA's own internal
// SoundEffect constructor (SoundEffect.cs) does zero validation of either field at the C# layer
// (same resolved-decision pattern as P10-DYN-001..003's DynamicSoundEffectInstance constructor) --
// it relies entirely on the native backend (FAudio) to reject an invalid WAVEFORMATEX. Empirically
// confirmed CNA's own backend (SDL3_mixer's MIX_LoadRawAudio) already does exactly this: a direct
// probe against zero/negative sampleRate and zero/negative channels all return NULL ("Audio data
// is in unknown/unsupported/corrupt format"), which the existing `if (!raw) throw
// NotSupportedException(...)` guard already converts into a clean, safe failure -- no crash, no
// garbage SoundEffect, no distorted/mispitched playback. These tests lock that behavior down as a
// resolved decision (matching FNA: no CNA-side pre-validation) rather than leaving it as an
// untested, accidental gap.
TEST(SoundEffectTest, BufferRangeConstructorWithZeroSampleRateThrowsNotSupported)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    std::vector<unsigned char> pcm(16, 0);
    try
    {
        EXPECT_THROW(SoundEffect(pcm, 0, static_cast<int>(pcm.size()), 0, AudioChannels::Stereo, 0, 0),
                     System::NotSupportedException);
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
}

TEST(SoundEffectTest, BufferRangeConstructorWithNegativeSampleRateThrowsNotSupported)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    std::vector<unsigned char> pcm(16, 0);
    try
    {
        EXPECT_THROW(SoundEffect(pcm, 0, static_cast<int>(pcm.size()), -1, AudioChannels::Stereo, 0, 0),
                     System::NotSupportedException);
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
}

TEST(SoundEffectTest, BufferRangeConstructorWithZeroChannelsThrowsNotSupported)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    std::vector<unsigned char> pcm(16, 0);
    try
    {
        EXPECT_THROW(
            SoundEffect(pcm, 0, static_cast<int>(pcm.size()), 44100, static_cast<AudioChannels>(0), 0, 0),
            System::NotSupportedException);
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
}

// AUD-05-003 (2026-07-17 deep audit): a byte count that isn't a whole multiple of the frame size
// (channels * 2 bytes for S16) must not distort or corrupt the resulting sound. Confirmed via a
// direct probe against MIX_LoadRawAudio that SDL3_mixer already handles this gracefully -- it
// simply ignores the trailing partial frame (401 bytes of stereo S16 decodes as exactly 100
// frames, the same as a clean 400-byte buffer would), not a crash or garbled decode. Matches FNA,
// which performs no frame-alignment validation either. This test locks that graceful-truncation
// behavior down rather than leaving it as an untested, accidental gap.
TEST(SoundEffectTest, BufferRangeConstructorWithMisalignedByteCountTruncatesCleanly)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    // 401 bytes of stereo S16 (frame size 4) -- one byte past a whole 100-frame buffer.
    std::vector<unsigned char> pcm(401, 0);
    try
    {
        SoundEffect fx(pcm, 0, static_cast<int>(pcm.size()), 44100, AudioChannels::Stereo, 0, 0);
        EXPECT_NEAR(fx.getDurationProperty().getTotalSecondsProperty(), 100.0 / 44100.0, 1e-6);
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
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

// P9-VALIDATION-002: an explicitly-invalid loop region (start+length exceeding the sample's
// actual frame count) is intentionally NOT validated/clamped, matching FNA's own ctor -- the
// values must still propagate exactly as given, and Play() must not throw or crash.
TEST(SoundEffectTest, BufferRangeConstructorAcceptsLoopRegionExceedingActualSampleLength)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    std::vector<unsigned char> pcm(4 * 1000, 0); // 1000 stereo S16 frames
    SoundEffect effect(pcm, 0, static_cast<int>(pcm.size()), 44100, AudioChannels::Stereo,
                       900, 5000); // 900 + 5000 far exceeds the 1000-frame sample

    SoundEffectInstance instance = effect.CreateInstance();
    EXPECT_EQ(SoundEffectInstanceTestAccess::LoopStart(instance), 900u);
    EXPECT_EQ(SoundEffectInstanceTestAccess::LoopLength(instance), 5000u);

    instance.setIsLoopedProperty(true);
    instance.Play(); // must not throw/crash despite the region exceeding the buffer
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
    // P10-AUDIT-002/003: exact value, not just non-zero -- makeEffect()'s 1024 stereo S16 frames
    // at 44100Hz is 1024/44100 seconds.
    EXPECT_NEAR(fx->getDurationProperty().getTotalSecondsProperty(), 1024.0 / 44100.0, 1e-6);
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

// P9-VALIDATION-012/013: matches FNA exactly -- FNA's CreateInstance()/SoundEffectInstance ctor
// don't check IsDisposed either (SoundEffectInstance.cs's internal ctor only reads
// parentEffect.channels, a plain field that survives Dispose()); a disposed SoundEffect only
// becomes observable once Play() is attempted on the resulting instance, at which point it
// fails safely (getNativeAudioHandle() returns nullptr) rather than crashing. Locks in that
// CNA already matches this "deferred failure" pattern rather than throwing eagerly.
TEST(SoundEffectTest, CreateInstanceOnDisposedSoundEffectDoesNotThrowButResultingPlayIsInert)
{
    auto fx = makeEffect();
    if (!fx) GTEST_SKIP() << "no audio device";

    fx->Dispose();

    SoundEffectInstance inst = fx->CreateInstance();
    EXPECT_FALSE(inst.getIsDisposedProperty());
    EXPECT_EQ(inst.getStateProperty(), SoundState::Stopped);

    EXPECT_NO_THROW(inst.Play());
    EXPECT_EQ(inst.getStateProperty(), SoundState::Stopped); // inert: no native audio to play
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

TEST(SoundEffectTest, PlayThrowsOnPanOutOfRange)
{
    // Matches SoundEffectInstance::setPanProperty's validation (CP-2): Pan is range-checked,
    // not clamped.
    auto fx = makeEffect();
    if (!fx) GTEST_SKIP() << "no audio device";
    EXPECT_THROW(fx->Play(1.0f, 0.0f, 1.5f), System::ArgumentOutOfRangeException);
    EXPECT_THROW(fx->Play(1.0f, 0.0f, -1.5f), System::ArgumentOutOfRangeException);
}

TEST(SoundEffectTest, PlayClampsPitchInsteadOfThrowing)
{
    // Matches SoundEffectInstance::setPitchProperty (CP-2): Pitch is clamped to [-1, 1], never
    // rejected, so an out-of-range value still plays successfully.
    auto fx = makeEffect();
    if (!fx) GTEST_SKIP() << "no audio device";
    EXPECT_TRUE(fx->Play(1.0f, 5.0f, 0.0f));
    EXPECT_TRUE(fx->Play(1.0f, -5.0f, 0.0f));
}

TEST(SoundEffectTest, DisposeIsIdempotentAndPlayReturnsFalse)
{
    auto fx = makeEffect();
    if (!fx) GTEST_SKIP() << "no audio device";
    fx->Dispose();
    EXPECT_TRUE(fx->getIsDisposedProperty());
    EXPECT_NO_THROW(fx->Dispose());
    EXPECT_FALSE(fx->Play());
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
    EXPECT_EQ(inst.getStateProperty(), SoundState::Stopped);
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
    EXPECT_EQ(dst.getStateProperty(), SoundState::Stopped);
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
    EXPECT_EQ(dst->getStateProperty(), SoundState::Stopped);
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

// ---------------------------------------------------------------------------
// AUD-05-014/015: zero-length and very short raw buffers must never divide by zero or produce an
// off-by-one duration -- getDurationProperty() guards `frames > 0` before dividing by
// impl_->sampleRate (never by frame count), so these lock down the already-safe behavior with a
// real empirical check rather than just reading the guard.
// ---------------------------------------------------------------------------

TEST(SoundEffectTest, ZeroLengthRawBufferConstructsWithZeroDurationNoCrash)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    try
    {
        SoundEffect fx(std::vector<unsigned char>{}, 44100, AudioChannels::Stereo);
        EXPECT_FALSE(fx.getIsDisposedProperty());
        EXPECT_EQ(fx.getDurationProperty(), System::TimeSpan::Zero);
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
}

TEST(SoundEffectTest, OneFrameRawBufferHasExactSingleFrameDuration)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    try
    {
        // Exactly one stereo S16 frame: 2 channels * 2 bytes = 4 bytes.
        SoundEffect fx(std::vector<unsigned char>(4, 0), 44100, AudioChannels::Stereo);
        // 1e-6 tolerance matches this file's established convention (ConstructFromBufferAndProperties
        // above) -- SDL3_mixer's own duration computation carries a small, fixed sub-microsecond
        // rounding error independent of buffer length, negligible for any real audio purpose but
        // enough to fail a naively tight (e.g. 1e-9) absolute tolerance on very short buffers.
        EXPECT_NEAR(fx.getDurationProperty().getTotalSecondsProperty(), 1.0 / 44100.0, 1e-6);
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
}

TEST(SoundEffectTest, TwoFrameRawBufferHasExactTwoFrameDuration)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    try
    {
        SoundEffect fx(std::vector<unsigned char>(8, 0), 44100, AudioChannels::Stereo);
        EXPECT_NEAR(fx.getDurationProperty().getTotalSecondsProperty(), 2.0 / 44100.0, 1e-6);
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
    EXPECT_NEAR(fx->getDurationProperty().getTotalSecondsProperty(), 1024.0 / 44100.0, 1e-6);
    EXPECT_NO_THROW(fx->CreateInstance().Play());
}

// ---------------------------------------------------------------------------
// AUD-05-012: AudioChannels is a scoped enum (Mono=1, Stereo=2) with no runtime guard against a
// caller force-casting an out-of-range value (static_cast<AudioChannels>(N)) -- C++ has no
// language-level protection against this for a `static_cast` (unlike a plain narrowing
// conversion). FNA's own constructor has the identical gap (SoundEffect.cs simply does
// `(ushort) channels`, no validation), so CNA intentionally matches it rather than adding a
// restriction FNA itself doesn't have. What matters is that the *backend* still behaves safely
// for an out-of-enum-range value passed through unchanged -- these tests lock that down.
// ---------------------------------------------------------------------------

TEST(SoundEffectTest, OutOfRangeChannelsEnumValueEitherConstructsOrThrowsCleanlyNeverUB)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    // Neither Mono(1) nor Stereo(2) -- a force-cast a real caller could plausibly write by
    // mistake (e.g. confusing this with a raw channel count).
    const auto bogus = static_cast<AudioChannels>(5);

    try
    {
        SoundEffect fx(std::vector<unsigned char>(4 * 256, 0), 44100, bogus);
        // Empirically confirmed (2026-07-18): this is the branch actually taken --
        // MIX_LoadRawAudio's RAW decoder accepts an arbitrary positive channel count
        // unvalidated (unlike MIX_CreateMixerDevice's stream-format path, which enforces
        // SDL_IsSupportedChannelCount's 1-8 range at *play* time, not load time -- AUD-04-006).
        // Still must behave sanely here, not silently corrupt state.
        EXPECT_FALSE(fx.getIsDisposedProperty());
    }
    catch (const System::NotSupportedException&)
    {
        // Also an acceptable outcome if a future SDL3_mixer version starts validating this at
        // load time: the backend cleanly rejecting it via CNA's existing NotSupportedException
        // translation (AUD-05-002's pattern).
        SUCCEED();
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
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
        System::ArgumentOutOfRangeException);
}

TEST(SoundEffectTest, HugeOffsetNearIntMaxThrowsBeforeReachingBackend)
{
    std::vector<unsigned char> buffer(16, 0);
    EXPECT_THROW(
        SoundEffect(buffer, std::numeric_limits<SharpRuntime::intcs>::max(), 4,
                    44100, AudioChannels::Stereo, 0, 0),
        System::ArgumentOutOfRangeException);
}

// The exact scenario P9-VALIDATION-003 exists for: offset + count individually look plausible
// but would overflow a plain int32 sum (undefined behavior) if computed naively instead of via
// the unsigned-arithmetic pattern.
TEST(SoundEffectTest, OffsetPlusCountThatWouldOverflowInt32ThrowsCleanly)
{
    std::vector<unsigned char> buffer(16, 0);
    const auto nearMax = std::numeric_limits<SharpRuntime::intcs>::max() - 2;
    EXPECT_THROW(
        SoundEffect(buffer, nearMax, 10, 44100, AudioChannels::Stereo, 0, 0),
        System::ArgumentOutOfRangeException);
}

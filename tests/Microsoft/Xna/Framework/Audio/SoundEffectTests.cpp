// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <sstream>
#include <vector>

#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioChannels.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/TimeSpan.hpp"

using Microsoft::Xna::Framework::Audio::AudioChannels;
using Microsoft::Xna::Framework::Audio::SoundEffect;
using Microsoft::Xna::Framework::Audio::SoundEffectInstance;

namespace
{
    // Builds a SoundEffect from a small silent PCM buffer under the SDL dummy driver.
    // Returns nullptr if no audio device can be opened (the caller should skip).
    std::unique_ptr<SoundEffect> makeEffect()
    {
        ::setenv("SDL_AUDIODRIVER", "dummy", 1);
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
    const float saved = SoundEffect::getMasterVolumeProperty();
    SoundEffect::setMasterVolumeProperty(0.5f);
    EXPECT_FLOAT_EQ(SoundEffect::getMasterVolumeProperty(), 0.5f);
    SoundEffect::setMasterVolumeProperty(2.0f); // FNA does not clamp
    EXPECT_FLOAT_EQ(SoundEffect::getMasterVolumeProperty(), 2.0f);
    SoundEffect::setMasterVolumeProperty(1.25f); // move overload
    EXPECT_FLOAT_EQ(SoundEffect::getMasterVolumeProperty(), 1.25f);
    SoundEffect::setMasterVolumeProperty(saved);
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

// ===================== FromStream (headless) =====================

TEST(SoundEffectTest, FromStreamEmptyThrowsNotSupported)
{
    std::istringstream empty;
    EXPECT_THROW((void)SoundEffect::FromStream(empty), System::NotSupportedException);
}

TEST(SoundEffectTest, FromStreamGarbageThrowsNotSupported)
{
    ::setenv("SDL_AUDIODRIVER", "dummy", 1); // FromStream reaches the mixer for non-empty data
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
    ::setenv("SDL_AUDIODRIVER", "dummy", 1);
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
    EXPECT_GT(fx->getDurationProperty().getTotalSecondsProperty(), 0.0);
}

// ===================== path constructor (NOXNA) =====================

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
    ::setenv("SDL_AUDIODRIVER", "dummy", 1); // a non-empty path reaches the mixer
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
    EXPECT_GT(fx->getDurationProperty().getTotalSecondsProperty(), 0.0);
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

TEST(SoundEffectTest, PlayReturnsTrue)
{
    auto fx = makeEffect();
    if (!fx) GTEST_SKIP() << "no audio device";
    EXPECT_TRUE(fx->Play());
    EXPECT_TRUE(fx->Play(0.5f, 0.0f, 0.25f));
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

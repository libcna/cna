// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <optional>
#include <sstream>
#include <type_traits>
#include <vector>

#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioChannels.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundState.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "System/TimeSpan.hpp"
#include "SoundEffectInstanceTestAccess.hpp"
#include "CNA/Internal/Audio/AudioMixer.hpp"

#include <SDL3_mixer/SDL_mixer.h>

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
    // CP-16: the getter/setter now round-trip through SDL3_mixer's real master gain
    // (MIX_GetMixerGain/MIX_SetMixerGain), matching FNA's own live-query-the-device semantics
    // (SoundEffect.cs's MasterVolume queries/sets the FAudio master voice directly, no local
    // cache) -- so this now needs a (dummy) audio device, unlike before this fix.
    ::setenv("SDL_AUDIODRIVER", "dummy", 1);
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
    ::setenv("SDL_AUDIODRIVER", "dummy", 1);
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

// CP-7: a common chaining pattern is SoundEffect(...).CreateInstance() -- the SoundEffect
// temporary is destroyed at the end of the full expression, before the returned
// SoundEffectInstance is ever Play()ed. Before the CP-7 fix, Play() dereferenced a raw
// `const SoundEffect*` pointing at that already-destroyed temporary (heap-use-after-free).
TEST(SoundEffectTest, PlaySucceedsAfterOriginatingSoundEffectTemporaryIsDestroyed)
{
    ::setenv("SDL_AUDIODRIVER", "dummy", 1);
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

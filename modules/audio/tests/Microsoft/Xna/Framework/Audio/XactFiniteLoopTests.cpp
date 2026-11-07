// SPDX-License-Identifier: MS-PL
// This source also enters the normal module test glob. The dedicated executable
// alone wraps the device factory; it must never interpose on other test binaries.
#if defined(CNA_GAP02_OFFLINE_HARNESS)
#include <gtest/gtest.h>
#include "CNA/TestSupport/TestScratch.hpp"
#include "CNA/Audio/Platform/IAudioDevice.hpp"
#include "CNA/Internal/Audio/MixerEngine.hpp"
#include "CNA/Internal/Audio/AudioMixer.hpp"
#include "../../../../../src/Internal/WetBus.hpp"
#include "CNA/Internal/Audio/XactTypes.hpp"
#include "CNA/Internal/Audio/EncodedWaveBankDecoder.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundBank.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Audio/WaveBank.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "CueTestAccess.hpp"
#include "AudioEngineTestAccess.hpp"
#include "SoundBankTestAccess.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioCategory.hpp"
#include "SoundEffectInstanceTestAccess.hpp"
#include "WaveBankTestAccess.hpp"
#include <algorithm>
#include <atomic>
#include <barrier>
#include <future>
#include <thread>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <map>
#include <cmath>
#include <span>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace {
namespace AP = CNA::Audio::Platform;
using namespace CNA::Internal::Audio;
using namespace Microsoft::Xna::Framework::Audio;

class ManualDevice;
ManualDevice* manualDevice = nullptr;
int factoryCalls = 0;
class ManualDevice final : public AP::IAudioDevice
{
    std::shared_ptr<AP::IAudioBufferCallback> callback_;
    bool running_ = false;
public:
    ManualDevice() { manualDevice = this; ++factoryCalls; }
    AP::AudioFormat Open(const AP::AudioFormat&,
                        std::shared_ptr<AP::IAudioBufferCallback> callback) override
    {
        callback_ = std::move(callback);
        return GetFormat();
    }
    void Start() override { running_ = true; }
    void Stop() noexcept override { running_ = false; }
    void Close() noexcept override { Stop(); callback_.reset(); }
    bool IsOpen() const noexcept override { return bool(callback_); }
    bool IsRunning() const noexcept override { return running_; }
    AP::AudioFormat GetFormat() const noexcept override
    { return callback_ ? AP::AudioFormat{44100, 2, AP::AudioSampleFormat::Float32} : AP::AudioFormat{}; }
    std::vector<float> Render(int frames)
    {
        if (!running_ || !callback_) throw std::runtime_error("manual device is not running");
        std::vector<float> result(static_cast<std::size_t>(frames) * 2, -99.0f);
        callback_->FillBuffer(std::as_writable_bytes(std::span(result)), result.size());
        return result;
    }
};

using Bytes = std::vector<std::uint8_t>;
void U8(Bytes& b, std::uint8_t v) { b.push_back(v); }
void U16(Bytes& b, std::uint16_t v) { U8(b, v & 255); U8(b, v >> 8); }
void U32(Bytes& b, std::uint32_t v) { U16(b, v & 65535); U16(b, v >> 16); }
void Text(Bytes& b, const char* s, std::size_t width)
{
    const auto start = b.size();
    b.resize(start + width, 0);
    std::memcpy(b.data() + start, s, std::min(width, std::strlen(s)));
}

Bytes Xgs()
{
    // Literal XGS layout: 65-byte header, one 10-byte category, then "Default\0".
    Bytes b; Text(b, "XGSF", 4); U16(b, 46); U16(b, 0); U16(b, 0);
    b.resize(18, 0); U8(b, 3);
    U16(b, 1); for (int i = 0; i < 6; ++i) U16(b, 0);
    U32(b, 65); U32(b, 0); U32(b, 0); U32(b, 0);
    U32(b, 0); U32(b, 0); U32(b, 75); U32(b, 0);
    U8(b, 255); U16(b, 0); U16(b, 0); U8(b, 0); U16(b, 65535);
    U8(b, 180); U8(b, 0); Text(b, "Default", 8);
    return b;
}

Bytes Xsb(std::uint8_t count)
{
    // XSB v46: bank names at 74/138, complex sound at 202; track table
    // 9+1+9 bytes, event count at 221, one 16-byte PlayWave at 222.
    constexpr std::uint32_t sound = 202, events = 221, cue = 238, names = 243, text = 249;
    Bytes b; Text(b, "SDBK", 4); U16(b, 46); U16(b, 0); U16(b, 0);
    b.resize(18, 0); U8(b, 0);
    U16(b, 1); U16(b, 0); U16(b, 0); U16(b, 0); U8(b, 1);
    U16(b, 1); U16(b, 0); U16(b, 0);
    U32(b, cue); U32(b, -1); U32(b, -1); U32(b, 0); U32(b, -1);
    U32(b, 0); U32(b, 138); U32(b, 0); U32(b, names); U32(b, sound);
    Text(b, "Gap02Sound", 64); Text(b, "Gap02Wave", 64);
    U8(b, 1); U16(b, 0); U8(b, 180); U16(b, 0); U8(b, 0); U16(b, 0);
    U8(b, 1); U8(b, 180); U32(b, events); U16(b, 0); U16(b, 0);
    U8(b, 1); U32(b, 1); U16(b, 0); U8(b, 255); // one event, timestamp zero
    U8(b, 0); U16(b, 0); U8(b, 0); U8(b, count); U16(b, 0); U16(b, 0);
    U8(b, 0); U32(b, sound); U32(b, text); U16(b, 0); Text(b, "Loop", 5);
    return b;
}

Bytes Xwb(int codec, bool region, bool streamed, bool throughEnd = false)
{
    const bool adpcm = codec == 1, pcm8 = codec == 2;
    // Four 48-frame blocks with markers 1/8, 1/4, 3/8, 1/2. Loop is
    // [48,144): 48 prefix + 96 region + 48 suffix = 192 source frames.
    const std::uint32_t bytes = adpcm ? 120 : pcm8 ? 192 : 384;
    Bytes b; Text(b, "WBND", 4); U32(b, 1);
    for (const auto [offset, length] : std::array<std::pair<int,int>,5>{
             {{48,96},{144,24},{168,0},{168,0},{168,static_cast<int>(bytes)}}})
    { U32(b, offset); U32(b, length); }
    U32(b, streamed ? 1 : 0); U32(b, 1); Text(b, "Gap02Wave", 64);
    U32(b, 24); U32(b, 0); U32(b, 4); U32(b, 0); U32(b, 0); U32(b, 0);
    U32(b, 192u << 4);
    U32(b, adpcm ? (2u | (1u<<2) | (44100u<<5) | (8u<<23))
                 : ((1u<<2) | (44100u<<5) | ((pcm8 ? 1u : 2u)<<23) | (pcm8 ? 0u : (1u<<31))));
    U32(b, 0); U32(b, bytes); U32(b, region ? 48 : 0); U32(b, region && !throughEnd ? 96 : 0);
    for (int block = 0; block < 4; ++block)
    {
        const auto marker = static_cast<std::uint16_t>((block + 1) * 4096);
        if (adpcm)
        {
            // MS-ADPCM predictor 0: (256*s1 + 0*s2)/256 = s1.
            // s1=s2=marker, delta=16, zero nibbles => all 48 decoded samples
            // equal marker. 7 header bytes + 23 bytes * 2 nibbles = 48 frames.
            U8(b, 0); U16(b, 16); U16(b, marker); U16(b, marker);
            for (int i = 0; i < 23; ++i) U8(b, 0);
        }
        else if (pcm8) for (int i = 0; i < 48; ++i) U8(b, 128 + marker/256);
        else for (int i = 0; i < 48; ++i) U16(b, marker);
    }
    return b;
}

class Banks
{
    std::filesystem::path directory_;
    std::string Write(const char* name, const Bytes& bytes)
    {
        const auto path = directory_ / name;
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!f) throw std::runtime_error("could not write literal XACT fixture");
        return path.string();
    }
public:
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<WaveBank> wave;
    std::unique_ptr<SoundBank> sound;
    Banks(std::uint8_t count, int codec, bool region, bool streamed, bool throughEnd = false)
    {
        directory_ = CNA::TestSupport::FixtureRoot() / "gap02-fixture";
        std::filesystem::create_directories(directory_);
        engine = std::make_unique<AudioEngine>(Write("fixture.xgs", Xgs()));
        const auto xwb = Write("fixture.xwb", Xwb(codec, region, streamed, throughEnd));
        if (streamed) wave = std::make_unique<WaveBank>(engine.get(), xwb, 0, 2);
        else wave = std::make_unique<WaveBank>(engine.get(), xwb);
        sound = std::make_unique<SoundBank>(engine.get(), Write("fixture.xsb", Xsb(count)));
    }
    ~Banks()
    {
        sound.reset(); wave.reset(); engine.reset();
        std::error_code ignored; std::filesystem::remove_all(directory_, ignored);
    }
    std::unique_ptr<Cue> Play()
    {
        std::unique_ptr<Cue> result(sound->GetCue("Loop"));
        result->Play();
        auto* instance = CueTestAccess::ActiveInstance(*result, 0);
        if (!instance) throw std::runtime_error("fixture did not resolve a real wave");
        // Isolate looping from XACT byte-volume conversion (tested elsewhere).
        // Device has not rendered anything, so every observed frame has unity gain.
        instance->setVolumeProperty(1.0f);
        return result;
    }
    void Update(Cue& cue)
    {
        engine->Update();
        if(auto* instance=CueTestAccess::EventVoice(cue,0,0))instance->setVolumeProperty(1);
    }
};

float Marker(int frame) { return static_cast<float>((frame / 48 + 1) * 4096) / 32768.0f; }
int Total(int count, bool region) { return region ? 192 + count * 96 : 192 * (count + 1); }
float Expected(int frame, int count, bool region)
{
    // Wave-level finite loop contract: prefix, count+1 passes, suffix once.
    // Oracle: FACT.c FACTWaveBank_Prepare:1871-1890 + literal loop arithmetic.
    // Event counts and complete-wave tests are separate; see docs/xact-looping.md.
    if (count != 255 && frame >= Total(count, region)) return 0.0f;
    if (!region || count == 0) return Marker(frame % 192);
    if (frame < 48) return Marker(frame);
    if (count != 255 && frame >= 48 + 96 * (count + 1))
        return Marker(144 + frame - (48 + 96 * (count + 1)));
    return Marker(48 + (frame - 48) % 96);
}
void ExpectOutput(const std::vector<float>& actual, int start, int count, bool region)
{
    ASSERT_EQ(actual.size() % 2, 0u);
    for (std::size_t i = 0; i < actual.size()/2; ++i)
    {
        const float expected = Expected(start + static_cast<int>(i), count, region);
        // Native-rate F32 output, integer markers/32768: exact dyadic values.
        // 1e-6 allows only float conversion noise, far below the 1/8 marker gap.
        ASSERT_NEAR(actual[i*2], expected, 1e-6f) << "left frame " << start+i;
        ASSERT_NEAR(actual[i*2+1], expected, 1e-6f) << "right frame " << start+i;
    }
}
void ExpectSilence(const std::vector<float>& actual)
{
    for (float f : actual) ASSERT_EQ(f, 0.0f); // no active source => literal zero
}

class XactFiniteLoop : public ::testing::TestWithParam<std::tuple<int,bool,bool>>
{
protected:
    void SetUp() override
    {
        EnsureMixer();
        ASSERT_EQ(factoryCalls, 1) << "real selected device must never be opened";
        ASSERT_NE(manualDevice, nullptr);
        SoundEffect::setMasterVolumeProperty(1.0f);
    }
};

#include "EncodedWaveBankCases.hpp"

TEST_P(XactFiniteLoop, AuthoredCountsHaveExactOutputAndCompletion)
{
    const auto [codec, region, streamed] = GetParam();
    for (int count : {0,1,2,254,255})
    {
        SCOPED_TRACE(count);
        const auto parsed = ParseXsb(Xsb(static_cast<std::uint8_t>(count)));
        ASSERT_EQ(parsed.sounds.size(), 1u);
        ASSERT_EQ(parsed.sounds[0].waves.size(), 1u);
        ASSERT_EQ(parsed.sounds[0].waves[0].loopCount, count); // literal byte in Xsb()
        Banks banks(static_cast<std::uint8_t>(count), codec, region, streamed);
        auto cue = banks.Play();
        ASSERT_TRUE(cue->getIsPlayingProperty());
        if(count==255)ExpectOutput(manualDevice->Render(Total(255,region)+32),0,255,region);
        else for(int pass=0;pass<std::max(count,1);++pass)
        {
            // Event counts include the initial complete wave; buffer repeats do not.
            ExpectOutput(manualDevice->Render(192),0,0,false);
            ExpectSilence(manualDevice->Render(32));banks.Update(*cue);
            EXPECT_EQ(cue->getIsStoppedProperty(),pass+1==std::max(count,1));
        }
        EXPECT_EQ(cue->getIsStoppedProperty(), count != 255);
        EXPECT_EQ(cue->getIsPlayingProperty(), count == 255);
        cue->Stop(AudioStopOptions::Immediate);
        ExpectSilence(manualDevice->Render(32));
    }
}

TEST_P(XactFiniteLoop, FiniteCountsCannotSurviveAnExtraWholePassBudget)
{
    const auto [codec, region, streamed] = GetParam();
    for (int count : {0,1,2,254})
    {
        SCOPED_TRACE(count);
        Banks banks(static_cast<std::uint8_t>(count), codec, region, streamed);
        auto cue = banks.Play();
        for(int pass=0;pass<std::max(count,1);++pass)
        {
            ExpectOutput(manualDevice->Render(192),0,0,false);
            ExpectSilence(manualDevice->Render(32));banks.Update(*cue);
        }
        // No further pass after the native event budget, including raw0/raw1.
        ExpectSilence(manualDevice->Render(192));
        ExpectSilence(manualDevice->Render(32));
        EXPECT_TRUE(cue->getIsStoppedProperty());
        EXPECT_FALSE(cue->getIsPlayingProperty());
    }
}

TEST_P(XactFiniteLoop, PauseResumePreservesPositionAndRemainingPasses)
{
    const auto [codec, region, streamed] = GetParam();
    Banks banks(2, codec, region, streamed);
    auto cue = banks.Play();
    ExpectOutput(manualDevice->Render(64), 0, 2, region);
    cue->Pause();
    EXPECT_TRUE(cue->getIsPausedProperty()); EXPECT_TRUE(cue->getIsPlayingProperty());
    ExpectSilence(manualDevice->Render(1024));
    cue->Resume();
    ExpectOutput(manualDevice->Render(128),64,0,false);
    ExpectSilence(manualDevice->Render(32));banks.Update(*cue);
    ExpectOutput(manualDevice->Render(192),0,0,false);
    ExpectSilence(manualDevice->Render(32));banks.Update(*cue);
    EXPECT_TRUE(cue->getIsStoppedProperty());
}

TEST_P(XactFiniteLoop, BothCueStopsSilenceAndFreshCueRestartsBudget)
{
    const auto [codec, region, streamed] = GetParam();
    for (auto stop : {AudioStopOptions::Immediate, AudioStopOptions::AsAuthored})
    {
        Banks banks(2, codec, region, streamed);
        auto cue = banks.Play();
        ExpectOutput(manualDevice->Render(64), 0, 2, region);
        cue->Stop(stop); // FACTCue_Stop: no fade/RPC release authored => hard stop
        EXPECT_TRUE(cue->getIsStoppedProperty());
        ExpectSilence(manualDevice->Render(1024));
        cue->Play(); // A Cue is single-use; a fresh GetCue is the replay route.
        ExpectSilence(manualDevice->Render(32));
        auto fresh = banks.Play();
        for(int pass=0;pass<2;++pass)
        {
            ExpectOutput(manualDevice->Render(192),0,0,false);
            ExpectSilence(manualDevice->Render(32));banks.Update(*fresh);
        }
        EXPECT_TRUE(fresh->getIsStoppedProperty());
    }
}

TEST_P(XactFiniteLoop, FiniteEventsReplayCompleteWavesWhenRegionExtendsThroughEof)
{
    const auto [codec, region, streamed] = GetParam();
    Banks banks(2, codec, region, streamed, true);
    auto cue = banks.Play();
    // Finite events pass wave loop0, so a through-EOF region cannot omit the prefix.
    for(int pass=0;pass<2;++pass)
    {
        ExpectOutput(manualDevice->Render(192),0,0,false);
        ExpectSilence(manualDevice->Render(32));banks.Update(*cue);
    }
    EXPECT_TRUE(cue->getIsStoppedProperty());
}

TEST_P(XactFiniteLoop, DisposalCancelsFinitePlayback)
{
    const auto [codec, region, streamed] = GetParam();
    Banks banks(2, codec, region, streamed);
    auto cue = banks.Play();
    ExpectOutput(manualDevice->Render(64), 0, 2, region);
    cue->Dispose();
    EXPECT_TRUE(cue->getIsDisposedProperty());
    ExpectSilence(manualDevice->Render(1024));
}

INSTANTIATE_TEST_SUITE_P(Pcm8Pcm16AdpcmWholeRegionResidentStream, XactFiniteLoop,
    ::testing::Combine(::testing::Values(0,1,2), ::testing::Bool(), ::testing::Bool()));

TEST(XactFiniteVoice, PublicBooleanStillMeansOnceOrInfinite)
{
    EnsureMixer();
    Bytes pcm; for (int i=0; i<192; ++i) U16(pcm, (i/48+1)*4096);
    SoundEffect effect(pcm, 44100, AudioChannels::Mono);
    for (bool looped : {false,true})
    {
        auto instance = effect.CreateInstance();
        instance.setIsLoopedProperty(looped);
        EXPECT_EQ(instance.getIsLoopedProperty(), looped); // Refresh get_IsLooped IL_0000-0006
        instance.Play();
        ExpectOutput(manualDevice->Render(608), 0, looped ? 255 : 0, false);
        EXPECT_EQ(instance.getStateProperty(), looped ? SoundState::Playing : SoundState::Stopped);
        // Refresh set_IsLooped IL_001e-0030 refuses after packet submission.
        EXPECT_THROW(instance.setIsLoopedProperty(looped), System::InvalidOperationException);
        instance.Stop();
    }
}

TEST(XactFiniteVoice, AuthoredExitPlaysSuffixAndReplayRestoresFiniteCount)
{
    EnsureMixer();
    Bytes pcm; for (int i=0; i<192; ++i) U16(pcm, (i/48+1)*4096);
    SoundEffect effect(pcm, 0, static_cast<int>(pcm.size()), 44100, AudioChannels::Mono, 48, 96);
    auto instance = effect.CreateInstance();
    SoundEffectInstanceTestAccess::SetXactLoopCount(instance, 2);
    instance.Play();
    ExpectOutput(manualDevice->Render(64), 0, 2, true);
    instance.Stop(false); // ExitLoop: finish this pass and [144,192), no further repeats.
    ExpectOutput(manualDevice->Render(160), 64, 0, false);
    EXPECT_EQ(instance.getStateProperty(), SoundState::Stopped);
    instance.Play();
    ExpectOutput(manualDevice->Render(Total(2,true)+32), 0, 2, true);
    EXPECT_EQ(instance.getStateProperty(), SoundState::Stopped);
    instance.Play(); manualDevice->Render(64); instance.Stop(false); instance.Stop(true);
    ExpectSilence(manualDevice->Render(1024)); // pending suffix cannot undo immediate Stop
    instance.Play();
    ExpectOutput(manualDevice->Render(Total(2,true)+32), 0, 2, true);
}

TEST(XactFiniteVoice, MovesKeepTheAuthoredCount)
{
    EnsureMixer();
    Bytes pcm; for (int i=0; i<192; ++i) U16(pcm, (i/48+1)*4096);
    SoundEffect effect(pcm, 44100, AudioChannels::Mono);
    auto first = effect.CreateInstance();
    SoundEffectInstanceTestAccess::SetXactLoopCount(first, 2);
    auto second = std::move(first);
    auto third = effect.CreateInstance();
    third = std::move(second);
    third.Play();
    ExpectOutput(manualDevice->Render(Total(2,false)+32), 0, 2, false);
    EXPECT_EQ(third.getStateProperty(), SoundState::Stopped);
}

TEST(XactFiniteAdapter, SuffixDoesNotPublishAPrematureStoppedCallback)
{
    EnsureMixer();
    Bytes pcm; for (int i=0; i<192; ++i) U16(pcm, (i/48+1)*4096);
    auto audio = LoadMixerRawAudio(std::as_bytes(std::span(pcm)),
                                   {44100,1,MixerSampleFormat::Signed16});
    ASSERT_TRUE(audio);
    struct TrackOwner { MixerTrack* value = CreateMixerTrack();
                        ~TrackOwner() { SetMixerTrackStoppedCallback(value, nullptr, nullptr);
                                        DestroyMixerTrack(value); } } track;
    ASSERT_NE(track.value, nullptr);
    ASSERT_TRUE(SetMixerTrackAudio(track.value, audio.get()));
    int stopped = 0;
    const auto callback = +[](void* data, MixerTrack*) { ++*static_cast<int*>(data); };
    SetMixerTrackStoppedCallback(track.value, callback, &stopped);
    MixerPlayOptions options;
    options.loopCount = 2; options.playTailAfterLoops = true;
    options.hasLoopStartFrame = true; options.loopStartFrame = 48;
    options.hasMaxFrame = true; options.maxFrame = 144;
    ASSERT_TRUE(PlayMixerTrack(track.value, options));
    // Replacing/removing user callbacks must retain the internal suffix callback.
    SetMixerTrackStoppedCallback(track.value, nullptr, nullptr);
    // Cross the internal region end with no user callback installed: the
    // adapter's suffix callback must remain active independently of that tuple.
    ExpectOutput(manualDevice->Render(352), 0, 2, true);
    EXPECT_EQ(stopped, 0);
    EXPECT_TRUE(IsMixerTrackPlaying(track.value));
    SetMixerTrackStoppedCallback(track.value, callback, &stopped);
    ExpectOutput(manualDevice->Render(64), 352, 2, true);
    EXPECT_EQ(stopped, 1); // only final EOF, never the internal region-end transition
    EXPECT_FALSE(IsMixerTrackPlaying(track.value));
    ExpectSilence(manualDevice->Render(32)); EXPECT_EQ(stopped, 1);
}

TEST(XactFiniteAdapter, ExplicitMaxWithoutTailKeepsItsExistingTruncationContract)
{
    EnsureMixer();
    Bytes pcm; for (int i=0; i<192; ++i) U16(pcm, (i/48+1)*4096);
    auto audio = LoadMixerRawAudio(std::as_bytes(std::span(pcm)),
                                   {44100,1,MixerSampleFormat::Signed16});
    ASSERT_TRUE(audio);
    struct TrackOwner { MixerTrack* value = CreateMixerTrack();
                        ~TrackOwner() { DestroyMixerTrack(value); } } track;
    ASSERT_NE(track.value, nullptr);
    ASSERT_TRUE(SetMixerTrackAudio(track.value, audio.get()));
    MixerPlayOptions options;
    options.loopCount = 2; // playTailAfterLoops deliberately remains false
    options.hasLoopStartFrame = true; options.loopStartFrame = 48;
    options.hasMaxFrame = true; options.maxFrame = 144;
    ASSERT_TRUE(PlayMixerTrack(track.value, options));
    const auto output = manualDevice->Render(384);
    for (int frame=0; frame<384; ++frame)
    {
        // Explicit max-frame facade contract, also pinned by
        // CnaMixer.ALoopRegionPlaysTheIntroOnceThenRepeatsOnlyTheRegion:
        // 48 + 3*96 = 336 nonzero frames, then silence; no suffix requested.
        const float expected = frame < 336 ? Expected(frame,2,true) : 0.0f;
        ASSERT_NEAR(output[frame*2], expected, 1e-6f) << frame;
        ASSERT_NEAR(output[frame*2+1], expected, 1e-6f) << frame;
    }
    EXPECT_FALSE(IsMixerTrackPlaying(track.value));
}

// Calls arrive on the test/control thread; the manual device is synchronous.
// Recording never substitutes for forwarding to either real mixer adapter.
struct VoiceReceipt { float gain = -99.0f, ratio = -99.0f; };
std::map<void*, VoiceReceipt> voiceReceipts;

#include "XactSchedulerCases.hpp"
#include "XactValueEventCases.hpp"
#include "XactAttackOriginCases.hpp"
#include "XactPositiveGainCases.hpp"
#include "XactCategoryGainCases.hpp"
#include "XactInstanceLimitCases.hpp"
#include "XactInstanceCountCases.hpp"
#include "XactLoopVariationCases.hpp"
#include "XactPitchRangeCases.hpp"
#include "XactPitchOrderCases.hpp"
#include "XactEffectPitchWrapCases.hpp"
#include "XactVolumeLogCases.hpp"
#include "XactFiniteEventBudgetCases.hpp"
#include "XactReverbCases.hpp"
#include "XactReverbBusCases.hpp"

} // namespace

// Only the selected device factory is wrapped. Decoder, Cue, voice options,
// MixerEngine and native/CNA mixing remain production code in both configurations.
extern "C" std::unique_ptr<AP::IAudioDevice>
__wrap__ZN3CNA5Audio8Platform25CreateSelectedAudioDeviceEv()
{
    return std::make_unique<ManualDevice>();
}
extern "C" void __real__ZN3CNA8Internal5Audio17SetMixerTrackGainEPNS1_10MixerTrackEf(
    CNA::Internal::Audio::MixerTrack*, float) noexcept;
extern "C" void __wrap__ZN3CNA8Internal5Audio17SetMixerTrackGainEPNS1_10MixerTrackEf(
    CNA::Internal::Audio::MixerTrack* track, float gain) noexcept
{
    voiceReceipts[track].gain = gain;
    __real__ZN3CNA8Internal5Audio17SetMixerTrackGainEPNS1_10MixerTrackEf(track, gain);
}
extern "C" void __real__ZN3CNA8Internal5Audio27SetMixerTrackFrequencyRatioEPNS1_10MixerTrackEf(
    CNA::Internal::Audio::MixerTrack*, float) noexcept;
extern "C" void __wrap__ZN3CNA8Internal5Audio27SetMixerTrackFrequencyRatioEPNS1_10MixerTrackEf(
    CNA::Internal::Audio::MixerTrack* track, float ratio) noexcept
{
    voiceReceipts[track].ratio = ratio;
    __real__ZN3CNA8Internal5Audio27SetMixerTrackFrequencyRatioEPNS1_10MixerTrackEf(track, ratio);
}
#endif

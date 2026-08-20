// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include <cmath>

#include "OfflineAudioRenderer.hpp"

using namespace CNA::Test::Audio;

namespace
{
    // ~0.1% tolerance, matching plans/plan_audio.md's "Global numerical gates" initial calibration
    // tone frequency gate.
    constexpr double kFrequencyTolerance = 0.001;

    bool WithinRelativeTolerance(double measured, double expected, double tolerance)
    {
        if (expected == 0.0) return std::fabs(measured) < 1e-9;
        return std::fabs(measured - expected) / expected <= tolerance;
    }
}

// ---------------------------------------------------------------------------
// AUD-03-001/002/003/004/005/006: harness self-tests -- proving the harness itself measures
// what it claims to measure, before using it to test production behavior.
// ---------------------------------------------------------------------------

TEST(OfflineAudioRendererTest, SilenceRendersAllZeroSamplesWithZeroRms)
{
    auto pcm = GenerateSilenceS16(44100, 1, 0.1);
    auto result = RenderRawPcmOffline(pcm, SDL_AUDIO_S16LE, 1, 44100, 44100, 1, 4410);
    ASSERT_TRUE(result.ok);
    EXPECT_DOUBLE_EQ(MeasureRms(result.samples, 1), 0.0);
    EXPECT_DOUBLE_EQ(MeasurePeak(result.samples, 1), 0.0);
    EXPECT_FALSE(ContainsNaNOrInf(result.samples));
}

TEST(OfflineAudioRendererTest, SineWaveProducesExpectedFrameCountAndNoNaNs)
{
    // 1 second of 440 Hz mono at 44100 Hz -> exactly 44100 frames.
    auto pcm = GenerateSineWaveS16(440.0, 44100, 1, 1.0);
    auto result = RenderRawPcmOffline(pcm, SDL_AUDIO_S16LE, 1, 44100, 44100, 1, 44100);
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.samples.size(), 44100u);
    EXPECT_FALSE(ContainsNaNOrInf(result.samples));
    // 16-bit PCM round-trips through F32 with quantization noise only, not silence.
    EXPECT_GT(MeasureRms(result.samples, 1), 0.1);
}

// 440 Hz at neutral settings must remain approximately 440 Hz (plans/plan_audio.md's own required
// example: "A 440 Hz source played with neutral settings should remain approximately 440 Hz and
// retain the correct duration").
TEST(OfflineAudioRendererTest, Calibration440HzMonoAtNativeRateMeasuresCorrectFrequency)
{
    auto pcm = GenerateSineWaveS16(440.0, 44100, 1, 1.0);
    auto result = RenderRawPcmOffline(pcm, SDL_AUDIO_S16LE, 1, 44100, 44100, 1, 44100);
    ASSERT_TRUE(result.ok);

    double goertzel440 = GoertzelMagnitude(result.samples, 1, 0, 44100, 440.0);
    double goertzel880 = GoertzelMagnitude(result.samples, 1, 0, 44100, 880.0); // one octave up
    EXPECT_GT(goertzel440, goertzel880 * 10.0)
        << "440 Hz bin must dominate; a double-speed bug would make 880 Hz dominant instead";

    double estimatedHz = RefineFrequencyEstimateHz(result.samples, 1, 0, 44100, 440.0);
    EXPECT_TRUE(WithinRelativeTolerance(estimatedHz, 440.0, kFrequencyTolerance))
        << "measured " << estimatedHz << " Hz, expected ~440 Hz";

    // Zero-crossing is an independent, coarser sanity check (inherent +/-1 crossing
    // quantization over the window, ~0.11% at best) -- not the tight precision gate.
    double zeroCrossingHz = MeasureZeroCrossingFrequencyHz(result.samples, 1, 0, 44100);
    EXPECT_TRUE(WithinRelativeTolerance(zeroCrossingHz, 440.0, 0.01))
        << "measured " << zeroCrossingHz << " Hz, expected ~440 Hz";
}

TEST(OfflineAudioRendererTest, StereoChannelsAreIndependentlyMeasurable)
{
    // Left channel at 440 Hz, right channel at 880 Hz -- a channel-order/duplication bug would
    // make both channels agree instead of measuring their own distinct tone.
    auto leftPcm = GenerateSineWaveS16(440.0, 44100, 1, 0.5);
    auto rightPcm = GenerateSineWaveS16(880.0, 44100, 1, 0.5);
    std::vector<uint8_t> stereo(leftPcm.size() * 2);
    auto* dst = reinterpret_cast<int16_t*>(stereo.data());
    const auto* leftSamples = reinterpret_cast<const int16_t*>(leftPcm.data());
    const auto* rightSamples = reinterpret_cast<const int16_t*>(rightPcm.data());
    const std::size_t frames = leftPcm.size() / sizeof(int16_t);
    for (std::size_t i = 0; i < frames; ++i)
    {
        dst[i * 2 + 0] = leftSamples[i];
        dst[i * 2 + 1] = rightSamples[i];
    }

    auto result = RenderRawPcmOffline(stereo, SDL_AUDIO_S16LE, 2, 44100, 44100, 2, static_cast<int>(frames));
    ASSERT_TRUE(result.ok);

    double leftHz = RefineFrequencyEstimateHz(result.samples, 2, 0, 44100, 440.0);
    double rightHz = RefineFrequencyEstimateHz(result.samples, 2, 1, 44100, 880.0);
    EXPECT_TRUE(WithinRelativeTolerance(leftHz, 440.0, kFrequencyTolerance));
    EXPECT_TRUE(WithinRelativeTolerance(rightHz, 880.0, kFrequencyTolerance));
}

// ---------------------------------------------------------------------------
// AUD-05-017..030: golden sample-rate matrix. Each source rate must round-trip through
// SDL3_mixer's real resampler to the render rate with the correct frequency and frame count --
// directly exercising the exact "wrong sample rate declared" failure signatures the deep audit's
// table lists (e.g. 22050 Hz data consumed as 44100 Hz would double the measured frequency).
// ---------------------------------------------------------------------------

class GoldenSampleRateTest : public ::testing::TestWithParam<std::tuple<int, int>>
{
};

TEST_P(GoldenSampleRateTest, MonoSourceAtNativeRenderRateMeasuresCorrectFrequencyAndDuration)
{
    auto [sourceRate, channels] = GetParam();
    const double toneHz = 220.0; // well below Nyquist even at the lowest tested rate (11025 Hz)
    const double durationSeconds = 0.2;

    auto pcm = GenerateSineWaveS16(toneHz, sourceRate, channels, durationSeconds);
    const int expectedFrames = static_cast<int>(durationSeconds * sourceRate);

    // Render at the SAME rate as the source (native, no resampling) first, to establish the
    // frequency/duration baseline before AUD-04's cross-rate tests below.
    auto result = RenderRawPcmOffline(pcm, SDL_AUDIO_S16LE, channels, sourceRate,
                                       sourceRate, channels, expectedFrames);
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(static_cast<int>(result.samples.size()), expectedFrames * channels);

    double measuredHz = RefineFrequencyEstimateHz(result.samples, channels, 0, sourceRate, toneHz);
    EXPECT_TRUE(WithinRelativeTolerance(measuredHz, toneHz, kFrequencyTolerance))
        << "sourceRate=" << sourceRate << " channels=" << channels
        << ": measured " << measuredHz << " Hz, expected ~" << toneHz << " Hz";
}

INSTANTIATE_TEST_SUITE_P(
    AUD05GoldenMatrix,
    GoldenSampleRateTest,
    ::testing::Values(
        std::make_tuple(8000, 1), std::make_tuple(8000, 2),
        std::make_tuple(11025, 1), std::make_tuple(11025, 2),
        std::make_tuple(22050, 1), std::make_tuple(22050, 2),
        std::make_tuple(32000, 1), std::make_tuple(32000, 2),
        std::make_tuple(44100, 1), std::make_tuple(44100, 2),
        std::make_tuple(48000, 1), std::make_tuple(48000, 2),
        std::make_tuple(96000, 1), std::make_tuple(96000, 2)));

// ---------------------------------------------------------------------------
// AUD-04-002/003: the single most direct test of the user's reported "high-pitched/sped-up"
// regression class. CNA's AudioMixer.cpp hard-codes a 44100 Hz S16 stereo mixer (A-06) -- this
// proves whether playing correctly-declared 22050/48000 Hz source content through that
// hard-coded-44100 Hz pipeline changes pitch/duration by itself (a genuine backend resampling
// defect) or preserves it exactly (meaning any reported bug must come from a WRONG declared
// sample rate somewhere upstream of the mixer, not the mixer's resampler itself).
// ---------------------------------------------------------------------------

TEST(OfflineAudioRendererTest, Source22050HzThroughRenderMixer44100HzPreservesFrequency)
{
    const double toneHz = 220.0;
    const double durationSeconds = 0.2;
    auto pcm = GenerateSineWaveS16(toneHz, 22050, 1, durationSeconds);

    // Render at 44100 Hz (CNA's hard-coded AudioMixer.cpp spec) from a correctly-declared
    // 22050 Hz source -- SDL3_mixer must resample, not misinterpret the byte rate.
    auto result = RenderRawPcmOffline(pcm, SDL_AUDIO_S16LE, 1, 22050,
                                       44100, 1, static_cast<int>(durationSeconds * 44100));
    ASSERT_TRUE(result.ok);

    double measuredHz = RefineFrequencyEstimateHz(result.samples, 1, 0, 44100, toneHz);
    EXPECT_TRUE(WithinRelativeTolerance(measuredHz, toneHz, kFrequencyTolerance))
        << "measured " << measuredHz << " Hz -- if this were ~440 Hz (2x) instead, that would "
           "reproduce the audit's 'source rate misdeclared/mishandled' signature exactly";

    // Duration must also be preserved (0.2s of real audio), not compressed to 0.1s.
    double durationMeasured = static_cast<double>(result.samples.size()) / 44100.0;
    EXPECT_TRUE(WithinRelativeTolerance(durationMeasured, durationSeconds, 0.05));
}

TEST(OfflineAudioRendererTest, Source48000HzThroughRenderMixer44100HzPreservesFrequency)
{
    const double toneHz = 220.0;
    const double durationSeconds = 0.2;
    auto pcm = GenerateSineWaveS16(toneHz, 48000, 1, durationSeconds);

    auto result = RenderRawPcmOffline(pcm, SDL_AUDIO_S16LE, 1, 48000,
                                       44100, 1, static_cast<int>(durationSeconds * 44100));
    ASSERT_TRUE(result.ok);

    double measuredHz = RefineFrequencyEstimateHz(result.samples, 1, 0, 44100, toneHz);
    EXPECT_TRUE(WithinRelativeTolerance(measuredHz, toneHz, kFrequencyTolerance))
        << "measured " << measuredHz << " Hz";
}

TEST(OfflineAudioRendererTest, Source44100HzThroughRenderMixer48000HzPreservesFrequency)
{
    // The reverse direction: a 48 kHz device/mixer playing correctly-declared 44100 Hz content.
    const double toneHz = 220.0;
    const double durationSeconds = 0.2;
    auto pcm = GenerateSineWaveS16(toneHz, 44100, 1, durationSeconds);

    auto result = RenderRawPcmOffline(pcm, SDL_AUDIO_S16LE, 1, 44100,
                                       48000, 1, static_cast<int>(durationSeconds * 48000));
    ASSERT_TRUE(result.ok);

    double measuredHz = RefineFrequencyEstimateHz(result.samples, 1, 0, 48000, toneHz);
    EXPECT_TRUE(WithinRelativeTolerance(measuredHz, toneHz, kFrequencyTolerance))
        << "measured " << measuredHz << " Hz";
}

// Reproduces the EXACT failure signature from the audit's own table: "22,050 Hz data incorrectly
// declared as 44,100 Hz produces exactly double speed and one octave higher pitch." This is not
// a mixer defect -- it is what happens when the WRONG sourceSampleRate is passed to the very same
// correctly-behaving pipeline proven above. Included as a negative-control/documentation test so
// the distinction between "mixer bug" and "wrong declared metadata" is captured in an executable,
// not just prose.
TEST(OfflineAudioRendererTest, MisdeclaredSourceRateReproducesExactlyDoubleFrequencySignature)
{
    const double toneHz = 220.0;
    const double durationSeconds = 0.2;
    auto pcm = GenerateSineWaveS16(toneHz, 22050, 1, durationSeconds);

    // Deliberately declare the 22050 Hz-authored buffer AS 44100 Hz to the source spec -- this is
    // the exact "wrong metadata" bug class, not a mixer/resampler defect.
    auto result = RenderRawPcmOffline(pcm, SDL_AUDIO_S16LE, 1, /*sourceSampleRate=*/44100,
                                       44100, 1, static_cast<int>(pcm.size() / sizeof(int16_t)));
    ASSERT_TRUE(result.ok);

    double measuredHz = RefineFrequencyEstimateHz(result.samples, 1, 0, 44100, toneHz * 2.0);
    EXPECT_TRUE(WithinRelativeTolerance(measuredHz, toneHz * 2.0, kFrequencyTolerance))
        << "measured " << measuredHz << " Hz -- confirms the audit's exact-2x signature comes "
           "from a misdeclared sample rate, not from SDL3_mixer's resampler";
}

// ---------------------------------------------------------------------------
// AUD-08-023..031/AUD-10-005: pitch-ratio-to-frequency golden matrix at the SDL3_mixer track
// level (MIX_SetTrackFrequencyRatio) -- the shared mechanism every higher-level pitch contributor
// (SoundEffectInstance.Pitch, XACT cents, RPC, Doppler) ultimately composes into a single ratio
// and applies. Proves the ratio genuinely produces the expected frequency multiplication, not
// just that CNA's own 2^Pitch math is arithmetically correct in isolation.
// ---------------------------------------------------------------------------

struct PitchCase { float pitch; double expectedRatio; };

class GoldenPitchRatioTest : public ::testing::TestWithParam<PitchCase>
{
};

TEST_P(GoldenPitchRatioTest, FrequencyRatioMatchesExpectedXnaPitchMapping)
{
    const PitchCase& tc = GetParam();
    const double toneHz = 440.0;
    const double durationSeconds = 0.2;
    auto pcm = GenerateSineWaveS16(toneHz, 44100, 1, durationSeconds);

    // XNA's own documented mapping is ratio = 2^pitch; apply that here directly (matching
    // SoundEffectInstance::INTERNAL_calculatePitchRatio's formula) as the value fed to
    // MIX_SetTrackFrequencyRatio, then measure whether the actually-rendered frequency matches.
    float ratio = std::pow(2.0f, tc.pitch);
    auto result = RenderRawPcmOffline(pcm, SDL_AUDIO_S16LE, 1, 44100, 44100, 1,
                                       static_cast<int>(durationSeconds * 44100 * 2), ratio);
    ASSERT_TRUE(result.ok);

    // At ratio > 1 the source exhausts before the requested render length, and MIX_Generate
    // appends silence for the remainder -- trim to the real (non-silence) portion via
    // realBytesRendered first, or the trailing silence would dilute the frequency estimate
    // (energy concentrated in the real portion, diluted if analyzed against the padded window).
    std::vector<float> realSamples(
        result.samples.begin(),
        result.samples.begin() + static_cast<std::ptrdiff_t>(result.realBytesRendered / sizeof(float)));

    double expectedHz = toneHz * tc.expectedRatio;
    double measuredHz = RefineFrequencyEstimateHz(realSamples, 1, 0, 44100, expectedHz);
    EXPECT_TRUE(WithinRelativeTolerance(measuredHz, expectedHz, kFrequencyTolerance))
        << "pitch=" << tc.pitch << " ratio=" << ratio << ": measured " << measuredHz
        << " Hz, expected ~" << expectedHz << " Hz";
}

INSTANTIATE_TEST_SUITE_P(
    AUD08GoldenPitchMatrix,
    GoldenPitchRatioTest,
    ::testing::Values(
        PitchCase{-1.0f, 0.5},
        PitchCase{-0.75f, 0.5946036},
        PitchCase{-0.5f, 0.7071068},
        PitchCase{-0.25f, 0.8408964},
        PitchCase{0.0f, 1.0},
        PitchCase{0.25f, 1.1892071},
        PitchCase{0.5f, 1.4142136},
        PitchCase{0.75f, 1.6817928},
        PitchCase{1.0f, 2.0}));

// ---------------------------------------------------------------------------
// AUD-04-014/016: master volume (SoundEffect.MasterVolume, applied via MIX_SetMixerGain) and
// per-instance volume (SoundEffectInstance.Volume, applied via MIX_SetTrackGain) are two
// independent multiplicative stages -- confirmed by reading real SDL3_mixer source
// (MIX_SetTrackGain -> SDL_SetAudioStreamGain on track->output_stream, applied when the mixer's
// group-mixing loop pulls from that stream; MIX_SetMixerGain -> mixer->gain, applied separately
// by MixFloat32Audio when accumulating that already-track-gained sample into the group mix
// buffer). These tests measure the real, decoded RMS amplitude to prove the composition is
// exactly `trackGain * mixerGain`, not double-applied and not omitted.
// ---------------------------------------------------------------------------

TEST(OfflineAudioRendererTest, TrackGainAloneScalesRmsLinearly)
{
    auto pcm = GenerateSineWaveS16(440.0, 44100, 1, 0.2);
    auto baseline = RenderRawPcmOffline(pcm, SDL_AUDIO_S16LE, 1, 44100, 44100, 1, 8820,
                                         1.0f, false, 1.0f, 1.0f);
    auto halved = RenderRawPcmOffline(pcm, SDL_AUDIO_S16LE, 1, 44100, 44100, 1, 8820,
                                       1.0f, false, 0.5f, 1.0f);
    ASSERT_TRUE(baseline.ok);
    ASSERT_TRUE(halved.ok);

    const double baselineRms = MeasureRms(baseline.samples, 1);
    const double halvedRms = MeasureRms(halved.samples, 1);
    ASSERT_GT(baselineRms, 0.0);
    EXPECT_NEAR(halvedRms / baselineRms, 0.5, 0.01);
}

TEST(OfflineAudioRendererTest, MixerGainAloneScalesRmsLinearly)
{
    auto pcm = GenerateSineWaveS16(440.0, 44100, 1, 0.2);
    auto baseline = RenderRawPcmOffline(pcm, SDL_AUDIO_S16LE, 1, 44100, 44100, 1, 8820,
                                         1.0f, false, 1.0f, 1.0f);
    auto halved = RenderRawPcmOffline(pcm, SDL_AUDIO_S16LE, 1, 44100, 44100, 1, 8820,
                                       1.0f, false, 1.0f, 0.5f);
    ASSERT_TRUE(baseline.ok);
    ASSERT_TRUE(halved.ok);

    const double baselineRms = MeasureRms(baseline.samples, 1);
    const double halvedRms = MeasureRms(halved.samples, 1);
    ASSERT_GT(baselineRms, 0.0);
    EXPECT_NEAR(halvedRms / baselineRms, 0.5, 0.01);
}

// The core AUD-04-014 claim: both gains active together compose multiplicatively (0.5 * 0.5 =
// 0.25), not additively, not double-applied (which would read as 0.25*0.25=0.0625), and not
// omitting one factor (which would read as 0.5).
TEST(OfflineAudioRendererTest, TrackAndMixerGainComposeMultiplicativelyNotDoubleAppliedOrOmitted)
{
    auto pcm = GenerateSineWaveS16(440.0, 44100, 1, 0.2);
    auto baseline = RenderRawPcmOffline(pcm, SDL_AUDIO_S16LE, 1, 44100, 44100, 1, 8820,
                                         1.0f, false, 1.0f, 1.0f);
    auto both = RenderRawPcmOffline(pcm, SDL_AUDIO_S16LE, 1, 44100, 44100, 1, 8820,
                                     1.0f, false, 0.5f, 0.5f);
    ASSERT_TRUE(baseline.ok);
    ASSERT_TRUE(both.ok);

    const double baselineRms = MeasureRms(baseline.samples, 1);
    const double bothRms = MeasureRms(both.samples, 1);
    ASSERT_GT(baselineRms, 0.0);
    EXPECT_NEAR(bothRms / baselineRms, 0.25, 0.01);
}

// AUD-04-016: extreme aggregate gain (both stages maxed high) must remain finite -- SDL3_mixer's
// own float32 mixing pipeline has no hard clamp, but must never produce NaN/Inf regardless of how
// large the composed gain is.
TEST(OfflineAudioRendererTest, ExtremeAggregateGainRemainsFiniteNoNaNOrInf)
{
    auto pcm = GenerateSineWaveS16(440.0, 44100, 1, 0.2);
    auto result = RenderRawPcmOffline(pcm, SDL_AUDIO_S16LE, 1, 44100, 44100, 1, 8820,
                                       1.0f, false, 1000.0f, 1000.0f);
    ASSERT_TRUE(result.ok);
    EXPECT_FALSE(ContainsNaNOrInf(result.samples));
    EXPECT_GT(MeasurePeak(result.samples, 1), 0.0);
}

// AUD-04-016: zero aggregate gain (either stage zeroed) must render clean silence, not NaN
// (a 0/0 or similar degenerate case some naive gain-ramp implementations mishandle).
TEST(OfflineAudioRendererTest, ZeroTrackGainRendersSilenceNotNaN)
{
    auto pcm = GenerateSineWaveS16(440.0, 44100, 1, 0.2);
    auto result = RenderRawPcmOffline(pcm, SDL_AUDIO_S16LE, 1, 44100, 44100, 1, 8820,
                                       1.0f, false, 0.0f, 1.0f);
    ASSERT_TRUE(result.ok);
    EXPECT_FALSE(ContainsNaNOrInf(result.samples));
    EXPECT_DOUBLE_EQ(MeasureRms(result.samples, 1), 0.0);
}

// ---------------------------------------------------------------------------
// AUD-04-015: mixer/track gain changes must affect an ALREADY-PLAYING ("active") voice live, not
// only voices created after the change -- MIX_Generate() can be called repeatedly to pull
// successive chunks from the same still-playing track, so a gain change between two calls (with
// no new track created) directly tests the "active voice" half of the acceptance criterion.
// (AUD-04-014's tests above, each creating a brand-new track already reflecting the gain passed
// at creation time, already cover the "future voices" half -- there is no code path where a
// freshly created track could see a stale gain, since MIX_SetTrackGain/MIX_SetMixerGain are
// called before MIX_PlayTrack in every one of those tests.)
// ---------------------------------------------------------------------------

TEST(OfflineAudioRendererTest, MixerGainChangeMidPlaybackAffectsActiveVoiceOnNextChunk)
{
    // 1s of source so a two-chunk, 0.1s-each render can't exhaust it.
    auto pcm = GenerateSineWaveS16(440.0, 44100, 1, 1.0);

    ASSERT_TRUE(MIX_Init());
    SDL_AudioSpec renderSpec{};
    renderSpec.format = SDL_AUDIO_F32;
    renderSpec.channels = 1;
    renderSpec.freq = 44100;
    MIX_Mixer* mixer = MIX_CreateMixer(&renderSpec);
    ASSERT_NE(mixer, nullptr);

    SDL_AudioSpec sourceSpec{};
    sourceSpec.format = SDL_AUDIO_S16LE;
    sourceSpec.channels = 1;
    sourceSpec.freq = 44100;
    SDL_IOStream* io = SDL_IOFromConstMem(pcm.data(), pcm.size());
    ASSERT_NE(io, nullptr);
    MIX_Audio* audio = MIX_LoadRawAudio_IO(mixer, io, &sourceSpec, true);
    ASSERT_NE(audio, nullptr);

    MIX_Track* track = MIX_CreateTrack(mixer);
    ASSERT_NE(track, nullptr);
    ASSERT_TRUE(MIX_SetTrackAudio(track, audio));
    ASSERT_TRUE(MIX_SetTrackGain(track, 1.0f));
    ASSERT_TRUE(MIX_PlayTrack(track, 0));

    const int chunkFrames = 4410; // 0.1s
    const std::size_t chunkBytes = static_cast<std::size_t>(chunkFrames) * sizeof(float);

    std::vector<uint8_t> raw1(chunkBytes);
    ASSERT_GE(MIX_Generate(mixer, raw1.data(), static_cast<int>(chunkBytes)), 0);

    // Change gain on the SAME still-playing track -- no new track, no Stop/Play cycle.
    ASSERT_TRUE(MIX_SetTrackGain(track, 0.25f));

    std::vector<uint8_t> raw2(chunkBytes);
    ASSERT_GE(MIX_Generate(mixer, raw2.data(), static_cast<int>(chunkBytes)), 0);

    MIX_DestroyTrack(track);
    MIX_DestroyAudio(audio);
    MIX_DestroyMixer(mixer);
    MIX_Quit();

    std::vector<float> samples1(chunkFrames), samples2(chunkFrames);
    std::memcpy(samples1.data(), raw1.data(), chunkBytes);
    std::memcpy(samples2.data(), raw2.data(), chunkBytes);

    const double rms1 = MeasureRms(samples1, 1);
    const double rms2 = MeasureRms(samples2, 1);
    ASSERT_GT(rms1, 0.0);
    EXPECT_NEAR(rms2 / rms1, 0.25, 0.02)
        << "gain change mid-playback did not take effect on the already-active voice's next chunk";
}

// ---------------------------------------------------------------------------
// AUD-05-004: matches FNA's own SoundEffect internal ctor (SoundEffect.cs: `this.loopStart =
// (uint) loopStart;`), which does zero C#-level validation of loopStart/loopLength against the
// real decoded frame count (P9-VALIDATION-002, SoundEffect.cpp) -- relying entirely on the native
// backend to behave safely for an out-of-range loop region. This test empirically confirms
// SDL3_mixer (CNA's chosen backend) does exactly that: MIX_PlayTrack's own loop_start clamp
// (>= 0 only, no upper-bound check at play() time -- read directly from
// third_party/SDL_mixer/src/SDL_mixer.c) and the mixing loop's natural EOF/seek-failure handling
// (a loop-start beyond the real audio's length hits the decoder's own seek() call, which either
// clamps or fails cleanly, never a crash or garbage read) together mean a loop region far beyond
// the real decoded length degrades gracefully -- never reaches the backend "unchecked" in the
// sense of causing memory-unsafe behavior, even though CNA itself performs no explicit bounds
// check (matching FNA's own documented lack of one).
// ---------------------------------------------------------------------------
TEST(OfflineAudioRendererTest, LoopRegionFarBeyondDecodedLengthDegradesGracefullyNoCrashNoNaN)
{
    // Only 0.05s (2205 frames) of real audio -- loopStart/maxFrame below are set to values far
    // beyond that, deliberately nonsensical relative to the real content.
    auto pcm = GenerateSineWaveS16(440.0, 44100, 1, 0.05);

    ASSERT_TRUE(MIX_Init());
    SDL_AudioSpec renderSpec{};
    renderSpec.format = SDL_AUDIO_F32;
    renderSpec.channels = 1;
    renderSpec.freq = 44100;
    MIX_Mixer* mixer = MIX_CreateMixer(&renderSpec);
    ASSERT_NE(mixer, nullptr);

    SDL_AudioSpec sourceSpec{};
    sourceSpec.format = SDL_AUDIO_S16LE;
    sourceSpec.channels = 1;
    sourceSpec.freq = 44100;
    SDL_IOStream* io = SDL_IOFromConstMem(pcm.data(), pcm.size());
    ASSERT_NE(io, nullptr);
    MIX_Audio* audio = MIX_LoadRawAudio_IO(mixer, io, &sourceSpec, true);
    ASSERT_NE(audio, nullptr);

    MIX_Track* track = MIX_CreateTrack(mixer);
    ASSERT_NE(track, nullptr);
    ASSERT_TRUE(MIX_SetTrackAudio(track, audio));

    SDL_PropertiesID props = SDL_CreateProperties();
    ASSERT_NE(props, 0u);
    SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, -1); // loop forever
    // Loop start ~10x past the real 2205-frame source, and a max-frame ~100x past it -- neither
    // corresponds to anything in the real decoded content.
    SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOP_START_FRAME_NUMBER, 22050);
    SDL_SetNumberProperty(props, MIX_PROP_PLAY_MAX_FRAME_NUMBER, 220500);
    ASSERT_TRUE(MIX_PlayTrack(track, props));
    SDL_DestroyProperties(props);

    // Render well past the real source's natural length (2205 frames) so this must have hit the
    // nonsensical loop points (or the decoder's own EOF handling) at least once.
    const int framesToRender = 8820; // 0.2s, 4x the real source length
    const std::size_t totalBytes = static_cast<std::size_t>(framesToRender) * sizeof(float);
    std::vector<uint8_t> raw(totalBytes);
    int generated = MIX_Generate(mixer, raw.data(), static_cast<int>(totalBytes));

    MIX_DestroyTrack(track);
    MIX_DestroyAudio(audio);
    MIX_DestroyMixer(mixer);
    MIX_Quit();

    ASSERT_GE(generated, 0) << "MIX_Generate must not fail even with a nonsensical loop region";
    std::vector<float> samples(framesToRender);
    std::memcpy(samples.data(), raw.data(), totalBytes);
    EXPECT_FALSE(ContainsNaNOrInf(samples));
}

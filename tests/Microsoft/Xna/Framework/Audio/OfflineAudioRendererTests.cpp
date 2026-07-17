// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include <cmath>

#include "OfflineAudioRenderer.hpp"

using namespace CNA::Test::Audio;

namespace
{
    // ~0.1% tolerance, matching plan_audio.md's "Global numerical gates" initial calibration
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

// 440 Hz at neutral settings must remain approximately 440 Hz (plan_audio.md's own required
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

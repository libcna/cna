// SPDX-License-Identifier: MS-PL

#include <cmath>
#include <numbers>

#include <gtest/gtest.h>

#include "CNA/Internal/Media/VisualizationCapture.hpp"
#include "CNA/Internal/Media/VisualizationFFT.hpp"

using CNA::Internal::Media::VisualizationCapture;
using CNA::Internal::Media::VisualizationFFT;

namespace
{
    // Fills `out` with a unit-amplitude sine at exactly `bin` cycles per window, so the resulting
    // tone lands precisely on FFT bin `bin` with no leakage between neighbours.
    void FillSineAtBin(std::array<float, VisualizationFFT::InputSize>& out, std::size_t bin)
    {
        for (std::size_t i = 0; i < out.size(); ++i)
        {
            const float phase = 2.0f * std::numbers::pi_v<float> *
                                static_cast<float>(bin) * static_cast<float>(i) /
                                static_cast<float>(out.size());
            out[i] = std::sin(phase);
        }
    }

    std::size_t IndexOfMax(const std::array<float, VisualizationFFT::BinCount>& bins)
    {
        std::size_t best = 0;
        for (std::size_t i = 1; i < bins.size(); ++i)
        {
            if (bins[i] > bins[best]) best = i;
        }
        return best;
    }
}

// plans/plan_media.md MEDIA-187: deterministic, device-free proof that the FFT is a real transform and
// not a placeholder -- a pure tone must peak in its own bin, DC must land in bin 0, and silence
// must produce nothing.

TEST(VisualizationFFTTest, PureTonePeaksInItsOwnBin)
{
    for (std::size_t bin : {8u, 32u, 100u})
    {
        std::array<float, VisualizationFFT::InputSize> input{};
        FillSineAtBin(input, bin);

        std::array<float, VisualizationFFT::BinCount> bins{};
        VisualizationFFT::ComputeMagnitudes(input, bins);

        EXPECT_EQ(IndexOfMax(bins), bin)
            << "a tone at bin " << bin << " peaked at bin " << IndexOfMax(bins) << " instead";
        EXPECT_GT(bins[bin], 0.1f) << "peak magnitude implausibly small for a full-scale sine";
    }
}

TEST(VisualizationFFTTest, ConstantInputPutsEnergyInTheDcBin)
{
    std::array<float, VisualizationFFT::InputSize> input{};
    input.fill(1.0f);

    std::array<float, VisualizationFFT::BinCount> bins{};
    VisualizationFFT::ComputeMagnitudes(input, bins);

    EXPECT_EQ(IndexOfMax(bins), 0u) << "constant (DC) input should peak in bin 0";
    EXPECT_GT(bins[0], 0.1f);
}

TEST(VisualizationFFTTest, SilenceProducesAllZeroMagnitudes)
{
    std::array<float, VisualizationFFT::InputSize> input{}; // all zeros
    std::array<float, VisualizationFFT::BinCount> bins{};
    bins.fill(123.0f); // poison, so a no-op implementation would be caught

    VisualizationFFT::ComputeMagnitudes(input, bins);

    for (std::size_t i = 0; i < bins.size(); ++i)
    {
        EXPECT_NEAR(bins[i], 0.0f, 1e-6f) << "bin " << i << " non-zero for silent input";
    }
}

// plans/plan_media.md MEDIA-186: the ring buffer is the audio-thread-facing half. These exercise its
// contract directly, without needing a real audio device.

TEST(VisualizationCaptureTest, ReadsBackTheMostRecentSamplesOldestFirst)
{
    VisualizationCapture capture;
    std::vector<float> pushed;
    for (int i = 0; i < 300; ++i) pushed.push_back(static_cast<float>(i));
    capture.Push(pushed.data(), static_cast<int>(pushed.size()), 1);

    std::array<float, 4> out{};
    capture.Read(out.data(), out.size());

    // Last four values pushed were 296..299, oldest first.
    EXPECT_FLOAT_EQ(out[0], 296.0f);
    EXPECT_FLOAT_EQ(out[1], 297.0f);
    EXPECT_FLOAT_EQ(out[2], 298.0f);
    EXPECT_FLOAT_EQ(out[3], 299.0f);
}

TEST(VisualizationCaptureTest, DownmixesInterleavedStereoToMonoAverage)
{
    VisualizationCapture capture;
    // Two stereo frames: (1.0, 3.0) -> 2.0 and (10.0, 20.0) -> 15.0
    const float stereo[] = {1.0f, 3.0f, 10.0f, 20.0f};
    capture.Push(stereo, 4, 2);

    std::array<float, 2> out{};
    capture.Read(out.data(), out.size());
    EXPECT_FLOAT_EQ(out[0], 2.0f);
    EXPECT_FLOAT_EQ(out[1], 15.0f);
}

TEST(VisualizationCaptureTest, ReportsNoDataBeforeAnythingIsPushedAndZeroFillsShortReads)
{
    VisualizationCapture capture;
    EXPECT_FALSE(capture.HasData());

    std::array<float, 8> out{};
    out.fill(7.0f);
    capture.Read(out.data(), out.size()); // nothing captured yet
    for (float v : out) EXPECT_FLOAT_EQ(v, 0.0f) << "short read must zero-fill, not leak garbage";
}

TEST(VisualizationCaptureTest, ResetClearsPreviouslyCapturedAudio)
{
    VisualizationCapture capture;
    const float samples[] = {1.0f, 2.0f, 3.0f, 4.0f};
    capture.Push(samples, 4, 1);
    ASSERT_TRUE(capture.HasData());

    capture.Reset();
    EXPECT_FALSE(capture.HasData()) << "Reset must drop audio from the previous session";
}

TEST(VisualizationCaptureTest, WrapsAroundWithoutLosingTheNewestSamples)
{
    VisualizationCapture capture;
    // Push well past Capacity so the ring wraps several times.
    std::vector<float> data;
    for (std::size_t i = 0; i < VisualizationCapture::Capacity * 3 + 17; ++i)
    {
        data.push_back(static_cast<float>(i));
    }
    capture.Push(data.data(), static_cast<int>(data.size()), 1);

    std::array<float, 3> out{};
    capture.Read(out.data(), out.size());
    const float last = static_cast<float>(data.size() - 1);
    EXPECT_FLOAT_EQ(out[2], last);
    EXPECT_FLOAT_EQ(out[1], last - 1.0f);
    EXPECT_FLOAT_EQ(out[0], last - 2.0f);
}

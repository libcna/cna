#include <gtest/gtest.h>

#include "CNA/Internal/Renderers/Glide/GlideDisplayModeSelection.hpp"

using CNA::Internal::Renderers::Glide::GlideResolutionCandidate;
using CNA::Internal::Renderers::Glide::KnownGlideDisplayMode;
using CNA::Internal::Renderers::Glide::kGlideRefresh60Hz;
using CNA::Internal::Renderers::Glide::SelectGlideDisplayMode;

namespace
{
    constexpr std::int32_t kResolution320x240 = 0x1;
    constexpr std::int32_t kResolution640x480 = 0x7;
    constexpr std::int32_t kResolution800x600 = 0x8;
    constexpr std::int32_t kRefresh75Hz = 0x3;
    constexpr std::int32_t kRefresh85Hz = 0x7;
} // namespace

TEST(GlideDisplayModeSelectionTest, KnownDisplayModeReturnsFixedDimensionsForHistoricalTokens)
{
    EXPECT_EQ(KnownGlideDisplayMode(kResolution640x480).width, 640);
    EXPECT_EQ(KnownGlideDisplayMode(kResolution640x480).height, 480);
    EXPECT_EQ(KnownGlideDisplayMode(0x999).width, 0);
}

TEST(GlideDisplayModeSelectionTest, PicksTheSmallestSufficientResolution)
{
    const std::vector<GlideResolutionCandidate> candidates{
        {kResolution320x240, kGlideRefresh60Hz},
        {kResolution640x480, kGlideRefresh60Hz},
        {kResolution800x600, kGlideRefresh60Hz}};

    const auto selected = SelectGlideDisplayMode(candidates, 640, 480);

    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected->mode.resolution, kResolution640x480);
    EXPECT_EQ(selected->mode.width, 640);
    EXPECT_EQ(selected->mode.height, 480);
    EXPECT_EQ(selected->refresh, kGlideRefresh60Hz);
}

TEST(GlideDisplayModeSelectionTest, UsesTheOnlyOfferedRefreshWhenNo60HzCandidateExists)
{
    // The chosen dimensions are only ever reported at 75 Hz in this runtime's candidate list.
    // Startup must open exactly that refresh, never a hardcoded 60 Hz that was never validated.
    const std::vector<GlideResolutionCandidate> candidates{
        {kResolution640x480, kRefresh75Hz}};

    const auto selected = SelectGlideDisplayMode(candidates, 640, 480);

    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected->mode.resolution, kResolution640x480);
    EXPECT_EQ(selected->refresh, kRefresh75Hz);
}

TEST(GlideDisplayModeSelectionTest, PrefersSixtyHertzAmongMultipleRefreshRatesAtTheSameDimensions)
{
    const std::vector<GlideResolutionCandidate> candidates{
        {kResolution640x480, kRefresh85Hz},
        {kResolution640x480, kRefresh75Hz},
        {kResolution640x480, kGlideRefresh60Hz}};

    const auto selected = SelectGlideDisplayMode(candidates, 640, 480);

    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected->refresh, kGlideRefresh60Hz);
}

TEST(GlideDisplayModeSelectionTest, IsDeterministicWithoutA60HzCandidateAtTiedDimensions)
{
    // Neither refresh rate is 60 Hz, so the tie-break falls through to "first encountered wins" --
    // exercised twice with the candidate order reversed to prove the result tracks input order
    // deterministically rather than depending on refresh-value magnitude.
    const std::vector<GlideResolutionCandidate> forward{
        {kResolution640x480, kRefresh75Hz},
        {kResolution640x480, kRefresh85Hz}};
    const std::vector<GlideResolutionCandidate> reversed{
        {kResolution640x480, kRefresh85Hz},
        {kResolution640x480, kRefresh75Hz}};

    EXPECT_EQ(SelectGlideDisplayMode(forward, 640, 480)->refresh, kRefresh75Hz);
    EXPECT_EQ(SelectGlideDisplayMode(reversed, 640, 480)->refresh, kRefresh85Hz);
}

TEST(GlideDisplayModeSelectionTest, NeverCombinesAResolutionFromOneCandidateWithAnotherCandidatesRefresh)
{
    // A deliberately awkward matrix: several resolutions, several refresh rates, no candidate
    // offering 60 Hz at all. The winning (resolution, refresh) pair must be exactly one of the
    // supplied candidates, never a resolution and refresh stitched together from two different ones.
    const std::vector<GlideResolutionCandidate> candidates{
        {kResolution320x240, kRefresh75Hz},
        {kResolution640x480, kRefresh85Hz},
        {kResolution640x480, kRefresh75Hz},
        {kResolution800x600, kRefresh75Hz}};

    const auto selected = SelectGlideDisplayMode(candidates, 640, 480);

    ASSERT_TRUE(selected.has_value());
    bool matchedARealCandidate = false;
    for (const GlideResolutionCandidate& candidate : candidates)
    {
        if (candidate.resolution == selected->mode.resolution && candidate.refresh == selected->refresh)
        {
            matchedARealCandidate = true;
        }
    }
    EXPECT_TRUE(matchedARealCandidate);
}

TEST(GlideDisplayModeSelectionTest, FailsWhenNoCandidateIsLargeEnough)
{
    const std::vector<GlideResolutionCandidate> candidates{
        {kResolution320x240, kGlideRefresh60Hz}};

    EXPECT_FALSE(SelectGlideDisplayMode(candidates, 640, 480).has_value());
}

TEST(GlideDisplayModeSelectionTest, FailsOnAnEmptyCandidateList)
{
    EXPECT_FALSE(SelectGlideDisplayMode({}, 640, 480).has_value());
}

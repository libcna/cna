#include <gtest/gtest.h>

#include "CNA/Internal/Backends/Glide/GlideTextureEviction.hpp"

using CNA::Internal::Backends::Glide::GlideResidentTextureView;
using CNA::Internal::Backends::Glide::SelectGlideEvictionVictim;

namespace
{
    // Distinct addresses to use as opaque texture identities; only pointer identity matters.
    int textureA = 0;
    int textureB = 0;
    int textureC = 0;
} // namespace

TEST(GlideTextureEvictionTest, PicksTheLeastRecentlyUsedResidentCandidate)
{
    const std::vector<GlideResidentTextureView> candidates{
        {&textureA, true, 5},
        {&textureB, true, 2},
        {&textureC, true, 8}};

    EXPECT_EQ(SelectGlideEvictionVictim(candidates, nullptr), &textureB);
}

TEST(GlideTextureEvictionTest, NeverSelectsTheRequesterEvenIfItIsOldest)
{
    const std::vector<GlideResidentTextureView> candidates{
        {&textureA, true, 1},
        {&textureB, true, 9}};

    EXPECT_EQ(SelectGlideEvictionVictim(candidates, &textureA), &textureB);
}

TEST(GlideTextureEvictionTest, SkipsNonResidentCandidates)
{
    const std::vector<GlideResidentTextureView> candidates{
        {&textureA, false, 0}, // already evicted, or mid-rebuild for itself -- nothing to free
        {&textureB, true, 3}};

    EXPECT_EQ(SelectGlideEvictionVictim(candidates, nullptr), &textureB);
}

TEST(GlideTextureEvictionTest, ReturnsNullWhenNoCandidateIsEligible)
{
    const std::vector<GlideResidentTextureView> onlyTheRequester{{&textureA, true, 1}};
    EXPECT_EQ(SelectGlideEvictionVictim(onlyTheRequester, &textureA), nullptr);

    const std::vector<GlideResidentTextureView> onlyNonResident{{&textureA, false, 1}};
    EXPECT_EQ(SelectGlideEvictionVictim(onlyNonResident, nullptr), nullptr);

    EXPECT_EQ(SelectGlideEvictionVictim({}, nullptr), nullptr);
}

TEST(GlideTextureEvictionTest, TiedCountersDeterministicallyKeepTheFirstCandidateEncountered)
{
    const std::vector<GlideResidentTextureView> forward{
        {&textureA, true, 4}, {&textureB, true, 4}};
    const std::vector<GlideResidentTextureView> reversed{
        {&textureB, true, 4}, {&textureA, true, 4}};

    EXPECT_EQ(SelectGlideEvictionVictim(forward, nullptr), &textureA);
    EXPECT_EQ(SelectGlideEvictionVictim(reversed, nullptr), &textureB);
}

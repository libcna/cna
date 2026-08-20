// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-8: the engine layer's revision number.
//
// Two things are worth pinning and neither is obvious. The header's macro and the library's
// function must agree in a tree that was built in one piece -- that is the whole point of having
// both, and a test is the only place the two are ever compared. And the number must not go
// backwards: it is written into logs and settings files, and a build that reports a lower revision
// than one that came before it makes every such record ambiguous.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/EngineLayerVersion.hpp"

namespace {

TEST(EngineLayerVersionTest, TheHeaderAndTheLibraryAgree)
{
    // The mismatch this pair exists to catch: the macro is what this translation unit was compiled
    // against, the function is what the linked library reports. In a tree built in one piece they
    // are the same number, and when they are not, something stale is in the link.
    EXPECT_EQ(CNA::Graphics::getEngineLayerVersion(), CNA_CNAEXT_ENGINE_VERSION);
}

TEST(EngineLayerVersionTest, TheCurrentRevisionIsTwoAndNeverGoesBackwards)
{
    // plans/plan_modern.md MOD-1904 moved this from 1 to 2, and the literal is deliberate: bumping the
    // macro without touching this line is not possible, so a bump is always a decision. What
    // changed between the two is in docs/cnaext-engine-changelog.md.
    EXPECT_EQ(CNA_CNAEXT_ENGINE_VERSION, 2);
    EXPECT_GE(CNA::Graphics::getEngineLayerVersion(), 1) << "the revision never goes below its "
                                                            "first published value";
}

TEST(EngineLayerVersionTest, TheStringFormIsStableAndCarriesTheNumber)
{
    // Spelled out rather than built from the macro: a test that re-derives the implementation
    // measures nothing. This line moves by hand on each bump, which is the intended cost.
    EXPECT_EQ(CNA::Graphics::getEngineLayerVersionString(), "CNA engine layer 2");
}

TEST(EngineLayerVersionTest, TheAnswerDoesNotChangeBetweenCalls)
{
    // A constant that is not constant would be worse than no constant: it is read once at start-up
    // and written into logs that are compared much later.
    EXPECT_EQ(CNA::Graphics::getEngineLayerVersion(), CNA::Graphics::getEngineLayerVersion());
    EXPECT_EQ(CNA::Graphics::getEngineLayerVersionString(),
              CNA::Graphics::getEngineLayerVersionString());
}

} // namespace

#endif // CNA_CNAEXT

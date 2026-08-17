// SPDX-License-Identifier: MS-PL
// Task 26: EffectTechnique and EffectPass unit tests.

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"

using namespace Microsoft::Xna::Framework::Graphics;

// --- EffectTechnique ---

TEST(EffectTechniqueTest, DefaultConstructorHasEmptyName)
{
    EffectTechnique t;
    EXPECT_EQ(t.getNameProperty(), "");
}

TEST(EffectTechniqueTest, NamedConstructorReturnsName)
{
    EffectTechnique t(nullptr, "Technique0");
    EXPECT_EQ(t.getNameProperty(), "Technique0");
}

TEST(EffectTechniqueTest, DefaultConstructorHasZeroPasses)
{
    EffectTechnique t;
    EXPECT_EQ(t.getPassesProperty().getCountProperty(), 0);
}

TEST(EffectTechniqueTest, NamedConstructorSeedsOneDefaultPass)
{
    // EffectTechnique(owner, name) pre-adds a default "P0" pass
    EffectTechnique t(nullptr, "T");
    EXPECT_EQ(t.getPassesProperty().getCountProperty(), 1);
    EXPECT_EQ(t.getPassesProperty()[0].getNameProperty(), "P0");
}

TEST(EffectTechniqueTest, AddPassIncreasesCount)
{
    EffectTechnique t(nullptr, "T");
    // starts with 1 default pass; adding one more → 2
    t.getPassesProperty().Add(EffectPass(nullptr, "Pass1"));
    EXPECT_EQ(t.getPassesProperty().getCountProperty(), 2);
}

TEST(EffectTechniqueTest, PassAccessibleByIndex)
{
    EffectTechnique t(nullptr, "T");
    // [0] = "P0" (seeded by constructor), [1] = "Extra"
    t.getPassesProperty().Add(EffectPass(nullptr, "Extra"));
    EXPECT_EQ(t.getPassesProperty()[0].getNameProperty(), "P0");
    EXPECT_EQ(t.getPassesProperty()[1].getNameProperty(), "Extra");
}

TEST(EffectTechniqueTest, PassAccessibleByName)
{
    EffectTechnique t(nullptr, "T");
    t.getPassesProperty().Add(EffectPass(nullptr, "Alpha"));
    const EffectPass* p = t.getPassesProperty()["Alpha"];
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->getNameProperty(), "Alpha");
}

TEST(EffectTechniqueTest, DefaultSeededPassAccessibleByName)
{
    EffectTechnique t(nullptr, "T");
    const EffectPass* p = t.getPassesProperty()["P0"];
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->getNameProperty(), "P0");
}

TEST(EffectTechniqueTest, PassLookupByUnknownNameReturnsNull)
{
    EffectTechnique t(nullptr, "T");
    const EffectPass* p = t.getPassesProperty()["NoSuch"];
    EXPECT_EQ(p, nullptr);
}

// --- EffectPass ---

TEST(EffectPassTest, NameReturnsConstructorName)
{
    EffectPass p(nullptr, "Pass0");
    EXPECT_EQ(p.getNameProperty(), "Pass0");
}

TEST(EffectPassTest, EmptyNameAllowed)
{
    EffectPass p(nullptr, "");
    EXPECT_EQ(p.getNameProperty(), "");
}

TEST(EffectPassTest, IndexDefaultsToZero)
{
    EffectPass p(nullptr, "Pass0");
    EXPECT_EQ(p.getIndexInternal(), 0u);
}

TEST(EffectPassTest, IndexReturnsConstructorIndex)
{
    EffectPass p(nullptr, "Pass2", 7u, 2u);
    EXPECT_EQ(p.getIndexInternal(), 2u);
}

TEST(EffectPassTest, IndexIsIndependentOfTechniqueIdentity)
{
    // The two trailing constructor arguments are separate facts: which technique owns the pass,
    // and where the pass sits inside it.
    EffectPass first(nullptr, "P", 100u, 0u);
    EffectPass second(nullptr, "P", 100u, 3u);
    EXPECT_EQ(first.getIndexInternal(), 0u);
    EXPECT_EQ(second.getIndexInternal(), 3u);
}


// SPDX-License-Identifier: MS-PL
// Task 185: EffectTechniqueCollection, EffectParameterCollection, and
// EffectPassCollection unit tests — indexing, name lookup, iteration,
// and GetParameterBySemantic.

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterClass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterType.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechniqueCollection.hpp"

using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    EffectParameter MakeParam(const std::string& name, const std::string& sem = "")
    {
        return EffectParameter(name, sem, 1, 1,
                               EffectParameterClass::Scalar,
                               EffectParameterType::Single);
    }
}

// ============================================================
// EffectTechniqueCollection
// ============================================================

TEST(EffectTechniqueCollectionTest, EmptyCollectionHasZeroCount)
{
    EffectTechniqueCollection col;
    EXPECT_EQ(col.getCountProperty(), 0);
}

TEST(EffectTechniqueCollectionTest, AddIncreasesCount)
{
    EffectTechniqueCollection col;
    col.Add(EffectTechnique(nullptr, "T0"));
    EXPECT_EQ(col.getCountProperty(), 1);
    col.Add(EffectTechnique(nullptr, "T1"));
    EXPECT_EQ(col.getCountProperty(), 2);
}

TEST(EffectTechniqueCollectionTest, IndexByIntReturnsTechnique)
{
    EffectTechniqueCollection col;
    col.Add(EffectTechnique(nullptr, "Alpha"));
    col.Add(EffectTechnique(nullptr, "Beta"));
    EXPECT_EQ(col[0].getNameProperty(), "Alpha");
    EXPECT_EQ(col[1].getNameProperty(), "Beta");
}

TEST(EffectTechniqueCollectionTest, IndexByNameReturnsCorrectTechnique)
{
    EffectTechniqueCollection col;
    col.Add(EffectTechnique(nullptr, "Alpha"));
    col.Add(EffectTechnique(nullptr, "Beta"));
    const EffectTechnique* t = col["Beta"];
    ASSERT_NE(t, nullptr);
    EXPECT_EQ(t->getNameProperty(), "Beta");
}

TEST(EffectTechniqueCollectionTest, IndexByNameReturnsNullForMissingName)
{
    EffectTechniqueCollection col;
    col.Add(EffectTechnique(nullptr, "Alpha"));
    EXPECT_EQ(col["NoSuch"], nullptr);
}

TEST(EffectTechniqueCollectionTest, ConstIndexByIntReturnsTechnique)
{
    EffectTechniqueCollection col;
    col.Add(EffectTechnique(nullptr, "T"));
    const EffectTechniqueCollection& cref = col;
    EXPECT_EQ(cref[0].getNameProperty(), "T");
}

TEST(EffectTechniqueCollectionTest, ConstIndexByNameReturnsTechnique)
{
    EffectTechniqueCollection col;
    col.Add(EffectTechnique(nullptr, "T"));
    const EffectTechniqueCollection& cref = col;
    const EffectTechnique* t = cref["T"];
    ASSERT_NE(t, nullptr);
    EXPECT_EQ(t->getNameProperty(), "T");
}

TEST(EffectTechniqueCollectionTest, IterationVisitsAllTechniques)
{
    EffectTechniqueCollection col;
    col.Add(EffectTechnique(nullptr, "X"));
    col.Add(EffectTechnique(nullptr, "Y"));
    col.Add(EffectTechnique(nullptr, "Z"));
    int count = 0;
    for (const auto& t : col) { (void)t; ++count; }
    EXPECT_EQ(count, 3);
}

// ============================================================
// EffectParameterCollection
// ============================================================

TEST(EffectParameterCollectionTest, EmptyCollectionHasZeroCount)
{
    EffectParameterCollection col;
    EXPECT_EQ(col.getCountProperty(), 0);
}

TEST(EffectParameterCollectionTest, AddIncreasesCount)
{
    EffectParameterCollection col;
    col.Add(MakeParam("P0"));
    EXPECT_EQ(col.getCountProperty(), 1);
    col.Add(MakeParam("P1"));
    EXPECT_EQ(col.getCountProperty(), 2);
}

TEST(EffectParameterCollectionTest, IndexByIntReturnsParameter)
{
    EffectParameterCollection col;
    col.Add(MakeParam("First"));
    col.Add(MakeParam("Second"));
    EXPECT_EQ(col[0].getNameProperty(), "First");
    EXPECT_EQ(col[1].getNameProperty(), "Second");
}

TEST(EffectParameterCollectionTest, IndexByNameReturnsCorrectParameter)
{
    EffectParameterCollection col;
    col.Add(MakeParam("A"));
    col.Add(MakeParam("B"));
    EffectParameter* p = col["B"];
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->getNameProperty(), "B");
}

TEST(EffectParameterCollectionTest, IndexByNameReturnsNullForMissingName)
{
    EffectParameterCollection col;
    col.Add(MakeParam("A"));
    EXPECT_EQ(col["Missing"], nullptr);
}

TEST(EffectParameterCollectionTest, ConstIndexByIntReturnsParameter)
{
    EffectParameterCollection col;
    col.Add(MakeParam("P"));
    const EffectParameterCollection& cref = col;
    EXPECT_EQ(cref[0].getNameProperty(), "P");
}

TEST(EffectParameterCollectionTest, ConstIndexByNameReturnsParameter)
{
    EffectParameterCollection col;
    col.Add(MakeParam("P"));
    const EffectParameterCollection& cref = col;
    const EffectParameter* p = cref["P"];
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->getNameProperty(), "P");
}

TEST(EffectParameterCollectionTest, GetParameterBySemanticFound)
{
    EffectParameterCollection col;
    col.Add(MakeParam("World", "WORLD"));
    col.Add(MakeParam("View",  "VIEW"));
    EffectParameter* p = col.GetParameterBySemantic("VIEW");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->getNameProperty(), "View");
}

TEST(EffectParameterCollectionTest, GetParameterBySemanticNotFound)
{
    EffectParameterCollection col;
    col.Add(MakeParam("World", "WORLD"));
    EXPECT_EQ(col.GetParameterBySemantic("PROJECTION"), nullptr);
}

TEST(EffectParameterCollectionTest, ConstGetParameterBySemanticFound)
{
    EffectParameterCollection col;
    col.Add(MakeParam("Proj", "PROJECTION"));
    const EffectParameterCollection& cref = col;
    const EffectParameter* p = cref.GetParameterBySemantic("PROJECTION");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->getNameProperty(), "Proj");
}

TEST(EffectParameterCollectionTest, IterationVisitsAllParameters)
{
    EffectParameterCollection col;
    col.Add(MakeParam("A"));
    col.Add(MakeParam("B"));
    col.Add(MakeParam("C"));
    int count = 0;
    for (const auto& p : col) { (void)p; ++count; }
    EXPECT_EQ(count, 3);
}

// Task 358: FNA's EffectParameterCollection is a thin wrapper over a plain List<EffectParameter>
// populated once at construction; GetEnumerator() just returns the list's own enumerator, so
// foreach order is exactly declaration/insertion order (no sort, no reorder). Names are chosen
// deliberately non-alphabetical here so an accidental sort would be caught, not masked by
// insertion order coinciding with alphabetical order.
TEST(EffectParameterCollectionTest, IterationOrderMatchesInsertionOrder)
{
    EffectParameterCollection col;
    col.Add(MakeParam("Zebra"));
    col.Add(MakeParam("Apple"));
    col.Add(MakeParam("Mango"));
    std::vector<std::string> names;
    for (const auto& p : col) { names.push_back(p.getNameProperty()); }
    ASSERT_EQ(names.size(), 3u);
    EXPECT_EQ(names[0], "Zebra");
    EXPECT_EQ(names[1], "Apple");
    EXPECT_EQ(names[2], "Mango");
}

// Task 357: FNA's this[string name] does a plain `name.Equals(elem.Name)` scan — ordinal,
// case-sensitive, first match wins on duplicates (foreach loop, no dedup/ambiguity check).
// GetParameterBySemantic uses the identical pattern over Semantic instead of Name. Neither
// was previously verified for case-sensitivity or duplicate-name/-semantic tie-breaking.

TEST(EffectParameterCollectionTest, IndexByNameIsCaseSensitive)
{
    EffectParameterCollection col;
    col.Add(MakeParam("Alpha"));
    EXPECT_EQ(col["alpha"], nullptr);
    EXPECT_NE(col["Alpha"], nullptr);
}

TEST(EffectParameterCollectionTest, IndexByNameFirstMatchWinsOnDuplicateNames)
{
    EffectParameterCollection col;
    col.Add(EffectParameter("Dup", "", 1, 1, EffectParameterClass::Scalar, EffectParameterType::Single));
    col.Add(EffectParameter("Dup", "", 4, 4, EffectParameterClass::Matrix, EffectParameterType::Single));
    EffectParameter* p = col["Dup"];
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->getParameterClassProperty(), EffectParameterClass::Scalar);
}

TEST(EffectParameterCollectionTest, GetParameterBySemanticIsCaseSensitive)
{
    EffectParameterCollection col;
    col.Add(MakeParam("Proj", "PROJECTION"));
    EXPECT_EQ(col.GetParameterBySemantic("projection"), nullptr);
    EXPECT_NE(col.GetParameterBySemantic("PROJECTION"), nullptr);
}

TEST(EffectParameterCollectionTest, GetParameterBySemanticFirstMatchWinsOnDuplicates)
{
    EffectParameterCollection col;
    col.Add(MakeParam("first", "TEXCOORD"));
    col.Add(MakeParam("second", "TEXCOORD"));
    EffectParameter* p = col.GetParameterBySemantic("TEXCOORD");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->getNameProperty(), "first");
}

// ============================================================
// EffectPassCollection (standalone, beyond EffectTechniqueTests)
// ============================================================

TEST(EffectPassCollectionTest, EmptyCollectionHasZeroCount)
{
    EffectPassCollection col;
    EXPECT_EQ(col.getCountProperty(), 0);
}

TEST(EffectPassCollectionTest, AddIncreasesCount)
{
    EffectPassCollection col;
    col.Add(EffectPass(nullptr, "Pass0"));
    EXPECT_EQ(col.getCountProperty(), 1);
}

TEST(EffectPassCollectionTest, IndexByIntReturnsPass)
{
    EffectPassCollection col;
    col.Add(EffectPass(nullptr, "Pass0"));
    col.Add(EffectPass(nullptr, "Pass1"));
    EXPECT_EQ(col[0].getNameProperty(), "Pass0");
    EXPECT_EQ(col[1].getNameProperty(), "Pass1");
}

TEST(EffectPassCollectionTest, IndexByNameReturnsCorrectPass)
{
    EffectPassCollection col;
    col.Add(EffectPass(nullptr, "Alpha"));
    col.Add(EffectPass(nullptr, "Beta"));
    const EffectPass* p = col["Beta"];
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->getNameProperty(), "Beta");
}

TEST(EffectPassCollectionTest, IndexByNameReturnsNullForMissingName)
{
    EffectPassCollection col;
    col.Add(EffectPass(nullptr, "Pass0"));
    EXPECT_EQ(col["NoSuch"], nullptr);
}

TEST(EffectPassCollectionTest, IterationVisitsAllPasses)
{
    EffectPassCollection col;
    col.Add(EffectPass(nullptr, "A"));
    col.Add(EffectPass(nullptr, "B"));
    int count = 0;
    for (const auto& p : col) { (void)p; ++count; }
    EXPECT_EQ(count, 2);
}

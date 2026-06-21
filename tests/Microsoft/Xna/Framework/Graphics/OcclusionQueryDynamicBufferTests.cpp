// SPDX-License-Identifier: MS-PL
//
// Task 130: unit tests for OcclusionQuery, EffectPass, EffectPassCollection,
// DynamicVertexBuffer, and DynamicIndexBuffer.
//
// OcclusionQuery / DynamicVertexBuffer / DynamicIndexBuffer all require a
// GraphicsDevice to construct; their GPU-side behaviour is covered by the
// occlusion_query_test and easygl_textured_quad_test integration tests.
// What can be verified without a GPU:
//  - EffectPass: Apply() with null owner (no-op), annotations empty
//  - EffectPassCollection: out-of-bounds throws, range-for, const overloads
//  - BufferUsage enum values (used by DynamicVertexBuffer constructor)
//  - IndexElementSize enum values (used by DynamicIndexBuffer constructor)

#include <gtest/gtest.h>
#include <stdexcept>
#include <vector>
#include <string>

#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"

using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::EffectPass;
using Microsoft::Xna::Framework::Graphics::EffectPassCollection;
using Microsoft::Xna::Framework::Graphics::IndexElementSize;

// -----------------------------------------------------------------------
// BufferUsage — XNA 4.0: None=0, WriteOnly=1
// -----------------------------------------------------------------------

TEST(BufferUsageTest, NoneIsZero)
{
    EXPECT_EQ(static_cast<int>(BufferUsage::None), 0);
}

TEST(BufferUsageTest, WriteOnlyIsOne)
{
    EXPECT_EQ(static_cast<int>(BufferUsage::WriteOnly), 1);
}

TEST(BufferUsageTest, NoneAndWriteOnlyAreDistinct)
{
    EXPECT_NE(BufferUsage::None, BufferUsage::WriteOnly);
}

// -----------------------------------------------------------------------
// IndexElementSize — XNA 4.0: SixteenBits=16, ThirtyTwoBits=32
// -----------------------------------------------------------------------

TEST(IndexElementSizeTest, SixteenBitsIssixteen)
{
    EXPECT_EQ(static_cast<int>(IndexElementSize::SixteenBits), 16);
}

TEST(IndexElementSizeTest, ThirtyTwoBitsIsThirtyTwo)
{
    EXPECT_EQ(static_cast<int>(IndexElementSize::ThirtyTwoBits), 32);
}

TEST(IndexElementSizeTest, SixteenAndThirtyTwoAreDistinct)
{
    EXPECT_NE(IndexElementSize::SixteenBits, IndexElementSize::ThirtyTwoBits);
}

// -----------------------------------------------------------------------
// EffectPass — gaps not covered by EffectTechniqueTests.cpp
// -----------------------------------------------------------------------

TEST(EffectPassTest, ApplyWithNullOwnerDoesNotThrow)
{
    EffectPass p(nullptr, "P0");
    EXPECT_NO_THROW(p.Apply());
}

TEST(EffectPassTest, AnnotationsEmptyOnConstruction)
{
    EffectPass p(nullptr, "P0");
    EXPECT_EQ(p.getAnnotationsProperty().getCountProperty(), 0);
}

TEST(EffectPassTest, ConstAnnotationsEmptyOnConstruction)
{
    const EffectPass p(nullptr, "P0");
    EXPECT_EQ(p.getAnnotationsProperty().getCountProperty(), 0);
}

// -----------------------------------------------------------------------
// EffectPassCollection — gaps not covered by EffectTechniqueTests.cpp
// -----------------------------------------------------------------------

TEST(EffectPassCollectionTest, DefaultConstructorIsEmpty)
{
    EffectPassCollection col;
    EXPECT_EQ(col.getCountProperty(), 0);
}

TEST(EffectPassCollectionTest, IndexOutOfBoundsThrows)
{
    EffectPassCollection col;
    col.Add(EffectPass(nullptr, "P0"));
    EXPECT_THROW({ [[maybe_unused]] EffectPass& p = col[1]; }, std::out_of_range);
}

TEST(EffectPassCollectionTest, NegativeIndexThrows)
{
    EffectPassCollection col;
    col.Add(EffectPass(nullptr, "P0"));
    EXPECT_THROW({ [[maybe_unused]] EffectPass& p = col[-1]; }, std::out_of_range);
}

TEST(EffectPassCollectionTest, ConstIndexOutOfBoundsThrows)
{
    EffectPassCollection col;
    col.Add(EffectPass(nullptr, "P0"));
    const EffectPassCollection& cref = col;
    EXPECT_THROW({ [[maybe_unused]] const EffectPass& p = cref[1]; }, std::out_of_range);
}

TEST(EffectPassCollectionTest, ConstIndexByNameFound)
{
    EffectPassCollection col;
    col.Add(EffectPass(nullptr, "Alpha"));
    const EffectPassCollection& cref = col;
    const EffectPass* p = cref["Alpha"];
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->getNameProperty(), "Alpha");
}

TEST(EffectPassCollectionTest, ConstIndexByNameNotFoundReturnsNull)
{
    EffectPassCollection col;
    col.Add(EffectPass(nullptr, "Alpha"));
    const EffectPassCollection& cref = col;
    EXPECT_EQ(cref["Beta"], nullptr);
}

TEST(EffectPassCollectionTest, RangeForIteratesAllPasses)
{
    EffectPassCollection col;
    col.Add(EffectPass(nullptr, "A"));
    col.Add(EffectPass(nullptr, "B"));
    col.Add(EffectPass(nullptr, "C"));

    std::vector<std::string> names;
    for (const EffectPass& p : col)
        names.push_back(p.getNameProperty());

    ASSERT_EQ(names.size(), 3u);
    EXPECT_EQ(names[0], "A");
    EXPECT_EQ(names[1], "B");
    EXPECT_EQ(names[2], "C");
}

TEST(EffectPassCollectionTest, RangeForOnEmptyCollectionDoesNotIterate)
{
    EffectPassCollection col;
    int count = 0;
    for ([[maybe_unused]] const EffectPass& p : col)
        ++count;
    EXPECT_EQ(count, 0);
}

TEST(EffectPassCollectionTest, MutableIndexAllowsAccess)
{
    EffectPassCollection col;
    col.Add(EffectPass(nullptr, "Mutable"));
    EXPECT_EQ(col[0].getNameProperty(), "Mutable");
}

TEST(EffectPassCollectionTest, AddMultiplePasses)
{
    EffectPassCollection col;
    col.Add(EffectPass(nullptr, "X"));
    col.Add(EffectPass(nullptr, "Y"));
    EXPECT_EQ(col.getCountProperty(), 2);
    EXPECT_EQ(col[0].getNameProperty(), "X");
    EXPECT_EQ(col[1].getNameProperty(), "Y");
}

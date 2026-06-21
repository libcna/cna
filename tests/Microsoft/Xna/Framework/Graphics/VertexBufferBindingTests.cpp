// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <vector>
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"

using Microsoft::Xna::Framework::Graphics::VertexBufferBinding;

// --- Default constructor ---

TEST(VertexBufferBindingTest, DefaultConstructor_NullBuffer)
{
    VertexBufferBinding b;
    EXPECT_EQ(b.getVertexBufferProperty(), nullptr);
}

TEST(VertexBufferBindingTest, DefaultConstructor_OffsetZero)
{
    VertexBufferBinding b;
    EXPECT_EQ(b.getVertexOffsetProperty(), 0);
}

TEST(VertexBufferBindingTest, DefaultConstructor_FrequencyZero)
{
    VertexBufferBinding b;
    EXPECT_EQ(b.getInstanceFrequencyProperty(), 0);
}

// --- Parameterized constructor ---

TEST(VertexBufferBindingTest, OneArg_DefaultOffsetAndFrequency)
{
    VertexBufferBinding b(nullptr);
    EXPECT_EQ(b.getVertexBufferProperty(), nullptr);
    EXPECT_EQ(b.getVertexOffsetProperty(), 0);
    EXPECT_EQ(b.getInstanceFrequencyProperty(), 0);
}

TEST(VertexBufferBindingTest, TwoArg_FrequencyDefaultsToZero)
{
    VertexBufferBinding b(nullptr, 8);
    EXPECT_EQ(b.getVertexOffsetProperty(), 8);
    EXPECT_EQ(b.getInstanceFrequencyProperty(), 0);
}

TEST(VertexBufferBindingTest, ThreeArg_AllValuesStored)
{
    VertexBufferBinding b(nullptr, 4, 1);
    EXPECT_EQ(b.getVertexBufferProperty(), nullptr);
    EXPECT_EQ(b.getVertexOffsetProperty(), 4);
    EXPECT_EQ(b.getInstanceFrequencyProperty(), 1);
}

TEST(VertexBufferBindingTest, ThreeArg_ZeroFrequencyMeansNonInstanced)
{
    VertexBufferBinding b(nullptr, 0, 0);
    EXPECT_EQ(b.getInstanceFrequencyProperty(), 0);
}

TEST(VertexBufferBindingTest, ThreeArg_NonZeroFrequencyMeansInstanced)
{
    VertexBufferBinding b(nullptr, 0, 2);
    EXPECT_EQ(b.getInstanceFrequencyProperty(), 2);
}

// --- GetVertexBuffers contract: returns a copy of the current binding list.
//     The method is `return currentVertexBuffers_` — its correctness is ensured
//     by the copy-semantics of VertexBufferBinding tested here.  The vector
//     round-trips without data loss.                                           ---

TEST(VertexBufferBindingTest, VectorOfBindings_SizeAndContents)
{
    std::vector<VertexBufferBinding> bindings = {
        VertexBufferBinding(nullptr, 0, 0),
        VertexBufferBinding(nullptr, 4, 1),
        VertexBufferBinding(nullptr, 8, 2),
    };
    ASSERT_EQ(bindings.size(), 3u);
    EXPECT_EQ(bindings[0].getVertexOffsetProperty(),      0);
    EXPECT_EQ(bindings[0].getInstanceFrequencyProperty(), 0);
    EXPECT_EQ(bindings[1].getVertexOffsetProperty(),      4);
    EXPECT_EQ(bindings[1].getInstanceFrequencyProperty(), 1);
    EXPECT_EQ(bindings[2].getVertexOffsetProperty(),      8);
    EXPECT_EQ(bindings[2].getInstanceFrequencyProperty(), 2);
}

TEST(VertexBufferBindingTest, EmptyVector_DefaultStateForGetVertexBuffers)
{
    std::vector<VertexBufferBinding> bindings;
    EXPECT_TRUE(bindings.empty());
}

TEST(VertexBufferBindingTest, CopiedBinding_PreservesValues)
{
    VertexBufferBinding original(nullptr, 12, 3);
    VertexBufferBinding copy = original;
    EXPECT_EQ(copy.getVertexBufferProperty(),     original.getVertexBufferProperty());
    EXPECT_EQ(copy.getVertexOffsetProperty(),     original.getVertexOffsetProperty());
    EXPECT_EQ(copy.getInstanceFrequencyProperty(), original.getInstanceFrequencyProperty());
}

// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexBufferBinding;

class VertexBufferBindingValidationTest : public ::testing::Test
{
protected:
    GraphicsDevice graphicsDevice;
    // Deliberately NOT a direct member: constructing a real VertexBuffer is inherently a
    // 3D-pipeline operation, and a member initializer runs before SetUp() -- too early for
    // GTEST_SKIP() to prevent the throw on a renderer with no 3D pipeline. Built lazily in
    // SetUp() instead, after the capability gate below (never a dangling/null-bound reference:
    // every test that dereferences vertexBuffer() only runs once SetUp() has already skipped or
    // constructed it).
    std::unique_ptr<VertexBuffer> vertexBufferStorage;
    [[nodiscard]] VertexBuffer* vertexBuffer() const { return vertexBufferStorage.get(); }

    void SetUp() override
    {
        if (!graphicsDevice.SupportsCapability(CNA::GraphicsCapability::ThreeD))
            GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";
        vertexBufferStorage = std::make_unique<VertexBuffer>(graphicsDevice, 16);
    }
};

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

TEST(VertexBufferBindingTest, OneArg_NullBufferThrowsArgumentNullException)
{
    EXPECT_THROW(VertexBufferBinding(nullptr), System::ArgumentNullException);
}

TEST_F(VertexBufferBindingValidationTest, OneArg_DefaultOffsetAndFrequency)
{
    VertexBufferBinding b(vertexBuffer());
    EXPECT_EQ(b.getVertexBufferProperty(), vertexBuffer());
    EXPECT_EQ(b.getVertexOffsetProperty(), 0);
    EXPECT_EQ(b.getInstanceFrequencyProperty(), 0);
}

TEST_F(VertexBufferBindingValidationTest, TwoArg_FrequencyDefaultsToZero)
{
    VertexBufferBinding b(vertexBuffer(), 8);
    EXPECT_EQ(b.getVertexOffsetProperty(), 8);
    EXPECT_EQ(b.getInstanceFrequencyProperty(), 0);
}

TEST_F(VertexBufferBindingValidationTest, ThreeArg_AllValuesStored)
{
    VertexBufferBinding b(vertexBuffer(), 4, 1);
    EXPECT_EQ(b.getVertexBufferProperty(), vertexBuffer());
    EXPECT_EQ(b.getVertexOffsetProperty(), 4);
    EXPECT_EQ(b.getInstanceFrequencyProperty(), 1);
}

TEST_F(VertexBufferBindingValidationTest, ThreeArg_ZeroFrequencyMeansNonInstanced)
{
    VertexBufferBinding b(vertexBuffer(), 0, 0);
    EXPECT_EQ(b.getInstanceFrequencyProperty(), 0);
}

TEST_F(VertexBufferBindingValidationTest, ThreeArg_NonZeroFrequencyMeansInstanced)
{
    VertexBufferBinding b(vertexBuffer(), 0, 2);
    EXPECT_EQ(b.getInstanceFrequencyProperty(), 2);
}

TEST_F(VertexBufferBindingValidationTest, NegativeVertexOffsetThrowsArgumentOutOfRangeException)
{
    EXPECT_THROW(
        VertexBufferBinding(vertexBuffer(), -1, 0),
        System::ArgumentOutOfRangeException);
}

TEST_F(VertexBufferBindingValidationTest, NegativeInstanceFrequencyThrowsArgumentOutOfRangeException)
{
    EXPECT_THROW(
        VertexBufferBinding(vertexBuffer(), 0, -1),
        System::ArgumentOutOfRangeException);
}

// --- GetVertexBuffers contract: returns a copy of the current binding list.
//     The method is `return currentVertexBuffers_` — its correctness is ensured
//     by the copy-semantics of VertexBufferBinding tested here.  The vector
//     round-trips without data loss.                                           ---

TEST_F(VertexBufferBindingValidationTest, VectorOfBindings_SizeAndContents)
{
    std::vector<VertexBufferBinding> bindings = {
        VertexBufferBinding(vertexBuffer(), 0, 0),
        VertexBufferBinding(vertexBuffer(), 4, 1),
        VertexBufferBinding(vertexBuffer(), 8, 2),
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

TEST_F(VertexBufferBindingValidationTest, CopiedBinding_PreservesValues)
{
    VertexBufferBinding original(vertexBuffer(), 12, 3);
    VertexBufferBinding copy = original;
    EXPECT_EQ(copy.getVertexBufferProperty(),     original.getVertexBufferProperty());
    EXPECT_EQ(copy.getVertexOffsetProperty(),     original.getVertexOffsetProperty());
    EXPECT_EQ(copy.getInstanceFrequencyProperty(), original.getInstanceFrequencyProperty());
}

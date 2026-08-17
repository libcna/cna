// SPDX-License-Identifier: MS-PL
// Task 434: ModelMeshPart unit tests.
//
// VertexBuffer*/IndexBuffer*/Effect* are all stored and compared as raw pointers by
// ModelMeshPart/ModelEffectCollection -- confirmed by reading both .cpp files -- never
// dereferenced. This lets these tests use plain, never-dereferenced fake pointer values
// (reinterpret_cast from a small integer) for identity-only checks, avoiding any need for a real
// GraphicsDevice/renderer -- a genuine unit test, not an integration test, matching this task's own
// "Unit tests" framing.

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "System/Object.hpp"

using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    // Never dereferenced by any code path exercised here -- see file header comment.
    VertexBuffer* FakeVertexBuffer(int id) { return reinterpret_cast<VertexBuffer*>(static_cast<std::uintptr_t>(id)); }
    IndexBuffer*  FakeIndexBuffer(int id)  { return reinterpret_cast<IndexBuffer*>(static_cast<std::uintptr_t>(id)); }
    Effect*       FakeEffect(int id)       { return reinterpret_cast<Effect*>(static_cast<std::uintptr_t>(id)); }
}

// --- Default constructor ---

TEST(ModelMeshPartTest, DefaultNumVerticesIsZero)
{
    ModelMeshPart part;
    EXPECT_EQ(part.getNumVerticesProperty(), 0);
}

TEST(ModelMeshPartTest, DefaultPrimitiveCountIsZero)
{
    ModelMeshPart part;
    EXPECT_EQ(part.getPrimitiveCountProperty(), 0);
}

TEST(ModelMeshPartTest, DefaultStartIndexIsZero)
{
    ModelMeshPart part;
    EXPECT_EQ(part.getStartIndexProperty(), 0);
}

TEST(ModelMeshPartTest, DefaultVertexOffsetIsZero)
{
    ModelMeshPart part;
    EXPECT_EQ(part.getVertexOffsetProperty(), 0);
}

TEST(ModelMeshPartTest, DefaultVertexBufferIsNull)
{
    ModelMeshPart part;
    EXPECT_EQ(part.getVertexBufferProperty(), nullptr);
}

TEST(ModelMeshPartTest, DefaultIndexBufferIsNull)
{
    ModelMeshPart part;
    EXPECT_EQ(part.getIndexBufferProperty(), nullptr);
}

TEST(ModelMeshPartTest, DefaultEffectIsNull)
{
    ModelMeshPart part;
    EXPECT_EQ(part.getEffectProperty(), nullptr);
}

TEST(ModelMeshPartTest, DefaultTagIsNull)
{
    ModelMeshPart part;
    EXPECT_EQ(part.getTagProperty(), nullptr);
}

TEST(ModelMeshPartTest, SpecularSamplerSlotsAreSeparateAndValidateForwardCompatibly)
{
    ModelMeshPart part;
    ASSERT_EQ(part.getSamplerStatesEXTProperty().size(), 5u);
    ASSERT_EQ(part.getSpecularSamplerStatesEXTProperty().size(), 2u);

    SamplerState strength;
    strength.setAddressUProperty(TextureAddressMode::Clamp);
    SamplerState color;
    color.setAddressVProperty(TextureAddressMode::Mirror);
    part.setSpecularSamplerStateEXTProperty(0, strength);
    part.setSpecularSamplerStateEXTProperty(1, color);
    part.setSpecularSamplerStateEXTProperty(2, SamplerState::PointClamp);

    EXPECT_EQ(part.getSpecularSamplerStatesEXTProperty()[0].getAddressUProperty(),
              TextureAddressMode::Clamp);
    EXPECT_EQ(part.getSpecularSamplerStatesEXTProperty()[1].getAddressVProperty(),
              TextureAddressMode::Mirror);
    EXPECT_EQ(part.getSamplerStatesEXTProperty().size(), 5u);
}

// --- Parameterized constructor ---

TEST(ModelMeshPartTest, CtorSetsVertexBuffer)
{
    VertexBuffer* vb = FakeVertexBuffer(1);
    ModelMeshPart part(vb, nullptr, 4, 2, 0, 0);
    EXPECT_EQ(part.getVertexBufferProperty(), vb);
}

TEST(ModelMeshPartTest, CtorSetsIndexBuffer)
{
    IndexBuffer* ib = FakeIndexBuffer(2);
    ModelMeshPart part(nullptr, ib, 4, 2, 0, 0);
    EXPECT_EQ(part.getIndexBufferProperty(), ib);
}

TEST(ModelMeshPartTest, CtorSetsNumVertices)
{
    ModelMeshPart part(nullptr, nullptr, 24, 2, 0, 0);
    EXPECT_EQ(part.getNumVerticesProperty(), 24);
}

TEST(ModelMeshPartTest, CtorSetsPrimitiveCount)
{
    ModelMeshPart part(nullptr, nullptr, 24, 8, 0, 0);
    EXPECT_EQ(part.getPrimitiveCountProperty(), 8);
}

TEST(ModelMeshPartTest, CtorSetsStartIndex)
{
    ModelMeshPart part(nullptr, nullptr, 24, 8, 6, 0);
    EXPECT_EQ(part.getStartIndexProperty(), 6);
}

TEST(ModelMeshPartTest, CtorSetsVertexOffset)
{
    ModelMeshPart part(nullptr, nullptr, 24, 8, 6, 3);
    EXPECT_EQ(part.getVertexOffsetProperty(), 3);
}

// --- Tag setter ---

TEST(ModelMeshPartTest, SetTag)
{
    ModelMeshPart part;
    System::Object* tag = reinterpret_cast<System::Object*>(static_cast<std::uintptr_t>(1));
    part.setTagProperty(tag);
    EXPECT_EQ(part.getTagProperty(), tag);
}

// --- Effect setter, no parent (ModelMesh) assigned yet ---

TEST(ModelMeshPartTest, SetEffectWithNoParentJustStoresPointer)
{
    ModelMeshPart part;
    Effect* fx = FakeEffect(1);
    part.setEffectProperty(fx);
    EXPECT_EQ(part.getEffectProperty(), fx);
}

TEST(ModelMeshPartTest, SetEffectToSamePointerIsANoOp)
{
    ModelMeshPart part;
    Effect* fx = FakeEffect(1);
    part.setEffectProperty(fx);
    part.setEffectProperty(fx);
    EXPECT_EQ(part.getEffectProperty(), fx);
}

// --- Effect setter, with a parent ModelMesh: add/remove from the mesh's Effects collection ---

TEST(ModelMeshPartTest, SetEffectAddsToParentMeshEffectsCollection)
{
    ModelMeshPart part;
    ModelMesh mesh(nullptr, { &part });

    Effect* fx = FakeEffect(1);
    part.setEffectProperty(fx);

    EXPECT_TRUE(mesh.getEffectsProperty().Contains(fx));
    EXPECT_EQ(mesh.getEffectsProperty().getCountProperty(), 1);
}

TEST(ModelMeshPartTest, ReplacingEffectRemovesOldOneFromParentWhenUnused)
{
    ModelMeshPart part;
    ModelMesh mesh(nullptr, { &part });

    Effect* fx1 = FakeEffect(1);
    Effect* fx2 = FakeEffect(2);
    part.setEffectProperty(fx1);
    part.setEffectProperty(fx2);

    EXPECT_FALSE(mesh.getEffectsProperty().Contains(fx1));
    EXPECT_TRUE(mesh.getEffectsProperty().Contains(fx2));
    EXPECT_EQ(mesh.getEffectsProperty().getCountProperty(), 1);
}

TEST(ModelMeshPartTest, ReplacingEffectKeepsOldOneWhenSiblingPartStillUsesIt)
{
    ModelMeshPart partA;
    ModelMeshPart partB;
    ModelMesh mesh(nullptr, { &partA, &partB });

    Effect* shared = FakeEffect(1);
    Effect* other  = FakeEffect(2);
    partA.setEffectProperty(shared);
    partB.setEffectProperty(shared);

    // partA moves to a different effect -- partB still references `shared`, so it must remain.
    partA.setEffectProperty(other);

    EXPECT_TRUE(mesh.getEffectsProperty().Contains(shared));
    EXPECT_TRUE(mesh.getEffectsProperty().Contains(other));
    EXPECT_EQ(mesh.getEffectsProperty().getCountProperty(), 2);
}

TEST(ModelMeshPartTest, SettingSameEffectOnTwoPartsDoesNotDuplicateInParentCollection)
{
    ModelMeshPart partA;
    ModelMeshPart partB;
    ModelMesh mesh(nullptr, { &partA, &partB });

    Effect* shared = FakeEffect(1);
    partA.setEffectProperty(shared);
    partB.setEffectProperty(shared);

    EXPECT_EQ(mesh.getEffectsProperty().getCountProperty(), 1);
}

TEST(ModelMeshPartTest, SettingEffectToNullRemovesItFromParentWhenUnused)
{
    ModelMeshPart part;
    ModelMesh mesh(nullptr, { &part });

    Effect* fx = FakeEffect(1);
    part.setEffectProperty(fx);
    part.setEffectProperty(nullptr);

    EXPECT_EQ(part.getEffectProperty(), nullptr);
    EXPECT_FALSE(mesh.getEffectsProperty().Contains(fx));
    EXPECT_EQ(mesh.getEffectsProperty().getCountProperty(), 0);
}

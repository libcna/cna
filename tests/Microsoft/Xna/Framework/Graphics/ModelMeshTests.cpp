// SPDX-License-Identifier: MS-PL
// Task 29: ModelMesh unit tests.

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"

using namespace Microsoft::Xna::Framework::Graphics;

TEST(ModelMeshTest, DefaultNameIsEmpty)
{
    ModelMesh m(nullptr, {});
    EXPECT_EQ(m.getNameProperty(), "");
}

TEST(ModelMeshTest, NamedConstructorReturnsName)
{
    ModelMesh m(nullptr, "Body", {});
    EXPECT_EQ(m.getNameProperty(), "Body");
}

TEST(ModelMeshTest, EmptyPartsCountIsZero)
{
    ModelMesh m(nullptr, {});
    EXPECT_EQ(m.getMeshPartsProperty().getCountProperty(), 0);
}

TEST(ModelMeshTest, ParentBoneIsNullBeforeModelAssigns)
{
    ModelMesh m(nullptr, {});
    EXPECT_EQ(m.getParentBoneProperty(), nullptr);
}

TEST(ModelMeshTest, TagIsNullByDefault)
{
    ModelMesh m(nullptr, {});
    EXPECT_EQ(m.getTagProperty(), nullptr);
}

TEST(ModelMeshTest, EffectsEmptyByDefault)
{
    ModelMesh m(nullptr, {});
    EXPECT_EQ(m.getEffectsProperty().getCountProperty(), 0);
}

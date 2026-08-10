// SPDX-License-Identifier: MS-PL
// Task 29: ModelMesh unit tests.

#include <cstdint>
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

TEST(ModelMeshTest, SetParentBonePropertyStoresValue)
{
    // Task 936: a fake, never-dereferenced pointer is enough here -- this test only
    // verifies the setter stores whatever pointer it is given, mirroring
    // ModelMeshPartTest's own established fake-pointer convention for owner/reference
    // fields that this test suite never needs to actually construct a real ModelBone for.
    ModelMesh m(nullptr, {});
    auto* bone = reinterpret_cast<ModelBone*>(static_cast<std::uintptr_t>(1));
    m.setParentBoneProperty(bone);
    EXPECT_EQ(m.getParentBoneProperty(), bone);
}

TEST(ModelMeshTest, SetParentBonePropertyNullClearsValue)
{
    ModelMesh m(nullptr, {});
    auto* bone = reinterpret_cast<ModelBone*>(static_cast<std::uintptr_t>(1));
    m.setParentBoneProperty(bone);
    m.setParentBoneProperty(nullptr);
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

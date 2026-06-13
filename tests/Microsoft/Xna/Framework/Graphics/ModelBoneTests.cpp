// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"

using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Graphics::ModelBone;

// --- Default constructor ---

TEST(ModelBoneTest, DefaultNameEmpty)
{
    ModelBone bone;
    EXPECT_TRUE(bone.getNameProperty().empty());
}

TEST(ModelBoneTest, DefaultIndexZero)
{
    ModelBone bone;
    EXPECT_EQ(bone.getIndexProperty(), 0);
}

TEST(ModelBoneTest, DefaultParentNull)
{
    ModelBone bone;
    EXPECT_EQ(bone.getParentProperty(), nullptr);
}

TEST(ModelBoneTest, DefaultChildrenEmpty)
{
    ModelBone bone;
    EXPECT_TRUE(bone.getChildrenProperty().empty());
}

TEST(ModelBoneTest, DefaultTransformIdentity)
{
    ModelBone bone;
    const Matrix& t = bone.getTransformProperty();
    EXPECT_FLOAT_EQ(t.M11, 1.0f);
    EXPECT_FLOAT_EQ(t.M22, 1.0f);
    EXPECT_FLOAT_EQ(t.M33, 1.0f);
    EXPECT_FLOAT_EQ(t.M44, 1.0f);
    EXPECT_FLOAT_EQ(t.M12, 0.0f);
}

// --- Parameterized constructor ---

TEST(ModelBoneTest, CtorName)
{
    ModelBone bone(3, "Spine");
    EXPECT_EQ(bone.getNameProperty(), "Spine");
}

TEST(ModelBoneTest, CtorIndex)
{
    ModelBone bone(3, "Spine");
    EXPECT_EQ(bone.getIndexProperty(), 3);
}

// --- Transform setter ---

TEST(ModelBoneTest, SetTransform)
{
    ModelBone bone;
    Matrix m = Matrix::getIdentityProperty();
    m.M41 = 5.0f;
    bone.setTransformProperty(m);
    EXPECT_FLOAT_EQ(bone.getTransformProperty().M41, 5.0f);
}

// --- Parent/child chain ---

TEST(ModelBoneTest, AddChildSetsParent)
{
    ModelBone parent(0, "Root");
    ModelBone child(1, "Arm");
    parent.AddChild(&child);
    EXPECT_EQ(child.getParentProperty(), &parent);
}

TEST(ModelBoneTest, AddChildAppearsInChildren)
{
    ModelBone parent(0, "Root");
    ModelBone child(1, "Arm");
    parent.AddChild(&child);
    ASSERT_EQ(parent.getChildrenProperty().size(), 1u);
    EXPECT_EQ(parent.getChildrenProperty()[0], &child);
}

TEST(ModelBoneTest, MultipleChildren)
{
    ModelBone root(0, "Root");
    ModelBone left(1, "Left");
    ModelBone right(2, "Right");
    root.AddChild(&left);
    root.AddChild(&right);
    EXPECT_EQ(root.getChildrenProperty().size(), 2u);
}

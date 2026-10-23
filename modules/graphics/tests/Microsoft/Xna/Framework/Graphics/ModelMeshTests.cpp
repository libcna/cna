// SPDX-License-Identifier: MS-PL
// Task 29: ModelMesh unit tests.

#include <cstdint>
#include <memory>
#include <vector>
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"

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

// CNA-DEMOS-ISSUE-081: a mesh built from parts that already carry effects starts with those
// effects in Effects -- each distinct non-null one once, in part order -- exactly as if they had
// been assigned while the parts belonged to it. (Fake, never-dereferenced Effect pointers, as in
// ModelMeshPartTest.)
TEST(ModelMeshTest, ConstructionSeedsEffectsFromTheParts)
{
    Effect* first = reinterpret_cast<Effect*>(static_cast<std::uintptr_t>(1));
    Effect* second = reinterpret_cast<Effect*>(static_cast<std::uintptr_t>(2));
    ModelMeshPart a, b, c, d;
    a.setEffectProperty(first);
    b.setEffectProperty(second);
    c.setEffectProperty(first);
    for (const bool named : {false, true})
    {
        SCOPED_TRACE(named ? "named" : "unnamed");
        // Heap-held so the parts' parent pointers name the mesh itself, not a moved-from copy.
        const std::vector<ModelMeshPart*> parts{&a, &b, &c, &d};
        auto mesh = named ? std::make_unique<ModelMesh>(nullptr, "Seeded", parts)
                          : std::make_unique<ModelMesh>(nullptr, parts);
        const ModelEffectCollection& effects = mesh->getEffectsProperty();
        ASSERT_EQ(effects.getCountProperty(), 2);
        EXPECT_EQ(effects[0], first);
        EXPECT_EQ(effects[1], second);

        // The seeded collection keeps following the parts.
        b.setEffectProperty(nullptr);
        EXPECT_EQ(effects.getCountProperty(), 1);
        b.setEffectProperty(second);
        EXPECT_EQ(effects.getCountProperty(), 2);
    }
}

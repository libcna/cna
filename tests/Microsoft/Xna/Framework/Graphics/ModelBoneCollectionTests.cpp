// SPDX-License-Identifier: MS-PL
// Task 30: ModelBoneCollection unit tests.

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Graphics/ModelBoneCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"

using namespace Microsoft::Xna::Framework::Graphics;

TEST(ModelBoneCollectionTest, DefaultConstructorCountIsZero)
{
    ModelBoneCollection col;
    EXPECT_EQ(col.getCountProperty(), 0);
}

TEST(ModelBoneCollectionTest, IndexOutOfRangeThrows)
{
    ModelBoneCollection col;
    EXPECT_THROW(col[0], std::out_of_range);
}

TEST(ModelBoneCollectionTest, NameLookupNotFoundThrows)
{
    ModelBoneCollection col;
    EXPECT_THROW(col[std::string("Root")], std::out_of_range);
}

// --- Populated collection (Task 432): built via ModelBone::AddChild, since
// ModelBoneCollection's storage is private and only ModelBone/Model can populate it. ---

namespace
{
    struct PopulatedBones
    {
        ModelBone root { 0, "Root" };
        ModelBone left { 1, "Left" };
        ModelBone right { 2, "Right" };

        PopulatedBones()
        {
            root.AddChild(&left);
            root.AddChild(&right);
        }
    };
}

TEST(ModelBoneCollectionTest, CountReflectsPopulatedSize)
{
    PopulatedBones fixture;
    EXPECT_EQ(fixture.root.getChildrenProperty().getCountProperty(), 2);
}

TEST(ModelBoneCollectionTest, IndexByIntReturnsCorrectBone)
{
    PopulatedBones fixture;
    const ModelBoneCollection& col = fixture.root.getChildrenProperty();
    EXPECT_EQ(col[0], &fixture.left);
    EXPECT_EQ(col[1], &fixture.right);
}

TEST(ModelBoneCollectionTest, IndexByNameReturnsCorrectBone)
{
    PopulatedBones fixture;
    const ModelBoneCollection& col = fixture.root.getChildrenProperty();
    EXPECT_EQ(col["Left"], &fixture.left);
    EXPECT_EQ(col["Right"], &fixture.right);
}

TEST(ModelBoneCollectionTest, TryGetValueFindsExisting)
{
    PopulatedBones fixture;
    const ModelBoneCollection& col = fixture.root.getChildrenProperty();
    ModelBone* found = nullptr;
    EXPECT_TRUE(col.TryGetValue("Right", found));
    EXPECT_EQ(found, &fixture.right);
}

TEST(ModelBoneCollectionTest, TryGetValueFailsForMissing)
{
    PopulatedBones fixture;
    const ModelBoneCollection& col = fixture.root.getChildrenProperty();
    ModelBone* found = nullptr;
    EXPECT_FALSE(col.TryGetValue("Nope", found));
    EXPECT_EQ(found, nullptr);
}

TEST(ModelBoneCollectionTest, ContainsFindsExisting)
{
    PopulatedBones fixture;
    const ModelBoneCollection& col = fixture.root.getChildrenProperty();
    EXPECT_TRUE(col.Contains(&fixture.left));
}

TEST(ModelBoneCollectionTest, ContainsFailsForAbsentBone)
{
    PopulatedBones fixture;
    ModelBone unrelated(3, "Unrelated");
    const ModelBoneCollection& col = fixture.root.getChildrenProperty();
    EXPECT_FALSE(col.Contains(&unrelated));
}

TEST(ModelBoneCollectionTest, SupportsRangeBasedForLoop)
{
    PopulatedBones fixture;
    const ModelBoneCollection& col = fixture.root.getChildrenProperty();

    int count = 0;
    for (ModelBone* bone : col)
    {
        EXPECT_TRUE(bone == &fixture.left || bone == &fixture.right);
        ++count;
    }
    EXPECT_EQ(count, 2);
}

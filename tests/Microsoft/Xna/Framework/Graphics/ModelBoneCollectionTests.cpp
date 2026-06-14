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

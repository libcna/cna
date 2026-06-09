#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/ContainmentType.hpp"

using Microsoft::Xna::Framework::ContainmentType;

TEST(ContainmentTypeTest, DisjointValueIsZero)
{
    EXPECT_EQ(static_cast<int>(ContainmentType::Disjoint), 0);
}

TEST(ContainmentTypeTest, ContainsValueIsOne)
{
    EXPECT_EQ(static_cast<int>(ContainmentType::Contains), 1);
}

TEST(ContainmentTypeTest, IntersectsValueIsTwo)
{
    EXPECT_EQ(static_cast<int>(ContainmentType::Intersects), 2);
}

TEST(ContainmentTypeTest, ValuesAreDistinct)
{
    EXPECT_NE(ContainmentType::Disjoint,   ContainmentType::Contains);
    EXPECT_NE(ContainmentType::Contains,   ContainmentType::Intersects);
    EXPECT_NE(ContainmentType::Disjoint,   ContainmentType::Intersects);
}

TEST(ContainmentTypeTest, EqualityHolds)
{
    ContainmentType a = ContainmentType::Contains;
    ContainmentType b = ContainmentType::Contains;
    EXPECT_EQ(a, b);
}

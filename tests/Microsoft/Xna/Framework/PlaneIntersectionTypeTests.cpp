// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/PlaneIntersectionType.hpp"

using Microsoft::Xna::Framework::PlaneIntersectionType;

TEST(PlaneIntersectionTypeTest, ValuesAreDistinct)
{
    EXPECT_NE(PlaneIntersectionType::Front, PlaneIntersectionType::Back);
    EXPECT_NE(PlaneIntersectionType::Front, PlaneIntersectionType::Intersecting);
    EXPECT_NE(PlaneIntersectionType::Back, PlaneIntersectionType::Intersecting);
}

TEST(PlaneIntersectionTypeTest, OrdinalValuesMatchXna)
{
    // XNA ordinal order: Front=0, Back=1, Intersecting=2
    EXPECT_EQ(static_cast<int>(PlaneIntersectionType::Front), 0);
    EXPECT_EQ(static_cast<int>(PlaneIntersectionType::Back), 1);
    EXPECT_EQ(static_cast<int>(PlaneIntersectionType::Intersecting), 2);
}

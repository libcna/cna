// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"

using Microsoft::Xna::Framework::PlayerIndex;

TEST(PlayerIndexTest, ValuesAreDistinct)
{
    EXPECT_NE(PlayerIndex::One,   PlayerIndex::Two);
    EXPECT_NE(PlayerIndex::One,   PlayerIndex::Three);
    EXPECT_NE(PlayerIndex::One,   PlayerIndex::Four);
    EXPECT_NE(PlayerIndex::Two,   PlayerIndex::Three);
    EXPECT_NE(PlayerIndex::Two,   PlayerIndex::Four);
    EXPECT_NE(PlayerIndex::Three, PlayerIndex::Four);
}

TEST(PlayerIndexTest, OrdinalValuesMatchXna)
{
    EXPECT_EQ(static_cast<int>(PlayerIndex::One),   0);
    EXPECT_EQ(static_cast<int>(PlayerIndex::Two),   1);
    EXPECT_EQ(static_cast<int>(PlayerIndex::Three), 2);
    EXPECT_EQ(static_cast<int>(PlayerIndex::Four),  3);
}

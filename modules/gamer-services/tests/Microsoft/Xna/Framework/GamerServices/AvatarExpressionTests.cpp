// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/GamerServices/AvatarExpression.hpp"

using namespace Microsoft::Xna::Framework::GamerServices;

TEST(AvatarExpressionTest, DefaultsToNeutral) {
    AvatarExpression expression;
    EXPECT_EQ(expression.getMouthProperty(),        AvatarMouth::Neutral);
    EXPECT_EQ(expression.getLeftEyeProperty(),       AvatarEye::Neutral);
    EXPECT_EQ(expression.getRightEyeProperty(),      AvatarEye::Neutral);
    EXPECT_EQ(expression.getLeftEyebrowProperty(),   AvatarEyebrow::Neutral);
    EXPECT_EQ(expression.getRightEyebrowProperty(),  AvatarEyebrow::Neutral);
}

TEST(AvatarExpressionTest, MouthGetSet) {
    AvatarExpression expression;
    expression.setMouthProperty(AvatarMouth::Laughing);
    EXPECT_EQ(expression.getMouthProperty(), AvatarMouth::Laughing);
}

TEST(AvatarExpressionTest, LeftEyeGetSet) {
    AvatarExpression expression;
    expression.setLeftEyeProperty(AvatarEye::Blink);
    EXPECT_EQ(expression.getLeftEyeProperty(), AvatarEye::Blink);
}

TEST(AvatarExpressionTest, RightEyeGetSet) {
    AvatarExpression expression;
    expression.setRightEyeProperty(AvatarEye::LookUp);
    EXPECT_EQ(expression.getRightEyeProperty(), AvatarEye::LookUp);
}

TEST(AvatarExpressionTest, LeftEyebrowGetSet) {
    AvatarExpression expression;
    expression.setLeftEyebrowProperty(AvatarEyebrow::Raised);
    EXPECT_EQ(expression.getLeftEyebrowProperty(), AvatarEyebrow::Raised);
}

TEST(AvatarExpressionTest, RightEyebrowGetSet) {
    AvatarExpression expression;
    expression.setRightEyebrowProperty(AvatarEyebrow::Angry);
    EXPECT_EQ(expression.getRightEyebrowProperty(), AvatarEyebrow::Angry);
}

TEST(AvatarExpressionTest, PropertiesAreIndependent) {
    AvatarExpression expression;
    expression.setMouthProperty(AvatarMouth::Shocked);
    expression.setLeftEyeProperty(AvatarEye::Sad);
    EXPECT_EQ(expression.getMouthProperty(),   AvatarMouth::Shocked);
    EXPECT_EQ(expression.getLeftEyeProperty(), AvatarEye::Sad);
    EXPECT_EQ(expression.getRightEyeProperty(), AvatarEye::Neutral);
}

TEST(AvatarExpressionTest, SettersPreserveUndefinedEnumValuesAndCopiesAreIndependent) {
    // XNA AvatarExpression setters, each IL_0000..0007, only assign their backing
    // field: no enum-range validation or clamping. The struct copies all five fields by value.
    AvatarExpression original;
    original.setMouthProperty(static_cast<AvatarMouth>(-1));
    original.setLeftEyeProperty(static_cast<AvatarEye>(123));
    original.setRightEyeProperty(static_cast<AvatarEye>(-2));
    original.setLeftEyebrowProperty(static_cast<AvatarEyebrow>(456));
    original.setRightEyebrowProperty(static_cast<AvatarEyebrow>(-3));
    const auto copy = original;
    original.setMouthProperty(AvatarMouth::Neutral);
    original.setLeftEyeProperty(AvatarEye::Neutral);
    original.setRightEyeProperty(AvatarEye::Neutral);
    original.setLeftEyebrowProperty(AvatarEyebrow::Neutral);
    original.setRightEyebrowProperty(AvatarEyebrow::Neutral);
    EXPECT_EQ(-1, static_cast<int>(copy.getMouthProperty()));
    EXPECT_EQ(123, static_cast<int>(copy.getLeftEyeProperty()));
    EXPECT_EQ(-2, static_cast<int>(copy.getRightEyeProperty()));
    EXPECT_EQ(456, static_cast<int>(copy.getLeftEyebrowProperty()));
    EXPECT_EQ(-3, static_cast<int>(copy.getRightEyebrowProperty()));
    EXPECT_EQ(AvatarMouth::Neutral, original.getMouthProperty());
    EXPECT_EQ(AvatarEye::Neutral, original.getLeftEyeProperty());
    EXPECT_EQ(AvatarEye::Neutral, original.getRightEyeProperty());
    EXPECT_EQ(AvatarEyebrow::Neutral, original.getLeftEyebrowProperty());
    EXPECT_EQ(AvatarEyebrow::Neutral, original.getRightEyebrowProperty());
}

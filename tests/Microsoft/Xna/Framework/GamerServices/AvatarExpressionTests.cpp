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

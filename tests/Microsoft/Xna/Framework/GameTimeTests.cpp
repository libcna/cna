#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/GameTime.h"

using namespace Microsoft::Xna::Framework;

// Test default constructor
TEST(GameTimeTest, DefaultConstructor) {
    GameTime gameTime;
    EXPECT_EQ(gameTime.getElapsedGameTimeProperty().getTicksProperty(), 0);
    EXPECT_FALSE(gameTime.getIsRunningSlowlyProperty());
}

// Test setter with lvalue
TEST(GameTimeTest, SetElapsedGameTime_LValue) {
    GameTime gameTime;
    TimeSpan t(100);

    gameTime.setElapsedGameTimeProperty(t);
    EXPECT_EQ(gameTime.getElapsedGameTimeProperty().getTicksProperty(), 100);
}

// Test setter with rvalue
TEST(GameTimeTest, SetElapsedGameTime_RValue) {
    GameTime gameTime;
    gameTime.setElapsedGameTimeProperty(TimeSpan(200));

    EXPECT_EQ(gameTime.getElapsedGameTimeProperty().getTicksProperty(), 200);
}

TEST(GameTimeTest, GetterReturnsCorrectReference) {
    GameTime gameTime;
    TimeSpan t(150);

    gameTime.setElapsedGameTimeProperty(t);

    TimeSpan& elapsedTimeRef = gameTime.getElapsedGameTimeProperty();

    EXPECT_EQ(elapsedTimeRef.getTicksProperty(), 150);
    EXPECT_EQ(&elapsedTimeRef, &gameTime.getElapsedGameTimeProperty());
}

TEST(GameTimeTest, SetElapsedGameTime_RValue_NoCopy) {
    GameTime gameTime;

    TimeSpan::resetCopyCount();
    TimeSpan::resetMoveCount();

    gameTime.setElapsedGameTimeProperty(TimeSpan(300));

    EXPECT_EQ(TimeSpan::getCopyCount(), 0);
    EXPECT_GT(TimeSpan::getMoveCount(), 0);
}

TEST(GameTimeTest, GetterReturnsCorrectReference2) {
    GameTime gameTime;
    TimeSpan t(150);

    gameTime.setElapsedGameTimeProperty(t);

    TimeSpan& elapsedTimeRef = gameTime.getElapsedGameTimeProperty();

    EXPECT_EQ(elapsedTimeRef.getTicksProperty(), 150);
    EXPECT_EQ(&elapsedTimeRef, &gameTime.getElapsedGameTimeProperty());
}




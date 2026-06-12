// SPDX-License-Identifier: MS-PL
// #include <gtest/gtest.h>
// #include "Microsoft/Xna/Framework/Game.hpp"
// #include "System/TimeSpan.hpp"
//
// using namespace Microsoft::Xna::Framework;
// using namespace System;
//
// class MockGame : public Game {
// public:
//     MockGame() : Game() {}
// };
//
// TEST(GameCrashTest, TargetElapsedTimeNull) {
//     MockGame game;
//     // Set TargetElapsedTime_ to NULL to simulate the condition that might lead to the crash
//     game.setTargetElapsedTimeProperty(nullptr);
//
//     // This should trigger the crash if XNA5 is defined and it's called
// #ifdef XNA5
//     EXPECT_EXIT(game.getTargetMsFrameTimeProperty(), ::testing::KilledBySignal(SIGSEGV), ".*");
// #endif
// }

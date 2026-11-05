// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/Platform/CannedGamepad.hpp"
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadState.hpp"
#include <memory>
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Input;
namespace {
class Tq075GamePadIndexPlatformTest : public ::testing::Test {
protected:
    CNA::Platform::Testing::CannedGamepadPlatform platform;
    std::unique_ptr<CNA::Platform::Testing::ScopedCurrentPlatform> installed;
    void SetUp() override {
        installed = std::make_unique<CNA::Platform::Testing::ScopedCurrentPlatform>(platform);
    }
};
}
#include "System/InvalidOperationException.hpp"
#include <limits>

TEST_F(Tq075GamePadIndexPlatformTest, Tq075UndefinedPlayersRefuseEveryXnaGamePadCallBeforeRumble)
{
    // Refresh ThrottleDisconnectedRetries IL_0000..0009 bypasses cache for invalid indices;
    // native invalid-controller result reaches GetState IL_0035..003f,
    // GetCapabilities IL_0036..0040, SetVibration IL_0058..0062: InvalidOperationException.
    for (int raw : {-1, 4, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()}) {
        const auto player = static_cast<PlayerIndex>(raw);
        EXPECT_THROW(GamePad::GetState(player), System::InvalidOperationException);
        for (auto mode : {GamePadDeadZone::None, GamePadDeadZone::IndependentAxes, GamePadDeadZone::Circular})
            EXPECT_THROW(GamePad::GetState(player, mode), System::InvalidOperationException);
        EXPECT_THROW(GamePad::GetCapabilities(player), System::InvalidOperationException);
        EXPECT_THROW(GamePad::SetVibration(player, 0.25f, 0.75f), System::InvalidOperationException);
        EXPECT_EQ(platform.Canned().RumbleCalls(), 0);
    }
}

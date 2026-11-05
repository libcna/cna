// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/Platform/CannedGamepad.hpp"
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadState.hpp"
#include <memory>
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Input;
namespace {
class Tq075GamePadPacketPlatformTest : public ::testing::Test {
protected:
    CNA::Platform::Testing::CannedGamepadPlatform platform;
    std::unique_ptr<CNA::Platform::Testing::ScopedCurrentPlatform> installed;
    void SetUp() override {
        installed = std::make_unique<CNA::Platform::Testing::ScopedCurrentPlatform>(platform);
    }
};
}

TEST_F(Tq075GamePadPacketPlatformTest, Tq075PacketNumberPreservesNativeSignedBitsForEverySlot)
{
    // Refresh native-state ctor IL_001d..0024 copies the int32 packet verbatim;
    // get_PacketNumber IL_0000..0006 returns it, independent of dead-zone choice.
    const struct { std::uint32_t raw; int expected; } packets[] = {
        {0u, 0}, {1u, 1}, {0x7fffffffu, 2147483647},
        {0x80000000u, (-2147483647 - 1)}, {0xffffffffu, -1}};
    for (int slot = 0; slot < 4; ++slot) {
        const auto player = static_cast<PlayerIndex>(slot);
        for (const auto& packet : packets) {
            CNA::Platform::GamepadSnapshot snapshot;
            snapshot.connected = true;
            snapshot.packetNumber = packet.raw;
            platform.Canned().SetPendingSnapshot(slot, snapshot);
            platform.Canned().Update();
            for (auto mode : {GamePadDeadZone::None, GamePadDeadZone::IndependentAxes,
                              GamePadDeadZone::Circular})
                EXPECT_EQ(GamePad::GetState(player, mode).getPacketNumberProperty(), packet.expected);
            EXPECT_EQ(GamePad::GetState(player).getPacketNumberProperty(), packet.expected);
        }
    }
}

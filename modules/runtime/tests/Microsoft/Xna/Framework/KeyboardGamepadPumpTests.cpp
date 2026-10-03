// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/Platform/CannedKeyboard.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "RuntimePlatformTestSupport.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Input;
using CNA::Platform::KeyCode;

TEST(KeyboardGamepadPumpTest, GamePublishesBeforeUpdateAndClearsWhenKeyboardServiceDisappears)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow()) GTEST_SKIP();
    auto platform = std::make_unique<CNA::Platform::Testing::CannedKeyboardPlatform>();
    auto* fixture = platform.get();
    class PumpGame final : public Game
    {
    public:
        PumpGame(std::unique_ptr<CNA::Platform::IPlatform> platform,
                 CNA::Platform::Testing::CannedKeyboardPlatform* fixture)
            : Game(std::move(platform)), manager(this), fixture(fixture)
        {
            GamePad::setKeyboardEmulationEnabledEXT(true);
            setIsFixedTimeStepProperty(false);
        }
        ~PumpGame() override { GamePad::setKeyboardEmulationEnabledEXT(false); }
        GraphicsDeviceManager manager;
        CNA::Platform::Testing::CannedKeyboardPlatform* fixture;
        int phase = 0;
    protected:
        void Update(GameTime&) override
        {
            const auto state = GamePad::GetState(PlayerIndex::One);
            switch (phase++)
            {
                case 0:
                    EXPECT_TRUE(state.getIsConnectedProperty());
                    EXPECT_TRUE(state.IsButtonUp(Buttons::A));
                    fixture->Canned().SetPending({KeyCode::K, KeyCode::W}); break;
                case 1:
                    EXPECT_TRUE(state.IsButtonDown(Buttons::A));
                    EXPECT_FLOAT_EQ(state.getThumbSticksProperty().getLeftProperty().Y, 1);
                    EXPECT_TRUE(Keyboard::GetState().IsKeyDown(Keys::K));
                    fixture->SetKeyboardAvailable(false); break;
                case 2:
                    EXPECT_TRUE(state.IsButtonUp(Buttons::A));
                    EXPECT_EQ(state.getThumbSticksProperty().getLeftProperty(), Vector2::Zero);
                    Exit(); break;
                default: ADD_FAILURE() << "Pump did not exit"; Exit();
            }
        }
    } game(std::move(platform), fixture);
    game.Run();
    EXPECT_EQ(game.phase, 3);
}

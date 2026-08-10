// SPDX-License-Identifier: MS-PL
//
// input_smoke — a small XNA-like sample that reads Keyboard, Mouse, GamePad, Touch, and TextInput
// together in one Game update loop (Phase I10 task 854). It is a practical smoke test, not a unit
// test: each frame it polls every input device, twice a second it logs a compact status line, and
// it exits on Esc or after a bounded number of frames so it can run unattended in CI/headless
// checks. Override the frame budget with the env var CNA_INPUT_SMOKE_FRAMES (0 = run until Esc).

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Input/ButtonState.hpp"
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadState.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
#include "Microsoft/Xna/Framework/Input/MouseState.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/GestureType.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Input;
using namespace Microsoft::Xna::Framework::Input::Touch;

namespace
{
    class InputSmoke : public Game
    {
    public:
        InputSmoke()
        {
            getWindowProperty().setTitleProperty("CNA input_smoke");
            if (const char* f = std::getenv("CNA_INPUT_SMOKE_FRAMES"))
                maxFrames_ = std::atoi(f);
        }

    protected:
        void LoadContent() override
        {
            // TextInput: accumulate typed UTF-16 code units (Backspace pops).
            TextInputEXT::TextInput = [this](charcs c)
            {
                if (c == 8) { if (!text_.empty()) text_.pop_back(); }
                else if (text_.size() < 64) { text_.push_back(c); }
            };
            TextInputEXT::StartTextInput();
            TouchPanel::setEnabledGesturesProperty(GestureType::Tap | GestureType::FreeDrag);
        }

        void Update(GameTime& gameTime) override
        {
            (void)gameTime;

            // Read all five input surfaces in one loop, XNA-style.
            const KeyboardState   kb = Keyboard::GetState();
            const MouseState      ms = Mouse::GetState();
            const GamePadState    gp = GamePad::GetState(PlayerIndex::One);
            const TouchCollection tc = TouchPanel::GetState();

            if (kb.IsKeyDown(Keys::Escape))
            {
                Exit();
                return;
            }

            if (frame_ % 30 == 0) // ~twice per second at 60 fps
            {
                std::cout << "[input_smoke] frame=" << frame_
                          << " keysDown=" << kb.GetPressedKeys().size()
                          << " mouse=(" << ms.getXProperty() << ',' << ms.getYProperty() << ')'
                          << " Lbtn=" << (ms.getLeftButtonProperty() == ButtonState::Pressed ? 1 : 0)
                          << " wheel=" << ms.getScrollWheelValueProperty()
                          << " pad1=" << (gp.getIsConnectedProperty() ? "connected" : "disconnected")
                          << " touches=" << tc.getCountProperty()
                          << " textLen=" << text_.size()
                          << std::endl;
            }

            if (maxFrames_ > 0 && ++frame_ >= maxFrames_)
            {
                Exit();
            }
        }

        void Draw(const GameTime& gameTime) override
        {
            (void)gameTime;
            getGraphicsDeviceProperty().Clear(Color(15, 15, 20, 255));
        }

    private:
        std::u16string text_;
        int frame_     = 0;
        int maxFrames_ = 600; // ~10 s at 60 fps; unattended-safe default
    };
}

int main(int /*argc*/, char* /*argv*/[])
{
    InputSmoke game;
    game.Run();
    return 0;
}

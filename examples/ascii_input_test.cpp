// SPDX-License-Identifier: MS-PL
// plan_ascii.md Phase G6 (ASCII-50/51): Mouse/Keyboard/GamePad need zero new code against this
// renderer's real window (design decision 1/section 0.2) -- this test proves it directly, on top
// of the pre-existing MouseTest.SetPositionConvertsLogicalToWindowForLetterboxedRenderer /
// SetPositionHandlesLetterboxOffsetNotJustScale tests (tests/Microsoft/Xna/Framework/Input/
// MouseInputTests.cpp) already confirmed passing under CNA_GRAPHICS_RENDERER=ASCII as part of the
// full CnaTests regression.
//
// Check A -- a real window exists (GetWindowInternal() != nullptr) -- unlike HEADLESS/SOFTWARE,
//   this renderer needs one (design decision 1).
// Check B -- Mouse.GetState()/Keyboard.GetState()/GamePad.GetState() do not throw.
// Check C -- Mouse.SetPosition() to the logical center of the game's own virtual resolution,
//   then Mouse.GetState() reads back a position within 1px of that -- a real, ASCII-specific
//   round-trip through the actual coordinate-transform path (SdlInputBridge), not just relying
//   on the shared test suite happening to also cover this renderer.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"

#include "CNA/Internal/Renderers/Ascii/AsciiRenderer.hpp"

#include <cmath>
#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace Microsoft::Xna::Framework::Input;
using namespace CNA::Internal::Renderers::Ascii;

class AsciiInputTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<AsciiRenderer&>(dev.GetRenderer());

        check(renderer.GetWindowInternal() != nullptr, "A real window exists for the ASCII renderer");

        bool threw = false;
        try
        {
            (void)Mouse::GetState();
            (void)Keyboard::GetState();
            (void)GamePad::GetState(PlayerIndex::One);
        }
        catch (const std::exception&) { threw = true; }
        check(!threw, "Mouse/Keyboard/GamePad GetState() do not throw");

        const int cx = 40, cy = 24; // logical center of the 80x48 virtual resolution below
        Mouse::SetPosition(cx, cy);
        const MouseState state = Mouse::GetState();
        const bool withinOnePixel = std::abs(state.getXProperty() - cx) <= 1 && std::abs(state.getYProperty() - cy) <= 1;
        check(withinOnePixel, "Mouse.SetPosition()/GetState() round-trips through the real coordinate-transform path");

        std::printf("=== %d/%d PASS ===\n", passCount_, 3);
        result_ = (passCount_ == 3) ? 0 : 1;
        Exit();
    }

public:
    AsciiInputTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(80);
        gdm_->setPreferredBackBufferHeightProperty(48);
    }

    int getResult() const { return result_; }
};

int main()
{
    AsciiInputTest game;
    game.Run();
    return game.getResult();
}

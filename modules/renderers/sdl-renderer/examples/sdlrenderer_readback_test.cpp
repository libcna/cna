// SPDX-License-Identifier: MS-PL
// Task 666 (SDL_Renderer audit phase, foundational prerequisite): verify
// GraphicsDevice::GetBackBufferData actually reads real pixel content on SDL_Renderer.
//
// Before this fix, SdlRenderer never overrode IGraphicsRenderer::ReadBackbuffer at all --
// the shared default throws std::runtime_error("ReadBackbuffer: not implemented in this
// renderer"), meaning EVERY pixel-verification test written using this project's established
// methodology (SpriteBatch/BasicEffect draw + GetBackBufferData readback, used by every EasyGL/
// Vulkan/Bgfx test in this project) was structurally impossible on this renderer. This blocked the
// entire SDL_Renderer-specific pixel-test audit phase (plans/plan_graphics.md Tasks 666-861) before any
// of it could even start.
//
// Fixed via SDL_RenderReadPixels(). One real subtlety found while implementing it: that function
// operates in *physical* output coordinates, but every other renderer (and GetViewportSize()'s own
// contract) works in *logical* (virtual-resolution) coordinates. This project's default
// presentation mode, CnaPresentationMode::FixedHeightDynamicWidth, deliberately does NOT map
// logical pixels 1:1 to physical pixels (it derives a dynamic logical width from the window's
// real aspect ratio at a fixed logical height) -- exact-pixel readback cannot be trusted through
// that scaling. This test explicitly requests PresentationMode::NativeBackBuffer (maps to
// SDL_LOGICAL_PRESENTATION_DISABLED), which is the mode that actually gives 1:1 logical/physical
// correspondence, matching how a real test-writer needing exact pixel control would configure it.
// SdlRenderer::ReadBackbuffer itself now detects any non-1:1 scaling (letterbox/stretch
// still active) and throws clearly rather than silently returning wrong/aliased data.
//
// Draws a 20x20 red sprite at (10,10) over a green-cleared 64x64 backbuffer, then reads back one
// pixel from inside the sprite and one from the untouched background.
//
// Exit code 0 = both checks PASS, 1 = either FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kCanvasSize = 64;

class SdlRendererReadbackTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> sb_;
    std::unique_ptr<Texture2D> redTex_;
    bool done_   = false;
    int  result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(device);
        const std::vector<uint8_t> red = { 255, 0, 0, 255 };
        redTex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, red));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        device.Clear(Color(0, 255, 0, 255));
        sb_->Begin();
        sb_->Draw(*redTex_, Rectangle(10, 10, 20, 20), Rectangle(0, 0, 1, 1), Color::White);
        sb_->End();

        Color inside(0, 0, 0, 0);
        const Rectangle insideReg(15, 15, 1, 1);
        device.GetBackBufferData(&insideReg, &inside, 0, 1);

        Color outside(0, 0, 0, 0);
        const Rectangle outsideReg(0, 0, 1, 1);
        device.GetBackBufferData(&outsideReg, &outside, 0, 1);

        const bool insideOk  = (inside.getRProperty() > 200 && inside.getGProperty() < 50 && inside.getBProperty() < 50);
        const bool outsideOk = (outside.getGProperty() > 200 && outside.getRProperty() < 50 && outside.getBProperty() < 50);

        std::printf("[%s] inside sprite (15,15): got=(%d,%d,%d) want=(255,0,0)\n",
                    insideOk ? "PASS" : "FAIL",
                    inside.getRProperty(), inside.getGProperty(), inside.getBProperty());
        std::printf("[%s] outside sprite (0,0): got=(%d,%d,%d) want=(0,255,0)\n",
                    outsideOk ? "PASS" : "FAIL",
                    outside.getRProperty(), outside.getGProperty(), outside.getBProperty());

        result_ = (insideOk && outsideOk) ? 0 : 1;
        Exit();
    }

public:
    SdlRendererReadbackTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kCanvasSize);
        gdm_->setPreferredBackBufferHeightProperty(kCanvasSize);
        // See this file's header comment: NativeBackBuffer is the presentation mode that gives
        // exact 1:1 logical/physical pixel correspondence, required for exact-pixel readback.
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlRendererReadbackTest game;
    game.Run();
    return game.getResult();
}

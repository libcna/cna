// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: pixel-exact proof for real CnaPresentationMode::Overscan semantics --
// previously fell back to the virtual size verbatim (no actual scaling/cropping), a documented
// shared gap with EasyGL. Mirrors SdlGpuRenderer::ComputeLogicalViewport's algorithm (the
// established "this renderer actually does it right" reference elsewhere in this codebase).
//
// Uses a deliberately non-square window (128x64, resized in after startup -- see below) against a
// square virtual resolution (64x64): Overscan scale=max(128/64,64/64)=2, giving 128x128 content
// centred vertically -- since the window is only 64 tall, the ENTIRE window is covered (no bars
// anywhere) with the top/bottom 32 (of 128) logical rows cropped off-screen.
//
// PresentationMode is fixed for the lifetime of a process (GraphicsDevice::SetPresentationMode is
// private, reachable only via GraphicsDeviceManager::ApplyChanges(), which also re-asserts the
// window size from PresentationParameters -- so mode and an independently-resized window can't
// both be exercised through a single ApplyChanges() call). Set once via
// setPreferredPresentationModeProperty() before the game starts (matching how a real game would
// configure this at startup), then the window is resized directly via SDL (bypassing
// GraphicsDeviceManager entirely, so the virtual resolution stays untouched) -- see this file's
// Letterbox/Stretch twins for the other two modes.
//
// Check A -- left/right physical window edges AND the centre are all the drawn sprite's own
//   colour -- no bars anywhere, unlike Letterbox.
// Check B -- 30 frames of the whole scene render with no exception.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "CNA/Internal/Renderers/OpenGL2/OpenGL2Renderer.hpp"

#include "common/PixelTestGame.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::OpenGL2;

namespace
{
    constexpr int kTotalFrames = 30;
    constexpr int kWindowW = 128;
    constexpr int kWindowH = 64;
    constexpr int kVirtual = 64;

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    bool Matches(const Color& c, const Color& expected, int tol)
    {
        return CloseTo(c.getRProperty(), expected.getRProperty(), tol)
            && CloseTo(c.getGProperty(), expected.getGProperty(), tol)
            && CloseTo(c.getBProperty(), expected.getBProperty(), tol);
    }

    std::string ColorStr(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty())
             + "," + std::to_string(c.getBProperty()) + ")";
    }
}

class OpenGL2PresentationModeOverscanTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> greenTex_;

    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;

    void Check(const std::string& label, bool ok)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

    Color ReadPixel(int x, int y)
    {
        // GFX-165: GetBackBufferData validates its rectangle against PresentationParameters'
        // backbuffer size, which this test's own raw SDL_SetWindowSize deliberately does not
        // update -- XNA's backbuffer does not follow the OS window until a device reset. What
        // this test needs is the REAL framebuffer pixel, which is exactly the renderer's own
        // ReadBackbuffer (the same top-down row convention GetBackBufferData itself wraps).
        std::uint8_t px[4] = {0, 0, 0, 0};
        getGraphicsDeviceProperty().GetRenderer().ReadBackbuffer(x, y, 1, 1, px);
        return Color(px[0], px[1], px[2], px[3]);
    }

    void DrawFullCanvasSprite(GraphicsDevice& dev)
    {
        dev.Clear(Color::Black);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque);
        spriteBatch_->Draw(*greenTex_, Rectangle(0, 0, kVirtual, kVirtual), Rectangle(0, 0, 1, 1), Color::White);
        spriteBatch_->End();
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(dev);
        const Color green(0, 255, 0, 255);
        greenTex_ = std::make_unique<Texture2D>(dev, 1, 1);
        greenTex_->SetData(&green, 1);
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<OpenGL2Renderer&>(dev.GetRenderer());

        if (frame_ == 1)
        {
            SDL_SetWindowSize(reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty()), kWindowW, kWindowH);
            SDL_SyncWindow(reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty()));
            return;
        }

        DrawFullCanvasSprite(dev);

        if (frame_ == 2)
        {
            const Color left = ReadPixel(2, kWindowH / 2);
            const Color right = ReadPixel(kWindowW - 2, kWindowH / 2);
            const Color centre = ReadPixel(kWindowW / 2, kWindowH / 2);
            Check("Overscan: no bars, full coverage: left=" + ColorStr(left) + " right=" + ColorStr(right) + " centre=" + ColorStr(centre),
                  Matches(left, Color(0, 255, 0, 255), 10) && Matches(right, Color(0, 255, 0, 255), 10) && Matches(centre, Color(0, 255, 0, 255), 10));
        }

        if (frame_ == kTotalFrames)
        {
            Check(std::to_string(kTotalFrames) + " frames render with no exception", true);
            std::printf("=== %d/%d PASS ===\n", passCount_, 2);
            result_ = (passCount_ == 2) ? 0 : 1;
            Exit();
        }
    }

public:
    OpenGL2PresentationModeOverscanTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kVirtual);
        gdm_->setPreferredBackBufferHeightProperty(kVirtual);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::Overscan);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    OpenGL2PresentationModeOverscanTest game;
    game.Run();
    return game.getResult();
}

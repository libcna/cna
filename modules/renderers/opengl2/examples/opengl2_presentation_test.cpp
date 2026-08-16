// SPDX-License-Identifier: MS-PL
// plan_opengl2.md: proof that the default FixedHeightDynamicWidth presentation mode actually
// adapts to a resized window on the native OpenGL 2.1 graphics renderer, mirroring
// EasyGLRenderer::getLogicalSize's identical behavior.
//
// Check A -- GetViewportSize() initially reports the requested 320x240 back buffer.
// Check B -- after resizing the real SDL window to 640x240 (same height, double width) and
//   syncing, GetViewportSize() reports the ADAPTED width (640, not the original 320) while the
//   height stays fixed at 240 -- the defining behavior of FixedHeightDynamicWidth.
// Check C -- a SpriteBatch quad drawn to fill the new, wider logical width (0..640) actually
//   covers the real window's new right edge when read back -- proves the GL viewport itself was
//   re-issued to match, not just the C++-side GetViewportSize() bookkeeping.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "CNA/Internal/Renderers/OpenGL2/OpenGL2Renderer.hpp"

#include "common/PixelTestGame.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::OpenGL2;

namespace
{
    constexpr int kTotalFrames = 30;

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    bool Matches(const Color& c, const Color& expected, int tol)
    {
        return CloseTo(c.getRProperty(), expected.getRProperty(), tol)
            && CloseTo(c.getGProperty(), expected.getGProperty(), tol)
            && CloseTo(c.getBProperty(), expected.getBProperty(), tol);
    }
}

class OpenGL2PresentationTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> texRed_;

    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const std::string& label)
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

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(dev);
        texRed_ = std::make_unique<Texture2D>(
            Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{255, 0, 0, 255}));
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<OpenGL2Renderer&>(dev.GetRenderer());

        if (frame_ == 1)
        {
            int w = 0, h = 0;
            renderer.GetViewportSize(w, h);
            Check(w == 320 && h == 240, "GetViewportSize() initially reports 320x240");
        }
        else if (frame_ == 2)
        {
            SDL_SetWindowSize(reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty()), 640, 240);
            SDL_SyncWindow(reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty()));
        }
        else if (frame_ == 3)
        {
            int w = 0, h = 0;
            renderer.GetViewportSize(w, h);
            Check(w == 640 && h == 240,
                  "GetViewportSize() adapts width to 640 after resize, height stays 240 (got " +
                  std::to_string(w) + "x" + std::to_string(h) + ")");
        }

        dev.Clear(Color::Black);
        if (frame_ >= 3)
        {
            spriteBatch_->Begin();
            spriteBatch_->Draw(*texRed_, Rectangle(600, 100, 40, 40), Rectangle(0, 0, 1, 1), Color::White);
            spriteBatch_->End();
        }

        if (frame_ == 4)
        {
            const Color got = ReadPixel(620, 120);
            Check(Matches(got, Color::Red, 10),
                  "SpriteBatch draw near the new right edge (x=620 of 640) is visible -- the real GL viewport widened");
        }

        if (frame_ == kTotalFrames)
        {
            Check(true, std::to_string(kTotalFrames) + " frames render with no exception");
            std::printf("=== %d/%d PASS ===\n", passCount_, 4);
            result_ = (passCount_ == 4) ? 0 : 1;
            Exit();
        }
    }

public:
    OpenGL2PresentationTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(320);
        gdm_->setPreferredBackBufferHeightProperty(240);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    OpenGL2PresentationTest game;
    game.Run();
    return game.getResult();
}

// SPDX-License-Identifier: MS-PL
// WEBGPU-91/92: verify WebGPUGraphicsBackend::ReadBackbuffer()/GraphicsDevice::GetBackBufferData()
// actually return the pixels that were just drawn, including within a single Draw() call with no
// intervening real Present() -- WebGPU's swapchain-backed render target means a naive
// implementation can only ever observe the *previous* frame's content (nothing is readable until
// the frame has been submitted), so ReadBackbuffer() renders any pending Clear()/SpriteBatch work
// on demand before mapping the readback buffer, matching the Vulkan/Bgfx backends' own
// on-demand-submit readback semantics (see WebGPUGraphicsBackend::EnsureFrameRendered()).
//
// Check A -- Clear(Red) is visible via GetBackBufferData() in the same Draw() call.
// Check B -- a second Clear(Blue) right after replaces it (no stale/cached frame served).
// Check C -- a SpriteBatch-drawn solid green quad is visible inside its destination rectangle...
// Check D -- ...and the still-black background outside that rectangle is unaffected (partial
//   coverage, not an accidental full-screen overwrite).
// Check E -- a fully-transparent (alpha=0) sprite must not modify the destination at all.
// Check F -- a sourceRectangle selecting only the right half of a 2x1 (red|blue) texture must
//   sample blue, not red or an unpredictable blend -- proves source-rectangle cropping, not just
//   whole-texture sampling.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    bool colorNear(Color a, Color b, int tol = 8)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) <= tol &&
               std::abs(a.getGProperty() - b.getGProperty()) <= tol &&
               std::abs(a.getBProperty() - b.getBProperty()) <= tol;
    }

    Color readPixel(GraphicsDevice& dev, int x, int y)
    {
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }
}

class WebGpuClearReadbackTest : public Game
{
    static constexpr int kChecks = 6;

    std::unique_ptr<GraphicsDeviceManager> gdm_;
    SpriteBatch* spriteBatch_ = nullptr;
    Texture2D greenTex_;
    Texture2D redBlueTex_;
    bool done_ = false;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

protected:
    void LoadContent() override
    {
        spriteBatch_ = new SpriteBatch(getGraphicsDeviceProperty());
        // Color::Lime, not Color::Green (XNA's Color::Green is the darker (0,128,0) "web green").
        greenTex_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                                                 std::vector<std::uint8_t>{0, 255, 0, 255});
        // 2x1: left pixel red, right pixel blue -- for the source-rectangle crop check.
        redBlueTex_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 2, 1,
                                                   std::vector<std::uint8_t>{255, 0, 0, 255,
                                                                              0, 0, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        // Check A: Clear(Red) observed without an intervening real Present().
        dev.Clear(Color::Red);
        check(colorNear(readPixel(dev, kSize / 2, kSize / 2), Color::Red),
              "Clear(Red) visible via GetBackBufferData() in the same Draw() call");

        // Check B: a second Clear() right after replaces it -- no stale cached frame.
        dev.Clear(Color::Blue);
        check(colorNear(readPixel(dev, kSize / 2, kSize / 2), Color::Blue),
              "Clear(Blue) right after Clear(Red) is observed, not the stale Red frame");

        // Check C/D: Clear(Black), draw a green quad over the left half only, then verify both
        // sides.
        dev.Clear(Color::Black);
        const Rectangle destination(0, 0, kSize / 2, kSize);
        spriteBatch_->Begin();
        spriteBatch_->Draw(greenTex_, destination, Color::White);
        spriteBatch_->End();

        check(colorNear(readPixel(dev, kSize / 4, kSize / 2), Color::Lime),
              "SpriteBatch-drawn green (Lime) quad is visible inside its destination rectangle");
        check(colorNear(readPixel(dev, kSize - kSize / 4, kSize / 2), Color::Black),
              "background outside the quad's destination rectangle stays black");

        // Check E: alpha=0 sprite must not modify the destination.
        dev.Clear(Color::White);
        spriteBatch_->Begin();
        spriteBatch_->Draw(greenTex_, Rectangle(0, 0, kSize, kSize), Color(255, 255, 255, 0));
        spriteBatch_->End();
        check(colorNear(readPixel(dev, kSize / 2, kSize / 2), Color::White),
              "alpha=0 sprite leaves the destination unmodified");

        // Check F: sourceRectangle selecting only the right (blue) texel of a 2x1 texture.
        dev.Clear(Color::Black);
        const Rectangle rightTexel(1, 0, 1, 1);
        spriteBatch_->Begin();
        spriteBatch_->Draw(redBlueTex_, Rectangle(0, 0, kSize, kSize), rightTexel, Color::White);
        spriteBatch_->End();
        check(colorNear(readPixel(dev, kSize / 2, kSize / 2), Color::Blue),
              "sourceRectangle selecting the right texel samples blue, not red");

        std::printf("=== %d/%d PASS ===\n", passCount_, kChecks);
        result_ = (passCount_ == kChecks) ? 0 : 1;
        Exit();
    }

public:
    WebGpuClearReadbackTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    WebGpuClearReadbackTest game;
    game.Run();
    return game.getResult();
}

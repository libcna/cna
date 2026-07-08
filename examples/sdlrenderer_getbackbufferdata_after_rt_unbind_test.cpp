// SPDX-License-Identifier: MS-PL
// Task 707: Verify GetBackBufferData reads correct pixels after SetRenderTarget(nullptr)
// restores the backbuffer on SDL_Renderer.
//
// SdlGraphicsBackend::ReadBackbuffer branches on SDL_GetRenderTarget(renderer) == nullptr to
// decide whether to map through SDL_GetRenderLogicalPresentationRect() (the real backbuffer path)
// or treat coordinates as addressing a bound render target's own texture space directly -- so its
// correctness after unbinding genuinely depends on GraphicsDevice::SetRenderTarget(nullptr)
// correctly restoring both the SDL render target AND (via ResetViewportAndScissorForRenderTarget)
// the Viewport back to the backbuffer's own dimensions.
//
// Tasks 704-706's tests all used explicit small Rectangle regions for GetBackBufferData; this
// test instead specifically exercises the NO-RECT, "whole backbuffer" overload
// (GetBackBufferData(Color*, int)), which internally resolves its region from
// backend_->GetViewportSize() -- deliberately binding a render target of a DIFFERENT, SMALLER
// size than the backbuffer in between, so that if the viewport/size resolution were left stale
// (still reflecting the smaller render target instead of reverting to the backbuffer), the
// full-backbuffer readback would only fill part of the buffer, leaving the rest at whatever the
// caller's buffer was pre-filled with instead of the backbuffer's own actual Magenta content.
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels operates
// in physical output coordinates, while this backend's default presentation mode
// (FixedHeightDynamicWidth) does not map logical pixels 1:1 to physical ones.
//
// Exit code 0 = all checks PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kMagenta(255, 0, 255, 255);

    bool IsMagenta(const Color& c)
    {
        return c.getRProperty() >= 240 && c.getGProperty() <= 15 && c.getBProperty() >= 240;
    }
}

class SdlGetBackBufferDataAfterRtUnbindTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

    bool done_   = false;
    int  result_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) result_ = 1;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        const auto& vp = dev.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();
        const int pixelCount = W * H;

        dev.setBlendStateProperty(BlendState::Opaque);
        dev.Clear(kMagenta);

        // Sanity control: full no-rect readback BEFORE any render-target activity.
        std::vector<Color> before(pixelCount, Color(0, 0, 0, 0));
        dev.GetBackBufferData(before.data(), pixelCount);
        bool allMagentaBefore = true;
        for (const auto& c : before) if (!IsMagenta(c)) { allMagentaBefore = false; break; }
        check(allMagentaBefore, "Control: full-backbuffer no-rect readback is all Magenta before any RenderTarget2D activity");

        // Bind a render target SMALLER than the backbuffer, draw into it, unbind.
        RenderTarget2D rt(dev, 4, 4);
        dev.SetRenderTarget(&rt);
        dev.Clear(Color(0, 255, 255, 255)); // Cyan -- deliberately different from Magenta.
        dev.SetRenderTarget(nullptr);

        check(dev.getViewportProperty().getWidthProperty() == W && dev.getViewportProperty().getHeightProperty() == H,
              "Viewport reverts to the backbuffer's own W/H after unbinding a smaller render target");

        // Full no-rect readback AFTER the bind/unbind cycle -- every element, including the tail,
        // must still be the backbuffer's own Magenta, not left over from the 4x4 render target.
        std::vector<Color> after(pixelCount, Color(0, 0, 0, 0));
        dev.GetBackBufferData(after.data(), pixelCount);
        bool allMagentaAfter = true;
        int firstBadIndex = -1;
        for (int i = 0; i < pixelCount; ++i)
        {
            if (!IsMagenta(after[static_cast<std::size_t>(i)]))
            {
                allMagentaAfter = false;
                firstBadIndex = i;
                break;
            }
        }
        if (!allMagentaAfter)
        {
            const Color& bad = after[static_cast<std::size_t>(firstBadIndex)];
            std::printf("       first bad index=%d got=(%d,%d,%d)\n", firstBadIndex,
                        bad.getRProperty(), bad.getGProperty(), bad.getBProperty());
        }
        check(allMagentaAfter,
              "Full-backbuffer no-rect readback is still all Magenta after binding+drawing into a smaller RenderTarget2D and unbinding");

        Exit();
    }

public:
    SdlGetBackBufferDataAfterRtUnbindTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(16);
        gdm_->setPreferredBackBufferHeightProperty(16);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlGetBackBufferDataAfterRtUnbindTest game;
    game.Run();
    return game.getResult();
}

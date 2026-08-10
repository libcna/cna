// SPDX-License-Identifier: MS-PL
// REMED-GFX-081 (cross-renderer): SpriteBatch.Begin must APPLY its RasterizerState argument on every
// renderer. This is a shared-layer test -- the fix lives in SpriteBatch.cpp (setRasterizerStateProperty
// in Begin), not in any renderer -- so the SAME source is registered against multiple renderers to
// prove the shared change works across very different architectures (deferred BatchSnapshot on
// Vulkan/SdlGpu, view-state on Bgfx, immediate on D3D11/EasyGL). The scissor rectangle itself is
// already correct per renderer (GFX-013 Vulkan / GFX-068 SdlGpu / GFX-066 Bgfx / native D3D11); the
// only thing under test here is whether the RasterizerState reaches it THROUGH Begin.
//
// Everything renders into an off-screen 64x64 RenderTarget2D and is read back with GetData(), the
// portable path that works even on renderers without GetBackBufferData (SdlGpu). Scene: bind the RT,
// clear GREEN, set ScissorRectangle = top-left 32x32, then a full-target red sprite drawn via
// SpriteBatch with the scissor RasterizerState supplied ONLY through Begin (never assigned to
// GraphicsDevice.RasterizerState). Probe the top-left quadrant centre (16,16) and the bottom-right
// quadrant centre (48,48).
//   Begin(rasterizerState = ScissorTestEnable:true):  TL=RED, BR=GREEN (clipped).
//             Pre-fix (argument dropped): BR=RED too (no clip) -> FAIL.
//   Begin(rasterizerState = ScissorTestEnable:false): TL=RED, BR=RED  (unclipped control).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    bool isRed(const Color& c)   { return c.getRProperty() > 200 && c.getGProperty() < 80 && c.getBProperty() < 80; }
    bool isGreen(const Color& c) { return c.getGProperty() > 200 && c.getRProperty() < 80 && c.getBProperty() < 80; }
}

class SpriteBatchBeginRasterizerStateScissorTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<RenderTarget2D> rt_;
    Texture2D whiteTex_;
    bool done_ = false;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const char* label, const Color& got, const char* expected)
    {
        std::printf("[%s] %s: (%d,%d,%d) expected %s\n", ok ? "PASS" : "FAIL", label,
            got.getRProperty(), got.getGProperty(), got.getBProperty(), expected);
        if (ok) ++pass_; else ++fail_;
    }

    // Render one scene into the RT: clear green, scissor to top-left 32x32, full-target red sprite
    // with the scissor state supplied ONLY through Begin. Returns the read-back RT pixels.
    std::vector<Color> renderScene(GraphicsDevice& dev, bool scissorEnable)
    {
        dev.SetRenderTarget(rt_.get());                 // resets viewport+scissor to full RT
        dev.setScissorRectangleProperty(Rectangle(0, 0, kSize / 2, kSize / 2));   // top-left 32x32
        dev.Clear(Color(0, 255, 0, 255));               // GREEN
        {
            RasterizerState rs;
            rs.setScissorTestEnableProperty(scissorEnable);
            SpriteBatch sb(dev);
            SamplerState point = SamplerState::PointClamp;
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &rs);
            sb.Draw(whiteTex_, Rectangle(0, 0, kSize, kSize), Rectangle(0, 0, 1, 1), Color(255, 0, 0, 255));
            sb.End();
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));   // flushes the deferred batch

        std::vector<Color> pix(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
        rt_->GetData(0, nullptr, pix.data(), 0, static_cast<int>(pix.size()));
        return pix;
    }

    static const Color& at(const std::vector<Color>& pix, int x, int y)
    { return pix[static_cast<std::size_t>(y) * kSize + x]; }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        whiteTex_ = Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255});
        rt_ = std::make_unique<RenderTarget2D>(dev, kSize, kSize, false, SurfaceFormat::Color,
                                               DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();
        const int tlX = kSize / 4, tlY = kSize / 4;           // (16,16) inside scissor
        const int brX = kSize * 3 / 4, brY = kSize * 3 / 4;   // (48,48) outside scissor

        const std::vector<Color> on = renderScene(dev, /*scissorEnable=*/true);
        check(isRed(at(on, tlX, tlY)),   "Begin(scissor=true): inside  (16,16)", at(on, tlX, tlY), "RED");
        check(isGreen(at(on, brX, brY)), "Begin(scissor=true): outside (48,48)", at(on, brX, brY), "GREEN (clipped)");

        const std::vector<Color> off = renderScene(dev, /*scissorEnable=*/false);
        check(isRed(at(off, tlX, tlY)), "Begin(scissor=false): inside  (16,16)", at(off, tlX, tlY), "RED");
        check(isRed(at(off, brX, brY)), "Begin(scissor=false): outside (48,48)", at(off, brX, brY), "RED (unclipped)");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    SpriteBatchBeginRasterizerStateScissorTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    SpriteBatchBeginRasterizerStateScissorTest game;
    game.Run();
    return game.getResult();
}

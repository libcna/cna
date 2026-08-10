// SPDX-License-Identifier: MS-PL
// REMED-GFX-081 (Bgfx control): SpriteBatch.Begin must APPLY its RasterizerState argument. The fix
// is shared (SpriteBatch.cpp), but Bgfx gets its own harness because its single-frame RenderTarget2D
// GetData returns black -- the established Bgfx pattern (bgfx_rendertarget_scissor_test) renders to
// the backbuffer and reads the WHOLE backbuffer in one GetBackBufferData call inside a short frame
// loop (Bgfx first-read-per-frame quirk). GFX-066 proved bgfx::setScissor clips when
// ScissorTestEnable is truly active, so this is a clean discriminator that the RasterizerState
// reaches Bgfx THROUGH Begin (never via a direct GraphicsDevice.RasterizerState assignment).
//
// Scene (64x64 backbuffer): clear GREEN, ScissorRectangle = top-left 32x32, full-target red sprite
// via SpriteBatch with the scissor RasterizerState supplied ONLY through Begin. Probe (16,16) inside
// and (48,48) outside.
//   Begin(scissor=true):  inside RED, outside GREEN (clipped).  Pre-fix: outside RED (no clip).
//   Begin(scissor=false): inside RED, outside RED   (unclipped control).
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
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
    bool isBlack(const Color& c) { return c.getRProperty() < 30 && c.getGProperty() < 30 && c.getBProperty() < 30; }
}

class BgfxSpriteBatchBeginRasterizerStateTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D whiteTex_;
    int frame_ = 0;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const char* label, const Color& got, const char* expected)
    {
        std::printf("[%s] %s: (%d,%d,%d) expected %s\n", ok ? "PASS" : "FAIL", label,
            got.getRProperty(), got.getGProperty(), got.getBProperty(), expected);
        if (ok) ++pass_; else ++fail_;
    }

    // Render the scene once and read the whole backbuffer (Bgfx: one read per frame, first-read wins).
    void renderAndRead(GraphicsDevice& dev, bool scissorEnable, std::vector<Color>& buf)
    {
        dev.setScissorRectangleProperty(Rectangle(0, 0, kSize / 2, kSize / 2));   // top-left 32x32
        dev.Clear(Color(0, 255, 0, 255));   // GREEN
        RasterizerState rs;
        rs.setScissorTestEnableProperty(scissorEnable);
        SpriteBatch sb(dev);
        SamplerState point = SamplerState::PointClamp;
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &rs);
        sb.Draw(whiteTex_, Rectangle(0, 0, kSize, kSize), Rectangle(0, 0, 1, 1), Color(255, 0, 0, 255));
        sb.End();
        const Rectangle whole(0, 0, kSize, kSize);
        dev.GetBackBufferData(&whole, buf.data(), 0, kSize * kSize);
    }

    static const Color& at(const std::vector<Color>& buf, int x, int y)
    { return buf[static_cast<std::size_t>(y) * kSize + x]; }

    // Repeat until a fully-settled frame (both probes non-black), tolerating Bgfx's per-frame
    // present/readback latency, then check.
    void runCase(GraphicsDevice& dev, bool scissorEnable, const char* insideLbl, const char* outsideLbl,
                 bool outsideExpectRed)
    {
        const int tlX = kSize / 4, tlY = kSize / 4, brX = kSize * 3 / 4, brY = kSize * 3 / 4;
        std::vector<Color> buf(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
        for (int i = 0; i < 20; ++i)
        {
            renderAndRead(dev, scissorEnable, buf);
            if (!isBlack(at(buf, tlX, tlY)) && !isBlack(at(buf, brX, brY))) break;   // settled frame
        }
        check(isRed(at(buf, tlX, tlY)), insideLbl, at(buf, tlX, tlY), "RED");
        if (outsideExpectRed)
            check(isRed(at(buf, brX, brY)), outsideLbl, at(buf, brX, brY), "RED (unclipped)");
        else
            check(isGreen(at(buf, brX, brY)), outsideLbl, at(buf, brX, brY), "GREEN (clipped)");
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        whiteTex_ = Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        if (frame_ == 0)
        {
            runCase(dev, /*scissorEnable=*/true,
                    "Begin(scissor=true): inside  (16,16)", "Begin(scissor=true): outside (48,48)", false);
            ++frame_;
            return;
        }
        runCase(dev, /*scissorEnable=*/false,
                "Begin(scissor=false): inside  (16,16)", "Begin(scissor=false): outside (48,48)", true);
        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BgfxSpriteBatchBeginRasterizerStateTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BgfxSpriteBatchBeginRasterizerStateTest game;
    game.Run();
    return game.getResult();
}

// SPDX-License-Identifier: MS-PL
// REMED-GFX-067: a RenderTarget2D sampled back through SpriteBatch must read UPRIGHT (XNA top-left
// origin), NOT vertically mirrored, on the Bgfx renderer — for EVERY render-target size.
//
// Root cause: on bgfx renderers whose caps->originBottomLeft is true (OpenGL/GLES/WebGL) a render
// target's color attachment stores its texel memory BOTTOM-UP, so sampling it with the ordinary
// top-down V convention yields a vertically-mirrored image. An ordinary (SetData-uploaded)
// Texture2D is top-down and unaffected; the backbuffer readback already compensates its own
// bottom-up storage via the screenshot's _yflip. bgfx Vulkan/D3D/Metal (originBottomLeft == false)
// store the attachment top-down and never mirrored. The fix flips the sampled V of a render-target
// source in BgfxRenderer::SubmitSprite when caps->originBottomLeft is set.
//
// The finding was originally recorded as "some RT sizes read Y-mirrored, others upright" — a FALSE
// size dependency. It is uniform across all sizes; the earlier flaky, size-correlated observations
// were artifacts of (a) Y-uniform / Y-symmetric / viewport-position-only test content that could
// not observe a vertical mirror, and (b) the bgfx first-read-per-frame screenshot pipeline latency
// combined with retry loops that break on the first upright frame. This test uses an unambiguous
// four-quadrant pattern that DOES observe a vertical mirror, over a matrix of sizes, plus a
// multiple-RT-in-one-frame sequence (the scenario earlier RT tests routed around).
//
//   Four-quadrant pattern rendered into the RT (identity MVP; NDC y=+1 is the image TOP):
//       top-left = RED     top-right = GREEN
//       bot-left = BLUE    bot-right = YELLOW
//
// Upright readback => TL=RED, TR=GREEN, BL=BLUE, BR=YELLOW. A vertical mirror swaps top<->bottom
// (TL=BLUE, BL=RED, ...); a horizontal mirror swaps left<->right; 180 swaps both.
//
// Methodology mirrors bgfx_rendertarget_viewport_test.cpp (the proven Bgfx RT-sample path): render
// into the RT, unbind, SpriteBatch-blit 1:1 onto the backbuffer, read the whole region in ONE
// GetBackBufferData call (bgfx reflects only the first read per rendered frame), retrying until the
// frame is fresh.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kBBW = 64;   // backbuffer >= every RT in the matrix (matches the proven
static constexpr int kBBH = 64;   // bgfx_rendertarget_viewport_test config for this display sandbox)

class BgfxRenderTargetOrientationTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    int pass_ = 0;
    int fail_ = 0;

    static const Color RED, GREEN, BLUE, YELLOW;

    static bool is(const Color& c, const Color& want)
    {
        auto near = [](int a, int b){ return a - b <= 40 && b - a <= 40; };
        return near(c.getRProperty(), want.getRProperty())
            && near(c.getGProperty(), want.getGProperty())
            && near(c.getBProperty(), want.getBProperty());
    }
    static bool isFresh(const Color& c)
    {
        return c.getRProperty() > 60 || c.getGProperty() > 60 || c.getBProperty() > 60;
    }

    void check(bool ok, const char* label, const Color& got, const char* expected)
    {
        std::printf("[%s] %s: (%d,%d,%d) expected %s\n", ok ? "PASS" : "FAIL", label,
            got.getRProperty(), got.getGProperty(), got.getBProperty(), expected);
        if (ok) ++pass_; else ++fail_;
    }

    // Draw the four-quadrant pattern directly in NDC (identity MVP): NDC y=+1 is the image top.
    static void drawQuadrants(GraphicsDevice& dev)
    {
        auto quad = [](float x0, float y0, float x1, float y1, const Color& c, VertexPositionColor* o){
            o[0] = { Vector3(x0, y1, 0.f), c }; o[1] = { Vector3(x0, y0, 0.f), c };
            o[2] = { Vector3(x1, y0, 0.f), c }; o[3] = { Vector3(x0, y1, 0.f), c };
            o[4] = { Vector3(x1, y0, 0.f), c }; o[5] = { Vector3(x1, y1, 0.f), c };
        };
        VertexPositionColor v[24];
        quad(-1.f, 0.f, 0.f, 1.f, RED,    v +  0);  quad(0.f, 0.f, 1.f, 1.f, GREEN,  v +  6);
        quad(-1.f,-1.f, 0.f, 0.f, BLUE,   v + 12);  quad(0.f,-1.f, 1.f, 0.f, YELLOW, v + 18);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, v, 0, 8);
    }

    // Render the four-quadrant pattern into rt, unbind, blit 1:1 onto the backbuffer, read the whole
    // region in one call, retrying until fresh (bgfx first-read-per-frame quirk).
    std::vector<Color> renderRTAndRead(GraphicsDevice& dev, RenderTarget2D& rt, int w, int h)
    {
        std::vector<Color> buf(static_cast<std::size_t>(kBBW) * kBBH, Color(0, 0, 0, 0));
        const Rectangle whole(0, 0, kBBW, kBBH);
        SamplerState point = SamplerState::PointClamp;

        for (int i = 0; i < 24; ++i)
        {
            BasicEffect fx(dev);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.VertexColorEnabled = true;

            dev.SetRenderTarget(&rt);
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            fx.Apply();
            drawQuadrants(dev);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            dev.Clear(Color(0, 0, 0, 255));
            dev.setRasterizerStateProperty(RasterizerState());
            dev.setBlendStateProperty(BlendState::Opaque);
            sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            sb_->Draw(rt, Rectangle(0, 0, w, h), Rectangle(0, 0, w, h), Color::White);
            sb_->End();

            dev.GetBackBufferData(&whole, buf.data(), 0, kBBW * kBBH);
            if (isFresh(at(buf, w / 4, h / 4)) && isFresh(at(buf, 3 * w / 4, 3 * h / 4)))
                break; // both far corners fresh => a full, settled frame
        }
        return buf;
    }

    static Color at(const std::vector<Color>& buf, int x, int y)
    {
        return buf[static_cast<std::size_t>(y) * kBBW + x];
    }

    // Assert an upright four-quadrant readback for an RT/texture of size w x h blitted to (0,0).
    void checkUpright(const std::vector<Color>& b, int w, int h, const char* tag)
    {
        const int xL = w / 4, xR = 3 * w / 4, yT = h / 4, yB = 3 * h / 4;
        char lbl[96];
        std::snprintf(lbl, sizeof(lbl), "%s %dx%d TL", tag, w, h);
        check(is(at(b, xL, yT), RED),    lbl, at(b, xL, yT), "RED");
        std::snprintf(lbl, sizeof(lbl), "%s %dx%d TR", tag, w, h);
        check(is(at(b, xR, yT), GREEN),  lbl, at(b, xR, yT), "GREEN");
        std::snprintf(lbl, sizeof(lbl), "%s %dx%d BL", tag, w, h);
        check(is(at(b, xL, yB), BLUE),   lbl, at(b, xL, yB), "BLUE");
        std::snprintf(lbl, sizeof(lbl), "%s %dx%d BR", tag, w, h);
        check(is(at(b, xR, yB), YELLOW), lbl, at(b, xR, yB), "YELLOW");
        // Strict vertical-orientation guard well inside the top and bottom eighths (away from the
        // exact edge row, which can be an uncovered half-texel on some renderers): the top band must
        // be in the RED|GREEN half, the bottom band in the BLUE|YELLOW half. A vertical mirror flips it.
        const Color top = at(b, w / 2 - 1, h / 8);
        const Color bot = at(b, w / 2 - 1, 7 * h / 8);
        std::snprintf(lbl, sizeof(lbl), "%s %dx%d topBand is R|G", tag, w, h);
        check(is(top, RED) || is(top, GREEN), lbl, top, "RED or GREEN");
        std::snprintf(lbl, sizeof(lbl), "%s %dx%d botBand is B|Y", tag, w, h);
        check(is(bot, BLUE) || is(bot, YELLOW), lbl, bot, "BLUE or YELLOW");
    }

    std::unique_ptr<Texture2D> buildQuadTexture(GraphicsDevice& dev, int w, int h)
    {
        auto tex = std::make_unique<Texture2D>(dev, w, h);
        std::vector<Color> px(static_cast<std::size_t>(w) * h, Color(0, 0, 0, 255));
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const bool top = y < h / 2, left = x < w / 2;
                px[static_cast<std::size_t>(y) * w + x] = top ? (left ? RED : GREEN)
                                                              : (left ? BLUE : YELLOW);
            }
        tex->SetData(px.data(), static_cast<int>(px.size()));
        return tex;
    }

    std::vector<Color> blitTextureAndRead(GraphicsDevice& dev, const Texture2D& tex, int w, int h)
    {
        std::vector<Color> buf(static_cast<std::size_t>(kBBW) * kBBH, Color(0, 0, 0, 0));
        const Rectangle whole(0, 0, kBBW, kBBH);
        SamplerState point = SamplerState::PointClamp;
        for (int i = 0; i < 24; ++i)
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.setRasterizerStateProperty(RasterizerState());
            dev.setBlendStateProperty(BlendState::Opaque);
            sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            sb_->Draw(tex, Rectangle(0, 0, w, h), Rectangle(0, 0, w, h), Color::White);
            sb_->End();
            dev.GetBackBufferData(&whole, buf.data(), 0, kBBW * kBBH);
            if (isFresh(at(buf, w / 4, h / 4)) && isFresh(at(buf, 3 * w / 4, 3 * h / 4)))
                break;
        }
        return buf;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        sb_ = std::make_unique<SpriteBatch>(getGraphicsDeviceProperty());
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        // A — size-independence: the same four-quadrant pattern reads upright at every size,
        // including the historically "flipped" (48x32, 40x48, 64x56) and "upright" (64x48) sizes.
        struct Sz { int w, h; };
        const Sz sizes[] = { {64, 48}, {48, 32}, {40, 48}, {64, 56}, {32, 32}, {56, 40}, {48, 56} };
        for (const Sz& s : sizes)
        {
            RenderTarget2D rt(dev, s.w, s.h);
            checkUpright(renderRTAndRead(dev, rt, s.w, s.h), s.w, s.h, "RT");
        }

        // B — ordinary Texture2D control: an uploaded (top-down) texture must ALSO read upright
        // (the fix must not flip ordinary textures, only render-target sources).
        {
            auto tex = buildQuadTexture(dev, 64, 48);
            checkUpright(blitTextureAndRead(dev, *tex, 64, 48), 64, 48, "Tex2D");
        }

        // C — multiple render targets sampled within one frame sequence (the scenario earlier Bgfx
        // RT tests routed around as GFX-067 "contamination"): each must independently read upright.
        {
            RenderTarget2D a(dev, 64, 48);
            RenderTarget2D b(dev, 48, 32);
            checkUpright(renderRTAndRead(dev, a, 64, 48), 64, 48, "Seq A0");
            checkUpright(renderRTAndRead(dev, b, 48, 32), 48, 32, "Seq B0");
            checkUpright(renderRTAndRead(dev, a, 64, 48), 64, 48, "Seq A1");
            checkUpright(renderRTAndRead(dev, b, 48, 32), 48, 32, "Seq B1");
        }

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BgfxRenderTargetOrientationTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

const Color BgfxRenderTargetOrientationTest::RED(255, 0, 0, 255);
const Color BgfxRenderTargetOrientationTest::GREEN(0, 255, 0, 255);
const Color BgfxRenderTargetOrientationTest::BLUE(0, 0, 255, 255);
const Color BgfxRenderTargetOrientationTest::YELLOW(255, 255, 0, 255);

int main()
{
    BgfxRenderTargetOrientationTest game;
    game.Run();
    return game.getResult();
}

// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-078: a RenderTarget2D used as a GENERIC 3D-EFFECT texture (effect.Texture = rt) must
// (1) bind its real handle without the former UB static_cast<const BgfxTextureRenderer&> — a
// RenderTarget2D's renderer is BgfxRenderTargetRenderer, an unrelated sibling of BgfxTextureRenderer —
// and (2) sample UPRIGHT (XNA top-left convention) on BOTH bgfx renderers, exactly as REMED-GFX-067
// established for SpriteBatch. The generic-effect path receives mesh UVs (not SpriteBatch-generated
// UVs), so the originBottomLeft (OpenGL/GLES/WebGL) bottom-up-FBO compensation is applied in the
// fragment shaders via u_rtFlipV — per sampler slot, so a Texture2D on one slot and a RenderTarget2D
// on another (DualTextureEffect) are each individually upright.
//
// Method — same proven readback as the GFX-067 orientation test, but the RT is sampled through a
// 3D effect textured quad instead of SpriteBatch:
//   1. Render a four-quadrant pattern into the RT (identity MVP; NDC y=+1 is the image TOP):
//        top-left = RED    top-right = GREEN
//        bottom-left = BLUE  bottom-right = YELLOW
//   2. Unbind the RT, then draw a full-screen textured quad to the backbuffer that samples it with
//      the standard XNA texture-on-quad UV map (NDC top-left -> UV (0,0)).
//   3. GetBackBufferData (bgfx reflects only the first read per rendered frame, so retry until the
//      frame has settled), classify the four backbuffer quadrants by hue and assert upright.
// An ordinary (SetData, top-down) Texture2D control sampled through the identical quad must ALSO
// read upright — the fix must not flip ordinary textures, only render-target color sources. Because
// the control and the RT use the same quad/UVs, an upright control anchors the expected orientation
// and any RT mirror is a real defect, not a test-UV mistake.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kBBW = 64;   // full-screen sampling quad fills the backbuffer; the RT is the
static constexpr int kBBH = 64;   // texture source (matches the proven GFX-067 sandbox config)

class BgfxRenderTargetEffectTextureTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    static const Color RED, GREEN, BLUE, YELLOW;

    // Hue classifier robust to brightness/saturation differences between the pure-textured path
    // (texture * white diffuse) and DualTextureEffect (base.rgb*2 * second sample): a quadrant is
    // classified by which channels are "high" vs "low", not by exact values.
    enum Hue { QRED, QGREEN, QBLUE, QYELLOW, QOTHER };
    static Hue classify(const Color& c)
    {
        const bool r = c.getRProperty() > 120;
        const bool g = c.getGProperty() > 120;
        const bool b = c.getBProperty() > 120;
        if ( r && !g && !b) return QRED;
        if (!r &&  g && !b) return QGREEN;
        if (!r && !g &&  b) return QBLUE;
        if ( r &&  g && !b) return QYELLOW;
        return QOTHER;
    }
    static const char* hueName(Hue h)
    {
        switch (h) { case QRED: return "RED"; case QGREEN: return "GREEN"; case QBLUE: return "BLUE";
                     case QYELLOW: return "YELLOW"; default: return "OTHER"; }
    }
    static bool isFresh(const Color& c)
    {
        return c.getRProperty() > 60 || c.getGProperty() > 60 || c.getBProperty() > 60;
    }

    void check(bool ok, const char* label, Hue got, Hue expected)
    {
        std::printf("[%s] %s: %s expected %s\n", ok ? "PASS" : "FAIL", label,
                    hueName(got), hueName(expected));
        if (ok) ++pass_; else ++fail_;
    }

    static Color at(const std::vector<Color>& buf, int x, int y)
    {
        return buf[static_cast<std::size_t>(y) * kBBW + x];
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

    // Render the four-quadrant pattern into rt with a BasicEffect (VertexColorEnabled), then unbind.
    void renderFourQuadrantsIntoRT(GraphicsDevice& dev, RenderTarget2D& rt)
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
    }

    // Full-screen quad in NDC with the standard XNA texture UV map: NDC top-left -> UV (0,0), so a
    // correctly (upright) sampled source appears upright in the backbuffer.
    static void fillSamplingQuad(VertexPositionTexture* q)
    {
        const Vector3 tl(-1.f, 1.f, 0.f), bl(-1.f, -1.f, 0.f), br(1.f, -1.f, 0.f), tr(1.f, 1.f, 0.f);
        q[0] = { tl, Vector2(0.f, 0.f) }; q[1] = { bl, Vector2(0.f, 1.f) }; q[2] = { br, Vector2(1.f, 1.f) };
        q[3] = { tl, Vector2(0.f, 0.f) }; q[4] = { br, Vector2(1.f, 1.f) }; q[5] = { tr, Vector2(1.f, 0.f) };
    }

    // Sample `tex` (a Texture2D or a RenderTarget2D) through BasicEffect's textured path (exercises
    // fs_textured3d via the former UB cast site) onto the backbuffer, and read the whole frame.
    std::vector<Color> sampleThroughBasicEffect(GraphicsDevice& dev, Texture2D& tex, bool reRenderRT,
                                                RenderTarget2D* rtSource)
    {
        std::vector<Color> buf(static_cast<std::size_t>(kBBW) * kBBH, Color(0, 0, 0, 0));
        const Rectangle whole(0, 0, kBBW, kBBH);
        VertexPositionTexture q[6];
        fillSamplingQuad(q);
        for (int i = 0; i < 24; ++i)
        {
            if (reRenderRT && rtSource) renderFourQuadrantsIntoRT(dev, *rtSource);

            BasicEffect fx(dev);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(&tex);

            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);

            dev.GetBackBufferData(&whole, buf.data(), 0, kBBW * kBBH);
            if (isFresh(at(buf, kBBW / 4, kBBH / 4)) && isFresh(at(buf, 3 * kBBW / 4, 3 * kBBH / 4)))
                break;
        }
        return buf;
    }

    // Sample two sources through DualTextureEffect (exercises fs_dual_texture3d — both slot 0 and
    // slot 1 went through the former UB cast). texA feeds slot 0, texB feeds slot 1.
    std::vector<Color> sampleThroughDualTexture(GraphicsDevice& dev, Texture2D& texA, Texture2D& texB,
                                                RenderTarget2D* rtA, RenderTarget2D* rtB)
    {
        std::vector<Color> buf(static_cast<std::size_t>(kBBW) * kBBH, Color(0, 0, 0, 0));
        const Rectangle whole(0, 0, kBBW, kBBH);
        VertexPositionTexture q[6];
        fillSamplingQuad(q);
        for (int i = 0; i < 24; ++i)
        {
            if (rtA) renderFourQuadrantsIntoRT(dev, *rtA);
            if (rtB) renderFourQuadrantsIntoRT(dev, *rtB);

            DualTextureEffect fx(dev);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.setTextureProperty(&texA);
            fx.setTexture2Property(&texB);

            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);

            dev.GetBackBufferData(&whole, buf.data(), 0, kBBW * kBBH);
            if (isFresh(at(buf, kBBW / 4, kBBH / 4)) && isFresh(at(buf, 3 * kBBW / 4, 3 * kBBH / 4)))
                break;
        }
        return buf;
    }

    void assertUpright(const std::vector<Color>& b, const char* tag)
    {
        const int xL = kBBW / 4, xR = 3 * kBBW / 4, yT = kBBH / 4, yB = 3 * kBBH / 4;
        char lbl[96];
        std::snprintf(lbl, sizeof(lbl), "%s TL", tag);
        check(classify(at(b, xL, yT)) == QRED,    lbl, classify(at(b, xL, yT)), QRED);
        std::snprintf(lbl, sizeof(lbl), "%s TR", tag);
        check(classify(at(b, xR, yT)) == QGREEN,  lbl, classify(at(b, xR, yT)), QGREEN);
        std::snprintf(lbl, sizeof(lbl), "%s BL", tag);
        check(classify(at(b, xL, yB)) == QBLUE,   lbl, classify(at(b, xL, yB)), QBLUE);
        std::snprintf(lbl, sizeof(lbl), "%s BR", tag);
        check(classify(at(b, xR, yB)) == QYELLOW, lbl, classify(at(b, xR, yB)), QYELLOW);
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

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        // 1 — single-Texture2D effect (BasicEffect textured), RenderTarget2D source: the former UB
        //     cast site. Must bind safely AND read upright at multiple sizes on both renderers.
        {
            const struct { int w, h; } sizes[] = { {64, 48}, {40, 48} };
            for (const auto& s : sizes)
            {
                RenderTarget2D rt(dev, s.w, s.h);
                char tag[48];
                std::snprintf(tag, sizeof(tag), "BasicEffect RT %dx%d", s.w, s.h);
                assertUpright(sampleThroughBasicEffect(dev, rt, /*reRenderRT*/ true, &rt), tag);
            }
        }

        // 2 — ordinary Texture2D control through the SAME BasicEffect quad: must ALSO read upright
        //     (the fix must not flip ordinary, top-down textures).
        {
            auto tex = buildQuadTexture(dev, 64, 48);
            assertUpright(sampleThroughBasicEffect(dev, *tex, /*reRenderRT*/ false, nullptr),
                          "BasicEffect Tex2D control");
        }

        // 3 — DualTextureEffect per-slot orientation (Phase 24 discriminator): each slot must be
        //     upright independently — a single global "flip all UVs" flag would fail these.
        {
            Color grayHalf(128, 128, 128, 255);
            Texture2D neutral(dev, 1, 1); neutral.SetData(&grayHalf, 1); // 2*base*0.5 ~ passthrough

            // 3a: slot0 = RT (four-quadrant), slot1 = neutral -> tests slot 0 RT orientation.
            {
                RenderTarget2D rt(dev, 64, 48);
                assertUpright(sampleThroughDualTexture(dev, rt, neutral, &rt, nullptr),
                              "Dual slot0=RT slot1=Tex2D");
            }
            // 3b: slot0 = neutral, slot1 = RT (four-quadrant) -> tests slot 1 RT orientation.
            {
                RenderTarget2D rt(dev, 64, 48);
                assertUpright(sampleThroughDualTexture(dev, neutral, rt, nullptr, &rt),
                              "Dual slot0=Tex2D slot1=RT");
            }
            // 3c: slot0 = ordinary Texture2D (four-quadrant), slot1 = RT (four-quadrant): both
            //     patterned. Per quadrant the hue is the product of the two samples, so if EITHER
            //     slot were mirrored the top-left would become RED*BLUE = black (OTHER) and fail.
            {
                auto texA = buildQuadTexture(dev, 64, 48);
                RenderTarget2D rt(dev, 64, 48);
                assertUpright(sampleThroughDualTexture(dev, *texA, rt, nullptr, &rt),
                              "Dual slot0=Tex2D slot1=RT patterned");
            }
            // 3d: reverse — slot0 = RT (four-quadrant), slot1 = ordinary Texture2D (four-quadrant).
            {
                auto texB = buildQuadTexture(dev, 64, 48);
                RenderTarget2D rt(dev, 64, 48);
                assertUpright(sampleThroughDualTexture(dev, rt, *texB, &rt, nullptr),
                              "Dual slot0=RT slot1=Tex2D patterned");
            }
            // 3e: ordinary Tex2D + Tex2D control (no RT) -> must stay upright (no flip on either).
            {
                auto texA = buildQuadTexture(dev, 64, 48);
                Texture2D n2(dev, 1, 1); n2.SetData(&grayHalf, 1);
                assertUpright(sampleThroughDualTexture(dev, *texA, n2, nullptr, nullptr),
                              "Dual Tex2D+Tex2D control");
            }
        }

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BgfxRenderTargetEffectTextureTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

const Color BgfxRenderTargetEffectTextureTest::RED(255, 0, 0, 255);
const Color BgfxRenderTargetEffectTextureTest::GREEN(0, 255, 0, 255);
const Color BgfxRenderTargetEffectTextureTest::BLUE(0, 0, 255, 255);
const Color BgfxRenderTargetEffectTextureTest::YELLOW(255, 255, 0, 255);

int main()
{
    BgfxRenderTargetEffectTextureTest game;
    game.Run();
    return game.getResult();
}

// SPDX-License-Identifier: MS-PL
// REMED-GFX-077: the Software (CPU-raster) backend must honor BlendState.ColorWriteChannels
// (per-channel colour write mask) and BlendState.MultiSampleMask, now that both are plumbed through
// IGraphicsBackend::ApplyBlendState (the interface previously carried only the 6 blend factor/
// function ordinals, so both were silent no-ops on every backend).
//
// XNA/FNA contract:
//   * ColorWriteChannels is a bit flag: None=0, Red=1, Green=2, Blue=4, Alpha=8, All=15. A disabled
//     channel keeps its existing destination component (identity) AFTER blending -- it is NOT zeroed.
//   * The colour write mask controls colour writes only, never depth (depth is written per
//     DepthStencilState regardless of the mask).
//   * MultiSampleMask (default -1 == 0xFFFFFFFF) selects which coverage samples are written. Software
//     is strictly single-sample, so only bit 0 is meaningful: bit 0 clear discards the whole
//     fragment (no colour, no depth).
//
// Strategy: dest D via Clear(); one full-screen source S drawn Opaque (blend off, so the write mask
// is isolated) with a given ColorWriteChannels; read the centre pixel back with GetBackBufferData
// and assert exact RGBA. Both the 3D colored path (DrawPrimitives) and the 2D SpriteBatch path are
// exercised, on the backbuffer and on a RenderTarget2D.
//
// Exit code 0 = all checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kW = 32;
    constexpr int kH = 24;
}

class SoftwareColorWriteChannelsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D whiteTex_;
    bool done_ = false;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++totalCount_;
        if (ok) ++passCount_;
    }

    // A BlendState with the requested ColorWriteChannels + MultiSampleMask. Factors default to
    // Opaque (One/Zero) unless `blend` requests premultiplied AlphaBlend, so the write mask is
    // isolated from any blend equation for the exact-value checks.
    static BlendState MakeBlend(ColorWriteChannels cw, bool blend = false,
                                unsigned int sampleMask = 0xFFFFFFFFu)
    {
        BlendState bs;
        if (blend)
        {
            bs.setColorSourceBlendProperty(Blend::One);
            bs.setColorDestinationBlendProperty(Blend::InverseSourceAlpha);
            bs.setAlphaSourceBlendProperty(Blend::One);
            bs.setAlphaDestinationBlendProperty(Blend::InverseSourceAlpha);
        }
        bs.setColorWriteChannelsProperty(cw);
        bs.setMultiSampleMaskProperty(static_cast<int>(sampleMask));
        return bs;
    }

    // Full-screen solid-colour quad via the 3D colored path (BasicEffect + VertexPositionColor).
    void DrawColorQuad(GraphicsDevice& dev, const Color& c, float z = 0.5f)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f,  1.0f, z), c }, { Vector3( 1.0f,  1.0f, z), c },
            { Vector3( 1.0f, -1.0f, z), c }, { Vector3(-1.0f,  1.0f, z), c },
            { Vector3( 1.0f, -1.0f, z), c }, { Vector3(-1.0f, -1.0f, z), c },
        };
        VertexBuffer vb(dev, 6);
        vb.SetData(verts, 6);
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    // Full-screen solid-colour sprite via the 2D SpriteBatch path (white 1x1 texel * tint).
    void DrawColorSprite(GraphicsDevice& dev, const Color& tint, const BlendState& bs)
    {
        SpriteBatch sb(dev);
        SamplerState point = SamplerState::PointClamp;
        sb.Begin(SpriteSortMode::Deferred, bs, &point, nullptr, nullptr);
        sb.Draw(whiteTex_, Rectangle(0, 0, kW, kH), Rectangle(0, 0, 1, 1), tint);
        sb.End();
    }

    Color CenterPixel(GraphicsDevice& dev, int w = kW, int h = kH)
    {
        std::vector<Color> pix(static_cast<std::size_t>(w) * h, Color(0, 0, 0, 0));
        const Rectangle whole(0, 0, w, h);
        dev.GetBackBufferData(&whole, pix.data(), 0, static_cast<int>(pix.size()));
        return pix[static_cast<std::size_t>(h / 2) * w + (w / 2)];
    }

    static std::string Str(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) + "," +
               std::to_string(c.getBProperty()) + "," + std::to_string(c.getAProperty()) + ")";
    }
    static bool Eq(const Color& c, int r, int g, int b, int a)
    {
        return c.getRProperty() == r && c.getGProperty() == g && c.getBProperty() == b && c.getAProperty() == a;
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        whiteTex_ = Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();
        dev.setDepthStencilStateProperty(DepthStencilState::None);

        const Color D(10, 20, 30, 40);       // destination (clear) colour
        const Color S(200, 100, 50, 220);    // source colour (valid premultiplied channels: RGB <= A)

        // ===== Phase 9: exact per-channel masks (Opaque, blend off), 3D colored path =====
        struct Case { ColorWriteChannels cw; int r, g, b, a; const char* name; };
        const Case cases[] = {
            { ColorWriteChannels::None,                              10,  20, 30,  40, "None" },
            { ColorWriteChannels::Red,                              200,  20, 30,  40, "Red" },
            { ColorWriteChannels::Green,                             10, 100, 30,  40, "Green" },
            { ColorWriteChannels::Blue,                              10,  20, 50,  40, "Blue" },
            { ColorWriteChannels::Alpha,                             10,  20, 30, 220, "Alpha" },
            { ColorWriteChannels::Red | ColorWriteChannels::Blue,   200,  20, 50,  40, "Red|Blue" },
            { ColorWriteChannels::All,                              200, 100, 50, 220, "All" },
        };
        for (const auto& c : cases)
        {
            dev.setBlendStateProperty(MakeBlend(c.cw));
            dev.Clear(D);
            DrawColorQuad(dev, S);
            const Color px = CenterPixel(dev);
            check(Eq(px, c.r, c.g, c.b, c.a),
                  std::string("Phase9 3D ColorWriteChannels.") + c.name + " -> expect (" +
                  std::to_string(c.r) + "," + std::to_string(c.g) + "," + std::to_string(c.b) + "," +
                  std::to_string(c.a) + "), got " + Str(px));
        }

        // ===== Phase 10: mask applies AFTER blending (enabled channels blend, disabled keep dest) =
        // AlphaBlend is premultiplied source + destination*(1-sourceAlpha) on the enabled
        // channels. Sa=220/255, so the backend clamps in float and converts once at the end:
        //   blended R = 200 + 10*(35/255) -> 201
        //   blended B =  50 + 30*(35/255) ->  54
        // Green/Alpha are masked off -> keep dest 20 / 40 (identity, NOT zeroed). The point is that a
        // disabled channel keeps the destination while enabled channels take the BLENDED value.
        {
            dev.Clear(D);
            DrawColorSprite(dev, S, MakeBlend(ColorWriteChannels::Red | ColorWriteChannels::Blue, true));
            const Color px = CenterPixel(dev);
            check(Eq(px, 201, 20, 54, 40),
                  "Phase10 mask-after-blend: R/B take the blended value, G(=20)/A(=40) kept from dest: " + Str(px));
        }

        // ===== Phase 11: depth is independent of the colour write mask =====
        // Control: no near occluder -> far quad (mask All) is visible.
        dev.setDepthStencilStateProperty(DepthStencilState::Default);
        {
            dev.setBlendStateProperty(MakeBlend(ColorWriteChannels::All));
            dev.Clear(D);                                   // clears colour to D and depth to 1.0
            DrawColorQuad(dev, S, 0.8f);                    // far
            const Color px = CenterPixel(dev);
            check(Eq(px, 200, 100, 50, 220), "Phase11 control: far quad visible with no occluder: " + Str(px));
        }
        // Test: draw a NEAR quad with mask None (writes NO colour but MUST write depth), then a FAR
        // quad with mask All. If the near quad wrote depth, the far quad fails the depth test and the
        // colour stays D -> proves ColorWriteChannels.None still wrote depth.
        {
            dev.Clear(D);
            dev.setBlendStateProperty(MakeBlend(ColorWriteChannels::None));
            DrawColorQuad(dev, Color(255, 255, 255, 255), 0.2f);   // near, colour masked off
            dev.setBlendStateProperty(MakeBlend(ColorWriteChannels::All));
            DrawColorQuad(dev, S, 0.8f);                            // far, would draw if depth were clear
            const Color px = CenterPixel(dev);
            check(Eq(px, 10, 20, 30, 40),
                  "Phase11 depth-independent: mask None wrote depth, occluding the far quad (stays D): " + Str(px));
        }
        dev.setDepthStencilStateProperty(DepthStencilState::None);

        // ===== Phase 37: SpriteBatch with ColorWriteChannels.None -> no colour change =====
        {
            dev.Clear(D);
            DrawColorSprite(dev, S, MakeBlend(ColorWriteChannels::None));
            const Color px = CenterPixel(dev);
            check(Eq(px, 10, 20, 30, 40), "Phase37 SpriteBatch None -> dest unchanged: " + Str(px));
            // Then default mask again -> normal rendering resumes.
            dev.Clear(D);
            DrawColorSprite(dev, S, MakeBlend(ColorWriteChannels::All));
            check(Eq(CenterPixel(dev), 200, 100, 50, 220), "Phase37 SpriteBatch All -> normal render resumes");
        }

        // ===== Phase 38: Alpha-only write mask -> RGB unchanged, Alpha changed =====
        {
            dev.setBlendStateProperty(MakeBlend(ColorWriteChannels::Alpha));
            dev.Clear(D);
            DrawColorQuad(dev, S);
            const Color px = CenterPixel(dev);
            check(Eq(px, 10, 20, 30, 220), "Phase38 Alpha-only: RGB kept (10,20,30), A=220: " + Str(px));
        }

        // ===== Phase 13/39: three masks in ONE frame, each draw uses its own state (A->B->A) =====
        // Split-screen: Red-only quad (left), Green-only (mid), Red-only (right) via scissor-free
        // full quads that each overwrite -- so verify the FINAL frame reflects the last-applied per
        // draw by drawing to distinct destinations. Simplest: sequential clears + reads.
        {
            dev.Clear(D); dev.setBlendStateProperty(MakeBlend(ColorWriteChannels::Red));
            DrawColorQuad(dev, S);
            const bool aRed = Eq(CenterPixel(dev), 200, 20, 30, 40);
            dev.Clear(D); dev.setBlendStateProperty(MakeBlend(ColorWriteChannels::Green));
            DrawColorQuad(dev, S);
            const bool bGreen = Eq(CenterPixel(dev), 10, 100, 30, 40);
            dev.Clear(D); dev.setBlendStateProperty(MakeBlend(ColorWriteChannels::Red));
            DrawColorQuad(dev, S);
            const bool aRed2 = Eq(CenterPixel(dev), 200, 20, 30, 40);
            check(aRed && bGreen && aRed2, "Phase13 A(Red)->B(Green)->A(Red): each draw used its own mask");
        }

        // ===== Phase 28/31: MultiSampleMask (single-sample) =====
        {
            dev.setBlendStateProperty(MakeBlend(ColorWriteChannels::All, false, 0u)); // mask 0
            dev.Clear(D);
            DrawColorQuad(dev, S);
            check(Eq(CenterPixel(dev), 10, 20, 30, 40),
                  "Phase28 MultiSampleMask=0 -> single sample discarded, dest unchanged: " + Str(CenterPixel(dev)));
            dev.setBlendStateProperty(MakeBlend(ColorWriteChannels::All, false, 0xFFFFFFFFu)); // default
            dev.Clear(D);
            DrawColorQuad(dev, S);
            check(Eq(CenterPixel(dev), 200, 100, 50, 220), "Phase29 MultiSampleMask=all -> normal (unregressed)");
        }

        // ===== Phase 40: same mask test on a RenderTarget2D (no target-state leakage) =====
        {
            const int rtW = 24, rtH = 20;
            RenderTarget2D rt(dev, rtW, rtH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&rt);
            dev.setBlendStateProperty(MakeBlend(ColorWriteChannels::Red | ColorWriteChannels::Blue));
            dev.Clear(D);
            DrawColorSprite(dev, S, MakeBlend(ColorWriteChannels::Red | ColorWriteChannels::Blue));
            const Color px = CenterPixel(dev, rtW, rtH);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            check(Eq(px, 200, 20, 50, 40), "Phase40 RenderTarget2D Red|Blue -> (200,20,50,40): " + Str(px));
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    SoftwareColorWriteChannelsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kW);
        gdm_->setPreferredBackBufferHeightProperty(kH);
    }

    int getResult() const { return result_; }
};

int main()
{
    SoftwareColorWriteChannelsTest game;
    game.Run();
    return game.getResult();
}

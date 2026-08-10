// SPDX-License-Identifier: MS-PL
// Task 747: verify TextureFilter::Point vs Linear on a 3D stock effect (DualTextureEffect), for
// both magnification and minification, on Bgfx.
//
// Bgfx-specific adaptation of examples/easygl_texture_filter_point_vs_linear_test.cpp (Task 297,
// already reused verbatim on Vulkan). Not a verbatim reuse: that file uses
// GraphicsDevice::SetDepthTestEnabled(false), which throws on Bgfx (a pre-existing, deliberate
// stub), replaced with the equivalent DepthStencilState-based call; and draws all 4 columns in one
// rendered frame before taking 4 separate GetBackBufferData reads, incompatible with Bgfx's "first
// read per rendered frame" quirk (Task 406) -- restructured into one separately-read RunCheck()
// pass per column, each redrawing just that column fresh.
//
// CNA maps XNA's single TextureFilter value to one GL/Vulkan/bgfx min+mag filter pair (matching
// FNA's flat enum -- there is no separate min/mag control), and generates no mipmaps by default,
// so magnification and minification share identical underlying sampler math here; this test still
// exercises both scales explicitly since that's the task's ask, and to confirm scale itself
// doesn't break the sampler-state fix (Task 750).
//
// Each check draws one column full-screen (a 2-texel Red|Green texture for magnification, a
// 256-texel alternating R/G texture for minification, compressed 2 texels/pixel) with the given
// filter, sampled exactly at a texel boundary (the midpoint between two texel centers, guaranteed
// NOT to coincide with any texel center): Point sampling always returns one pure texel colour
// there; Linear sampling always returns a genuine ~50/50 blend (both R and G channels
// significant), regardless of on-screen scale.
//
// Exit code 0 = all 4 checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

namespace
{
    SamplerState MakeSampler(TextureFilter filter)
    {
        SamplerState ss;
        ss.setFilterProperty(filter);
        ss.setAddressUProperty(TextureAddressMode::Clamp);
        ss.setAddressVProperty(TextureAddressMode::Clamp);
        return ss;
    }

    // True if both channels are in a genuine mid-range blend (~50/50 of 255).
    bool IsBlended(const Color& c)
    {
        return c.getRProperty() >= 90 && c.getRProperty() <= 165
            && c.getGProperty() >= 90 && c.getGProperty() <= 165;
    }

    // True if exactly one of R/G is a pure, unblended channel value.
    bool IsPure(const Color& c)
    {
        const bool rHigh = c.getRProperty() >= 235, rLow = c.getRProperty() <= 20;
        const bool gHigh = c.getGProperty() >= 235, gLow = c.getGProperty() <= 20;
        return (rHigh && gLow) || (rLow && gHigh);
    }
}

class BgfxTextureFilterPointVsLinearTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D tex2_;   // 2x1: Red | Green
    Texture2D tex256_; // 256x1: alternating Red/Green per texel
    Texture2D whiteTex_;
    bool done_   = false;
    int  result_ = 1;

    void DrawFullscreen(GraphicsDevice& dev, Texture2D& tex, TextureFilter filter)
    {
        dev.getSamplerStatesProperty()[0] = MakeSampler(filter);

        DualTextureEffect fx(dev);
        fx.setTextureProperty(&tex);
        fx.setTexture2Property(&whiteTex_);
        // Task 383 fix: DualTextureEffect's shader now correctly applies FNA's
        // `color.rgb *= 2` doubling factor to the first texture slot. With a white (identity)
        // overlay texture that doubling would otherwise propagate straight into the final pixel
        // and push this test's mid-range blend samples up near saturation. Compensate with
        // DiffuseColor=0.5 (2 * 0.5 = 1) so the raw texel colours this test cares about are
        // preserved unscaled.
        fx.setDiffuseColorProperty(Vector3(0.5f, 0.5f, 0.5f));
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();

        const VertexPositionTexture verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), Vector2(1.0f, 1.0f) },
        };
        // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real default
        // RasterizerState -- needs CullNone.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }

    // Draws `tex` full-screen with `filter`, samples at the viewport's exact texel-boundary
    // x-fraction (0.5, since the fullscreen quad maps the whole texture width across the whole
    // viewport width and the boundary between the 2 halves of a 2-texel Red|Green texture -- or
    // the analogous boundary for the 256-texel one -- both land at U=0.5 of the viewport here).
    Color RunCheck(GraphicsDevice& dev, Texture2D& tex, TextureFilter filter)
    {
        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 0, 0, 255));
            DepthStencilState ds;
            ds.setDepthBufferEnableProperty(false);
            dev.setDepthStencilStateProperty(ds);
            dev.setBlendStateProperty(BlendState::Opaque);

            DrawFullscreen(dev, tex, filter);

            const auto& vp = dev.getViewportProperty();
            const int W = vp.getWidthProperty();
            const int H = vp.getHeightProperty();
            const Rectangle reg(W / 2, H / 2, 1, 1);
            dev.GetBackBufferData(&reg, &got, 0, 1);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break;  // Bgfx's GetBackBufferData only reliably reflects the first read per
                        // rendered frame (Task 406 finding); this test reads once per frame.
        }
        return got;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        const std::vector<uint8_t> pattern2 = {
            255,   0,   0, 255,
              0, 255,   0, 255
        };
        tex2_ = Texture2D::CreateFromPixels(device, 2, 1, pattern2);

        std::vector<uint8_t> pattern256(256 * 4);
        for (int i = 0; i < 256; ++i)
        {
            const bool red = (i % 2) == 0;
            pattern256[i * 4 + 0] = red ? 255 : 0;
            pattern256[i * 4 + 1] = red ? 0   : 255;
            pattern256[i * 4 + 2] = 0;
            pattern256[i * 4 + 3] = 255;
        }
        tex256_ = Texture2D::CreateFromPixels(device, 256, 1, pattern256);

        const std::vector<uint8_t> white = { 255, 255, 255, 255 };
        whiteTex_ = Texture2D::CreateFromPixels(device, 1, 1, white);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        const Color magPoint  = RunCheck(dev, tex2_,   TextureFilter::Point);
        const Color magLinear = RunCheck(dev, tex2_,   TextureFilter::Linear);
        const Color minPoint  = RunCheck(dev, tex256_, TextureFilter::Point);
        const Color minLinear = RunCheck(dev, tex256_, TextureFilter::Linear);

        struct Check { const char* label; Color c; bool (*pred)(const Color&); };
        const Check checks[4] = {
            { "Magnification/Point (expect pure)",    magPoint,  IsPure    },
            { "Magnification/Linear (expect blend)",  magLinear, IsBlended },
            { "Minification/Point (expect pure)",     minPoint,  IsPure    },
            { "Minification/Linear (expect blend)",   minLinear, IsBlended },
        };

        int passCount = 0;
        for (const auto& chk : checks)
        {
            const bool ok = chk.pred(chk.c);
            std::printf("[%s] %s: sample=(%d,%d,%d)\n", ok ? "PASS" : "FAIL", chk.label,
                        chk.c.getRProperty(), chk.c.getGProperty(), chk.c.getBProperty());
            if (ok) ++passCount;
        }

        std::printf("=== %d/4 PASS ===\n", passCount);
        result_ = (passCount == 4) ? 0 : 1;
        Exit();
    }

public:
    BgfxTextureFilterPointVsLinearTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxTextureFilterPointVsLinearTest game;
    game.Run();
    return game.getResult();
}

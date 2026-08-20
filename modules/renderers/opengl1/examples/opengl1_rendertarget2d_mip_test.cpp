// SPDX-License-Identifier: MS-PL
// OPENGL1 renderer: RenderTarget2D mip-chain generation on unbind (plans/plan_opengl1.md item 21,
// EasyGL parity).
//
// Before this, OpenGL1Renderer::CreateRenderTarget2D() silently dropped its own mipMap
// parameter (unnamed bool) -- OpenGL1RenderTargetRenderer never generated any mip levels beyond
// level 0, and even a texture that DID have real mip storage would still have been sampled with
// mip=false everywhere (BasicEffect/DualTextureEffect 3D sampling and SpriteBatch::Draw all
// dynamic_cast<const OpenGL1TextureRenderer*> to decide mip-awareness, which is always null for an
// OpenGL1RenderTargetRenderer -- a different class entirely).
//
// A solid-color fill is NOT a valid probe here: OpenGL1's mip-aware sampling path gracefully
// degrades to a plain (non-mipmap) GL_LINEAR MIN_FILTER whenever the sampled texture's HasMips()
// is false, so a flat color reads back identically whether or not a real mip chain exists --
// exactly the "default value masks bug" trap this project's own test-design history has already
// been burned by once (see plans/plan_opengl1.md phase 6's SamplerState finding). Confirmed empirically:
// a first version of this test using solid blue passed even with mip regeneration completely
// disabled via mutation.
//
// Reuses the same discriminator OpenGL1_MipmapGeneration (examples/opengl1_mipmap_generation_test.cpp)
// already established for Texture2D: a checkerboard pattern aliases sharply when sampled at native
// size (level 0), but blurs to an EXACT uniform mid-gray once minified enough that the GPU's LOD
// computation selects a higher, box-filtered mip level -- a checkerboard's 2x2 box average is
// exactly gray at every level once the block size, giving a robust, driver-rounding-tolerant
// pass/fail signal. Content here is GPU-rendered into the RenderTarget2D itself (a checkerboard of
// 8x8-pixel blocks drawn via SpriteBatch, the only way to populate render-target pixels), not
// uploaded via SetData.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kViewport   = 64;
    constexpr int kRTSize     = 64;
    constexpr int kBlockSize  = 8; // 8x8 blocks -> 8 blocks/side, same period-vs-size ratio
                                    // OpenGL1_MipmapGeneration's own 8x8-checker/8px-texture case uses.
}

class RenderTarget2DMipTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<RenderTarget2D>        rt_;
    bool done_ = false;
    int  pass_ = 0;
    int  fail_ = 0;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    Color ReadCenter(GraphicsDevice& dev)
    {
        const Rectangle reg(kViewport / 2, kViewport / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(device);
        rt_ = std::make_unique<RenderTarget2D>(device, kRTSize, kRTSize, /*mipMap=*/true,
                                                SurfaceFormat::Color, DepthFormat::None);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        // 1x1 white texel used to paint the checkerboard's white blocks.
        Texture2D white1x1(dev, 1, 1, /*mipMap=*/false, SurfaceFormat::Color);
        Color whitePixel[1] = { Color::White };
        white1x1.SetData(whitePixel, 1);

        // Render an 8x8-block checkerboard into the RenderTarget2D itself (GPU-drawn content --
        // RenderTarget2D has no CPU-side SetData path), then unbind (triggers mip regeneration).
        dev.SetRenderTarget(rt_.get());
        dev.Clear(Color::Black);
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque);
        const int blocks = kRTSize / kBlockSize;
        for (int by = 0; by < blocks; ++by)
            for (int bx = 0; bx < blocks; ++bx)
                if ((bx + by) % 2 == 0)
                    sb_->Draw(white1x1, Rectangle(bx * kBlockSize, by * kBlockSize, kBlockSize, kBlockSize),
                              Rectangle(0, 0, 1, 1), Color::White);
        sb_->End();
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        // REMED-GFX-081: SpriteBatch::Begin now applies its FNA-faithful default rasterizer state
        // (CullCounterClockwise) through the device property, and -- real XNA semantics -- that
        // state persists after End(). The CullNone this test set before painting the checkerboard
        // is therefore gone by the time the 3D quads below draw, and their winding was authored
        // under CullNone. Re-assert it where the 3D draws actually depend on it.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        Matrix world = Matrix::getIdentityProperty();
        Matrix view  = Matrix::getIdentityProperty();
        Matrix proj  = Matrix::CreateOrthographicOffCenter(0.0f, (float)kViewport, (float)kViewport, 0.0f, -1.0f, 1.0f);

        auto drawQuadAndReadCenter = [&](float size, TextureFilter filter) -> Color
        {
            dev.Clear(Color(64, 64, 64, 255));
            const float half = size / 2.0f;
            const float cx = kViewport / 2.0f, cy = kViewport / 2.0f;
            const VertexPositionTexture q[6] = {
                { Vector3(cx - half, cy - half, 0.0f), Vector2(0.0f, 0.0f) },
                { Vector3(cx - half, cy + half, 0.0f), Vector2(0.0f, 1.0f) },
                { Vector3(cx + half, cy + half, 0.0f), Vector2(1.0f, 1.0f) },
                { Vector3(cx - half, cy - half, 0.0f), Vector2(0.0f, 0.0f) },
                { Vector3(cx + half, cy + half, 0.0f), Vector2(1.0f, 1.0f) },
                { Vector3(cx + half, cy - half, 0.0f), Vector2(1.0f, 0.0f) },
            };

            dev.getSamplerStatesProperty()[0].setFilterProperty(filter);

            BasicEffect fx(dev);
            fx.setWorldProperty(world);
            fx.setViewProperty(view);
            fx.setProjectionProperty(proj);
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(rt_.get());
            fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            fx.setAlphaProperty(1.0f);
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
            return ReadCenter(dev);
        };

        // Native size (64 world units == the render target's own resolution): LOD selects level 0,
        // the raw high-frequency checkerboard -- must NOT read as uniform gray.
        Color nativeGot = drawQuadAndReadCenter((float)kRTSize, TextureFilter::LinearMipPoint);
        std::printf("native (size=%d) got=(%d,%d,%d)\n", kRTSize,
                    nativeGot.getRProperty(), nativeGot.getGProperty(), nativeGot.getBProperty());
        const int nativeDeviationFromGray = std::abs(nativeGot.getRProperty() - 127);
        Check(nativeDeviationFromGray > 60,
              "native size (level 0): checkerboard aliases, center is far from mid-gray");

        // Minified ~25x (2.5 world units, same 64x64 render target): LOD selects a high mip level
        // (>=4, where each texel already spans a full block period) -- must read as uniform gray,
        // proving the mip chain was actually generated (on unbind) and actually sampled (via the
        // HasMipsFor()-gated mip-aware filter every 3D/SpriteBatch texturing call site now shares).
        //
        // Deliberately NOT size=2.0 (h=1.0): with this quad/viewport geometry that maps the
        // read-back pixel's center to EXACTLY texel-space coordinate 48.0 -- a texel-grid corner
        // where a checkerboard's diagonal 2x2 neighborhood (2 white + 2 black) blends to an exact
        // gray under plain bilinear filtering ALONE, with no mip chain involved at all. Confirmed
        // empirically: an earlier version of this test using size=2.0 stayed gray even with mip
        // regeneration mutated out entirely, a false pass. size=2.5 (h=1.25) maps the read-back
        // pixel to texel-space 44.8 -- solidly inside a single 8-texel block (block boundaries are
        // only at multiples of 8), so an unmipped level-0-only sample reads a PURE block color
        // (this checkerboard's convention makes that block white), unambiguously far from gray.
        Color minifiedGot = drawQuadAndReadCenter(2.5f, TextureFilter::LinearMipPoint);
        std::printf("minified (size=2.5) got=(%d,%d,%d)\n",
                    minifiedGot.getRProperty(), minifiedGot.getGProperty(), minifiedGot.getBProperty());
        const int minifiedDeviationFromGray = std::abs(minifiedGot.getRProperty() - 127);
        Check(minifiedDeviationFromGray <= 20,
              "minified 32x + LinearMipPoint: samples a pre-averaged higher mip level, reads as mid-gray");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    RenderTarget2DMipTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kViewport);
        gdm_->setPreferredBackBufferHeightProperty(kViewport);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    RenderTarget2DMipTest game;
    game.Run();
    return game.getResult();
}

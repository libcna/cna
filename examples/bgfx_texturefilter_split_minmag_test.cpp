// SPDX-License-Identifier: MS-PL
// Task 743: verify Bgfx's SamplerState -> BGFX_SAMPLER_* flag mapping is complete for the 6 XNA
// TextureFilter values with a split Min/Mag/Mip combination (LinearMipPoint, PointMipLinear,
// MinLinearMagPointMipLinear, MinLinearMagPointMipPoint, MinPointMagLinearMipLinear,
// MinPointMagLinearMipPoint) -- not just Linear/Point/Anisotropic.
//
// Real finding (this task's own audit): BgfxGraphicsBackend::ApplySamplerState's filter switch
// previously handled only Point and Anisotropic explicitly; all 6 split values fell through to the
// `default` (plain Linear, no flags) branch, silently discarding the Min/Mag/Point-vs-Linear
// distinction entirely. EasyGLGraphicsBackend::ApplySamplerState already maps all 9 XNA
// TextureFilter values correctly via GL's combined min-filter enum (NearestMipmapLinear etc.) --
// cross-referenced against real XNA semantics (name encodes Min/Mag/Mip in that order) to derive
// the equivalent bgfx per-axis flag combination for each of the 6 previously-unhandled values.
// Fixed by adding explicit cases mapping each to bgfx's 3 independent MIN/MAG/MIP_POINT bits.
//
// This test verifies the Mag-filter half only (Point -> pure sample at a texel boundary under
// magnification, Linear -> a genuine blend) -- the Mip half can't be independently observed without
// a real generated mip chain (CNA does not generate mipmaps by default for CreateFromPixels
// textures, matching the established convention already noted in Task 297's own test), but every
// one of the 6 previously-broken values differs from at least one neighbor in its Mag component,
// so this fully discriminates "the split is applied at all" from "everything collapses to Linear".
//
// Exit code 0 = all 6 checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
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
    bool IsBlended(const Color& c)
    {
        return c.getRProperty() >= 90 && c.getRProperty() <= 165
            && c.getGProperty() >= 90 && c.getGProperty() <= 165;
    }

    bool IsPure(const Color& c)
    {
        const bool rHigh = c.getRProperty() >= 235, rLow = c.getRProperty() <= 20;
        const bool gHigh = c.getGProperty() >= 235, gLow = c.getGProperty() <= 20;
        return (rHigh && gLow) || (rLow && gHigh);
    }

    SamplerState MakeSampler(TextureFilter filter)
    {
        SamplerState ss;
        ss.setFilterProperty(filter);
        ss.setAddressUProperty(TextureAddressMode::Clamp);
        ss.setAddressVProperty(TextureAddressMode::Clamp);
        return ss;
    }
}

class BgfxTextureFilterSplitMinMagTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D tex2_; // 2x1: Red | Green
    Texture2D whiteTex_;
    bool done_   = false;
    int  result_ = 1;

    Color RunCheck(GraphicsDevice& dev, TextureFilter filter)
    {
        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 0, 0, 255));
            DepthStencilState ds;
            ds.setDepthBufferEnableProperty(false);
            dev.setDepthStencilStateProperty(ds);
            dev.setBlendStateProperty(BlendState::Opaque);

            dev.getSamplerStatesProperty()[0] = MakeSampler(filter);

            DualTextureEffect fx(dev);
            fx.setTextureProperty(&tex2_);
            fx.setTexture2Property(&whiteTex_);
            // Task 383 fix compensation, matching Task 747's own test: DualTextureEffect's
            // color.rgb *= 2 doubling would otherwise push these mid-range blend samples toward
            // saturation with a white overlay texture; DiffuseColor=0.5 cancels it out.
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

            const auto& vp = dev.getViewportProperty();
            const int W = vp.getWidthProperty();
            const int H = vp.getHeightProperty();
            const Rectangle reg(W / 2, H / 2, 1, 1); // texel boundary (u=0.5)
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

        const std::vector<uint8_t> white = { 255, 255, 255, 255 };
        whiteTex_ = Texture2D::CreateFromPixels(device, 1, 1, white);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        struct Check { const char* label; TextureFilter filter; bool magIsPoint; };
        const Check checks[6] = {
            { "LinearMipPoint (Mag=Linear -> expect blend)",              TextureFilter::LinearMipPoint,             false },
            { "PointMipLinear (Mag=Point -> expect pure)",                TextureFilter::PointMipLinear,             true  },
            { "MinLinearMagPointMipLinear (Mag=Point -> expect pure)",    TextureFilter::MinLinearMagPointMipLinear, true  },
            { "MinLinearMagPointMipPoint (Mag=Point -> expect pure)",     TextureFilter::MinLinearMagPointMipPoint,  true  },
            { "MinPointMagLinearMipLinear (Mag=Linear -> expect blend)",  TextureFilter::MinPointMagLinearMipLinear, false },
            { "MinPointMagLinearMipPoint (Mag=Linear -> expect blend)",   TextureFilter::MinPointMagLinearMipPoint,  false },
        };

        int passCount = 0;
        for (const auto& chk : checks)
        {
            const Color c = RunCheck(dev, chk.filter);
            const bool ok = chk.magIsPoint ? IsPure(c) : IsBlended(c);
            std::printf("[%s] %s: sample=(%d,%d,%d)\n", ok ? "PASS" : "FAIL", chk.label,
                        c.getRProperty(), c.getGProperty(), c.getBProperty());
            if (ok) ++passCount;
        }

        std::printf("=== %d/6 PASS ===\n", passCount);
        result_ = (passCount == 6) ? 0 : 1;
        Exit();
    }

public:
    BgfxTextureFilterSplitMinMagTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxTextureFilterSplitMinMagTest game;
    game.Run();
    return game.getResult();
}

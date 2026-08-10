// SPDX-License-Identifier: MS-PL
// Task 889: DualTextureEffect.VertexColorEnabled, verified on all 3 renderers (EasyGL/Vulkan/Bgfx
// share this exact source, mirroring Task 950's graphicsdevice_clear_depth_test.cpp pattern).
//
// FNA reference (Graphics/Effect/StockEffects/HLSL/DualTextureEffect.fx): `VSDualTextureVc`
// executes `vout.Diffuse *= vin.Color` on top of `ComputeCommonVSOutput()`'s
// `vout.Diffuse = DiffuseColor`; `PSDualTexture` computes
// `color = SAMPLE_TEXTURE(Texture,pin.TexCoord); color.rgb *= 2; color *= overlay * pin.Diffuse`.
// With both textures white (an identity factor for both the base-times-2 and overlay multiplies,
// since 2*1.0 clamps back to 1.0 on an 8-bit render target), the whole formula reduces to
// `RGB = VertexColor * DiffuseColor * Alpha` when VertexColorEnabled, or just
// `DiffuseColor * Alpha` when disabled — two genuinely different, non-degenerate expected colors,
// so this discriminates VertexColorEnabled's effect directly without needing an alpha-test/discard
// trick (DualTextureEffect has no alpha test at all).
//
// Bug this closes (Task 889, opened by Task 389's cross-renderer capstone test): before this fix,
// all 3 renderers' dual-texture vertex shaders declared only position+UV (no color attribute at
// all) and never read VertexColorEnabled, so both cases below produced the SAME
// "DiffuseColor*Alpha only" output — this test would have failed the VertexColorEnabled=true case
// on every renderer prior to the fix.
//
// Uses VertexColor RGB=(200,100,50) Alpha=200, DiffuseColor=(0.6,0.4,0.8), effect Alpha=0.8
// (identical constants to Task 377/887's AlphaTestEffect vertex-color tests, so the expected
// values are directly cross-checkable against that established precedent).
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

// PSDualTexture computes `base = SAMPLE(Texture); base.rgb *= 2; color = base * overlay * pin.Diffuse`
// with NO intermediate clamp (a single fragment-shader invocation's float math is only clamped at
// the final framebuffer write) -- so a white base texture would double the final RGB instead of
// acting as an identity factor. Using a half-gray base texture instead makes base.rgb*2 ~= 1.0,
// the intended "double a half-bright lightmap back to full" identity case.
static const Color kHalfGray(128, 128, 128, 255);
static const Color kWhite(255, 255, 255, 255);
static const Color kVertexColor(200, 100, 50, 200);
static const Vector3 kDiffuse(0.6f, 0.4f, 0.8f);
static constexpr float kEffectAlpha = 0.8f;

// VertexColorEnabled=true: BaseTexture(~0.502*2~=1.0) * Overlay(1,1,1) * VertexColor(200,100,50)/255
// * DiffuseColor * Alpha.
static const Color kExpectedEnabled(96, 32, 32, 255);
// VertexColorEnabled=false: DiffuseColor * Alpha only (vertex color factor dropped).
static const Color kExpectedDisabled(122, 82, 163, 255);
static const Color kBlack(0, 0, 0, 255);

class DualTextureEffectVertexColorTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int  pass_ = 0;
    int  fail_ = 0;

    void check(bool ok, const char* label, const Color& got, const char* expected)
    {
        if (ok)
        {
            std::printf("[PASS] %s: got=(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
            ++pass_;
        }
        else
        {
            std::printf("[FAIL] %s: got=(%d,%d,%d) expected %s\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(), expected);
            ++fail_;
        }
    }

    static bool closeTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }

    static bool matches(const Color& c, const Color& expected)
    {
        return closeTo(c.getRProperty(), expected.getRProperty(), 8)
            && closeTo(c.getGProperty(), expected.getGProperty(), 8)
            && closeTo(c.getBProperty(), expected.getBProperty(), 8);
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    Color renderWith(GraphicsDevice& dev, Texture2D& tex0, Texture2D& tex1,
                      const VertexPositionColorTexture (&quad)[6], bool vertexColorEnabled)
    {
        DualTextureEffect fx(dev);
        fx.setTextureProperty(&tex0);
        fx.setTexture2Property(&tex1);
        fx.setVertexColorEnabledProperty(vertexColorEnabled);
        fx.setDiffuseColorProperty(kDiffuse);
        fx.setAlphaProperty(kEffectAlpha);
        fx.Apply();

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(kBlack);
            dev.setBlendStateProperty(BlendState::Opaque);
            // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real
            // default RasterizerState -- needs CullNone.
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            got = readCenter(dev);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break; // skip blank/black readback frames
        }
        return got;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        Texture2D tex0(dev, 1, 1);
        tex0.SetData(&kHalfGray, 1);
        Texture2D tex1(dev, 1, 1);
        tex1.SetData(&kWhite, 1);

        const Vector3 tl(-1.0f,  1.0f, 0.0f), bl(-1.0f, -1.0f, 0.0f);
        const Vector3 br( 1.0f, -1.0f, 0.0f), tr( 1.0f,  1.0f, 0.0f);
        const Vector2 uv0(0.0f, 0.0f), uv1(0.0f, 1.0f), uv2(1.0f, 1.0f), uv3(1.0f, 0.0f);
        const VertexPositionColorTexture quad[6] = {
            { tl, kVertexColor, uv0 }, { bl, kVertexColor, uv1 }, { br, kVertexColor, uv2 },
            { tl, kVertexColor, uv0 }, { br, kVertexColor, uv2 }, { tr, kVertexColor, uv3 },
        };

        const Color enabledGot = renderWith(dev, tex0, tex1, quad, true);
        check(matches(enabledGot, kExpectedEnabled),
              "VertexColorEnabled=true: RGB == VertexColor*DiffuseColor*Alpha",
              enabledGot, "(96,32,32)");

        const Color disabledGot = renderWith(dev, tex0, tex1, quad, false);
        check(matches(disabledGot, kExpectedDisabled),
              "VertexColorEnabled=false: RGB == DiffuseColor*Alpha only",
              disabledGot, "(122,82,163)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    DualTextureEffectVertexColorTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    DualTextureEffectVertexColorTest game;
    game.Run();
    return game.getResult();
}

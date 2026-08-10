// SPDX-License-Identifier: MS-PL
// Task 367: BasicEffect pixel test — TextureEnabled=true AND VertexColorEnabled=true, the
// stride-24 VertexPositionColorTexture path (EasyGL renderer).
//
// FNA reference (Graphics/Effect/StockEffects/BasicEffect.cs OnApply() + HLSL/BasicEffect.fx +
// Common.fxh): with LightingEnabled=false (default), VertexColorEnabled=true and
// TextureEnabled=true (both explicitly set here), BasicEffect selects shaderIndex
// 1(no fog)+2(vertex color)+4(texture)=7 -> VSBasicTxVcNoFog/PSBasicTxNoFog. Common.fxh's
// ComputeCommonVSOutput() sets vout.Diffuse = DiffuseColor (the *material* color, already
// alpha-premultiplied by EffectHelpers.SetMaterialColor: diffuse.rgb =
// (DiffuseColor+EmissiveColor)*Alpha, diffuse.a = Alpha), and VSBasicTxVcNoFog additionally
// executes `vout.Diffuse *= vin.Color`, i.e. the material color is multiplied component-wise by
// the per-vertex color attribute. PSBasicTxNoFog (shared with Task 366's texture-only path)
// returns `SAMPLE_TEXTURE(Texture, pin.TexCoord) * pin.Diffuse`, i.e. the sampled texture color
// multiplied component-wise by that already vertex-color-multiplied diffuse. Net expected
// fragment output (EmissiveColor at its default (0,0,0)) =
// TextureColor * VertexColor * DiffuseColor * Alpha, component-wise — a genuine 3-way product,
// distinct from either Task 365 (vertex color x diffuse, no texture) or Task 366 (texture x
// diffuse, no vertex color).
//
// NOTE (deferred, not this task's scope): same +EmissiveColor gap as Tasks 364-366, tracked as
// part of Task 369.
//
// REAL BUG FOUND AND FIXED by writing this test: CNA's EasyGL `EnsureColoredTextured3DProgram()`
// (the stride-24 "col+textured" shader used for VertexPositionColorTexture) dropped DiffuseColor
// entirely — its fragment shader was `texture(uTexture,vUV)*vColor` with no `uDiffuseColor`
// uniform at all, unlike the sibling no-texture `EnsureColored3DProgram()` (Task 364) and the
// texture-only `EnsureTextured3DProgram()` (Task 366), both of which do multiply by
// `uDiffuseColor`. It also had no `VertexColorEnabled` gate (always multiplied by the raw vertex
// color regardless of the flag), unlike `EnsureColored3DProgram()`'s Task-364 fix. Fixed by
// mirroring that exact pattern: added `uniform vec4 uDiffuseColor` + `uniform float
// uVertexColorEnabled` to the fragment shader, computing `vc=(uVertexColorEnabled>0.5)?vColor:
// vec4(1)` then `FragColor=texture(...)*vc*uDiffuseColor`. The generic `BindDrawParams()` uniform
// setter already handles `loc_diffuse`/`loc_vertexcolor` whenever the program exposes them, so no
// other C++ changes were needed. The identical bug (DiffuseColor silently dropped in the
// stride-24 vertex+fragment shader pair) was independently found and fixed the same way on Bgfx;
// Vulkan's `colored_textured3d.vert.glsl` already multiplied by `pc.diffuseColor` correctly and
// needed no fix.
//
// Uses 3 distinctly-valued, non-white colors chosen so all three inputs and every 2-of-3 partial
// product are numerically distinguishable from the true 3-way product: texture color
// (200,100,50), vertex color (150,200,100), DiffuseColor (0.8,0.4,0.6). Correct output:
// TextureColor*VertexColor*DiffuseColor/255 = (94,31,12). Partial-product failure references:
// texture*diffuse only (204,102,153 diffuse-ignoring... see kTextureDiffuseOnly below) etc. — see
// the named failure-mode constants for the exact values a broken path would produce.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
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

// 1x1 texture color: an oddly-valued, opaque color, deliberately not white.
static const Color kTexColor(200, 100, 50, 255);
// Per-vertex color: a second, distinct oddly-valued opaque color.
static const Color kVertexColor(150, 200, 100, 255);
// DiffuseColor(0.8, 0.4, 0.6) * Alpha(1.0).
static const Vector3 kDiffuse(0.8f, 0.4f, 0.6f);

// Expected: TextureColor/255 * VertexColor/255 * DiffuseColor * 255 = (94, 31, 12).
static const Color kExpected(94, 31, 12, 255);
// Failure-mode references, used only to prove discriminating power / diagnose a broken path.
static const Color kTextureDiffuseOnly(160, 40, 30, 255);   // vertex color ignored
static const Color kVertexDiffuseOnly(120, 80, 60, 255);    // texture ignored
static const Color kTextureVertexOnly(118, 78, 20, 255);    // diffuse ignored
static const Color kTextureOnly(200, 100, 50, 255);
static const Color kVertexOnly(150, 200, 100, 255);
static const Color kDiffuseOnly(204, 102, 153, 255);

class BasicEffectTextureVertexColorEnabledTest : public Game
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

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        Texture2D tex(dev, 1, 1);
        tex.SetData(&kTexColor, 1);

        BasicEffect fx(dev);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&tex);
        fx.VertexColorEnabled = true;
        fx.setDiffuseColorProperty(kDiffuse);

        // Full-screen NDC quad, VertexPositionColorTexture (stride 24): position + per-vertex
        // color + texture coordinate, all three inputs live at once.
        const Vector3 tl(-1.0f,  1.0f, 0.0f), bl(-1.0f, -1.0f, 0.0f);
        const Vector3 br( 1.0f, -1.0f, 0.0f), tr( 1.0f,  1.0f, 0.0f);
        const Vector2 uv0(0.0f, 0.0f), uv1(0.0f, 1.0f), uv2(1.0f, 1.0f), uv3(1.0f, 0.0f);
        const VertexPositionColorTexture q[6] = {
            { tl, kVertexColor, uv0 }, { bl, kVertexColor, uv1 }, { br, kVertexColor, uv2 },
            { tl, kVertexColor, uv0 }, { br, kVertexColor, uv2 }, { tr, kVertexColor, uv3 },
        };

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            fx.Apply();
            // Task 896 finding (mirrors the Bgfx sibling's Task 364/884 fix): this quad's winding
            // is culled by the real default RasterizerState once EasyGL pushes it at construction.
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
            got = readCenter(dev);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break; // skip blank/black frames
        }

        check(matches(got, kExpected),
              "Texture+VertexColor: pixel == TextureColor*VertexColor*DiffuseColor",
              got, "(94,31,12)");
        check(!matches(got, kTextureDiffuseOnly),
              "Texture+VertexColor: pixel != texture*diffuse alone (vertex color not ignored)",
              got, "not (160,40,30)");
        check(!matches(got, kVertexDiffuseOnly),
              "Texture+VertexColor: pixel != vertex*diffuse alone (texture not ignored)",
              got, "not (120,80,60)");
        check(!matches(got, kTextureVertexOnly),
              "Texture+VertexColor: pixel != texture*vertex alone (diffuse not ignored)",
              got, "not (118,78,20)");
        check(!matches(got, kTextureOnly), "Texture+VertexColor: pixel != texture color alone",
              got, "not (200,100,50)");
        check(!matches(got, kVertexOnly), "Texture+VertexColor: pixel != vertex color alone",
              got, "not (150,200,100)");
        check(!matches(got, kDiffuseOnly), "Texture+VertexColor: pixel != DiffuseColor alone",
              got, "not (204,102,153)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BasicEffectTextureVertexColorEnabledTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BasicEffectTextureVertexColorEnabledTest game;
    game.Run();
    return game.getResult();
}

// SPDX-License-Identifier: MS-PL
// Task 379: AlphaTestEffect null/no-texture behavior (EasyGL renderer).
//
// FNA reference: `AlphaTestEffect` has no `TextureEnabled` flag at all (unlike `BasicEffect`) —
// every one of its 4 shader variants unconditionally does `SAMPLE_TEXTURE(Texture,pin.TexCoord) *
// pin.Diffuse`. A texture is *required* for meaningful use; FNA itself does not define what
// happens if `Texture` is left null (sampling an unbound texture unit is graphics-API-dependent
// undefined behavior in real Direct3D9/OpenGL, not a documented XNA fallback).
//
// CNA's own, already-established, cross-renderer convention (used by every texture-sampling
// shader path, not just `AlphaTestEffect`): if no texture is bound, sample an internal 1×1 opaque
// **white** texture instead of leaving the sampler unbound or stale — a safe, defined, documented
// intentional deviation from FNA's undefined behavior (matches this project's `EnsureDefaultWhiteTexture()`
// pattern, already used consistently by `BasicEffect`/`DualTextureEffect`/etc.).
//
// REAL BUG FOUND AND FIXED ON BGFX by writing this test (see the Bgfx variant of this test and
// plans/plan_graphics.md's Task 379 entry for the full finding): Bgfx's texture-binding code only ever
// called `bgfx::setTexture()` when a real texture was present (`if (params.texture0 && ...)`),
// with **no fallback at all** — meaning a null-texture draw silently left whatever texture the
// *previous* draw call had bound, a stale, undefined result inconsistent with EasyGL's and
// Vulkan's own deliberate white-texture fallback. Fixed by adding a `defaultWhiteTexture3D_` and
// an `else` branch to all 7 of Bgfx's texture-binding call sites (every one of the C++ dispatch
// branches that samples `texColor3DSampler_`, not just the `AlphaTestEffect`-specific one — the
// bug was general, not limited to this one effect). This EasyGL test itself found no bug — EasyGL
// already had the correct fallback (`EnsureDefaultWhiteTexture()`/`default_white_texture_`, used
// generically since before this task).
//
// Also confirmed, not fixed here (deliberately out of scope — `DualTextureEffect`, not
// `AlphaTestEffect`, and much lower impact): Bgfx's *second* texture slot
// (`texColor3DSampler2_`/`params.texture1`) has the exact same unconditional-skip shape and could
// use the identical fix, but `DualTextureEffect` always requires both textures by design (unlike
// `AlphaTestEffect`, where a null texture is a real, if unusual, usage pattern), so this is a much
// narrower edge case — noted for a future task, not opened as its own numbered item.
//
// Uses a distinctive DiffuseColor=(0.6,0.4,0.8) (not white) so the 3 hypotheses are numerically
// distinct: correct white-fallback multiplied by DiffuseColor (153,102,204), the previous draw's
// real texture retained (120,40,40, same as the first sub-test), or a black-fallback (0,0,0).
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

static const Color kTexColor(200, 100, 50, 255);
static const Vector3 kDiffuse(0.6f, 0.4f, 0.8f);

// Expected with a real texture bound: TextureColor * DiffuseColor.
static const Color kExpectedWithTexture(120, 40, 40, 255);
// Expected with Texture=null: white fallback * DiffuseColor.
static const Color kExpectedNullTexture(153, 102, 204, 255);

class AlphaTestNullTextureTest : public Game
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

    Color renderWith(GraphicsDevice& dev, Texture2D* tex, const VertexPositionTexture (&quad)[6])
    {
        AlphaTestEffect fx(dev);
        fx.setTextureProperty(tex);
        fx.setDiffuseColorProperty(kDiffuse);
        fx.Apply();

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            // Task 896 finding (mirrors the Bgfx sibling's Task 364/884 fix): this quad's
            // winding is culled by the real default RasterizerState once EasyGL pushes it at
            // construction.
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            got = readCenter(dev);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break; // skip blank/black frames
        }
        return got;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        Texture2D tex(dev, 1, 1);
        tex.SetData(&kTexColor, 1);

        const Vector3 tl(-1.0f,  1.0f, 0.0f), bl(-1.0f, -1.0f, 0.0f);
        const Vector3 br( 1.0f, -1.0f, 0.0f), tr( 1.0f,  1.0f, 0.0f);
        const Vector2 uv0(0.0f, 0.0f), uv1(0.0f, 1.0f), uv2(1.0f, 1.0f), uv3(1.0f, 0.0f);
        const VertexPositionTexture quad[6] = {
            { tl, uv0 }, { bl, uv1 }, { br, uv2 },
            { tl, uv0 }, { br, uv2 }, { tr, uv3 },
        };

        // Sub-test 1: real texture bound, establishes the "previous draw" state.
        const Color withTexGot = renderWith(dev, &tex, quad);
        check(matches(withTexGot, kExpectedWithTexture),
              "real texture bound: TextureColor*DiffuseColor", withTexGot, "(120,40,40)");

        // Sub-test 2: Texture=null. Must fall back to white, not retain sub-test 1's texture.
        const Color nullTexGot = renderWith(dev, nullptr, quad);
        check(matches(nullTexGot, kExpectedNullTexture),
              "Texture=null: falls back to white (not the previous draw's stale texture)",
              nullTexGot, "(153,102,204)");
        check(!matches(nullTexGot, kExpectedWithTexture),
              "Texture=null: pixel != previous draw's texture (proves no stale-state leak)",
              nullTexGot, "not (120,40,40)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    AlphaTestNullTextureTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    AlphaTestNullTextureTest game;
    game.Run();
    return game.getResult();
}

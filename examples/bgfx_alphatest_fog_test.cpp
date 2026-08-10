// SPDX-License-Identifier: MS-PL
// Task 888: AlphaTestEffect linear fog pixel integration test — Bgfx renderer.
//
// Fog was a total GPU no-op on Bgfx for AlphaTestEffect (Task 378's finding). Fixed by adding
// u_fogColor/u_fogParams uniforms (shared by every 3D program) and the standard fog blend to
// vs/fs_alpha_test3d.sc (also picked up by vs_alpha_test_colored3d.sc, which shares the same
// fragment shader).
//
// REMED-GFX-005: fog factor corrected to FNA/EasyGL's Task-1111 form (the prior Task 378/888
// (FogEnd - Z) formula was the mirror image and wrong):
//   geomFraction = clamp((Z + FogEnd) / (FogEnd - FogStart), 0, 1)   (raw object-space Z)
//   finalRGB     = mix(FogColor, geomRGB, geomFraction)
// This test's FogStart=-0.9/FogEnd=0.9 range is asymmetric enough that it does NOT collapse under
// the correction: the corrected formula maps z=-0.9 → full fog and z=+0.9 → no fog (the mirror
// mapped them the opposite way), so the two endpoint expectations are simply swapped vs. the
// pre-fix asserts. Each endpoint discriminates the mirror from the correct formula.
//
// Uses a stride-20 VertexPositionTexture quad (Bgfx's alpha_test3d shader requires a_texcoord0,
// so a texture must be bound for this pipeline to be meaningfully exercised) with a white 1x1
// texture (identity factor) and AlphaTestEffect's own default AlphaFunction=Greater/
// ReferenceAlpha=0 (always passes here since combined alpha is always 1.0), isolating the fog
// blend from the alpha-test discard logic entirely. Same z-sweep and expected values as
// examples/easygl_alphatest_fog_test.cpp (Bgfx's OpenGL renderer uses the same [-1,1] clip-space Z
// range as EasyGL, unlike Vulkan's [0,1]).
//
// Bgfx-only note (Task 364/896 finding): RasterizerState::CullNone is required, matching every
// other Bgfx pixel test in this family.
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

static const Color kWhite(255, 255, 255, 255);
static const Vector3 kDiffuse(0.8f, 0.2f, 0.4f);
static const Vector3 kFogColor(0.1f, 0.6f, 0.9f);
static constexpr float kFogStart = -0.9f;
static constexpr float kFogEnd   =  0.9f;

// Expected: mix(FogColor, MaterialColor, clamp((z+FogEnd)/(FogEnd-FogStart),0,1)), where
// MaterialColor = white(identity) * DiffuseColor = DiffuseColor*255 = (204,51,102).
static const Color kExpectedNoFog(204, 51, 102, 255);
static const Color kExpectedFullFog(26, 153, 230, 255);
static const Color kExpectedHalfFog(115, 102, 166, 255);

class AlphaTestFogBgfxTest : public Game
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

    Color renderAtZ(GraphicsDevice& dev, Texture2D& tex, float z)
    {
        AlphaTestEffect fx(dev);
        fx.setTextureProperty(&tex);
        fx.setDiffuseColorProperty(kDiffuse);
        fx.setFogEnabledProperty(true);
        fx.setFogColorProperty(kFogColor);
        fx.setFogStartProperty(kFogStart);
        fx.setFogEndProperty(kFogEnd);
        fx.Apply();

        const Vector3 tl(-1.0f,  1.0f, z), bl(-1.0f, -1.0f, z);
        const Vector3 br( 1.0f, -1.0f, z), tr( 1.0f,  1.0f, z);
        const Vector2 uv0(0.0f, 0.0f), uv1(0.0f, 1.0f), uv2(1.0f, 1.0f), uv3(1.0f, 0.0f);
        const VertexPositionTexture quad[6] = {
            { tl, uv0 }, { bl, uv1 }, { br, uv2 },
            { tl, uv0 }, { br, uv2 }, { tr, uv3 },
        };

        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            dev.GetBackBufferData(&reg, &got, 0, 1);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break; // Bgfx's GetBackBufferData only reliably reflects the first read per
                        // rendered frame (Task 406 finding); this test reads once per frame.
        }
        return got;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        Texture2D tex(dev, 1, 1);
        tex.SetData(&kWhite, 1);

        // REMED-GFX-005: corrected formula maps z=-0.9 → geomFraction=(−0.9+0.9)/1.8=0 → FULL fog
        // (the mirror gave (0.9+0.9)/1.8=1 → material color: the discriminating case).
        const Color fullFogGot = renderAtZ(dev, tex, kFogStart);
        check(matches(fullFogGot, kExpectedFullFog),
              "z=-0.9: full fog color (corrected (z+FogEnd) formula)", fullFogGot, "(26,153,230)");

        // z=0.9 → geomFraction=(0.9+0.9)/1.8=1 → NO fog, material color (mirror gave fog color).
        const Color noFogGot = renderAtZ(dev, tex, kFogEnd);
        check(matches(noFogGot, kExpectedNoFog),
              "z=0.9: unblended material color (corrected formula)", noFogGot, "(204,51,102)");

        const Color halfFogGot = renderAtZ(dev, tex, 0.0f);
        check(matches(halfFogGot, kExpectedHalfFog),
              "z=0 (halfway): 50/50 blend, proves real interpolation not on/off",
              halfFogGot, "(115,102,166)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    AlphaTestFogBgfxTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    AlphaTestFogBgfxTest game;
    game.Run();
    return game.getResult();
}

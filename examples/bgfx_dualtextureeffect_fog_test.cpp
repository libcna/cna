// SPDX-License-Identifier: MS-PL
// Task 888: DualTextureEffect linear fog pixel integration test — Bgfx backend.
//
// Direct port of examples/easygl_dualtextureeffect_fog_test.cpp (Task 388), which found this
// exact gap on Vulkan/Bgfx: fog was a total GPU no-op for DualTextureEffect on both backends
// (Task 888, discovered by Task 378). Fixed by adding u_fogColor/u_fogParams uniforms (shared by
// every 3D program) and the standard fog blend to vs/fs_dual_texture3d.sc.
//
// `Texture2=gray(128,128,128)` deliberately cancels Task 383's `color.rgb *= 2` doubling factor
// (`1(white)*2*0.502(gray)≈1.004`) so the pre-fog material color reduces to ~`DiffuseColor`
// directly, isolating this test's own variable (fog) the same way the EasyGL original does.
//
// A 3-point Z-sweep (z=-0.9 no fog, z=0.9 full fog, z=0 half fog) proves the blend is a genuine
// interpolation, not just an on/off switch. `World`/`View`/`Projection` are all Identity, so raw
// vertex Z is `gl_Position.z` directly (Bgfx's OpenGL backend uses the same [-1,1] clip-space Z
// range as EasyGL, unlike Vulkan's [0,1] -- this is why Vulkan's DualTextureEffect fog is
// deferred to Task 899 rather than needing a Z-range workaround here).
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
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
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

static const Vector3 kDiffuse(0.8f, 0.2f, 0.4f);
static const Vector3 kFogColor(0.1f, 0.6f, 0.9f);
static constexpr float kFogStart = -0.9f;
static constexpr float kFogEnd   =  0.9f;

// Expected: mix(FogColor, MaterialColor, clamp((FogEnd-z)/(FogEnd-FogStart),0,1)), where
// MaterialColor ≈ white*2*gray(0.502)*DiffuseColor ≈ DiffuseColor (the *2/gray cancellation is
// not exact -- 2*128/255=1.00392 -- so expected material color is (205,51,102), not (204,51,102)).
static const Color kExpectedNoFog(205, 51, 102, 255);
static const Color kExpectedFullFog(26, 153, 230, 255);
static const Color kExpectedHalfFog(115, 102, 166, 255);

class DualTextureFogBgfxTest : public Game
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

    Color renderAtZ(GraphicsDevice& dev, Texture2D& texWhite, Texture2D& texGray, float z)
    {
        DualTextureEffect fx(dev);
        fx.setTextureProperty(&texWhite);
        fx.setTexture2Property(&texGray);
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

        const Color kWhite(255, 255, 255, 255);
        const Color kGrayHalf(128, 128, 128, 255);
        Texture2D texWhite(dev, 1, 1); texWhite.SetData(&kWhite, 1);
        Texture2D texGray(dev, 1, 1);  texGray.SetData(&kGrayHalf, 1);

        const Color noFogGot = renderAtZ(dev, texWhite, texGray, kFogStart);
        check(matches(noFogGot, kExpectedNoFog),
              "z=-0.9 (at FogStart): unblended material color", noFogGot, "(205,51,102)");

        const Color fullFogGot = renderAtZ(dev, texWhite, texGray, kFogEnd);
        check(matches(fullFogGot, kExpectedFullFog),
              "z=0.9 (at FogEnd): pure fog color", fullFogGot, "(26,153,230)");

        const Color halfFogGot = renderAtZ(dev, texWhite, texGray, 0.0f);
        check(matches(halfFogGot, kExpectedHalfFog),
              "z=0 (halfway): 50/50 blend, proves real interpolation not on/off",
              halfFogGot, "(115,102,166)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    DualTextureFogBgfxTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    DualTextureFogBgfxTest game;
    game.Run();
    return game.getResult();
}

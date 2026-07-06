// SPDX-License-Identifier: MS-PL
// Task 378: AlphaTestEffect fog behavior (EasyGL backend).
//
// REAL BUG FOUND AND FIXED by writing this test: `AlphaTestEffect::FillGpuDrawParams()` never
// forwarded `FogEnabled`/`FogColor`/`FogStart`/`FogEnd` to the GPU at all — fog was a total no-op
// for `AlphaTestEffect` regardless of what the user set, even though EasyGL's shared per-stride
// shaders (`EnsureColored3DProgram()` for this stride-16 case, the same shader `BasicEffect` also
// uses) already fully implement fog generically (`uFogEnabled`/`uFogColor`/`uFogStart`/`uFogEnd`
// uniforms + `mix(uFogColor,FragColor.rgb,vFogFactor)` blend, wired since long before this task).
// Fixed by forwarding these 4 fields in `FillGpuDrawParams()`, mirroring `BasicEffect`'s identical
// pattern exactly — no shader changes needed on EasyGL.
//
// CNA's fog formula (EasyGL-specific, simpler than FNA's WorldViewProj-derived `FogVector` dot
// product but equivalent in effect for these axis-aligned test cases): `vFogFactor =
// clamp((FogEnd-z)/(FogEnd-FogStart), 0, 1)` per-vertex (raw object-space Z, `World`/`View`/
// `Projection` are all Identity in this test — meaning `gl_Position.z` equals raw vertex `z`
// directly, unprojected, so `z` must stay within OpenGL's valid clip-space NDC range of `[-1,1]`
// or the primitive is frustum-clipped away entirely and never reaches the fragment shader at all;
// `FogStart`/`FogEnd` are chosen inside that range accordingly, not at FNA-typical world-space
// distances), then `FragColor.rgb = mix(FogColor, original, vFogFactor)` — `vFogFactor=1`
// (`z<=FogStart`) shows the original color unblended; `vFogFactor=0` (`z>=FogEnd`) shows pure fog
// color; values between interpolate. A 3-point sweep (`z=-0.9` no fog, `z=0.9` full fog, `z=0`
// half fog) proves the blend is a genuine interpolation, not just an on/off switch.
//
// CONFIRMED, NOT FIXED HERE (a much larger, pre-existing, project-wide gap discovered while
// investigating this task, unrelated to `AlphaTestEffect` specifically): **fog is completely
// unimplemented in every Vulkan and Bgfx 3D shader** — confirmed by grepping every `.glsl`/`.sc`
// shader file in both backends for "fog" and finding zero matches anywhere, not just in the
// alpha-test-specific shaders Task 377 already flagged. This means `BasicEffect.FogEnabled` (and
// every other stock effect's fog support) is *also* a silent no-op on Vulkan/Bgfx today, despite
// `BasicEffect::FillGpuDrawParams()` already (and correctly) forwarding the fog fields — the C++
// side was never the problem there, only the GPU side. Implementing real fog on Vulkan/Bgfx means
// adding fog uniforms/varyings and the blend formula to essentially every 3D shader in both
// backends (8+ shader pairs × 2 backends) — a genuinely large, project-wide feature addition, not
// a Task-378-sized fix. Tracked as new **Task 888**; no Vulkan/Bgfx test is added here for the
// same reason Task 377 didn't commit one — it would encode today's confirmed no-op as a "passing"
// assertion that would need updating (and could be forgotten) once Task 888 lands.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

static const Color kMaterialColor(204, 51, 102, 255); // DiffuseColor(0.8,0.2,0.4)*255
static const Vector3 kDiffuse(0.8f, 0.2f, 0.4f);
static const Vector3 kFogColor(0.1f, 0.6f, 0.9f);
// Kept inside OpenGL's valid clip-space NDC range [-1,1] (World/View/Projection are all Identity
// in this test, so raw vertex z IS gl_Position.z — anything outside [-1,1] would be frustum-
// clipped away before the fragment shader ever runs).
static constexpr float kFogStart = -0.9f;
static constexpr float kFogEnd   =  0.9f;

// Expected: mix(FogColor, MaterialColor, clamp((FogEnd-z)/(FogEnd-FogStart),0,1)).
static const Color kExpectedNoFog(204, 51, 102, 255);   // z=-0.9: factor=1, unblended material color
static const Color kExpectedFullFog(26, 153, 230, 255); // z=0.9:  factor=0, pure fog color
static const Color kExpectedHalfFog(115, 102, 166, 255);// z=0:    factor=0.5, halfway blend

class AlphaTestFogTest : public Game
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

    Color renderAtZ(GraphicsDevice& dev, float z)
    {
        AlphaTestEffect fx(dev);
        fx.setDiffuseColorProperty(kDiffuse);
        fx.setFogEnabledProperty(true);
        fx.setFogColorProperty(kFogColor);
        fx.setFogStartProperty(kFogStart);
        fx.setFogEndProperty(kFogEnd);
        fx.Apply();

        const Vector3 tl(-1.0f,  1.0f, z), bl(-1.0f, -1.0f, z);
        const Vector3 br( 1.0f, -1.0f, z), tr( 1.0f,  1.0f, z);
        const VertexPositionColor quad[6] = {
            { tl, kMaterialColor }, { bl, kMaterialColor }, { br, kMaterialColor },
            { tl, kMaterialColor }, { br, kMaterialColor }, { tr, kMaterialColor },
        };

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
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

        const Color noFogGot = renderAtZ(dev, kFogStart);
        check(matches(noFogGot, kExpectedNoFog),
              "z=-0.9 (at FogStart): unblended material color", noFogGot, "(204,51,102)");

        const Color fullFogGot = renderAtZ(dev, kFogEnd);
        check(matches(fullFogGot, kExpectedFullFog),
              "z=0.9 (at FogEnd): pure fog color", fullFogGot, "(26,153,230)");

        const Color halfFogGot = renderAtZ(dev, 0.0f);
        check(matches(halfFogGot, kExpectedHalfFog),
              "z=0 (halfway): 50/50 blend, proves real interpolation not on/off",
              halfFogGot, "(115,102,166)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    AlphaTestFogTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    AlphaTestFogTest game;
    game.Run();
    return game.getResult();
}

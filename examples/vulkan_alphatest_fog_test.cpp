// SPDX-License-Identifier: MS-PL
// Task 888: AlphaTestEffect linear fog pixel integration test — Vulkan backend.
//
// Fog was a total GPU no-op on Vulkan for AlphaTestEffect (Task 378's finding). Fixed by packing
// fog data into alpha_test3d's push constant's previously-unused trailing bytes (28 spare bytes
// were left after Task 887's vertexColorEnabled field) and computing the standard fog blend
// after the existing alpha-discard test in the fragment shader.
//
// Formula (matches EasyGL's already-tested formula exactly, Task 378):
//   fogFactor = clamp((FogEnd - Z) / (FogEnd - FogStart), 0, 1)   (raw object-space Z)
//   finalRGB  = mix(FogColor, geomRGB, fogFactor)
//
// Uses a stride-20 VertexPositionTexture quad (routes through alpha_test3d, the non-colored
// variant) with a white 1x1 texture (identity factor) and AlphaTestEffect's own default
// AlphaFunction=Greater/ReferenceAlpha=0 (always passes here since combined alpha is always 1.0),
// isolating the fog blend from the alpha-test discard logic entirely.
//
// A 3-point Z-sweep (z=-0.9 no fog, z=0.9 full fog, z=0 half fog) proves the blend is a genuine
// interpolation, not just an on/off switch — same methodology and exact expected values as
// examples/easygl_alphatest_fog_test.cpp.
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
// Kept inside Vulkan's valid clip-space Z range [0,1] (NOT OpenGL's [-1,1] -- World/View/
// Projection are all Identity here, so raw vertex z IS gl_Position.z/w before the y-flip, and a
// negative z would be near-plane clipped away before the fragment shader ever runs).
static constexpr float kFogStart = 0.05f;
static constexpr float kFogEnd   = 0.95f;

// Expected: mix(FogColor, MaterialColor, clamp((FogEnd-z)/(FogEnd-FogStart),0,1)), where
// MaterialColor = white(identity) * DiffuseColor = DiffuseColor*255 = (204,51,102).
static const Color kExpectedNoFog(204, 51, 102, 255);
static const Color kExpectedFullFog(26, 153, 230, 255);
static const Color kExpectedHalfFog(115, 102, 166, 255);

class AlphaTestFogVulkanTest : public Game
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

        Texture2D tex(dev, 1, 1);
        tex.SetData(&kWhite, 1);

        const Color noFogGot = renderAtZ(dev, tex, kFogStart);
        check(matches(noFogGot, kExpectedNoFog),
              "z=FogStart: unblended material color", noFogGot, "(204,51,102)");

        const Color fullFogGot = renderAtZ(dev, tex, kFogEnd);
        check(matches(fullFogGot, kExpectedFullFog),
              "z=FogEnd: pure fog color", fullFogGot, "(26,153,230)");

        const Color halfFogGot = renderAtZ(dev, tex, 0.5f);
        check(matches(halfFogGot, kExpectedHalfFog),
              "z=0.5 (halfway): 50/50 blend, proves real interpolation not on/off",
              halfFogGot, "(115,102,166)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    AlphaTestFogVulkanTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    AlphaTestFogVulkanTest game;
    game.Run();
    return game.getResult();
}

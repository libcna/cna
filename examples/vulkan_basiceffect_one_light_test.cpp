// SPDX-License-Identifier: MS-PL
// Task 368: BasicEffect pixel test — LightingEnabled=true with a single DirectionalLight
// (Vulkan backend).
//
// See examples/easygl_basiceffect_one_light_test.cpp for the full FNA-derived expected-output
// derivation and for the `DirectionalLight0.Enabled` bug this test found and fixed in the shared
// `BasicEffect::FillGpuDrawParams()` (common C++ code — one fix covers all 3 backends). Summary:
// expected fragment = (AmbientLightColor + DirectionalLight0.DiffuseColor * max(dot(-Light0Direction,
// N), 0)) * DiffuseColor, texture kept white to isolate the lighting formula.
//
// Uses the same non-saturating NdotL=0.5 normal, back-facing normal, and Enabled=false cases as
// the EasyGL test.
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
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

static const Color kWhite(255, 255, 255, 255);
static const Vector3 kAmbient(0.1f, 0.1f, 0.1f);
static const Vector3 kMaterialDiffuse(0.5f, 0.5f, 1.0f);
static const Vector3 kLightDiffuse(1.0f, 0.6f, 0.2f);
static const Vector3 kLightDir(0.0f, 0.0f, 1.0f);

static const Vector3 kNormalLit(0.8660254f, 0.0f, -0.5f);
static const Vector3 kNormalBackFacing(-0.8660254f, 0.0f, 0.5f);

static const Color kExpectedLit(77, 51, 51, 255);
static const Color kExpectedAmbientOnly(13, 13, 26, 255);
static const Color kFullySaturated(140, 89, 77, 255);
static const Color kAmbientIgnored(64, 38, 26, 255);

class VulkanBasicEffectOneLightTest : public Game
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

    Color renderWith(GraphicsDevice& dev, Texture2D& tex, const Vector3& normal, bool light0Enabled)
    {
        BasicEffect fx(dev);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&tex);
        fx.setLightingEnabledProperty(true);
        fx.setAmbientLightColorProperty(kAmbient);
        fx.setDiffuseColorProperty(kMaterialDiffuse);
        fx.DirectionalLight0.setEnabledProperty(light0Enabled);
        fx.DirectionalLight0.setDirectionProperty(kLightDir);
        fx.DirectionalLight0.setDiffuseColorProperty(kLightDiffuse);

        const Vector3 tl(-1.0f,  1.0f, 0.0f), bl(-1.0f, -1.0f, 0.0f);
        const Vector3 br( 1.0f, -1.0f, 0.0f), tr( 1.0f,  1.0f, 0.0f);
        const Vector2 uv0(0.0f, 0.0f), uv1(0.0f, 1.0f), uv2(1.0f, 1.0f), uv3(1.0f, 0.0f);
        const VertexPositionNormalTexture q[6] = {
            { tl, normal, uv0 }, { bl, normal, uv1 }, { br, normal, uv2 },
            { tl, normal, uv0 }, { br, normal, uv2 }, { tr, normal, uv3 },
        };

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
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

        const Color litGot = renderWith(dev, tex, kNormalLit, true);
        check(matches(litGot, kExpectedLit),
              "One light: NdotL=0.5 partial lit == (Ambient+Light*0.5)*Diffuse",
              litGot, "(77,51,51)");
        check(!matches(litGot, kFullySaturated),
              "One light: pixel != NdotL=1 saturated (dot product is real, not boolean)",
              litGot, "not (140,89,77)");
        check(!matches(litGot, kAmbientIgnored),
              "One light: pixel != ambient-dropped result (Ambient term is additive)",
              litGot, "not (64,38,26)");

        const Color backGot = renderWith(dev, tex, kNormalBackFacing, true);
        check(matches(backGot, kExpectedAmbientOnly),
              "One light: back-facing normal == ambient-only (negative dot clamped to 0)",
              backGot, "(13,13,26)");

        const Color disabledGot = renderWith(dev, tex, kNormalLit, false);
        check(matches(disabledGot, kExpectedAmbientOnly),
              "One light: DirectionalLight0.Enabled=false == ambient-only (light contributes 0)",
              disabledGot, "(13,13,26)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    VulkanBasicEffectOneLightTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanBasicEffectOneLightTest game;
    game.Run();
    return game.getResult();
}

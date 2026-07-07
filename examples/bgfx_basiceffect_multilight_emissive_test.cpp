// SPDX-License-Identifier: MS-PL
// Task 885: BasicEffect pixel test — LightingEnabled=true with DirectionalLight1/DirectionalLight2
// forwarded, and EmissiveColor added on the lit path (Bgfx backend).
//
// See examples/easygl_basiceffect_multilight_emissive_test.cpp for the full FNA-derived expected-
// output derivation and for the previously-missing DirectionalLight1/DirectionalLight2/
// EmissiveColor forwarding this task found and fixed in the shared `BasicEffect::
// FillGpuDrawParams()` (common C++ code — one fix covers all 3 backends) plus each backend's own
// lit shader (EasyGL/Bgfx here; Vulkan's push-constant-budget expansion tracked separately as a
// follow-up, since its 128-byte push constant is already fully used by the shared Ext3D layout).
//
// Per Task 364's finding (tracked as Task 896, not fixed there or here): Bgfx's default
// RasterizerState cull state (`BGFX_STATE_CULL_CCW`) is the only one of the 3 backends that
// actually matches FNA's real `CullCounterClockwiseFace` default, so it silently culls the
// standard NDC quad winding used throughout this pixel-test family unless `RasterizerState::
// CullNone` is set explicitly — worked around here identically to Tasks 364-369's own Bgfx tests.
//
// Uses the same 3 checks as the EasyGL test.
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
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

static const Color kWhite(255, 255, 255, 255);
static const Vector3 kAmbient(0.05f, 0.05f, 0.05f);
static const Vector3 kMaterialDiffuse(1.0f, 1.0f, 1.0f);
static const Vector3 kEmissive(0.10f, 0.05f, 0.02f);
static const Vector3 kLightDir(0.0f, 0.0f, 1.0f);
static const Vector3 kLight1DirOffAxis(1.0f, 0.0f, 0.0f);
static const Vector3 kLight0Diffuse(0.6f, 0.0f, 0.0f);
static const Vector3 kLight1Diffuse(0.0f, 0.6f, 0.0f);
static const Vector3 kLight2Diffuse(0.0f, 0.0f, 0.6f);

// dot(-kLightDir, N) = 0.5 for all 3 lights when they share kLightDir.
static const Vector3 kNormal(0.8660254f, 0.0f, -0.5f);

// Check 1: (Ambient + 0.5*L0 + 0.5*L1 + 0.5*L2) + Emissive = (0.45,0.40,0.37) -> *255.
static const Color kExpectedAllLights(115, 102, 94, 255);
// Check 2: light2 disabled -> blue channel's 0.5*L2 term drops out = (0.45,0.40,0.07) -> *255.
static const Color kExpectedLight2Disabled(115, 102, 18, 255);
// Check 3: light1 rotated off-axis (NdotL1=0) -> green channel's 0.5*L1 term drops out =
// (0.45,0.10,0.37) -> *255.
static const Color kExpectedLight1OffAxis(115, 26, 94, 255);

class BasicEffectMultiLightEmissiveTest : public Game
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

    Color renderWith(GraphicsDevice& dev, Texture2D& tex, bool light2Enabled, const Vector3& light1Dir)
    {
        BasicEffect fx(dev);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&tex);
        fx.setLightingEnabledProperty(true);
        fx.setAmbientLightColorProperty(kAmbient);
        fx.setDiffuseColorProperty(kMaterialDiffuse);
        fx.setEmissiveColorProperty(kEmissive);

        fx.DirectionalLight0.setEnabledProperty(true);
        fx.DirectionalLight0.setDirectionProperty(kLightDir);
        fx.DirectionalLight0.setDiffuseColorProperty(kLight0Diffuse);

        fx.DirectionalLight1.setEnabledProperty(true);
        fx.DirectionalLight1.setDirectionProperty(light1Dir);
        fx.DirectionalLight1.setDiffuseColorProperty(kLight1Diffuse);

        fx.DirectionalLight2.setEnabledProperty(light2Enabled);
        fx.DirectionalLight2.setDirectionProperty(kLightDir);
        fx.DirectionalLight2.setDiffuseColorProperty(kLight2Diffuse);

        const Vector3 tl(-1.0f,  1.0f, 0.0f), bl(-1.0f, -1.0f, 0.0f);
        const Vector3 br( 1.0f, -1.0f, 0.0f), tr( 1.0f,  1.0f, 0.0f);
        const Vector2 uv0(0.0f, 0.0f), uv1(0.0f, 1.0f), uv2(1.0f, 1.0f), uv3(1.0f, 0.0f);
        const VertexPositionNormalTexture q[6] = {
            { tl, kNormal, uv0 }, { bl, kNormal, uv1 }, { br, kNormal, uv2 },
            { tl, kNormal, uv0 }, { br, kNormal, uv2 }, { tr, kNormal, uv3 },
        };

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            // See Task 364/896 finding: Bgfx's default cull state culls this quad's winding,
            // unlike EasyGL/Vulkan's own defaults.
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
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

        const Color allLightsGot = renderWith(dev, tex, true, kLightDir);
        check(matches(allLightsGot, kExpectedAllLights),
              "All 3 lights + EmissiveColor == (Ambient+0.5*(L0+L1+L2))+Emissive",
              allLightsGot, "(115,102,94)");

        const Color light2OffGot = renderWith(dev, tex, false, kLightDir);
        check(matches(light2OffGot, kExpectedLight2Disabled),
              "DirectionalLight2.Enabled=false zeroes its contribution (blue channel drops)",
              light2OffGot, "(115,102,18)");

        const Color light1OffAxisGot = renderWith(dev, tex, true, kLight1DirOffAxis);
        check(matches(light1OffAxisGot, kExpectedLight1OffAxis),
              "DirectionalLight1 uses its own Direction field (green channel drops when rotated off-axis)",
              light1OffAxisGot, "(115,26,94)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BasicEffectMultiLightEmissiveTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BasicEffectMultiLightEmissiveTest game;
    game.Run();
    return game.getResult();
}

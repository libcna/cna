// SPDX-License-Identifier: MS-PL
// Task 365: BasicEffect pixel test — VertexColorEnabled=true, no texture, vertex color
// multiplication (Vulkan renderer).
//
// See examples/easygl_basiceffect_vertexcolor_enabled_test.cpp for the full FNA-derived expected-
// output derivation. Summary: with LightingEnabled=false, TextureEnabled=false (both real FNA
// defaults) and VertexColorEnabled=true (explicitly set here), BasicEffect's shader must output
// DiffuseColor*Alpha*VertexColor, component-wise (Common.fxh's ComputeCommonVSOutput() sets
// vout.Diffuse = DiffuseColor, then VSBasicVcNoFog additionally does
// `vout.Diffuse *= vin.Color`). This is the `true` branch of the same VertexColorEnabled gate
// that Task 364 added to Vulkan's stride==16 `colored3d` pipeline (which had previously ignored
// both DiffuseColor and the flag entirely) — this test exercises the pre-existing multiply that
// Task 364 preserved rather than the newly-added skip branch.
//
// Uses a distinctive per-vertex color (200,100,50,200) and DiffuseColor (0.8,0.4,0.6) chosen so
// the correct component-wise product (160,40,30) is numerically distinct from either input alone
// (DiffuseColor-only would read back as (204,102,153); VertexColor-only as (200,100,50)).
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

static const Color kVertexColor(200, 100, 50, 200);
static const Vector3 kDiffuse(0.8f, 0.4f, 0.6f);

static const Color kExpected(160, 40, 30, 255);
static const Color kDiffuseOnly(204, 102, 153, 255);
static const Color kVertexOnly(200, 100, 50, 255);

class VulkanBasicEffectVertexColorEnabledTest : public Game
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

        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.setDiffuseColorProperty(kDiffuse);

        const Vector3 tl(-1.0f,  1.0f, 0.0f), bl(-1.0f, -1.0f, 0.0f);
        const Vector3 br( 1.0f, -1.0f, 0.0f), tr( 1.0f,  1.0f, 0.0f);
        const VertexPositionColor q[6] = {
            { tl, kVertexColor }, { bl, kVertexColor }, { br, kVertexColor },
            { tl, kVertexColor }, { br, kVertexColor }, { tr, kVertexColor },
        };

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            // Task 896 finding (mirrors the Bgfx sibling's Task 364/884 fix): the standard NDC
            // quad winding used throughout this pixel-test family is culled once the real
            // default RasterizerState reaches the GPU.
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
            got = readCenter(dev);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break; // skip blank/black frames
        }

        check(matches(got, kExpected), "VertexColorEnabled=true: pixel == VertexColor*DiffuseColor",
              got, "(160,40,30)");
        check(!matches(got, kDiffuseOnly), "VertexColorEnabled=true: pixel != DiffuseColor alone",
              got, "not (204,102,153)");
        check(!matches(got, kVertexOnly), "VertexColorEnabled=true: pixel != VertexColor alone",
              got, "not (200,100,50)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    VulkanBasicEffectVertexColorEnabledTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanBasicEffectVertexColorEnabledTest game;
    game.Run();
    return game.getResult();
}

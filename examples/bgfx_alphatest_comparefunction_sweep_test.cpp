// SPDX-License-Identifier: MS-PL
// Task 375: AlphaTestEffect all CompareFunction modes — threshold sweep (Bgfx backend).
//
// See examples/easygl_alphatest_comparefunction_sweep_test.cpp for the full derivation. This is
// the first AlphaTestEffect CompareFunction pixel test on Bgfx at all (Task 190 was EasyGL-only).
//
// Per Task 364's finding (tracked as Task 884, not fixed there or here): Bgfx's default
// RasterizerState cull state (`BGFX_STATE_CULL_CCW`) is the only one of the 3 backends that
// actually matches FNA's real `CullCounterClockwiseFace` default, so it silently culls the
// standard NDC quad winding used throughout this pixel-test family unless `RasterizerState::
// CullNone` is set explicitly — worked around here identically to prior Bgfx tests.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static const Color kBlack(0, 0, 0, 255);
static const Color kWhite(255, 255, 255, 255);

static constexpr int kReference = 128;
static constexpr float kBelow = 64.0f  / 255.0f;
static constexpr float kAt    = 128.0f / 255.0f;
static constexpr float kAbove = 192.0f / 255.0f;

struct FuncCase
{
    CompareFunction func;
    bool below, at, above;
    const char* name;
};

static const FuncCase kCases[] = {
    { CompareFunction::Always,       true,  true,  true,  "Always"       },
    { CompareFunction::Never,        false, false, false, "Never"        },
    { CompareFunction::Less,         true,  false, false, "Less"         },
    { CompareFunction::LessEqual,    true,  true,  false, "LessEqual"    },
    { CompareFunction::Equal,        false, true,  false, "Equal"        },
    { CompareFunction::NotEqual,     true,  false, true,  "NotEqual"     },
    { CompareFunction::GreaterEqual, false, true,  true,  "GreaterEqual" },
    { CompareFunction::Greater,      false, false, true,  "Greater"      },
};

class BgfxAlphaTestCompareFunctionSweepTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int  pass_ = 0;
    int  fail_ = 0;

    bool runOne(GraphicsDevice& dev, Texture2D& tex, const VertexPositionTexture (&quad)[6],
                CompareFunction func, float alpha)
    {
        dev.Clear(kBlack);

        AlphaTestEffect fx(dev);
        fx.setTextureProperty(&tex);
        fx.setAlphaProperty(alpha);
        fx.setReferenceAlphaProperty(kReference);
        fx.setAlphaFunctionProperty(func);
        // See Task 364/884 finding: Bgfx's default cull state culls this quad's winding, unlike
        // EasyGL/Vulkan's own defaults.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        fx.Apply();

        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);

        const auto& vp = dev.getViewportProperty();
        const Rectangle centerReg(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        Color got(0, 0, 0, 0);
        dev.GetBackBufferData(&centerReg, &got, 0, 1);

        return got.getRProperty() > 50;
    }

    void check(bool ok, const char* label, bool got, bool expected)
    {
        if (ok)
        {
            std::printf("[PASS] %s\n", label);
            ++pass_;
        }
        else
        {
            std::printf("[FAIL] %s: got %s, expected %s\n", label,
                got ? "drawn" : "discarded", expected ? "drawn" : "discarded");
            ++fail_;
        }
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        // Note: SetDepthTestEnabled is not exercised here — the Bgfx backend doesn't wire it up
        // yet (throws), same as examples/bgfx_env_map_test.cpp. Not needed: each iteration clears
        // depth via dev.Clear() before drawing a single full-screen quad at z=0.
        dev.setBlendStateProperty(BlendState::Opaque);

        Texture2D whiteTex(dev, 1, 1);
        whiteTex.SetData(&kWhite, 1);

        const VertexPositionTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), Vector2(1.0f, 1.0f) },
        };

        for (const auto& tc : kCases)
        {
            char label[128];

            const bool belowGot = runOne(dev, whiteTex, quad, tc.func, kBelow);
            std::snprintf(label, sizeof(label), "%s: alpha=64/255 (below reference)", tc.name);
            check(belowGot == tc.below, label, belowGot, tc.below);

            const bool atGot = runOne(dev, whiteTex, quad, tc.func, kAt);
            std::snprintf(label, sizeof(label), "%s: alpha=128/255 (at reference)", tc.name);
            check(atGot == tc.at, label, atGot, tc.at);

            const bool aboveGot = runOne(dev, whiteTex, quad, tc.func, kAbove);
            std::snprintf(label, sizeof(label), "%s: alpha=192/255 (above reference)", tc.name);
            check(aboveGot == tc.above, label, aboveGot, tc.above);
        }

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BgfxAlphaTestCompareFunctionSweepTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(200);
        gdm_->setPreferredBackBufferHeightProperty(200);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BgfxAlphaTestCompareFunctionSweepTest game;
    game.Run();
    return game.getResult();
}

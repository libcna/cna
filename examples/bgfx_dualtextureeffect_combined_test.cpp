// SPDX-License-Identifier: MS-PL
// Task 389: DualTextureEffect cross-backend image comparison suite — closes out Phase 44's
// pixel-verification work (Bgfx backend).
//
// See examples/easygl_dualtextureeffect_combined_test.cpp for the full derivation. Summary:
// combines the `color.rgb *= 2` doubling factor (Task 383), two-texture multiply, and
// `DiffuseColor` into one scene — expected fragment = Texture × 2 × Texture2 × DiffuseColor ×
// Alpha. `Texture2=gray(128,128,128)` cancels the doubling factor so expected values reduce to
// ≈`TexelColor × DiffuseColor`. Uses a real 2×2 multi-texel texture with 4 separate draws, each
// holding UV constant across the whole quad to sample one exact texel.
//
// Per Task 364's finding (tracked as Task 884, not fixed there or here): Bgfx's default
// RasterizerState cull state is the only one of the 3 backends that actually matches FNA's
// real CullCounterClockwiseFace default, so it silently culls the standard NDC quad winding
// used throughout this pixel-test family unless RasterizerState::CullNone is set
// explicitly -- worked around here identically to prior Bgfx tests.
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

static const Color kTexels[4] = {
    Color(200, 100, 50,  255),
    Color( 50, 200, 100, 255),
    Color(100,  50, 200, 255),
    Color(150, 150, 150, 255),
};

static const Color kGrayHalf(128, 128, 128, 255);
static const Vector3 kDiffuse(0.6f, 0.4f, 0.8f);

struct Sample { Vector2 uv; Color expected; const char* label; };

static const Sample kSamples[4] = {
    { Vector2(0.25f, 0.25f), Color(120,  40,  40, 255), "top-left texel"     },
    { Vector2(0.75f, 0.25f), Color( 30,  80,  80, 255), "top-right texel"    },
    { Vector2(0.25f, 0.75f), Color( 60,  20, 161, 255), "bottom-left texel"  },
    { Vector2(0.75f, 0.75f), Color( 90,  60, 120, 255), "bottom-right texel" },
};

class BgfxDualTextureCombinedTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int  pass_ = 0;
    int  fail_ = 0;

    void check(bool ok, const char* label, const Color& got, const Color& expected)
    {
        if (ok)
        {
            std::printf("[PASS] %s: got=(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
            ++pass_;
        }
        else
        {
            std::printf("[FAIL] %s: got=(%d,%d,%d) expected=(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(),
                expected.getRProperty(), expected.getGProperty(), expected.getBProperty());
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

        Texture2D tex(dev, 2, 2);
        tex.SetData(kTexels, 4);
        Texture2D texGray(dev, 1, 1);
        texGray.SetData(&kGrayHalf, 1);

        const Vector3 tl(-1.0f,  1.0f, 0.0f), bl(-1.0f, -1.0f, 0.0f);
        const Vector3 br( 1.0f, -1.0f, 0.0f), tr( 1.0f,  1.0f, 0.0f);

        for (const auto& s : kSamples)
        {
            DualTextureEffect fx(dev);
            fx.setTextureProperty(&tex);
            fx.setTexture2Property(&texGray);
            fx.setDiffuseColorProperty(kDiffuse);

            const VertexPositionTexture q[6] = {
                { tl, s.uv }, { bl, s.uv }, { br, s.uv },
                { tl, s.uv }, { br, s.uv }, { tr, s.uv },
            };

            Color got(0, 0, 0, 0);
            for (int i = 0; i < 20; ++i)
            {
                dev.Clear(Color(0, 0, 0, 255));
                dev.setBlendStateProperty(BlendState::Opaque);
                // See Task 364/884 finding: Bgfx's default cull state culls this quad's winding,
                // unlike EasyGL/Vulkan's own defaults.
                dev.setRasterizerStateProperty(RasterizerState::CullNone);
                fx.Apply();
                dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
                got = readCenter(dev);
                if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                    break; // skip blank/black frames
            }

            check(matches(got, s.expected), s.label, got, s.expected);
        }

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BgfxDualTextureCombinedTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BgfxDualTextureCombinedTest game;
    game.Run();
    return game.getResult();
}

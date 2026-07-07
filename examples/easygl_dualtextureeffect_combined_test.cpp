// SPDX-License-Identifier: MS-PL
// Task 389: DualTextureEffect cross-backend image comparison suite — closes out Phase 44's
// pixel-verification work (EasyGL backend).
//
// Combines everything Tasks 383-388 individually verified into one scene, to prove the fixes
// compose correctly rather than only working in isolation: the `color.rgb *= 2` doubling factor
// on `Texture` (Task 383), `DiffuseColor` (Task 383/384), and `Alpha`'s premultiplication (Task
// 385, exercised here at Alpha=1, a degenerate-but-still-exercised case since Task 385 already
// dedicated-verified the non-degenerate case). Expected fragment =
// **Texture × 2 × Texture2 × DiffuseColor × Alpha**, component-wise.
//
// Uses a real 2×2 multi-texel texture for `Texture` (mirrors Task 370's `BasicEffect` capstone
// innovation — the first `DualTextureEffect` test in this project to use a real multi-texel
// texture rather than a 1×1 solid color), proving per-vertex UV correctly reaches the right
// texel through the full doubling+two-texture+diffuse pipeline. `Texture2=gray(128,128,128)`
// deliberately cancels the doubling factor (`2×128/255≈1.004`, same compensation technique as
// Tasks 385/386/388) so the expected values reduce to ≈`TexelColor × DiffuseColor` — CNA's
// `DualTextureEffect` shares one UV set across both texture slots (unlike FNA's real
// `VSInputTx2`, which has independent `TexCoord`/`TexCoord2`), a pre-existing, already-consistent
// simplification across every prior `DualTextureEffect` test in this project, not something this
// capstone changes or is scoped to fix.
//
// 4 separate draws sample each of the 4 texels via a UV held constant across the whole quad
// (avoiding bilinear-filtering ambiguity by sampling exactly at each texel's center), each
// compared against its own independently-derived expected value.
//
// Fog is deliberately excluded from this combined test: Task 388 fixed it on EasyGL only, but
// Vulkan/Bgfx have zero fog GPU implementation (Task 888) — including it here would make this
// capstone's shared assertions diverge per backend, defeating its own purpose.
//
// This is the capstone test for Phase 44's pixel-verification work (Tasks 383-388): identical
// assertions run unmodified (up to standard per-backend workarounds) on Vulkan and Bgfx.
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

// 2x2 texture, row-major: [0]=top-left, [1]=top-right, [2]=bottom-left, [3]=bottom-right.
static const Color kTexels[4] = {
    Color(200, 100, 50,  255),
    Color( 50, 200, 100, 255),
    Color(100,  50, 200, 255),
    Color(150, 150, 150, 255),
};

static const Color kGrayHalf(128, 128, 128, 255);
static const Vector3 kDiffuse(0.6f, 0.4f, 0.8f);

struct Sample { Vector2 uv; Color expected; const char* label; };

// Expected = TexelColor * 2 * (128/255) * DiffuseColor ≈ TexelColor * DiffuseColor (the doubling
// factor and gray-cancellation combine to ~1.00392, not exactly 1 -- absorbed by tolerance).
static const Sample kSamples[4] = {
    { Vector2(0.25f, 0.25f), Color(120,  40,  40, 255), "top-left texel"     },
    { Vector2(0.75f, 0.25f), Color( 30,  80,  80, 255), "top-right texel"    },
    { Vector2(0.25f, 0.75f), Color( 60,  20, 161, 255), "bottom-left texel"  },
    { Vector2(0.75f, 0.75f), Color( 90,  60, 120, 255), "bottom-right texel" },
};

class DualTextureCombinedTest : public Game
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

            // UV held constant across the whole quad so every pixel samples the exact same
            // texel — isolates "does this UV reach the right texel" from filtering/gradients.
            const VertexPositionTexture q[6] = {
                { tl, s.uv }, { bl, s.uv }, { br, s.uv },
                { tl, s.uv }, { br, s.uv }, { tr, s.uv },
            };

            Color got(0, 0, 0, 0);
            for (int i = 0; i < 20; ++i)
            {
                dev.Clear(Color(0, 0, 0, 255));
                dev.setBlendStateProperty(BlendState::Opaque);
                fx.Apply();
                // Task 896 finding (mirrors the Bgfx sibling's Task 364/884 fix): this quad's winding
                // is culled by the real default RasterizerState once EasyGL pushes it at construction.
                dev.setRasterizerStateProperty(RasterizerState::CullNone);
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
    DualTextureCombinedTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    DualTextureCombinedTest game;
    game.Run();
    return game.getResult();
}

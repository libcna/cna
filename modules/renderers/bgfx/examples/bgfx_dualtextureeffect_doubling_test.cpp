// SPDX-License-Identifier: MS-PL
// Task 383: verify DualTextureEffect's two-texture blend formula on Bgfx, including FNA's
// `color.rgb *= 2` doubling factor. See examples/easygl_dualtextureeffect_doubling_test.cpp
// for the full derivation and for the real bug this test found and fixed: CNA's Bgfx/EasyGL/
// Vulkan dual-texture shaders all multiplied `texture0 * texture1 * diffuse` directly,
// silently missing FNA's `*2` factor on texture0's RGB channels -- invisible to every prior
// DualTextureEffect test (Tasks 133/135/191/293/294/296/297) since they all used pure
// 0/1-saturated texture values, where a missing `*2` clamps right back to the same result.
// This is also the **first-ever** Bgfx pixel-integration test for DualTextureEffect at all
// (no `examples/bgfx_dual*texture*` file existed before this task). Fixed by adding
// `base.rgb *= 2.0;` to fs_dual_texture3d.sc (RGB only, matching FNA's `color.rgb *= 2`,
// alpha untouched), then regenerating bgfx_shaders.hpp.
//
// Per Task 364's finding (tracked as Task 884, not fixed there or here): Bgfx's default
// RasterizerState cull state is the only one of the 3 renderers that actually matches FNA's
// real `CullCounterClockwiseFace` default, so it silently culls the standard NDC quad winding
// used throughout this pixel-test family unless `RasterizerState::CullNone` is set
// explicitly -- worked around here identically to prior Bgfx tests.
//
// Exit code 0 = all PASS, 1 = any FAIL.

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

namespace
{
    bool closeTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }

    bool colourMatch(Color got, Color want, int tol = 20)
    {
        return closeTo(got.getRProperty(), want.getRProperty(), tol)
            && closeTo(got.getGProperty(), want.getGProperty(), tol)
            && closeTo(got.getBProperty(), want.getBProperty(), tol);
    }
}

static constexpr int kSize = 64;

class BgfxDualTextureDoublingTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_   = false;
    int  result_ = 0;

    void check(bool cond, const char* label, Color got, Color want)
    {
        if (cond)
            std::printf("[PASS] %s: got=(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
        else
        {
            std::printf("[FAIL] %s: got=(%d,%d,%d), expected≈(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(),
                want.getRProperty(), want.getGProperty(), want.getBProperty());
            result_ = 1;
        }
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    Color drawAndRead(GraphicsDevice& dev, Texture2D& tex0, Texture2D& tex1,
                       const Vector3& diffuse, const VertexPositionTexture (&quad)[6])
    {
        DualTextureEffect fx(dev);
        fx.setTextureProperty(&tex0);
        fx.setTexture2Property(&tex1);
        fx.setDiffuseColorProperty(diffuse);
        fx.Apply();

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            // See Task 364/884 finding: Bgfx's default cull state culls this quad's winding,
            // unlike EasyGL/Vulkan's own defaults.
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
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
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        const Color kWhite(255, 255, 255, 255);
        const Color kGray100(100, 100, 100, 255);

        Texture2D texWhite(dev, 1, 1); texWhite.SetData(&kWhite, 1);
        Texture2D texGray(dev, 1, 1);  texGray.SetData(&kGray100, 1);

        const VertexPositionTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), Vector2(1.0f, 1.0f) },
        };

        // (a) tex0=gray(100), tex1=white, diffuse=white(identity) → doubling factor isolated.
        Color a = drawAndRead(dev, texGray, texWhite, Vector3(1.0f, 1.0f, 1.0f), quad);
        check(colourMatch(a, Color(200, 200, 200, 255)),
              "(a) tex0=gray(100)×2, tex1=white → gray(200)", a, Color(200, 200, 200, 255));

        // (b) tex0=white, tex1=white, diffuse=red → red (original task-title case).
        Color b = drawAndRead(dev, texWhite, texWhite, Vector3(1.0f, 0.0f, 0.0f), quad);
        check(colourMatch(b, Color(255, 0, 0, 255)),
              "(b) two white textures, diffuse=red → red", b, Color(255, 0, 0, 255));

        Exit();
    }

public:
    BgfxDualTextureDoublingTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxDualTextureDoublingTest game;
    game.Run();
    return game.getResult();
}

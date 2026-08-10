// SPDX-License-Identifier: MS-PL
// Task 383: verify DualTextureEffect's two-texture blend formula on EasyGL, including
// FNA's `color.rgb *= 2` doubling factor that every prior DualTextureEffect test (Tasks
// 133/135/191/293/294/296/297) missed entirely, since all of them used pure 0/1-saturated
// texture values -- a missing `*2` is invisible when 1*2 clamps right back to 1.
//
// FNA's PSDualTexture (DualTextureEffect.fx):
//   float4 color = SAMPLE_TEXTURE(Texture, pin.TexCoord);
//   float4 overlay = SAMPLE_TEXTURE(Texture2, pin.TexCoord2);
//   color.rgb *= 2;
//   color *= overlay * pin.Diffuse;
//
// Found and fixed a real bug while writing this test: CNA's EasyGL/Vulkan/Bgfx dual-texture
// shaders all multiplied `texture0 * texture1 * diffuse` directly, silently missing FNA's
// `*2` factor on texture0's RGB channels entirely -- affecting every non-saturated
// DualTextureEffect draw across all 3 renderers (a real pixel-fidelity gap, not just a test
// gap). Fixed by adding `base.rgb *= 2.0;` (RGB only, matching FNA's `color.rgb *= 2`,
// alpha untouched) to all 3 renderers' dual-texture fragment shaders.
//
// (a) tex0=gray(100,100,100), tex1=white, diffuse=default(white,alpha=1):
//     correct: 100/255 * 2 * 1 * 1 = 200/255 -> (200,200,200)
//     buggy (no doubling): 100/255 * 1 * 1 = 100/255 -> (100,100,100)
//     A 100-unit-of-255 separation -- unambiguous discriminating power.
// (b) tex0=white, tex1=white, diffuse=red (the original task-title case): both hypotheses
//     saturate to the same (255,0,0) result (2*1*1 and 1*1*1 both clamp to 1 on the R
//     channel), so this sub-case is a plain regression/sanity check, not discriminating --
//     the doubling bug is only visible in sub-case (a).
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

class DualTextureDoublingTest : public Game
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
        const auto& vp = dev.getViewportProperty();
        const Rectangle reg(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);

        const Color kBlack(0, 0, 0, 255);
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

        // (a) tex0=gray(100), tex1=white, diffuse=default → doubling factor isolated.
        {
            dev.Clear(kBlack);
            DualTextureEffect fx(dev);
            fx.setTextureProperty(&texGray);
            fx.setTexture2Property(&texWhite);
            fx.Apply();
            // Task 896 finding (mirrors the Bgfx sibling's Task 364/884 fix): this quad's winding
            // is culled by the real default RasterizerState once EasyGL pushes it at construction.
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            Color got = readCenter(dev);
            check(colourMatch(got, Color(200, 200, 200, 255)),
                  "(a) tex0=gray(100)×2, tex1=white → gray(200)", got, Color(200, 200, 200, 255));
        }

        // (b) tex0=white, tex1=white, diffuse=red → red (original task-title case).
        {
            dev.Clear(kBlack);
            DualTextureEffect fx(dev);
            fx.setTextureProperty(&texWhite);
            fx.setTexture2Property(&texWhite);
            fx.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
            fx.Apply();
            // Task 896 finding (mirrors the Bgfx sibling's Task 364/884 fix): this quad's winding
            // is culled by the real default RasterizerState once EasyGL pushes it at construction.
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            Color got = readCenter(dev);
            check(colourMatch(got, Color(255, 0, 0, 255)),
                  "(b) two white textures, diffuse=red → red", got, Color(255, 0, 0, 255));
        }

        Exit();
    }

public:
    DualTextureDoublingTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    DualTextureDoublingTest game;
    game.Run();
    return game.getResult();
}

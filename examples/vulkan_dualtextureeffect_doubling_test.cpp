// SPDX-License-Identifier: MS-PL
// Task 383: verify DualTextureEffect's two-texture blend formula on Vulkan, including FNA's
// `color.rgb *= 2` doubling factor. See examples/easygl_dualtextureeffect_doubling_test.cpp
// for the full derivation and for the real bug this test found and fixed: CNA's Vulkan/
// EasyGL/Bgfx dual-texture shaders all multiplied `texture0 * texture1 * diffuse` directly,
// silently missing FNA's `*2` factor on texture0's RGB channels -- invisible to every prior
// DualTextureEffect test (Tasks 133/135/191/293/294/296/297) since they all used pure
// 0/1-saturated texture values, where a missing `*2` clamps right back to the same result.
// Fixed by adding `tex1.rgb *= 2.0;` to dual_texture3d.frag.glsl (RGB only, matching FNA's
// `color.rgb *= 2`, alpha untouched), then regenerating spirv_shaders.hpp.
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

class VulkanDualTextureDoublingTest : public Game
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
        // Task 896 finding (mirrors the Bgfx sibling's Task 364/884 fix): this quad's winding
        // is culled under FNA's real default RasterizerState.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

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
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            Color got = readCenter(dev);
            check(colourMatch(got, Color(255, 0, 0, 255)),
                  "(b) two white textures, diffuse=red → red", got, Color(255, 0, 0, 255));
        }

        Exit();
    }

public:
    VulkanDualTextureDoublingTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    VulkanDualTextureDoublingTest game;
    game.Run();
    return game.getResult();
}

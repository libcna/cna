// SPDX-License-Identifier: MS-PL
// Task 386: verify DualTextureEffect's first texture (`Texture`, slot 0) null behavior on
// Vulkan. See examples/easygl_dualtextureeffect_null_texture0_test.cpp for the full
// derivation. Verify-only, zero bugs expected: source-reading confirmed
// VulkanRenderer::DrawIndexedPrimitivesEx's dual-texture branch already falls back
// to `defaultWhiteView_` for `params.texture0` when null
// (`v0 = vs0 ? vs0->GetVkImageView() : defaultWhiteView_;`) -- this test empirically
// confirms that with a real pixel readback.
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

class VulkanDualTextureNullTexture0Test : public Game
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
        const Color kDistinctivePrev(200, 20, 20, 255); // "previous draw" texture0
        const Color kTex2(80, 40, 120, 255);            // non-saturated Texture2

        Texture2D texPrev(dev, 1, 1); texPrev.SetData(&kDistinctivePrev, 1);
        Texture2D tex2(dev, 1, 1);    tex2.SetData(&kTex2, 1);

        const VertexPositionTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), Vector2(1.0f, 1.0f) },
        };

        // First draw: real, distinctive texture0 + texture2 -- establishes "previous draw" state.
        dev.Clear(kBlack);
        {
            DualTextureEffect fx(dev);
            fx.setTextureProperty(&texPrev);
            fx.setTexture2Property(&tex2);
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
        }

        // Second draw: Texture=null, Texture2=kTex2 -- the actual behavior under test.
        dev.Clear(kBlack);
        {
            DualTextureEffect fx(dev);
            fx.setTextureProperty(nullptr);
            fx.setTexture2Property(&tex2);
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
        }

        Color got = readCenter(dev);
        check(colourMatch(got, Color(160, 80, 240, 255)),
              "Texture=null falls back to white (not the previous draw's texture)",
              got, Color(160, 80, 240, 255));
        check(!colourMatch(got, kDistinctivePrev),
              "Texture=null: pixel != previous draw's texture (proves no stale-state leak)",
              got, kDistinctivePrev);

        Exit();
    }

public:
    VulkanDualTextureNullTexture0Test()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    VulkanDualTextureNullTexture0Test game;
    game.Run();
    return game.getResult();
}

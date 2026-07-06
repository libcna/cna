// SPDX-License-Identifier: MS-PL
// Task 387: verify DualTextureEffect's second texture (`Texture2`, slot 1) null behavior.
//
// See examples/easygl_dualtextureeffect_null_texture0_test.cpp (Task 386) for the full
// derivation. Task 386's source-reading found EasyGL and Vulkan already correctly fall back
// to white for `Texture2` when null, but Bgfx's `texColor3DSampler2_` had NO fallback at
// all -- an unconditional-skip gap Task 379 explicitly flagged as deliberately not fixed
// there (Task 379 only unified the other 7 texture-binding call sites, all of which used
// `texColor3DSampler_`/slot 0, not this second-slot site).
//
// **Real bug found and fixed on Bgfx.** Fixed by adding an else-branch to Bgfx's
// `texColor3DSampler2_` binding, mirroring slot 0's own Task 379 fix exactly.
//
// Draws a real, distinctive `Texture2` FIRST (establishes "previous draw" state), then
// switches to `Texture2=null` with a non-saturated `Texture=(80,40,120)` so 3 hypotheses are
// numerically distinct:
//   correct white-fallback: Texture(80,40,120)*2(doubling)*white(1,1,1) = (160,80,240)
//   stale-previous-texture: would retain the first draw's distinctive Texture2 color
//   black-fallback:         (0,0,0)
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

class DualTextureNullTexture2Test : public Game
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

    void drawQuad(GraphicsDevice& dev, const VertexPositionTexture (&quad)[6])
    {
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
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
        const Color kDistinctivePrev(20, 200, 20, 255); // "previous draw" texture2
        const Color kTex(80, 40, 120, 255);             // non-saturated Texture (slot 0)

        Texture2D tex(dev, 1, 1);      tex.SetData(&kTex, 1);
        Texture2D texPrev(dev, 1, 1);  texPrev.SetData(&kDistinctivePrev, 1);

        const VertexPositionTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), Vector2(1.0f, 1.0f) },
        };

        // First draw: Texture + real, distinctive texture2 -- establishes "previous draw" state.
        dev.Clear(kBlack);
        {
            DualTextureEffect fx(dev);
            fx.setTextureProperty(&tex);
            fx.setTexture2Property(&texPrev);
            fx.Apply();
            drawQuad(dev, quad);
        }

        // Second draw: Texture=kTex, Texture2=null -- the actual behavior under test.
        dev.Clear(kBlack);
        {
            DualTextureEffect fx(dev);
            fx.setTextureProperty(&tex);
            fx.setTexture2Property(nullptr);
            fx.Apply();
            drawQuad(dev, quad);
        }

        Color got = readCenter(dev);
        check(colourMatch(got, Color(160, 80, 240, 255)),
              "Texture2=null falls back to white (not the previous draw's texture)",
              got, Color(160, 80, 240, 255));
        check(!colourMatch(got, kDistinctivePrev),
              "Texture2=null: pixel != previous draw's texture (proves no stale-state leak)",
              got, kDistinctivePrev);

        Exit();
    }

public:
    DualTextureNullTexture2Test()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    DualTextureNullTexture2Test game;
    game.Run();
    return game.getResult();
}

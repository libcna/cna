// SPDX-License-Identifier: MS-PL
// Task 757: verify GraphicsDevice.BlendFactor propagation from BlendState on Bgfx.
//
// Bgfx-specific adaptation of examples/easygl_blendstate_blendfactor_test.cpp (Task 309, already
// reused verbatim on Vulkan). Not a verbatim reuse: that file uses
// GraphicsDevice::SetDepthTestEnabled(false), which throws on Bgfx ("SetDepthTestEnabled /
// SetBlend* are not yet wired into bgfx state flags" — a pre-existing, deliberate stub), replaced
// with the equivalent DepthStencilState-based call. Everything else identical — this test does
// exactly 1 Draw + 1 GetBackBufferData read per frame, so Bgfx's own "first read per rendered
// frame" quirk (Task 406) does not apply.
//
// Builds a custom BlendState with ColorSourceBlend=Blend::BlendFactor, ColorDestinationBlend=
// Blend::Zero, and a distinctive BlendFactor colour baked into the state object itself
// (200, 100, 0, 255), then calls ONLY GraphicsDevice.setBlendStateProperty (no separate
// setBlendFactorProperty call) and confirms the device's active blend-constant colour used by
// the GPU already matches the state's own BlendFactor.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

class BgfxBlendStateBlendFactorTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_   = false;
    int  result_ = 1;

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        const auto& vp = dev.getViewportProperty();

        dev.Clear(Color(0, 0, 0, 255));
        DepthStencilState ds;
        ds.setDepthBufferEnableProperty(false);
        dev.setDepthStencilStateProperty(ds);

        BlendState state;
        state.setColorSourceBlendProperty(Blend::BlendFactor);
        state.setColorDestinationBlendProperty(Blend::Zero);
        state.setAlphaSourceBlendProperty(Blend::One);
        state.setAlphaDestinationBlendProperty(Blend::Zero);
        state.setBlendFactorProperty(Color(200, 100, 0, 255));

        // Deliberately no separate dev.setBlendFactorProperty(...) call: the whole point of this
        // test is that setBlendStateProperty alone must propagate the state's own BlendFactor.
        dev.setBlendStateProperty(state);

        const Color kSource(255, 255, 255, 255);
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), kSource },
            { Vector3(-1.0f, -1.0f, 0.0f), kSource },
            { Vector3( 1.0f, -1.0f, 0.0f), kSource },
            { Vector3(-1.0f,  1.0f, 0.0f), kSource },
            { Vector3( 1.0f, -1.0f, 0.0f), kSource },
            { Vector3( 1.0f,  1.0f, 0.0f), kSource },
        };

        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();

        // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real
        // default RasterizerState -- needs CullNone.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);

        const Rectangle reg(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        Color got(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &got, 0, 1);

        const bool ok = got.getRProperty() >= 185 && got.getRProperty() <= 215
                     && got.getGProperty() >= 85  && got.getGProperty() <= 115
                     && got.getBProperty() <= 15;

        std::printf("[%s] centre=(%d,%d,%d), expected ~(200,100,0)\n",
                    ok ? "PASS" : "FAIL", got.getRProperty(), got.getGProperty(), got.getBProperty());
        if (!ok)
        {
            std::printf("[INFO] A mismatch here means BlendState.BlendFactor is not propagated to\n"
                        "       GraphicsDevice when setBlendStateProperty is called.\n");
        }

        result_ = ok ? 0 : 1;
        Exit();
    }

public:
    BgfxBlendStateBlendFactorTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxBlendStateBlendFactorTest game;
    game.Run();
    return game.getResult();
}

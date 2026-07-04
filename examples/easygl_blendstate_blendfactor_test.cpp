// SPDX-License-Identifier: MS-PL
// Task 309: verify GraphicsDevice.BlendFactor propagation from BlendState.
//
// FNA applies a BlendState's own baked-in BlendFactor atomically as part of
// FNA3D_SetBlendState whenever GraphicsDevice.BlendState is assigned (see FNA's
// GraphicsDevice.ApplyState -> FNA3D_SetBlendState(GLDevice, ref nextBlend.state), which passes
// the whole BlendState struct including its BlendFactor field). This test builds a custom
// BlendState with ColorSourceBlend=Blend::BlendFactor, ColorDestinationBlend=Blend::Zero, and a
// distinctive BlendFactor colour baked into the state object itself (200, 100, 0, 255), then
// calls ONLY GraphicsDevice.setBlendStateProperty (no separate setBlendFactorProperty call) and
// confirms the device's active blend-constant colour used by the GPU already matches the state's
// own BlendFactor.
//
// Background: Color(0, 0, 0, 255) (irrelevant, fully overwritten by BlendFactor*source).
// Source: Color(255, 255, 255, 255) (irrelevant colour; only the BlendFactor constant matters
//   since ColorSourceBlend=Blend::BlendFactor multiplies the source colour by the constant, and
//   the source colour here is all-ones so the result is exactly the constant).
// Expected result: centre pixel ~= (200, 100, 0), i.e. the state's own BlendFactor, NOT the
//   device's prior default BlendFactor (White = 255,255,255,255) which would have produced white.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class BlendStateBlendFactorTest : public Game
{
    bool done_   = false;
    int  result_ = 1;

protected:
    void Initialize() override { Game::Initialize(); }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        const auto& vp = dev.getViewportProperty();

        dev.Clear(Color(0, 0, 0, 255));
        dev.SetDepthTestEnabled(false);

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
    int getResult() const { return result_; }
};

int main()
{
    BlendStateBlendFactorTest game;
    game.Run();
    return game.getResult();
}

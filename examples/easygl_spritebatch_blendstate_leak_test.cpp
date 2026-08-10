// SPDX-License-Identifier: MS-PL
// Task 956: verify EasyGLSpriteBatchRenderer::Begin() no longer clobbers the real GL blend state
// with a hardcoded SrcAlpha/OneMinusSrcAlpha, and that a 3D draw issued after a SpriteBatch
// Begin()/End() pair -- without the game explicitly reassigning BlendState -- correctly inherits
// whatever GraphicsDevice.BlendState currently is (matching real FNA: SpriteBatch.Begin() genuinely
// changes GraphicsDevice.BlendState as a lasting side effect, see FNA's SpriteBatch.cs, it does not
// restore the prior state on End()).
//
// Deliberately draws the SpriteBatch sprite in a small corner region, NOT covering the full
// backbuffer, to avoid the separate, unrelated, still-open Task 933 finding ("a full-backbuffer
// SpriteBatch draw before any 3D draw call in the same frame breaks that frame's 3D rendering
// entirely") -- this test is about blend-state persistence, not that bug.
//
// Sequence:
//   1. Clear to mid-gray background BG=(100,100,100,255).
//   2. SpriteBatch.Begin(Deferred, BlendState::Additive) -> draw a small opaque sprite in the
//      corner -> End(). This sets GraphicsDevice.BlendState = Additive as a real, lasting side
//      effect (matching FNA), NOT the SpriteBatch renderer's own now-removed hardcoded blend.
//   3. Without touching BlendState again, draw a full-viewport opaque red 3D quad
//      (VertexColorEnabled BasicEffect, no explicit BlendState/SetBlendState call).
//   4. Read back a pixel far from the sprite's corner.
//
// Expected math at the readback point, with Additive (colorSrc=colorDst=alphaSrc=alphaDst=One)
// genuinely still active: result = red(200,0,0) + gray(100,100,100), clamped = (255,100,100).
// Under the old bug, EasyGLSpriteBatchRenderer::Begin() would have hardcoded
// SrcAlpha/OneMinusSrcAlpha regardless of Additive ever being requested; at the 3D quad's
// alpha=255, that gives dst factor (1-1)=0, dropping the gray background entirely:
// result = (200,0,0) -- G=0 instead of the correct G=100. G is the clean discriminator.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
}

class SpriteBatchBlendStateLeakTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> sb_;
    std::unique_ptr<Texture2D> cornerTex_;
    bool done_   = false;
    int  result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);

        const Color white(255, 255, 255, 255);
        cornerTex_ = std::make_unique<Texture2D>(dev, 1, 1);
        cornerTex_->SetData(&white, 1);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color(100, 100, 100, 255));
        dev.SetDepthTestEnabled(false);

        // Small corner sprite only -- not full-backbuffer, avoids Task 933's unrelated trigger.
        // Sets GraphicsDevice.BlendState = Additive as a real, lasting side effect (FNA-faithful).
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Additive);
        sb_->Draw(*cornerTex_, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 1, 1), Color::White);
        sb_->End();

        // No explicit BlendState reassignment here -- this 3D draw must inherit whatever
        // GraphicsDevice.BlendState currently is (Additive), not a leftover hardcoded value.
        const Color kRed(200, 0, 0, 255);
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), kRed },
            { Vector3(-1.0f, -1.0f, 0.0f), kRed },
            { Vector3( 1.0f, -1.0f, 0.0f), kRed },
            { Vector3(-1.0f,  1.0f, 0.0f), kRed },
            { Vector3( 1.0f, -1.0f, 0.0f), kRed },
            { Vector3( 1.0f,  1.0f, 0.0f), kRed },
        };

        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();

        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);

        const auto& vp = dev.getViewportProperty();
        const Rectangle reg(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        Color got(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &got, 0, 1);

        // G=100 only survives if Additive's dst factor (One) is genuinely still active;
        // the old bug's leftover SrcAlpha/OneMinusSrcAlpha would drop it to G=0 (dst factor
        // (1-1)=0 at alpha=255).
        const bool pass = got.getGProperty() >= 80 && got.getGProperty() <= 120;

        if (pass)
        {
            std::printf("[PASS] BlendState persists from SpriteBatch to a following 3D draw: "
                        "centre=(%d,%d,%d), expected ~(255,100,100)\n",
                        got.getRProperty(), got.getGProperty(), got.getBProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] BlendState persists from SpriteBatch to a following 3D draw: "
                        "centre=(%d,%d,%d), expected G in [80,120] (gray background additively "
                        "preserved). G~0 means the 3D draw inherited a leftover hardcoded "
                        "SrcAlpha/OneMinusSrcAlpha blend from SpriteBatch instead of the real "
                        "GraphicsDevice.BlendState (Additive).\n",
                        got.getRProperty(), got.getGProperty(), got.getBProperty());
        }
        Exit();
    }

public:
    SpriteBatchBlendStateLeakTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    SpriteBatchBlendStateLeakTest game;
    game.Run();
    return game.getResult();
}

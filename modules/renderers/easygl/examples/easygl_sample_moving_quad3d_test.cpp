// SPDX-License-Identifier: MS-PL
// Task 498 (sample 1 of 4 new samples; EasyGL renderer): a classic "moving 3D object" demo --
// a small unlit, vertex-colored BasicEffect quad translated along the World-space X axis by a
// fixed amount every Update() call, over several real frames, proving the full
// Update()-state -> World matrix -> BasicEffect -> EasyGL draw -> backbuffer readback pipeline
// works end to end for genuine 3D rendering (not just SpriteBatch, as Task 730's SDL_Renderer
// samples already proved for 2D).
//
// View/Projection are left at BasicEffect's own real FNA-matching default (Identity), so NDC
// space and BasicEffect's own "world" space coincide -- the screen-space mapping used by
// Viewport::Project (screenX = (ndcX+1)/2*width) applies directly to the raw World-space X
// translation, keeping the expected-pixel math simple and independent of any camera setup.
//
// Quad: 0.3x0.3 (NDC units) unlit, vertex-colored Red square, VertexColorEnabled=true,
// LightingEnabled=false (BasicEffect's own real defaults), so the rendered pixel is the exact
// vertex color with no lighting/texture modulation.
//
// Motion: starts at World-space X=-0.6, +0.3/Update(), 4 Update() calls -> final X=0.6.
// Expected screen X: (x+1)/2*64 -> start ~13, end ~51 (backbuffer is 64x64).
//
// Final-frame checks: (1) the quad is genuinely rendered Red at the END position; (2) the START
// position no longer shows Red (background), proving real per-frame motion, not a static quad
// that happens to cover both positions.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    constexpr int kFrameCount = 4;
    constexpr float kStartX = -0.6f;
    constexpr float kStepX = 0.3f;
    constexpr float kHalf = 0.15f;

    int ndcToScreenX(float ndcX) { return static_cast<int>((ndcX + 1.0f) * 0.5f * kSize); }
}

class MovingQuad3DSample : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<BasicEffect> effect_;

    float x_ = kStartX;
    int frame_ = 0;
    bool done_ = false;
    int pass_ = 0, fail_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    Color readAt(GraphicsDevice& dev, int px, int py)
    {
        Rectangle r(px, py, 1, 1);
        Color c(0, 0, 0, 0);
        dev.GetBackBufferData(&r, &c, 0, 1);
        return c;
    }

    void drawQuad(GraphicsDevice& dev)
    {
        dev.Clear(Color(0, 0, 0, 255));
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        effect_->setWorldProperty(Matrix::CreateTranslation(x_, 0.0f, 0.0f));
        effect_->Apply();

        const Color red(255, 0, 0, 255);
        const VertexPositionColor q[6] = {
            { Vector3(-kHalf,  kHalf, 0.0f), red }, { Vector3(-kHalf, -kHalf, 0.0f), red },
            { Vector3( kHalf, -kHalf, 0.0f), red },
            { Vector3(-kHalf,  kHalf, 0.0f), red }, { Vector3( kHalf, -kHalf, 0.0f), red },
            { Vector3( kHalf,  kHalf, 0.0f), red },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        effect_ = std::make_unique<BasicEffect>(getGraphicsDeviceProperty());
        effect_->VertexColorEnabled = true;
    }

    void Update(GameTime&) override
    {
        if (done_) return;
        x_ += kStepX;
        ++frame_;
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        drawQuad(dev);

        if (frame_ < kFrameCount || done_) return;
        done_ = true;

        const int screenY = kSize / 2;
        const int endScreenX = ndcToScreenX(x_);
        const int startScreenX = ndcToScreenX(kStartX);

        Color endPx(0, 0, 0, 0);
        for (int i = 0; i < 10; ++i)
        {
            endPx = readAt(dev, endScreenX, screenY);
            if (endPx.getRProperty() > 0) break;
            drawQuad(dev); // retry: some drivers need a couple of extra present cycles
        }
        check(endPx.getRProperty() >= 240 && endPx.getGProperty() < 16 && endPx.getBProperty() < 16,
              "Quad genuinely rendered Red at the final Update()-driven World-space position");

        const Color startPx = readAt(dev, startScreenX, screenY);
        check(startPx.getRProperty() < 16,
              "Start position no longer Red -- the quad actually moved, not statically covering both spots");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    MovingQuad3DSample()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    MovingQuad3DSample game;
    game.Run();
    return game.getResult();
}

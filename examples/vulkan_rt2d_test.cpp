// SPDX-License-Identifier: MS-PL
// Task 148: Vulkan integration test — RenderTarget2D full cycle.
//
// Exercises the Vulkan deferred two-phase command recording with a real RT:
//
//   RecordCommandBuffer Phase 1 (RT pass):
//     clear = green, draw red VertexPositionColor quad → RT colour = red
//
//   RecordCommandBuffer Phase 2 (backbuffer pass):
//     clear = green, blit RT as texture via SpriteBatch → screen colour = red
//
//   GetBackBufferData: asserts centre pixel R>=200, G<=50, B<=50.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class VulkanRT2DTest : public Game
{
    std::unique_ptr<SpriteBatch>    sb_;
    std::unique_ptr<RenderTarget2D> rt_;
    bool done_   = false;
    int  result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        sb_ = std::make_unique<SpriteBatch>(device);
        rt_ = std::make_unique<RenderTarget2D>(device,
                                               vp.getWidthProperty(),
                                               vp.getHeightProperty());
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        // Global clear colour: used by both passes in RecordCommandBuffer.
        device.Clear(Color(0, 255, 0, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);

        // --- Phase 1: queue red quad into RT ---
        device.SetRenderTarget(rt_.get());

        // BasicEffect with vertex colour enabled (default) — required so DrawUserPrimitives
        // sees a currentEffect_ and actually queues the draw.
        BasicEffect fx(device);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();

        // Full-screen NDC quad in red (VertexPositionColor, stride=16 → colored3d pipeline).
        // Vulkan Y-flip in the vertex shader preserves full-screen NDC coverage.
        const VertexPositionColor redQuad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), Color(255, 0, 0, 255) },
            { Vector3(-1.0f, -1.0f, 0.0f), Color(255, 0, 0, 255) },
            { Vector3( 1.0f, -1.0f, 0.0f), Color(255, 0, 0, 255) },
            { Vector3(-1.0f,  1.0f, 0.0f), Color(255, 0, 0, 255) },
            { Vector3( 1.0f, -1.0f, 0.0f), Color(255, 0, 0, 255) },
            { Vector3( 1.0f,  1.0f, 0.0f), Color(255, 0, 0, 255) },
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, redQuad, 0, 2);

        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        // --- Phase 2: blit RT as full-screen texture to the backbuffer ---
        // SpriteBatch queues draws with rt=nullptr → processed in Phase 2.
        // After Phase 1 ends the RT render pass, the RT image transitions to
        // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL and can be sampled here.
        sb_->Begin();
        sb_->Draw(*rt_,
                  Rectangle(0, 0, W, H),
                  Rectangle(0, 0, W, H),
                  Color::White);
        sb_->End();

        // GetBackBufferData triggers Present() → RecordCommandBuffer executes
        // both phases; the centre pixel must be red.
        const Rectangle centReg(W / 2, H / 2, 1, 1);
        Color centPx(0, 0, 0, 0);
        device.GetBackBufferData(&centReg, &centPx, 0, 1);

        const bool pass = (centPx.getRProperty() >= 200 &&
                           centPx.getGProperty() <= 50  &&
                           centPx.getBProperty() <= 50);

        if (pass)
        {
            std::printf("[PASS] VulkanRT2D: centre=(%d,%d,%d)\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] VulkanRT2D: centre=(%d,%d,%d), "
                        "expected red (R>=200, G<=50, B<=50)\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty());
        }
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    VulkanRT2DTest game;
    game.Run();
    return game.getResult();
}

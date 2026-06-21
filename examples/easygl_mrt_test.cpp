// SPDX-License-Identifier: MS-PL
// Task 145: EasyGL integration test — Multi-Render-Target (MRT) via SetRenderTargets.
//
// Tests the EasyGL MRT path (mrtFbo_ with 2 color attachments):
//   1. rt0 cleared to red,  rt1 cleared to blue (separate single-RT clears)
//   2. SetRenderTargets({rt0, rt1}) — MRT FBO: 2 draw buffers active
//   3. Draw green quad — colored3D shader (stride=16) writes to layout(location=0)
//      → only attachment 0 (rt0) receives green; rt1 stays blue
//   4. SetRenderTargets({}) — back to default framebuffer
//   5. Blit rt0 (green) to left half, rt1 (blue) to right half via textured3D
//   6. Pixel readback: left quarter should be green, right quarter should be blue
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class MRTTest : public Game
{
    std::unique_ptr<RenderTarget2D> rt0_; // left  — starts red,  overwritten green by MRT draw
    std::unique_ptr<RenderTarget2D> rt1_; // right — starts blue, NOT overwritten (attachment 1)
    bool done_   = false;
    int  result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        rt0_ = std::make_unique<RenderTarget2D>(device, W, H);
        rt1_ = std::make_unique<RenderTarget2D>(device, W, H);

        // Initialize rt0 to red (single-RT clear)
        device.SetRenderTarget(rt0_.get());
        device.Clear(Color(255, 0, 0, 255));
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        // Initialize rt1 to blue (single-RT clear)
        device.SetRenderTarget(rt1_.get());
        device.Clear(Color(0, 0, 255, 255));
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);

        // --- MRT draw: green quad → attachment 0 (rt0) only ---
        device.SetRenderTargets({ RenderTargetBinding(rt0_.get()),
                                  RenderTargetBinding(rt1_.get()) });

        BasicEffect fxGreen(device);
        fxGreen.VertexColorEnabled = true;
        fxGreen.setWorldProperty(Matrix::getIdentityProperty());
        fxGreen.setViewProperty(Matrix::getIdentityProperty());
        fxGreen.setProjectionProperty(Matrix::getIdentityProperty());
        fxGreen.Apply();

        const Color green(0, 255, 0, 255);
        const VertexPositionColor greenQuad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), green },
            { Vector3(-1.0f, -1.0f, 0.0f), green },
            { Vector3( 1.0f, -1.0f, 0.0f), green },
            { Vector3(-1.0f,  1.0f, 0.0f), green },
            { Vector3( 1.0f, -1.0f, 0.0f), green },
            { Vector3( 1.0f,  1.0f, 0.0f), green },
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, greenQuad, 0, 2);

        // --- Back to backbuffer ---
        device.SetRenderTargets({});
        device.Clear(Color(0, 0, 0, 255)); // black background

        // --- Blit rt0 (now green) onto left half ---
        {
            BasicEffect blit(device);
            blit.VertexColorEnabled = false;
            blit.setTextureEnabledProperty(true);
            blit.setTextureProperty(rt0_.get());
            blit.setWorldProperty(Matrix::getIdentityProperty());
            blit.setViewProperty(Matrix::getIdentityProperty());
            blit.setProjectionProperty(Matrix::getIdentityProperty());
            blit.Apply();

            const VertexPositionTexture leftQuad[6] = {
                { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
                { Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f) },
                { Vector3( 0.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
                { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
                { Vector3( 0.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
                { Vector3( 0.0f,  1.0f, 0.0f), Vector2(1.0f, 1.0f) },
            };
            device.DrawUserPrimitives(PrimitiveType::TriangleList, leftQuad, 0, 2);
        }

        // --- Blit rt1 (still blue) onto right half ---
        {
            BasicEffect blit(device);
            blit.VertexColorEnabled = false;
            blit.setTextureEnabledProperty(true);
            blit.setTextureProperty(rt1_.get());
            blit.setWorldProperty(Matrix::getIdentityProperty());
            blit.setViewProperty(Matrix::getIdentityProperty());
            blit.setProjectionProperty(Matrix::getIdentityProperty());
            blit.Apply();

            const VertexPositionTexture rightQuad[6] = {
                { Vector3( 0.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
                { Vector3( 0.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f) },
                { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
                { Vector3( 0.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
                { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
                { Vector3( 1.0f,  1.0f, 0.0f), Vector2(1.0f, 1.0f) },
            };
            device.DrawUserPrimitives(PrimitiveType::TriangleList, rightQuad, 0, 2);
        }

        // --- Pixel readback ---
        const Rectangle leftReg ( W / 4,     H / 2, 1, 1);
        const Rectangle rightReg(3 * W / 4, H / 2, 1, 1);
        Color leftPx(0,0,0,0), rightPx(0,0,0,0);
        device.GetBackBufferData(&leftReg,  &leftPx,  0, 1);
        device.GetBackBufferData(&rightReg, &rightPx, 0, 1);

        // rt0 was overwritten green by MRT draw; rt1 was not touched → still blue
        const bool leftGreen = (leftPx.getRProperty()  <= 50  &&
                                leftPx.getGProperty()  >= 200 &&
                                leftPx.getBProperty()  <= 50);
        const bool rightBlue = (rightPx.getRProperty() <= 50  &&
                                rightPx.getGProperty() <= 50  &&
                                rightPx.getBProperty() >= 200);

        if (leftGreen && rightBlue)
        {
            std::printf("[PASS] MRT: left=(%d,%d,%d) right=(%d,%d,%d)\n",
                        leftPx.getRProperty(),  leftPx.getGProperty(),  leftPx.getBProperty(),
                        rightPx.getRProperty(), rightPx.getGProperty(), rightPx.getBProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] MRT: left=(%d,%d,%d) [expect green], "
                        "right=(%d,%d,%d) [expect blue]\n",
                        leftPx.getRProperty(),  leftPx.getGProperty(),  leftPx.getBProperty(),
                        rightPx.getRProperty(), rightPx.getGProperty(), rightPx.getBProperty());
        }
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    MRTTest game;
    game.Run();
    return game.getResult();
}

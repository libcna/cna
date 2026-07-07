// SPDX-License-Identifier: MS-PL
// Task 335: verify RenderTarget2D's DepthStencilFormat actually produces a real, functional
// depth buffer, not just a stored property value.
//
// Task 331's property test only proves getDepthStencilFormatProperty() returns whatever was
// passed at construction — a plain stored value never connected to any GPU resource. This test
// proves the depth buffer actually GATES draws while rendering INTO the render target itself.
//
// Method: create a RenderTarget2D with DepthFormat::Depth24Stencil8, bind it, clear color+depth,
// draw a near GREEN quad (z=0.2) then a far RED quad (z=0.8) at the SAME screen position with
// DepthStencilState::Default (depth test+write enabled, LessEqual). If the RT's depth buffer is
// real and wired into the pipeline used for offscreen draws, the farther RED quad must be
// REJECTED by the depth test, leaving GREEN. If depth testing is silently bypassed for
// render-target draws (e.g. no depth attachment reaches the pipeline, or depth state is only
// applied for backbuffer draws), RED (drawn last) incorrectly overwrites, and the result is RED.
//
// Unbind, then sample the RT back via SpriteBatch onto the backbuffer — this uses the
// RenderTarget2D sampling-after-unbind path already pixel-verified working on both EasyGL (Task
// 87) and Vulkan (Task 148), isolating "does depth actually function inside the RT" as the only
// new variable under test here.
//
// Task 912 (Bgfx registration): the read-back is retried (Clear+Draw+Read, up to 20x) until
// non-black — Bgfx's GetBackBufferData() only reliably reflects the first read call per rendered
// frame (NEXT.md §5); sampling a freshly-filled-then-unbound RT via SpriteBatch within the same
// un-advanced bgfx frame can read back stale/black content on that very first read. This is the
// same established convention bgfx_rendertarget2d_mip_test.cpp already uses, applied here so this
// shared test can finally be registered for Bgfx too (previously EasyGL/Vulkan only) — confirmed
// this is the sole reason Bgfx originally appeared to fail this exact scenario (investigated as a
// suspected separate bug, tracked as Task 912, and root-caused to this, not a distinct bug).
// EasyGL/Vulkan are unaffected: both already produce the correct pixel on the very first read, so
// the retry loop's condition is satisfied and it breaks after exactly one iteration.
//
// Exit code 0 = PASS (GREEN, nearer quad correctly wins), 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kRTSize = 64;

namespace
{
    void DrawFullQuad(GraphicsDevice& dev, float z, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f,  1.0f, z), color },
            { Vector3(-1.0f, -1.0f, z), color },
            { Vector3( 1.0f, -1.0f, z), color },
            { Vector3(-1.0f,  1.0f, z), color },
            { Vector3( 1.0f, -1.0f, z), color },
            { Vector3( 1.0f,  1.0f, z), color },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }
}

class RenderTarget2DDepthTest : public Game
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
        sb_ = std::make_unique<SpriteBatch>(device);
        rt_ = std::make_unique<RenderTarget2D>(device, kRTSize, kRTSize, false,
                                                SurfaceFormat::Color, DepthFormat::Depth24Stencil8);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.setBlendStateProperty(BlendState::Opaque);

        // --- Pass 1: render into the RT with real depth testing ---
        device.SetRenderTarget(rt_.get());
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, Color(0, 0, 0, 255), 1.0f, 0);

        BasicEffect fx(device);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.VertexColorEnabled = true;
        fx.Apply();

        device.setDepthStencilStateProperty(DepthStencilState::Default);
        // Task 896 finding: DrawFullQuad()'s winding is CCW/back-facing under CNA's real
        // default RasterizerState — needs CullNone.
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        DrawFullQuad(device, 0.2f, Color(0, 255, 0, 255));  // near, green — should win
        DrawFullQuad(device, 0.8f, Color(255, 0, 0, 255));  // far, red — must be rejected

        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        // --- Pass 2: sample the unbound RT as a Texture2D via SpriteBatch (proven-good path) ---
        // Retried until non-black (Task 912/Bgfx registration — see file header comment).
        const Rectangle centReg(W / 2, H / 2, 1, 1);
        Color centPx(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            device.Clear(Color(0, 0, 0, 255));
            sb_->Begin();
            sb_->Draw(*rt_,
                      Rectangle(0, 0, W, H),
                      Rectangle(0, 0, kRTSize, kRTSize),
                      Color::White);
            sb_->End();
            device.GetBackBufferData(&centReg, &centPx, 0, 1);
            if (centPx.getRProperty() > 10 || centPx.getGProperty() > 10 || centPx.getBProperty() > 10)
                break;
        }

        const bool pass = (centPx.getRProperty() <= 50  &&
                           centPx.getGProperty() >= 200 &&
                           centPx.getBProperty() <= 50);

        if (pass)
        {
            std::printf("[PASS] RenderTarget2D depth buffer: centre=(%d,%d,%d), near quad correctly won\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] RenderTarget2D depth buffer: centre=(%d,%d,%d), expected green "
                        "(R<=50, G>=200, B<=50) — far quad incorrectly won, depth test not "
                        "functioning inside the render target\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty());
        }
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    RenderTarget2DDepthTest game;
    game.Run();
    return game.getResult();
}

// SPDX-License-Identifier: MS-PL
// Task 911: Vulkan RenderTarget2D per-instance DepthStencilFormat fidelity.
//
// Before this task every Vulkan RenderTarget2D/RenderTargetCube silently shared the backend's
// own device-wide depthFormat_ VkFormat regardless of the DepthFormat actually requested at
// construction time -- a RenderTarget2D constructed with DepthFormat::None still got a full,
// real depth+stencil buffer under the hood, so depth testing "worked" (incorrectly) against it.
//
// This test proves genuine per-instance fidelity by binding a depth-tested "far quad drawn on
// top of a near quad" pair into three RenderTarget2D instances with three different requested
// DepthFormats, all in a single frame:
//
//   rt_none    (DepthFormat::None)           -- expect GREEN: no real depth attachment at all,
//                                                so the far quad (drawn second) cannot be
//                                                depth-rejected and simply overpaints the near one.
//   rt_default (DepthFormat::Depth24Stencil8) -- expect RED: a real depth buffer rejects the
//                                                farther second quad (LessEqual, the real XNA
//                                                default DepthBufferFunction).
//   rt_depth16 (DepthFormat::Depth16)         -- expect RED too: proves a *non-default*, distinct
//                                                real depth format also gets its own genuinely
//                                                functioning depth buffer/render pass, coexisting
//                                                in the same frame as the other two formats
//                                                (each needs its own render pass/pipeline --
//                                                Vulkan pipeline/render-pass compatibility
//                                                requires an exact attachment-format match).
//
// Before Task 911, rt_none would have incorrectly rendered RED (a real, shared depth buffer was
// always allocated regardless of the requested DepthFormat), so this test would fail against the
// pre-fix code -- a genuine differential.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
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

static constexpr int kSize = 64;

// Full-screen NDC quad (two triangles, 6 vertices) at a fixed depth z.
static void drawFullScreenZ(GraphicsDevice& dev, Color col, float z)
{
    const VertexPositionColor q[6] = {
        { Vector3(-1.f,  1.f, z), col },
        { Vector3(-1.f, -1.f, z), col },
        { Vector3( 1.f, -1.f, z), col },
        { Vector3(-1.f,  1.f, z), col },
        { Vector3( 1.f, -1.f, z), col },
        { Vector3( 1.f,  1.f, z), col },
    };
    dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
}

class VulkanRTDepthFormatFidelityTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<RenderTarget2D>        rtNone_;
    std::unique_ptr<RenderTarget2D>        rtDefault_;
    std::unique_ptr<RenderTarget2D>        rtDepth16_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    static bool isRed(const Color& px)
    {
        return px.getRProperty() >= 200 && px.getGProperty() <= 60 && px.getBProperty() <= 60;
    }
    static bool isGreen(const Color& px)
    {
        return px.getGProperty() >= 200 && px.getRProperty() <= 60 && px.getBProperty() <= 60;
    }

    Color readBB(GraphicsDevice& dev, int x, int y)
    {
        Color px(0, 0, 0, 0);
        Rectangle reg(x, y, 1, 1);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_        = std::make_unique<SpriteBatch>(dev);
        rtNone_    = std::make_unique<RenderTarget2D>(dev, kSize, kSize, false,
                        SurfaceFormat::Color, DepthFormat::None);
        rtDefault_ = std::make_unique<RenderTarget2D>(dev, kSize, kSize, false,
                        SurfaceFormat::Color, DepthFormat::Depth24Stencil8);
        rtDepth16_ = std::make_unique<RenderTarget2D>(dev, kSize, kSize, false,
                        SurfaceFormat::Color, DepthFormat::Depth16);
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(true);
        dev.SetDepthWriteEnabled(true);
        dev.setBlendStateProperty(BlendState::Opaque);
        // Task 896 finding (see vulkan_render_target_usage_test.cpp): this quad winding is
        // back-facing under CNA's real default RasterizerState -- needs CullNone.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        BasicEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.VertexColorEnabled = true;
        fx.Apply();

        // Near red quad (z=0.2) then far green quad (z=0.8) into each RT, depth-tested
        // (XNA's real default DepthBufferFunction is LessEqual): with a genuine depth buffer the
        // farther green quad must be rejected, leaving red; with no depth buffer at all, the
        // later draw always wins regardless of depth, leaving green.
        for (auto* rt : { rtNone_.get(), rtDefault_.get(), rtDepth16_.get() }) {
            dev.SetRenderTarget(rt);
            drawFullScreenZ(dev, Color::Red,   0.2f);
            drawFullScreenZ(dev, Color::Lime,  0.8f);
            dev.SetRenderTarget(nullptr);
        }

        sb_->Begin();
        sb_->Draw(*rtNone_,    Rectangle(0,          0, kSize, kSize),
                  Rectangle(0, 0, kSize, kSize), Color::White);
        sb_->Draw(*rtDefault_, Rectangle(kSize,      0, kSize, kSize),
                  Rectangle(0, 0, kSize, kSize), Color::White);
        sb_->Draw(*rtDepth16_, Rectangle(kSize * 2,  0, kSize, kSize),
                  Rectangle(0, 0, kSize, kSize), Color::White);
        sb_->End();

        const int cy = kSize / 2;
        Color pNone    = readBB(dev, kSize / 2,            cy);
        Color pDefault = readBB(dev, kSize + kSize / 2,     cy);
        Color pDepth16 = readBB(dev, kSize * 2 + kSize / 2, cy);

        char buf[160];
        std::snprintf(buf, sizeof(buf), "DepthFormat::None: (%d,%d,%d) expected GREEN (no real depth buffer)",
                      pNone.getRProperty(), pNone.getGProperty(), pNone.getBProperty());
        check(isGreen(pNone), buf);
        std::snprintf(buf, sizeof(buf), "DepthFormat::Depth24Stencil8: (%d,%d,%d) expected RED (real depth test)",
                      pDefault.getRProperty(), pDefault.getGProperty(), pDefault.getBProperty());
        check(isRed(pDefault), buf);
        std::snprintf(buf, sizeof(buf), "DepthFormat::Depth16: (%d,%d,%d) expected RED (real depth test)",
                      pDepth16.getRProperty(), pDepth16.getGProperty(), pDepth16.getBProperty());
        check(isRed(pDepth16), buf);

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    VulkanRTDepthFormatFidelityTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize * 3);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanRTDepthFormatFidelityTest game;
    game.Run();
    return game.getResult();
}

// SPDX-License-Identifier: MS-PL
// OPENGL1 backend: context-loss resource recreation registry (plan_opengl1.md phase 8).
//
// OpenGL1GraphicsBackend::DebugSimulateContextLoss() destroys and immediately recreates the SDL
// GL context (one atomic operation on desktop -- matching every other backend's own
// DebugSimulateContextLoss()/DebugRestoreContext() semantics, see IGraphicsBackend.hpp's own doc
// comment), then walks an OPENGL1-private OpenGL1ResourceRegistry (no dependency on EasyGL's own
// ::easygl::ResourceRegistry) to rebuild every tracked GPU resource:
//   - Texture2D: re-uploaded from the SAME CPU-side pixel buffer Texture2D itself keeps and
//     shares with the backend via ITextureBackend::ShareCpuPixels() -- content survives exactly.
//   - RenderTarget2D: rebuilt as an empty FBO/color-texture/depth-renderbuffer of the same
//     size/format -- content does NOT survive (a render target's content is GPU-produced, XNA/FNA
//     RenderTarget2D itself does not guarantee content survives a real device reset either unless
//     RenderTargetUsage.PreserveContents, which this backend does not implement). This test only
//     asserts the render target remains USABLE (can still be bound and drawn into) after the
//     simulated loss, not that its prior content survived.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class OpenGL1ContextLossTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int  pass_ = 0;
    int  fail_ = 0;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    Color ReadCenter(GraphicsDevice& dev)
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
        dev.setBlendStateProperty(BlendState::Opaque);

        // ---- Texture2D: content must survive a simulated context loss -----------------------
        const Color kMagenta(255, 0, 255, 255);
        Texture2D tex(dev, 1, 1);
        tex.SetData(&kMagenta, 1);

        SpriteBatch sb(dev);
        auto drawTexAndReadCenter = [&]() -> Color
        {
            dev.Clear(Color(0, 0, 0, 255));
            sb.Begin();
            sb.Draw(tex, Rectangle(0, 0, dev.getViewportProperty().getWidthProperty(),
                                    dev.getViewportProperty().getHeightProperty()),
                    Color(255, 255, 255, 255));
            sb.End();
            return ReadCenter(dev);
        };

        Color before = drawTexAndReadCenter();
        Check(before.getRProperty() > 200 && before.getBProperty() > 200 && before.getGProperty() < 20,
              "Texture2D: magenta before simulated context loss");

        dev.GetBackend().DebugSimulateContextLoss();

        Color after = drawTexAndReadCenter();
        std::printf("texture after context loss: (%d,%d,%d)\n",
                    after.getRProperty(), after.getGProperty(), after.getBProperty());
        Check(after.getRProperty() > 200 && after.getBProperty() > 200 && after.getGProperty() < 20,
              "Texture2D: content survives DebugSimulateContextLoss() (recreated from CPU shadow)");

        // ---- RenderTarget2D: must remain usable (bindable/drawable) after loss --------------
        {
            auto rt = std::make_unique<RenderTarget2D>(dev, 8, 8);
            dev.SetRenderTarget(rt.get());
            dev.Clear(Color(0, 255, 0, 255));
            dev.SetRenderTarget(nullptr);

            dev.GetBackend().DebugSimulateContextLoss();

            // A dangling render-target handle from before the loss must not crash when bound
            // and drawn into again -- RecreateGLResource() rebuilt an empty, usable FBO.
            bool survivedBindAndClear = true;
            try
            {
                dev.SetRenderTarget(rt.get());
                dev.Clear(Color(0, 0, 255, 255));
                dev.SetRenderTarget(nullptr);
                sb.Begin();
                sb.Draw(*rt, Rectangle(0, 0, dev.getViewportProperty().getWidthProperty(),
                                        dev.getViewportProperty().getHeightProperty()),
                        Color(255, 255, 255, 255));
                sb.End();
            }
            catch (...) { survivedBindAndClear = false; }
            Check(survivedBindAndClear,
                  "RenderTarget2D: remains bindable/usable after DebugSimulateContextLoss() (rebuilt empty)");
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    OpenGL1ContextLossTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(32);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    OpenGL1ContextLossTest game;
    game.Run();
    return game.getResult();
}

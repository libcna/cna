// SPDX-License-Identifier: MS-PL
// OPENGL1 renderer: context-loss resource recreation registry (plans/plan_opengl1.md phase 8).
//
// OpenGL1Renderer::DebugSimulateContextLoss() destroys and immediately recreates the SDL
// GL context (one atomic operation on desktop -- matching every other renderer's own
// DebugSimulateContextLoss()/DebugRestoreContext() semantics, see IGraphicsRenderer.hpp's own doc
// comment), then walks an OPENGL1-private OpenGL1ResourceRegistry (no dependency on EasyGL's own
// ::easygl::ResourceRegistry) to rebuild every tracked GPU resource:
//   - Texture2D/TextureCube: re-uploaded from the SAME CPU-side pixel buffer(s) Texture2D/
//     TextureCube themselves keep and share with the renderer via ITextureRenderer::
//     ShareCpuPixels()/ITextureCubeRenderer::ShareCpuPixels(face,...) -- content survives exactly.
//     TextureCube's own CPU shadow (and the ShareCpuPixels(face,...) hook itself) were added
//     specifically to close this renderer's own previously-documented, intentional gap.
//   - RenderTarget2D: rebuilt as an empty FBO/color-texture/depth-renderbuffer of the same
//     size/format -- content does NOT survive (a render target's content is GPU-produced, XNA/FNA
//     RenderTarget2D itself does not guarantee content survives a real device reset either unless
//     RenderTargetUsage.PreserveContents, which this renderer does not implement). This test keeps
//     the RT bound as the ACTIVE render target across the simulated loss (the scenario that
//     matters -- see the check's own comment below) and proves a post-recovery draw into it
//     actually lands on the RT itself, not silently on the backbuffer: OpenGL1Renderer
//     must explicitly re-bind currentRt_'s rebuilt FBO after recovery, since a brand-new GL
//     context always defaults to the backbuffer regardless of what was bound before the loss.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

#include <cstdio>
#include <memory>
#include <vector>

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

        dev.GetRenderer().DebugSimulateContextLoss();

        Color after = drawTexAndReadCenter();
        std::printf("texture after context loss: (%d,%d,%d)\n",
                    after.getRProperty(), after.getGProperty(), after.getBProperty());
        Check(after.getRProperty() > 200 && after.getBProperty() > 200 && after.getGProperty() < 20,
              "Texture2D: content survives DebugSimulateContextLoss() (recreated from CPU shadow)");

        // ---- TextureCube: content must survive a simulated context loss, same as Texture2D ----
        {
            const Color kOrange(255, 128, 0, 255);
            const Color kBlack(0, 0, 0, 0);
            TextureCube cube(dev, 2, false, SurfaceFormat::Color);
            const Color faceTexels[4] = { kOrange, kOrange, kOrange, kOrange };
            cube.SetData(CubeMapFace::PositiveY, faceTexels, 4);

            Color cubeBefore[4] = { kBlack, kBlack, kBlack, kBlack };
            cube.GetData(CubeMapFace::PositiveY, cubeBefore, 4);
            Check(cubeBefore[0].getRProperty() > 200 && cubeBefore[0].getGProperty() > 100 &&
                  cubeBefore[0].getGProperty() < 180 && cubeBefore[0].getBProperty() < 20,
                  "TextureCube: orange face before simulated context loss");

            dev.GetRenderer().DebugSimulateContextLoss();

            Color cubeAfter[4] = { kBlack, kBlack, kBlack, kBlack };
            cube.GetData(CubeMapFace::PositiveY, cubeAfter, 4);
            std::printf("cube face after context loss: (%d,%d,%d)\n",
                        cubeAfter[0].getRProperty(), cubeAfter[0].getGProperty(), cubeAfter[0].getBProperty());
            Check(cubeAfter[0].getRProperty() > 200 && cubeAfter[0].getGProperty() > 100 &&
                  cubeAfter[0].getGProperty() < 180 && cubeAfter[0].getBProperty() < 20,
                  "TextureCube: face content survives DebugSimulateContextLoss() (recreated from CPU shadow)");

            // A face that was NEVER written (no CPU shadow ever shared for it) must not crash on
            // rebuild -- Build() seeds it from a null cpuPixels_[face] (an empty pre-allocated
            // level, same as a brand new cube map), not from stale/uninitialized memory.
            bool untouchedFaceSurvivedRecovery = true;
            try
            {
                Color untouched[4] = { kBlack, kBlack, kBlack, kBlack };
                cube.GetData(CubeMapFace::NegativeY, untouched, 4);
            }
            catch (...) { untouchedFaceSurvivedRecovery = false; }
            Check(untouchedFaceSurvivedRecovery,
                  "TextureCube: a face with no CPU shadow (never SetData'd) still reads back cleanly after recovery");
        }

        // ---- RenderTarget2D: must remain the ACTIVE, correctly-bound target across a loss ---
        // Deliberately keeps the RT bound WHILE the simulated loss happens (not unbound first --
        // an earlier version of this test unbound before the loss and could not have caught the
        // real bug below, since the at-risk code path only fires when a render target is still
        // active at the moment of loss).
        {
            auto rt = std::make_unique<RenderTarget2D>(dev, 8, 8);
            dev.SetRenderTarget(rt.get());
            dev.Clear(Color(0, 255, 0, 255)); // pre-loss content -- not expected to survive.

            bool survivedLossWhileBound = true;
            try { dev.GetRenderer().DebugSimulateContextLoss(); }
            catch (...) { survivedLossWhileBound = false; }
            Check(survivedLossWhileBound,
                  "RenderTarget2D: DebugSimulateContextLoss() does not throw while the RT is still the active target");

            // The new context defaults to the backbuffer regardless of what was bound before the
            // loss. If OpenGL1Renderer doesn't explicitly re-bind the still-current render
            // target's rebuilt FBO after recovery, this Clear() would silently land on the
            // backbuffer instead -- leaving the RT's own (freshly rebuilt, empty/black) content
            // unchanged. Sampling the RT back afterward is what actually discriminates the two
            // cases, not just "no exception was thrown".
            dev.Clear(Color(0, 0, 255, 255));
            dev.SetRenderTarget(nullptr);

            // Read the RT's own FBO/color texture DIRECTLY (RenderTarget2D::GetData(), which
            // always glReadPixels off the RT's specific fbo_ regardless of whatever is currently
            // bound elsewhere) rather than drawing it through SpriteBatch onto the backbuffer and
            // reading THAT -- the indirect path can't distinguish "the RT itself is blue" from
            // "the backbuffer already happened to be blue before SpriteBatch drew anything".
            // A GPU-only (RenderTarget2D) source only supports a full-texture GetData() read
            // (Texture2D::GetData's own gpuOnlyContent_ fallback requires elementCount==width*
            // height), so read all 8x8 texels and check the first one -- a solid Clear() makes
            // every texel identical.
            std::vector<Color> rtPixels(8 * 8, Color(0, 0, 0, 0));
            rt->GetData(rtPixels.data(), 0, static_cast<int>(rtPixels.size()));
            const Color& rtPixel = rtPixels[0];
            std::printf("render target's own content after post-recovery clear: (%d,%d,%d)\n",
                        rtPixel.getRProperty(), rtPixel.getGProperty(), rtPixel.getBProperty());
            Check(rtPixel.getBProperty() > 200 && rtPixel.getRProperty() < 20 && rtPixel.getGProperty() < 20,
                  "RenderTarget2D: still the active render target after recovery -- the post-recovery Clear() "
                  "actually landed on the RT itself (blue), not silently on the backbuffer");
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

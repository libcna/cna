// SPDX-License-Identifier: MS-PL
//
// plan_vulkan.md VULKAN-213 -- the render-target lifetime shapes the existing tests do not reach.
//
// This renderer defers every draw to a single Present-time record, so "the game is done with this
// target" and "the GPU is done with this target" are different moments, and the gap between them is
// where a render target can be destroyed out from under queued work. `Vulkan_RenderTarget_GetData
// Lifetime`, `_DeferredResourceLifetime`, `_BoundTargetLifetime` and `_CubeFaceReadbackDependency`
// already cover four shapes. This covers the three the row named and they did not:
//
//   A  A target drawn into and then DISPOSED with its content still pending -- no readback, no
//      Present in between. The queued pass names a framebuffer whose wrapper is gone; REMED-GFX-166
//      says the pass keeps it alive, and this is what says so out loud.
//   B  A target written and then SAMPLED in the same frame, and disposed immediately after the
//      draw that sampled it -- so the sampling draw is still queued when the source dies.
//   C  A RenderTargetCube disposed with only some faces rendered and all of that work pending.
//
// None of the three can be judged by a pixel: the target is gone, so there is nothing to read. What
// they are judged by is that the frame completes, the NEXT frame renders correctly, and no
// validation message appears -- including at `vkDestroyDevice`, which is after this program's last
// statement and therefore the `VULKAN-408` CTest gate's half of the verdict rather than a check()'s.
// A leg that draws a known quad after each shape is what turns "did not crash" into "still works".
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

namespace
{
constexpr int kN = 8;
const Color kClear(0, 0, 0, 255);
const Color kSource(200, 40, 40, 255);
const Color kProbe(20, 220, 90, 255);
}  // namespace

class VulkanRenderTargetLifetimeShapesTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<RenderTarget2D> witness_;
    int pass_ = 0;
    int fail_ = 0;
    int frame_ = 0;
    std::size_t messagesBefore_ = 0;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ok ? ++pass_ : ++fail_;
    }

    VulkanRenderer& Renderer()
    {
        return *dynamic_cast<VulkanRenderer*>(&getGraphicsDeviceProperty().GetRenderer());
    }

    static bool Is(const Color& got, const Color& want)
    {
        return got.getRProperty() == want.getRProperty() &&
               got.getGProperty() == want.getGProperty() &&
               got.getBProperty() == want.getBProperty();
    }

    static std::string Text(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) +
               "," + std::to_string(c.getBProperty()) + ")";
    }

    void FillTarget(RenderTarget2D& rt, const Color& c)
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.SetRenderTarget(&rt);
        dev.Clear(kClear);
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.setLightingEnabledProperty(false);
        fx.setTextureEnabledProperty(false);
        fx.setFogEnabledProperty(false);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();
        const VertexPositionColor t[6] = {
            { Vector3(-1.f,  1.f, 0.f), c }, { Vector3( 1.f,  1.f, 0.f), c },
            { Vector3(-1.f, -1.f, 0.f), c }, { Vector3( 1.f,  1.f, 0.f), c },
            { Vector3( 1.f, -1.f, 0.f), c }, { Vector3(-1.f, -1.f, 0.f), c } };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, t, 0, 2);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    }

    /// Renders a known quad into the surviving witness target and reads it back. This is what
    /// turns "the shape above did not crash" into "the device still works after it".
    Color ProbeStillWorks()
    {
        FillTarget(*witness_, kProbe);
        std::vector<Color> p(static_cast<std::size_t>(kN * kN), Color(0, 0, 0, 0));
        witness_->GetData(p.data(), 0, kN * kN);
        return p[kN * kN / 2];
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        if (frame_ == 0)
        {
            witness_ = std::make_unique<RenderTarget2D>(
                dev, kN, kN, false, SurfaceFormat::Color, DepthFormat::Depth24Stencil8, 0,
                RenderTargetUsage::DiscardContents);
            messagesBefore_ = Renderer().GetValidationMessagesEXT().size();

            // A. Drawn into, then disposed with the work still queued -- no readback (which would
            //    flush it) and no Present in between.
            {
                bool threw = false;
                std::string what;
                try {
                    RenderTarget2D doomed(dev, kN, kN, false, SurfaceFormat::Color,
                                          DepthFormat::Depth24Stencil8, 0,
                                          RenderTargetUsage::DiscardContents);
                    FillTarget(doomed, kSource);
                    doomed.Dispose();
                } catch (const std::exception& e) { threw = true; what = e.what(); }
                const Color got = ProbeStillWorks();
                check(!threw && Is(got, kProbe),
                      "A a target disposed with its draw still queued: threw=" +
                          (threw ? what : std::string("no")) + " and the device still renders, "
                          "probe=" + Text(got) + " (want " + Text(kProbe) + ")");
            }

            // B. Written, then SAMPLED in the same frame, then disposed while the sampling draw is
            //    itself still queued -- so the source dies between its use and its execution.
            {
                bool threw = false;
                std::string what;
                try {
                    auto source = std::make_unique<RenderTarget2D>(
                        dev, kN, kN, false, SurfaceFormat::Color, DepthFormat::Depth24Stencil8, 0,
                        RenderTargetUsage::DiscardContents);
                    FillTarget(*source, kSource);
                    dev.SetRenderTarget(witness_.get());
                    dev.Clear(kClear);
                    {
                        SamplerState point = SamplerState::PointClamp;
                        SpriteBatch sb(dev);
                        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr,
                                 nullptr);
                        sb.Draw(*source, Rectangle(0, 0, kN, kN), Rectangle(0, 0, kN, kN),
                                Color(255, 255, 255, 255));
                        sb.End();
                    }
                    dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                    source.reset();     // the sampled source dies with the sampling draw pending
                } catch (const std::exception& e) { threw = true; what = e.what(); }
                const Color got = ProbeStillWorks();
                check(!threw && Is(got, kProbe),
                      "B a target sampled in the frame it was written, then disposed while the "
                      "sampling draw is still queued: threw=" +
                          (threw ? what : std::string("no")) + " probe=" + Text(got));
            }

            // C. A RenderTargetCube with only some faces rendered, disposed with all of it pending.
            {
                bool threw = false;
                std::string what;
                try {
                    RenderTargetCube cube(dev, kN, false, SurfaceFormat::Color,
                                          DepthFormat::Depth24Stencil8, 0,
                                          RenderTargetUsage::DiscardContents);
                    for (int face = 0; face < 3; ++face) {
                        dev.SetRenderTarget(&cube, static_cast<CubeMapFace>(face));
                        dev.Clear(kClear);
                        BasicEffect fx(dev);
                        fx.VertexColorEnabled = true;
                        fx.setLightingEnabledProperty(false);
                        fx.setTextureEnabledProperty(false);
                        fx.setFogEnabledProperty(false);
                        fx.setWorldProperty(Matrix::getIdentityProperty());
                        fx.setViewProperty(Matrix::getIdentityProperty());
                        fx.setProjectionProperty(Matrix::getIdentityProperty());
                        fx.Apply();
                        const VertexPositionColor t[3] = {
                            { Vector3(-1.f,  1.f, 0.f), kSource },
                            { Vector3( 1.f,  1.f, 0.f), kSource },
                            { Vector3(-1.f, -1.f, 0.f), kSource } };
                        dev.DrawUserPrimitives(PrimitiveType::TriangleList, t, 0, 1);
                    }
                    dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                    cube.Dispose();     // three faces rendered, three never touched, all pending
                } catch (const std::exception& e) { threw = true; what = e.what(); }
                const Color got = ProbeStillWorks();
                check(!threw && Is(got, kProbe),
                      "C a RenderTargetCube disposed with three faces rendered and all the work "
                      "pending: threw=" + (threw ? what : std::string("no")) + " probe=" +
                          Text(got));
            }

            ++frame_;
            return;
        }

        // Frame 1: everything above was presented in frame 0. The device has now actually executed
        // the queued work whose targets are gone, so this is where a use-after-free surfaces.
        {
            const Color got = ProbeStillWorks();
            check(Is(got, kProbe),
                  "D the frame AFTER all three shapes still renders correctly, probe=" +
                      Text(got) + " (want " + Text(kProbe) +
                      "; the queued work executed between the two frames)");
        }
        {
            const std::size_t after = Renderer().GetValidationMessagesEXT().size();
            std::string firstNew;
            if (after > messagesBefore_)
                firstNew = " first new: " + Renderer().GetValidationMessagesEXT()[messagesBefore_];
            check(!VulkanRenderer::IsValidationActiveEXT() || after == messagesBefore_,
                  "E no validation message from any of the three shapes: " +
                      std::to_string(messagesBefore_) + " -> " + std::to_string(after) +
                      " (the vkDestroyDevice half is the VULKAN-408 CTest gate's, not this leg's)" +
                      firstNew);
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        witness_.reset();
        Exit();
    }

public:
    VulkanRenderTargetLifetimeShapesTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanRenderTargetLifetimeShapesTest game;
    game.Run();
    return game.getResult();
}

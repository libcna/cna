// SPDX-License-Identifier: MS-PL
//
// plan_vulkan.md VULKAN-214 -- does a RenderTargetUsage::PreserveContents target still preserve
// after the window has been resized under it?
//
// On Vulkan a resize is a swapchain recreation, which is the most disruptive thing that happens to
// this renderer while resources are alive: the swapchain, its image views, its framebuffers and the
// backbuffer depth image are all destroyed and rebuilt, the frame generation advances, and the
// readback cache is invalidated. A render target created before all that keeps its own images and
// its own framebuffer, and `PreserveContents` promises its texels survive an unbind/rebind cycle.
// Nothing tested that the promise still holds across the recreation.
//
// Renderer-agnostic and registered on Vulkan and EasyGL, because "preserve survives a resize" is a
// claim about the XNA contract rather than about Vulkan, and a one-renderer result cannot separate
// a renderer defect from a shared-layer one.
//
//   A  Before any resize: a preserved target holds what was drawn into it. The control -- if this
//      fails, B is measuring PreserveContents itself and not the resize.
//   B  After the resize: rebinding the SAME target and drawing into the OTHER half leaves the
//      first half's texels intact. A target whose framebuffer or render pass did not survive shows
//      up here as the clear colour, or as a validation message the VULKAN-408 gate catches.
//   C  A Texture2D created before the resize is still readable after it, with its original pixels.
//      Cheap, and it separates "render targets specifically" from "every resource".
//   D  Vulkan only: the swapchain really was recreated. Without it A-C could all pass on a build
//      where the resize quietly did nothing, which is exactly the failure this row is about.
//
// The resize is driven through GraphicsDeviceManager (setPreferredBackBufferWidth/Height +
// ApplyChanges()) rather than by resizing the native window directly. Two reasons, and the second
// is the load-bearing one: the GDM path is synchronous, so there is no compositor-throttling
// deadline to wait on and no environment-dependent failure mode; and this file lives under
// modules/graphics/, where the platform ratchet (cmake/PlatformRatchet.cmake) refuses a new
// example that reaches for the windowing library by name -- correctly, because a test of the XNA
// contract has no business doing so. A Vulkan-only leg asserts the swapchain really was recreated,
// so the test cannot pass by never resizing at all.
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
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#if defined(CNA_RENDERER_VULKAN)
#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"
#endif

#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
constexpr int kN = 8;
const Color kClear(0, 0, 0, 255);
const Color kFirst(255, 0, 0, 255);
const Color kSecond(0, 0, 255, 255);
const Color kTexel(17, 200, 90, 255);
}  // namespace

class RenderTargetPreserveAcrossResizeTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<RenderTarget2D> preserved_;
    std::unique_ptr<Texture2D> before_;
    int pass_ = 0;
    int fail_ = 0;
    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ok ? ++pass_ : ++fail_;
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

    void DrawQuad(float x0, float x1, const Color& c)
    {
        auto& dev = getGraphicsDeviceProperty();
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
            { Vector3(x0,  1.f, 0.f), c }, { Vector3(x1,  1.f, 0.f), c },
            { Vector3(x0, -1.f, 0.f), c }, { Vector3(x1,  1.f, 0.f), c },
            { Vector3(x1, -1.f, 0.f), c }, { Vector3(x0, -1.f, 0.f), c } };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, t, 0, 2);
    }

    std::vector<Color> ReadTarget()
    {
        std::vector<Color> p(static_cast<std::size_t>(kN * kN), Color(0, 0, 0, 0));
        preserved_->GetData(p.data(), 0, kN * kN);
        return p;
    }

    void Finish()
    {
        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        preserved_.reset();
        before_.reset();
        Exit();
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();

        preserved_ = std::make_unique<RenderTarget2D>(
            dev, kN, kN, false, SurfaceFormat::Color, DepthFormat::Depth24Stencil8, 0,
            RenderTargetUsage::PreserveContents);
        before_ = std::make_unique<Texture2D>(dev, 2, 2, false, SurfaceFormat::Color);
        const std::array<std::uint8_t, 16> px{
            kTexel.getRProperty(), kTexel.getGProperty(), kTexel.getBProperty(), 255,
            kTexel.getRProperty(), kTexel.getGProperty(), kTexel.getBProperty(), 255,
            kTexel.getRProperty(), kTexel.getGProperty(), kTexel.getBProperty(), 255,
            kTexel.getRProperty(), kTexel.getGProperty(), kTexel.getBProperty(), 255 };
        before_->SetDataRGBA(px.data(), 4);

        // The one clear this target ever receives: PreserveContents means every later bind must
        // find what the previous one left.
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.SetRenderTarget(preserved_.get());
        dev.Clear(kClear);
        DrawQuad(-1.f, 0.f, kFirst);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        {
            const std::vector<Color> p = ReadTarget();
            check(Is(p[1], kFirst) && Is(p[kN - 2], kClear),
                  "A control: before any resize, the preserved target holds the first draw, left=" +
                      Text(p[1]) + " right=" + Text(p[kN - 2]) + " (want " + Text(kFirst) +
                      " and " + Text(kClear) + ")");
        }

#if defined(CNA_RENDERER_VULKAN)
        const uint64_t recreatesBefore =
            dynamic_cast<CNA::Internal::Renderers::Vulkan::VulkanRenderer&>(dev.GetRenderer())
                .GetSwapchainRecreateCountEXT();
#endif
        // The resize itself: synchronous, through the public API, no window manager involved.
        gdm_->setPreferredBackBufferWidthProperty(160);
        gdm_->setPreferredBackBufferHeightProperty(96);
        gdm_->ApplyChanges();

        // B. The measurement. Rebind the SAME target, draw into the OTHER half, and require the
        //    first half to still be there. PreserveContents means no clear, so anything lost was
        //    lost by the resize.
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.SetRenderTarget(preserved_.get());
        DrawQuad(0.f, 1.f, kSecond);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        {
            const std::vector<Color> p = ReadTarget();
            check(Is(p[1], kFirst) && Is(p[kN - 2], kSecond),
                  "B a PreserveContents target keeps its pre-resize texels across the resize, left=" +
                      Text(p[1]) + " right=" + Text(p[kN - 2]) + " (want " + Text(kFirst) +
                      " and " + Text(kSecond) +
                      "; the clear colour on the left means the content was lost)");
        }

        // C. Every other resource kind created before the resize is still usable after it.
        {
            std::vector<Color> texels(4, Color(0, 0, 0, 0));
            bool threw = false;
            std::string what;
            try { before_->GetData(texels.data(), 0, 4); }
            catch (const std::exception& e) { threw = true; what = e.what(); }
            check(!threw && Is(texels[0], kTexel) && Is(texels[3], kTexel),
                  "C a Texture2D created before the resize still reads back its own pixels: threw=" +
                      (threw ? what : std::string("no")) + " first=" + Text(texels[0]) + " last=" +
                      Text(texels[3]) + " (want " + Text(kTexel) + ")");
        }

#if defined(CNA_RENDERER_VULKAN)
        // D. Without this the three legs above could all pass on a build where ApplyChanges did
        //    nothing at all, which is the one way this test could lie.
        {
            const uint64_t recreatesAfter =
                dynamic_cast<CNA::Internal::Renderers::Vulkan::VulkanRenderer&>(dev.GetRenderer())
                    .GetSwapchainRecreateCountEXT();
            check(recreatesAfter > recreatesBefore,
                  "D the swapchain really was recreated by the resize: count " +
                      std::to_string(recreatesBefore) + " -> " + std::to_string(recreatesAfter) +
                      " (equal means legs A-C measured nothing)");
        }
#endif

        Finish();
    }

public:
    RenderTargetPreserveAcrossResizeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    RenderTargetPreserveAcrossResizeTest game;
    game.Run();
    return game.getResult();
}

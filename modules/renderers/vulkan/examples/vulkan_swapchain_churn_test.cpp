// SPDX-License-Identifier: MS-PL
//
// plan_vulkan.md VULKAN-338 -- repeated swapchain recreation, a minimized (0x0) window, and a
// restore, with live resources across all of it.
//
// A swapchain recreation is the most disruptive thing that happens to this renderer while a game's
// resources are alive: the swapchain, its image views, its framebuffers and the backbuffer depth
// image are destroyed and rebuilt, the acquired-image record is reset, and the readback cache is
// invalidated. `Vulkan_RealWindowResize` and `Vulkan_RenderTargetPreserveAcrossResize` each drive
// ONE. This drives more than twenty in a row, and puts the two cases neither of them reaches in the
// middle of the run: a zero drawable extent, and a frame PRESENTED while in that state.
//
// The zero extent is delivered the way the platform delivers it -- `OnSurfaceChanged` with a 0x0
// drawable, which `RecreateSwapchain` deliberately returns early from (a minimized window has no
// surface to build a swapchain against). The interesting part is not that call; it is the frame the
// game submits afterwards, while the renderer is still holding a swapchain sized for a window that
// is no longer visible. Nothing tested that.
//
// Vulkan-specific by construction and with no EasyGL twin, so it names VulkanRenderer directly and
// reads the renderer's own counters rather than inferring them.
//
//   A  The pre-existing RenderTarget2D survives the churn: PreserveContents, so the marker drawn
//      before the first resize must still be there after the last.
//   B  The pre-existing Texture2D still reads back its own texels.
//   C  The pre-existing VertexBuffer still DRAWS -- readable is not the same as usable, and a
//      buffer whose memory was retired under it would pass B's kind of check and fail this one.
//   D  The churn really happened: the swapchain recreate count rose by at least the number of
//      resize cycles, so A-C cannot pass by never having been disturbed.
//   E  No validation message, in-process. The CTest gate says the same thing about teardown; this
//      leg is what localizes a message to THIS test's churn rather than to the whole binary.
//   F  A zero drawable does NOT recreate the swapchain. `OnSurfaceChanged` guards on it, and this
//      is the observable half of that guard.
//
// One limit of this file, stated because a green run would otherwise imply more than it proves:
// `RecreateSwapchain`'s OWN `if (w == 0 || h == 0) return;` is not falsifiable here. Removing it
// changes nothing on this stack, because `CreateSwapchain` prefers `caps.currentExtent` and only
// falls back to `surfaceInfo_.drawableSize` CLAMPED to `[minImageExtent, maxImageExtent]` -- so a
// zero in `surfaceInfo_` can never reach `vkCreateSwapchainKHR`, and the X11 surface under Xvfb
// always reports a real `currentExtent`. That guard earns its keep on a platform whose surface
// reports a genuinely zero `currentExtent` while minimized (Windows does), where
// `vkCreateSwapchainKHR` would reject it. Measured: with the guard deleted, all six legs still
// pass. Leg F covers the guard that IS reachable, in `OnSurfaceChanged`.
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
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
constexpr int kResizeCycles = 22;   ///< the row asks for at least twenty
const Color kClear(0, 0, 0, 255);
const Color kMarker(255, 0, 0, 255);
const Color kTexel(17, 200, 90, 255);
const Color kQuad(30, 60, 240, 255);
}  // namespace

class VulkanSwapchainChurnTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<RenderTarget2D> preserved_;
    std::unique_ptr<Texture2D> texture_;
    std::unique_ptr<VertexBuffer> buffer_;
    int pass_ = 0;
    int fail_ = 0;
    int frame_ = 0;
    uint64_t recreatesBefore_ = 0;
    uint64_t recreatesAtMinimize_ = 0;
    uint64_t recreatesAfterMinimize_ = 0;
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

    void DrawUserQuad(float x0, float x1, const Color& c)
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

    void Finish()
    {
        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        preserved_.reset();
        texture_.reset();
        buffer_.reset();
        Exit();
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        // --- frame 0: build the resources, mark the target, then churn the swapchain ----------
        if (frame_ == 0)
        {
            preserved_ = std::make_unique<RenderTarget2D>(
                dev, kN, kN, false, SurfaceFormat::Color, DepthFormat::Depth24Stencil8, 0,
                RenderTargetUsage::PreserveContents);
            texture_ = std::make_unique<Texture2D>(dev, 2, 2, false, SurfaceFormat::Color);
            const std::array<std::uint8_t, 16> px{
                kTexel.getRProperty(), kTexel.getGProperty(), kTexel.getBProperty(), 255,
                kTexel.getRProperty(), kTexel.getGProperty(), kTexel.getBProperty(), 255,
                kTexel.getRProperty(), kTexel.getGProperty(), kTexel.getBProperty(), 255,
                kTexel.getRProperty(), kTexel.getGProperty(), kTexel.getBProperty(), 255 };
            texture_->SetDataRGBA(px.data(), 4);

            // Raw 16-byte records rather than an array of VertexPositionColor: the stock vertex
            // types are polymorphic here, so sizeof() is not the packed stride and SetDataRaw over
            // an array of them would upload a vtable pointer as position. Measured while writing
            // this: leg C came back white -- BasicEffect's default DiffuseColor -- because the
            // colour element landed in the wrong bytes, which looks exactly like a resource the
            // churn corrupted and is not one.
            constexpr int kStride = 16;
            const VertexDeclaration decl(
                kStride,
                { VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                  VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0) });
            buffer_ = std::make_unique<VertexBuffer>(dev, decl, 6, BufferUsage::None);
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(6 * kStride), 0);
            constexpr float kX[6] = { -1.f,  1.f, -1.f,  1.f,  1.f, -1.f };
            constexpr float kY[6] = {  1.f,  1.f, -1.f,  1.f, -1.f, -1.f };
            for (int i = 0; i < 6; ++i) {
                std::uint8_t* v = bytes.data() + i * kStride;
                const float pos[3] = { kX[i], kY[i], 0.0f };
                std::memcpy(v, pos, sizeof(pos));
                v[12] = kQuad.getRProperty();
                v[13] = kQuad.getGProperty();
                v[14] = kQuad.getBProperty();
                v[15] = kQuad.getAProperty();
            }
            buffer_->SetDataRaw(bytes.data(), 6, kStride);

            dev.setBlendStateProperty(BlendState::Opaque);
            dev.SetRenderTarget(preserved_.get());
            dev.Clear(kClear);
            DrawUserQuad(-1.f, 0.f, kMarker);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            recreatesBefore_ = Renderer().GetSwapchainRecreateCountEXT();
            messagesBefore_  = Renderer().GetValidationMessagesEXT().size();

            // Twenty-two back-to-back recreations, each a different extent so none can be skipped
            // as a no-op, and the aspect ratio changing so the presented rectangle moves too.
            for (int i = 0; i < kResizeCycles; ++i)
            {
                gdm_->setPreferredBackBufferWidthProperty(96 + (i % 7) * 16);
                gdm_->setPreferredBackBufferHeightProperty(64 + (i % 5) * 24);
                gdm_->ApplyChanges();
            }

            // The minimized step. This is what the platform delivers when a window is iconified:
            // the same surface, a zero drawable. RecreateSwapchain returns early on it by design,
            // so the renderer keeps a swapchain sized for a window that is no longer visible --
            // and the frame this Draw() is part of is then PRESENTED in exactly that state.
            {
                recreatesAtMinimize_ = Renderer().GetSwapchainRecreateCountEXT();
                CNA::Internal::Renderers::RendererSurfaceInfo minimized =
                    Renderer().GetSurfaceInfoEXT();
                minimized.drawableSize.width  = 0;
                minimized.drawableSize.height = 0;
                Renderer().OnSurfaceChanged(minimized);
                recreatesAfterMinimize_ = Renderer().GetSwapchainRecreateCountEXT();
            }
            ++frame_;
            return;
        }

        // --- frame 1: still minimized, so this frame is submitted with a zero drawable too ----
        if (frame_ == 1)
        {
            DrawUserQuad(-1.f, 1.f, kQuad);   // ordinary work, into a surface that is not there
            ++frame_;
            return;
        }

        // --- frame 2: restore, which is the recreation the minimized state deferred -----------
        if (frame_ == 2)
        {
            CNA::Internal::Renderers::RendererSurfaceInfo restored = Renderer().GetSurfaceInfoEXT();
            restored.drawableSize.width  = 160;
            restored.drawableSize.height = 96;
            Renderer().OnSurfaceChanged(restored);
            ++frame_;
            return;
        }

        // --- frame 3: every assertion, on resources that predate all of the above -------------
        {
            std::vector<Color> p(static_cast<std::size_t>(kN * kN), Color(0, 0, 0, 0));
            bool threw = false;
            std::string what;
            try { preserved_->GetData(p.data(), 0, kN * kN); }
            catch (const std::exception& e) { threw = true; what = e.what(); }
            check(!threw && Is(p[1], kMarker) && Is(p[kN - 2], kClear),
                  "A the PreserveContents target still holds its pre-churn marker after " +
                      std::to_string(kResizeCycles) + " recreations, a minimize and a restore: "
                      "threw=" + (threw ? what : std::string("no")) + " left=" + Text(p[1]) +
                      " right=" + Text(p[kN - 2]));
        }
        {
            std::vector<Color> texels(4, Color(0, 0, 0, 0));
            bool threw = false;
            std::string what;
            try { texture_->GetData(texels.data(), 0, 4); }
            catch (const std::exception& e) { threw = true; what = e.what(); }
            check(!threw && Is(texels[0], kTexel) && Is(texels[3], kTexel),
                  "B the Texture2D still reads back its own texels: threw=" +
                      (threw ? what : std::string("no")) + " first=" + Text(texels[0]) +
                      " last=" + Text(texels[3]));
        }
        {
            // Readable is not usable: a buffer whose memory was retired under it can still be
            // asked for its VertexCount and still fail to draw.
            bool threw = false;
            std::string what;
            Color got(0, 0, 0, 0);
            try {
                dev.setBlendStateProperty(BlendState::Opaque);
                dev.SetRenderTarget(preserved_.get());
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
                dev.SetVertexBuffer(buffer_.get());
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
                dev.SetVertexBuffer(nullptr);
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                std::vector<Color> p(static_cast<std::size_t>(kN * kN), Color(0, 0, 0, 0));
                preserved_->GetData(p.data(), 0, kN * kN);
                got = p[kN * kN / 2];
            } catch (const std::exception& e) { threw = true; what = e.what(); }
            check(!threw && Is(got, kQuad),
                  "C the VertexBuffer still DRAWS after the churn: threw=" +
                      (threw ? what : std::string("no")) + " centre=" + Text(got) + " (want " +
                      Text(kQuad) + ")");
        }
        {
            const uint64_t after = Renderer().GetSwapchainRecreateCountEXT();
            check(after >= recreatesBefore_ + static_cast<uint64_t>(kResizeCycles),
                  "D the churn really happened: swapchain recreate count " +
                      std::to_string(recreatesBefore_) + " -> " + std::to_string(after) +
                      " (want at least +" + std::to_string(kResizeCycles) +
                      "; equal would mean legs A-C measured an undisturbed renderer)");
        }
        {
            const std::size_t after = Renderer().GetValidationMessagesEXT().size();
            std::string firstNew;
            if (after > messagesBefore_)
                firstNew = " first new: " + Renderer().GetValidationMessagesEXT()[messagesBefore_];
            check(!VulkanRenderer::IsValidationActiveEXT() || after == messagesBefore_,
                  "E no validation message from the churn itself: " +
                      std::to_string(messagesBefore_) + " -> " + std::to_string(after) +
                      (VulkanRenderer::IsValidationActiveEXT()
                           ? "" : " (layer not active in this build -- the CTest gate still covers teardown)") +
                      firstNew);
        }

        {
            check(recreatesAfterMinimize_ == recreatesAtMinimize_,
                  "F a zero drawable does not recreate the swapchain: count " +
                      std::to_string(recreatesAtMinimize_) + " -> " +
                      std::to_string(recreatesAfterMinimize_) +
                      " across OnSurfaceChanged(0x0) (a rise means a minimized window would be "
                      "handed a swapchain sized for a surface it no longer has)");
        }

        Finish();
    }

public:
    VulkanSwapchainChurnTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(128);
        gdm_->setPreferredBackBufferHeightProperty(96);
    }

    [[nodiscard]] int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanSwapchainChurnTest game;
    game.Run();
    return game.getResult();
}

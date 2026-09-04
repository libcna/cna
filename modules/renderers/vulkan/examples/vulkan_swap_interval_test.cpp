// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-332 (finding F-05): a runtime VSync change must reach the swapchain.
//
// swapInterval_ was read exactly once, when the swapchain was first created. So
// GraphicsDeviceManager.SynchronizeWithVerticalRetrace + ApplyChanges() -- which routes through
// GraphicsDevice::Reset and DOES call IGraphicsRenderer::SetSwapInterval -- changed nothing here,
// and IGraphicsRenderer's own comment that Vulkan "cannot change VSync at runtime" described that
// implementation rather than the API. CreateSwapchain already picks its VkPresentModeKHR from
// swapInterval_, and this renderer already rebuilds its swapchain on every resize: the mechanism
// was there and was simply never invoked.
//
// The oracle is deliberately NOT the recorded interval. A renderer that stored the request and
// rebuilt nothing would satisfy that, which is exactly the defect. Two independent observations
// are used instead:
//
//   A  The live swapchain's own VkPresentModeKHR follows the request: FIFO with vsync on, one of
//      IMMEDIATE/MAILBOX with it off, and FIFO again when it comes back. That mode is written only
//      by CreateSwapchain, so it cannot move unless the swapchain was genuinely rebuilt -- which
//      makes it a stronger oracle than the recreation counter and a much more stable one. The
//      counter is printed rather than asserted on purpose: ApplyChanges() goes through
//      GraphicsDevice::Reset, which re-applies the virtual resolution and the presentation format
//      too, and how many recreations THOSE cost depends on what the frame did beforehand.
//      Measured while writing this: the same no-change ApplyChanges() cost 0 recreations before any
//      draw and 1 after one. Asserting that number would be testing Reset's bookkeeping, not this.
//      A device offering neither IMMEDIATE nor MAILBOX is reported rather than failed -- FIFO is
//      the only present mode Vulkan guarantees, and holding a driver to a mode it does not have
//      would be testing the driver.
//   C  Rendering still works afterwards. A swapchain rebuilt mid-run drags every framebuffer,
//      image view and depth resource with it, so "the mode changed" is worth nothing on its own.
//   D  No validation messages across the whole sequence.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <cstdio>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

namespace
{
    constexpr int kSize = 64;

    const char* PresentModeName(VkPresentModeKHR mode)
    {
        switch (mode) {
            case VK_PRESENT_MODE_IMMEDIATE_KHR:    return "IMMEDIATE";
            case VK_PRESENT_MODE_MAILBOX_KHR:      return "MAILBOX";
            case VK_PRESENT_MODE_FIFO_KHR:         return "FIFO";
            case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "FIFO_RELAXED";
            default:                               return "other";
        }
    }
}

class VulkanSwapIntervalTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int  pass_ = 0;
    int  fail_ = 0;
    bool done_ = false;

    void check(bool ok, const std::string& label, const std::string& detail)
    {
        std::printf("[%s] %s: %s\n", ok ? "PASS" : "FAIL", label.c_str(), detail.c_str());
        if (ok) ++pass_; else ++fail_;
    }

    VulkanRenderer& Renderer()
    {
        return *dynamic_cast<VulkanRenderer*>(&getGraphicsDeviceProperty().GetRenderer());
    }

    /// Draws a red quad and returns the centre pixel, so a rebuilt swapchain has to still work.
    Color RenderProbe(GraphicsDevice& dev)
    {
        dev.Clear(Color(0, 0, 0, 255));
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        const Color red(255, 0, 0, 255);
        const VertexPositionColor q[6] = {
            { Vector3(-0.9f,  0.9f, 0.5f), red }, { Vector3(-0.9f, -0.9f, 0.5f), red },
            { Vector3( 0.9f, -0.9f, 0.5f), red }, { Vector3(-0.9f,  0.9f, 0.5f), red },
            { Vector3( 0.9f, -0.9f, 0.5f), red }, { Vector3( 0.9f,  0.9f, 0.5f), red },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
        Color got(0, 0, 0, 0);
        const Rectangle probe(kSize / 2, kSize / 2, 1, 1);
        dev.GetBackBufferData(&probe, &got, 0, 1);
        return got;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();

        const VkPresentModeKHR modeVsyncOn = Renderer().GetAppliedPresentModeEXT();
        check(modeVsyncOn == VK_PRESENT_MODE_FIFO_KHR,
              "A vsync on starts at FIFO, the only mode Vulkan guarantees",
              PresentModeName(modeVsyncOn));

        // ---- vsync OFF, through the public route ----
        const std::uint64_t rebuildsBefore = Renderer().GetSwapchainRecreateCountEXT();
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
        gdm_->ApplyChanges();
        const VkPresentModeKHR modeVsyncOff = Renderer().GetAppliedPresentModeEXT();
        std::printf("[INFO] swapchain recreations across the vsync-off ApplyChanges(): %llu -> %llu\n",
                    static_cast<unsigned long long>(rebuildsBefore),
                    static_cast<unsigned long long>(Renderer().GetSwapchainRecreateCountEXT()));

        // The branch is taken on a MEASURED fact about the surface, never on the outcome. Asking
        // "did the mode change, and if not was that allowed" the other way round is an escape
        // hatch that excuses the defect: measured while writing this, a version that inferred
        // availability from the result still passed with SetSwapInterval reverted to its no-op.
        if (Renderer().SupportsUnsynchronisedPresentModeEXT()) {
            check(modeVsyncOff == VK_PRESENT_MODE_IMMEDIATE_KHR
                      || modeVsyncOff == VK_PRESENT_MODE_MAILBOX_KHR,
                  "A this surface offers an unsynchronised mode, so vsync off must reach one",
                  PresentModeName(modeVsyncOff));
        } else {
            check(modeVsyncOff == VK_PRESENT_MODE_FIFO_KHR,
                  "A this surface offers neither IMMEDIATE nor MAILBOX, so FIFO is the correct "
                  "answer for vsync off too",
                  PresentModeName(modeVsyncOff));
        }

        const Color afterOff = RenderProbe(dev);
        check(afterOff.getRProperty() > 200 && afterOff.getGProperty() < 60,
              "C rendering still works after the rebuild",
              "(" + std::to_string(afterOff.getRProperty()) + ","
                  + std::to_string(afterOff.getGProperty()) + ","
                  + std::to_string(afterOff.getBProperty()) + ")");

        // ---- back ON, and the mode must follow again ----
        gdm_->setSynchronizeWithVerticalRetraceProperty(true);
        gdm_->ApplyChanges();
        const VkPresentModeKHR modeBackOn = Renderer().GetAppliedPresentModeEXT();
        check(modeBackOn == VK_PRESENT_MODE_FIFO_KHR,
              "A turning vsync back on returns the swapchain to FIFO",
              PresentModeName(modeBackOn));

        // ---- re-requesting the interval already in force keeps the mode where it is ----
        gdm_->setSynchronizeWithVerticalRetraceProperty(true);
        gdm_->ApplyChanges();
        check(Renderer().GetAppliedPresentModeEXT() == VK_PRESENT_MODE_FIFO_KHR,
              "A re-requesting the interval already in force leaves the mode alone",
              PresentModeName(Renderer().GetAppliedPresentModeEXT()));

        const Color afterOn = RenderProbe(dev);
        check(afterOn.getRProperty() > 200 && afterOn.getGProperty() < 60,
              "C rendering still works after the second rebuild",
              "(" + std::to_string(afterOn.getRProperty()) + ","
                  + std::to_string(afterOn.getGProperty()) + ","
                  + std::to_string(afterOn.getBProperty()) + ")");

        const auto& messages = Renderer().GetValidationMessagesEXT();
        check(messages.empty(), "D no validation messages",
              messages.empty() ? "0 captured"
                               : std::to_string(messages.size()) + " captured, first: "
                                     + messages.front());

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    VulkanSwapIntervalTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    VulkanSwapIntervalTest g;
    g.Run();
    return g.getResult();
}

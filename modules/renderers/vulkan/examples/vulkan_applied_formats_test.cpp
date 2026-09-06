// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-348: the applied back-buffer and depth-stencil formats must be what
// is in effect, not what was asked for.
//
// The defect
// ----------
// `VulkanRenderer` overrode neither `GetAppliedBackBufferFormatEXT` nor
// `GetAppliedDepthStencilFormatEXT`, so both took the identity default while the renderer really
// substitutes: the swapchain takes what the surface offers rather than the requested
// `SurfaceFormat`, and `FindDepthFormat()`/`PickDepthFormat` choose a depth format the request
// does not name. `NormalizeAppliedPresentationFormats` writes those answers into
// `PresentationParameters`, so a game read back its own request as though it had been honoured.
//
// Which seam this test uses, and why
// ----------------------------------
// `GraphicsDevice::SetPresentationParameters` -- the CNAEXT store-only path -- for the same reason
// `VULKAN-347` had to use it: `Reset()` and `ApplyChanges` write the multisample count back from
// `ApplyMultiSampleCount`'s own return, and Game startup reaches `Reset()` too, so a leg driven
// through either passes with the fix reverted and proves nothing. The store-only path is the one
// that depends on the renderer telling the truth.
//
// The depth leg is deliberately asked with a request the device cannot have honoured verbatim.
// `Depth16` is the discriminating one: `FindDepthFormat()` tries stencil-capable formats FIRST and
// never returns `D16_UNORM` for the back buffer, so a renderer that echoes the request answers
// `Depth16` while the device runs a 24- or 32-bit format with a stencil plane -- and
// `SupportsCapability(StencilBuffer)`, which reads the same member this fix reads, already reports
// that plane as present. Those two answers cannot both be right.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <cstdio>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

namespace {

class AppliedFormatsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_     = false;
    int  failures_ = 0;

    void check(bool ok, const std::string& what)
    {
        std::printf("%s %s\n", ok ? "[ok]  " : "[FAIL]", what.c_str());
        if (!ok) ++failures_;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        auto* vk  = dynamic_cast<VulkanRenderer*>(&dev.GetRenderer());
        if (vk == nullptr)
        {
            std::printf("[FAIL] renderer is not the Vulkan renderer\n");
            ++failures_;
            Exit();
            return;
        }

        // Ask for a back-buffer format and a depth format the device cannot have honoured
        // verbatim, then read back what was stored.
        PresentationParameters wants = dev.getPresentationParametersProperty().Clone();
        wants.setBackBufferFormatProperty(SurfaceFormat::Bgr565);
        wants.setDepthStencilFormatProperty(DepthFormat::Depth16);
        dev.SetPresentationParameters(wants);

        const PresentationParameters& got = dev.getPresentationParametersProperty();

        // ---- leg A: the back buffer -------------------------------------------
        check(got.getBackBufferFormatProperty() == SurfaceFormat::Color,
              "A a Bgr565 request stores Color, the only format this swapchain produces "
              "(stored ordinal " +
                  std::to_string(static_cast<int>(got.getBackBufferFormatProperty())) + ")");

        // ---- leg B: the depth format ------------------------------------------
        const DepthFormat storedDepth = got.getDepthStencilFormatProperty();
        check(storedDepth != DepthFormat::Depth16,
              "B a Depth16 request does not store Depth16 -- FindDepthFormat() prefers "
              "stencil-capable formats and never returns D16 for the back buffer (stored " +
                  std::to_string(static_cast<int>(storedDepth)) + ")");

        // ---- leg C: and the stored answer agrees with the capability -----------
        // The two must not contradict: both read the same VkFormat. Without this leg, leg B is
        // satisfied by any value that merely differs from the request.
        const bool storedSaysStencil = (storedDepth == DepthFormat::Depth24Stencil8);
        const bool capabilitySaysStencil =
            dev.SupportsCapability(CNA::GraphicsCapability::StencilBuffer);
        check(storedSaysStencil == capabilitySaysStencil,
              std::string("C the stored depth format and SupportsCapability(StencilBuffer) agree "
                          "about the stencil plane (stored says ") +
                  (storedSaysStencil ? "yes" : "no") + ", capability says " +
                  (capabilitySaysStencil ? "yes" : "no") + ")");

        Exit();
    }

public:
    AppliedFormatsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return failures_ == 0 ? 0 : 1; }
};

} // namespace

int main()
{
    AppliedFormatsTest game;
    game.Run();
    return game.getResult();
}

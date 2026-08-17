// SPDX-License-Identifier: MS-PL
// plan_igl.md IGL-60: the Vulkan back buffer's physical format is whatever the platform window
// system surface natively offers -- on this renderer's Linux/X11/Mesa target that is
// `BGRA_UNorm8`, not `RGBA_UNorm8` (`igl::IFramebuffer::copyBytesColorAttachment()` copies raw
// physical bytes with no channel reordering on either backend). Every other renderer family, and
// IGL's own OpenGL back buffer, happen to be R-first, so a readback that assumed R-first byte
// order unconditionally silently swapped red and blue on Vulkan: a pure-red clear read back as
// pure blue. Green was accidentally unaffected (R and B are both already zero), which is what let
// this ship unnoticed until a from-scratch investigation of `Igl_2D`/`Igl_3D`'s existing Vulkan
// failures (see plan_igl.md's IGL-60 note) traced it to something Vulkan-specific and simple
// enough to isolate with clears alone, no draws at all.
//
// This test forces the Vulkan backend via its own `CNA_IGL_BACKEND=vulkan` ctest ENVIRONMENT
// entry (the same mechanism every other igl_*_test.cpp is registered with, just adding one more
// variable) specifically so it exercises the actual BGR code path, not merely re-confirming the
// OpenGL back buffer (already correct before this fix). `RunPixelTest<TGame>()`'s own
// VK_ERROR_SURFACE_LOST_KHR handling (`PixelTestGame.hpp`) already SKIPs cleanly on a display
// that lacks Vulkan WSI support, matching every other renderer's own DRI3-less-Xvfb safety net --
// this test relies on that same mechanism rather than adding a new one.
//
// Exit code 0 = all PASS, 1 = any FAIL, 77 = SKIP (no GPU/display, or no Vulkan WSI here).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "common/PixelTestGame.hpp"

#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
}

class IglVulkanBackBufferBgrTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        device.Clear(Color(static_cast<bytecs>(255), static_cast<bytecs>(0),
                           static_cast<bytecs>(0), static_cast<bytecs>(255)));
        ExpectPixel("a pure-red clear reads back red, not blue (BGR/RGB byte-order regression)",
                    Rectangle(kSize / 2, kSize / 2, 1, 1),
                    Color(static_cast<bytecs>(255), static_cast<bytecs>(0),
                          static_cast<bytecs>(0), static_cast<bytecs>(255)));

        // A second clear with all three channels distinct, so a channel swap in EITHER direction
        // (R<->B, or a more exotic reordering) is guaranteed to produce a value this cannot match,
        // unlike red/green/blue alone which can coincide with certain swaps by chance.
        device.Clear(Color(static_cast<bytecs>(30), static_cast<bytecs>(90),
                           static_cast<bytecs>(200), static_cast<bytecs>(255)));
        ExpectPixel("a mixed-channel clear reads back byte-exact, not channel-reordered",
                    Rectangle(kSize / 2, kSize / 2, 1, 1),
                    Color(static_cast<bytecs>(30), static_cast<bytecs>(90),
                          static_cast<bytecs>(200), static_cast<bytecs>(255)));
    }

public:
    IglVulkanBackBufferBgrTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<IglVulkanBackBufferBgrTest>();
}

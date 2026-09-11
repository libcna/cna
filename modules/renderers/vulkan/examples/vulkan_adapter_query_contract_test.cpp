// SPDX-License-Identifier: MS-PL
//
// plan_vulkan.md VULKAN-372 -- does `GraphicsAdapter` tell a game the same thing the device does?
//
// `GraphicsAdapter::QueryRenderTargetFormat` is the XNA call a game makes BEFORE creating anything,
// to find out what it may ask for. It reaches the renderer through the registry's `adapterQueries`
// hooks rather than through an `IGraphicsRenderer` virtual, because there is no device yet -- and
// **EasyGL registers those hooks while this renderer does not**, so the answer comes from a generic
// shared table. This test measures what that costs, rather than reasoning about it.
//
//   A  Control: the renderer really does refuse a non-`Color` render target. Without this leg, B
//      would be comparing two guesses.
//   B  The adapter's verdict for that same format, against the device's. A disagreement here means
//      a game is told it may ask for something that then throws.
//   C  MSAA: the adapter's `selectedMultiSampleCount` for a count the device supports.
//   D  `IsProfileSupported` answers, and is recorded rather than asserted -- the shared fallback
//      returns `true` for every profile on every renderer that supplies no hook, which is
//      deliberate (a hardcoded table pretending to be a capability query is worse), so the leg
//      exists to pin the value rather than to judge it.
//
// Exit code 0 = all PASS, 1 = any FAIL. Legs B and C report; what they report is the row's finding.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <cstdio>
#include <exception>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class VulkanAdapterQueryContractTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ok ? ++pass_ : ++fail_;
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();
        GraphicsAdapter& adapter = GraphicsAdapter::getDefaultAdapterProperty();

        // A. What the device really does with a non-Color render target.
        bool deviceRefuses = false;
        std::string refusal;
        try {
            RenderTarget2D rt(dev, 8, 8, false, SurfaceFormat::Bgr565, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            (void)rt;
        } catch (const std::exception& e) { deviceRefuses = true; refusal = e.what(); }
        check(deviceRefuses,
              "A control: the device refuses a Bgr565 render target: " +
                  (deviceRefuses ? refusal.substr(0, 90) : std::string("it did NOT refuse")));

        // B. What the adapter tells a game about the same format, before any of that.
        {
            SurfaceFormat selected = SurfaceFormat::Color;
            DepthFormat selectedDepth = DepthFormat::None;
            SharpRuntime::intcs selectedSamples = 0;
            const bool exact = adapter.QueryRenderTargetFormat(
                GraphicsProfile::HiDef, SurfaceFormat::Bgr565, DepthFormat::None, 0,
                selected, selectedDepth, selectedSamples);
            const bool agrees = (selected == SurfaceFormat::Color) && !exact;
            check(agrees,
                  "B the adapter agrees with the device about Bgr565: exact=" +
                      std::string(exact ? "true" : "false") + " selectedFormat=" +
                      std::to_string(static_cast<int>(selected)) +
                      " (Color is " + std::to_string(static_cast<int>(SurfaceFormat::Color)) +
                      "; disagreeing means a game is told it may ask for what then throws)");
        }

        // C. MSAA, which this device does support.
        {
            SurfaceFormat selected = SurfaceFormat::Color;
            DepthFormat selectedDepth = DepthFormat::Depth24Stencil8;
            SharpRuntime::intcs selectedSamples = 0;
            const bool exact = adapter.QueryRenderTargetFormat(
                GraphicsProfile::HiDef, SurfaceFormat::Color, DepthFormat::Depth24Stencil8, 4,
                selected, selectedDepth, selectedSamples);
            // Was a MEASUREMENT under `VULKAN-372`, which found it reporting 0 on a device with
            // 4x or 8x (F-34); `VULKAN-187` registered this renderer's clamp and it is an
            // assertion now. Four rather than the device maximum on purpose: llvmpipe offers 4x
            // and RADV 8x, and this leg has to mean the same thing on both.
            check(selectedSamples == 4 && exact,
                  "C the adapter reports the 4x MSAA this device supports: exact=" +
                      std::string(exact ? "true" : "false") + " selectedMultiSampleCount=" +
                      std::to_string(static_cast<int>(selectedSamples)) +
                      " (0 would mean no adapter hook answered -- F-34)");
        }

        // D. Recorded, not judged.
        {
            const bool reach = adapter.IsProfileSupported(GraphicsProfile::Reach);
            const bool hidef = adapter.IsProfileSupported(GraphicsProfile::HiDef);
            check(true, std::string("D IsProfileSupported, recorded: Reach=") +
                            (reach ? "true" : "false") + " HiDef=" + (hidef ? "true" : "false"));
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    VulkanAdapterQueryContractTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanAdapterQueryContractTest game;
    game.Run();
    return game.getResult();
}

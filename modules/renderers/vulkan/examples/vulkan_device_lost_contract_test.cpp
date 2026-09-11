// SPDX-License-Identifier: MS-PL
//
// plan_vulkan.md VULKAN-334 -- what this renderer does when the Vulkan device is lost.
//
// **What CNA promises a game.** `GraphicsDevice` carries XNA's three events -- `DeviceLost`,
// `DeviceResetting`, `DeviceReset` -- plus `GraphicsDeviceStatus` and `ContentLost` on tracked
// resources. They are delivered by the renderer, through
// `GraphicsRendererCreateArgs::deviceEventCallback`, and before this row **nine of the ten
// renderers never called it**; Vulkan did not mention `VK_ERROR_DEVICE_LOST` anywhere at all, so a
// lost device surfaced as whatever generic failure the next call happened to raise.
//
// **What this renderer does now, and what it deliberately does not.** Every call that can report
// the loss -- submit, present, acquire, fence wait -- is checked, the loss is reported **once** to
// the shared layer so a game's own `DeviceLost` handler runs and the status becomes `Lost`, and
// then the call fails with a message that names the entry point and says a reset is not attempted.
// It is not attempted because a lost `VkDevice` cannot be recovered: the specification requires
// destroying it and every object made from it, which under live `Texture2D`/`RenderTarget2D`/
// `Effect` wrappers is a different feature from D3D9's `Reset` and is not one this renderer has.
// The explicit non-goal of the row that opened this is a GL-shaped loss/restore state machine
// built by analogy, and there is none here.
//
//   A  A game's `DeviceLost` handler runs, and `GraphicsDeviceStatus` becomes `Lost`.
//   B  The failure names the call and says the reset is not attempted, rather than surfacing as a
//      bare "vkQueueSubmit failed".
//   C  The loss is reported ONCE. A lost device fails every later call the same way, and a game
//      told again on each of them cannot tell one loss from many.
//   D  Before any of that, the device is NOT lost and the status is Normal -- the control that
//      stops A-C from passing on a renderer that reports a loss at startup.
//
// The loss is injected rather than provoked: no driver here exposes a way to make a real device
// loss happen inside the process. The injection replaces the RESULT of one check; everything after
// it is the production path.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDeviceStatus.hpp"
#include "System/EventArgs.hpp"

#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <cstdio>
#include <exception>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

class VulkanDeviceLostContractTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;
    int lostEvents_ = 0;

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

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.DeviceLost += [this](System::Object*, const System::EventArgs&) { ++lostEvents_; };

        check(!Renderer().IsDeviceLostEXT() &&
                  dev.getGraphicsDeviceStatusProperty() == GraphicsDeviceStatus::Normal &&
                  lostEvents_ == 0,
              "D control: before the injection the device is not lost and the status is Normal");

        std::string what;
        bool threw = false;
        Renderer().InjectDeviceLostForTestEXT(1);
        try { EndDraw(); }
        catch (const std::exception& e) { threw = true; what = e.what(); }

        check(threw && lostEvents_ == 1 &&
                  dev.getGraphicsDeviceStatusProperty() == GraphicsDeviceStatus::Lost,
              "A the game's DeviceLost handler ran and the status is Lost: events=" +
                  std::to_string(lostEvents_) + " status=" +
                  std::to_string(static_cast<int>(dev.getGraphicsDeviceStatusProperty())));
        check(threw && what.find("VK_ERROR_DEVICE_LOST") != std::string::npos &&
                  what.find("does not attempt a reset") != std::string::npos,
              "B the failure names the cause and the refusal: " +
                  (threw ? what : std::string("nothing thrown")));

        // A lost device fails every later call. The event must not fire again.
        Renderer().InjectDeviceLostForTestEXT(1);
        try { EndDraw(); } catch (const std::exception&) { }
        check(lostEvents_ == 1,
              "C the loss is reported once, not once per failing call: events=" +
                  std::to_string(lostEvents_));

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    VulkanDeviceLostContractTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanDeviceLostContractTest game;
    game.Run();
    return game.getResult();
}

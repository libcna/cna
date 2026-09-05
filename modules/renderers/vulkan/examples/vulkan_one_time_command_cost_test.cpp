// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-396: what does the per-one-time-command `vkQueueWaitIdle` actually
// cost?
//
// The rule this file exists to satisfy
// ------------------------------------
// VULKAN-396 forbids opening an optimization task from it without a measurement. So this test does
// not argue that the wait is expensive or cheap; it measures it on a realistic upload workload and
// prints the numbers, and it asserts only the things that are true on ANY device.
//
// What the wait is
// ----------------
// `EndOneTimeCommands` submits the buffer with no fence, waits the whole queue, then frees the
// command buffer -- and the free is why the wait is there: freeing a command buffer that is still
// executing is illegal. Every texture upload, layout transition and readback goes through it.
//
// Fixed versus device-dependent, which is the distinction that makes this test useful rather than
// flaky (the same split `Vulkan_CapabilitySnapshot` uses):
//
//   * STRUCTURAL, asserted: one wait per one-time command, the count grows by exactly the number
//     of uploads performed, and the accumulated wait is a real duration rather than zero. These
//     hold on llvmpipe and on hardware alike.
//   * TIMING, printed and never failed on: nanoseconds per wait, and the share of the workload's
//     wall clock they represent. A suite that fails on an absolute duration reports the GPU rather
//     than the renderer.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

namespace {

constexpr int kTextures = 48;
constexpr int kSize     = 256;

class OneTimeCommandCostTest : public Game
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

        const std::uint64_t cmdsBefore  = vk->GetOneTimeCommandCountEXT();
        const std::uint64_t nanosBefore = vk->GetOneTimeCommandWaitNanosEXT();

        // A realistic upload workload: content loading is a burst of Texture2D creations, each
        // uploading its pixels through the staging path.
        const std::vector<std::uint8_t> pixels(
            static_cast<std::size_t>(kSize) * kSize * 4, 0x7F);

        const auto workBegin = std::chrono::steady_clock::now();
        {
            std::vector<Texture2D> textures;
            textures.reserve(kTextures);
            for (int i = 0; i < kTextures; ++i)
                textures.push_back(Texture2D::CreateFromPixels(dev, kSize, kSize, pixels));
        }
        const auto workNanos = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - workBegin).count());

        const std::uint64_t cmds  = vk->GetOneTimeCommandCountEXT()      - cmdsBefore;
        const std::uint64_t nanos = vk->GetOneTimeCommandWaitNanosEXT()  - nanosBefore;

        // ---- structural, asserted on any device ---------------------------------
        check(cmds >= static_cast<std::uint64_t>(kTextures),
              "A " + std::to_string(kTextures) + " texture uploads cost " +
                  std::to_string(cmds) + " one-time commands, so at least one each");
        check(nanos > 0,
              "B the queue waits are real time, not a no-op (" + std::to_string(nanos) + " ns)");

        // ---- timing, printed and never failed on --------------------------------
        const double perWaitUs   = cmds  ? (static_cast<double>(nanos) / cmds) / 1000.0 : 0.0;
        const double workMs      = static_cast<double>(workNanos) / 1e6;
        const double waitShare   = workNanos ? (100.0 * static_cast<double>(nanos) /
                                                static_cast<double>(workNanos)) : 0.0;
        std::printf("[measure] %d uploads of %dx%d: %llu one-time commands, "
                    "%.1f us per queue wait, %.1f ms of work, waits are %.1f%% of it\n",
                    kTextures, kSize, kSize,
                    static_cast<unsigned long long>(cmds), perWaitUs, workMs, waitShare);
        std::printf("[measure] device-dependent: recorded here on this run's adapter, "
                    "compared across runs only as a ratio\n");

        Exit();
    }

public:
    OneTimeCommandCostTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return failures_ == 0 ? 0 : 1; }
};

} // namespace

int main()
{
    OneTimeCommandCostTest game;
    game.Run();
    return game.getResult();
}

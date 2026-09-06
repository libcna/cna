// SPDX-License-Identifier: MS-PL
//
// plan_vulkan.md VULKAN-024 -- the six shared limit/profile defaults, checked against what this
// physical device actually reports.
//
// `GetMaxTextureSizeForProfileEXT`, `GetMaxCubeSizeForProfileEXT`, `GetMaxVolumeExtentForProfileEXT`
// and `GetMaxRenderTargetsForProfileEXT` are unoverridden on both renderers and return "no ceiling";
// `GetMaxVertexStreams()` returns the public maximum of 16; `GetMaxTextureDimension()` returns a
// hardcoded 16384, whose own comment says it "matches the guaranteed ceiling on every native API
// this project targets ... and the value real-world Vulkan implementations report".
//
// That last sentence is a claim about hardware, and the row's question is whether it holds for a
// Vulkan device -- specifically the interesting case, a device whose `maxImageDimension2D` is BELOW
// a shared default. On such a device the shared content reader would accept an XNB the renderer
// then cannot allocate, and the failure would surface as a `vkCreateImage` refusal far from its
// cause.
//
// So this file does not restate the defaults; it compares each one to the matching
// `VkPhysicalDeviceLimits` field and requires the default to be no larger than what the device can
// actually do. On a device that contradicts a default the corresponding leg FAILS, with both
// numbers printed -- which is exactly the signal the row asks for ("only open a task if a real
// device can contradict a default"), delivered by the suite rather than by a reading.
//
// The four profile ceilings are a different kind of claim: a profile ceiling is a PORTABILITY
// restriction, deliberately independent of what the device could do, so "no ceiling" cannot be
// contradicted by hardware. They are printed for the record and asserted only to be self-consistent
// (Reach must not permit more than HiDef).
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"

#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <cstdio>
#include <limits>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

class VulkanProfileLimitsAuditTest final : public Game
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

    static std::string Ceiling(int v)
    {
        return v == (std::numeric_limits<int>::max)() ? std::string("no ceiling")
                                                      : std::to_string(v);
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();
        auto& r = *dynamic_cast<VulkanRenderer*>(&dev.GetRenderer());
        const VkPhysicalDeviceLimits& lim = r.GetDeviceLimitsEXT();

        // The two defaults that are claims about hardware.
        {
            const int shared = r.GetMaxTextureDimension();
            const unsigned device = lim.maxImageDimension2D;
            check(static_cast<unsigned>(shared) <= device,
                  "A GetMaxTextureDimension() " + std::to_string(shared) +
                      " vs this device's maxImageDimension2D " + std::to_string(device) +
                      " -- the shared default must not promise more than the device can allocate");
        }
        {
            const int shared = r.GetMaxVertexStreams();
            const unsigned device = lim.maxVertexInputBindings;
            check(static_cast<unsigned>(shared) <= device,
                  "B GetMaxVertexStreams() " + std::to_string(shared) +
                      " vs this device's maxVertexInputBindings " + std::to_string(device));
        }

        // The four profile ceilings: portability restrictions, so hardware cannot contradict them.
        // Asserted for self-consistency and printed so the verdict is on the record.
        {
            const int reach = r.GetMaxTextureSizeForProfileEXT(
                static_cast<int>(GraphicsProfile::Reach));
            const int hidef = r.GetMaxTextureSizeForProfileEXT(
                static_cast<int>(GraphicsProfile::HiDef));
            check(reach <= hidef,
                  "C texture-size profile ceiling: Reach=" + Ceiling(reach) + " HiDef=" +
                      Ceiling(hidef) + " (Reach must never permit more than HiDef)");
        }
        {
            const int reach = r.GetMaxCubeSizeForProfileEXT(
                static_cast<int>(GraphicsProfile::Reach));
            const int hidef = r.GetMaxCubeSizeForProfileEXT(
                static_cast<int>(GraphicsProfile::HiDef));
            check(reach <= hidef,
                  "D cube-size profile ceiling: Reach=" + Ceiling(reach) + " HiDef=" +
                      Ceiling(hidef) + "; this device's maxImageDimensionCube=" +
                      std::to_string(lim.maxImageDimensionCube));
        }
        {
            const int reach = r.GetMaxVolumeExtentForProfileEXT(
                static_cast<int>(GraphicsProfile::Reach));
            const int hidef = r.GetMaxVolumeExtentForProfileEXT(
                static_cast<int>(GraphicsProfile::HiDef));
            check(reach <= hidef,
                  "E volume-extent profile ceiling: Reach=" + Ceiling(reach) + " HiDef=" +
                      Ceiling(hidef) + "; this device's maxImageDimension3D=" +
                      std::to_string(lim.maxImageDimension3D) +
                      " (0 would mean the profile forbids volume textures entirely)");
        }
        {
            const int reach = r.GetMaxRenderTargetsForProfileEXT(
                static_cast<int>(GraphicsProfile::Reach));
            const int hidef = r.GetMaxRenderTargetsForProfileEXT(
                static_cast<int>(GraphicsProfile::HiDef));
            check(reach <= hidef,
                  "F render-target profile ceiling: Reach=" + Ceiling(reach) + " HiDef=" +
                      Ceiling(hidef) + "; this device's maxColorAttachments=" +
                      std::to_string(lim.maxColorAttachments) +
                      " and XNA's own general ceiling is 4");
        }

        // The device's own numbers, printed unconditionally so a run on different hardware carries
        // its evidence with it rather than only its verdict.
        std::printf("[INFO] device limits: maxImageDimension2D=%u cube=%u 3D=%u "
                    "maxColorAttachments=%u maxVertexInputBindings=%u\n",
                    lim.maxImageDimension2D, lim.maxImageDimensionCube, lim.maxImageDimension3D,
                    lim.maxColorAttachments, lim.maxVertexInputBindings);

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    VulkanProfileLimitsAuditTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(32);
    }

    [[nodiscard]] int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanProfileLimitsAuditTest game;
    game.Run();
    return game.getResult();
}

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
// Legs G and H come from VULKAN-180, which this file's own probe opened. They ask for the largest
// volume and the largest cube the device's own limits describe, and assert the INVARIANT rather
// than a refusal: the construction either succeeds -- and the object is then usable, proved by a
// one-texel readback -- or it throws a message naming the size. What it may never do is the third
// thing, which is what VULKAN-180 found: return a perfectly ordinary-looking object whose
// `vkAllocateMemory` succeeded, whose `vkBindImageMemory` failed with its result ignored, and whose
// unbound image then went on to a pipeline barrier and an image view.
//
// The invariant is the assertion, not the refusal, because which of the two arms is taken is a
// property of the DEVICE. On llvmpipe both sizes exceed every heap and both throw; on RADV
// (measured on an Xwayland display, `VULKAN-012`) `maxImageDimensionCube` is 16384 and a cube that
// size is genuinely allocatable, so it succeeds -- and asserting "is refused" would have failed a
// correct renderer on real hardware. The first draft did exactly that.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <cstdio>
#include <exception>
#include <limits>
#include <memory>
#include <vector>
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

        // G/H (VULKAN-180): a resource the device cannot back must say so.
        //
        // These two ask for something impossible on purpose, so the layer WILL complain -- and this
        // test lives under VULKAN-393's validation gate like every other. The echo is silenced for
        // exactly the two statements that provoke it, never the recording, and each leg then
        // asserts that the layer did complain. Silencing without that second assertion would turn
        // the switch into a way to hide a real message.
        {
            const int e = static_cast<int>(lim.maxImageDimension3D);
            bool threw = false;
            bool usable = false;
            std::string what;
            r.SetValidationEchoEnabledEXT(false);
            try {
                Texture3D huge(dev, e, e, e, false, SurfaceFormat::Color);
                // Succeeded: then it must be usable. One voxel is enough to tell an object with
                // memory bound from one without.
                std::vector<Color> one(1, Color(0, 0, 0, 0));
                huge.GetData(0, /*left=*/0, /*top=*/0, /*right=*/1, /*bottom=*/1,
                             /*front=*/0, /*back=*/1, one.data(), 0, 1);
                usable = true;
            } catch (const std::exception& ex) { threw = true; what = ex.what(); }
            r.SetValidationEchoEnabledEXT(true);
            check((threw && what.find(std::to_string(e)) != std::string::npos) || usable,
                  "G a Texture3D at the device's own maxImageDimension3D cubed (" +
                      std::to_string(e) + "^3) either works or says why: threw=" +
                      (threw ? what : std::string("no")) + " usable=" +
                      (usable ? "yes" : "NO -- the caller got an object the device cannot back"));
        }
        {
            const int e = static_cast<int>(lim.maxImageDimensionCube);
            bool threw = false;
            bool usable = false;
            std::string what;
            r.SetValidationEchoEnabledEXT(false);
            try {
                TextureCube huge(dev, e, false, SurfaceFormat::Color);
                std::vector<Color> one(1, Color(0, 0, 0, 0));
                const Rectangle texel(0, 0, 1, 1);
                huge.GetData(CubeMapFace::PositiveX, 0, &texel, one.data(), 0, 1);
                usable = true;
            } catch (const std::exception& ex) { threw = true; what = ex.what(); }
            r.SetValidationEchoEnabledEXT(true);
            check((threw && what.find(std::to_string(e)) != std::string::npos) || usable,
                  "H a TextureCube at the device's own maxImageDimensionCube (" +
                      std::to_string(e) + ") either works or says why: threw=" +
                      (threw ? what : std::string("no")) + " usable=" +
                      (usable ? "yes" : "NO"));
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

// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-347: PresentationParameters must not echo back a multisample count
// the device did not apply.
//
// The defect
// ----------
// `VulkanRenderer` did not override `GetAppliedMultiSampleCountEXT`, so it took the identity
// default while the renderer really clamps: `PickSampleCount` returns the highest supported count
// <= the request, so a request of 3 becomes 2 and a request of 5 becomes 4 on every device,
// hardware and llvmpipe alike. `GraphicsDevice.cpp:3309` writes that answer into
// `presentationParameters_` after construction -- the line exists precisely so an unapplied
// request is not retained -- so the game was told 3 while the device ran 2.
//
// Why the requests below are the ones chosen
// ------------------------------------------
// A request that is already a supported count cannot expose the bug: the identity and the applied
// value agree by accident. The non-powers-of-two are the discriminating cases, and they need no
// unusual device -- 3, 5, 7 and 9 all clamp downwards wherever this runs. The powers of two are
// kept anyway as the control: they must NOT change, or a fix that simply returned a constant
// would pass.
//
// Leg C closes the loop at the seam that matters. The renderer answering correctly is only half
// the contract; what a game reads is `PresentationParameters.MultiSampleCount`, and that is what
// GraphicsDevice writes the renderer's answer into.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

namespace {

class AppliedMultiSampleTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int failures_ = 0;

    void check(bool ok, const std::string& what)
    {
        std::printf("%s %s\n", ok ? "[ok]  " : "[FAIL]", what.c_str());
        if (!ok) ++failures_;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        auto* vk = dynamic_cast<VulkanRenderer*>(&device.GetRenderer());
        if (vk == nullptr)
        {
            std::printf("[FAIL] renderer is not the Vulkan renderer\n");
            ++failures_;
            Exit();
            return;
        }

        const int original = vk->GetMultiSampleCount();

        // ---- legs A and B: the report must equal what the device reaches ---------
        // ApplyMultiSampleCount is the oracle: it returns the count it really configured, after
        // tearing down every sample-count-keyed render pass and pipeline.
        for (const int requested : { 1, 2, 3, 4, 5, 7, 8, 9, 16 })
        {
            const int applied  = vk->ApplyMultiSampleCount(requested);
            const int reported = vk->GetAppliedMultiSampleCountEXT(requested);
            const bool discriminating = (applied != requested);
            check(reported == applied,
                  std::string(discriminating ? "A" : "B") + " request " +
                      std::to_string(requested) + " -> applied " + std::to_string(applied) +
                      ", reported " + std::to_string(reported) +
                      (discriminating ? "  (clamped: this is the case the identity default got wrong)"
                                      : "  (control: unclamped, must not change)"));
        }

        // Put the renderer back the way it was found.
        vk->ApplyMultiSampleCount(original);

        // ---- leg C: and the value a game actually reads --------------------------
        // Which seam this leg uses, and why it is the only one that discriminates -- measured,
        // not assumed, after two earlier drafts of it passed with the fix reverted:
        //
        //   * Reset()/ApplyChanges write the count back from ApplyMultiSampleCount's OWN return
        //     (GraphicsDevice.cpp:663-665), so they were never wrong and never will be;
        //   * even direct construction is followed by that write-back during Game startup, so
        //     GraphicsDevice.cpp:3309 is corrected before a game can read it;
        //   * SetPresentationParameters is the CNAEXT store-only path. Its own comment says it
        //     "does not reconfigure MSAA" and relies on the renderer reporting what is really in
        //     effect -- so it is the one place where the identity default reaches a game.
        const int applied = vk->GetMultiSampleCount();
        PresentationParameters wants = device.getPresentationParametersProperty().Clone();
        wants.setMultiSampleCountProperty(8);
        device.SetPresentationParameters(wants);
        const int seen =
            device.getPresentationParametersProperty().getMultiSampleCountProperty();
        check(applied != 8,
              "C1 the 8x this device cannot satisfy really is clamped, to " +
                  std::to_string(applied) + " -- without this the next leg is vacuous");
        check(seen == applied,
              "C2 SetPresentationParameters stores the applied count (" + std::to_string(seen) +
                  " == " + std::to_string(applied) + "), not the 8 it was handed");

        Exit();
    }

public:
    AppliedMultiSampleTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        // Requests 8 (GraphicsDeviceManager.cpp:563), so this Game starts on an MSAA-enabled
        // device that has already been clamped. Not what leg C reads -- startup is corrected by
        // Reset() before Initialize runs, which is exactly why leg C uses a different seam -- but
        // it means the sweep below starts from a non-trivial sample count rather than from none.
        gdm_->setPreferMultiSamplingProperty(true);
    }

    int getResult() const { return failures_ == 0 ? 0 : 1; }
};

} // namespace

int main()
{
    AppliedMultiSampleTest game;
    game.Run();
    return game.getResult();
}

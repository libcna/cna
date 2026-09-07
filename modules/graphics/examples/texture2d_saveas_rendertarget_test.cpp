// SPDX-License-Identifier: MS-PL
//
// plan_vulkan.md VULKAN-178 -- what `SaveAsPng`/`SaveAsJpeg` actually read, measured rather than
// assumed.
//
// The row was written expecting these to "go through readback and upload paths that differ per
// renderer". They do not: `Texture2D::SaveAsPng` encodes `cpuPixels_`, a CPU-side shadow the
// texture keeps of whatever was last uploaded to it, and never asks the renderer for anything. So
// the interesting question is not whether Vulkan's readback is right here -- it is what happens
// when there IS no shadow, which is exactly the case a game hits when it saves a screenshot: a
// `RenderTarget2D` it drew into and never called `SetData` on.
//
//   A  Control: a Texture2D that was SetData'd saves, and the bytes decode back to its texels.
//      Without this leg, leg B's answer could be "saving is broken here" rather than "there is
//      nothing to save".
//   B  A RenderTarget2D drawn into and never SetData'd: what does SaveAsPng do? Recorded as a
//      measurement, whatever it is.
//   C  ...and after `GetData` has read the target back into the game's own array, does the answer
//      change? That is the idiom a game would reach for next, so whether it works decides
//      whether the gap has a workaround or not.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/IO/MemoryStream.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
constexpr int kN = 4;
const Color kFill(20, 180, 90, 255);
}  // namespace

class VulkanTexture2DSaveAsRenderTargetTest final : public Game
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

    /// Saves through the stream overload and reports what happened, without deciding whether that
    /// is right -- the legs do that.
    static std::string TrySave(const Texture2D& tex, std::size_t& bytes)
    {
        bytes = 0;
        System::IO::MemoryStream ms;
        try {
            tex.SaveAsPng(&ms, tex.getWidthProperty(), tex.getHeightProperty());
        } catch (const std::exception& e) {
            return std::string("threw: ") + e.what();
        }
        bytes = static_cast<std::size_t>(ms.getLengthProperty());
        return "saved " + std::to_string(bytes) + " bytes";
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();

        {
            Texture2D tex(dev, 2, 2, false, SurfaceFormat::Color);
            const std::array<std::uint8_t, 16> px{
                kFill.getRProperty(), kFill.getGProperty(), kFill.getBProperty(), 255,
                kFill.getRProperty(), kFill.getGProperty(), kFill.getBProperty(), 255,
                kFill.getRProperty(), kFill.getGProperty(), kFill.getBProperty(), 255,
                kFill.getRProperty(), kFill.getGProperty(), kFill.getBProperty(), 255};
            tex.SetDataRGBA(px.data(), 4);
            std::size_t bytes = 0;
            const std::string what = TrySave(tex, bytes);
            check(bytes > 0, "A control: an uploaded Texture2D saves as PNG -- " + what);
        }

        RenderTarget2D rt(dev, kN, kN, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.SetRenderTarget(&rt);
        dev.Clear(kFill);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        std::size_t rtBytes = 0;
        const std::string rtWhat = TrySave(rt, rtBytes);
        check(true, "B MEASUREMENT: a RenderTarget2D drawn into and never SetData'd -- " + rtWhat);

        std::vector<Color> readback(static_cast<std::size_t>(kN * kN), Color(0, 0, 0, 0));
        rt.GetData(readback.data(), 0, kN * kN);
        const bool readBackFine = readback[0].getRProperty() == kFill.getRProperty() &&
                                  readback[0].getGProperty() == kFill.getGProperty() &&
                                  readback[0].getBProperty() == kFill.getBProperty();
        std::size_t afterBytes = 0;
        const std::string afterWhat = TrySave(rt, afterBytes);
        check(readBackFine,
              "C control: GetData reads the target's real pixels back (" +
                  std::to_string(readback[0].getRProperty()) + "," +
                  std::to_string(readback[0].getGProperty()) + "," +
                  std::to_string(readback[0].getBProperty()) + ")");
        check(true, "C MEASUREMENT: SaveAsPng after GetData -- " + afterWhat);

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    VulkanTexture2DSaveAsRenderTargetTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanTexture2DSaveAsRenderTargetTest game;
    game.Run();
    return game.getResult();
}

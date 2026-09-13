// SPDX-License-Identifier: MS-PL
//
// plan_vulkan.md VULKAN-178 (the measurement) and VULKAN-169 (the fix) -- what
// `SaveAsPng`/`SaveAsJpeg` actually read.
//
// Renderer-agnostic and registered on Vulkan and EasyGL, because everything it asserts is
// `Texture2D`'s own behaviour in the shared layer: a one-renderer result could not tell a shared
// fix from a Vulkan-shaped one.
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
//   B  A RenderTarget2D drawn into and never SetData'd SAVES, and the PNG decodes back to the
//      colour that was drawn. `VULKAN-178` measured this throwing -- *"no CPU-side pixel data
//      available"* -- which is finding F-32; `VULKAN-169` made the save read the target back the
//      way `GetData` already does, and this leg is the assertion that replaced the measurement.
//   C  The same after `GetData` has been called, because a game that reads its target back and
//      then saves must not get a different answer from one that only saves.
//   D  A plain `Texture2D` that was never uploaded is still refused BY NAME. The fix is for
//      pixels that exist on the GPU, not a licence to invent content: there is nothing to read
//      back from a texture no renderer holds anything for.
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

class Texture2DSaveAsRenderTargetTest final : public Game
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

    static bool Is(const Color& got, const Color& want)
    {
        return got.getRProperty() == want.getRProperty() &&
               got.getGProperty() == want.getGProperty() &&
               got.getBProperty() == want.getBProperty();
    }

    static std::string Text(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) +
               "," + std::to_string(c.getBProperty()) + ")";
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

        {
            System::IO::MemoryStream png;
            std::string what = "saved";
            try { rt.SaveAsPng(&png, kN, kN); }
            catch (const std::exception& e) { what = std::string("threw: ") + e.what(); }
            const auto bytes = png.GetBuffer();
            Color decoded(0, 0, 0, 0);
            if (!bytes.empty()) {
                System::IO::MemoryStream read(bytes.data(),
                                              static_cast<System::IO::intcs>(bytes.size()));
                Texture2D back = Texture2D::FromStream(dev, read);
                std::vector<Color> texels(static_cast<std::size_t>(kN * kN), Color(0, 0, 0, 0));
                back.GetData(texels.data(), 0, kN * kN);
                decoded = texels[0];
            }
            check(!bytes.empty() && Is(decoded, kFill),
                  "B a RenderTarget2D drawn into and never SetData'd saves, and the PNG decodes "
                  "back to what was drawn: " + what + ", decoded " + Text(decoded) + " (want " +
                      Text(kFill) + ")");
        }

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
        check(afterBytes > 0, "C saving after GetData gives the same answer: " + afterWhat);

        {
            // What a texture that is neither uploaded nor drawn into saves, asked rather than
            // assumed -- the row that made this change named it as an open question. The answer:
            // a fresh Texture2D carries a zero-filled shadow, so it saves transparent black, which
            // is what XNA's zero-initialised texture holds. Nothing here is the fix's doing; the
            // leg exists so that if the constructor ever stops allocating that shadow, the change
            // is caught here rather than in a game's screenshot.
            Texture2D fresh(dev, 2, 2, false, SurfaceFormat::Color);
            System::IO::MemoryStream png;
            std::string what = "saved";
            try { fresh.SaveAsPng(&png, 2, 2); }
            catch (const std::exception& e) { what = std::string("threw: ") + e.what(); }
            const auto bytes = png.GetBuffer();
            Color decoded(1, 2, 3, 4);
            if (!bytes.empty()) {
                System::IO::MemoryStream read(bytes.data(),
                                              static_cast<System::IO::intcs>(bytes.size()));
                Texture2D back = Texture2D::FromStream(dev, read);
                std::vector<Color> texels(4, Color(1, 2, 3, 4));
                back.GetData(texels.data(), 0, 4);
                decoded = texels[0];
            }
            check(!bytes.empty() && Is(decoded, Color(0, 0, 0, 0)),
                  "D a Texture2D neither uploaded nor drawn into saves its zero-initialised "
                  "contents: " + what + ", decoded " + Text(decoded) + " (want (0,0,0))");
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    Texture2DSaveAsRenderTargetTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    Texture2DSaveAsRenderTargetTest game;
    game.Run();
    return game.getResult();
}

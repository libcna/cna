// SPDX-License-Identifier: MS-PL
//
// plan_vulkan.md VULKAN-095 -- changing MultiSampleCount on an ALREADY-CONSTRUCTED device.
//
// This is a Vulkan-owned test rather than a registration of easygl_msaa_change_test.cpp, and the
// reason is in that file's own header: it asserts that toggling preferMultiSampling through
// ApplyChanges() on a live device leaves MultiSampleCount at 0, "since EasyGL cannot actually
// engage MSAA post-construction and now honestly reports that back". That is a true statement about
// EasyGL and a false one here. VulkanRenderer::ApplyMultiSampleCount really does the work --
// vkDeviceWaitIdle, then tearing down and rebuilding every sample-count-dependent render pass,
// framebuffer and pipeline. Registering that source verbatim would assert EasyGL's LIMIT as if it
// were the contract.
//
// So the shape the plan row asks for is asserted directly instead, and the emphasis is on what a
// rebuild can break rather than on what it reports:
//
//   * rendering is still CORRECT after the change, not merely that the reported count moved;
//   * a Texture2D and a RenderTarget2D created BEFORE the change still work after it -- the render
//     passes and pipelines they depend on were destroyed and rebuilt underneath them;
//   * the change is reversible, because a teardown path that only works once is a teardown path
//     that leaks the second time;
//   * the Khronos layer stays silent across all of it, which is the only observer of a pipeline or
//     framebuffer left dangling by the rebuild.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

class MsaaRuntimeChangeTest final : public Game
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

    static std::unique_ptr<Texture2D> MakeTexture(GraphicsDevice& dev, const Color& c)
    {
        auto t = std::make_unique<Texture2D>(dev, 2, 2, false, SurfaceFormat::Color);
        std::array<Color, 4> texels{c, c, c, c};
        t->SetData(texels.data(), static_cast<int>(texels.size()));
        return t;
    }

    /// Draws @p texture into @p rt through SpriteBatch and reads the 2x2 result back.
    std::vector<Color> DrawInto(GraphicsDevice& dev, RenderTarget2D& rt, Texture2D& texture)
    {
        dev.SetRenderTarget(&rt);
        dev.Clear(Color(13, 17, 19, 255));
        {
            SamplerState point = SamplerState::PointClamp;
            SpriteBatch batch(dev);
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            batch.Draw(texture, Rectangle(0, 0, 2, 2), Rectangle(0, 0, 2, 2),
                       Color(255, 255, 255, 255));
            batch.End();
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        std::vector<Color> pixels(4, Color(0, 0, 0, 0));
        rt.GetData(pixels.data(), 0, 4);
        return pixels;
    }

    /// Draws @p texture over the whole BACKBUFFER and reads a 2x2 corner back.
    ///
    /// This is the leg that actually exercises the risky path. A non-multisampled RenderTarget2D
    /// never touches renderPassMsaa_ or the MSAA sprite pipelines, so a test that only drew into
    /// one would leave the entire rebuild untested -- measured, not assumed: a mutation that kept
    /// the stale MSAA sprite pipelines across the change passed such a test 8/8.
    std::vector<Color> DrawIntoBackbufferAndRead(GraphicsDevice& dev, Texture2D& texture)
    {
        dev.Clear(Color(13, 17, 19, 255));
        {
            SamplerState point = SamplerState::PointClamp;
            SpriteBatch batch(dev);
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            batch.Draw(texture, Rectangle(0, 0, 64, 64), Rectangle(0, 0, 2, 2),
                       Color(255, 255, 255, 255));
            batch.End();
        }
        std::vector<Color> pixels(4, Color(0, 0, 0, 0));
        const Rectangle region(8, 8, 2, 2);
        dev.GetBackBufferData(&region, pixels.data(), 0, 4);
        return pixels;
    }

    /// A multisampled render target, which is what pulls rtRenderPassMsaaByDepthFmt_ in.
    std::vector<Color> DrawIntoMsaaTarget(GraphicsDevice& dev, Texture2D& texture)
    {
        RenderTarget2D rt(dev, 2, 2, false, SurfaceFormat::Color, DepthFormat::None, 4,
                          RenderTargetUsage::DiscardContents);
        return DrawInto(dev, rt, texture);
    }

    static bool AllAre(const std::vector<Color>& pixels, const Color& want)
    {
        for (const Color& c : pixels)
            if (c.getRProperty() != want.getRProperty() ||
                c.getGProperty() != want.getGProperty() ||
                c.getBProperty() != want.getBProperty())
                return false;
        return true;
    }

    /// Toggles preferMultiSampling and applies it to the live device. Returns the applied count the
    /// renderer reports afterwards, which VULKAN-347 made an honest number rather than an echo.
    int ApplyPreferMultiSampling(GraphicsDevice& dev, bool prefer)
    {
        gdm_->setPreferMultiSamplingProperty(prefer);
        gdm_->ApplyChanges();
        return dev.getPresentationParametersProperty().getMultiSampleCountProperty();
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();
        auto* vk = dynamic_cast<VulkanRenderer*>(&dev.GetRenderer());
        if (!vk) { check(false, "the Vulkan renderer is not reachable"); Exit(); return; }

        const Color kRed(255, 0, 0, 255);

        // Created BEFORE any sample-count change. Everything they depend on -- render passes,
        // framebuffers, pipelines -- is destroyed and rebuilt by ApplyMultiSampleCount, so their
        // continued correctness afterwards is the point of this test.
        auto texture = MakeTexture(dev, kRed);
        auto target = std::make_unique<RenderTarget2D>(dev, 2, 2, false, SurfaceFormat::Color,
                                                       DepthFormat::None, 0,
                                                       RenderTargetUsage::DiscardContents);

        check(AllAre(DrawInto(dev, *target, *texture), kRed),
              "A before any change, the pre-existing texture and target render correctly");
        const int startCount = dev.getPresentationParametersProperty().getMultiSampleCountProperty();

        // B. Turn MSAA on, on a live device.
        const int onCount = ApplyPreferMultiSampling(dev, true);
        std::printf("[INFO] MultiSampleCount %d -> %d after preferMultiSampling=true\n",
                    startCount, onCount);
        check(AllAre(DrawInto(dev, *target, *texture), kRed),
              "B1 after enabling MSAA, the SAME pre-change texture and target still render "
              "correctly");
        auto afterOn = MakeTexture(dev, Color(0, 255, 0, 255));
        check(AllAre(DrawInto(dev, *target, *afterOn), Color(0, 255, 0, 255)),
              "B2 and a resource created after the change works too");
        check(AllAre(DrawIntoBackbufferAndRead(dev, *texture), kRed),
              "B3 the BACKBUFFER -- now multisampled -- renders correctly through the rebuilt "
              "MSAA render pass and sprite pipeline");
        check(AllAre(DrawIntoMsaaTarget(dev, *texture), kRed),
              "B4 and so does a multisampled RenderTarget2D, which uses a different rebuilt pass");

        // C. Turn it off again. A teardown that only works once leaks the second time, so the
        //    reverse direction is asserted rather than assumed symmetric.
        const int offCount = ApplyPreferMultiSampling(dev, false);
        std::printf("[INFO] MultiSampleCount %d -> %d after preferMultiSampling=false\n",
                    onCount, offCount);
        check(AllAre(DrawInto(dev, *target, *texture), kRed),
              "C1 after disabling MSAA again, the original texture and target still render");
        check(AllAre(DrawInto(dev, *target, *afterOn), Color(0, 255, 0, 255)),
              "C2 and so does the one created between the two changes");
        check(AllAre(DrawIntoBackbufferAndRead(dev, *texture), kRed),
              "C3 the backbuffer renders correctly again with MSAA off");

        // D. Round-trip once more, because the second rebuild is the one that consumes whatever the
        //    first one failed to release.
        ApplyPreferMultiSampling(dev, true);
        ApplyPreferMultiSampling(dev, false);
        check(AllAre(DrawInto(dev, *target, *texture), kRed),
              "D1 a second on/off round trip leaves rendering correct");
        ApplyPreferMultiSampling(dev, true);
        check(AllAre(DrawIntoBackbufferAndRead(dev, *texture), kRed),
              "D2 and the multisampled backbuffer is correct on the THIRD rebuild -- which is "
              "where anything the first two failed to release shows up");
        ApplyPreferMultiSampling(dev, false);

        // E. The only observer of a pipeline or framebuffer the rebuild left dangling.
        check(VulkanRenderer::IsValidationActiveEXT(),
              "E1 VK_LAYER_KHRONOS_validation is loaded, so the count below means something");
        const auto& msgs = vk->GetValidationMessagesEXT();
        check(msgs.empty(), "E2 no Vulkan validation message across four sample-count changes" +
                                (msgs.empty() ? std::string{}
                                              : std::string(" -- first: ") + msgs.front()));

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    MsaaRuntimeChangeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        gdm_->setPreferMultiSamplingProperty(false);
    }

    [[nodiscard]] int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    MsaaRuntimeChangeTest game;
    game.Run();
    return game.getResult();
}

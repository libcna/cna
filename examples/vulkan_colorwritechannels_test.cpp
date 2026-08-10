// SPDX-License-Identifier: MS-PL
// REMED-GFX-077: the Vulkan renderer must honor BlendState.ColorWriteChannels (per-render-target
// colour write mask) and BlendState.MultiSampleMask, now that both are plumbed through
// IGraphicsRenderer::ApplyBlendState. Previously the colour attachment hardcoded colorWriteMask=RGBA
// and the multisample state hardcoded pSampleMask=all-ones (both silent no-ops).
//
// Vulkan mapping (this task): ColorWriteChannels slot i -> VkPipelineColorBlendAttachmentState
// .colorWriteMask (XNA bits R=1,G=2,B=4,A=8 are identical to VK_COLOR_COMPONENT_*), folded into the
// pipeline cache key (PackColorWriteBits). MultiSampleMask -> VkPipelineMultisampleStateCreateInfo
// .pSampleMask (set only for a non-default mask), also in the key. Both are static pipeline state.
//
// Strategy: render into an off-screen RenderTarget2D and read it back with GetData() (Vulkan RT
// GetData is synchronous after REMED-GFX-074). Opaque sprite (blend off) so the write mask is
// isolated; MSAA RT for the sample-mask discriminator (mask==0 must resolve to the clear colour).
//
// Exit code 0 = all checks PASS, 1 = any FAIL. Runs under Xvfb + llvmpipe/Lavapipe.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace { constexpr int kRtW = 64; constexpr int kRtH = 64; }

class VulkanColorWriteChannelsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<RenderTarget2D> rt_;
    std::unique_ptr<RenderTarget2D> msaaRt_;
    Texture2D whiteTex_;
    bool done_ = false;
    int passCount_ = 0, totalCount_ = 0, result_ = 1;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++totalCount_; if (ok) ++passCount_;
    }

    static BlendState MakeBlend(ColorWriteChannels cw, unsigned int sampleMask = 0xFFFFFFFFu)
    {
        BlendState bs; // default = Opaque (One/Zero) => blend off, mask isolated
        bs.setColorWriteChannelsProperty(cw);
        bs.setMultiSampleMaskProperty(static_cast<int>(sampleMask));
        return bs;
    }

    void DrawSprite(GraphicsDevice& dev, const Color& tint, const BlendState& blend)
    {
        SpriteBatch sb(dev);
        SamplerState point = SamplerState::PointClamp;
        sb.Begin(SpriteSortMode::Deferred, blend, &point, nullptr, nullptr);
        sb.Draw(whiteTex_, Rectangle(0, 0, kRtW, kRtH), Rectangle(0, 0, 1, 1), tint);
        sb.End();
    }

    Color RenderCenter(GraphicsDevice& dev, RenderTarget2D* target, const Color& bg,
                       const Color& tint, const BlendState& blend)
    {
        dev.SetRenderTarget(target);
        dev.Clear(bg);
        DrawSprite(dev, tint, blend);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        std::vector<Color> pix(static_cast<std::size_t>(kRtW) * kRtH, Color(0, 0, 0, 0));
        target->GetData(0, nullptr, pix.data(), 0, static_cast<int>(pix.size()));
        return pix[static_cast<std::size_t>(kRtH / 2) * kRtW + kRtW / 2];
    }

    static std::string Str(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) + "," +
               std::to_string(c.getBProperty()) + "," + std::to_string(c.getAProperty()) + ")";
    }
    static bool Eq(const Color& c, int r, int g, int b, int a)
    {
        return c.getRProperty() == r && c.getGProperty() == g && c.getBProperty() == b && c.getAProperty() == a;
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        rt_ = std::make_unique<RenderTarget2D>(dev, kRtW, kRtH, false, SurfaceFormat::Color,
                                               DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        msaaRt_ = std::make_unique<RenderTarget2D>(dev, kRtW, kRtH, false, SurfaceFormat::Color,
                                                   DepthFormat::None, 4, RenderTargetUsage::DiscardContents);
        whiteTex_ = Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();
        const Color D(10, 20, 30, 40), S(200, 100, 50, 220);

        // ===== RT0 colour write mask (Opaque sprite, keyed 2D pipeline) =====
        struct Case { ColorWriteChannels cw; int r, g, b, a; const char* name; };
        const Case cases[] = {
            { ColorWriteChannels::None,                             10,  20, 30,  40, "None" },
            { ColorWriteChannels::Red,                             200,  20, 30,  40, "Red" },
            { ColorWriteChannels::Green,                            10, 100, 30,  40, "Green" },
            { ColorWriteChannels::Blue,                             10,  20, 50,  40, "Blue" },
            { ColorWriteChannels::Alpha,                            10,  20, 30, 220, "Alpha" },
            { ColorWriteChannels::Red | ColorWriteChannels::Blue,  200,  20, 50,  40, "Red|Blue" },
            { ColorWriteChannels::All,                             200, 100, 50, 220, "All" },
        };
        for (const auto& c : cases)
        {
            const Color px = RenderCenter(dev, rt_.get(), D, S, MakeBlend(c.cw));
            check(Eq(px, c.r, c.g, c.b, c.a),
                  std::string("ColorWriteChannels.") + c.name + " -> (" + std::to_string(c.r) + "," +
                  std::to_string(c.g) + "," + std::to_string(c.b) + "," + std::to_string(c.a) + "), got " + Str(px));
        }

        // ===== A(Red)->B(Green)->A(Red) in ONE frame/target: pipeline key must not go stale =====
        {
            dev.SetRenderTarget(rt_.get());
            dev.Clear(D);
            DrawSprite(dev, S, MakeBlend(ColorWriteChannels::Red));    // -> R only
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            std::vector<Color> p1(static_cast<std::size_t>(kRtW) * kRtH, Color(0,0,0,0));
            rt_->GetData(0, nullptr, p1.data(), 0, static_cast<int>(p1.size()));
            const bool aOk = Eq(p1[(kRtH/2)*kRtW + kRtW/2], 200, 20, 30, 40);

            const Color b = RenderCenter(dev, rt_.get(), D, S, MakeBlend(ColorWriteChannels::Green));
            const Color a2 = RenderCenter(dev, rt_.get(), D, S, MakeBlend(ColorWriteChannels::Red));
            check(aOk && Eq(b, 10, 100, 30, 40) && Eq(a2, 200, 20, 30, 40),
                  "A(Red)->B(Green)->A(Red): each draw selects its own keyed pipeline (no stale mask)");
        }

        // ===== MultiSampleMask on a 4x MSAA RenderTarget (pSampleMask) =====
        // mask==0 -> no coverage samples written -> resolved output == clear colour.
        {
            const Color px0 = RenderCenter(dev, msaaRt_.get(), D, S,
                                           MakeBlend(ColorWriteChannels::All, 0u));
            check(Eq(px0, 10, 20, 30, 40),
                  "MSAA MultiSampleMask=0 -> no samples written, resolves to clear D: " + Str(px0));
            // default mask -> full-coverage opaque sprite resolves to S (unregressed).
            const Color pxAll = RenderCenter(dev, msaaRt_.get(), D, S,
                                             MakeBlend(ColorWriteChannels::All, 0xFFFFFFFFu));
            check(Eq(pxAll, 200, 100, 50, 220),
                  "MSAA MultiSampleMask=all -> normal full-coverage resolve to S: " + Str(pxAll));
            // Compose sample mask + colour write mask: mask=all-samples + ColorWriteChannels=Red|Blue.
            const Color pxc = RenderCenter(dev, msaaRt_.get(), D, S,
                                           MakeBlend(ColorWriteChannels::Red | ColorWriteChannels::Blue, 0xFFFFFFFFu));
            check(Eq(pxc, 200, 20, 50, 40),
                  "MSAA compose: sample-mask(all) + ColorWriteChannels(Red|Blue) -> (200,20,50,40): " + Str(pxc));
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    VulkanColorWriteChannelsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kRtW);
        gdm_->setPreferredBackBufferHeightProperty(kRtH);
    }
    int getResult() const { return result_; }
};

int main()
{
    VulkanColorWriteChannelsTest game;
    game.Run();
    return game.getResult();
}

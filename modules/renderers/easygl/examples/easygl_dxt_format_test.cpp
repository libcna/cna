// SPDX-License-Identifier: MS-PL
// REMED-GFX-244: the three block-compressed SurfaceFormats GraphicsProfile.Reach permits --
// Dxt1, Dxt3 and Dxt5 -- uploaded as raw 4x4 blocks and SAMPLED in a real draw.
//
// Two blocks side by side rather than one. A single block would still look right if the renderer
// mis-sized a block (8 bytes for DXT1, 16 for the other two) or walked the block stream in the
// wrong order; with a red block beside a blue one, either mistake shows as a wrong half.
//
// The renderer takes one of two paths and this case must not be able to tell them apart. Where the
// driver reports S3TC the blocks are stored compressed; where it does not they are decoded to
// Color, because Reach promises the game that these formats work and a missing extension is not the
// game's problem. Run once as configured and once with the probe forced false -- both must be green,
// which is the only way to know the fallback is real rather than merely written.
//
// The colours are chosen to survive RGB565 exactly: 31/31 expands back to precisely 255, so the
// comparison below is exact and no tolerance can hide a channel swap.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
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

namespace
{
    constexpr std::uint16_t kRed565  = 0xF800;
    constexpr std::uint16_t kBlue565 = 0x001F;

    /// One DXT1 colour block whose sixteen texels all take colour0. colour1 is left at zero so
    /// colour0 > colour1 and the block is in the opaque four-colour mode rather than the
    /// one-bit-alpha mode, which keeps alpha out of this test's way.
    void AppendColourBlock(std::vector<std::uint8_t>& out, std::uint16_t colour)
    {
        out.push_back(static_cast<std::uint8_t>(colour & 0xFF));
        out.push_back(static_cast<std::uint8_t>(colour >> 8));
        out.push_back(0x00);
        out.push_back(0x00);
        for (int i = 0; i < 4; ++i) out.push_back(0x00);   // every index selects colour0
    }

    std::vector<std::uint8_t> MakeBlocks(SurfaceFormat format)
    {
        std::vector<std::uint8_t> out;
        for (const std::uint16_t colour : {kRed565, kBlue565})
        {
            if (format == SurfaceFormat::Dxt3)
            {
                for (int i = 0; i < 8; ++i) out.push_back(0xFF);       // 4-bit alpha, all opaque
            }
            else if (format == SurfaceFormat::Dxt5)
            {
                out.push_back(0xFF);                                    // alpha0
                out.push_back(0xFF);                                    // alpha1
                for (int i = 0; i < 6; ++i) out.push_back(0x00);        // every index selects alpha0
            }
            AppendColourBlock(out, colour);
        }
        return out;
    }
}

class DxtFormatTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        ok ? ++pass_ : ++fail_;
    }

    void RunLeg(GraphicsDevice& dev, const char* name, SurfaceFormat format)
    {
        // plan_vulkan.md VULKAN-172. This source is registered by more than one renderer now, and
        // block-compressed storage is not universal: CNA's Vulkan renderer claims BC1/BC2/BC3 only
        // on a device whose VkPhysicalDeviceFeatures.textureCompressionBC is true (and whose
        // VkFormatProperties back the individual format), and refuses them by name where it is
        // not. EasyGL claims all three on every profile, because its fallback decodes.
        //
        // The gate is the RENDERER'S OWN CLAIM, never a renderer name and never a silent skip: a
        // claimed format must pass the pixel check below, an unclaimed one must refuse. So this
        // cannot hide a regression in either direction.
        using CNA::Internal::Renderers::RendererFormatVerdict;
        const bool claimed =
            dev.GetRenderer().ClassifySurfaceFormatEXT(static_cast<int>(format)) ==
            RendererFormatVerdict::Supported;

        std::unique_ptr<Texture2D> texture;
        try
        {
            texture = std::make_unique<Texture2D>(dev, 8, 4, false, format);
        }
        catch (const std::exception& e)
        {
            check(!claimed, std::string(name) +
                                ": a format the renderer does not claim is refused by name, and "
                                "one it claims must construct [" + e.what() + "]");
            return;
        }
        if (!claimed)
        {
            check(false, std::string(name) +
                             ": constructed a block-compressed format the renderer does not claim");
            return;
        }

        const std::vector<std::uint8_t> blocks = MakeBlocks(format);
        try
        {
            texture->SetData(blocks.data(), static_cast<int>(blocks.size()));
        }
        catch (const std::exception& e)
        {
            check(false, std::string(name) + ": SetData threw: " + e.what());
            return;
        }

        RenderTarget2D rt(dev, 8, 4, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&rt);
        dev.Clear(Color(13, 17, 19, 255));
        {
            SamplerState point = SamplerState::PointClamp;
            SpriteBatch batch(dev);
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            batch.Draw(*texture, Rectangle(0, 0, 8, 4), Rectangle(0, 0, 8, 4),
                       Color(255, 255, 255, 255));
            batch.End();
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        std::vector<Color> pixels(32, Color(0, 0, 0, 0));
        rt.GetData(pixels.data(), 0, 32);

        int wrong = 0;
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 8; ++x)
            {
                const bool leftBlock = x < 4;
                const Color& got = pixels[static_cast<std::size_t>(y) * 8u + static_cast<std::size_t>(x)];
                const int expectedR = leftBlock ? 255 : 0;
                const int expectedB = leftBlock ? 0 : 255;
                if (got.getRProperty() != expectedR || got.getGProperty() != 0 ||
                    got.getBProperty() != expectedB)
                {
                    if (wrong < 3)
                        std::printf("        %s (%d,%d): expected (%d,0,%d) got (%d,%d,%d)\n",
                                    name, x, y, expectedR, expectedB, got.getRProperty(),
                                    got.getGProperty(), got.getBProperty());
                    ++wrong;
                }
            }
        check(wrong == 0, std::string(name) +
                              ": the red block and the blue block each cover their own half "
                              "(mismatches=" + std::to_string(wrong) + ")");
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();
        RunLeg(dev, "A Dxt1", SurfaceFormat::Dxt1);
        RunLeg(dev, "B Dxt3", SurfaceFormat::Dxt3);
        RunLeg(dev, "C Dxt5", SurfaceFormat::Dxt5);

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    DxtFormatTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    DxtFormatTest game;
    game.Run();
    return game.getResult();
}

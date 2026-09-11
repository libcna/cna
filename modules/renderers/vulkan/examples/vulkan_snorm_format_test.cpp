// SPDX-License-Identifier: MS-PL
//
// plan_vulkan.md VULKAN-174 -- the two signed-normalized byte formats, NormalizedByte2 and
// NormalizedByte4, uploaded through their typed SetData overloads and then SAMPLED in a real draw.
//
// It has to be a draw, and that is measured rather than stylistic: VULKAN-170 established that
// Texture2D::GetData is served from the shared CPU shadow, so a construct-and-read test round-trips
// a completely wrong upload. Only the GPU's own sampling can tell VK_FORMAT_R8G8B8A8_SNORM from
// VK_FORMAT_R8G8B8A8_UNORM, and the two differ by exactly the thing this row is about.
//
// The values are chosen so the comparison can be EXACT rather than toleranced. A SNORM byte encodes
// [-1, 1] over [-127, 127], so 0.0 and 1.0 land on 0 and 127 and sample back to exactly 0.0 and
// 1.0; anything in between would need a tolerance, and a tolerance is where a channel swap hides.
//
// NormalizedByte2 carries X and Y only, so it samples as (r, g, 0, 1) -- the missing channels are
// the Vulkan defaults, and asserting them is part of the contract rather than an accident: a
// renderer that quietly stored it as a four-channel format would return the caller's third byte in
// blue instead of zero.
//
// A NEGATIVE leg is included, and it is the one that distinguishes SNORM from UNORM storage. -1.0
// encodes as -127 and samples to -1.0, which the sprite shader writes and the Color render target
// clamps to 0. Under UNORM storage the same byte (0x81) reads as 129/255 = 0.506 and would come back
// as a mid grey. So "black" here is a positive result, not an absence of one.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte4.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::RendererFormatVerdict;

namespace
{
    /// x, y, z, w in [-1, 1], and the RGB the render target must show after clamping.
    struct Case { float x, y, z, w; int r, g, b; };
}

class SnormFormatTest final : public Game
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

    std::vector<Color> DrawAndRead(GraphicsDevice& dev, Texture2D& texture)
    {
        RenderTarget2D rt(dev, 2, 2, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
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

    template <typename Packed, typename Make>
    void RunLeg(GraphicsDevice& dev, const char* name, SurfaceFormat format,
                const std::array<Case, 4>& cases, Make make)
    {
        // Gated on the renderer's OWN verdict, the same way VULKAN-173's fixture is: a claimed
        // format must draw correctly, an unclaimed one must refuse by name. Neither is skipped.
        const bool claimed =
            dev.GetRenderer().ClassifySurfaceFormatEXT(static_cast<int>(format)) ==
            RendererFormatVerdict::Supported;

        std::unique_ptr<Texture2D> texture;
        try
        {
            texture = std::make_unique<Texture2D>(dev, 2, 2, false, format);
        }
        catch (const std::exception& e)
        {
            check(!claimed, std::string(name) + ": an unclaimed format refuses by name, a claimed "
                                               "one constructs [" + e.what() + "]");
            return;
        }
        if (!claimed)
        {
            check(false, std::string(name) + ": constructed a format the renderer does not claim");
            return;
        }

        std::array<Packed, 4> data{};
        for (std::size_t i = 0; i < data.size(); ++i) data[i] = make(cases[i]);
        texture->SetData(data.data(), static_cast<int>(data.size()));

        const std::vector<Color> pixels = DrawAndRead(dev, *texture);
        int wrong = 0;
        for (std::size_t i = 0; i < pixels.size(); ++i)
        {
            const Color& got = pixels[i];
            if (got.getRProperty() != cases[i].r || got.getGProperty() != cases[i].g ||
                got.getBProperty() != cases[i].b)
            {
                ++wrong;
                std::printf("        %s texel %zu: expected (%d,%d,%d) got (%d,%d,%d)\n",
                            name, i, cases[i].r, cases[i].g, cases[i].b,
                            got.getRProperty(), got.getGProperty(), got.getBProperty());
            }
        }
        check(wrong == 0, std::string(name) + ": every one of the 4 texels samples back to its own "
                                              "value (mismatches=" + std::to_string(wrong) + ")");
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();

        // NormalizedByte4: X->R, Y->G, Z->B, W->A. Texel 3 is the negative leg.
        static const std::array<Case, 4> kFour{{
            {1.0f, 0.0f, 0.0f, 1.0f, 255,   0,   0},
            {0.0f, 1.0f, 0.0f, 1.0f,   0, 255,   0},
            {0.0f, 0.0f, 1.0f, 1.0f,   0,   0, 255},
            {-1.0f, -1.0f, -1.0f, 1.0f, 0,   0,   0},   // SNORM: -1 clamps to black; UNORM would grey
        }};
        RunLeg<PackedVector::NormalizedByte4>(dev, "A NormalizedByte4",
            SurfaceFormat::NormalizedByte4, kFour,
            [](const Case& c) { return PackedVector::NormalizedByte4(c.x, c.y, c.z, c.w); });

        // NormalizedByte2: X->R, Y->G, and blue must be the format's own zero, not the caller's
        // third byte -- there is no third byte.
        static const std::array<Case, 4> kTwo{{
            {1.0f, 0.0f, 0.0f, 0.0f, 255,   0, 0},
            {0.0f, 1.0f, 0.0f, 0.0f,   0, 255, 0},
            {1.0f, 1.0f, 0.0f, 0.0f, 255, 255, 0},
            {-1.0f, 1.0f, 0.0f, 0.0f,  0, 255, 0},      // the negative leg again
        }};
        RunLeg<PackedVector::NormalizedByte2>(dev, "B NormalizedByte2",
            SurfaceFormat::NormalizedByte2, kTwo,
            [](const Case& c) { return PackedVector::NormalizedByte2(c.x, c.y); });

        // C. The Color-shaped transfer must be refused for both, whatever their byte width says.
        // NormalizedByte4 is FOUR bytes wide, so the framework's own "multiple of four" rule would
        // admit it; this is the renderer overriding that rule because the bytes are signed.
        for (const auto& [format, label] :
             std::array<std::pair<SurfaceFormat, const char*>, 2>{{
                 {SurfaceFormat::NormalizedByte4, "C1 NormalizedByte4"},
                 {SurfaceFormat::NormalizedByte2, "C2 NormalizedByte2"}}})
        {
            check(dev.GetRenderer().ClassifyColorTransferFormatEXT(static_cast<int>(format)) ==
                      RendererFormatVerdict::Unsupported,
                  std::string(label) + ": a Color-shaped transfer is refused, not deferred to the "
                                       "framework's byte-width rule");
        }

        // D. The Khronos layer's verdict on everything above.
        {
            using CNA::Internal::Renderers::Vulkan::VulkanRenderer;
            check(VulkanRenderer::IsValidationActiveEXT(),
                  "D1 VK_LAYER_KHRONOS_validation is loaded, so the count below means something");
            auto* vk = dynamic_cast<VulkanRenderer*>(&dev.GetRenderer());
            const auto& msgs = vk->GetValidationMessagesEXT();
            check(vk != nullptr && msgs.empty(),
                  "D2 no Vulkan validation message" +
                      (msgs.empty() ? std::string{} : std::string(" -- first: ") + msgs.front()));
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    SnormFormatTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    SnormFormatTest game;
    game.Run();
    return game.getResult();
}

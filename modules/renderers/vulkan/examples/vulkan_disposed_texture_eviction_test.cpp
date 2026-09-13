// SPDX-License-Identifier: MS-PL
//
// plan_vulkan.md VULKAN-177 -- a texture destroyed while a cached descriptor set still names its
// VkImageView, which is the one disposal case that is Vulkan-specific rather than shared.
//
// The two renderer-agnostic sources this row also registers (Vulkan_DisposedResource,
// Vulkan_BoundResourceDispose) prove the PUBLIC contract: a disposed resource throws, a target
// destroyed while bound unbinds cleanly. Neither of them reaches
// VulkanRenderer::EvictSampledViewFromCaches, because neither samples a texture with a non-default
// SamplerState first -- and it is exactly that (VkImageView, VkSampler) pair that gets cached.
//
// Why the eviction matters, and why "the picture looked right" is not evidence of it: Vulkan reuses
// handle VALUES. A VkImageView freed by one texture can come back as the next texture's view, so a
// stale cache entry keyed on that value would silently serve the DEAD texture's descriptor set to
// the live one -- REMED-GFX-076's exact finding. A renderer that cached nothing at all would also
// draw the second texture correctly, which is why this test asserts the cache SIZE on both sides of
// the disposal rather than only the pixels.
//
// Leg B is therefore the discriminator and leg A is its precondition: without A's assertion that an
// entry was created, B's "the entry is gone" would be true of a renderer that never made one.

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
#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

class DisposedTextureEvictionTest final : public Game
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

    /// A 2x2 texture of one solid colour.
    static std::unique_ptr<Texture2D> MakeTexture(GraphicsDevice& dev, const Color& c)
    {
        auto t = std::make_unique<Texture2D>(dev, 2, 2, false, SurfaceFormat::Color);
        std::array<Color, 4> texels{c, c, c, c};
        t->SetData(texels.data(), static_cast<int>(texels.size()));
        return t;
    }

    /// Draws @p texture through a NON-DEFAULT sampler, which is what creates the cache entry, and
    /// returns the 2x2 readback.
    std::vector<Color> DrawWithPointWrap(GraphicsDevice& dev, Texture2D& texture)
    {
        RenderTarget2D rt(dev, 2, 2, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&rt);
        dev.Clear(Color(13, 17, 19, 255));
        {
            SamplerState wrap = SamplerState::PointWrap;
            SpriteBatch batch(dev);
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &wrap, nullptr, nullptr);
            batch.Draw(texture, Rectangle(0, 0, 2, 2), Rectangle(0, 0, 2, 2),
                       Color(255, 255, 255, 255));
            batch.End();
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        std::vector<Color> pixels(4, Color(0, 0, 0, 0));
        rt.GetData(pixels.data(), 0, 4);
        return pixels;
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

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();
        auto* vk = dynamic_cast<VulkanRenderer*>(&dev.GetRenderer());
        if (!vk) { check(false, "the Vulkan renderer is not reachable"); Exit(); return; }

        const std::size_t before = vk->GetSampledDescriptorSetCacheSizeEXT();

        const Color kRed(255, 0, 0, 255);
        const Color kBlue(0, 0, 255, 255);

        // A. Sample a texture through a non-default sampler, so a (view, sampler) entry is cached.
        //    Without this assertion leg B proves nothing.
        auto first = MakeTexture(dev, kRed);
        const std::vector<Color> firstPixels = DrawWithPointWrap(dev, *first);
        const std::size_t cached = vk->GetSampledDescriptorSetCacheSizeEXT();
        check(AllAre(firstPixels, kRed), "A1 the first texture draws its own colour");
        check(cached > before,
              "A2 sampling through a non-default sampler created a descriptor-set cache entry (" +
                  std::to_string(before) + " -> " + std::to_string(cached) + ")");

        // B. Destroy it. EvictSampledViewFromCaches must drop the entry that names its view.
        first.reset();
        const std::size_t afterDispose = vk->GetSampledDescriptorSetCacheSizeEXT();
        check(afterDispose < cached,
              "B the disposal evicted the entry naming the dead view (" + std::to_string(cached) +
                  " -> " + std::to_string(afterDispose) + ")");

        // C. A new texture, very likely handed the freed VkImageView handle VALUE, must draw as
        //    ITSELF. A surviving stale entry would serve the dead texture's descriptor set here and
        //    this would come back red.
        auto second = MakeTexture(dev, kBlue);
        const std::vector<Color> secondPixels = DrawWithPointWrap(dev, *second);
        check(AllAre(secondPixels, kBlue),
              "C the replacement texture draws ITS colour, not the disposed one's [got (" +
                  std::to_string(secondPixels[0].getRProperty()) + "," +
                  std::to_string(secondPixels[0].getGProperty()) + "," +
                  std::to_string(secondPixels[0].getBProperty()) + ")]");

        // D. The Khronos layer's verdict: a descriptor set left pointing at a freed view is exactly
        //    what it reports, so a clean run is part of the claim.
        check(VulkanRenderer::IsValidationActiveEXT(),
              "D1 VK_LAYER_KHRONOS_validation is loaded, so the count below means something");
        const auto& msgs = vk->GetValidationMessagesEXT();
        check(msgs.empty(), "D2 no Vulkan validation message" +
                                (msgs.empty() ? std::string{}
                                              : std::string(" -- first: ") + msgs.front()));

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    DisposedTextureEvictionTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    DisposedTextureEvictionTest game;
    game.Run();
    return game.getResult();
}

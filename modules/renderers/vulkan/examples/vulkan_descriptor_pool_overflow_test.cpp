// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-390 (finding F-06): descriptor-pool exhaustion must not draw the
// wrong picture.
//
// VulkanRenderer::GetOrCreateTexSamplerDescSet used to return defaultWhiteDescSet_ when
// vkAllocateDescriptorSets failed. The pool holds MaxDescriptorSets = 512 combined-image-sampler
// sets and the cache is keyed by (VkImageView, VkSampler) with entries evicted only when a view
// dies, so a game holding more than 512 live texture x sampler-state combinations drew WHITE
// sprites -- with no exception, no log and no validation message. A wrong picture is the one
// failure mode a renderer must never choose for itself.
//
// The plan required constructing the exhaustion rather than assuming it, and constructing it took
// more than volume. Measured here first, because it decides the whole shape of this test: with the
// production capacity of 512, the allocation never fails on this machine's drivers.
// vkAllocateDescriptorSets keeps succeeding past the pool's maxSets -- which the specification
// permits, since running out is a runtime error an implementation MAY report and not a usage
// violation the layer will flag -- and 4000 simultaneously live pairs still did not reach it.
// Shrinking the pool does not help for the same reason: it was tried, at 8 sets per pool, and 640
// live pairs still allocated cleanly from the first pool.
//
// So the exhaustion arm is not reachable by volume at all, and a test built on volume alone would
// pass while executing none of the code it claims to cover. The failure is therefore INJECTED,
// which is the other option the plan allows.
//
// Four separate assertions:
//
//   A  A single injected failure chains one more pool, and the sprite still draws its own texture.
//      Read from the renderer's own GetTexSamplerDescriptorPoolCountEXT(), so this fails with a
//      number rather than by whether the pixels happened to look right.
//   B  640 distinct live textures in one frame each draw their own texture. This is the leg the
//      old code failed in production shape: every sprite past the 512th was white.
//   C  A failure the fresh pool cannot satisfy either raises a NAMED exception. This is the arm
//      that replaced the silent white substitution, and the only way to see it is to inject past
//      the chained pool's own first allocation.
//   D  The validation layer stayed silent.
//
// Each texture is 1x1 and its colour encodes its index, so a substituted resource cannot be
// mistaken for a correct one: white is not a colour any of them holds.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

namespace
{
    // Past the production MaxDescriptorSets = 512, so leg B is the real-shape volume case even
    // though (see the header) no driver here fails an allocation at that count.
    constexpr int kTextureCount = 640;
    constexpr int kSize         = 64;   // 64 x 64 backbuffer holds 640 one-pixel sprites in 10 rows

    // Index -> colour. (r, g) is unique for every index below 65536; b is a constant marker that
    // is neither the clear colour's nor white's, so a substituted default-white descriptor set
    // reads as obviously wrong rather than as some other sprite's texture.
    Color ColorForIndex(int i)
    {
        return Color(static_cast<std::uint8_t>(i & 0xFF),
                     static_cast<std::uint8_t>((i >> 8) & 0xFF),
                     0x80, 0xFF);
    }
}

class VulkanDescriptorPoolOverflowTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const std::string& label, const std::string& detail)
    {
        std::printf("[%s] %s: %s\n", ok ? "PASS" : "FAIL", label.c_str(), detail.c_str());
        if (ok) ++pass_; else ++fail_;
    }

    VulkanRenderer& Renderer()
    {
        return *dynamic_cast<VulkanRenderer*>(&getGraphicsDeviceProperty().GetRenderer());
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();

        // ---- Leg A: one injected failure must chain a pool and still draw the right texture ----
        {
            const std::size_t poolsBefore = Renderer().GetTexSamplerDescriptorPoolCountEXT();
            Texture2D marker(dev, 1, 1);
            Color markerColor(11, 222, 33, 255);
            marker.SetData(&markerColor, 1);

            VulkanRenderer::SetTexSamplerDescriptorAllocationFailuresForTestEXT(1);
            dev.Clear(Color(0, 0, 0, 255));
            SamplerState pointClamp = SamplerState::PointClamp;
            {
                SpriteBatch sb(dev);
                sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr,
                         nullptr, nullptr, Matrix::getIdentityProperty());
                sb.Draw(marker, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 1, 1),
                        Color(255, 255, 255, 255), 0.0f, Vector2(0.0f, 0.0f),
                        SpriteEffects::None, 0.0f);
                sb.End();
            }
            VulkanRenderer::SetTexSamplerDescriptorAllocationFailuresForTestEXT(0);

            const std::size_t poolsAfter = Renderer().GetTexSamplerDescriptorPoolCountEXT();
            check(poolsAfter == poolsBefore + 1,
                  "A one injected allocation failure chains exactly one more pool",
                  std::to_string(poolsBefore) + " -> " + std::to_string(poolsAfter) + " pool(s)");

            Color got(0, 0, 0, 0);
            const Rectangle probe(1, 1, 1, 1);
            dev.GetBackBufferData(&probe, &got, 0, 1);
            check(got.getRProperty() == markerColor.getRProperty()
                      && got.getGProperty() == markerColor.getGProperty()
                      && got.getBProperty() == markerColor.getBProperty(),
                  "A the sprite drawn across that failure is still its own texture",
                  "(" + std::to_string(got.getRProperty()) + ","
                      + std::to_string(got.getGProperty()) + ","
                      + std::to_string(got.getBProperty()) + "), expected ("
                      + std::to_string(markerColor.getRProperty()) + ","
                      + std::to_string(markerColor.getGProperty()) + ","
                      + std::to_string(markerColor.getBProperty()) + ")");
        }

        const std::size_t poolsBefore = Renderer().GetTexSamplerDescriptorPoolCountEXT();

        // Every texture stays alive for the whole frame, which is what makes the cache entries
        // live simultaneously -- entries are evicted only when a view dies.
        std::vector<std::unique_ptr<Texture2D>> textures;
        textures.reserve(kTextureCount);
        for (int i = 0; i < kTextureCount; ++i) {
            auto tex = std::make_unique<Texture2D>(dev, 1, 1);
            Color px = ColorForIndex(i);
            tex->SetData(&px, 1);
            textures.push_back(std::move(tex));
        }

        dev.Clear(Color(0, 0, 0, 255));
        SamplerState sampler = SamplerState::PointClamp;
        {
            SpriteBatch sb(dev);
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &sampler, nullptr, nullptr,
                     nullptr, Matrix::getIdentityProperty());
            for (int i = 0; i < kTextureCount; ++i) {
                const Rectangle dest(i % kSize, i / kSize, 1, 1);
                sb.Draw(*textures[static_cast<std::size_t>(i)], dest, Rectangle(0, 0, 1, 1),
                        Color(255, 255, 255, 255), 0.0f, Vector2(0.0f, 0.0f),
                        SpriteEffects::None, 0.0f);
            }
            sb.End();
        }

        const std::size_t poolsAfter = Renderer().GetTexSamplerDescriptorPoolCountEXT();
        std::printf("[INFO] %d live texture/sampler pairs needed %zu -> %zu pool(s) with no "
                    "injected failure -- this driver does not enforce maxSets\n",
                    kTextureCount, poolsBefore, poolsAfter);

        std::vector<Color> frame(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
        const Rectangle full(0, 0, kSize, kSize);
        dev.GetBackBufferData(&full, frame.data(), 0, static_cast<int>(frame.size()));

        int wrong = 0;
        int white = 0;
        int firstWrong = -1;
        for (int i = 0; i < kTextureCount; ++i) {
            const Color want = ColorForIndex(i);
            const Color got  = frame[static_cast<std::size_t>(i)];
            const bool match = got.getRProperty() == want.getRProperty()
                            && got.getGProperty() == want.getGProperty()
                            && got.getBProperty() == want.getBProperty();
            if (!match) {
                ++wrong;
                if (firstWrong < 0) firstWrong = i;
                if (got.getRProperty() > 240 && got.getGProperty() > 240 && got.getBProperty() > 240)
                    ++white;
            }
        }
        std::string detail = std::to_string(kTextureCount - wrong) + "/"
                           + std::to_string(kTextureCount) + " sprites correct";
        if (wrong > 0) {
            const Color got = frame[static_cast<std::size_t>(firstWrong)];
            detail += "; first wrong at index " + std::to_string(firstWrong) + " = ("
                    + std::to_string(got.getRProperty()) + ","
                    + std::to_string(got.getGProperty()) + ","
                    + std::to_string(got.getBProperty()) + "), "
                    + std::to_string(white) + " of them white (the old substituted descriptor set)";
        }
        check(wrong == 0, "B every sprite past the pool's capacity drew its own texture", detail);

        // ---- Leg C: a failure the chained pool cannot satisfy either is refused BY NAME ----
        {
            Texture2D doomed(dev, 1, 1);
            Color doomedColor(1, 2, 3, 255);
            doomed.SetData(&doomedColor, 1);
            // More failures than the chaining path has attempts, so the fresh pool's own first
            // allocation fails too and the refusal arm is reached.
            VulkanRenderer::SetTexSamplerDescriptorAllocationFailuresForTestEXT(64);
            bool refused = false;
            std::string how = "no exception -- a resource was substituted or the draw silently "
                              "succeeded";
            try {
                SpriteBatch sb(dev);
                sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &sampler, nullptr, nullptr,
                         nullptr, Matrix::getIdentityProperty());
                sb.Draw(doomed, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1),
                        Color(255, 255, 255, 255), 0.0f, Vector2(0.0f, 0.0f),
                        SpriteEffects::None, 0.0f);
                sb.End();
            } catch (const std::exception& e) {
                refused = true;
                how = e.what();
            }
            VulkanRenderer::SetTexSamplerDescriptorAllocationFailuresForTestEXT(0);
            check(refused && how.find("Refused rather than drawing a substituted texture")
                                 != std::string::npos,
                  "C an unsatisfiable allocation is refused by name, not substituted", how);
        }

        const auto& messages = Renderer().GetValidationMessagesEXT();
        check(messages.empty(), "D no validation messages",
              messages.empty() ? "0 captured"
                               : std::to_string(messages.size()) + " captured, first: "
                                     + messages.front());

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    VulkanDescriptorPoolOverflowTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    VulkanDescriptorPoolOverflowTest g;
    g.Run();
    return g.getResult();
}

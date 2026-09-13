// SPDX-License-Identifier: MS-PL
//
// plan_vulkan.md VULKAN-164 -- the test `VULKAN-094` proposed and could not write: a `Texture3D`
// sampled with `AddressW` **wrap** versus **clamp**, at a W coordinate outside [0,1], gives two
// different pixels.
//
// `VULKAN-094` proved that `SamplerState.AddressW` reaches `VkSamplerCreateInfo::addressModeW`
// intact and then had to stop, because nothing three-dimensional could be sampled on this renderer
// (F-31): `Texture3D` was upload/readback storage. `VULKAN-163` made the attempt refuse loudly and
// `VULKAN-254` gave the volume a real `sampler3D` path at descriptor set 1, binding 8 + unit. Two
// things still stood between that path and this test, and both were the same defect wearing
// different clothes -- sampler state that the shared layer or this renderer dropped on the floor:
//
//   1. `SpriteBatch::Begin` forwarded the batch SamplerState's filter and its U and V axes to the
//      renderer and **dropped W**. XNA assigns the whole state to `GraphicsDevice.SamplerStates[0]`.
//      Invisible to 2D sampling, which never consults W -- and decisive here.
//   2. The set-1 descriptors were written with the renderer's fixed `defaultSampler_`, so even a W
//      that survived (1) could not have reached a bound volume.
//
// The shader samples at `vec3(0.5, 0.5, 1.25)` -- deliberately outside [0,1] on W, which is the
// only place an address mode is observable. The volume is 1x1x2, so the X and Y axes cannot
// contribute to the answer and the two legs differ in exactly one thing:
//
//   Wrap : 1.25 -> 0.25 -> slice 0 -> RED
//   Clamp: 1.25 -> 1.00 -> slice 1 -> BLUE
//
//   A  Wrap reads slice 0.
//   B  Clamp reads slice 1 -- and A != B is this row's acceptance sentence.
//   C  Wrap again, through the SAME ShaderEffect, reads slice 0 again. Without it, B could be any
//      second-batch divergence rather than the W axis; with it, the effect's descriptor set is also
//      proved to follow the sampler back, which is the cache bug this row had to fix to work at all.
//   D  The two SamplerStates differ in nothing but W. Asserted rather than assumed, because a
//      typo in the setup would make A/B pass for the wrong reason.
//   E  No validation message.
//
// Exit code 0 = all PASS, 1 = any FAIL.

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
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"

#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

// ---------------------------------------------------------------------------
// Pre-compiled SPIR-V, from GLSL, through modules/renderers/vulkan/src/shaders/compile_shaders.py.
//   vert: modules/renderers/vulkan/src/shaders/sprite2d.vert.glsl, unchanged
//   frag: layout(set = 1, binding = 8) uniform sampler3D volume;
//         outColor = texture(volume, vec3(0.5, 0.5, 1.25));
// ---------------------------------------------------------------------------
static const uint32_t kVolumeWVertSpv[] = {
    0x07230203, 0x00010000, 0x000d000b, 0x00000032, 0x00000000, 0x00020011,
    0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
    0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x000b000f, 0x00000000,
    0x00000004, 0x6e69616d, 0x00000000, 0x0000000b, 0x00000021, 0x0000002a,
    0x0000002b, 0x0000002d, 0x0000002f, 0x00040047, 0x0000000b, 0x0000001e,
    0x00000000, 0x00030047, 0x0000000d, 0x00000002, 0x00050048, 0x0000000d,
    0x00000000, 0x00000023, 0x00000000, 0x00030047, 0x0000001f, 0x00000002,
    0x00050048, 0x0000001f, 0x00000000, 0x0000000b, 0x00000000, 0x00050048,
    0x0000001f, 0x00000001, 0x0000000b, 0x00000001, 0x00050048, 0x0000001f,
    0x00000002, 0x0000000b, 0x00000003, 0x00050048, 0x0000001f, 0x00000003,
    0x0000000b, 0x00000004, 0x00040047, 0x0000002a, 0x0000001e, 0x00000000,
    0x00040047, 0x0000002b, 0x0000001e, 0x00000001, 0x00040047, 0x0000002d,
    0x0000001e, 0x00000001, 0x00040047, 0x0000002f, 0x0000001e, 0x00000002,
    0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016,
    0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000002,
    0x00040020, 0x0000000a, 0x00000001, 0x00000007, 0x0004003b, 0x0000000a,
    0x0000000b, 0x00000001, 0x0003001e, 0x0000000d, 0x00000007, 0x00040020,
    0x0000000e, 0x00000009, 0x0000000d, 0x0004003b, 0x0000000e, 0x0000000f,
    0x00000009, 0x00040015, 0x00000010, 0x00000020, 0x00000001, 0x0004002b,
    0x00000010, 0x00000011, 0x00000000, 0x00040020, 0x00000012, 0x00000009,
    0x00000007, 0x0004002b, 0x00000006, 0x00000016, 0x40000000, 0x0004002b,
    0x00000006, 0x00000018, 0x3f800000, 0x00040017, 0x0000001b, 0x00000006,
    0x00000004, 0x00040015, 0x0000001c, 0x00000020, 0x00000000, 0x0004002b,
    0x0000001c, 0x0000001d, 0x00000001, 0x0004001c, 0x0000001e, 0x00000006,
    0x0000001d, 0x0006001e, 0x0000001f, 0x0000001b, 0x00000006, 0x0000001e,
    0x0000001e, 0x00040020, 0x00000020, 0x00000003, 0x0000001f, 0x0004003b,
    0x00000020, 0x00000021, 0x00000003, 0x0004002b, 0x00000006, 0x00000023,
    0x00000000, 0x00040020, 0x00000027, 0x00000003, 0x0000001b, 0x00040020,
    0x00000029, 0x00000003, 0x00000007, 0x0004003b, 0x00000029, 0x0000002a,
    0x00000003, 0x0004003b, 0x0000000a, 0x0000002b, 0x00000001, 0x0004003b,
    0x00000027, 0x0000002d, 0x00000003, 0x00040020, 0x0000002e, 0x00000001,
    0x0000001b, 0x0004003b, 0x0000002e, 0x0000002f, 0x00000001, 0x0005002c,
    0x00000007, 0x00000031, 0x00000018, 0x00000018, 0x00050036, 0x00000002,
    0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0004003d,
    0x00000007, 0x0000000c, 0x0000000b, 0x00050041, 0x00000012, 0x00000013,
    0x0000000f, 0x00000011, 0x0004003d, 0x00000007, 0x00000014, 0x00000013,
    0x00050088, 0x00000007, 0x00000015, 0x0000000c, 0x00000014, 0x0005008e,
    0x00000007, 0x00000017, 0x00000015, 0x00000016, 0x00050083, 0x00000007,
    0x0000001a, 0x00000017, 0x00000031, 0x00050051, 0x00000006, 0x00000024,
    0x0000001a, 0x00000000, 0x00050051, 0x00000006, 0x00000025, 0x0000001a,
    0x00000001, 0x00070050, 0x0000001b, 0x00000026, 0x00000024, 0x00000025,
    0x00000023, 0x00000018, 0x00050041, 0x00000027, 0x00000028, 0x00000021,
    0x00000011, 0x0003003e, 0x00000028, 0x00000026, 0x0004003d, 0x00000007,
    0x0000002c, 0x0000002b, 0x0003003e, 0x0000002a, 0x0000002c, 0x0004003d,
    0x0000001b, 0x00000030, 0x0000002f, 0x0003003e, 0x0000002d, 0x00000030,
    0x000100fd, 0x00010038,
};
static const uint32_t kVolumeWFragSpv[] = {
    0x07230203, 0x00010000, 0x000d000b, 0x00000014, 0x00000000, 0x00020011,
    0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
    0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0006000f, 0x00000004,
    0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x00030010, 0x00000004,
    0x00000007, 0x00040047, 0x00000009, 0x0000001e, 0x00000000, 0x00040047,
    0x0000000d, 0x00000021, 0x00000008, 0x00040047, 0x0000000d, 0x00000022,
    0x00000001, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002,
    0x00030016, 0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006,
    0x00000004, 0x00040020, 0x00000008, 0x00000003, 0x00000007, 0x0004003b,
    0x00000008, 0x00000009, 0x00000003, 0x00090019, 0x0000000a, 0x00000006,
    0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000000,
    0x0003001b, 0x0000000b, 0x0000000a, 0x00040020, 0x0000000c, 0x00000000,
    0x0000000b, 0x0004003b, 0x0000000c, 0x0000000d, 0x00000000, 0x00040017,
    0x0000000f, 0x00000006, 0x00000003, 0x0004002b, 0x00000006, 0x00000010,
    0x3f000000, 0x0004002b, 0x00000006, 0x00000011, 0x3fa00000, 0x0006002c,
    0x0000000f, 0x00000012, 0x00000010, 0x00000010, 0x00000011, 0x00050036,
    0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005,
    0x0004003d, 0x0000000b, 0x0000000e, 0x0000000d, 0x00050057, 0x00000007,
    0x00000013, 0x0000000e, 0x00000012, 0x0003003e, 0x00000009, 0x00000013,
    0x000100fd, 0x00010038,
};

namespace
{
constexpr int kN = 8;
const Color kWhite(255, 255, 255, 255);
const Color kRed(255, 0, 0, 255);
const Color kBlue(0, 0, 255, 255);
const Color kGreen(0, 255, 0, 255);
}  // namespace

class VulkanTexture3DAddressWTest final : public Game
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

    VulkanRenderer& Renderer()
    {
        return *dynamic_cast<VulkanRenderer*>(&getGraphicsDeviceProperty().GetRenderer());
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

    /// A SamplerState that differs from its sibling in the W axis and in nothing else.
    static SamplerState WAxis(const TextureAddressMode w)
    {
        SamplerState s;
        s.setFilterProperty(TextureFilter::Point);
        s.setAddressUProperty(TextureAddressMode::Clamp);
        s.setAddressVProperty(TextureAddressMode::Clamp);
        s.setAddressWProperty(w);
        return s;
    }

    /// Draws one sprite through `effect` with `sampler` and returns the centre texel.
    Color DrawWith(GraphicsDevice& dev, ShaderEffect& effect, Texture2D& sprite,
                   SamplerState sampler)
    {
        RenderTarget2D rt(dev, kN, kN, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.SetRenderTarget(&rt);
        dev.Clear(kGreen);
        {
            SpriteBatch sb(dev);
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &sampler, nullptr, nullptr,
                     &effect);
            sb.Draw(sprite, Rectangle(0, 0, kN, kN), Rectangle(0, 0, 2, 2), kWhite);
            sb.End();
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        std::vector<Color> p(static_cast<std::size_t>(kN * kN), Color(0, 0, 0, 0));
        rt.GetData(p.data(), 0, kN * kN);
        return p[kN * kN / 2];
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();
        const std::size_t messagesBefore = Renderer().GetValidationMessagesEXT().size();

        const std::string vert(reinterpret_cast<const char*>(kVolumeWVertSpv),
                               sizeof(kVolumeWVertSpv));
        const std::string frag(reinterpret_cast<const char*>(kVolumeWFragSpv),
                               sizeof(kVolumeWFragSpv));
        // ONE effect for every leg. Two would let each leg build its own descriptor set and hide
        // the case this row had to fix: a set cached for one sampler and reused under another.
        ShaderEffect effect(dev, vert, frag);

        Texture2D sprite(dev, 2, 2, false, SurfaceFormat::Color);
        const std::array<std::uint8_t, 16> white{255, 255, 255, 255, 255, 255, 255, 255,
                                                 255, 255, 255, 255, 255, 255, 255, 255};
        sprite.SetDataRGBA(white.data(), 4);

        // 1x1x2: one voxel per W slice, so X and Y cannot contribute to the answer.
        Texture3D volume(dev, 1, 1, 2, false, SurfaceFormat::Color);
        const std::array<Color, 2> voxels{kRed, kBlue};
        volume.SetData(voxels.data(), static_cast<int>(voxels.size()));
        effect.SetTexture(0, volume);

        const SamplerState wrap  = WAxis(TextureAddressMode::Wrap);
        const SamplerState clamp = WAxis(TextureAddressMode::Clamp);

        // D. The setup itself, asserted. A/B mean nothing if the two states differ in more than W.
        check(wrap.getFilterProperty() == clamp.getFilterProperty() &&
                  wrap.getAddressUProperty() == clamp.getAddressUProperty() &&
                  wrap.getAddressVProperty() == clamp.getAddressVProperty() &&
                  wrap.getAddressWProperty() != clamp.getAddressWProperty(),
              "D the two SamplerStates differ in the W axis and in nothing else");

        const Color gotWrap  = DrawWith(dev, effect, sprite, wrap);
        const Color gotClamp = DrawWith(dev, effect, sprite, clamp);
        const Color gotWrap2 = DrawWith(dev, effect, sprite, wrap);

        check(Is(gotWrap, kRed),
              "A AddressW=Wrap at W=1.25 wraps to 0.25 and reads slice 0: " + Text(gotWrap) +
                  " (want " + Text(kRed) + ")");
        check(Is(gotClamp, kBlue),
              "B AddressW=Clamp at the same W clamps to 1.0 and reads slice 1: " + Text(gotClamp) +
                  " (want " + Text(kBlue) + ")");
        check(!Is(gotWrap, gotClamp),
              "B' wrap and clamp give two different pixels: " + Text(gotWrap) + " vs " +
                  Text(gotClamp));
        check(Is(gotWrap2, kRed),
              "C wrap again through the same effect reads slice 0 again: " + Text(gotWrap2) +
                  " (want " + Text(kRed) +
                  "; the clamp answer here means the effect's descriptor set outlived its sampler)");

        {
            const std::size_t after = Renderer().GetValidationMessagesEXT().size();
            check(!VulkanRenderer::IsValidationActiveEXT() || after == messagesBefore,
                  "E no validation message: " + std::to_string(messagesBefore) + " -> " +
                      std::to_string(after));
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    VulkanTexture3DAddressWTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanTexture3DAddressWTest game;
    game.Run();
    return game.getResult();
}

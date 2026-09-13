// SPDX-License-Identifier: MS-PL
//
// plan_vulkan.md VULKAN-166 -- a `ShaderEffect`'s texture unit `u` is governed by
// `GraphicsDevice.SamplerStates[u]`, not by the sprite batch's slot-0 state.
//
// `VULKAN-164` had to get ONE sampler to a bound volume and gave it the one it could reach: every
// unit in descriptor set 1 took `slotSamplers_[0]`, the batch's own sampler. Two units bound in a
// single batch were therefore impossible to address or filter differently -- an XNA game that sets
// `SamplerStates[1].AddressW` and binds a second volume got the first unit's state on both.
//
// The obstacle was one layer up and is what this row's fix names: `applySamplerStatesToRenderer()`
// ran from the draw entry points only, so in a `SpriteBatch`-only frame nothing ever pushed
// `SamplerStates[1..15]` to any renderer at all. `SpriteBatch` now publishes them, from the two
// places FNA's `PrepRenderState` runs (`Begin()` for `Immediate`, `FlushBatch()` otherwise), and
// from slot 1 upward: slot 0 already has a writer -- the batch's own
// `SetSamplerFilter`/`SetSamplerAddressMode`/`SetSamplerAddressModeWEXT` -- and giving one slot two
// writers with no ordering between them across twelve renderer families is how VULKAN-164's
// defect (2) happened in the first place.
//
// Both volumes are 1x1x2 and are sampled at `W = 1.25`, outside [0,1], which is the only place an
// address mode is observable. X and Y cannot contribute -- there is one voxel on each.
//
//   volume 0:  slice 0 R=255   slice 1 R=0     -> the RED channel says which slice unit 0 read
//   volume 1:  slice 0 B=0     slice 1 B=255   -> the BLUE channel says which slice unit 1 read
//   the shader always writes G=128              -> green says the shader ran at all
//
//   Wrap  at W=1.25 -> 0.25 -> slice 0
//   Clamp at W=1.25 -> 1.00 -> slice 1
//
// One pixel therefore separates all four worlds, which is the property this test is built for:
//
//   unit0 Wrap  + unit1 Clamp -> (255,128,255)   both units resolved on their own state   <- A
//   unit0 Clamp + unit1 Wrap  -> (  0,128,  0)   the same, swapped                        <- B
//   both units on the batch's Wrap  -> (255,128,  0)   the defect this row closes
//   both units on the batch's Clamp -> (  0,128,255)   the same defect, seen from leg B
//
//   A  unit 0 Wrap, unit 1 Clamp: the two units read two different slices in ONE draw.
//   B  the states swapped between the two units gives the opposite pixel -- so A is the samplers
//      being routed per unit, not unit 1 being hard-wired to Clamp.
//   C  A repeated through the SAME effect after B, because a set cached for one unit's sampler and
//      reused under another is exactly the shape VULKAN-164 had to fix for a single unit.
//   D  the two SamplerStates differ in the W axis and in nothing else.
//   E  no validation message.
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
#include <cstddef>
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
//   frag: layout(set = 1, binding = 8) uniform sampler3D volume0;
//         layout(set = 1, binding = 9) uniform sampler3D volume1;
//         vec3 w = vec3(fragUV, 1.25);
//         outColor = vec4(texture(volume0, w).r, 0.5, texture(volume1, w).b, 1.0) * fragColor;
// ---------------------------------------------------------------------------
static constexpr uint32_t kTwoUnitsVertSpv[] = {
    0x07230203u, 0x00010000u, 0x000d000bu, 0x00000032u, 0x00000000u, 0x00020011u, 0x00000001u, 0x0006000bu,
    0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u,
    0x000b000fu, 0x00000000u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x0000000bu, 0x00000021u, 0x0000002au,
    0x0000002bu, 0x0000002du, 0x0000002fu, 0x00040047u, 0x0000000bu, 0x0000001eu, 0x00000000u, 0x00030047u,
    0x0000000du, 0x00000002u, 0x00050048u, 0x0000000du, 0x00000000u, 0x00000023u, 0x00000000u, 0x00030047u,
    0x0000001fu, 0x00000002u, 0x00050048u, 0x0000001fu, 0x00000000u, 0x0000000bu, 0x00000000u, 0x00050048u,
    0x0000001fu, 0x00000001u, 0x0000000bu, 0x00000001u, 0x00050048u, 0x0000001fu, 0x00000002u, 0x0000000bu,
    0x00000003u, 0x00050048u, 0x0000001fu, 0x00000003u, 0x0000000bu, 0x00000004u, 0x00040047u, 0x0000002au,
    0x0000001eu, 0x00000000u, 0x00040047u, 0x0000002bu, 0x0000001eu, 0x00000001u, 0x00040047u, 0x0000002du,
    0x0000001eu, 0x00000001u, 0x00040047u, 0x0000002fu, 0x0000001eu, 0x00000002u, 0x00020013u, 0x00000002u,
    0x00030021u, 0x00000003u, 0x00000002u, 0x00030016u, 0x00000006u, 0x00000020u, 0x00040017u, 0x00000007u,
    0x00000006u, 0x00000002u, 0x00040020u, 0x0000000au, 0x00000001u, 0x00000007u, 0x0004003bu, 0x0000000au,
    0x0000000bu, 0x00000001u, 0x0003001eu, 0x0000000du, 0x00000007u, 0x00040020u, 0x0000000eu, 0x00000009u,
    0x0000000du, 0x0004003bu, 0x0000000eu, 0x0000000fu, 0x00000009u, 0x00040015u, 0x00000010u, 0x00000020u,
    0x00000001u, 0x0004002bu, 0x00000010u, 0x00000011u, 0x00000000u, 0x00040020u, 0x00000012u, 0x00000009u,
    0x00000007u, 0x0004002bu, 0x00000006u, 0x00000016u, 0x40000000u, 0x0004002bu, 0x00000006u, 0x00000018u,
    0x3f800000u, 0x00040017u, 0x0000001bu, 0x00000006u, 0x00000004u, 0x00040015u, 0x0000001cu, 0x00000020u,
    0x00000000u, 0x0004002bu, 0x0000001cu, 0x0000001du, 0x00000001u, 0x0004001cu, 0x0000001eu, 0x00000006u,
    0x0000001du, 0x0006001eu, 0x0000001fu, 0x0000001bu, 0x00000006u, 0x0000001eu, 0x0000001eu, 0x00040020u,
    0x00000020u, 0x00000003u, 0x0000001fu, 0x0004003bu, 0x00000020u, 0x00000021u, 0x00000003u, 0x0004002bu,
    0x00000006u, 0x00000023u, 0x00000000u, 0x00040020u, 0x00000027u, 0x00000003u, 0x0000001bu, 0x00040020u,
    0x00000029u, 0x00000003u, 0x00000007u, 0x0004003bu, 0x00000029u, 0x0000002au, 0x00000003u, 0x0004003bu,
    0x0000000au, 0x0000002bu, 0x00000001u, 0x0004003bu, 0x00000027u, 0x0000002du, 0x00000003u, 0x00040020u,
    0x0000002eu, 0x00000001u, 0x0000001bu, 0x0004003bu, 0x0000002eu, 0x0000002fu, 0x00000001u, 0x0005002cu,
    0x00000007u, 0x00000031u, 0x00000018u, 0x00000018u, 0x00050036u, 0x00000002u, 0x00000004u, 0x00000000u,
    0x00000003u, 0x000200f8u, 0x00000005u, 0x0004003du, 0x00000007u, 0x0000000cu, 0x0000000bu, 0x00050041u,
    0x00000012u, 0x00000013u, 0x0000000fu, 0x00000011u, 0x0004003du, 0x00000007u, 0x00000014u, 0x00000013u,
    0x00050088u, 0x00000007u, 0x00000015u, 0x0000000cu, 0x00000014u, 0x0005008eu, 0x00000007u, 0x00000017u,
    0x00000015u, 0x00000016u, 0x00050083u, 0x00000007u, 0x0000001au, 0x00000017u, 0x00000031u, 0x00050051u,
    0x00000006u, 0x00000024u, 0x0000001au, 0x00000000u, 0x00050051u, 0x00000006u, 0x00000025u, 0x0000001au,
    0x00000001u, 0x00070050u, 0x0000001bu, 0x00000026u, 0x00000024u, 0x00000025u, 0x00000023u, 0x00000018u,
    0x00050041u, 0x00000027u, 0x00000028u, 0x00000021u, 0x00000011u, 0x0003003eu, 0x00000028u, 0x00000026u,
    0x0004003du, 0x00000007u, 0x0000002cu, 0x0000002bu, 0x0003003eu, 0x0000002au, 0x0000002cu, 0x0004003du,
    0x0000001bu, 0x00000030u, 0x0000002fu, 0x0003003eu, 0x0000002du, 0x00000030u, 0x000100fdu, 0x00010038u
};
static constexpr size_t kTwoUnitsVertSpv_size = sizeof(kTwoUnitsVertSpv);

static constexpr uint32_t kTwoUnitsFragSpv[] = {
    0x07230203u, 0x00010000u, 0x000d000bu, 0x0000002cu, 0x00000000u, 0x00020011u, 0x00000001u, 0x0006000bu,
    0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u,
    0x0008000fu, 0x00000004u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x0000000cu, 0x00000014u, 0x00000029u,
    0x00030010u, 0x00000004u, 0x00000007u, 0x00040047u, 0x0000000cu, 0x0000001eu, 0x00000000u, 0x00040047u,
    0x00000014u, 0x0000001eu, 0x00000000u, 0x00040047u, 0x00000018u, 0x00000021u, 0x00000008u, 0x00040047u,
    0x00000018u, 0x00000022u, 0x00000001u, 0x00040047u, 0x00000020u, 0x00000021u, 0x00000009u, 0x00040047u,
    0x00000020u, 0x00000022u, 0x00000001u, 0x00040047u, 0x00000029u, 0x0000001eu, 0x00000001u, 0x00020013u,
    0x00000002u, 0x00030021u, 0x00000003u, 0x00000002u, 0x00030016u, 0x00000006u, 0x00000020u, 0x00040017u,
    0x00000007u, 0x00000006u, 0x00000003u, 0x00040017u, 0x0000000au, 0x00000006u, 0x00000002u, 0x00040020u,
    0x0000000bu, 0x00000001u, 0x0000000au, 0x0004003bu, 0x0000000bu, 0x0000000cu, 0x00000001u, 0x0004002bu,
    0x00000006u, 0x0000000eu, 0x3fa00000u, 0x00040017u, 0x00000012u, 0x00000006u, 0x00000004u, 0x00040020u,
    0x00000013u, 0x00000003u, 0x00000012u, 0x0004003bu, 0x00000013u, 0x00000014u, 0x00000003u, 0x00090019u,
    0x00000015u, 0x00000006u, 0x00000002u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000001u, 0x00000000u,
    0x0003001bu, 0x00000016u, 0x00000015u, 0x00040020u, 0x00000017u, 0x00000000u, 0x00000016u, 0x0004003bu,
    0x00000017u, 0x00000018u, 0x00000000u, 0x0004002bu, 0x00000006u, 0x0000001fu, 0x3f000000u, 0x0004003bu,
    0x00000017u, 0x00000020u, 0x00000000u, 0x0004002bu, 0x00000006u, 0x00000026u, 0x3f800000u, 0x00040020u,
    0x00000028u, 0x00000001u, 0x00000012u, 0x0004003bu, 0x00000028u, 0x00000029u, 0x00000001u, 0x00050036u,
    0x00000002u, 0x00000004u, 0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u, 0x0004003du, 0x0000000au,
    0x0000000du, 0x0000000cu, 0x00050051u, 0x00000006u, 0x0000000fu, 0x0000000du, 0x00000000u, 0x00050051u,
    0x00000006u, 0x00000010u, 0x0000000du, 0x00000001u, 0x00060050u, 0x00000007u, 0x00000011u, 0x0000000fu,
    0x00000010u, 0x0000000eu, 0x0004003du, 0x00000016u, 0x00000019u, 0x00000018u, 0x00050057u, 0x00000012u,
    0x0000001bu, 0x00000019u, 0x00000011u, 0x00050051u, 0x00000006u, 0x0000001eu, 0x0000001bu, 0x00000000u,
    0x0004003du, 0x00000016u, 0x00000021u, 0x00000020u, 0x00050057u, 0x00000012u, 0x00000023u, 0x00000021u,
    0x00000011u, 0x00050051u, 0x00000006u, 0x00000025u, 0x00000023u, 0x00000002u, 0x00070050u, 0x00000012u,
    0x00000027u, 0x0000001eu, 0x0000001fu, 0x00000025u, 0x00000026u, 0x0004003du, 0x00000012u, 0x0000002au,
    0x00000029u, 0x00050085u, 0x00000012u, 0x0000002bu, 0x00000027u, 0x0000002au, 0x0003003eu, 0x00000014u,
    0x0000002bu, 0x000100fdu, 0x00010038u
};
static constexpr size_t kTwoUnitsFragSpv_size = sizeof(kTwoUnitsFragSpv);

namespace {

constexpr int kN = 8;

const Color kGreenClear(0, 200, 0, 255);
const Color kWhite(255, 255, 255, 255);

/// Unit 0's answer, encoded in RED: slice 0 is 255, slice 1 is 0.
const std::array<Color, 2> kVolume0{ Color(255, 0, 0, 255), Color(0, 0, 0, 255) };
/// Unit 1's answer, encoded in BLUE: slice 0 is 0, slice 1 is 255.
const std::array<Color, 2> kVolume1{ Color(0, 0, 0, 255), Color(0, 0, 255, 255) };

} // namespace

class VulkanShaderEffectPerUnitSamplerTest final : public Game
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

    static std::string Text(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) +
               "," + std::to_string(c.getBProperty()) + ")";
    }

    /// Names the world a measured pixel belongs to, so a FAIL says WHICH defect it saw.
    static std::string Diagnose(const Color& c)
    {
        const bool unit0Slice0 = c.getRProperty() > 128;
        const bool unit1Slice1 = c.getBProperty() > 128;
        if (c.getGProperty() < 64) return "the shader did not run (no green)";
        return std::string("unit 0 read slice ") + (unit0Slice0 ? "0 (Wrap)" : "1 (Clamp)") +
               ", unit 1 read slice " + (unit1Slice1 ? "1 (Clamp)" : "0 (Wrap)");
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

    /// One draw through `effect`, with `unit0` as the BATCH sampler and `unit1` published through
    /// GraphicsDevice.SamplerStates[1] -- which is the route this row exists to open.
    Color DrawWith(GraphicsDevice& dev, ShaderEffect& effect, Texture2D& sprite,
                   SamplerState unit0, const SamplerState& unit1)
    {
        dev.getSamplerStatesProperty()[1] = unit1;

        RenderTarget2D rt(dev, kN, kN, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.SetRenderTarget(&rt);
        dev.Clear(kGreenClear);
        {
            SpriteBatch sb(dev);
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &unit0, nullptr, nullptr,
                     &effect);
            sb.Draw(sprite, Rectangle(0, 0, kN, kN), Rectangle(0, 0, 2, 2), kWhite);
            sb.End();
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        std::vector<Color> p(static_cast<std::size_t>(kN * kN), Color(0, 0, 0, 0));
        rt.GetData(p.data(), 0, kN * kN);
        return p[static_cast<std::size_t>(kN * kN / 2)];
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();
        const std::size_t messagesBefore = Renderer().GetValidationMessagesEXT().size();

        const std::string vert(reinterpret_cast<const char*>(kTwoUnitsVertSpv),
                               sizeof(kTwoUnitsVertSpv));
        const std::string frag(reinterpret_cast<const char*>(kTwoUnitsFragSpv),
                               sizeof(kTwoUnitsFragSpv));
        // ONE effect for every leg, for the reason leg C states.
        ShaderEffect effect(dev, vert, frag);

        Texture2D sprite(dev, 2, 2, false, SurfaceFormat::Color);
        const std::array<std::uint8_t, 16> white{255, 255, 255, 255, 255, 255, 255, 255,
                                                 255, 255, 255, 255, 255, 255, 255, 255};
        sprite.SetDataRGBA(white.data(), 4);

        Texture3D volume0(dev, 1, 1, 2, false, SurfaceFormat::Color);
        volume0.SetData(kVolume0.data(), static_cast<int>(kVolume0.size()));
        Texture3D volume1(dev, 1, 1, 2, false, SurfaceFormat::Color);
        volume1.SetData(kVolume1.data(), static_cast<int>(kVolume1.size()));
        effect.SetTexture(0, volume0);
        effect.SetTexture(1, volume1);

        const SamplerState wrap  = WAxis(TextureAddressMode::Wrap);
        const SamplerState clamp = WAxis(TextureAddressMode::Clamp);

        // D. The setup itself, asserted: A and B mean nothing if the states differ in more than W.
        check(wrap.getFilterProperty() == clamp.getFilterProperty() &&
                  wrap.getAddressUProperty() == clamp.getAddressUProperty() &&
                  wrap.getAddressVProperty() == clamp.getAddressVProperty() &&
                  wrap.getAddressWProperty() != clamp.getAddressWProperty(),
              "D the two SamplerStates differ in the W axis and in nothing else");

        const Color a  = DrawWith(dev, effect, sprite, wrap,  clamp);
        const Color b  = DrawWith(dev, effect, sprite, clamp, wrap);
        const Color a2 = DrawWith(dev, effect, sprite, wrap,  clamp);

        const bool aOk = a.getRProperty() > 128 && a.getBProperty() > 128;
        check(aOk, "A unit 0 Wrap and unit 1 Clamp read two different slices in one draw: " +
                       Text(a) + " -- " + Diagnose(a));
        const bool bOk = b.getRProperty() < 128 && b.getBProperty() < 128;
        check(bOk, "B the same two states swapped give the opposite pixel: " + Text(b) + " -- " +
                       Diagnose(b));
        check(a.getRProperty() != b.getRProperty() && a.getBProperty() != b.getBProperty(),
              "B' both channels moved when the states were swapped: " + Text(a) + " vs " + Text(b));
        check(a2.getRProperty() == a.getRProperty() && a2.getBProperty() == a.getBProperty(),
              "C leg A repeated through the same effect gives leg A's pixel again: " + Text(a2) +
                  " (want " + Text(a) + "; anything else means the descriptor set outlived a "
                  "per-unit sampler change)");

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
    VulkanShaderEffectPerUnitSamplerTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanShaderEffectPerUnitSamplerTest game;
    game.Run();
    return game.getResult();
}

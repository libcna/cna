// SPDX-License-Identifier: MS-PL
//
// plan_vulkan.md VULKAN-253 -- a custom effect samples a texture it was EXPLICITLY BOUND, not the
// one SpriteBatch happened to hand it.
//
// Before this row `VulkanEffectRenderer::BindTexture` threw: a `ShaderEffect` could only ever
// sample whatever the draw supplied at set 0, so every effect that needs a second image -- a mask,
// a lookup table, a normal map -- was out of reach. `VULKAN-163` had made that refusal loud, which
// is the right state for a gap but not the end of it.
//
// The bound textures live in **descriptor set 1**; the sprite's own stays at set 0. Set 0 changes
// per sprite and set 1 is fixed for the batch, so sharing one set would mean rebuilding it for
// every draw. The shader below reads ONLY set 1, which is what makes this test discriminating: if
// the binding did not reach the shader, the output is not "slightly wrong", it is the white filler
// the renderer writes into unbound units.
//
//   A  A sprite drawn with a WHITE texture through an effect that samples the bound BLUE one comes
//      out blue. The sprite's own texture is white specifically so that a failure to bind reads as
//      white -- the same colour the filler produces, which is the honest failure signal here.
//   B  Rebinding a different texture between two batches changes the output. One binding proves
//      the plumbing exists; two prove the state is the effect's own and not a constant.
//   C  A unit outside the supported range is refused by name rather than silently dropped.
//   D  No validation message -- the set must be fully written before it is bound, and a shader
//      reading a unit nothing was bound to must still get a real descriptor.
//   E  plan_vulkan.md VULKAN-254: the same for a TextureCube, read through `samplerCube` at
//      binding 4 + unit.
//   F  ...and for a Texture3D, through `sampler3D` at binding 8 + unit. Each sampler kind gets its
//      own binding range because a descriptor's view type has to match the dimensionality the
//      shader declares -- a 2D filler cannot stand in for an unbound `samplerCube`, so sharing one
//      range of four would have made every unused unit a usage error rather than a white texel.
//   G  Texture2DArray upload/readback preserves two distinct layers byte-for-byte.
//   H  The same transfer path preserves an odd-sized nonzero mip subresource.
//   I  A custom sampler2DArray shader reads layer 0 through binding 16.
//   J  The same native array view exposes independently uploaded layer 1.
//   K  Array unit 3 is refused because the three set-1 array slots plus the other thirteen
//      fragment-stage samplers exactly consume Vulkan's guaranteed minimum of sixteen.
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
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "System/NotSupportedException.hpp"
#ifdef CNA_CNAEXT
#include "CNA/Graphics/Texture2DArray.hpp"
#endif

#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

// ---------------------------------------------------------------------------
// Pre-compiled SPIR-V, from the GLSL in this file's own comments, via libshaderc.
//   vert: the ordinary sprite vertex -- pixel coords to NDC, pass UV and colour through
//   frag: outColor = texture(sampler2D at set 1 binding 0, vUV)   <- the whole point
// ---------------------------------------------------------------------------
static const uint32_t kBoundVertSpv[] = {
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
static const uint32_t kBoundFragSpv[] = {
    0x07230203, 0x00010000, 0x000d000b, 0x00000014, 0x00000000, 0x00020011,
    0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
    0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0007000f, 0x00000004,
    0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x00000011, 0x00030010,
    0x00000004, 0x00000007, 0x00040047, 0x00000009, 0x0000001e, 0x00000000,
    0x00040047, 0x0000000d, 0x00000021, 0x00000000, 0x00040047, 0x0000000d,
    0x00000022, 0x00000001, 0x00040047, 0x00000011, 0x0000001e, 0x00000000,
    0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016,
    0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000004,
    0x00040020, 0x00000008, 0x00000003, 0x00000007, 0x0004003b, 0x00000008,
    0x00000009, 0x00000003, 0x00090019, 0x0000000a, 0x00000006, 0x00000001,
    0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000000, 0x0003001b,
    0x0000000b, 0x0000000a, 0x00040020, 0x0000000c, 0x00000000, 0x0000000b,
    0x0004003b, 0x0000000c, 0x0000000d, 0x00000000, 0x00040017, 0x0000000f,
    0x00000006, 0x00000002, 0x00040020, 0x00000010, 0x00000001, 0x0000000f,
    0x0004003b, 0x00000010, 0x00000011, 0x00000001, 0x00050036, 0x00000002,
    0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0004003d,
    0x0000000b, 0x0000000e, 0x0000000d, 0x0004003d, 0x0000000f, 0x00000012,
    0x00000011, 0x00050057, 0x00000007, 0x00000013, 0x0000000e, 0x00000012,
    0x0003003e, 0x00000009, 0x00000013, 0x000100fd, 0x00010038,
};
static const uint32_t kCubeFragSpv[] = {
    0x07230203, 0x00010000, 0x000d000b, 0x00000014, 0x00000000, 0x00020011,
    0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
    0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0006000f, 0x00000004,
    0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x00030010, 0x00000004,
    0x00000007, 0x00040047, 0x00000009, 0x0000001e, 0x00000000, 0x00040047,
    0x0000000d, 0x00000021, 0x00000004, 0x00040047, 0x0000000d, 0x00000022,
    0x00000001, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002,
    0x00030016, 0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006,
    0x00000004, 0x00040020, 0x00000008, 0x00000003, 0x00000007, 0x0004003b,
    0x00000008, 0x00000009, 0x00000003, 0x00090019, 0x0000000a, 0x00000006,
    0x00000003, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000000,
    0x0003001b, 0x0000000b, 0x0000000a, 0x00040020, 0x0000000c, 0x00000000,
    0x0000000b, 0x0004003b, 0x0000000c, 0x0000000d, 0x00000000, 0x00040017,
    0x0000000f, 0x00000006, 0x00000003, 0x0004002b, 0x00000006, 0x00000010,
    0x00000000, 0x0004002b, 0x00000006, 0x00000011, 0x3f800000, 0x0006002c,
    0x0000000f, 0x00000012, 0x00000010, 0x00000010, 0x00000011, 0x00050036,
    0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005,
    0x0004003d, 0x0000000b, 0x0000000e, 0x0000000d, 0x00050057, 0x00000007,
    0x00000013, 0x0000000e, 0x00000012, 0x0003003e, 0x00000009, 0x00000013,
    0x000100fd, 0x00010038,
};
static const uint32_t kVolumeFragSpv[] = {
    0x07230203, 0x00010000, 0x000d000b, 0x00000013, 0x00000000, 0x00020011,
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
    0x3f000000, 0x0006002c, 0x0000000f, 0x00000011, 0x00000010, 0x00000010,
    0x00000010, 0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003,
    0x000200f8, 0x00000005, 0x0004003d, 0x0000000b, 0x0000000e, 0x0000000d,
    0x00050057, 0x00000007, 0x00000012, 0x0000000e, 0x00000011, 0x0003003e,
    0x00000009, 0x00000012, 0x000100fd, 0x00010038,
};

namespace
{
constexpr int kN = 8;
const Color kWhite(255, 255, 255, 255);
const Color kBlue(0, 0, 255, 255);
const Color kGreen(0, 255, 0, 255);

#ifdef CNA_CNAEXT
std::string ArrayFragmentSpirV(const float layer)
{
    std::vector<std::uint32_t> words(std::begin(kVolumeFragSpv), std::end(kVolumeFragSpv));
    bool patchedBinding = false;
    bool patchedType = false;
    bool patchedCoordinate = false;
    for (std::size_t i = 0; i < words.size(); ++i)
    {
        if (i + 3 < words.size() && words[i] == 0x00040047 &&
            words[i + 1] == 0x0000000d && words[i + 2] == 0x00000021)
        {
            words[i + 3] = 16;
            patchedBinding = true;
        }
        if (i + 8 < words.size() && words[i] == 0x00090019)
        {
            words[i + 3] = 1; // Dim2D
            words[i + 5] = 1; // arrayed
            patchedType = true;
        }
        if (i + 3 < words.size() && words[i] == 0x0004002b &&
            words[i + 1] == 0x00000006 && words[i + 3] == 0x3f000000)
        {
            words[i + 3] = layer < 0.5f ? 0x00000000 : 0x3f800000;
            patchedCoordinate = true;
        }
    }
    if (!patchedBinding || !patchedType || !patchedCoordinate)
        throw std::runtime_error("the embedded sampler3D SPIR-V patch contract drifted");
    return {reinterpret_cast<const char*>(words.data()), words.size() * sizeof(words[0])};
}
#endif
}  // namespace

class VulkanEffectBoundTextureTest final : public Game
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

    static std::unique_ptr<Texture2D> Solid(GraphicsDevice& dev, const Color& c)
    {
        auto t = std::make_unique<Texture2D>(dev, 2, 2, false, SurfaceFormat::Color);
        const std::array<std::uint8_t, 16> px{
            c.getRProperty(), c.getGProperty(), c.getBProperty(), c.getAProperty(),
            c.getRProperty(), c.getGProperty(), c.getBProperty(), c.getAProperty(),
            c.getRProperty(), c.getGProperty(), c.getBProperty(), c.getAProperty(),
            c.getRProperty(), c.getGProperty(), c.getBProperty(), c.getAProperty()};
        t->SetDataRGBA(px.data(), 4);
        return t;
    }

    /// Draws `sprite` through `effect` into a fresh target and returns the centre texel.
    Color DrawThrough(GraphicsDevice& dev, ShaderEffect& effect, Texture2D& sprite)
    {
        RenderTarget2D rt(dev, kN, kN, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.SetRenderTarget(&rt);
        dev.Clear(kGreen);
        {
            SamplerState point = SamplerState::PointClamp;
            SpriteBatch sb(dev);
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr,
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

        const std::string vert(reinterpret_cast<const char*>(kBoundVertSpv), sizeof(kBoundVertSpv));
        const std::string frag(reinterpret_cast<const char*>(kBoundFragSpv), sizeof(kBoundFragSpv));
        ShaderEffect effect(dev, vert, frag);

        auto white = Solid(dev, kWhite);
        auto blue  = Solid(dev, kBlue);
        auto green = Solid(dev, kGreen);

        // A. The sprite's own texture is WHITE and the effect samples only set 1. If the binding
        //    never reached the shader, the unbound unit's filler is white too -- so white is the
        //    failure colour and blue can only come from the bound texture.
        effect.SetTexture(0, *blue);
        {
            const Color got = DrawThrough(dev, effect, *white);
            check(Is(got, kBlue),
                  "A a custom effect samples the texture it was bound, not the sprite's: " +
                      Text(got) + " (want " + Text(kBlue) + "; " + Text(kWhite) +
                      " is what an unbound unit reads)");
        }

        // B. Rebind, redraw. One binding proves plumbing; two prove it is the effect's own state.
        effect.SetTexture(0, *green);
        {
            const Color got = DrawThrough(dev, effect, *white);
            check(Is(got, kGreen),
                  "B rebinding between batches changes what the shader samples: " + Text(got) +
                      " (want " + Text(kGreen) + ")");
        }

        // C. Out of range is refused by name.
        {
            bool threw = false;
            std::string what;
            try { effect.SetTexture(64, *blue); }
            catch (const std::exception& e) { threw = true; what = e.what(); }
            check(threw && what.find("64") != std::string::npos,
                  "C a sampler unit outside the supported range is refused by name: " +
                      (threw ? what : std::string("NOT REFUSED")));
        }

        // E. VULKAN-254: a TextureCube through samplerCube at binding 4.
        {
            const std::string cubeFrag(reinterpret_cast<const char*>(kCubeFragSpv),
                                       sizeof(kCubeFragSpv));
            ShaderEffect cubeEffect(dev, vert, cubeFrag);
            TextureCube cube(dev, 2, false, SurfaceFormat::Color);
            const std::array<Color, 4> face{kBlue, kBlue, kBlue, kBlue};
            for (int f = 0; f < 6; ++f)
                cube.SetData(static_cast<CubeMapFace>(f), face.data(), 4);
            cubeEffect.SetTexture(0, cube);
            const Color got = DrawThrough(dev, cubeEffect, *white);
            check(Is(got, kBlue),
                  "E a bound TextureCube reaches samplerCube at binding 4: " + Text(got) +
                      " (want " + Text(kBlue) + ")");
        }

        // F. VULKAN-254: a Texture3D through sampler3D at binding 8.
        {
            const std::string volFrag(reinterpret_cast<const char*>(kVolumeFragSpv),
                                      sizeof(kVolumeFragSpv));
            ShaderEffect volEffect(dev, vert, volFrag);
            Texture3D volume(dev, 2, 2, 2, false, SurfaceFormat::Color);
            std::vector<Color> voxels(8, kBlue);
            volume.SetData(voxels.data(), static_cast<int>(voxels.size()));
            volEffect.SetTexture(0, volume);
            const Color got = DrawThrough(dev, volEffect, *white);
            check(Is(got, kBlue),
                  "F a bound Texture3D reaches sampler3D at binding 8: " + Text(got) +
                      " (want " + Text(kBlue) + ")");
        }

#ifdef CNA_CNAEXT
        // G/H/I/J. MOD-2226: real array storage, two distinct layers and an odd mip chain.
        // ArrayFragmentSpirV changes the proven volume payload above into sampler2DArray at
        // set 1 binding 16 without introducing a build-time shader-compiler dependency.
        {
            using CNA::Graphics::Texture2DArray;
            using CNA::Graphics::Texture2DArrayDescriptor;
            using CNA::Graphics::Texture2DArrayUsage;
            constexpr auto usage =
                Texture2DArrayUsage::Sampled | Texture2DArrayUsage::Filterable |
                Texture2DArrayUsage::TransferSource |
                Texture2DArrayUsage::TransferDestination;
            Texture2DArray array(
                dev, Texture2DArrayDescriptor(7, 5, 2, 3, SurfaceFormat::Color, usage));

            const auto solidBytes = [](const Color& color, const int width, const int height) {
                std::vector<std::uint8_t> bytes(
                    static_cast<std::size_t>(width * height * 4));
                for (std::size_t i = 0; i < bytes.size(); i += 4) {
                    bytes[i] = color.getRProperty();
                    bytes[i + 1] = color.getGProperty();
                    bytes[i + 2] = color.getBProperty();
                    bytes[i + 3] = color.getAProperty();
                }
                return bytes;
            };
            const std::vector<std::uint8_t> layer0 = solidBytes(kBlue, 7, 5);
            const std::vector<std::uint8_t> layer1 = solidBytes(kGreen, 7, 5);
            const std::vector<std::uint8_t> mipLayer0 =
                solidBytes(Color(255, 0, 0, 255), 3, 2);
            const std::vector<std::uint8_t> mipLayer1 =
                solidBytes(Color(255, 255, 0, 255), 3, 2);
            array.setData(0, 0, nullptr, layer0.data(), layer0.size());
            array.setData(1, 0, nullptr, layer1.data(), layer1.size());
            array.setData(0, 1, nullptr, mipLayer0.data(), mipLayer0.size());
            array.setData(1, 1, nullptr, mipLayer1.data(), mipLayer1.size());

            std::vector<std::uint8_t> readLayer0(layer0.size());
            std::vector<std::uint8_t> readLayer1(layer1.size());
            std::vector<std::uint8_t> readMip1(mipLayer1.size());
            array.getData(0, 0, nullptr, readLayer0.data(), readLayer0.size());
            array.getData(1, 0, nullptr, readLayer1.data(), readLayer1.size());
            array.getData(1, 1, nullptr, readMip1.data(), readMip1.size());
            check(readLayer0 == layer0 && readLayer1 == layer1,
                  "G upload/readback preserves two distinct array layers byte-for-byte");
            check(readMip1 == mipLayer1,
                  "H upload/readback preserves layer 1 of odd 7x5 image's 3x2 mip");

            ShaderEffect layer0Effect(dev, vert, ArrayFragmentSpirV(0.0f));
            ShaderEffect layer1Effect(dev, vert, ArrayFragmentSpirV(1.0f));
            layer0Effect.SetTextureArrayEXT(0, array);
            layer1Effect.SetTextureArrayEXT(0, array);
            const Color sampled0 = DrawThrough(dev, layer0Effect, *white);
            const Color sampled1 = DrawThrough(dev, layer1Effect, *white);
            check(Is(sampled0, kBlue),
                  "I sampler2DArray reads layer 0 at set 1 binding 16: " + Text(sampled0));
            check(Is(sampled1, kGreen),
                  "J sampler2DArray reads layer 1 at set 1 binding 16: " + Text(sampled1));

            bool unitThreeRefused = false;
            try
            {
                layer0Effect.SetTextureArrayEXT(3, array);
            }
            catch (const System::NotSupportedException&)
            {
                unitThreeRefused = true;
            }
            check(unitThreeRefused,
                  "K texture-array unit 3 is refused before it can alias a missing descriptor");
        }
#endif

        {
            const std::size_t after = Renderer().GetValidationMessagesEXT().size();
            check(!VulkanRenderer::IsValidationActiveEXT() || after == messagesBefore,
                  "D no validation message: " + std::to_string(messagesBefore) + " -> " +
                      std::to_string(after) +
                      " (an unwritten binding in a bound set is exactly what this would catch)");
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    VulkanEffectBoundTextureTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanEffectBoundTextureTest game;
    game.Run();
    return game.getResult();
}

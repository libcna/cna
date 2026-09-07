// SPDX-License-Identifier: MS-PL
//
// plan_vulkan.md VULKAN-252 -- a custom effect receives an ARRAY uniform, and the pixels say which
// element went where.
//
// `VULKAN-265` made the four array setters refuse instead of falling silent, and named the reason:
// a `ShaderEffect` here is driven by a fixed 128-byte push-constant block, which has nowhere to put
// an array. This row gives them somewhere -- four uniform-buffer ranges in descriptor set 1, one
// per element type, at bindings 12 (`float`), 13 (`vec2`), 14 (`vec3`) and 15 (`mat4`), all ranges
// of one buffer built at `SpriteBatch::End()` beside the bound textures and the push constants.
//
// The two shaders are written so that a plumbing mistake cannot look like success:
//
//   * the vector shader takes ONE channel from each of the three bindings, at a DIFFERENT element
//     index in each (1, 2, 3). A wrong binding, a wrong sub-range offset, or elements packed
//     tightly instead of padded to 16 bytes the way std140 pads them, each shows up as one wrong
//     channel rather than as a uniformly wrong colour;
//   * the matrix shader reads element 0 column 0 row 0, element 1 column 1 row 1 and element 2
//     column 2 row 2, so it fails on a transposed matrix, on a wrong element stride and on a
//     wrong element index independently.
//
//   A  A four-element `mat4` array renders a colour composed from three different elements.
//   B  Uploading a different array through the SAME effect changes the pixel. One upload proves
//      the plumbing; two prove the buffer is per-upload and that the descriptor set follows it --
//      the trap this renderer had to fix for the sampler in `VULKAN-164` and would have had again.
//   C  The `float`, `vec2` and `vec3` arrays each reach their own binding, read at three different
//      element indices.
//   D  An effect that never sets an array still draws, in black. The set is built unconditionally
//      (`VULKAN-253`'s lesson: a shader that statically uses set 1 with nothing bound there is an
//      invalid draw), so the array bindings need a real buffer even when nothing wrote one.
//   E  No validation message -- a mis-sized range or an unaligned offset lands here.
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
// Pre-compiled SPIR-V, through modules/renderers/vulkan/src/shaders/compile_shaders.py.
//   vert:       sprite2d.vert.glsl, unchanged
//   vector frag: vec4(uWeights[1], uOffsets[2].y, uDirs[3].z, 1.0)   bindings 12/13/14
//   matrix frag: vec4(uBones[0][0][0], uBones[1][1][1], uBones[2][2][2], 1.0)   binding 15
// ---------------------------------------------------------------------------
static const uint32_t kArrayVertSpv[] = {
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
static const uint32_t kVectorArrayFragSpv[] = {
    0x07230203, 0x00010000, 0x000d000b, 0x0000002a, 0x00000000, 0x00020011,
    0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
    0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0006000f, 0x00000004,
    0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x00030010, 0x00000004,
    0x00000007, 0x00040047, 0x00000009, 0x0000001e, 0x00000000, 0x00040047,
    0x0000000c, 0x00000006, 0x00000010, 0x00030047, 0x0000000d, 0x00000002,
    0x00050048, 0x0000000d, 0x00000000, 0x00000023, 0x00000000, 0x00040047,
    0x0000000f, 0x00000021, 0x0000000c, 0x00040047, 0x0000000f, 0x00000022,
    0x00000001, 0x00040047, 0x00000017, 0x00000006, 0x00000010, 0x00030047,
    0x00000018, 0x00000002, 0x00050048, 0x00000018, 0x00000000, 0x00000023,
    0x00000000, 0x00040047, 0x0000001a, 0x00000021, 0x0000000d, 0x00040047,
    0x0000001a, 0x00000022, 0x00000001, 0x00040047, 0x00000020, 0x00000006,
    0x00000010, 0x00030047, 0x00000021, 0x00000002, 0x00050048, 0x00000021,
    0x00000000, 0x00000023, 0x00000000, 0x00040047, 0x00000023, 0x00000021,
    0x0000000e, 0x00040047, 0x00000023, 0x00000022, 0x00000001, 0x00020013,
    0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016, 0x00000006,
    0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000004, 0x00040020,
    0x00000008, 0x00000003, 0x00000007, 0x0004003b, 0x00000008, 0x00000009,
    0x00000003, 0x00040015, 0x0000000a, 0x00000020, 0x00000000, 0x0004002b,
    0x0000000a, 0x0000000b, 0x00000048, 0x0004001c, 0x0000000c, 0x00000006,
    0x0000000b, 0x0003001e, 0x0000000d, 0x0000000c, 0x00040020, 0x0000000e,
    0x00000002, 0x0000000d, 0x0004003b, 0x0000000e, 0x0000000f, 0x00000002,
    0x00040015, 0x00000010, 0x00000020, 0x00000001, 0x0004002b, 0x00000010,
    0x00000011, 0x00000000, 0x0004002b, 0x00000010, 0x00000012, 0x00000001,
    0x00040020, 0x00000013, 0x00000002, 0x00000006, 0x00040017, 0x00000016,
    0x00000006, 0x00000002, 0x0004001c, 0x00000017, 0x00000016, 0x0000000b,
    0x0003001e, 0x00000018, 0x00000017, 0x00040020, 0x00000019, 0x00000002,
    0x00000018, 0x0004003b, 0x00000019, 0x0000001a, 0x00000002, 0x0004002b,
    0x00000010, 0x0000001b, 0x00000002, 0x0004002b, 0x0000000a, 0x0000001c,
    0x00000001, 0x00040017, 0x0000001f, 0x00000006, 0x00000003, 0x0004001c,
    0x00000020, 0x0000001f, 0x0000000b, 0x0003001e, 0x00000021, 0x00000020,
    0x00040020, 0x00000022, 0x00000002, 0x00000021, 0x0004003b, 0x00000022,
    0x00000023, 0x00000002, 0x0004002b, 0x00000010, 0x00000024, 0x00000003,
    0x0004002b, 0x0000000a, 0x00000025, 0x00000002, 0x0004002b, 0x00000006,
    0x00000028, 0x3f800000, 0x00050036, 0x00000002, 0x00000004, 0x00000000,
    0x00000003, 0x000200f8, 0x00000005, 0x00060041, 0x00000013, 0x00000014,
    0x0000000f, 0x00000011, 0x00000012, 0x0004003d, 0x00000006, 0x00000015,
    0x00000014, 0x00070041, 0x00000013, 0x0000001d, 0x0000001a, 0x00000011,
    0x0000001b, 0x0000001c, 0x0004003d, 0x00000006, 0x0000001e, 0x0000001d,
    0x00070041, 0x00000013, 0x00000026, 0x00000023, 0x00000011, 0x00000024,
    0x00000025, 0x0004003d, 0x00000006, 0x00000027, 0x00000026, 0x00070050,
    0x00000007, 0x00000029, 0x00000015, 0x0000001e, 0x00000027, 0x00000028,
    0x0003003e, 0x00000009, 0x00000029, 0x000100fd, 0x00010038,
};
static const uint32_t kMatrixArrayFragSpv[] = {
    0x07230203, 0x00010000, 0x000d000b, 0x00000021, 0x00000000, 0x00020011,
    0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
    0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0006000f, 0x00000004,
    0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x00030010, 0x00000004,
    0x00000007, 0x00040047, 0x00000009, 0x0000001e, 0x00000000, 0x00040047,
    0x0000000d, 0x00000006, 0x00000040, 0x00030047, 0x0000000e, 0x00000002,
    0x00040048, 0x0000000e, 0x00000000, 0x00000005, 0x00050048, 0x0000000e,
    0x00000000, 0x00000007, 0x00000010, 0x00050048, 0x0000000e, 0x00000000,
    0x00000023, 0x00000000, 0x00040047, 0x00000010, 0x00000021, 0x0000000f,
    0x00040047, 0x00000010, 0x00000022, 0x00000001, 0x00020013, 0x00000002,
    0x00030021, 0x00000003, 0x00000002, 0x00030016, 0x00000006, 0x00000020,
    0x00040017, 0x00000007, 0x00000006, 0x00000004, 0x00040020, 0x00000008,
    0x00000003, 0x00000007, 0x0004003b, 0x00000008, 0x00000009, 0x00000003,
    0x00040018, 0x0000000a, 0x00000007, 0x00000004, 0x00040015, 0x0000000b,
    0x00000020, 0x00000000, 0x0004002b, 0x0000000b, 0x0000000c, 0x00000048,
    0x0004001c, 0x0000000d, 0x0000000a, 0x0000000c, 0x0003001e, 0x0000000e,
    0x0000000d, 0x00040020, 0x0000000f, 0x00000002, 0x0000000e, 0x0004003b,
    0x0000000f, 0x00000010, 0x00000002, 0x00040015, 0x00000011, 0x00000020,
    0x00000001, 0x0004002b, 0x00000011, 0x00000012, 0x00000000, 0x0004002b,
    0x0000000b, 0x00000013, 0x00000000, 0x00040020, 0x00000014, 0x00000002,
    0x00000006, 0x0004002b, 0x00000011, 0x00000017, 0x00000001, 0x0004002b,
    0x0000000b, 0x00000018, 0x00000001, 0x0004002b, 0x00000011, 0x0000001b,
    0x00000002, 0x0004002b, 0x0000000b, 0x0000001c, 0x00000002, 0x0004002b,
    0x00000006, 0x0000001f, 0x3f800000, 0x00050036, 0x00000002, 0x00000004,
    0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x00080041, 0x00000014,
    0x00000015, 0x00000010, 0x00000012, 0x00000012, 0x00000012, 0x00000013,
    0x0004003d, 0x00000006, 0x00000016, 0x00000015, 0x00080041, 0x00000014,
    0x00000019, 0x00000010, 0x00000012, 0x00000017, 0x00000017, 0x00000018,
    0x0004003d, 0x00000006, 0x0000001a, 0x00000019, 0x00080041, 0x00000014,
    0x0000001d, 0x00000010, 0x00000012, 0x0000001b, 0x0000001b, 0x0000001c,
    0x0004003d, 0x00000006, 0x0000001e, 0x0000001d, 0x00070050, 0x00000007,
    0x00000020, 0x00000016, 0x0000001a, 0x0000001e, 0x0000001f, 0x0003003e,
    0x00000009, 0x00000020, 0x000100fd, 0x00010038,
};

namespace
{
constexpr int kN = 8;
const Color kWhite(255, 255, 255, 255);
const Color kGreen(0, 255, 0, 255);
const Color kMagenta(255, 0, 255, 255);
const Color kCyan(0, 255, 255, 255);
const Color kYellow(255, 255, 0, 255);
const Color kBlack(0, 0, 0, 255);
}  // namespace

class VulkanShaderEffectUniformArraysTest final : public Game
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

    /// Four 4x4 matrices, column-major, all zero but for the diagonal component each channel of
    /// the matrix shader reads: element 0 at [0][0], element 1 at [1][1], element 2 at [2][2].
    static std::array<float, 64> Bones(float r, float g, float b)
    {
        std::array<float, 64> m{};
        m[0]       = r;   // element 0, column 0 row 0
        m[16 + 5]  = g;   // element 1, column 1 row 1
        m[32 + 10] = b;   // element 2, column 2 row 2
        return m;         // element 3 stays zero: the row asks for a FOUR-element array
    }

    Color DrawWith(GraphicsDevice& dev, ShaderEffect& effect, Texture2D& sprite)
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

        const std::string vert(reinterpret_cast<const char*>(kArrayVertSpv), sizeof(kArrayVertSpv));
        const std::string vecFrag(reinterpret_cast<const char*>(kVectorArrayFragSpv),
                                  sizeof(kVectorArrayFragSpv));
        const std::string matFrag(reinterpret_cast<const char*>(kMatrixArrayFragSpv),
                                  sizeof(kMatrixArrayFragSpv));

        Texture2D sprite(dev, 2, 2, false, SurfaceFormat::Color);
        const std::array<std::uint8_t, 16> white{255, 255, 255, 255, 255, 255, 255, 255,
                                                 255, 255, 255, 255, 255, 255, 255, 255};
        sprite.SetDataRGBA(white.data(), 4);

        // A and B share one effect on purpose: the second upload has to replace the buffer AND the
        // descriptor set that names it.
        ShaderEffect matEffect(dev, vert, matFrag);
        {
            const std::array<float, 64> bones = Bones(1.0f, 0.0f, 1.0f);
            matEffect.SetUniformMat4Array("uBones", bones.data(), 4);
            const Color got = DrawWith(dev, matEffect, sprite);
            check(Is(got, kMagenta),
                  "A a four-element mat4 array reaches the shader, one channel per element: " +
                      Text(got) + " (want " + Text(kMagenta) + ")");
        }
        {
            const std::array<float, 64> bones = Bones(0.0f, 1.0f, 1.0f);
            matEffect.SetUniformMat4Array("uBones", bones.data(), 4);
            const Color got = DrawWith(dev, matEffect, sprite);
            check(Is(got, kCyan),
                  "B a second upload through the same effect changes the pixel: " + Text(got) +
                      " (want " + Text(kCyan) + "; " + Text(kMagenta) +
                      " means the batch re-used the first upload's buffer)");
        }

        {
            ShaderEffect vecEffect(dev, vert, vecFrag);
            const std::array<float, 4> weights{0.0f, 1.0f, 0.0f, 0.0f};   // uWeights[1] = 1 -> R
            const std::array<float, 8> offsets{0, 0, 0, 0, 0, 1, 0, 0};   // uOffsets[2].y = 1 -> G
            const std::array<float, 12> dirs{};                           // uDirs[3].z = 0 -> B
            vecEffect.SetUniformFloatArray("uWeights", weights.data(), 4);
            vecEffect.SetUniformVec2Array("uOffsets", offsets.data(), 4);
            vecEffect.SetUniformVec3Array("uDirs", dirs.data(), 4);
            const Color got = DrawWith(dev, vecEffect, sprite);
            check(Is(got, kYellow),
                  "C the float, vec2 and vec3 arrays each reach their own binding, read at "
                  "elements 1, 2 and 3: " + Text(got) + " (want " + Text(kYellow) + ")");
        }

        {
            ShaderEffect untouched(dev, vert, matFrag);
            const Color got = DrawWith(dev, untouched, sprite);
            check(Is(got, kBlack),
                  "D an effect that set no array still draws, in black: " + Text(got) + " (want " +
                      Text(kBlack) + "; the clear colour " + Text(kGreen) +
                      " would mean the draw never happened)");
        }

        {
            const std::size_t after = Renderer().GetValidationMessagesEXT().size();
            check(!VulkanRenderer::IsValidationActiveEXT() || after == messagesBefore,
                  "E no validation message: " + std::to_string(messagesBefore) + " -> " +
                      std::to_string(after) +
                      " (a mis-sized range or an unaligned offset lands here)");
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    VulkanShaderEffectUniformArraysTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanShaderEffectUniformArraysTest game;
    game.Run();
    return game.getResult();
}

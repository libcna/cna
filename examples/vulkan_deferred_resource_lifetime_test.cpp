// SPDX-License-Identifier: MS-PL
// REMED-GFX-075: Vulkan defers every draw to a single Present-time (or GetData-flush-time) record.
// A deferred entry therefore borrows GPU resources that belong to the *source* objects a draw
// samples/uses -- the VkImageView baked into a captured descriptor set (Texture2D / TextureCube /
// RenderTarget2D-used-as-sampler), a custom Effect's VkPipeline, and an OcclusionQuery's VkQueryPool.
// Destroying any of those AFTER queuing the draw but BEFORE the record consumes it must NOT:
//   * dereference freed memory (heap-use-after-free -- run under ASan), nor
//   * hand Vulkan a destroyed VkImageView / VkPipeline / VkQueryPool (validation lifetime VUIDs), nor
//   * silently drop / alter the already-issued draw.
// The draw was legitimately issued; a deferred backend must behave like an immediate one, i.e. the
// resource is logically consumed the moment the public Draw()/SpriteBatch.End() returns.
//
// GFX-074 fixed the *destination* render-target identity (PurgeDeferredWorkForTarget). This pins the
// *source*-resource paths GFX-074's destination-keyed purge does not cover.
//
// Every content scene reads back through RenderTarget2D::GetData (GFX-074's flush) or
// GetBackBufferData so the deferred work is actually recorded on the GPU with the source resource
// already destroyed -- the exact moment the pre-fix code dangled. Exit 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

// ---------------------------------------------------------------------------
// Pre-compiled SPIR-V for a tint effect (texture * vColor * pc.uColor), copied
// verbatim from examples/vulkan_shader_effect_test.cpp (Task 119). Push-constant
// contract: vec2 vpSize | mat4 uMatrix | vec4 uColor | float uFloat0.
// ---------------------------------------------------------------------------
static const uint32_t kTintVertSpv[] = {
    0x07230203, 0x00010000, 0x000d000a, 0x00000036, 0x00000000, 0x00020011,
    0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
    0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x000b000f, 0x00000000,
    0x00000004, 0x6e69616d, 0x00000000, 0x0000000b, 0x00000022, 0x0000002f,
    0x00000030, 0x00000032, 0x00000034, 0x00030003, 0x00000002, 0x000001c2,
    0x000a0004, 0x475f4c47, 0x4c474f4f, 0x70635f45, 0x74735f70, 0x5f656c79,
    0x656e696c, 0x7269645f, 0x69746365, 0x00006576, 0x00080004, 0x475f4c47,
    0x4c474f4f, 0x6e695f45, 0x64756c63, 0x69645f65, 0x74636572, 0x00657669,
    0x00040005, 0x00000004, 0x6e69616d, 0x00000000, 0x00030005, 0x00000009,
    0x0063646e, 0x00040005, 0x0000000b, 0x736f5061, 0x00000000, 0x00030005,
    0x0000000f, 0x00004350, 0x00050006, 0x0000000f, 0x00000000, 0x69537076,
    0x0000657a, 0x00050006, 0x0000000f, 0x00000001, 0x74614d75, 0x00786972,
    0x00050006, 0x0000000f, 0x00000002, 0x6c6f4375, 0x0000726f, 0x00050006,
    0x0000000f, 0x00000003, 0x6f6c4675, 0x00307461, 0x00030005, 0x00000011,
    0x00006370, 0x00060005, 0x00000020, 0x505f6c67, 0x65567265, 0x78657472,
    0x00000000, 0x00060006, 0x00000020, 0x00000000, 0x505f6c67, 0x7469736f,
    0x006e6f69, 0x00070006, 0x00000020, 0x00000001, 0x505f6c67, 0x746e696f,
    0x657a6953, 0x00000000, 0x00070006, 0x00000020, 0x00000002, 0x435f6c67,
    0x4470696c, 0x61747369, 0x0065636e, 0x00070006, 0x00000020, 0x00000003,
    0x435f6c67, 0x446c6c75, 0x61747369, 0x0065636e, 0x00030005, 0x00000022,
    0x00000000, 0x00030005, 0x0000002f, 0x00565576, 0x00030005, 0x00000030,
    0x00565561, 0x00040005, 0x00000032, 0x6c6f4376, 0x0000726f, 0x00040005,
    0x00000034, 0x6c6f4361, 0x0000726f, 0x00040047, 0x0000000b, 0x0000001e,
    0x00000000, 0x00050048, 0x0000000f, 0x00000000, 0x00000023, 0x00000000,
    0x00040048, 0x0000000f, 0x00000001, 0x00000005, 0x00050048, 0x0000000f,
    0x00000001, 0x00000023, 0x00000010, 0x00050048, 0x0000000f, 0x00000001,
    0x00000007, 0x00000010, 0x00050048, 0x0000000f, 0x00000002, 0x00000023,
    0x00000050, 0x00050048, 0x0000000f, 0x00000003, 0x00000023, 0x00000060,
    0x00030047, 0x0000000f, 0x00000002, 0x00050048, 0x00000020, 0x00000000,
    0x0000000b, 0x00000000, 0x00050048, 0x00000020, 0x00000001, 0x0000000b,
    0x00000001, 0x00050048, 0x00000020, 0x00000002, 0x0000000b, 0x00000003,
    0x00050048, 0x00000020, 0x00000003, 0x0000000b, 0x00000004, 0x00030047,
    0x00000020, 0x00000002, 0x00040047, 0x0000002f, 0x0000001e, 0x00000000,
    0x00040047, 0x00000030, 0x0000001e, 0x00000001, 0x00040047, 0x00000032,
    0x0000001e, 0x00000001, 0x00040047, 0x00000034, 0x0000001e, 0x00000002,
    0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016,
    0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000002,
    0x00040020, 0x00000008, 0x00000007, 0x00000007, 0x00040020, 0x0000000a,
    0x00000001, 0x00000007, 0x0004003b, 0x0000000a, 0x0000000b, 0x00000001,
    0x00040017, 0x0000000d, 0x00000006, 0x00000004, 0x00040018, 0x0000000e,
    0x0000000d, 0x00000004, 0x0006001e, 0x0000000f, 0x00000007, 0x0000000e,
    0x0000000d, 0x00000006, 0x00040020, 0x00000010, 0x00000009, 0x0000000f,
    0x0004003b, 0x00000010, 0x00000011, 0x00000009, 0x00040015, 0x00000012,
    0x00000020, 0x00000001, 0x0004002b, 0x00000012, 0x00000013, 0x00000000,
    0x00040020, 0x00000014, 0x00000009, 0x00000007, 0x0004002b, 0x00000006,
    0x00000018, 0x40000000, 0x0004002b, 0x00000006, 0x0000001a, 0x3f800000,
    0x0005002c, 0x00000007, 0x0000001b, 0x0000001a, 0x0000001a, 0x00040015,
    0x0000001d, 0x00000020, 0x00000000, 0x0004002b, 0x0000001d, 0x0000001e,
    0x00000001, 0x0004001c, 0x0000001f, 0x00000006, 0x0000001e, 0x0006001e,
    0x00000020, 0x0000000d, 0x00000006, 0x0000001f, 0x0000001f, 0x00040020,
    0x00000021, 0x00000003, 0x00000020, 0x0004003b, 0x00000021, 0x00000022,
    0x00000003, 0x0004002b, 0x0000001d, 0x00000023, 0x00000000, 0x00040020,
    0x00000024, 0x00000007, 0x00000006, 0x0004002b, 0x00000006, 0x0000002a,
    0x00000000, 0x00040020, 0x0000002c, 0x00000003, 0x0000000d, 0x00040020,
    0x0000002e, 0x00000003, 0x00000007, 0x0004003b, 0x0000002e, 0x0000002f,
    0x00000003, 0x0004003b, 0x0000000a, 0x00000030, 0x00000001, 0x0004003b,
    0x0000002c, 0x00000032, 0x00000003, 0x00040020, 0x00000033, 0x00000001,
    0x0000000d, 0x0004003b, 0x00000033, 0x00000034, 0x00000001, 0x00050036,
    0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005,
    0x0004003b, 0x00000008, 0x00000009, 0x00000007, 0x0004003d, 0x00000007,
    0x0000000c, 0x0000000b, 0x00050041, 0x00000014, 0x00000015, 0x00000011,
    0x00000013, 0x0004003d, 0x00000007, 0x00000016, 0x00000015, 0x00050088,
    0x00000007, 0x00000017, 0x0000000c, 0x00000016, 0x0005008e, 0x00000007,
    0x00000019, 0x00000017, 0x00000018, 0x00050083, 0x00000007, 0x0000001c,
    0x00000019, 0x0000001b, 0x0003003e, 0x00000009, 0x0000001c, 0x00050041,
    0x00000024, 0x00000025, 0x00000009, 0x00000023, 0x0004003d, 0x00000006,
    0x00000026, 0x00000025, 0x00050041, 0x00000024, 0x00000027, 0x00000009,
    0x0000001e, 0x0004003d, 0x00000006, 0x00000028, 0x00000027, 0x0004007f,
    0x00000006, 0x00000029, 0x00000028, 0x00070050, 0x0000000d, 0x0000002b,
    0x00000026, 0x00000029, 0x0000002a, 0x0000001a, 0x00050041, 0x0000002c,
    0x0000002d, 0x00000022, 0x00000013, 0x0003003e, 0x0000002d, 0x0000002b,
    0x0004003d, 0x00000007, 0x00000031, 0x00000030, 0x0003003e, 0x0000002f,
    0x00000031, 0x0004003d, 0x0000000d, 0x00000035, 0x00000034, 0x0003003e,
    0x00000032, 0x00000035, 0x000100fd, 0x00010038
};
static const size_t kTintVertSpv_size = 1768;

static const uint32_t kTintFragSpv[] = {
    0x07230203, 0x00010000, 0x000d000a, 0x00000025, 0x00000000, 0x00020011,
    0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
    0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0008000f, 0x00000004,
    0x00000004, 0x6e69616d, 0x00000000, 0x00000011, 0x00000015, 0x00000018,
    0x00030010, 0x00000004, 0x00000007, 0x00030003, 0x00000002, 0x000001c2,
    0x000a0004, 0x475f4c47, 0x4c474f4f, 0x70635f45, 0x74735f70, 0x5f656c79,
    0x656e696c, 0x7269645f, 0x69746365, 0x00006576, 0x00080004, 0x475f4c47,
    0x4c474f4f, 0x6e695f45, 0x64756c63, 0x69645f65, 0x74636572, 0x00657669,
    0x00040005, 0x00000004, 0x6e69616d, 0x00000000, 0x00050005, 0x00000009,
    0x43786574, 0x726f6c6f, 0x00000000, 0x00040005, 0x0000000d, 0x78655475,
    0x00000000, 0x00030005, 0x00000011, 0x00565576, 0x00050005, 0x00000015,
    0x67617266, 0x6f6c6f43, 0x00000072, 0x00040005, 0x00000018, 0x6c6f4376,
    0x0000726f, 0x00030005, 0x0000001c, 0x00004350, 0x00050006, 0x0000001c,
    0x00000000, 0x69537076, 0x0000657a, 0x00050006, 0x0000001c, 0x00000001,
    0x74614d75, 0x00786972, 0x00050006, 0x0000001c, 0x00000002, 0x6c6f4375,
    0x0000726f, 0x00050006, 0x0000001c, 0x00000003, 0x6f6c4675, 0x00307461,
    0x00030005, 0x0000001e, 0x00006370, 0x00040047, 0x0000000d, 0x00000022,
    0x00000000, 0x00040047, 0x0000000d, 0x00000021, 0x00000000, 0x00040047,
    0x00000011, 0x0000001e, 0x00000000, 0x00040047, 0x00000015, 0x0000001e,
    0x00000000, 0x00040047, 0x00000018, 0x0000001e, 0x00000001, 0x00050048,
    0x0000001c, 0x00000000, 0x00000023, 0x00000000, 0x00040048, 0x0000001c,
    0x00000001, 0x00000005, 0x00050048, 0x0000001c, 0x00000001, 0x00000023,
    0x00000010, 0x00050048, 0x0000001c, 0x00000001, 0x00000007, 0x00000010,
    0x00050048, 0x0000001c, 0x00000002, 0x00000023, 0x00000050, 0x00050048,
    0x0000001c, 0x00000003, 0x00000023, 0x00000060, 0x00030047, 0x0000001c,
    0x00000002, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002,
    0x00030016, 0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006,
    0x00000004, 0x00040020, 0x00000008, 0x00000007, 0x00000007, 0x00090019,
    0x0000000a, 0x00000006, 0x00000001, 0x00000000, 0x00000000, 0x00000000,
    0x00000001, 0x00000000, 0x0003001b, 0x0000000b, 0x0000000a, 0x00040020,
    0x0000000c, 0x00000000, 0x0000000b, 0x0004003b, 0x0000000c, 0x0000000d,
    0x00000000, 0x00040017, 0x0000000f, 0x00000006, 0x00000002, 0x00040020,
    0x00000010, 0x00000001, 0x0000000f, 0x0004003b, 0x00000010, 0x00000011,
    0x00000001, 0x00040020, 0x00000014, 0x00000003, 0x00000007, 0x0004003b,
    0x00000014, 0x00000015, 0x00000003, 0x00040020, 0x00000017, 0x00000001,
    0x00000007, 0x0004003b, 0x00000017, 0x00000018, 0x00000001, 0x00040018,
    0x0000001b, 0x00000007, 0x00000004, 0x0006001e, 0x0000001c, 0x0000000f,
    0x0000001b, 0x00000007, 0x00000006, 0x00040020, 0x0000001d, 0x00000009,
    0x0000001c, 0x0004003b, 0x0000001d, 0x0000001e, 0x00000009, 0x00040015,
    0x0000001f, 0x00000020, 0x00000001, 0x0004002b, 0x0000001f, 0x00000020,
    0x00000002, 0x00040020, 0x00000021, 0x00000009, 0x00000007, 0x00050036,
    0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005,
    0x0004003b, 0x00000008, 0x00000009, 0x00000007, 0x0004003d, 0x0000000b,
    0x0000000e, 0x0000000d, 0x0004003d, 0x0000000f, 0x00000012, 0x00000011,
    0x00050057, 0x00000007, 0x00000013, 0x0000000e, 0x00000012, 0x0003003e,
    0x00000009, 0x00000013, 0x0004003d, 0x00000007, 0x00000016, 0x00000009,
    0x0004003d, 0x00000007, 0x00000019, 0x00000018, 0x00050085, 0x00000007,
    0x0000001a, 0x00000016, 0x00000019, 0x00050041, 0x00000021, 0x00000022,
    0x0000001e, 0x00000020, 0x0004003d, 0x00000007, 0x00000023, 0x00000022,
    0x00050085, 0x00000007, 0x00000024, 0x0000001a, 0x00000023, 0x0003003e,
    0x00000015, 0x00000024, 0x000100fd, 0x00010038
};
static const size_t kTintFragSpv_size = 1216;

namespace
{
    constexpr int kW = 64;
    constexpr int kH = 48;

    bool IsRed(const Color& c)   { return c.getRProperty() >= 200 && c.getGProperty() <= 60 && c.getBProperty() <= 60; }
    bool IsGreen(const Color& c) { return c.getGProperty() >= 200 && c.getRProperty() <= 60 && c.getBProperty() <= 60; }
    bool IsBlue(const Color& c)  { return c.getBProperty() >= 200 && c.getRProperty() <= 60 && c.getGProperty() <= 60; }
}

class DeferredResourceLifetimeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D whiteTex_;
    bool done_ = false;
    int  pass_ = 0;
    int  total_ = 0;
    int  result_ = 1;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++total_;
        if (ok) ++pass_;
    }

    std::unique_ptr<RenderTarget2D> makeRt()
    {
        auto& dev = getGraphicsDeviceProperty();
        return std::make_unique<RenderTarget2D>(dev, kW, kH, false, SurfaceFormat::Color,
                                                DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
    }

    // A solid single-colour 1x1 texture (sampled/tinted to fill).
    std::unique_ptr<Texture2D> makeColorTex(std::uint8_t r, std::uint8_t g, std::uint8_t b)
    {
        auto& dev = getGraphicsDeviceProperty();
        return std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            dev, 1, 1, std::vector<std::uint8_t>{ r, g, b, 255 }));
    }

    void drawSpriteTex(const Texture2D& tex, const Rectangle& dst, const Color& tint)
    {
        auto& dev = getGraphicsDeviceProperty();
        SpriteBatch sb(dev);
        SamplerState point = SamplerState::PointClamp;
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        sb.Draw(tex, dst, Rectangle(0, 0, tex.getWidthProperty(), tex.getHeightProperty()), tint);
        sb.End();
    }

    std::vector<Color> readRt(RenderTarget2D& rt)
    {
        std::vector<Color> pix(static_cast<std::size_t>(kW) * kH, Color(0, 0, 0, 0));
        rt.GetData(0, nullptr, pix.data(), 0, static_cast<int>(pix.size()));
        return pix;
    }

    const Color& at(const std::vector<Color>& pix, int x, int y)
    {
        return pix[static_cast<std::size_t>(y) * kW + x];
    }

    template <typename Pred>
    int countIf(const std::vector<Color>& pix, Pred pred)
    {
        int n = 0;
        for (const auto& c : pix) if (pred(c)) ++n;
        return n;
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        whiteTex_ = Texture2D::CreateFromPixels(dev, 1, 1,
                        std::vector<std::uint8_t>{255, 255, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();

        // --- S1: Texture2D sprite source DESTROYED before the RT flush. -----------------------
        // The sprite's descriptor set bakes the texture's VkImageView; destroying the texture must
        // keep that view alive until GetData records the deferred batch. Content must be the
        // texture's colour, not garbage / a UAF.
        {
            auto rt = makeRt();
            auto redTex = makeColorTex(255, 0, 0);
            dev.SetRenderTarget(rt.get());
            dev.Clear(Color::Black);
            drawSpriteTex(*redTex, Rectangle(0, 0, kW, kH), Color::White);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            redTex.reset();                                 // destroy BEFORE the flush below
            const std::vector<Color> pix = readRt(*rt);     // flush records the batch (UAF window)
            check(countIf(pix, IsRed) >= kW * kH / 2,
                  "S1 Texture2D destroyed before flush: sprite still RED (view kept alive)");
        }

        // --- S2: ten sprites sharing ONE texture, destroyed before flush (Phase 18). ----------
        {
            auto rt = makeRt();
            auto greenTex = makeColorTex(0, 255, 0);
            dev.SetRenderTarget(rt.get());
            dev.Clear(Color::Black);
            for (int i = 0; i < 10; ++i)
                drawSpriteTex(*greenTex, Rectangle(i * 6, i * 4, 6, 4), Color::White);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            greenTex.reset();
            const std::vector<Color> pix = readRt(*rt);
            check(IsGreen(at(pix, 2, 2)) && IsGreen(at(pix, 32, 22)),
                  "S2 ten draws sharing one destroyed texture all render GREEN");
        }

        // --- S3: RenderTarget2D used as a SAMPLER, destroyed before the destination flush. -----
        // (Phase 6.) rtSrc is filled and flushed first (real content in its colour image), then
        // sampled into rtDst; destroying rtSrc must retire its sample view, not free it, so rtDst's
        // captured descriptor still points at valid data. PurgeDeferredWorkForTarget(rtSrc) must NOT
        // remove rtDst's draw (rtSrc is its SOURCE, not its destination).
        {
            auto rtSrc = makeRt();
            auto rtDst = makeRt();
            dev.SetRenderTarget(rtSrc.get());
            dev.Clear(Color::Black);
            drawSpriteTex(whiteTex_, Rectangle(0, 0, kW, kH), Color::Blue);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            (void)readRt(*rtSrc);                           // force rtSrc content into its image

            dev.SetRenderTarget(rtDst.get());
            dev.Clear(Color::Black);
            drawSpriteTex(*rtSrc, Rectangle(0, 0, kW, kH), Color::White);  // rtSrc AS a texture
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            rtSrc.reset();                                  // destroy the SOURCE render target
            const std::vector<Color> pix = readRt(*rtDst);  // flush samples rtSrc's retired view
            check(countIf(pix, IsBlue) >= kW * kH / 2,
                  "S3 RenderTarget2D-as-sampler destroyed before flush: rtDst sampled BLUE");
        }

        // --- S3b: S3 WITHOUT the intermediate readback of the source (REMED-GFX-166). -----------
        // S3 above contains `(void)readRt(*rtSrc)`, commented "force rtSrc content into its image".
        // That call is a FLUSH, and it is why S3 could not see REMED-GFX-166: it consumed rtSrc's
        // own producer work while rtSrc was still alive, so the destination-keyed purge in
        // ~VulkanRenderTargetBackend had nothing left to drop. Its comment asserted the right thing
        // about the CONSUMER draw ("rtSrc is its SOURCE, not its destination") and missed that the
        // purge was removing rtSrc's PRODUCER instead -- after which the consumer sampled an image
        // nothing had written.
        //
        // This scene is S3 with that one line deleted, which is the ordinary way a game writes it:
        // nothing reads the intermediate target, so the producer is still queued when it dies.
        {
            auto rtSrc = makeRt();
            auto rtDst = makeRt();
            dev.SetRenderTarget(rtSrc.get());
            dev.Clear(Color::Black);
            // A tint no other scene in this file uses, checked EXACTLY. Measured, not assumed: with
            // the loose IsBlue predicate and Color::Blue this scene PASSED against the unfixed
            // backend, because a purged producer leaves the target's image UNDEFINED and the
            // recycled device memory happened to satisfy "blue enough". An exact, unique colour
            // cannot be produced by accident.
            const Color kS3bTint(bytecs(205), bytecs(145), bytecs(65), bytecs(255));
            drawSpriteTex(whiteTex_, Rectangle(0, 0, kW, kH), kS3bTint);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            // <-- deliberately NO readRt(*rtSrc): the producer stays queued

            dev.SetRenderTarget(rtDst.get());
            dev.Clear(Color::Black);
            drawSpriteTex(*rtSrc, Rectangle(0, 0, kW, kH), Color::White);  // rtSrc AS a texture
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            rtSrc.reset();                                  // producer STILL QUEUED as it dies
            const std::vector<Color> pix = readRt(*rtDst);
            const int exact = countIf(pix, [&kS3bTint](const Color& c) {
                return c.getRProperty() == kS3bTint.getRProperty() &&
                       c.getGProperty() == kS3bTint.getGProperty() &&
                       c.getBProperty() == kS3bTint.getBProperty() &&
                       c.getAProperty() == kS3bTint.getAProperty();
            });
            check(exact >= kW * kH / 2,
                  "S3b unflushed RenderTarget2D source destroyed before the consumer record: "
                  "rtDst sampled the producer's EXACT colour (" + std::to_string(exact) + "/" +
                  std::to_string(kW * kH) + ")");
        }

        // --- S4: custom Effect DESTROYED before the backbuffer flush (Phase 7). ----------------
        // The sprite batch snapshots the effect's pipeline/layout/push-constants at End(); the
        // effect's VkPipeline must stay alive until the record. Read via GetBackBufferData (its own
        // deferred submit). white texture * red tint = red.
        {
            std::string vertSpv(reinterpret_cast<const char*>(kTintVertSpv), kTintVertSpv_size);
            std::string fragSpv(reinterpret_cast<const char*>(kTintFragSpv), kTintFragSpv_size);
            auto fx = std::make_unique<ShaderEffect>(dev, vertSpv, fragSpv);
            bool built = fx->IsEffectValid();
            dev.Clear(Color::Black);
            dev.SetDepthTestEnabled(false);
            {
                fx->SetUniformVec4("uColor", 1.0f, 0.0f, 0.0f, 1.0f);
                fx->Apply();
                SpriteBatch sb(dev);
                sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr, fx.get());
                sb.Draw(whiteTex_, Rectangle(20, 15, 50, 40), Rectangle(0, 0, 1, 1), Color::White);
                sb.End();
            }
            fx.reset();                                     // destroy the effect BEFORE the flush
            Color px(0, 0, 0, 0);
            Rectangle reg(40, 30, 1, 1);
            dev.GetBackBufferData(&reg, &px, 0, 1);
            check(built && IsRed(px),
                  "S4 custom Effect destroyed before flush: sprite still tinted RED");
        }

        // --- S5: Effect AND texture both destroyed before flush (Phase 19, multiple resources). -
        {
            std::string vertSpv(reinterpret_cast<const char*>(kTintVertSpv), kTintVertSpv_size);
            std::string fragSpv(reinterpret_cast<const char*>(kTintFragSpv), kTintFragSpv_size);
            auto fx = std::make_unique<ShaderEffect>(dev, vertSpv, fragSpv);
            auto srcTex = makeColorTex(255, 255, 255);      // white, tinted to red by the effect
            dev.Clear(Color::Black);
            dev.SetDepthTestEnabled(false);
            {
                fx->SetUniformVec4("uColor", 1.0f, 0.0f, 0.0f, 1.0f);
                fx->Apply();
                SpriteBatch sb(dev);
                sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr, fx.get());
                sb.Draw(*srcTex, Rectangle(20, 15, 50, 40), Rectangle(0, 0, 1, 1), Color::White);
                sb.End();
            }
            fx.reset();
            srcTex.reset();                                 // BOTH gone before the flush
            Color px(0, 0, 0, 0);
            Rectangle reg(40, 30, 1, 1);
            dev.GetBackBufferData(&reg, &px, 0, 1);
            check(IsRed(px), "S5 effect + texture both destroyed before flush: still RED");
        }

        // --- S6: address-reuse stress (Phase 20). ---------------------------------------------
        // Destroy the queued texture, allocate a differently-coloured one (allocator may reuse the
        // address/handle), then flush. The deferred draw must render the ORIGINAL colour, proving it
        // holds the retired resource, not a stale pointer aliasing the new one.
        {
            auto rt = makeRt();
            auto texA = makeColorTex(255, 0, 0);            // red
            dev.SetRenderTarget(rt.get());
            dev.Clear(Color::Black);
            drawSpriteTex(*texA, Rectangle(0, 0, kW, kH), Color::White);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            texA.reset();
            auto texB = makeColorTex(0, 255, 0);            // green, may reuse texA's slot
            drawSpriteTex(*texB, Rectangle(0, 0, 1, 1), Color::White);  // unrelated backbuffer draw
            const std::vector<Color> pix = readRt(*rt);
            check(countIf(pix, IsRed) >= kW * kH / 2 && countIf(pix, IsGreen) == 0,
                  "S6 address reuse: RT shows destroyed texA's RED, never texB's green");
            texB.reset();
        }

        // --- S7: TextureCube sampled by EnvironmentMapEffect, destroyed before flush (Phase 5). -
        // Memory-safety focus: the env-map draw bakes the cube's VkImageView into a descriptor set;
        // destroying the cube must not dangle it. amount=1 so the shader genuinely samples the cube
        // (ASan + validation catch a freed cube view even if the exact colour is effect-dependent).
        {
            auto rt = makeRt();
            auto cube = std::make_unique<TextureCube>(dev, 1, false, SurfaceFormat::Color);
            const Color faceColor(0, 0, 255, 255);
            const CubeMapFace faces[6] = {
                CubeMapFace::PositiveX, CubeMapFace::NegativeX,
                CubeMapFace::PositiveY, CubeMapFace::NegativeY,
                CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
            };
            for (CubeMapFace f : faces) cube->SetData(f, &faceColor, 1);

            dev.SetRenderTarget(rt.get());
            dev.Clear(Color::Black);
            dev.SetDepthTestEnabled(false);
            dev.setBlendStateProperty(BlendState::Opaque);
            {
                EnvironmentMapEffect fx(dev);
                fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
                fx.setEnvironmentMapAmountProperty(1.0f);
                fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
                fx.setTextureProperty(&whiteTex_);
                fx.setEnvironmentMapProperty(cube.get());
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                const Vector3 n(0.0f, 0.0f, 1.0f);
                const VertexPositionNormalTexture verts[6] = {
                    { Vector3(-1.f,  1.f, 0.f), n, Vector2(0.f, 1.f) },
                    { Vector3(-1.f, -1.f, 0.f), n, Vector2(0.f, 0.f) },
                    { Vector3( 1.f, -1.f, 0.f), n, Vector2(1.f, 0.f) },
                    { Vector3(-1.f,  1.f, 0.f), n, Vector2(0.f, 1.f) },
                    { Vector3( 1.f, -1.f, 0.f), n, Vector2(1.f, 0.f) },
                    { Vector3( 1.f,  1.f, 0.f), n, Vector2(1.f, 1.f) },
                };
                dev.setRasterizerStateProperty(RasterizerState::CullNone);
                fx.Apply();
                dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
            }
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            cube.reset();                                   // destroy the cube BEFORE the flush
            const std::vector<Color> pix = readRt(*rt);     // GPU samples the retired cube view
            check(!pix.empty(),
                  "S7 TextureCube destroyed before flush: env-map draw recorded without UAF");
        }

        // --- S8: OcclusionQuery DESTROYED before Present (Phase 8). ----------------------------
        // The deferred 3D draw stores a raw VulkanOcclusionQueryBackend* (read at record time).
        // Destroying the query before Present must detach it (draw preserved) without dereferencing
        // freed memory. Result is intentionally unobservable after disposal; safety is the contract.
        {
            auto query = std::make_unique<OcclusionQuery>(dev);
            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            dev.Clear(Color::Black);
            dev.SetDepthTestEnabled(true);
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            fx.Apply();
            query->Begin();
            const Color g(0, 255, 0, 255);
            const VertexPositionColor quad[6] = {
                { Vector3(-1.f,  1.f, 0.5f), g }, { Vector3(-1.f, -1.f, 0.5f), g },
                { Vector3( 1.f, -1.f, 0.5f), g }, { Vector3(-1.f,  1.f, 0.5f), g },
                { Vector3( 1.f, -1.f, 0.5f), g }, { Vector3( 1.f,  1.f, 0.5f), g },
            };
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            query->End();
            query.reset();                                  // destroy BEFORE Present records it
            dev.Clear(Color::CornflowerBlue);
            dev.Present();                                      // must not touch the freed query
            check(true, "S8 OcclusionQuery destroyed before Present did not crash");
        }

        std::printf("=== %d/%d PASS ===\n", pass_, total_);
        result_ = (pass_ == total_) ? 0 : 1;
        Exit();
    }

public:
    DeferredResourceLifetimeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(96);
        gdm_->setPreferredBackBufferHeightProperty(72);
    }

    int getResult() const { return result_; }
};

int main()
{
    DeferredResourceLifetimeTest game;
    game.Run();
    return game.getResult();
}

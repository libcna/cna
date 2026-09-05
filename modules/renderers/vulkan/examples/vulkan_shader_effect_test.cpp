// SPDX-License-Identifier: MS-PL
// Task 119: Vulkan integration test — ShaderEffect with pre-compiled SPIR-V.
//
// Renders a white 1×1 texture through a tint ShaderEffect that multiplies the
// texture colour by pc.uColor (a vec4 push constant).  The tint is set to red
// (1,0,0,1), so the output should be red.  The background is green.
//
// Push-constant contract (128 bytes, vert+frag stages, GLSL std140 alignment):
//   [0..7]   = vec2 vpSize  — set automatically by sprite batch
//   [8..15]  = padding      — GLSL aligns mat4 to 16 bytes
//   [16..79] = mat4 uMatrix — unused here (identity implicit)
//   [80..95] = vec4 uColor  — red tint (1,0,0,1)
//
// SPIR-V embedded below was compiled from:
//   vert: NDC-map from pixel coords, pass-through UV and colour
//   frag: texture * vColor * pc.uColor
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/NotSupportedException.hpp"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

// ---------------------------------------------------------------------------
// Pre-compiled SPIR-V for the tint shaders (NDC pixel-coord vertex + tint frag).
// Push-constant layout matches VulkanEffectRenderer contract:
//   vec2 vpSize | mat4 uMatrix | vec4 uColor | float uFloat0
// ---------------------------------------------------------------------------

// tint_test.vert — compiled with glslc from Android NDK 30
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

// tint_test.frag — compiled with glslc from Android NDK 30
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

class VulkanShaderEffectTest : public Game
{
    std::unique_ptr<SpriteBatch> sb_;
    Texture2D                    tex_;
    bool                         done_   = false;
    int                          result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(device);

        // 1×1 white texture — the tint effect will colourise it.
        const std::vector<uint8_t> white = { 255, 255, 255, 255 };
        tex_ = Texture2D::CreateFromPixels(device, 1, 1, white);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(Color(0, 255, 0, 255)); // green background
        device.SetDepthTestEnabled(false);

        // Build ShaderEffect from pre-compiled SPIR-V bytes.
        std::string vertSpv(reinterpret_cast<const char*>(kTintVertSpv), kTintVertSpv_size);
        std::string fragSpv(reinterpret_cast<const char*>(kTintFragSpv), kTintFragSpv_size);
        ShaderEffect fx(device, vertSpv, fragSpv);

        if (!fx.IsEffectValid())
        {
            std::printf("[FAIL] VulkanShaderEffect: ShaderEffect compile failed\n");
            Exit();
            return;
        }

        // Set red tint via SetUniformVec4 → pushConst_[20..23] (byte offset 80).
        fx.SetUniformVec4("uColor", 1.0f, 0.0f, 0.0f, 1.0f);
        fx.Apply();

        sb_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend,
                   nullptr, nullptr, nullptr, &fx);
        sb_->Draw(tex_,
                  Rectangle(W / 4, H / 4, W / 2, H / 2),
                  Rectangle(0, 0, 1, 1),
                  Color::White);
        sb_->End();

        // Centre pixel should be red (white texture × red tint), corner green (background).
        const Rectangle centReg(W / 2, H / 2, 1, 1);
        const Rectangle bgReg(1, 1, 1, 1);
        Color centPx(0, 0, 0, 0), bgPx(0, 0, 0, 0);
        device.GetBackBufferData(&centReg, &centPx, 0, 1);
        device.GetBackBufferData(&bgReg,   &bgPx,   0, 1);

        const bool centOk = (centPx.getRProperty() >= 200 && centPx.getGProperty() <= 50);
        const bool bgOk   = (bgPx.getGProperty()   >= 200 && bgPx.getRProperty()   <= 50);

        // plan_vulkan.md VULKAN-265: the four array uniform setters must REFUSE, not fall
        // silent. The centre-pixel check above is this leg's control: it proves the very same
        // `fx` still reaches the shader through SetUniformVec4, so four throws cannot be
        // explained away by an effect that is broken end to end.
        const float payload[16] = {};
        bool refusalsOk = true;
        auto expectRefusal = [&refusalsOk](const char* setter, auto&& call) {
            try {
                call();
                std::printf("[FAIL] VulkanShaderEffect: %s was accepted and silently ignored\n",
                            setter);
                refusalsOk = false;
            } catch (const System::NotSupportedException& e) {
                std::printf("[ok]   %s refused: %s\n", setter, e.what());
            } catch (...) {
                std::printf("[FAIL] VulkanShaderEffect: %s threw the wrong exception type\n",
                            setter);
                refusalsOk = false;
            }
        };
        expectRefusal("SetUniformFloatArray",
                      [&] { fx.SetUniformFloatArray("uWeights", payload, 4); });
        expectRefusal("SetUniformVec2Array",
                      [&] { fx.SetUniformVec2Array("uOffsets", payload, 2); });
        expectRefusal("SetUniformVec3Array",
                      [&] { fx.SetUniformVec3Array("uLightDirs", payload, 2); });
        expectRefusal("SetUniformMat4Array",
                      [&] { fx.SetUniformMat4Array("uBones", payload, 1); });

        if (centOk && bgOk && refusalsOk)
        {
            std::printf("[PASS] VulkanShaderEffect: centre=(%d,%d,%d) bg=(%d,%d,%d)\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty(),
                        bgPx.getRProperty(),   bgPx.getGProperty(),   bgPx.getBProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] VulkanShaderEffect: centre=(%d,%d,%d) bg=(%d,%d,%d)"
                        " refusals=%s\n"
                        "       expected: centre=red, bg=green, all four array setters refused\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty(),
                        bgPx.getRProperty(),   bgPx.getGProperty(),   bgPx.getBProperty(),
                        refusalsOk ? "ok" : "FAILED");
        }
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    VulkanShaderEffectTest game;
    game.Run();
    return game.getResult();
}

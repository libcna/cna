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
// The checked-in SPIR-V package was reproducibly compiled offline from:
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
#include "CNA/Graphics/ShaderPackageEXT.hpp"
#include "common/PortableTintShaderPackage.generated.hpp"
#include "System/NotSupportedException.hpp"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Graphics::ShaderCodeEXT;
using CNA::Graphics::ShaderPackageEXT;
namespace PortableTint = CNA::Examples::PortableTint;

template <std::size_t N>
static std::vector<std::uint8_t> ShaderBytes(const std::uint32_t (&words)[N])
{
    const auto* begin = reinterpret_cast<const std::uint8_t*>(words);
    return std::vector<std::uint8_t>(begin, begin + sizeof(words));
}

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

        bool portableOverloadsOk = false;
        std::unique_ptr<ShaderEffect> ownedEffect;
        {
            ShaderCodeEXT vertexCode(
                CNA::ShaderLanguageEXT::SpirV, CNA::ShaderStageEXT::Vertex,
                "main", "portable_tint/vulkan.vert.glsl",
                ShaderBytes(PortableTint::kVulkanVertexSpirV));
            ShaderCodeEXT fragmentCode(
                CNA::ShaderLanguageEXT::SpirV, CNA::ShaderStageEXT::Fragment,
                "main", "portable_tint/vulkan.frag.glsl",
                ShaderBytes(PortableTint::kVulkanFragmentSpirV));

            ShaderEffect direct(device, vertexCode, fragmentCode);
            const bool directOk = direct.IsEffectValid()
                && direct.GetSelectedShaderLanguageEXT() == CNA::ShaderLanguageEXT::SpirV
                && direct.GetVertexSource().size() == PortableTint::kVulkanVertexSpirVByteSize
                && direct.GetFragmentSource().size() == PortableTint::kVulkanFragmentSpirVByteSize;

            ShaderPackageEXT package(
                {ShaderCodeEXT(
                     CNA::ShaderLanguageEXT::Hlsl, CNA::ShaderStageEXT::Vertex,
                     "VSMain", "tint.hlsl", "hlsl vertex"),
                 ShaderCodeEXT(
                     CNA::ShaderLanguageEXT::Hlsl, CNA::ShaderStageEXT::Fragment,
                     "PSMain", "tint.hlsl", "hlsl fragment"),
                 vertexCode, fragmentCode},
                {CNA::ShaderStageEXT::Vertex, CNA::ShaderStageEXT::Fragment});
            ownedEffect = std::make_unique<ShaderEffect>(device, package);
            portableOverloadsOk = directOk && ownedEffect->IsEffectValid()
                && ownedEffect->GetSelectedShaderLanguageEXT()
                    == CNA::ShaderLanguageEXT::SpirV;
        }
        ShaderEffect& fx = *ownedEffect;
        std::unique_ptr<Effect> clonedEffect(fx.Clone());
        auto* clonedShaderEffect = dynamic_cast<ShaderEffect*>(clonedEffect.get());
        portableOverloadsOk = portableOverloadsOk
            && fx.GetSelectedShaderLanguageEXT() == CNA::ShaderLanguageEXT::SpirV
            && fx.GetVertexSource().size() == PortableTint::kVulkanVertexSpirVByteSize
            && fx.GetFragmentSource().size() == PortableTint::kVulkanFragmentSpirVByteSize
            && clonedShaderEffect != nullptr && clonedShaderEffect->IsEffectValid()
            && clonedShaderEffect->GetSelectedShaderLanguageEXT()
                == CNA::ShaderLanguageEXT::SpirV;
        std::printf("[%s] MOD-2214: direct code and package overloads retain selected SPIR-V\n",
                    portableOverloadsOk ? "ok" : "FAIL");

        if (!fx.IsEffectValid())
        {
            std::printf("[FAIL] VulkanShaderEffect: ShaderEffect compile failed\n");
            Exit();
            return;
        }

        // plan_vulkan.md VULKAN-251: TWO batches with DIFFERENT uniform values, into two regions.
        //
        // One value proves less than it looks. A shader that ignored `uColor` entirely would draw
        // the white texture and fail the old single check -- but a uniform delivered to the wrong
        // push-constant slot, or with only one channel arriving, can still land on "reddish". Two
        // distinct values make the drawn colour a FUNCTION of the uniform: both regions come out
        // identical unless the value is really being carried through.
        //
        // Sound because each batch snapshots the effect's push constants at End()
        // (`VulkanRenderer.cpp:1355`), so the two do not collapse onto the last value the way
        // F-12 predicted for buffers -- checked before relying on it.
        const auto drawTinted = [&](float r, float g, float b, const Rectangle& dest) {
            fx.SetUniformVec4("uColor", r, g, b, 1.0f);
            fx.Apply();
            sb_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend,
                       nullptr, nullptr, nullptr, &fx);
            sb_->Draw(tex_, dest, Rectangle(0, 0, 1, 1), Color::White);
            sb_->End();
        };
        drawTinted(1.0f, 0.0f, 0.0f, Rectangle(W / 8,     H / 4, W / 4, H / 2));  // left: red
        drawTinted(0.0f, 0.0f, 1.0f, Rectangle(W * 5 / 8, H / 4, W / 4, H / 2));  // right: blue

        const Rectangle leftReg(W / 4,     H / 2, 1, 1);
        const Rectangle rightReg(W * 3 / 4, H / 2, 1, 1);
        const Rectangle bgReg(1, 1, 1, 1);
        Color leftPx(0, 0, 0, 0), rightPx(0, 0, 0, 0), bgPx(0, 0, 0, 0);
        device.GetBackBufferData(&leftReg,  &leftPx,  0, 1);
        device.GetBackBufferData(&rightReg, &rightPx, 0, 1);
        device.GetBackBufferData(&bgReg,    &bgPx,    0, 1);

        const bool leftOk  = (leftPx.getRProperty()  >= 200 && leftPx.getBProperty()  <= 60);
        const bool rightOk = (rightPx.getBProperty() >= 200 && rightPx.getRProperty() <= 60);
        const bool centOk  = leftOk && rightOk;
        const bool bgOk    = (bgPx.getGProperty() >= 200 && bgPx.getRProperty() <= 50);
        std::printf("[%s] VULKAN-251: two uniform values give two colours -- left=(%d,%d,%d) "
                    "expected red, right=(%d,%d,%d) expected blue\n",
                    centOk ? "ok" : "FAIL",
                    leftPx.getRProperty(), leftPx.getGProperty(), leftPx.getBProperty(),
                    rightPx.getRProperty(), rightPx.getGProperty(), rightPx.getBProperty());

        // Kept for the refusal legs below, which need one live effect that demonstrably works.
        const Color centPx = leftPx;

        // plan_vulkan.md VULKAN-252: the four array uniform setters used to REFUSE here
        // (`VULKAN-265`), because a fixed 128-byte push-constant block has nowhere to put an
        // array. They accept now, through set 1's uniform-buffer ranges. What must never come back
        // is silence, so this leg pins both ends of the new contract: a call within capacity
        // succeeds, and one past it is refused BY NAME rather than truncated. The pixels an array
        // actually produces are `Vulkan_ShaderEffect_UniformArrays`' job, not this file's.
        // The centre-pixel check above is still this leg's control: it proves the same `fx`
        // reaches the shader, so an acceptance cannot be explained by an inert effect.
        const float payload[16] = {};
        bool arraysOk = true;
        auto expectAccepted = [&arraysOk](const char* setter, auto&& call) {
            try {
                call();
                std::printf("[ok]   %s accepted\n", setter);
            } catch (const std::exception& e) {
                std::printf("[FAIL] VulkanShaderEffect: %s was refused: %s\n", setter, e.what());
                arraysOk = false;
            }
        };
        expectAccepted("SetUniformFloatArray",
                       [&] { fx.SetUniformFloatArray("uWeights", payload, 4); });
        expectAccepted("SetUniformVec2Array",
                       [&] { fx.SetUniformVec2Array("uOffsets", payload, 2); });
        expectAccepted("SetUniformVec3Array",
                       [&] { fx.SetUniformVec3Array("uLightDirs", payload, 2); });
        expectAccepted("SetUniformMat4Array",
                       [&] { fx.SetUniformMat4Array("uBones", payload, 1); });
        {
            // Past the block's capacity. A renderer that quietly wrote 72 of the 1000 would pass
            // every leg above and corrupt whatever followed the array.
            bool refused = false;
            std::string what;
            try { fx.SetUniformMat4Array("uBones", payload, 1000); }
            catch (const System::NotSupportedException& e) { refused = true; what = e.what(); }
            catch (...) { what = "the wrong exception type"; }
            const bool namesIt = refused && what.find("1000") != std::string::npos;
            std::printf("[%s]   an array past the capacity is refused by name: %s\n",
                        namesIt ? "ok" : "FAIL", refused ? what.c_str() : "NOT REFUSED");
            if (!namesIt) arraysOk = false;
        }

        if (centOk && bgOk && arraysOk && portableOverloadsOk)
        {
            std::printf("[PASS] VulkanShaderEffect: centre=(%d,%d,%d) bg=(%d,%d,%d)\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty(),
                        bgPx.getRProperty(),   bgPx.getGProperty(),   bgPx.getBProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] VulkanShaderEffect: centre=(%d,%d,%d) bg=(%d,%d,%d)"
                        " arrays=%s portableOverloads=%s\n"
                        "       expected: centre=red, bg=green, all four array setters accepted "
                        "and an over-capacity one refused\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty(),
                        bgPx.getRProperty(),   bgPx.getGProperty(),   bgPx.getBProperty(),
                        arraysOk ? "ok" : "FAILED",
                        portableOverloadsOk ? "ok" : "FAILED");
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

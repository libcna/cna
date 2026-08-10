// SPDX-License-Identifier: MS-PL
// Task 947 (Phase 78 rollout): HLSL->GLSL shader-conversion proof -- Clouds.fx (EasyGL renderer).
// Blocks NetRumble (DEFERRED.md item #11) -- NetRumble's own missing.md documents 4 blocking
// shaders total: the 3-shader bloom trio (already proven, Task 946) plus this one, Clouds.fx.
// With this shader proven too, NetRumble's shader blocker is now fully cleared.
//
// FNA reference (`NetRumble_4_0/NetRumble/Content/Effects/Clouds.fx`):
//   float2 Position;
//   sampler TextureSampler : register(s0) = sampler_state { AddressU = Wrap; AddressV = Wrap; };
//   float4 PixelShaderFunction(float2 texCoord : TEXCOORD0) : COLOR0
//   {
//       texCoord.x += Position.x * 0.00025f;
//       texCoord.y += Position.y * 0.00025f;
//       texCoord *= 0.5f;
//       float4 results = float4(0,0,1,0.25) * tex2D(TextureSampler, texCoord);
//
//       texCoord.x += Position.x * 0.00025f + 0.25f;
//       texCoord.y += Position.y * 0.00025f - 0.15;
//       texCoord *= 0.4f;
//       results += float4(0,1,0,0.15) * tex2D(TextureSampler, texCoord);
//
//       return results;
//   }
//
// Ported 1:1 below -- note the SECOND `texCoord.x +=`/`texCoord.y +=` block mutates the ALREADY
// scaled/offset `texCoord` left over from the first sample (HLSL does not reset it), not the
// original input `TEXCOORD0` -- preserved exactly, not "fixed" into two independent samples.
// float4*float4 component-wise multiply means the first term keeps ONLY the texture's own Blue
// channel (mask (0,0,1,0.25)), the second term keeps ONLY Green (mask (0,1,0,0.15)) -- Red is
// always 0 in the result, and the two samples' alpha contributions sum (0.25a1 + 0.15a2).
//
// Test simplification (documented per this project's own deviation-disclosure convention): the
// real shader only overrides sampler ADDRESSING (Wrap), leaving XNA/FNA's default LINEAR filter
// active, so real gameplay gets a smoothly blended parallax cloud look. This test instead forces
// SamplerState::PointWrap (matches the Wrap addressing, swaps Linear for Point filtering) and
// samples deep inside 2 solid texel blocks of a 2-texel texture, comfortably clear of any texel
// boundary -- isolating the formula (channel masking + Position wiring) from bilinear-blend
// arithmetic, which is not what this shader-conversion proof is testing.
//
// Check A -- Position=(0,0): both internal samples land in texel0 (solid (0,60,180,255)) ->
//            R=0 (always), G=texel0.g=60 (from 2nd sample), B=texel0.b=180 (from 1st sample),
//            A=0.25*255+0.15*255=0.4*255~=102.
// Check B -- Position=(4000,0): both internal samples land in texel1 (solid (0,220,20,255)) ->
//            R=0, G=220, B=20, A~=102. Proves the `Position` uniform genuinely reaches the
//            shader and shifts sampling, not just that the formula is well-formed at Position=0.
//
// Exit code 0 = both PASS, 1 = either FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    void WriteFile(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream f(path, std::ios::binary);
        f << text;
    }

    const char* kVertSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
out vec2 TexCoord;
uniform mat4 projection;
void main() {
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";

    // Ported 1:1 from Clouds.fx's PixelShaderFunction (see this file's own header comment for
    // the sequential-texCoord-mutation nuance preserved here).
    const char* kFragSrc = R"(#version 300 es
precision mediump float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform vec2 uPosition;
void main() {
    vec2 texCoord = TexCoord;

    texCoord.x += uPosition.x * 0.00025;
    texCoord.y += uPosition.y * 0.00025;
    texCoord *= 0.5;
    vec4 results = vec4(0.0, 0.0, 1.0, 0.25) * texture(texture1, texCoord);

    texCoord.x += uPosition.x * 0.00025 + 0.25;
    texCoord.y += uPosition.y * 0.00025 - 0.15;
    texCoord *= 0.4;
    results += vec4(0.0, 1.0, 0.0, 0.15) * texture(texture1, texCoord);

    FragColor = results;
}
)";
}

class EasyGLCloudsShaderTest : public Game
{
    std::shared_ptr<Effect> fxBase_;
    Texture2D cloudTex_;
    bool done_   = false;
    int result_  = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        const auto root = std::filesystem::temp_directory_path()
            / ("cna_clouds_shader_test_" + std::to_string(
                   reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        WriteFile(root / "clouds.vert.glsl", kVertSrc);
        WriteFile(root / "clouds.frag.glsl", kFragSrc);
        WriteFile(root / "clouds.cnj", R"({
  "cnjVersion": 1,
  "type": "Effect",
  "vertex": "clouds.vert.glsl",
  "fragment": "clouds.frag.glsl"
})");

        getContentProperty().setRootDirectoryProperty(root.string());
        fxBase_ = getContentProperty().Load<std::shared_ptr<Effect>>("clouds");

        // 2x1 texel texture: texel0=(0,60,180,255), texel1=(0,220,20,255). Solid per-texel
        // blocks (not a gradient) so PointWrap sampling deep inside either half is unambiguous.
        const std::vector<uint8_t> pixels = {
            0, 60, 180, 255,
            0, 220, 20, 255,
        };
        cloudTex_ = Texture2D::CreateFromPixels(device, 2, 1, pixels);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        auto* fx = dynamic_cast<ShaderEffect*>(fxBase_.get());
        if (!fx || !fx->IsEffectValid())
        {
            std::printf("[FAIL] EasyGLCloudsShader: .cnj load or GLSL compile failed\n");
            Exit();
            return;
        }

        device.Clear(Color(0, 0, 0, 255));
        device.SetDepthTestEnabled(false);

        SpriteBatch sb(device);
        SamplerState pointWrap = SamplerState::PointWrap;

        // Check A: Position=(0,0).
        fx->Apply();
        fx->SetUniformVec2("uPosition", 0.0f, 0.0f);
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointWrap,
                 nullptr, nullptr, fx);
        sb.Draw(cloudTex_, Rectangle(0, 0, W, H), Color::White);
        sb.End();

        const Rectangle centreReg(W / 2, H / 2, 1, 1);
        Color a(0, 0, 0, 0);
        device.GetBackBufferData(&centreReg, &a, 0, 1);

        // Check B: Position=(4000,0).
        fx->Apply();
        fx->SetUniformVec2("uPosition", 4000.0f, 0.0f);
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointWrap,
                 nullptr, nullptr, fx);
        sb.Draw(cloudTex_, Rectangle(0, 0, W, H), Color::White);
        sb.End();

        Color b(0, 0, 0, 0);
        device.GetBackBufferData(&centreReg, &b, 0, 1);

        const auto close = [](int got, int expected) { return got >= expected - 5 && got <= expected + 5; };
        const bool aOk = a.getRProperty() == 0 && close(a.getGProperty(), 60)
                      && close(a.getBProperty(), 180) && close(a.getAProperty(), 102);
        const bool bOk = b.getRProperty() == 0 && close(b.getGProperty(), 220)
                      && close(b.getBProperty(), 20) && close(b.getAProperty(), 102);

        std::printf("[%s] Check A (Position=0,0): (%d,%d,%d,%d) expected~=(0,60,180,102)\n",
                    aOk ? "PASS" : "FAIL",
                    a.getRProperty(), a.getGProperty(), a.getBProperty(), a.getAProperty());
        std::printf("[%s] Check B (Position=4000,0): (%d,%d,%d,%d) expected~=(0,220,20,102)\n",
                    bOk ? "PASS" : "FAIL",
                    b.getRProperty(), b.getGProperty(), b.getBProperty(), b.getAProperty());

        result_ = (aOk && bOk) ? 0 : 1;
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    EasyGLCloudsShaderTest game;
    game.Run();
    return game.getResult();
}

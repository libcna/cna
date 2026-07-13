// SPDX-License-Identifier: MS-PL
// Task 946: HLSL→GLSL shader-conversion proof — BloomCombine.fx (EasyGL backend).
//
// FNA reference (`BloomSample_4_0/BloomPostprocess/Content/BloomCombine.fx`):
//   sampler BloomSampler : register(s0);
//   sampler BaseSampler  : register(s1);
//   float BloomIntensity, BaseIntensity, BloomSaturation, BaseSaturation;
//   float4 AdjustSaturation(float4 color, float saturation)
//   {
//       float grey = dot(color, float3(0.3, 0.59, 0.11));  // HLSL truncates color to .rgb here
//       return lerp(grey, color, saturation);               // scalar->vec4 broadcast
//   }
//   float4 PixelShaderFunction(float2 texCoord : TEXCOORD0) : COLOR0
//   {
//       float4 bloom = tex2D(BloomSampler, texCoord);
//       float4 base  = tex2D(BaseSampler, texCoord);
//       bloom = AdjustSaturation(bloom, BloomSaturation) * BloomIntensity;
//       base  = AdjustSaturation(base, BaseSaturation) * BaseIntensity;
//       base *= (1 - saturate(bloom));
//       return base + bloom;
//   }
//
// This is the shader that most exercises the 2nd-texture-unit binding capability this task added
// to ShaderEffect (SetTexture) -- the first custom shader in this codebase to sample two
// completely independent textures in one draw. Ported 1:1 to GLSL: HLSL's `dot(color, float3(...))`
// (a float4 implicitly truncated to float3 by the HLSL compiler) becomes an explicit `color.rgb`
// swizzle in GLSL (GLSL never truncates types implicitly); `lerp(grey, color, saturation)`'s
// scalar-to-vec4 broadcast of `grey` becomes an explicit `vec4(grey)` (GLSL's `mix()` requires
// matching operand types, unlike HLSL's `lerp()`).
//
// Method: BloomSaturation=BaseSaturation=1 (AdjustSaturation is then the identity — mix(x,y,1)=y —
// isolating the intensity/combine math from the saturation math, which is orthogonal and not
// exercised by this test), BloomIntensity=BaseIntensity=1. bloom texture = pure blue (0,0,1,1),
// base texture = pure red (1,0,0,1). Hand-computed expected output:
//   bloom' = (0,0,1,1) * 1 = (0,0,1,1)
//   base'  = (1,0,0,1) * 1 = (1,0,0,1)
//   base'' = base' * (1 - saturate(bloom')) = (1,0,0,1) * (1,1,0,0) = (1,0,0,0)
//   result = base'' + bloom' = (1,0,0,0) + (0,0,1,1) = (1,0,1,1)  -> magenta
// If the 2nd texture unit were never actually bound (the bug this task's ShaderEffect extension
// fixes), `base` would sample as black (0,0,0,0) instead of red, giving base''=(0,0,0,0) and a
// result of pure blue (0,0,1,1) instead of magenta -- a sharp, unambiguous distinguishing signal.
//
// Exit code 0 = PASS (magenta), 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
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

    // Ported 1:1 from BloomCombine.fx's AdjustSaturation()/PixelShaderFunction().
    const char* kFragSrc = R"(#version 300 es
precision mediump float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;      // BloomSampler (unit 0)
uniform sampler2D uBaseSampler;  // BaseSampler  (unit 1)
uniform float uBloomIntensity;
uniform float uBaseIntensity;
uniform float uBloomSaturation;
uniform float uBaseSaturation;

vec4 AdjustSaturation(vec4 color, float saturation) {
    float grey = dot(color.rgb, vec3(0.3, 0.59, 0.11));
    return mix(vec4(grey), color, saturation);
}

void main() {
    vec4 bloom = texture(texture1, TexCoord);
    vec4 base  = texture(uBaseSampler, TexCoord);
    bloom = AdjustSaturation(bloom, uBloomSaturation) * uBloomIntensity;
    base  = AdjustSaturation(base, uBaseSaturation) * uBaseIntensity;
    base *= (1.0 - clamp(bloom, 0.0, 1.0));
    FragColor = base + bloom;
}
)";
}

class EasyGLBloomCombineTest : public Game
{
    Texture2D bloomTex_;
    Texture2D baseTex_;
    bool done_   = false;
    int result_  = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        const std::vector<uint8_t> blue = { 0, 0, 255, 255 };
        bloomTex_ = Texture2D::CreateFromPixels(device, 1, 1, blue);
        const std::vector<uint8_t> red = { 255, 0, 0, 255 };
        baseTex_ = Texture2D::CreateFromPixels(device, 1, 1, red);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        ShaderEffect fx(device, kVertSrc, kFragSrc);
        if (!fx.IsEffectValid())
        {
            std::printf("[FAIL] EasyGLBloomCombine: GLSL compile error\n");
            Exit();
            return;
        }

        device.Clear(Color(0, 255, 0, 255));
        device.SetDepthTestEnabled(false);

        fx.Apply();
        fx.SetUniformInt("uBaseSampler", 1);
        fx.SetTexture(1, baseTex_);
        fx.SetUniformFloat("uBloomIntensity", 1.0f);
        fx.SetUniformFloat("uBaseIntensity", 1.0f);
        fx.SetUniformFloat("uBloomSaturation", 1.0f);
        fx.SetUniformFloat("uBaseSaturation", 1.0f);

        SpriteBatch sb(device);
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr, &fx);
        sb.Draw(bloomTex_, Rectangle(W / 4, H / 4, W / 2, H / 2), Color::White);
        sb.End();

        const Rectangle centReg(W / 2, H / 2, 1, 1);
        Color centPx(0, 0, 0, 0);
        device.GetBackBufferData(&centReg, &centPx, 0, 1);

        const bool ok = centPx.getRProperty() >= 200 && centPx.getGProperty() <= 30
                     && centPx.getBProperty() >= 200;

        if (ok)
        {
            std::printf("[PASS] EasyGLBloomCombine: centre=(%d,%d,%d,%d)\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty(), centPx.getAProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] EasyGLBloomCombine: centre=(%d,%d,%d,%d)\n"
                        "       expected: magenta (R>=200,G<=30,B>=200) -- red base + blue bloom\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty(), centPx.getAProperty());
        }
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    EasyGLBloomCombineTest game;
    game.Run();
    return game.getResult();
}

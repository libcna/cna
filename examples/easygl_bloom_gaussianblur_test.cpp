// SPDX-License-Identifier: MS-PL
// Task 946: HLSL→GLSL shader-conversion proof — GaussianBlur.fx (EasyGL backend).
//
// FNA reference (`BloomSample_4_0/BloomPostprocess/Content/GaussianBlur.fx`):
//   sampler TextureSampler : register(s0);
//   #define SAMPLE_COUNT 15
//   float2 SampleOffsets[SAMPLE_COUNT];
//   float SampleWeights[SAMPLE_COUNT];
//   float4 PixelShaderFunction(float2 texCoord : TEXCOORD0) : COLOR0
//   {
//       float4 c = 0;
//       for (int i = 0; i < SAMPLE_COUNT; i++)
//           c += tex2D(TextureSampler, texCoord + SampleOffsets[i]) * SampleWeights[i];
//       return c;
//   }
//
// This is the shader that most exercises the array-uniform capability this task added to
// ShaderEffect (SetUniformVec2Array/SetUniformFloatArray) -- ported 1:1 to GLSL below (GLSL ES
// 3.00 supports plain uniform arrays natively, so the loop translates directly, no restructuring
// needed). The SampleOffsets/SampleWeights *computation* (BloomComponent.cs's own
// SetBlurEffectParameters()/ComputeGaussian()) is part of "porting the shader" too -- it lives on
// the C++ caller side here, not in the shader itself, exactly as in the real XNA sample.
//
// Method: a 1-texel-wide bright vertical line on an otherwise black 64×64 source, blurred
// HORIZONTALLY only (dx = 1/64, dy = 0), BlurAmount=4 (matches BloomSettings' own "Default"
// preset). With SAMPLE_COUNT=15, max sample offset = (i*2+1.5) for i=6 -> 13.5 texels -- since the
// shader only ever samples those exact 15 discrete offsets (not a continuous kernel), any pixel
// further than 13.5 texels from the source line receives EXACTLY zero contribution, giving an
// exact, not approximate, "must be pure black" assertion.
//
// Check A — on the line (x=16): dimmer than the original 255 (energy spread to neighbours) but
//           still clearly non-zero (weight[0] alone is a sizeable fraction of the total).
// Check B — 1 texel right of the line (x=17): was pure black pre-blur; must now read non-zero
//           (proves the array uniforms are actually reaching the shader and being sampled).
// Check C — far from the line (x=40, 24 texels away, beyond the 13.5-texel max reach): must
//           remain exactly pure black.
//
// Exit code 0 = all 3 PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cmath>
#include <cstdio>
#include <memory>
#include <numbers>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSampleCount = 15;
    constexpr int kSize = 64;

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

    // Ported 1:1 from GaussianBlur.fx's PixelShaderFunction (GLSL ES 3.00 supports plain
    // uniform arrays, so the loop needs no restructuring from the HLSL original).
    const char* kFragSrc = R"(#version 300 es
precision mediump float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform vec2 uSampleOffsets[15];
uniform float uSampleWeights[15];
void main() {
    vec4 c = vec4(0.0);
    for (int i = 0; i < 15; i++) {
        c += texture(texture1, TexCoord + uSampleOffsets[i]) * uSampleWeights[i];
    }
    FragColor = c;
}
)";

    // Ported 1:1 from BloomComponent.cs's ComputeGaussian().
    float ComputeGaussian(float n, float theta)
    {
        return static_cast<float>((1.0 / std::sqrt(2.0 * std::numbers::pi * theta))
                                   * std::exp(-(n * n) / (2.0 * theta * theta)));
    }

    // Ported 1:1 from BloomComponent.cs's SetBlurEffectParameters(dx, dy).
    void ComputeBlurParameters(float dx, float dy, float theta,
                                float (&weights)[kSampleCount], float (&offsets)[kSampleCount][2])
    {
        weights[0] = ComputeGaussian(0.0f, theta);
        offsets[0][0] = 0.0f;
        offsets[0][1] = 0.0f;

        float totalWeights = weights[0];

        for (int i = 0; i < kSampleCount / 2; ++i)
        {
            const float weight = ComputeGaussian(static_cast<float>(i + 1), theta);
            weights[i * 2 + 1] = weight;
            weights[i * 2 + 2] = weight;
            totalWeights += weight * 2.0f;

            const float sampleOffset = static_cast<float>(i) * 2.0f + 1.5f;
            const float deltaX = dx * sampleOffset;
            const float deltaY = dy * sampleOffset;

            offsets[i * 2 + 1][0] = deltaX;
            offsets[i * 2 + 1][1] = deltaY;
            offsets[i * 2 + 2][0] = -deltaX;
            offsets[i * 2 + 2][1] = -deltaY;
        }

        for (float& w : weights) w /= totalWeights;
    }
}

class EasyGLBloomGaussianBlurTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D whiteTex_;
    bool done_   = false;
    int result_  = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        const std::vector<uint8_t> white = { 255, 255, 255, 255 };
        whiteTex_ = Texture2D::CreateFromPixels(device, 1, 1, white);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        device.SetDepthTestEnabled(false);

        ShaderEffect fx(device, kVertSrc, kFragSrc);
        if (!fx.IsEffectValid())
        {
            std::printf("[FAIL] EasyGLBloomGaussianBlur: GLSL compile error\n");
            Exit();
            return;
        }

        RenderTarget2D sourceRt(device, kSize, kSize);
        RenderTarget2D destRt(device, kSize, kSize);

        // Source: black background, 1-texel-wide bright vertical line at x=16.
        device.SetRenderTarget(&sourceRt);
        device.Clear(Color::Black);
        {
            SpriteBatch sb(device);
            sb.Begin();
            sb.Draw(whiteTex_, Rectangle(16, 0, 1, kSize), Color::White);
            sb.End();
        }

        // Horizontal blur pass: dx = 1/width, dy = 0, BlurAmount=4 (BloomSettings "Default").
        float weights[kSampleCount];
        float offsets[kSampleCount][2];
        ComputeBlurParameters(1.0f / static_cast<float>(kSize), 0.0f, 4.0f, weights, offsets);
        float flatOffsets[kSampleCount * 2];
        for (int i = 0; i < kSampleCount; ++i)
        {
            flatOffsets[i * 2]     = offsets[i][0];
            flatOffsets[i * 2 + 1] = offsets[i][1];
        }

        device.SetRenderTarget(&destRt);
        device.Clear(Color::Black);

        fx.Apply();
        fx.SetUniformVec2Array("uSampleOffsets", flatOffsets, kSampleCount);
        fx.SetUniformFloatArray("uSampleWeights", weights, kSampleCount);

        SpriteBatch sb(device);
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr, &fx);
        sb.Draw(sourceRt, Rectangle(0, 0, kSize, kSize), Color::White);
        sb.End();

        // Read back while destRt is still the bound FBO.
        const Rectangle onLineReg(16, kSize / 2, 1, 1);
        const Rectangle adjacentReg(17, kSize / 2, 1, 1);
        const Rectangle farReg(40, kSize / 2, 1, 1);
        Color onLinePx(0, 0, 0, 0), adjacentPx(0, 0, 0, 0), farPx(0, 0, 0, 0);
        device.GetBackBufferData(&onLineReg,   &onLinePx,   0, 1);
        device.GetBackBufferData(&adjacentReg, &adjacentPx, 0, 1);
        device.GetBackBufferData(&farReg,      &farPx,      0, 1);

        device.SetRenderTarget(nullptr);

        const bool onLineOk   = onLinePx.getRProperty() > 20 && onLinePx.getRProperty() < 250;
        const bool adjacentOk = adjacentPx.getRProperty() > 0;
        const bool farOk      = farPx.getRProperty() == 0 && farPx.getGProperty() == 0 && farPx.getBProperty() == 0;

        if (onLineOk && adjacentOk && farOk)
        {
            std::printf("[PASS] EasyGLBloomGaussianBlur: onLine=%d adjacent=%d far=%d\n",
                        onLinePx.getRProperty(), adjacentPx.getRProperty(), farPx.getRProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] EasyGLBloomGaussianBlur: onLine=%d adjacent=%d far=%d\n"
                        "       expected: onLine in (20,250), adjacent > 0, far == 0\n",
                        onLinePx.getRProperty(), adjacentPx.getRProperty(), farPx.getRProperty());
        }
        Exit();
    }

public:
    EasyGLBloomGaussianBlurTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    EasyGLBloomGaussianBlurTest game;
    game.Run();
    return game.getResult();
}

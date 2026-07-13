// SPDX-License-Identifier: MS-PL
// Task 946: HLSL→GLSL shader-conversion proof — full 4-pass BloomSample pipeline (EasyGL
// backend), proving the 3 individually-ported shaders (BloomExtract/GaussianBlur/BloomCombine,
// see the 3 sibling `easygl_bloom_*_test.cpp` files) compose end-to-end via chained
// RenderTarget2D passes, mirroring BloomComponent.cs's own Draw() sequence exactly:
//
//   1. Extract:   scene (full-res)      -> renderTarget1 (half-res), BloomExtract
//   2. Blur H:    renderTarget1         -> renderTarget2 (half-res), GaussianBlur (dx, 0)
//   3. Blur V:    renderTarget2         -> renderTarget1 (half-res), GaussianBlur (0, dy)
//   4. Combine:   renderTarget1 + scene -> backbuffer,               BloomCombine
//
// Scene: a small bright white square on an otherwise black background (mimicking BloomSample's
// sunset+tank scene, minus the 3D geometry/model-loading machinery that's explicitly out of this
// task's scope -- see NEXT.md, Task 946 is scoped to the shader-conversion workflow itself, not a
// full sample port). BloomThreshold=0.25 lets the bright square straight through the extract pass;
// the background (black, well below threshold) is fully suppressed.
//
// Check A — a "halo" pixel just outside the original square's own bounds: pure black in the base
//           scene, so any non-black reading here can only be bloom spillover from the blur --
//           proves the full pipeline (all 3 shaders + the RenderTarget2D chaining) really
//           composes end-to-end.
// Check B — a pixel far from the square (well beyond the blur kernel's ~13-texel half-res reach,
//           which is ~26 full-res texels after the 2x upsample): must remain pure black.
// Check C — the square's own centre: still bright after the full combine (BloomCombine's
//           `base + bloom` never darkens fully-lit source pixels).
//
// Writing this test found and fixed a real, previously-unknown bug (Task 1078):
// EasyGLSpriteBatchBackend::FlushBatch() always sized its viewport/orthographic projection to
// the WINDOW (SDL_GetWindowSize), never to whatever RenderTarget2D is currently bound -- so a
// custom-effect draw into a half-res intermediate RT (exactly what this real 4-pass pipeline
// needs) rendered into the wrong sub-region instead of filling the RT. Every prior ShaderEffect/
// SpriteBatch pixel test in this codebase happened to use a RenderTarget2D the same size as the
// window, which silently masked this. Fixed by adding
// EasyGLGraphicsBackend::GetCurrentRenderTarget2DSize() and preferring it over the window size
// whenever a 2D render target is bound.
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
    constexpr int kSceneSize   = 128; // full-res scene / backbuffer
    constexpr int kBloomSize   = kSceneSize / 2; // half-res, matches BloomComponent.cs

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

    const char* kExtractFragSrc = R"(#version 300 es
precision mediump float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform float uBloomThreshold;
void main() {
    vec4 c = texture(texture1, TexCoord);
    FragColor = clamp((c - uBloomThreshold) / (1.0 - uBloomThreshold), 0.0, 1.0);
}
)";

    const char* kBlurFragSrc = R"(#version 300 es
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

    const char* kCombineFragSrc = R"(#version 300 es
precision mediump float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform sampler2D uBaseSampler;
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

    float ComputeGaussian(float n, float theta)
    {
        return static_cast<float>((1.0 / std::sqrt(2.0 * std::numbers::pi * theta))
                                   * std::exp(-(n * n) / (2.0 * theta * theta)));
    }

    void ComputeBlurParameters(float dx, float dy, float theta,
                                float (&weights)[kSampleCount], float (&flatOffsets)[kSampleCount * 2])
    {
        weights[0] = ComputeGaussian(0.0f, theta);
        flatOffsets[0] = 0.0f;
        flatOffsets[1] = 0.0f;

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

            flatOffsets[(i * 2 + 1) * 2]     = deltaX;
            flatOffsets[(i * 2 + 1) * 2 + 1] = deltaY;
            flatOffsets[(i * 2 + 2) * 2]     = -deltaX;
            flatOffsets[(i * 2 + 2) * 2 + 1] = -deltaY;
        }

        for (float& w : weights) w /= totalWeights;
    }
}

class EasyGLBloomPipelineTest : public Game
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

        ShaderEffect extractFx(device, kVertSrc, kExtractFragSrc);
        ShaderEffect blurFx(device, kVertSrc, kBlurFragSrc);
        ShaderEffect combineFx(device, kVertSrc, kCombineFragSrc);
        if (!extractFx.IsEffectValid() || !blurFx.IsEffectValid() || !combineFx.IsEffectValid())
        {
            std::printf("[FAIL] EasyGLBloomPipeline: GLSL compile error\n");
            Exit();
            return;
        }

        RenderTarget2D sceneRt(device, kSceneSize, kSceneSize);
        RenderTarget2D rt1(device, kBloomSize, kBloomSize);
        RenderTarget2D rt2(device, kBloomSize, kBloomSize);

        SpriteBatch sb(device);

        // Scene: a small bright white square in the middle of an otherwise black background.
        device.SetRenderTarget(&sceneRt);
        device.Clear(Color::Black);
        {
            sb.Begin();
            const int sq = kSceneSize / 8;
            sb.Draw(whiteTex_,
                    Rectangle(kSceneSize / 2 - sq / 2, kSceneSize / 2 - sq / 2, sq, sq),
                    Color::White);
            sb.End();
        }

        // Pass 1: Extract -- scene (full-res) -> rt1 (half-res).
        device.SetRenderTarget(&rt1);
        device.Clear(Color::Black);
        extractFx.Apply();
        extractFx.SetUniformFloat("uBloomThreshold", 0.25f);
        {
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr, &extractFx);
            sb.Draw(sceneRt, Rectangle(0, 0, kBloomSize, kBloomSize), Color::White);
            sb.End();
        }

        // Pass 2: Blur horizontally -- rt1 -> rt2.
        float weightsH[kSampleCount], offsetsH[kSampleCount * 2];
        ComputeBlurParameters(1.0f / static_cast<float>(kBloomSize), 0.0f, 4.0f, weightsH, offsetsH);
        device.SetRenderTarget(&rt2);
        device.Clear(Color::Black);
        blurFx.Apply();
        blurFx.SetUniformVec2Array("uSampleOffsets", offsetsH, kSampleCount);
        blurFx.SetUniformFloatArray("uSampleWeights", weightsH, kSampleCount);
        {
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr, &blurFx);
            sb.Draw(rt1, Rectangle(0, 0, kBloomSize, kBloomSize), Color::White);
            sb.End();
        }

        // Pass 3: Blur vertically -- rt2 -> rt1.
        float weightsV[kSampleCount], offsetsV[kSampleCount * 2];
        ComputeBlurParameters(0.0f, 1.0f / static_cast<float>(kBloomSize), 4.0f, weightsV, offsetsV);
        device.SetRenderTarget(&rt1);
        device.Clear(Color::Black);
        blurFx.Apply();
        blurFx.SetUniformVec2Array("uSampleOffsets", offsetsV, kSampleCount);
        blurFx.SetUniformFloatArray("uSampleWeights", weightsV, kSampleCount);
        {
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr, &blurFx);
            sb.Draw(rt2, Rectangle(0, 0, kBloomSize, kBloomSize), Color::White);
            sb.End();
        }

        // Pass 4: Combine -- rt1 (bloom, half-res, upscaled to full-res by the dest rect) +
        // sceneRt (base, full-res) -> backbuffer.
        device.SetRenderTarget(nullptr);
        device.Clear(Color::Black);
        combineFx.Apply();
        combineFx.SetUniformInt("uBaseSampler", 1);
        combineFx.SetTexture(1, sceneRt);
        combineFx.SetUniformFloat("uBloomIntensity", 1.25f);
        combineFx.SetUniformFloat("uBaseIntensity", 1.0f);
        combineFx.SetUniformFloat("uBloomSaturation", 1.0f);
        combineFx.SetUniformFloat("uBaseSaturation", 1.0f);
        {
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr, &combineFx);
            sb.Draw(rt1, Rectangle(0, 0, kSceneSize, kSceneSize), Color::White);
            sb.End();
        }

        const Rectangle centreReg(kSceneSize / 2, kSceneSize / 2, 1, 1);
        const Rectangle haloReg(kSceneSize / 2 + kSceneSize / 16 + 4, kSceneSize / 2, 1, 1);
        const Rectangle farReg(4, 4, 1, 1);
        Color centrePx(0, 0, 0, 0), haloPx(0, 0, 0, 0), farPx(0, 0, 0, 0);
        device.GetBackBufferData(&centreReg, &centrePx, 0, 1);
        device.GetBackBufferData(&haloReg,   &haloPx,   0, 1);
        device.GetBackBufferData(&farReg,    &farPx,    0, 1);

        const bool centreOk = centrePx.getRProperty() >= 200;
        const bool haloOk   = haloPx.getRProperty() > 0;
        const bool farOk    = farPx.getRProperty() == 0 && farPx.getGProperty() == 0 && farPx.getBProperty() == 0;

        if (centreOk && haloOk && farOk)
        {
            std::printf("[PASS] EasyGLBloomPipeline: centre=%d halo=%d far=%d\n",
                        centrePx.getRProperty(), haloPx.getRProperty(), farPx.getRProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] EasyGLBloomPipeline: centre=%d halo=%d far=%d\n"
                        "       expected: centre>=200, halo>0 (bloom spillover), far==0\n",
                        centrePx.getRProperty(), haloPx.getRProperty(), farPx.getRProperty());
        }
        Exit();
    }

public:
    EasyGLBloomPipelineTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSceneSize);
        gdm_->setPreferredBackBufferHeightProperty(kSceneSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    EasyGLBloomPipelineTest game;
    game.Run();
    return game.getResult();
}

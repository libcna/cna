// SPDX-License-Identifier: MS-PL
// Task 947 (Phase 78 rollout): HLSL->GLSL shader-conversion proof -- Distort.fx's `DistortBlur`
// technique (the `distortionBlur=true` branch of the same `Distort_PixelShader` whose
// `distortionBlur=false` branch was already ported as the `Distort` technique). With this,
// `DistortionSample`'s shader blocker (DEFERRED.md item #11) is now fully cleared -- both of the
// sample's 3 distorters that set `DistortionBlur=true` (`Game.cs:85,101`) can now be served.
//
// FNA reference (`DistortionSample_4_0/Distortion/Content/Distort.fx`, `Distort_PixelShader`
// with `distortionBlur=true`):
//   float2 displacement = tex2D(DistortionMap, TexCoord).rg;
//   if ((displacement.x == 0) && (displacement.y == 0))
//       finalColor = tex2D(SceneTexture, TexCoord);              // sentinel -- no blur at all
//   else
//   {
//       displacement -= .5 + ZeroOffset;
//       for (int i = 0; i < SAMPLE_COUNT; i++)                   // SAMPLE_COUNT = 15
//           finalColor += tex2D(SceneTexture, TexCoord.xy + displacement + SampleOffsets[i])
//                         * SampleWeights[i];
//   }
//
// Combines two capabilities this task's own sibling tests already proved independently: the
// sentinel/displacement branch (already ported as the `Distort` technique) and the 15-tap
// array-uniform weighted blur sum (Task 946's `GaussianBlur.fx` -- `ComputeGaussian`/
// `ComputeBlurParameters` reused verbatim from that test, ported once, not re-derived). This row
// verifies they compose correctly together, not just that each works in isolation.
//
// Test: 2 separate draws of the exact same `GaussianBlur.fx` test's own source (a 1-texel-wide
// bright vertical line on an otherwise black 64x64 `SceneTexture`, horizontal blur, BlurAmount=4)
// -- differing only in the `DistortionMap` value:
// Draw A -- DistortionMap=(0,0,*,255), the sentinel: the blur loop must NOT run at all -- the
//   line must stay exactly as crisp as the pre-blur source (on-line=255 exactly, 1 texel over
//   =0 exactly), unlike Draw B below.
// Draw B -- DistortionMap=(128,128,*,255): a genuinely NON-zero raw texel (so the sentinel branch
//   is NOT taken -- proving branch selection, not just magic-zero special-casing) that decodes to
//   EXACTLY zero displacement (128/255 - 0.5 - 0.5/255 = 0.0 exactly), isolating the blur
//   computation from any position-shift complexity. Reuses GaussianBlur.fx's own proven
//   discriminating check shape: on-line dimmer-but-nonzero (energy spread to neighbours), 1 texel
//   over now nonzero (was pure black pre-blur), far away (24 texels, beyond the 13.5-texel max
//   reach of 15 discrete taps) stays exactly pure black.
//
// Exit code 0 = all checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSampleCount = 15;
    constexpr int kSize = 64;

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

    // Ported 1:1 from Distort_PixelShader (distortionBlur=true branch).
    const char* kFragSrc = R"(#version 300 es
precision highp float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D uSceneTexture;
uniform sampler2D uDistortionMap;
uniform vec2 uSampleOffsets[15];
uniform float uSampleWeights[15];
const float kZeroOffset = 0.5 / 255.0;
void main() {
    vec2 displacement = texture(uDistortionMap, TexCoord).rg;
    vec4 finalColor = vec4(0.0);
    if (displacement.x == 0.0 && displacement.y == 0.0) {
        finalColor = texture(uSceneTexture, TexCoord);
    } else {
        displacement -= vec2(0.5 + kZeroOffset);
        for (int i = 0; i < 15; i++) {
            finalColor += texture(uSceneTexture, TexCoord.xy + displacement + uSampleOffsets[i])
                          * uSampleWeights[i];
        }
    }
    FragColor = finalColor;
}
)";

    // Ported 1:1 from BloomComponent.cs's ComputeGaussian() (reused from Task 946's own
    // GaussianBlur.fx test, not re-derived).
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

class EasyGLDistortBlurTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::shared_ptr<Effect> fxBase_;
    Texture2D whiteTex_;
    Texture2D sentinelMap_;
    Texture2D blurMap_;
    bool done_   = false;
    int result_  = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        const auto root = std::filesystem::temp_directory_path()
            / ("cna_distortblur_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        WriteFile(root / "distortblur.vert.glsl", kVertSrc);
        WriteFile(root / "distortblur.frag.glsl", kFragSrc);
        WriteFile(root / "distortblur.cnj", R"({
  "cnjVersion": 1,
  "type": "Effect",
  "vertex": "distortblur.vert.glsl",
  "fragment": "distortblur.frag.glsl"
})");

        getContentProperty().setRootDirectoryProperty(root.string());
        fxBase_ = getContentProperty().Load<std::shared_ptr<Effect>>("distortblur");

        const std::vector<uint8_t> white = { 255, 255, 255, 255 };
        whiteTex_ = Texture2D::CreateFromPixels(device, 1, 1, white);

        const std::vector<uint8_t> sentinel = { 0, 0, 0, 255 };
        sentinelMap_ = Texture2D::CreateFromPixels(device, 1, 1, sentinel);

        // 128/255 - 0.5 - 0.5/255 = 0.0 exactly -- a genuinely non-zero raw texel (avoids the
        // sentinel branch) that decodes to zero displacement (isolates the blur math).
        const std::vector<uint8_t> blur = { 128, 128, 0, 255 };
        blurMap_ = Texture2D::CreateFromPixels(device, 1, 1, blur);
    }

    struct Sample { Color onLine, adjacent, far; };

    Sample DrawOnce(Texture2D& distortionMap)
    {
        auto& device = getGraphicsDeviceProperty();

        Texture2D sourceTex(device, kSize, kSize);
        {
            std::vector<uint8_t> pixels(static_cast<size_t>(kSize) * kSize * 4, 0);
            for (int y = 0; y < kSize; ++y)
            {
                const size_t idx = (static_cast<size_t>(y) * kSize + 16) * 4;
                pixels[idx + 0] = 255; pixels[idx + 1] = 255;
                pixels[idx + 2] = 255; pixels[idx + 3] = 255;
            }
            sourceTex.SetDataRGBA(pixels.data(), kSize * kSize);
        }

        RenderTarget2D destRt(device, kSize, kSize);

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
        device.SetDepthTestEnabled(false);

        auto* fx = dynamic_cast<ShaderEffect*>(fxBase_.get());
        fx->Apply();
        fx->SetTexture(1, distortionMap);
        fx->SetUniformInt("uDistortionMap", 1);
        fx->SetUniformVec2Array("uSampleOffsets", flatOffsets, kSampleCount);
        fx->SetUniformFloatArray("uSampleWeights", weights, kSampleCount);

        SpriteBatch sb(device);
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr, fx);
        sb.Draw(sourceTex, Rectangle(0, 0, kSize, kSize), Color::White);
        sb.End();

        const Rectangle onLineReg(16, kSize / 2, 1, 1);
        const Rectangle adjacentReg(17, kSize / 2, 1, 1);
        const Rectangle farReg(40, kSize / 2, 1, 1);
        Sample s{ Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0) };
        device.GetBackBufferData(&onLineReg,   &s.onLine,   0, 1);
        device.GetBackBufferData(&adjacentReg, &s.adjacent, 0, 1);
        device.GetBackBufferData(&farReg,      &s.far,      0, 1);

        device.SetRenderTarget(nullptr);
        return s;
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto* fx = dynamic_cast<ShaderEffect*>(fxBase_.get());
        if (!fx || !fx->IsEffectValid())
        {
            std::printf("[FAIL] EasyGLDistortBlur: .cnj load or GLSL compile failed\n");
            Exit();
            return;
        }

        const Sample a = DrawOnce(sentinelMap_);
        const Sample b = DrawOnce(blurMap_);

        const bool aOnLineOk   = a.onLine.getRProperty() == 255;
        const bool aAdjacentOk = a.adjacent.getRProperty() == 0;
        const bool bOnLineOk   = b.onLine.getRProperty() > 20 && b.onLine.getRProperty() < 250;
        const bool bAdjacentOk = b.adjacent.getRProperty() > 0;
        const bool bFarOk      = b.far.getRProperty() == 0 && b.far.getGProperty() == 0 && b.far.getBProperty() == 0;

        std::printf("[%s] Draw A onLine (sentinel, no blur): %d expected==255\n",
                    aOnLineOk ? "PASS" : "FAIL", a.onLine.getRProperty());
        std::printf("[%s] Draw A adjacent (sentinel, no blur): %d expected==0\n",
                    aAdjacentOk ? "PASS" : "FAIL", a.adjacent.getRProperty());
        std::printf("[%s] Draw B onLine (blurred): %d expected in (20,250)\n",
                    bOnLineOk ? "PASS" : "FAIL", b.onLine.getRProperty());
        std::printf("[%s] Draw B adjacent (blurred): %d expected > 0\n",
                    bAdjacentOk ? "PASS" : "FAIL", b.adjacent.getRProperty());
        std::printf("[%s] Draw B far (beyond blur reach): (%d,%d,%d) expected==(0,0,0)\n",
                    bFarOk ? "PASS" : "FAIL", b.far.getRProperty(), b.far.getGProperty(), b.far.getBProperty());

        result_ = (aOnLineOk && aAdjacentOk && bOnLineOk && bAdjacentOk && bFarOk) ? 0 : 1;
        Exit();
    }

public:
    EasyGLDistortBlurTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    EasyGLDistortBlurTest game;
    game.Run();
    return game.getResult();
}

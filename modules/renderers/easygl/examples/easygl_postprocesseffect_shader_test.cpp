// SPDX-License-Identifier: MS-PL
// Task 947 (Phase 78 rollout): HLSL->GLSL shader-conversion proof -- PostprocessEffect.Fx's 5
// techniques (`EdgeDetect`/`EdgeDetectMonoSketch`/`EdgeDetectColorSketch`/`MonoSketch`/
// `ColorSketch`). With this, `NonPhotoRealistic` sample's shader blocker (DEFERRED.md item #11)
// is fully cleared -- `CartoonEffect.Fx`'s 3 techniques were already ported (see plans/plan_graphics.md
// Task 947's own row).
//
// FNA reference (`NonPhotoRealisticSample_4_0/NonPhotoRealistic/Content/PostprocessEffect.Fx`):
// all 5 techniques compile the SAME `PixelShaderFunction(texCoord, uniform bool applyEdgeDetect,
// uniform bool applySketch, uniform bool sketchInColor)` with different compile-time constants
// for the 3 bools (`EdgeDetect`=T,F,F; `EdgeDetectMonoSketch`=T,T,F; `EdgeDetectColorSketch`=T,T,T;
// `MonoSketch`=F,T,F; `ColorSketch`=F,T,T). Ported here as ONE GLSL shader with the 3 bools as
// real RUNTIME uniforms instead of 5 separately-compiled static variants -- behaviourally
// identical (the HLSL `uniform bool` params are still just plain booleans read at shader-compile
// time in that engine; nothing in the branch logic depends on them being resolved at compile vs.
// run time), just a pragmatic adaptation to this project's `ShaderEffect` model (one GLSL program
// per port, not five). Documented here as an intentional deviation, not silently done.
//
//   float4 PixelShaderFunction(...) : COLOR0
//   {
//       float3 scene = tex2D(SceneSampler, texCoord);
//       if (applySketch) {
//           float3 saturatedScene = saturate((scene - SketchThreshold) * 2);
//           float3 sketchPattern = tex2D(SketchSampler, texCoord + SketchJitter);
//           float3 negativeSketch = (1 - saturatedScene) * (1 - sketchPattern);
//           float sketchResult = dot(1 - negativeSketch, SketchBrightness);
//           if (sketchInColor) scene *= sketchResult; else scene = sketchResult;
//       }
//       if (applyEdgeDetect) {
//           float2 edgeOffset = EdgeWidth / ScreenResolution;
//           float4 n1 = tex2D(NormalDepthSampler, texCoord + float2(-1,-1)*edgeOffset);
//           float4 n2 = tex2D(NormalDepthSampler, texCoord + float2( 1, 1)*edgeOffset);
//           float4 n3 = tex2D(NormalDepthSampler, texCoord + float2(-1, 1)*edgeOffset);
//           float4 n4 = tex2D(NormalDepthSampler, texCoord + float2( 1,-1)*edgeOffset);
//           float4 diagonalDelta = abs(n1-n2) + abs(n3-n4);
//           float normalDelta = saturate((dot(diagonalDelta.xyz,1) - NormalThreshold) * NormalSensitivity);
//           float depthDelta = saturate((diagonalDelta.w - DepthThreshold) * DepthSensitivity);
//           float edgeAmount = saturate(normalDelta + depthDelta) * EdgeIntensity;
//           scene *= (1 - edgeAmount);
//       }
//       return float4(scene, 1);
//   }
//
// Ported 1:1 below, including the order (sketch first, then edge detect operates on the
// already-sketched scene value) and HLSL's own implicit scalar-to-vec3 broadcast in
// `dot(float3, float SketchBrightness)` (made explicit as `dot(x, vec3(SketchBrightness))` in
// GLSL, which requires matching vector types).
//
// Fixture: a solid SceneTexture (0.8,0.6,0.4) and a solid black SketchTexture (0,0,0, i.e.
// "fully inked" -- the opposite of a blank white page) drive the sketch checks. A 64-texel-wide
// NormalDepthTexture with a hard left/right colour split (left=(0,0,1,0), right=(1,0,0,1) --
// deliberately very different so any diagonal delta crossing the boundary saturates both
// thresholds) drives the edge-detect checks: sampled far from the boundary, all 4 diagonal taps
// land in the same half (delta=0, no edge); sampled AT the boundary, the taps straddle both
// halves (large delta, full edge).
//
// Check 1 -- EdgeDetect, far from the boundary: all 4 diagonal NormalDepth samples identical ->
//   diagonalDelta=0 -> edgeAmount=0 -> scene unchanged ~= (204,153,102) (0.8,0.6,0.4 * 255).
// Check 2 -- EdgeDetect, at the boundary: diagonalDelta = 2*|(0,0,1,0)-(1,0,0,1)| = (2,0,2,2) ->
//   normalDelta=saturate((4-0.5)*1)=1, depthDelta=saturate((2-0.1)*10)=1 -> edgeAmount=
//   saturate(2)*1=1 -> scene *= 0 -> pure black (0,0,0).
// Check 3 -- MonoSketch: saturatedScene=(1,1,0.6) (from (0.8,0.6,0.4)), sketchPattern=(0,0,0) ->
//   negativeSketch=(1-satScene)*(1-0)=(0,0,0.4) -> sketchResult=dot((1,1,0.6),(0.333,..))~=0.8658
//   -> scene=vec3(sketchResult) ~= (221,221,221) (grey, scene colour discarded).
// Check 4 -- ColorSketch: same sketchResult~=0.8658, but scene *= sketchResult (colour KEPT) ~=
//   (0.8,0.6,0.4)*0.8658*255 ~= (177,133,88). Distinct from Check 3 -- proves `sketchInColor`
//   genuinely selects between "replace" and "multiply", not always the same branch.
// Check 5 -- EdgeDetectMonoSketch, far from the boundary (edgeAmount=0, so edge detect is a
//   no-op): must equal Check 3's own MonoSketch result exactly ~= (221,221,221) -- proves the two
//   stages compose (sketch first, then edge detect) rather than one silently overriding the other.
// Check 6 -- EdgeDetectColorSketch, at the boundary (edgeAmount=1): must be pure black regardless
//   of the ColorSketch result computed first -- proves edge detect's own multiply-by-zero
//   genuinely applies AFTER sketch, not skipped when sketch is also active.
//
// Exit code 0 = all 6 checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
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

    // Ported 1:1 from PostprocessEffect.Fx's PixelShaderFunction -- applyEdgeDetect/applySketch/
    // sketchInColor are real runtime uniforms here instead of 5 separately-compiled static
    // variants (see this file's own header comment for why).
    const char* kFragSrc = R"(#version 300 es
precision highp float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D uSceneTexture;
uniform sampler2D uNormalDepthTexture;
uniform sampler2D uSketchTexture;
uniform bool applyEdgeDetect;
uniform bool applySketch;
uniform bool sketchInColor;
uniform float EdgeWidth;
uniform float EdgeIntensity;
uniform float NormalThreshold;
uniform float DepthThreshold;
uniform float NormalSensitivity;
uniform float DepthSensitivity;
uniform float SketchThreshold;
uniform float SketchBrightness;
uniform vec2 SketchJitter;
uniform vec2 ScreenResolution;
void main() {
    vec3 scene = texture(uSceneTexture, TexCoord).rgb;

    if (applySketch) {
        vec3 saturatedScene = clamp((scene - SketchThreshold) * 2.0, 0.0, 1.0);
        vec3 sketchPattern = texture(uSketchTexture, TexCoord + SketchJitter).rgb;
        vec3 negativeSketch = (vec3(1.0) - saturatedScene) * (vec3(1.0) - sketchPattern);
        float sketchResult = dot(vec3(1.0) - negativeSketch, vec3(SketchBrightness));
        if (sketchInColor) scene *= sketchResult;
        else scene = vec3(sketchResult);
    }

    if (applyEdgeDetect) {
        vec2 edgeOffset = vec2(EdgeWidth) / ScreenResolution;
        vec4 n1 = texture(uNormalDepthTexture, TexCoord + vec2(-1.0, -1.0) * edgeOffset);
        vec4 n2 = texture(uNormalDepthTexture, TexCoord + vec2( 1.0,  1.0) * edgeOffset);
        vec4 n3 = texture(uNormalDepthTexture, TexCoord + vec2(-1.0,  1.0) * edgeOffset);
        vec4 n4 = texture(uNormalDepthTexture, TexCoord + vec2( 1.0, -1.0) * edgeOffset);
        vec4 diagonalDelta = abs(n1 - n2) + abs(n3 - n4);

        float normalDelta = dot(diagonalDelta.xyz, vec3(1.0));
        float depthDelta = diagonalDelta.w;

        normalDelta = clamp((normalDelta - NormalThreshold) * NormalSensitivity, 0.0, 1.0);
        depthDelta = clamp((depthDelta - DepthThreshold) * DepthSensitivity, 0.0, 1.0);

        float edgeAmount = clamp(normalDelta + depthDelta, 0.0, 1.0) * EdgeIntensity;
        scene *= (1.0 - edgeAmount);
    }

    FragColor = vec4(scene, 1.0);
}
)";
}

class EasyGLPostprocessEffectTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::shared_ptr<Effect> fxBase_;
    Texture2D sceneTex_;
    Texture2D normalDepthTex_;
    Texture2D sketchTex_;
    bool done_   = false;
    int result_  = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        const auto root = std::filesystem::temp_directory_path()
            / ("cna_postprocesseffect_test_" + std::to_string(
                   reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        WriteFile(root / "pp.vert.glsl", kVertSrc);
        WriteFile(root / "pp.frag.glsl", kFragSrc);
        WriteFile(root / "pp.cnj", R"({
  "cnjVersion": 1,
  "type": "Effect",
  "vertex": "pp.vert.glsl",
  "fragment": "pp.frag.glsl"
})");

        getContentProperty().setRootDirectoryProperty(root.string());
        fxBase_ = getContentProperty().Load<std::shared_ptr<Effect>>("pp");

        // Solid scene: (0.8,0.6,0.4) -> (204,153,102).
        const std::vector<uint8_t> scenePixels = { 204, 153, 102, 255 };
        sceneTex_ = Texture2D::CreateFromPixels(device, 1, 1, scenePixels);

        // Solid black sketch overlay ("fully inked").
        const std::vector<uint8_t> sketchPixels = { 0, 0, 0, 255 };
        sketchTex_ = Texture2D::CreateFromPixels(device, 1, 1, sketchPixels);

        // 64x1 NormalDepthTexture: left half (0,0,1,0), right half (1,0,0,1).
        std::vector<uint8_t> ndPixels(static_cast<size_t>(kSize) * 4, 0);
        for (int x = 0; x < kSize; ++x)
        {
            const size_t idx = static_cast<size_t>(x) * 4;
            if (x < kSize / 2)
            {
                ndPixels[idx + 0] = 0; ndPixels[idx + 1] = 0;
                ndPixels[idx + 2] = 255; ndPixels[idx + 3] = 0;
            }
            else
            {
                ndPixels[idx + 0] = 255; ndPixels[idx + 1] = 0;
                ndPixels[idx + 2] = 0; ndPixels[idx + 3] = 255;
            }
        }
        normalDepthTex_ = Texture2D::CreateFromPixels(device, kSize, 1, ndPixels);
    }

    // Reads back centre-row pixel at screen x = xPixel.
    Color DrawOnce(bool applyEdgeDetect, bool applySketch, bool sketchInColor, int xPixel)
    {
        auto& device = getGraphicsDeviceProperty();

        device.Clear(Color::Black);
        device.SetDepthTestEnabled(false);

        SpriteBatch sb(device);
        SamplerState pointClamp = SamplerState::PointClamp;

        auto* fx = dynamic_cast<ShaderEffect*>(fxBase_.get());
        fx->Apply();
        fx->SetTexture(1, normalDepthTex_);
        fx->SetUniformInt("uNormalDepthTexture", 1);
        fx->SetTexture(2, sketchTex_);
        fx->SetUniformInt("uSketchTexture", 2);
        fx->SetUniformInt("applyEdgeDetect", applyEdgeDetect ? 1 : 0);
        fx->SetUniformInt("applySketch", applySketch ? 1 : 0);
        fx->SetUniformInt("sketchInColor", sketchInColor ? 1 : 0);
        fx->SetUniformFloat("EdgeWidth", 1.0f);
        fx->SetUniformFloat("EdgeIntensity", 1.0f);
        fx->SetUniformFloat("NormalThreshold", 0.5f);
        fx->SetUniformFloat("DepthThreshold", 0.1f);
        fx->SetUniformFloat("NormalSensitivity", 1.0f);
        fx->SetUniformFloat("DepthSensitivity", 10.0f);
        fx->SetUniformFloat("SketchThreshold", 0.1f);
        fx->SetUniformFloat("SketchBrightness", 0.333f);
        fx->SetUniformVec2("SketchJitter", 0.0f, 0.0f);
        fx->SetUniformVec2("ScreenResolution", static_cast<float>(kSize), static_cast<float>(kSize));

        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr, fx);
        sb.Draw(sceneTex_, Rectangle(0, 0, kSize, kSize), Color::White);
        sb.End();

        const Rectangle reg(xPixel, kSize / 2, 1, 1);
        Color c(0, 0, 0, 0);
        device.GetBackBufferData(&reg, &c, 0, 1);
        return c;
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto* fx = dynamic_cast<ShaderEffect*>(fxBase_.get());
        if (!fx || !fx->IsEffectValid())
        {
            std::printf("[FAIL] EasyGLPostprocessEffect: .cnj load or GLSL compile failed\n");
            Exit();
            return;
        }

        // x=8: deep in the NormalDepthTexture's left half, far from the boundary.
        // x=32: exactly at the left/right boundary.
        const int farX = 8, boundaryX = 32;

        const Color c1 = DrawOnce(true, false, false, farX);
        const Color c2 = DrawOnce(true, false, false, boundaryX);
        const Color c3 = DrawOnce(false, true, false, farX);
        const Color c4 = DrawOnce(false, true, true, farX);
        const Color c5 = DrawOnce(true, true, false, farX);
        const Color c6 = DrawOnce(true, true, true, boundaryX);

        const auto close = [](int got, int expected) { return got >= expected - 8 && got <= expected + 8; };
        const bool ok1 = close(c1.getRProperty(), 204) && close(c1.getGProperty(), 153) && close(c1.getBProperty(), 102);
        const bool ok2 = c2.getRProperty() <= 10 && c2.getGProperty() <= 10 && c2.getBProperty() <= 10;
        const bool ok3 = close(c3.getRProperty(), 221) && close(c3.getGProperty(), 221) && close(c3.getBProperty(), 221);
        const bool ok4 = close(c4.getRProperty(), 177) && close(c4.getGProperty(), 133) && close(c4.getBProperty(), 88);
        const bool ok5 = close(c5.getRProperty(), c3.getRProperty()) && close(c5.getGProperty(), c3.getGProperty())
                       && close(c5.getBProperty(), c3.getBProperty());
        const bool ok6 = c6.getRProperty() <= 10 && c6.getGProperty() <= 10 && c6.getBProperty() <= 10;

        std::printf("[%s] Check1 EdgeDetect far: (%d,%d,%d) expected~=(204,153,102)\n",
                    ok1 ? "PASS" : "FAIL", c1.getRProperty(), c1.getGProperty(), c1.getBProperty());
        std::printf("[%s] Check2 EdgeDetect boundary: (%d,%d,%d) expected~=(0,0,0)\n",
                    ok2 ? "PASS" : "FAIL", c2.getRProperty(), c2.getGProperty(), c2.getBProperty());
        std::printf("[%s] Check3 MonoSketch: (%d,%d,%d) expected~=(221,221,221)\n",
                    ok3 ? "PASS" : "FAIL", c3.getRProperty(), c3.getGProperty(), c3.getBProperty());
        std::printf("[%s] Check4 ColorSketch: (%d,%d,%d) expected~=(177,133,88)\n",
                    ok4 ? "PASS" : "FAIL", c4.getRProperty(), c4.getGProperty(), c4.getBProperty());
        std::printf("[%s] Check5 EdgeDetectMonoSketch far (matches Check3): (%d,%d,%d)\n",
                    ok5 ? "PASS" : "FAIL", c5.getRProperty(), c5.getGProperty(), c5.getBProperty());
        std::printf("[%s] Check6 EdgeDetectColorSketch boundary: (%d,%d,%d) expected~=(0,0,0)\n",
                    ok6 ? "PASS" : "FAIL", c6.getRProperty(), c6.getGProperty(), c6.getBProperty());

        result_ = (ok1 && ok2 && ok3 && ok4 && ok5 && ok6) ? 0 : 1;
        Exit();
    }

public:
    EasyGLPostprocessEffectTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    EasyGLPostprocessEffectTest game;
    game.Run();
    return game.getResult();
}

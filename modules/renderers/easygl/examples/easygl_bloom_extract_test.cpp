// SPDX-License-Identifier: MS-PL
// Task 946: HLSL→GLSL shader-conversion proof — BloomExtract.fx (EasyGL renderer).
//
// FNA reference (`BloomSample_4_0/BloomPostprocess/Content/BloomExtract.fx`):
//   sampler TextureSampler : register(s0);
//   float BloomThreshold;
//   float4 PixelShaderFunction(float2 texCoord : TEXCOORD0) : COLOR0
//   {
//       float4 c = tex2D(TextureSampler, texCoord);
//       return saturate((c - BloomThreshold) / (1 - BloomThreshold));
//   }
//
// This is the simplest of BloomSample's 3 custom pixel shaders (a scalar-per-pixel threshold
// remap, no arrays, single texture) -- ported 1:1 to GLSL below. HLSL's implicit scalar-to-vec4
// broadcast for `c - BloomThreshold` / `1 - BloomThreshold` (applied to ALL 4 channels, including
// alpha) is native GLSL behaviour too, so the formula translates directly with no restructuring.
//
// Also proves the .cnj (Effect type) + GLSL descriptor round-trip via
// ContentManager::Load<Effect>() (EffectTypeReader,
// src/Microsoft/Xna/Framework/Content/ContentManager.cpp) -- implemented in CNA but, before this
// test, never exercised anywhere in the repo. Migrated from .shader.json to .cnj per plans/plan_cnj.md
// CNB-14/CNB-15.
//
// Check A — bright input (1,1,1,1), BloomThreshold=0.5: saturate((1-0.5)/0.5) = 1 on every
//           channel including alpha -> expect (255,255,255,255).
// Check B — dim input (0.3,0.3,0.3,1), BloomThreshold=0.5: RGB -> saturate((0.3-0.5)/0.5) =
//           saturate(-0.4) = 0; alpha -> saturate((1-0.5)/0.5) = 1 -> expect (0,0,0,255).
//
// Exit code 0 = both PASS, 1 = either FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
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

    // Ported 1:1 from BloomExtract.fx's PixelShaderFunction.
    const char* kFragSrc = R"(#version 300 es
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
}

class EasyGLBloomExtractTest : public Game
{
    std::shared_ptr<Effect> fxBase_;
    Texture2D brightTex_;
    Texture2D dimTex_;
    bool done_   = false;
    int result_  = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        const auto root = std::filesystem::temp_directory_path()
            / ("cna_bloom_extract_test_" + std::to_string(
                   reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        WriteFile(root / "bloom_extract.vert.glsl", kVertSrc);
        WriteFile(root / "bloom_extract.frag.glsl", kFragSrc);
        WriteFile(root / "bloom_extract.cnj", R"({
  "cnjVersion": 1,
  "type": "Effect",
  "vertex": "bloom_extract.vert.glsl",
  "fragment": "bloom_extract.frag.glsl"
})");

        getContentProperty().setRootDirectoryProperty(root.string());
        fxBase_ = getContentProperty().Load<std::shared_ptr<Effect>>("bloom_extract");

        const std::vector<uint8_t> bright = { 255, 255, 255, 255 };
        brightTex_ = Texture2D::CreateFromPixels(device, 1, 1, bright);
        const std::vector<uint8_t> dim = { 76, 76, 76, 255 }; // 0.3 * 255 ~= 76
        dimTex_ = Texture2D::CreateFromPixels(device, 1, 1, dim);
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
            std::printf("[FAIL] EasyGLBloomExtract: .cnj load or GLSL compile failed\n");
            Exit();
            return;
        }

        device.Clear(Color(0, 0, 255, 255));
        device.SetDepthTestEnabled(false);

        SpriteBatch sb(device);

        fx->Apply();
        fx->SetUniformFloat("uBloomThreshold", 0.5f);
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr, fx);
        sb.Draw(brightTex_, Rectangle(0, 0, W / 2, H), Color::White);
        sb.Draw(dimTex_,    Rectangle(W / 2, 0, W / 2, H), Color::White);
        sb.End();

        const Rectangle brightReg(W / 4, H / 2, 1, 1);
        const Rectangle dimReg(3 * W / 4, H / 2, 1, 1);
        Color brightPx(0, 0, 0, 0), dimPx(0, 0, 0, 0);
        device.GetBackBufferData(&brightReg, &brightPx, 0, 1);
        device.GetBackBufferData(&dimReg,    &dimPx,    0, 1);

        const bool brightOk = brightPx.getRProperty() >= 250 && brightPx.getGProperty() >= 250
                           && brightPx.getBProperty() >= 250 && brightPx.getAProperty() >= 250;
        const bool dimOk = dimPx.getRProperty() <= 5 && dimPx.getGProperty() <= 5
                         && dimPx.getBProperty() <= 5 && dimPx.getAProperty() >= 250;

        if (brightOk && dimOk)
        {
            std::printf("[PASS] EasyGLBloomExtract: bright=(%d,%d,%d,%d) dim=(%d,%d,%d,%d)\n",
                        brightPx.getRProperty(), brightPx.getGProperty(), brightPx.getBProperty(), brightPx.getAProperty(),
                        dimPx.getRProperty(), dimPx.getGProperty(), dimPx.getBProperty(), dimPx.getAProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] EasyGLBloomExtract: bright=(%d,%d,%d,%d) dim=(%d,%d,%d,%d)\n"
                        "       expected: bright=(255,255,255,255) dim=(0,0,0,255)\n",
                        brightPx.getRProperty(), brightPx.getGProperty(), brightPx.getBProperty(), brightPx.getAProperty(),
                        dimPx.getRProperty(), dimPx.getGProperty(), dimPx.getBProperty(), dimPx.getAProperty());
        }
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    EasyGLBloomExtractTest game;
    game.Run();
    return game.getResult();
}

// SPDX-License-Identifier: MS-PL
// Task 947 (Phase 78 rollout): HLSL->GLSL shader-conversion proof -- Distort.fx's `Distort`
// technique (no-blur variant; the `DistortBlur` technique -- same shader with the array-uniform
// weighted-sum path taken -- is a separate, larger port, not attempted in this row).
//
// FNA reference (`DistortionSample_4_0/Distortion/Content/Distort.fx`, `Distort_PixelShader`
// with `distortionBlur=false`):
//   float4 Distort_PixelShader(float2 TexCoord : TEXCOORD0, uniform bool distortionBlur) : COLOR0
//   {
//       float2 displacement = tex2D(DistortionMap, TexCoord).rg;
//       float4 finalColor = 0;
//       if ((displacement.x == 0) && (displacement.y == 0))
//           finalColor = tex2D(SceneTexture, TexCoord);
//       else
//       {
//           displacement -= .5 + ZeroOffset;   // ZeroOffset = 0.5/255
//           finalColor = tex2D(SceneTexture, TexCoord.xy + displacement);
//       }
//       return finalColor;
//   }
//
// Ported 1:1 below, including the shader's own documented reasoning: a `DistortionMap` texel of
// EXACTLY (0,0) is a sentinel meaning "this pixel of the scene was never touched by any
// distorter, pass the scene texture through unmodified" (the real pipeline clears the
// DistortionMap to black before any distorter draws into it) -- any other value is treated as a
// real UV displacement, decoded from [0,1] back to roughly [-0.5,0.5) via `- (0.5+ZeroOffset)`.
//
// Test: a 2-texel-wide SceneTexture (texel0=Red, texel1=Green) and a matching 2-texel-wide
// DistortionMap (texel0=(0,0,*,255), the sentinel -- pure passthrough; texel1=(25,25,*,255), a
// real displacement large enough to shift sampling clear across into the OTHER SceneTexture
// texel -- proving the displacement is genuinely applied, not just present). Point-sampled
// (SamplerState::PointClamp) to keep texel selection unambiguous.
//
// Check A -- screen x~0.25 (DistortionMap texel0, the (0,0) sentinel): passthrough ->
//   SceneTexture(TexCoord~0.25) = texel0 = Red.
// Check B -- screen x~0.75 (DistortionMap texel1, R=25/255~=0.098): displacement.x =
//   0.098 - 0.5 - 0.5/255 ~= -0.404. Sampled position = 0.75 - 0.404 ~= 0.346 -> lands back in
//   texel0's own [0,0.5) region -> Red. Without the displacement being applied at all (or applied
//   with the wrong sign/magnitude), this pixel's natural undisplaced UV (0.75) would read texel1
//   (Green) instead -- Red vs Green is the discriminator.
//
// Exit code 0 = both PASS, 1 = either FAIL.

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

    // Ported 1:1 from Distort_PixelShader (distortionBlur=false branch only).
    const char* kFragSrc = R"(#version 300 es
precision highp float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D uSceneTexture;
uniform sampler2D uDistortionMap;
const float kZeroOffset = 0.5 / 255.0;
void main() {
    vec2 displacement = texture(uDistortionMap, TexCoord).rg;
    vec4 finalColor;
    if (displacement.x == 0.0 && displacement.y == 0.0) {
        finalColor = texture(uSceneTexture, TexCoord);
    } else {
        displacement -= vec2(0.5 + kZeroOffset);
        finalColor = texture(uSceneTexture, TexCoord.xy + displacement);
    }
    FragColor = finalColor;
}
)";
}

class EasyGLDistortTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::shared_ptr<Effect> fxBase_;
    Texture2D sceneTex_;
    Texture2D distortionMap_;
    bool done_   = false;
    int result_  = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        const auto root = std::filesystem::temp_directory_path()
            / ("cna_distort_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        WriteFile(root / "distort.vert.glsl", kVertSrc);
        WriteFile(root / "distort.frag.glsl", kFragSrc);
        WriteFile(root / "distort.cnb", R"({
  "cnbVersion": 1,
  "type": "Effect",
  "vertex": "distort.vert.glsl",
  "fragment": "distort.frag.glsl"
})");

        getContentProperty().setRootDirectoryProperty(root.string());
        fxBase_ = getContentProperty().Load<std::shared_ptr<Effect>>("distort");

        // 2x1 texels: texel0=Red, texel1=Green.
        const std::vector<uint8_t> scenePixels = {
            255, 0, 0, 255,
            0, 255, 0, 255,
        };
        sceneTex_ = Texture2D::CreateFromPixels(device, 2, 1, scenePixels);

        // 2x1 texels: texel0=(0,0,*,255) sentinel, texel1=(25,25,*,255) real displacement.
        const std::vector<uint8_t> distortPixels = {
            0, 0, 0, 255,
            25, 25, 0, 255,
        };
        distortionMap_ = Texture2D::CreateFromPixels(device, 2, 1, distortPixels);
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
            std::printf("[FAIL] EasyGLDistort: .cnb load or GLSL compile failed\n");
            Exit();
            return;
        }

        device.Clear(Color(0, 0, 0, 255));
        device.SetDepthTestEnabled(false);

        SpriteBatch sb(device);
        SamplerState pointClamp = SamplerState::PointClamp;

        fx->Apply();
        fx->SetTexture(1, distortionMap_);
        fx->SetUniformInt("uDistortionMap", 1);

        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr, fx);
        sb.Draw(sceneTex_, Rectangle(0, 0, W, H), Color::White);
        sb.End();

        const Rectangle regA(W / 4, H / 2, 1, 1);
        const Rectangle regB(3 * W / 4, H / 2, 1, 1);
        Color a(0, 0, 0, 0), b(0, 0, 0, 0);
        device.GetBackBufferData(&regA, &a, 0, 1);
        device.GetBackBufferData(&regB, &b, 0, 1);

        const auto isRed = [](const Color& c) {
            return c.getRProperty() >= 200 && c.getGProperty() <= 30 && c.getBProperty() <= 30;
        };

        const bool aOk = isRed(a);
        const bool bOk = isRed(b);

        std::printf("[%s] Check A (sentinel, passthrough): (%d,%d,%d) expected Red\n",
                    aOk ? "PASS" : "FAIL", a.getRProperty(), a.getGProperty(), a.getBProperty());
        std::printf("[%s] Check B (displaced back into texel0): (%d,%d,%d) expected Red\n",
                    bOk ? "PASS" : "FAIL", b.getRProperty(), b.getGProperty(), b.getBProperty());

        result_ = (aOk && bOk) ? 0 : 1;
        Exit();
    }

public:
    EasyGLDistortTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    EasyGLDistortTest game;
    game.Run();
    return game.getResult();
}

// SPDX-License-Identifier: MS-PL
// Task 947 (Phase 78 rollout): HLSL->GLSL shader-conversion proof -- CartoonEffect.Fx's `Toon`
// technique (used by `NonPhotoRealisticSample` for banded cartoon shading, `Game.cs:251`). Shares
// the sibling `Lambert` technique's own `LightingVertexShader` verbatim (see that test's own
// header comment) -- only the pixel shader differs.
//
// FNA reference (`NonPhotoRealisticSample_4_0/NonPhotoRealistic/Content/CartoonEffect.Fx`,
// `ToonPixelShader`):
//   float ToonThresholds[2] = { 0.8, 0.4 };
//   float ToonBrightnessLevels[3] = { 1.3, 0.9, 0.5 };
//   float4 ToonPixelShader(LightingPixelShaderInput input) : COLOR0
//   {
//       float4 color = TextureEnabled ? tex2D(Sampler, input.TextureCoordinate) : 0;
//       float light;
//       if (input.LightAmount > ToonThresholds[0]) light = ToonBrightnessLevels[0];
//       else if (input.LightAmount > ToonThresholds[1]) light = ToonBrightnessLevels[1];
//       else light = ToonBrightnessLevels[2];
//       color.rgb *= light;
//       return color;
//   }
//
// Ported 1:1 below -- note `LightAmount` here is used RAW (unlike the sibling `Lambert`
// technique's own `saturate(input.LightAmount)`), so it can legitimately be negative and still
// correctly fall into the lowest ("else") band; not a bug, preserved faithfully either way.
//
// Same geometry/light/camera setup as the sibling Lambert test (quad centred at the origin,
// `LightDirection = normalize(1,1,1)`, camera at (0,0,3)).
//
// Check A -- World=Identity: worldNormal=(0,0,1). LightAmount = dot((0,0,1), normalize(1,1,1))
//   = 1/sqrt(3) ~= 0.57735. Not > 0.8; IS > 0.4 -> middle band, light=0.9. colour =
//   texColor*0.9 = (200,100,50)/255 * 0.9 ~= (180,90,45).
// Check B -- World=RotationY(180deg), same on-screen footprint, flips worldNormal to (0,0,-1):
//   LightAmount = dot((0,0,-1), normalize(1,1,1)) = -0.57735. Not > 0.8, not > 0.4 -> lowest
//   ("else") band, light=0.5. colour = texColor*0.5 ~= (100,50,25). Distinct band from Check A --
//   proves World reaches the vertex shader's normal transform and the pixel shader's own
//   threshold comparisons genuinely select different bands, not always the same one.
//
// Exit code 0 = both PASS, 1 = either FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cmath>
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

    // Identical to the sibling Lambert test's own vertex shader -- both XNA techniques share the
    // literal same LightingVertexShader function.
    const char* kVertSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
out vec2 vTexCoord;
out float vLightAmount;
uniform mat4 World;
uniform mat4 View;
uniform mat4 Projection;
uniform vec3 LightDirection;
void main() {
    gl_Position = Projection * View * World * vec4(aPosition, 1.0);
    vTexCoord = aTexCoord;
    vec3 worldNormal = mat3(World) * aNormal;
    vLightAmount = dot(worldNormal, LightDirection);
}
)";

    // Ported 1:1 from CartoonEffect.Fx's ToonPixelShader.
    const char* kFragSrc = R"(#version 300 es
precision mediump float;
in vec2 vTexCoord;
in float vLightAmount;
out vec4 FragColor;
uniform sampler2D texture1;
uniform bool TextureEnabled;
uniform float ToonThresholds[2];
uniform float ToonBrightnessLevels[3];
void main() {
    vec4 color = TextureEnabled ? texture(texture1, vTexCoord) : vec4(0.0);
    float light;
    if (vLightAmount > ToonThresholds[0]) light = ToonBrightnessLevels[0];
    else if (vLightAmount > ToonThresholds[1]) light = ToonBrightnessLevels[1];
    else light = ToonBrightnessLevels[2];
    color.rgb *= light;
    FragColor = color;
}
)";
}

class EasyGLCartoonEffectToonTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::shared_ptr<Effect> fxBase_;
    std::unique_ptr<VertexBuffer> vb_;
    std::unique_ptr<IndexBuffer> ib_;
    Texture2D tex_;
    bool done_   = false;
    int result_  = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        const auto root = std::filesystem::temp_directory_path()
            / ("cna_cartooneffect_toon_test_" + std::to_string(
                   reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        WriteFile(root / "toon.vert.glsl", kVertSrc);
        WriteFile(root / "toon.frag.glsl", kFragSrc);
        WriteFile(root / "toon.cnb", R"({
  "cnbVersion": 1,
  "type": "Effect",
  "vertex": "toon.vert.glsl",
  "fragment": "toon.frag.glsl"
})");

        getContentProperty().setRootDirectoryProperty(root.string());
        fxBase_ = getContentProperty().Load<std::shared_ptr<Effect>>("toon");

        const std::vector<uint8_t> pixels = { 200, 100, 50, 255 };
        tex_ = Texture2D::CreateFromPixels(device, 1, 1, pixels);

        const Vector3 n(0.0f, 0.0f, 1.0f);
        const VertexPositionNormalTexture verts[4] = {
            { Vector3(-0.5f,  0.5f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3(-0.5f, -0.5f, 0.0f), n, Vector2(0.0f, 0.0f) },
            { Vector3( 0.5f, -0.5f, 0.0f), n, Vector2(1.0f, 0.0f) },
            { Vector3( 0.5f,  0.5f, 0.0f), n, Vector2(1.0f, 1.0f) },
        };
        const std::uint16_t indices[6] = { 0, 1, 2, 0, 2, 3 };

        vb_ = std::make_unique<VertexBuffer>(device, 4);
        vb_->SetData(verts, 4);
        ib_ = std::make_unique<IndexBuffer>(device, 6);
        ib_->SetData(indices, 6);
    }

    Color DrawOnce(const Matrix& world)
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(Color(10, 10, 10, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        auto* fx = dynamic_cast<ShaderEffect*>(fxBase_.get());
        fx->setWorldProperty(world);
        fx->setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero,
                                                  Vector3(0.0f, 1.0f, 0.0f)));
        fx->setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(
            MathHelper::PiOver4, vp.getAspectRatioProperty(), 0.1f, 100.0f));
        fx->Apply();
        const float invSqrt3 = 1.0f / std::sqrt(3.0f);
        fx->SetUniformVec3("LightDirection", invSqrt3, invSqrt3, invSqrt3);
        const float thresholds[2] = { 0.8f, 0.4f };
        const float brightness[3] = { 1.3f, 0.9f, 0.5f };
        fx->SetUniformFloatArray("ToonThresholds", thresholds, 2);
        fx->SetUniformFloatArray("ToonBrightnessLevels", brightness, 3);
        fx->SetUniformInt("TextureEnabled", 1);
        fx->SetTexture(0, tex_);

        device.SetVertexBuffer(vb_.get());
        device.setIndicesProperty(ib_.get());
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);

        Color c(0, 0, 0, 0);
        const Rectangle centre(W / 2, H / 2, 1, 1);
        device.GetBackBufferData(&centre, &c, 0, 1);
        return c;
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto* fx = dynamic_cast<ShaderEffect*>(fxBase_.get());
        if (!fx || !fx->IsEffectValid())
        {
            std::printf("[FAIL] EasyGLCartoonEffectToon: .cnb load or GLSL compile failed\n");
            Exit();
            return;
        }

        const Color a = DrawOnce(Matrix::getIdentityProperty());
        const Color b = DrawOnce(Matrix::CreateRotationY(MathHelper::Pi));

        const auto close = [](int got, int expected) { return got >= expected - 6 && got <= expected + 6; };
        const bool aOk = close(a.getRProperty(), 180) && close(a.getGProperty(), 90) && close(a.getBProperty(), 45);
        const bool bOk = close(b.getRProperty(), 100) && close(b.getGProperty(), 50) && close(b.getBProperty(), 25);

        std::printf("[%s] Check A (World=Identity, middle band): (%d,%d,%d) expected~=(180,90,45)\n",
                    aOk ? "PASS" : "FAIL", a.getRProperty(), a.getGProperty(), a.getBProperty());
        std::printf("[%s] Check B (World=RotateY180, lowest band): (%d,%d,%d) expected~=(100,50,25)\n",
                    bOk ? "PASS" : "FAIL", b.getRProperty(), b.getGProperty(), b.getBProperty());

        result_ = (aOk && bOk) ? 0 : 1;
        Exit();
    }

public:
    EasyGLCartoonEffectToonTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    EasyGLCartoonEffectToonTest game;
    game.Run();
    return game.getResult();
}

// SPDX-License-Identifier: MS-PL
// Task 947 (Phase 78 rollout): HLSL->GLSL shader-conversion proof -- CartoonEffect.Fx's `Lambert`
// technique (used by `NonPhotoRealisticSample` whenever cartoon/toon shading is toggled off,
// `Game.cs:251-255`). A real 3D shader using Task 1079's new capability, `VertexPositionNormalTexture`
// (stride 32 -- Position+Normal+TexCoord, matching this shader's own `VertexShaderInput` exactly).
//
// FNA reference (`NonPhotoRealisticSample_4_0/NonPhotoRealistic/Content/CartoonEffect.Fx`,
// `LightingVertexShader` + `LambertPixelShader`):
//   LightingVertexShaderOutput LightingVertexShader(VertexShaderInput input)
//   {
//       output.Position = mul(mul(mul(input.Position, World), View), Projection);
//       output.TextureCoordinate = input.TextureCoordinate;
//       float3 worldNormal = mul(input.Normal, World);
//       output.LightAmount = dot(worldNormal, LightDirection);   // NOT saturated here
//       return output;
//   }
//   float4 LambertPixelShader(LightingPixelShaderInput input) : COLOR0
//   {
//       float4 color = TextureEnabled ? tex2D(Sampler, input.TextureCoordinate) : 0;
//       color.rgb *= saturate(input.LightAmount) * DiffuseLight + AmbientLight;
//       return color;
//   }
//
// Ported 1:1 below, including the original's own default uniform values
// (`LightDirection = normalize(1,1,1)`, `DiffuseLight = AmbientLight = 0.5`) -- this test sets
// them explicitly to those same defaults rather than relying on GLSL's own (different, all-zero)
// uninitialized-uniform behaviour.
//
// Check A -- World=Identity: worldNormal=(0,0,1). LightAmount = dot((0,0,1), normalize(1,1,1))
//   = 1/sqrt(3) ~= 0.57735 (already in [0,1], saturate is a no-op here). colour = texColor *
//   (0.57735*0.5 + 0.5) = texColor * 0.788675. texColor=(200,100,50)/255 ->
//   result ~= (0.618507,0.309254,0.154627) ~= (158,79,39).
// Check B -- World=RotationY(180deg), same on-screen footprint, flips worldNormal to (0,0,-1):
//   LightAmount = dot((0,0,-1), normalize(1,1,1)) = -0.57735 -> saturate clamps to 0 -> colour =
//   texColor * (0*0.5 + 0.5) = texColor * 0.5 ~= (100,50,25). Distinct from Check A -- proves
//   World reaches the vertex shader's normal transform and the pixel shader's own saturate()
//   clamp is genuinely exercised, not a no-op both times.
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

    // Ported 1:1 from CartoonEffect.Fx's LightingVertexShader.
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

    // Ported 1:1 from CartoonEffect.Fx's LambertPixelShader.
    const char* kFragSrc = R"(#version 300 es
precision mediump float;
in vec2 vTexCoord;
in float vLightAmount;
out vec4 FragColor;
uniform sampler2D texture1;
uniform bool TextureEnabled;
uniform vec3 DiffuseLight;
uniform vec3 AmbientLight;
void main() {
    vec4 color = TextureEnabled ? texture(texture1, vTexCoord) : vec4(0.0);
    color.rgb *= clamp(vLightAmount, 0.0, 1.0) * DiffuseLight + AmbientLight;
    FragColor = color;
}
)";
}

class EasyGLCartoonEffectLambertTest : public Game
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
            / ("cna_cartooneffect_lambert_test_" + std::to_string(
                   reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        WriteFile(root / "lambert.vert.glsl", kVertSrc);
        WriteFile(root / "lambert.frag.glsl", kFragSrc);
        WriteFile(root / "lambert.cnb", R"({
  "cnbVersion": 1,
  "type": "Effect",
  "vertex": "lambert.vert.glsl",
  "fragment": "lambert.frag.glsl"
})");

        getContentProperty().setRootDirectoryProperty(root.string());
        fxBase_ = getContentProperty().Load<std::shared_ptr<Effect>>("lambert");

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
        fx->SetUniformVec3("DiffuseLight", 0.5f, 0.5f, 0.5f);
        fx->SetUniformVec3("AmbientLight", 0.5f, 0.5f, 0.5f);
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
            std::printf("[FAIL] EasyGLCartoonEffectLambert: .cnb load or GLSL compile failed\n");
            Exit();
            return;
        }

        const Color a = DrawOnce(Matrix::getIdentityProperty());
        const Color b = DrawOnce(Matrix::CreateRotationY(MathHelper::Pi));

        const auto close = [](int got, int expected) { return got >= expected - 6 && got <= expected + 6; };
        const bool aOk = close(a.getRProperty(), 158) && close(a.getGProperty(), 79) && close(a.getBProperty(), 39);
        const bool bOk = close(b.getRProperty(), 100) && close(b.getGProperty(), 50) && close(b.getBProperty(), 25);

        std::printf("[%s] Check A (World=Identity): (%d,%d,%d) expected~=(158,79,39)\n",
                    aOk ? "PASS" : "FAIL", a.getRProperty(), a.getGProperty(), a.getBProperty());
        std::printf("[%s] Check B (World=RotateY180): (%d,%d,%d) expected~=(100,50,25)\n",
                    bOk ? "PASS" : "FAIL", b.getRProperty(), b.getGProperty(), b.getBProperty());

        result_ = (aOk && bOk) ? 0 : 1;
        Exit();
    }

public:
    EasyGLCartoonEffectLambertTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    EasyGLCartoonEffectLambertTest game;
    game.Run();
    return game.getResult();
}

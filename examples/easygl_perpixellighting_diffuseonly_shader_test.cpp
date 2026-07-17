// SPDX-License-Identifier: MS-PL
// Task 947 (Phase 78 rollout): HLSL->GLSL shader-conversion proof -- PerPixelLighting.fx's
// PerPixelDiffuse technique (2 of 5 effect/technique combinations `PerPixelLighting` sample
// cycles through -- see Task 947's own PerPixelDiffuseAndPhong row for the other 3, 1 done).
// Reuses PerPixelDiffuseVS verbatim (identical to the already-ported PerPixelDiffuseAndPhong
// technique's own vertex shader) with DiffuseOnlyPS (no specular term at all) instead of
// DiffuseAndPhongPS.
//
// FNA reference (`PerPixelLightingSample_4_0/PerPixelLighting/Content/PerPixelLighting.fx`,
// technique `PerPixelDiffuse` = `PerPixelDiffuseVS` + `DiffuseOnlyPS`):
//   float4 DiffuseOnlyPS(PixelShaderInputPerPixelDiffuse input) : COLOR
//   {
//       float3 directionToLight = normalize(lightPosition - input.WorldPosition);
//       float diffuseIntensity = saturate(dot(directionToLight, input.WorldNormal));
//       float4 diffuse = diffuseLightColor * diffuseIntensity;
//       float4 color = diffuse + ambientLightColor;
//       color.a = 1.0;
//       return color;
//   }
//
// Same geometry/light/camera setup as the PerPixelDiffuseAndPhong test (quad centred at the
// origin, so the centre pixel's interpolated WorldPosition is exactly (0,0,0) for any World;
// light (0,0,5), camera (0,0,3)) -- only the fragment shader differs (no specular term).
//
// Check A -- World=Identity: directionToLight=(0,0,1), worldNormal=(0,0,1),
//   diffuseIntensity=dot=1 -> diffuse=diffuseLightColor=(0.4,0.3,0.2). color=diffuse+ambient
//   = (0.4,0.3,0.2)+(0.1,0.05,0.02) = (0.5,0.35,0.22) ~= (128,89,56).
// Check B -- World=RotationY(180deg), same footprint, flips worldNormal to (0,0,-1):
//   diffuseIntensity=dot((0,0,1),(0,0,-1))=-1->clamped 0 -> diffuse=(0,0,0). color=ambient only
//   = (0.1,0.05,0.02) ~= (26,13,5). Distinct from Check A -- proves World reaches the shader.
//
// Exit code 0 = both PASS, 1 = either FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    void WriteFile(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream f(path, std::ios::binary);
        f << text;
    }

    // Identical to the already-ported PerPixelDiffuseAndPhong technique's own vertex shader --
    // both XNA techniques share the literal same PerPixelDiffuseVS function.
    const char* kVertSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
out vec3 vWorldNormal;
out vec3 vWorldPosition;
uniform mat4 World;
uniform mat4 View;
uniform mat4 Projection;
void main() {
    vec4 worldPos4 = World * vec4(aPosition, 1.0);
    vWorldPosition = worldPos4.xyz / worldPos4.w;
    vWorldNormal = mat3(World) * aNormal;
    gl_Position = Projection * View * worldPos4;
}
)";

    // Ported 1:1 from PerPixelLighting.fx's DiffuseOnlyPS.
    const char* kFragSrc = R"(#version 300 es
precision mediump float;
in vec3 vWorldNormal;
in vec3 vWorldPosition;
out vec4 FragColor;
uniform vec3 lightPosition;
uniform vec4 ambientLightColor;
uniform vec4 diffuseLightColor;
void main() {
    vec3 directionToLight = normalize(lightPosition - vWorldPosition);
    float diffuseIntensity = clamp(dot(directionToLight, vWorldNormal), 0.0, 1.0);
    vec4 diffuse = diffuseLightColor * diffuseIntensity;
    vec4 color = diffuse + ambientLightColor;
    color.a = 1.0;
    FragColor = color;
}
)";
}

class EasyGLPerPixelLightingDiffuseOnlyTest : public Game
{
    std::shared_ptr<Effect> fxBase_;
    std::unique_ptr<VertexBuffer> vb_;
    std::unique_ptr<IndexBuffer> ib_;
    bool done_   = false;
    int result_  = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        const auto root = std::filesystem::temp_directory_path()
            / ("cna_perpixellighting_diffuseonly_test_" + std::to_string(
                   reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        WriteFile(root / "diffuseonly.vert.glsl", kVertSrc);
        WriteFile(root / "diffuseonly.frag.glsl", kFragSrc);
        WriteFile(root / "diffuseonly.cnj", R"({
  "cnjVersion": 1,
  "type": "Effect",
  "vertex": "diffuseonly.vert.glsl",
  "fragment": "diffuseonly.frag.glsl"
})");

        getContentProperty().setRootDirectoryProperty(root.string());
        fxBase_ = getContentProperty().Load<std::shared_ptr<Effect>>("diffuseonly");

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
        fx->SetUniformVec3("lightPosition", 0.0f, 0.0f, 5.0f);
        fx->SetUniformVec4("ambientLightColor", 0.1f, 0.05f, 0.02f, 0.0f);
        fx->SetUniformVec4("diffuseLightColor", 0.4f, 0.3f, 0.2f, 0.0f);

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
            std::printf("[FAIL] EasyGLPerPixelLightingDiffuseOnly: .cnj load or GLSL compile failed\n");
            Exit();
            return;
        }

        const Color a = DrawOnce(Matrix::getIdentityProperty());
        const Color b = DrawOnce(Matrix::CreateRotationY(MathHelper::Pi));

        const auto close = [](int got, int expected) { return got >= expected - 6 && got <= expected + 6; };
        const bool aOk = close(a.getRProperty(), 128) && close(a.getGProperty(), 89) && close(a.getBProperty(), 56);
        const bool bOk = close(b.getRProperty(), 26) && close(b.getGProperty(), 13) && close(b.getBProperty(), 5);

        std::printf("[%s] Check A (World=Identity): (%d,%d,%d) expected~=(128,89,56)\n",
                    aOk ? "PASS" : "FAIL", a.getRProperty(), a.getGProperty(), a.getBProperty());
        std::printf("[%s] Check B (World=RotateY180): (%d,%d,%d) expected~=(26,13,5)\n",
                    bOk ? "PASS" : "FAIL", b.getRProperty(), b.getGProperty(), b.getBProperty());

        result_ = (aOk && bOk) ? 0 : 1;
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    EasyGLPerPixelLightingDiffuseOnlyTest game;
    game.Run();
    return game.getResult();
}

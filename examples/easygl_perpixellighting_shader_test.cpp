// SPDX-License-Identifier: MS-PL
// Task 947 (Phase 78 rollout): HLSL->GLSL shader-conversion proof -- PerPixelLighting.fx's
// PerPixelDiffuseAndPhong technique, the sample's own "best-looking" combination (ambient +
// per-pixel diffuse + per-pixel Phong specular, real light *position* not just direction). First
// real 3D (not SpriteBatch) shader conversion, using Task 1079's new ShaderEffect-drives-a-3D-draw
// capability -- PerPixelLighting.fx's own vertex format (Position+Normal only) fits the existing
// VertexPositionNormalTexture (stride 32) layout Task 1079 already supports.
//
// FNA reference (`PerPixelLightingSample_4_0/PerPixelLighting/Content/PerPixelLighting.fx`,
// `PerPixelDiffuseVS` + `DiffuseAndPhongPS`, technique `PerPixelDiffuseAndPhong`):
//   VertexShaderOutputPerPixelDiffuse PerPixelDiffuseVS(float3 position : POSITION, float3 normal : NORMAL)
//   {
//       output.Position = mul(float4(position, 1.0), mul(mul(world, view), projection));
//       output.WorldNormal = mul(normal, world);
//       float4 worldPosition = mul(float4(position, 1.0), world);
//       output.WorldPosition = worldPosition / worldPosition.w;
//   }
//   float4 DiffuseAndPhongPS(PixelShaderInputPerPixelDiffuse input) : COLOR
//   {
//       float3 directionToLight = normalize(lightPosition - input.WorldPosition);
//       float diffuseIntensity = saturate(dot(directionToLight, input.WorldNormal));
//       float4 diffuse = diffuseLightColor * diffuseIntensity;
//       float3 reflectionVector = normalize(reflect(-directionToLight, input.WorldNormal));
//       float3 directionToCamera = normalize(cameraPosition - input.WorldPosition);
//       float4 specular = specularLightColor * specularIntensity *
//                          pow(saturate(dot(reflectionVector, directionToCamera)), specularPower);
//       float4 color = specular + diffuse + ambientLightColor;
//       color.a = 1.0;
//       return color;
//   }
//
// Ported 1:1 below, including the original's own non-obvious characteristic: `input.WorldNormal`
// is used RAW in both the diffuse dot product and the specular reflect() call -- never
// re-normalized after per-vertex-to-per-pixel interpolation (unlike e.g. NormalMapping.fx, which
// does normalize). Preserved exactly, not "fixed" -- this project ports HLSL behavior faithfully,
// it doesn't correct the original author's own choices.
//
// World/View/Projection use Task 1079's new ShaderEffect IEffectMatrices properties; `mul(v,M)`
// (HLSL row-vector) translates to `M * v` (GLSL column-vector) throughout, matching the exact
// convention Task 1079's own EasyGL_ShaderEffect_3D test already proved correct.
//
// Test geometry: a small quad (VertexPositionNormalTexture, local normal (0,0,1)) centered at the
// origin -- centre pixel's interpolated WorldPosition is exactly (0,0,0) regardless of World
// (rotations here are all about the origin), letting every value below be hand-computed exactly
// rather than approximated. Light at (0,0,5), camera (and `cameraPosition` uniform) at (0,0,3).
//
// Check A -- World=Identity: worldNormal=(0,0,1). directionToLight=(0,0,1) (light straight ahead).
//   diffuseIntensity=dot((0,0,1),(0,0,1))=1 -> diffuse=diffuseLightColor.
//   reflect(-directionToLight, worldNormal)=reflect((0,0,-1),(0,0,1))=(0,0,1) -> reflectionVector=(0,0,1).
//   directionToCamera=(0,0,1). dot(reflectionVector,directionToCamera)=1 -> specular=specularLightColor*specularIntensity*1.
//   color = specular+diffuse+ambient = (0.3,0.3,0.3)+(0.4,0.3,0.2)+(0.1,0.05,0.02) = (0.8,0.65,0.52) ~= (204,166,133).
// Check B -- World=RotationY(180deg), same on-screen footprint (X-symmetric quad), worldNormal
//   flips to (0,0,-1): diffuseIntensity=dot((0,0,1),(0,0,-1))=-1->clamped 0 -> diffuse=(0,0,0).
//   reflect((0,0,-1),(0,0,-1)): dot(N,I)=1 -> reflect=I-2N=(0,0,-1)-(0,0,-2)=(0,0,1) -> same specular=(0.3,0.3,0.3).
//   color = (0.3,0.3,0.3)+(0,0,0)+(0.1,0.05,0.02) = (0.4,0.35,0.32) ~= (102,89,82).
//   Distinct from Check A -- proves World genuinely reaches the vertex shader's normal transform.
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

    // Ported 1:1 from PerPixelLighting.fx's DiffuseAndPhongPS -- see this file's own header
    // comment for the "WorldNormal used raw, never re-normalized" nuance preserved here.
    const char* kFragSrc = R"(#version 300 es
precision mediump float;
in vec3 vWorldNormal;
in vec3 vWorldPosition;
out vec4 FragColor;
uniform vec3 lightPosition;
uniform vec4 ambientLightColor;
uniform vec4 diffuseLightColor;
uniform vec4 specularLightColor;
uniform float specularPower;
uniform float specularIntensity;
uniform vec3 cameraPosition;
void main() {
    vec3 directionToLight = normalize(lightPosition - vWorldPosition);
    float diffuseIntensity = clamp(dot(directionToLight, vWorldNormal), 0.0, 1.0);
    vec4 diffuse = diffuseLightColor * diffuseIntensity;

    vec3 reflectionVector = normalize(reflect(-directionToLight, vWorldNormal));
    vec3 directionToCamera = normalize(cameraPosition - vWorldPosition);
    vec4 specular = specularLightColor * specularIntensity *
                     pow(clamp(dot(reflectionVector, directionToCamera), 0.0, 1.0), specularPower);

    vec4 color = specular + diffuse + ambientLightColor;
    color.a = 1.0;
    FragColor = color;
}
)";
}

class EasyGLPerPixelLightingShaderTest : public Game
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
            / ("cna_perpixellighting_shader_test_" + std::to_string(
                   reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        WriteFile(root / "perpixellighting.vert.glsl", kVertSrc);
        WriteFile(root / "perpixellighting.frag.glsl", kFragSrc);
        WriteFile(root / "perpixellighting.cnb", R"({
  "cnbVersion": 1,
  "type": "Effect",
  "vertex": "perpixellighting.vert.glsl",
  "fragment": "perpixellighting.frag.glsl"
})");

        getContentProperty().setRootDirectoryProperty(root.string());
        fxBase_ = getContentProperty().Load<std::shared_ptr<Effect>>("perpixellighting");

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
        fx->SetUniformVec3("cameraPosition", 0.0f, 0.0f, 3.0f);
        fx->SetUniformVec4("ambientLightColor", 0.1f, 0.05f, 0.02f, 0.0f);
        fx->SetUniformVec4("diffuseLightColor", 0.4f, 0.3f, 0.2f, 0.0f);
        fx->SetUniformVec4("specularLightColor", 1.0f, 1.0f, 1.0f, 0.0f);
        fx->SetUniformFloat("specularIntensity", 0.3f);
        fx->SetUniformFloat("specularPower", 20.0f);

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
            std::printf("[FAIL] EasyGLPerPixelLightingShader: .cnb load or GLSL compile failed\n");
            Exit();
            return;
        }

        const Color a = DrawOnce(Matrix::getIdentityProperty());
        const Color b = DrawOnce(Matrix::CreateRotationY(MathHelper::Pi));

        const auto close = [](int got, int expected) { return got >= expected - 6 && got <= expected + 6; };
        const bool aOk = close(a.getRProperty(), 204) && close(a.getGProperty(), 166) && close(a.getBProperty(), 133);
        const bool bOk = close(b.getRProperty(), 102) && close(b.getGProperty(), 89) && close(b.getBProperty(), 82);

        std::printf("[%s] Check A (World=Identity): (%d,%d,%d) expected~=(204,166,133)\n",
                    aOk ? "PASS" : "FAIL", a.getRProperty(), a.getGProperty(), a.getBProperty());
        std::printf("[%s] Check B (World=RotateY180): (%d,%d,%d) expected~=(102,89,82)\n",
                    bOk ? "PASS" : "FAIL", b.getRProperty(), b.getGProperty(), b.getBProperty());

        result_ = (aOk && bOk) ? 0 : 1;
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    EasyGLPerPixelLightingShaderTest game;
    game.Run();
    return game.getResult();
}

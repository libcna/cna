// SPDX-License-Identifier: MS-PL
// Task 947 (Phase 78 rollout): HLSL->GLSL shader-conversion proof -- PerPixelLighting.fx's
// PerVertexDiffuseAndPerPixelPhong technique (3 of 5 effect/technique combinations
// `PerPixelLighting` sample cycles through). A hybrid: diffuse+ambient computed once per vertex
// (interpolated as a COLOR varying), specular recomputed per pixel from interpolated
// WorldNormal/WorldPosition -- demonstrates the "faceted diffuse, smooth specular" middle ground
// between the sample's fully-per-vertex and fully-per-pixel techniques.
//
// FNA reference (`PerPixelLightingSample_4_0/PerPixelLighting/Content/PerPixelLighting.fx`,
// technique `PerVertexDiffuseAndPerPixelPhong` = `PerVertexDiffuseVS` + `PhongPS`):
//   VertexShaderOutputPerVertexDiffuse PerVertexDiffuseVS(float3 position : POSITION, float3 normal : NORMAL)
//   {
//       output.Position = mul(float4(position, 1.0), mul(mul(world, view), projection));
//       output.WorldNormal = mul(normal, world);
//       float4 worldPosition = mul(float4(position, 1.0), world);
//       output.WorldPosition = worldPosition / worldPosition.w;
//       float3 directionToLight = normalize(lightPosition - output.WorldPosition);
//       float diffuseIntensity = saturate(dot(directionToLight, output.WorldNormal));
//       float4 diffuse = diffuseLightColor * diffuseIntensity;
//       output.Color = diffuse + ambientLightColor;
//   }
//   float4 PhongPS(PixelShaderInputPerVertexDiffuse input) : COLOR
//   {
//       float3 directionToLight = normalize(lightPosition - input.WorldPosition);
//       float3 reflectionVector = normalize(reflect(-directionToLight, input.WorldNormal));
//       float3 directionToCamera = normalize(cameraPosition - input.WorldPosition);
//       float4 specular = specularLightColor * specularIntensity *
//                          pow(saturate(dot(reflectionVector, directionToCamera)), specularPower);
//       float4 color = input.Color + specular;
//       color.a = 1.0;
//       return color;
//   }
//
// Test geometry/light/camera identical to the sibling PerPixelDiffuseAndPhong/PerPixelDiffuse
// tests (quad centred at the origin, light (0,0,5), camera (0,0,3)) -- but unlike those, the
// DIFFUSE term here is evaluated once per VERTEX (at each of the quad's 4 corners, not the
// centre), so its exact value differs slightly from a fully-per-pixel diffuse evaluated at the
// centre. Because this quad's 4 corners are symmetric around the origin relative to the light on
// the Z axis, all 4 corners compute the IDENTICAL diffuse value -- so the interpolated colour at
// the centre pixel equals that one shared per-corner value exactly (no averaging discrepancy).
//
// Check A -- World=Identity: each corner's directionToLight = normalize((0,0,5)-corner); by the
//   quad's symmetry this has the same z-component (0.99015) at all 4 corners regardless of their
//   (x,y) sign, so diffuseIntensity = 0.99015 (not exactly 1, since corners aren't directly under
//   the light the way the centre is) -> diffuse = diffuseLightColor*0.99015 = (0.39606,0.29704,0.19803).
//   Specular is evaluated per-PIXEL at the exact centre (WorldPosition=(0,0,0)), same as the
//   PerPixelDiffuseAndPhong test's own Check A: (0.3,0.3,0.3). color = specular+diffuse+ambient
//   = (0.3+0.39606+0.1, 0.3+0.29704+0.05, 0.3+0.19803+0.02) ~= (203,165,132).
// Check B -- World=RotationY(180deg), same footprint: per-vertex diffuseIntensity clamps to 0 at
//   every corner (worldNormal flips, same as the sibling tests), so diffuse=(0,0,0); per-pixel
//   specular at the centre is unchanged (0.3,0.3,0.3), same symmetric-geometry reasoning as the
//   PerPixelDiffuseAndPhong test. color = (0.3,0.3,0.3)+(0,0,0)+(0.1,0.05,0.02) ~= (102,89,82).
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

    // Ported 1:1 from PerPixelLighting.fx's PerVertexDiffuseVS -- diffuse+ambient computed here,
    // per vertex, and interpolated as vColor to the fragment shader.
    const char* kVertSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
out vec3 vWorldNormal;
out vec3 vWorldPosition;
out vec4 vColor;
uniform mat4 World;
uniform mat4 View;
uniform mat4 Projection;
uniform vec3 lightPosition;
uniform vec4 ambientLightColor;
uniform vec4 diffuseLightColor;
void main() {
    vec4 worldPos4 = World * vec4(aPosition, 1.0);
    vWorldPosition = worldPos4.xyz / worldPos4.w;
    vWorldNormal = mat3(World) * aNormal;
    gl_Position = Projection * View * worldPos4;

    vec3 directionToLight = normalize(lightPosition - vWorldPosition);
    float diffuseIntensity = clamp(dot(directionToLight, vWorldNormal), 0.0, 1.0);
    vColor = diffuseLightColor * diffuseIntensity + ambientLightColor;
}
)";

    // Ported 1:1 from PerPixelLighting.fx's PhongPS. precision must be highp, not the more usual
    // mediump, to match the vertex shader's own highp declaration of `lightPosition` -- GLSL ES
    // requires matching precision for a uniform shared across both stages of the same linked
    // program (confirmed empirically: mediump here produces a real link error, "declarations for
    // uniform `lightPosition` have mismatching precision qualifiers", since PhongPS re-reads
    // `lightPosition` for its own directionToLight calculation instead of receiving it
    // pre-computed from the vertex shader).
    const char* kFragSrc = R"(#version 300 es
precision highp float;
in vec3 vWorldNormal;
in vec3 vWorldPosition;
in vec4 vColor;
out vec4 FragColor;
uniform vec3 lightPosition;
uniform vec3 cameraPosition;
uniform vec4 specularLightColor;
uniform float specularPower;
uniform float specularIntensity;
void main() {
    vec3 directionToLight = normalize(lightPosition - vWorldPosition);
    vec3 reflectionVector = normalize(reflect(-directionToLight, vWorldNormal));
    vec3 directionToCamera = normalize(cameraPosition - vWorldPosition);
    vec4 specular = specularLightColor * specularIntensity *
                     pow(clamp(dot(reflectionVector, directionToCamera), 0.0, 1.0), specularPower);
    vec4 color = vColor + specular;
    color.a = 1.0;
    FragColor = color;
}
)";
}

class EasyGLPerPixelLightingVertexDiffusePixelPhongTest : public Game
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
            / ("cna_perpixellighting_vertexdiffuse_pixelphong_test_" + std::to_string(
                   reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        WriteFile(root / "vdpp.vert.glsl", kVertSrc);
        WriteFile(root / "vdpp.frag.glsl", kFragSrc);
        WriteFile(root / "vdpp.cnj", R"({
  "cnjVersion": 1,
  "type": "Effect",
  "vertex": "vdpp.vert.glsl",
  "fragment": "vdpp.frag.glsl"
})");

        getContentProperty().setRootDirectoryProperty(root.string());
        fxBase_ = getContentProperty().Load<std::shared_ptr<Effect>>("vdpp");

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
            std::printf("[FAIL] EasyGLPerPixelLightingVertexDiffusePixelPhong: .cnj load or GLSL compile failed\n");
            Exit();
            return;
        }

        const Color a = DrawOnce(Matrix::getIdentityProperty());
        const Color b = DrawOnce(Matrix::CreateRotationY(MathHelper::Pi));

        const auto close = [](int got, int expected) { return got >= expected - 6 && got <= expected + 6; };
        const bool aOk = close(a.getRProperty(), 203) && close(a.getGProperty(), 165) && close(a.getBProperty(), 132);
        const bool bOk = close(b.getRProperty(), 102) && close(b.getGProperty(), 89) && close(b.getBProperty(), 82);

        std::printf("[%s] Check A (World=Identity): (%d,%d,%d) expected~=(203,165,132)\n",
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
    EasyGLPerPixelLightingVertexDiffusePixelPhongTest game;
    game.Run();
    return game.getResult();
}

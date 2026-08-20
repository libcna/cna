// SPDX-License-Identifier: MS-PL
// Task 947 (Phase 78 rollout): HLSL->GLSL shader-conversion proof -- VertexLighting.fx's
// PerVertexDiffuseAndPhong technique (5 of 5 effect/technique combinations `PerPixelLighting`
// sample cycles through -- with this row, all 5 are ported; see plans/plan_graphics.md Task 947's own
// PerPixelDiffuseAndPhong row for the running total). Diffuse + Phong specular + ambient all
// computed entirely in the vertex shader and interpolated as a plain COLOR varying -- the
// sample's own fully-per-vertex "faceted specular highlight" demonstration technique.
//
// FNA reference (`PerPixelLightingSample_4_0/PerPixelLighting/Content/VertexLighting.fx`,
// technique `PerVertexDiffuseAndPhong` = `VertexDiffuseAndPhong` + `SimplePixelShader`):
//   VertexShaderOutput VertexDiffuseAndPhong(float3 position : POSITION, float3 normal : NORMAL)
//   {
//       output.Position = mul(float4(position, 1.0), mul(mul(world, view), projection));
//       float3 worldNormal = mul(normal, world);
//       float4 worldPosition = mul(float4(position, 1.0), world);
//       worldPosition = worldPosition / worldPosition.w;
//       float3 directionToLight = normalize(lightPosition - worldPosition.xyz);
//       float diffuseIntensity = saturate(dot(directionToLight, worldNormal));
//       float4 diffuse = diffuseLightColor * diffuseIntensity;
//       float3 reflectionVector = normalize(reflect(-directionToLight, worldNormal));
//       float3 directionToCamera = normalize(cameraPosition - worldPosition.xyz);
//       float4 specular = specularLightColor * specularIntensity *
//                          pow(saturate(dot(reflectionVector, directionToCamera)), specularPower);
//       output.Color = specular + diffuse + ambientLightColor;
//       output.Color.a = 1.0;
//   }
//   float4 SimplePixelShader(PixelShaderInput input) : COLOR { return input.Color; }
//
// Test geometry/light/camera identical to the sibling tests (quad centred at the origin, light
// (0,0,5), camera (0,0,3)) -- but `specularPower` is deliberately set to 1 here (not 20 like the
// per-pixel-specular sibling tests), so the specular term reduces to a plain dot product instead
// of needing a transcendental pow() in the by-hand derivation below. Every term here (diffuse AND
// specular) is evaluated per-VERTEX, at the quad's 4 corners rather than its centre, and unlike
// the centre-pixel case the corner-to-light/corner-to-camera directions are NOT axis-aligned --
// worked out via exact vector algebra (not approximated) using the corner (0.5,0.5,0):
//   directionToLight  = (-0.5,-0.5,5)/sqrt(25.5)
//   reflect(-directionToLight, worldNormal=(0,0,1)) = (0.5,0.5,5)/sqrt(25.5)  [already unit length]
//   directionToCamera = (-0.5,-0.5,3)/sqrt(9.5)
//   dot(reflectionVector, directionToCamera) = 14.5 / sqrt(25.5*9.5) = 14.5/sqrt(242.25) ~= 0.9316
// By the quad's symmetry (all 4 corners equidistant from the light/camera on the Z axis) every
// corner computes this same value, so the interpolated centre-pixel colour equals it exactly.
//
// Check A -- World=Identity: diffuseIntensity=0.99015 (same per-corner value as the sibling
//   PerVertexDiffuse test) -> diffuse=(0.39606,0.29704,0.19803). specular (specularPower=1) =
//   specularLightColor*0.3*0.9316 = (0.27948,0.27948,0.27948). color = specular+diffuse+ambient
//   = (0.279+0.396+0.1, 0.279+0.297+0.05, 0.279+0.198+0.02) ~= (198,160,127).
// Check B -- World=RotationY(180deg), same footprint: worldNormal flips at every corner ->
//   diffuseIntensity clamps to 0 -> diffuse=(0,0,0); the specular dot product comes out to the
//   SAME 0.9316 (both -directionToLight and worldNormal flip sign consistently in reflect()'s
//   formula, cancelling out) -> specular unchanged = (0.27948,...). color = specular+0+ambient
//   ~= (97,84,76). Distinct from Check A -- proves World reaches the vertex shader.
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

    // Ported 1:1 from VertexLighting.fx's VertexDiffuseAndPhong -- all lighting (diffuse AND
    // specular) computed here; the fragment shader is a pure passthrough (SimplePixelShader).
    const char* kVertSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
out vec4 vColor;
uniform mat4 World;
uniform mat4 View;
uniform mat4 Projection;
uniform vec3 lightPosition;
uniform vec3 cameraPosition;
uniform vec4 ambientLightColor;
uniform vec4 diffuseLightColor;
uniform vec4 specularLightColor;
uniform float specularPower;
uniform float specularIntensity;
void main() {
    vec4 worldPos4 = World * vec4(aPosition, 1.0);
    vec3 worldPosition = worldPos4.xyz / worldPos4.w;
    vec3 worldNormal = mat3(World) * aNormal;
    gl_Position = Projection * View * worldPos4;

    vec3 directionToLight = normalize(lightPosition - worldPosition);
    float diffuseIntensity = clamp(dot(directionToLight, worldNormal), 0.0, 1.0);
    vec4 diffuse = diffuseLightColor * diffuseIntensity;

    vec3 reflectionVector = normalize(reflect(-directionToLight, worldNormal));
    vec3 directionToCamera = normalize(cameraPosition - worldPosition);
    vec4 specular = specularLightColor * specularIntensity *
                     pow(clamp(dot(reflectionVector, directionToCamera), 0.0, 1.0), specularPower);

    vColor = specular + diffuse + ambientLightColor;
    vColor.a = 1.0;
}
)";

    // Ported 1:1 from VertexLighting.fx's SimplePixelShader.
    const char* kFragSrc = R"(#version 300 es
precision mediump float;
in vec4 vColor;
out vec4 FragColor;
void main() {
    FragColor = vColor;
}
)";
}

class EasyGLVertexLightingDiffusePhongTest : public Game
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
            / ("cna_vertexlighting_diffusephong_test_" + std::to_string(
                   reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        WriteFile(root / "vldp.vert.glsl", kVertSrc);
        WriteFile(root / "vldp.frag.glsl", kFragSrc);
        WriteFile(root / "vldp.cnj", R"({
  "cnjVersion": 1,
  "type": "Effect",
  "vertex": "vldp.vert.glsl",
  "fragment": "vldp.frag.glsl"
})");

        getContentProperty().setRootDirectoryProperty(root.string());
        fxBase_ = getContentProperty().Load<std::shared_ptr<Effect>>("vldp");

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
        fx->SetUniformFloat("specularPower", 1.0f);

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
            std::printf("[FAIL] EasyGLVertexLightingDiffusePhong: .cnj load or GLSL compile failed\n");
            Exit();
            return;
        }

        const Color a = DrawOnce(Matrix::getIdentityProperty());
        const Color b = DrawOnce(Matrix::CreateRotationY(MathHelper::Pi));

        const auto close = [](int got, int expected) { return got >= expected - 8 && got <= expected + 8; };
        const bool aOk = close(a.getRProperty(), 198) && close(a.getGProperty(), 160) && close(a.getBProperty(), 127);
        const bool bOk = close(b.getRProperty(), 97) && close(b.getGProperty(), 84) && close(b.getBProperty(), 76);

        std::printf("[%s] Check A (World=Identity): (%d,%d,%d) expected~=(198,160,127)\n",
                    aOk ? "PASS" : "FAIL", a.getRProperty(), a.getGProperty(), a.getBProperty());
        std::printf("[%s] Check B (World=RotateY180): (%d,%d,%d) expected~=(97,84,76)\n",
                    bOk ? "PASS" : "FAIL", b.getRProperty(), b.getGProperty(), b.getBProperty());

        result_ = (aOk && bOk) ? 0 : 1;
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    EasyGLVertexLightingDiffusePhongTest game;
    game.Run();
    return game.getResult();
}

// SPDX-License-Identifier: MS-PL
// Task 947 (Phase 78 rollout): HLSL->GLSL shader-conversion proof -- VertexLightingSample's OWN
// `VertexLighting.fx` (technique `VertexLighting` = `DiffuseLighting` + `SimplePixelShader`).
// Not to be confused with the same-named `VertexLighting.fx` already ported for the
// `PerPixelLighting` sample (see plans/plan_graphics.md Task 947's own row) -- confirmed via `diff`
// these are genuinely different files: this one is a simple single-directional-light model
// (`lightDirection`/`lightColor`), not the point-light diffuse+Phong model already ported.
//
// FNA reference (`VertexLightingSample_4_0/VertexLighting/Content/VertexLighting.fx`):
//   VertexShaderOutput DiffuseLighting(float3 position : POSITION, float3 normal : NORMAL)
//   {
//       output.Position = mul(float4(position, 1.0), mul(mul(world, view), projection));
//       float3 worldNormal = mul(normal, world);
//       float diffuseIntensity = saturate(dot(-lightDirection, worldNormal));
//       float4 diffuseColor = lightColor * diffuseIntensity;
//       output.Color = diffuseColor + ambientColor;
//       diffuseColor.a = 1.0;   // <-- see this file's own header comment below
//       return output;
//   }
//   float4 SimplePixelShader(PixelShaderInput input) : COLOR { return input.Color; }
//
// **A real, faithfully-preserved bug in the ORIGINAL HLSL, not fixed here**: the comment above
// `diffuseColor.a = 1.0;` claims this forces the output alpha to fully opaque, but
// `output.Color` was already assigned on the PRECEDING line from the pre-mutation `diffuseColor`
// -- the `.a = 1.0` assignment mutates a local copy that is never read again, dead code. The
// sample's own real, rendered alpha is therefore `lightColor.a*diffuseIntensity + ambientColor.a`
// (whatever that naturally computes to), NOT clamped to 1.0 despite the comment's own claim. This
// project ports HLSL behaviour faithfully, including the original author's own bugs -- not
// "fixed" here. Check A/B below deliberately choose non-1.0-summing alpha values specifically to
// make this preserved bug independently observable in the test's own alpha channel.
//
// Test geometry/camera: same quad-centred-at-origin setup as the sibling PerPixelLighting-family
// tests, but a directional (not positional) light this time, so no light-distance/quad-size
// approximation issue exists at all -- every vertex computes the exact same value regardless of
// quad size, matching a true directional light's physical behaviour.
//
// Check A -- World=Identity: worldNormal=(0,0,1). -lightDirection=(0,0,1) (light pointing at
//   -Z, i.e. shining onto a surface facing +Z). dot=1 -> diffuseIntensity=1 ->
//   diffuseColor=lightColor=(0.5,0.4,0.3,0.3). color=diffuseColor+ambientColor
//   = (0.6,0.45,0.32,0.5) ~= (153,115,82,128).
// Check B -- World=RotationY(180deg), same on-screen footprint, flips worldNormal to (0,0,-1):
//   dot((0,0,1),(0,0,-1))=-1 -> clamped 0 -> diffuseIntensity=0 -> diffuseColor=(0,0,0,0)
//   (alpha scales by diffuseIntensity too, component-wise). color = ambientColor only
//   = (0.1,0.05,0.02,0.2) ~= (26,13,5,51). Distinct RGB (proves World reaches the shader) AND
//   distinct alpha (128 vs 51 -- proves the alpha isn't silently clamped to 1.0, confirming the
//   dead-code bug is preserved, not accidentally "fixed" by this port).
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

    // Ported 1:1 from VertexLighting.fx's DiffuseLighting -- all lighting computed here; the
    // fragment shader is a pure passthrough (SimplePixelShader). Note: NOT the same diffuseColor
    // formula as the already-ported PerPixelLighting-sample VertexLighting.fx -- this one uses a
    // directional light (-lightDirection), no light *position*/distance term at all.
    const char* kVertSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
out vec4 vColor;
uniform mat4 World;
uniform mat4 View;
uniform mat4 Projection;
uniform vec3 lightDirection;
uniform vec4 lightColor;
uniform vec4 ambientColor;
void main() {
    vec4 worldPos4 = World * vec4(aPosition, 1.0);
    vec3 worldNormal = mat3(World) * aNormal;
    gl_Position = Projection * View * worldPos4;

    float diffuseIntensity = clamp(dot(-lightDirection, worldNormal), 0.0, 1.0);
    vec4 diffuseColor = lightColor * diffuseIntensity;
    vColor = diffuseColor + ambientColor;
    // Deliberately NOT clamping vColor.a to 1.0 here -- the original HLSL's own `diffuseColor.a =
    // 1.0;` line is dead code (mutates a local copy after output.Color was already assigned), see
    // this file's own header comment. Faithfully preserved, not "fixed".
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

class EasyGLVertexLightingDirectionalTest : public Game
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
            / ("cna_vertexlighting_directional_test_" + std::to_string(
                   reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        WriteFile(root / "vldir.vert.glsl", kVertSrc);
        WriteFile(root / "vldir.frag.glsl", kFragSrc);
        WriteFile(root / "vldir.cnj", R"({
  "cnjVersion": 1,
  "type": "Effect",
  "vertex": "vldir.vert.glsl",
  "fragment": "vldir.frag.glsl"
})");

        getContentProperty().setRootDirectoryProperty(root.string());
        fxBase_ = getContentProperty().Load<std::shared_ptr<Effect>>("vldir");

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
        fx->SetUniformVec3("lightDirection", 0.0f, 0.0f, -1.0f);
        fx->SetUniformVec4("lightColor", 0.5f, 0.4f, 0.3f, 0.3f);
        fx->SetUniformVec4("ambientColor", 0.1f, 0.05f, 0.02f, 0.2f);

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
            std::printf("[FAIL] EasyGLVertexLightingDirectional: .cnj load or GLSL compile failed\n");
            Exit();
            return;
        }

        const Color a = DrawOnce(Matrix::getIdentityProperty());
        const Color b = DrawOnce(Matrix::CreateRotationY(MathHelper::Pi));

        const auto close = [](int got, int expected) { return got >= expected - 6 && got <= expected + 6; };
        const bool aOk = close(a.getRProperty(), 153) && close(a.getGProperty(), 115)
                       && close(a.getBProperty(), 82) && close(a.getAProperty(), 128);
        const bool bOk = close(b.getRProperty(), 26) && close(b.getGProperty(), 13)
                       && close(b.getBProperty(), 5) && close(b.getAProperty(), 51);

        std::printf("[%s] Check A (World=Identity): (%d,%d,%d,%d) expected~=(153,115,82,128)\n",
                    aOk ? "PASS" : "FAIL",
                    a.getRProperty(), a.getGProperty(), a.getBProperty(), a.getAProperty());
        std::printf("[%s] Check B (World=RotateY180): (%d,%d,%d,%d) expected~=(26,13,5,51)\n",
                    bOk ? "PASS" : "FAIL",
                    b.getRProperty(), b.getGProperty(), b.getBProperty(), b.getAProperty());

        result_ = (aOk && bOk) ? 0 : 1;
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    EasyGLVertexLightingDirectionalTest game;
    game.Run();
    return game.getResult();
}

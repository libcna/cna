// SPDX-License-Identifier: MS-PL
// Task 947 (Phase 78 rollout): HLSL->GLSL shader-conversion proof -- Distorters.fx's `PullIn`
// technique (used by DistortionSample's "Dude" distorter, `Game.cs:78-86`). View-space-normal-
// based displacement (surfaces facing the camera pull inward more than grazing-angle surfaces) --
// the last of Distorters.fx's 3 actually-used techniques (`ZeroDisplacement` is unused by the
// real sample, "provided for reference" per the shader's own comment, not ported).
//
// FNA reference (`DistortionSample_4_0/Distortion/Content/Distorters.fx`):
//   struct PositionNormal { float4 Position : POSITION; float3 Normal : NORMAL; };
//   struct PositionDisplacement { float4 Position : POSITION; float2 Displacement : TEXCOORD; };
//   PositionDisplacement PullIn_VertexShader(PositionNormal input)
//   {
//       output.Position = mul(input.Position, WorldViewProjection);
//       float3 normalWV = mul(input.Normal, WorldView);
//       normalWV.y = -normalWV.y;
//       float amount = dot(normalWV, float3(0,0,1)) * DistortionScale;
//       output.Displacement = float2(.5,.5) + float2(amount * normalWV.xy);
//       return output;
//   }
//   float4 DisplacementPassthrough_PixelShader(float2 displacement : TEXCOORD) : COLOR
//   { return float4(displacement, 0, 1); }
//
// Uses `WorldView` (World*View, no Projection) as a 2nd pre-combined uniform alongside
// `WorldViewProjection` -- both computed on the C++ side, same row-vector-product convention as
// the sibling DisplacementMapped test.
//
// Test geometry: View=Projection=Identity (only `World` varies between checks, isolating the
// normal-transform formula from camera-setup complexity) -- so WorldView reduces to World alone.
//
// Check A -- World=RotationY(45deg): local normal (0,0,1) rotates to normalWV=(sin45,0,cos45) =
//   (0.70711,0,0.70711) (Y-component 0, so the shader's own `normalWV.y=-normalWV.y` is a no-op
//   here). amount = dot(normalWV,(0,0,1))*DistortionScale = 0.70711*0.2 = 0.141421.
//   Displacement = (0.5,0.5) + amount*(normalWV.x,normalWV.y) = (0.5,0.5) + 0.141421*(0.70711,0)
//   = (0.5+0.1, 0.5+0) = (0.6,0.5) ~= (153,128,0,255) (0.141421*0.70711 = 0.1 exactly, by
//   construction -- sin45*cos45*0.2 = 0.5*sin90*0.2 = 0.1).
// Check B -- World=RotationY(-45deg): normalWV=(-0.70711,0,0.70711) (x-component sign flips, z
//   unaffected). amount unchanged (0.141421, dot only reads the z-component). Displacement =
//   (0.5,0.5) + 0.141421*(-0.70711,0) = (0.4,0.5) ~= (102,128,0,255). Distinct R channel from
//   Check A -- proves World reaches the normal transform and its sign genuinely affects the
//   result, not just its magnitude.
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

    // Ported 1:1 from PullIn_VertexShader.
    const char* kVertSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
out vec2 vDisplacement;
uniform mat4 WorldViewProjection;
uniform mat4 WorldView;
uniform float DistortionScale;
void main() {
    gl_Position = WorldViewProjection * vec4(aPosition, 1.0);
    vec3 normalWV = mat3(WorldView) * aNormal;
    normalWV.y = -normalWV.y;
    float amount = dot(normalWV, vec3(0.0, 0.0, 1.0)) * DistortionScale;
    vDisplacement = vec2(0.5, 0.5) + amount * normalWV.xy;
}
)";

    // Ported 1:1 from DisplacementPassthrough_PixelShader.
    const char* kFragSrc = R"(#version 300 es
precision mediump float;
in vec2 vDisplacement;
out vec4 FragColor;
void main() {
    FragColor = vec4(vDisplacement, 0.0, 1.0);
}
)";
}

class EasyGLDistortersPullInTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
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
            / ("cna_distorters_pullin_test_" + std::to_string(
                   reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        WriteFile(root / "pi.vert.glsl", kVertSrc);
        WriteFile(root / "pi.frag.glsl", kFragSrc);
        WriteFile(root / "pi.cnb", R"({
  "cnbVersion": 1,
  "type": "Effect",
  "vertex": "pi.vert.glsl",
  "fragment": "pi.frag.glsl"
})");

        getContentProperty().setRootDirectoryProperty(root.string());
        fxBase_ = getContentProperty().Load<std::shared_ptr<Effect>>("pi");

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

        // View = Projection = Identity -- WorldView reduces to World alone, isolating the normal
        // transform formula from camera-setup complexity. The quad's local -0.5..0.5 coordinates
        // already land directly in NDC with no additional camera needed.
        const Matrix worldView = world;
        const Matrix wvp = world;

        auto* fx = dynamic_cast<ShaderEffect*>(fxBase_.get());
        fx->Apply();
        float wvpCM[16], wvCM[16];
        wvp.ToColumnMajor(wvpCM);
        worldView.ToColumnMajor(wvCM);
        fx->SetUniformMat4("WorldViewProjection", wvpCM);
        fx->SetUniformMat4("WorldView", wvCM);
        fx->SetUniformFloat("DistortionScale", 0.2f);

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
            std::printf("[FAIL] EasyGLDistortersPullIn: .cnb load or GLSL compile failed\n");
            Exit();
            return;
        }

        const Color a = DrawOnce(Matrix::CreateRotationY(MathHelper::PiOver4));
        const Color b = DrawOnce(Matrix::CreateRotationY(-MathHelper::PiOver4));

        const auto close = [](int got, int expected) { return got >= expected - 6 && got <= expected + 6; };
        const bool aOk = close(a.getRProperty(), 153) && close(a.getGProperty(), 128) && a.getBProperty() == 0;
        const bool bOk = close(b.getRProperty(), 102) && close(b.getGProperty(), 128) && b.getBProperty() == 0;

        std::printf("[%s] Check A (World=RotateY(+45)): (%d,%d,%d) expected~=(153,128,0)\n",
                    aOk ? "PASS" : "FAIL", a.getRProperty(), a.getGProperty(), a.getBProperty());
        std::printf("[%s] Check B (World=RotateY(-45)): (%d,%d,%d) expected~=(102,128,0)\n",
                    bOk ? "PASS" : "FAIL", b.getRProperty(), b.getGProperty(), b.getBProperty());

        result_ = (aOk && bOk) ? 0 : 1;
        Exit();
    }

public:
    EasyGLDistortersPullInTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    EasyGLDistortersPullInTest game;
    game.Run();
    return game.getResult();
}

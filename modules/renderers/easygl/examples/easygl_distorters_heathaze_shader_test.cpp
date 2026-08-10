// SPDX-License-Identifier: MS-PL
// Task 947 (Phase 78 rollout): HLSL->GLSL shader-conversion proof -- Distorters.fx's `HeatHaze`
// technique (used by DistortionSample's "Cylinder" distorter, `Game.cs:87-95`). Procedurally
// GENERATES a distortion-map value from screen position + time, rather than sampling any texture
// -- part of the real pipeline's "render each distorter's own displacement into the shared
// DistortionMap render target" pass (composited later by `Distort.fx`, already ported).
//
// FNA reference (`DistortionSample_4_0/Distortion/Content/Distorters.fx`):
//   struct PositionPosition { float4 Position : POSITION; float4 PositionAsTexCoord : TEXCOORD; };
//   PositionPosition TransformAndCopyPosition_VertexShader(float4 position : POSITION)
//   {
//       output.Position = mul(position, WorldViewProjection);
//       output.PositionAsTexCoord = output.Position;   // clip-space position, NOT NDC/screen --
//                                                       // interpolated per-pixel like any other
//                                                       // TEXCOORD varying, not auto-divided by W
//       return output;
//   }
//   float4 HeatHaze_PixelShader(float4 position : TEXCOORD) : COLOR
//   {
//       float2 displacement;
//       displacement.x = sin(position.x/60 + Time*1.5) * sin(position.x/10) * cos(position.x/50);
//       displacement.y = sin(position.y/50 - Time*2.75);
//       displacement *= DistortionScale;
//       displacement = (displacement + float2(1,1)) / 2;
//       return float4(displacement, 0, 1);
//   }
//
// Ported 1:1 below, including the un-obvious re-use of the raw (pre-divide) clip-space position
// as a plain interpolated varying -- not gl_Position's own automatic NDC/screen mapping, a
// genuinely different value in general (only coincide when W=1 everywhere, exactly the case this
// test's own World=View=Projection=Identity setup produces, chosen deliberately for that reason).
//
// Test geometry: World=View=Projection=Identity, so `gl_Position = vec4(localPos,1)` directly --
// the quad's own local -0.5..0.5 coordinates already land in valid NDC/clip space (W=1
// everywhere), and the interpolated PositionAsTexCoord at any point equals that same point's
// local (x,y) exactly, letting every value below be derived exactly, not approximated.
//
// Check A -- Time=0, centre pixel (PositionAsTexCoord=(0,0)): displacement.x =
//   sin(0/60+0)*sin(0/10)*cos(0/50) = sin(0)*sin(0)*cos(0) = 0*0*1 = 0 (the middle sin(x/10)
//   factor is exactly 0 at x=0, regardless of Time -- x stays 0 in both checks below).
//   displacement.y = sin(0/50 - 0*2.75) = sin(0) = 0. displacement=(0,0)*scale=(0,0).
//   output = ((0,0)+(1,1))/2 = (0.5,0.5) ~= (128,128,0,255).
// Check B -- Time=pi/2/2.75 (so Time*2.75=pi/2 exactly): displacement.x still 0 (x-term
//   unaffected). displacement.y = sin(0 - pi/2) = sin(-pi/2) = -1. *DistortionScale(1.0) = -1.
//   output.g = (-1+1)/2 = 0 ~= (128,0,0,255). Distinct G channel from Check A -- proves the
//   `Time` uniform genuinely reaches and drives the shader.
//
// Exit code 0 = both PASS, 1 = either FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
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

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numbers>
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

    // Ported 1:1 from TransformAndCopyPosition_VertexShader -- reuses the raw clip-space Position
    // as a plain interpolated varying, matching the HLSL original's own (unusual) reuse of
    // output.Position for output.PositionAsTexCoord.
    const char* kVertSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
out vec4 vPositionAsTexCoord;
uniform mat4 WorldViewProjection;
void main() {
    gl_Position = WorldViewProjection * vec4(aPosition, 1.0);
    vPositionAsTexCoord = gl_Position;
}
)";

    // Ported 1:1 from HeatHaze_PixelShader.
    const char* kFragSrc = R"(#version 300 es
precision highp float;
in vec4 vPositionAsTexCoord;
out vec4 FragColor;
uniform float Time;
uniform float DistortionScale;
void main() {
    vec2 displacement;
    displacement.x = sin(vPositionAsTexCoord.x / 60.0 + Time * 1.5)
                    * sin(vPositionAsTexCoord.x / 10.0)
                    * cos(vPositionAsTexCoord.x / 50.0);
    displacement.y = sin(vPositionAsTexCoord.y / 50.0 - Time * 2.75);
    displacement *= DistortionScale;
    displacement = (displacement + vec2(1.0, 1.0)) / 2.0;
    FragColor = vec4(displacement, 0.0, 1.0);
}
)";
}

class EasyGLDistortersHeatHazeTest : public Game
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
            / ("cna_distorters_heathaze_test_" + std::to_string(
                   reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        WriteFile(root / "hh.vert.glsl", kVertSrc);
        WriteFile(root / "hh.frag.glsl", kFragSrc);
        WriteFile(root / "hh.cnj", R"({
  "cnjVersion": 1,
  "type": "Effect",
  "vertex": "hh.vert.glsl",
  "fragment": "hh.frag.glsl"
})");

        getContentProperty().setRootDirectoryProperty(root.string());
        fxBase_ = getContentProperty().Load<std::shared_ptr<Effect>>("hh");

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

    Color DrawOnce(float time)
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
        fx->Apply();
        float identCM[16];
        Matrix::getIdentityProperty().ToColumnMajor(identCM);
        fx->SetUniformMat4("WorldViewProjection", identCM);
        fx->SetUniformFloat("Time", time);
        fx->SetUniformFloat("DistortionScale", 1.0f);

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
            std::printf("[FAIL] EasyGLDistortersHeatHaze: .cnj load or GLSL compile failed\n");
            Exit();
            return;
        }

        const Color a = DrawOnce(0.0f);
        const float timeB = static_cast<float>(std::numbers::pi / 2.0 / 2.75);
        const Color b = DrawOnce(timeB);

        const auto close = [](int got, int expected) { return got >= expected - 6 && got <= expected + 6; };
        const bool aOk = close(a.getRProperty(), 128) && close(a.getGProperty(), 128) && a.getBProperty() == 0;
        const bool bOk = close(b.getRProperty(), 128) && close(b.getGProperty(), 0) && b.getBProperty() == 0;

        std::printf("[%s] Check A (Time=0): (%d,%d,%d) expected~=(128,128,0)\n",
                    aOk ? "PASS" : "FAIL", a.getRProperty(), a.getGProperty(), a.getBProperty());
        std::printf("[%s] Check B (Time=pi/2/2.75): (%d,%d,%d) expected~=(128,0,0)\n",
                    bOk ? "PASS" : "FAIL", b.getRProperty(), b.getGProperty(), b.getBProperty());

        result_ = (aOk && bOk) ? 0 : 1;
        Exit();
    }

public:
    EasyGLDistortersHeatHazeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    EasyGLDistortersHeatHazeTest game;
    game.Run();
    return game.getResult();
}

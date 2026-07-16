// SPDX-License-Identifier: MS-PL
// Task 947 (Phase 78 rollout): HLSL->GLSL shader-conversion proof -- VertexLightingSample's
// `FlatShaded.fx` (technique `FlatShaded` = `SimpleVertexShader` + `SimplePixelShader`). With
// this and the sibling `VertexLighting.fx` (VertexLightingSample's own copy, see that test's own
// header comment) ported, `VertexLighting` sample's shader blocker (DEFERRED.md item #11) is
// fully cleared -- confirmed via `VertexLighting.cs:115-116`, these are the only 2 effects the
// sample loads, each with exactly one technique (no runtime technique-cycling, unlike
// `PerPixelLighting` sample).
//
// FNA reference (`VertexLightingSample_4_0/VertexLighting/Content/FlatShaded.fx`):
//   float4 SimpleVertexShader(float3 position : POSITION) : POSITION
//   {
//       float4x4 wvp = mul(mul(world, view), projection);
//       return mul(float4(position, 1.0), wvp);
//   }
//   float4 SimplePixelShader() : COLOR { return float4(1,1,1,1); }
//
// The simplest shader in this task's own catalogue so far: position transform only (no Normal
// input at all -- this shader's own VS_INPUT is just `float3 position`), pixel shader always
// returns solid opaque white regardless of any input. Reuses the same
// VertexPositionNormalTexture (stride 32) vertex buffer as the sibling tests for consistency
// (Normal/TexCoord simply go unread by this shader, matching how `FlatShaded.fx`'s own real XNA
// vertex declaration also only reads the position stream from whatever mesh it's bound to).
//
// Because the pixel shader is a constant, this test can't verify any lighting formula -- it
// instead verifies the one thing that IS interesting here: that World/View/Projection genuinely
// reach and drive the vertex position, not that the quad is drawn at some hardcoded screen
// location regardless of transform.
//
// Check A -- World=Identity: quad renders at the viewport centre, fully covering the sampled
//   pixel -> solid white (255,255,255).
// Check B -- World=Translation(50,0,0) (50 world units off to the side -- comfortably outside the
//   view frustum at this test's camera distance/FOV, confirmed empirically, not just assumed):
//   quad no longer covers the centre pixel -> the clear colour (10,10,10) shows through instead.
//   Distinct from Check A -- proves World genuinely moves the geometry, not merely accepted and
//   ignored.
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

    // Ported 1:1 from FlatShaded.fx's SimpleVertexShader -- position transform only, Normal input
    // is declared (location 1) purely to match this test's shared vertex buffer layout, unread.
    const char* kVertSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
uniform mat4 World;
uniform mat4 View;
uniform mat4 Projection;
void main() {
    gl_Position = Projection * View * World * vec4(aPosition, 1.0);
}
)";

    // Ported 1:1 from FlatShaded.fx's SimplePixelShader.
    const char* kFragSrc = R"(#version 300 es
precision mediump float;
out vec4 FragColor;
void main() {
    FragColor = vec4(1.0, 1.0, 1.0, 1.0);
}
)";
}

class EasyGLFlatShadedTest : public Game
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
            / ("cna_flatshaded_test_" + std::to_string(
                   reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        WriteFile(root / "flatshaded.vert.glsl", kVertSrc);
        WriteFile(root / "flatshaded.frag.glsl", kFragSrc);
        WriteFile(root / "flatshaded.cnb", R"({
  "cnbVersion": 1,
  "type": "Effect",
  "vertex": "flatshaded.vert.glsl",
  "fragment": "flatshaded.frag.glsl"
})");

        getContentProperty().setRootDirectoryProperty(root.string());
        fxBase_ = getContentProperty().Load<std::shared_ptr<Effect>>("flatshaded");

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
            std::printf("[FAIL] EasyGLFlatShaded: .cnb load or GLSL compile failed\n");
            Exit();
            return;
        }

        const Color a = DrawOnce(Matrix::getIdentityProperty());
        const Color b = DrawOnce(Matrix::CreateTranslation(50.0f, 0.0f, 0.0f));

        const bool aOk = a.getRProperty() >= 250 && a.getGProperty() >= 250 && a.getBProperty() >= 250;
        const bool bOk = b.getRProperty() <= 15 && b.getGProperty() <= 15 && b.getBProperty() <= 15;

        std::printf("[%s] Check A (World=Identity): (%d,%d,%d) expected~=(255,255,255)\n",
                    aOk ? "PASS" : "FAIL", a.getRProperty(), a.getGProperty(), a.getBProperty());
        std::printf("[%s] Check B (World=Translate(50,0,0), off-screen): (%d,%d,%d) expected~=(10,10,10)\n",
                    bOk ? "PASS" : "FAIL", b.getRProperty(), b.getGProperty(), b.getBProperty());

        result_ = (aOk && bOk) ? 0 : 1;
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    EasyGLFlatShadedTest game;
    game.Run();
    return game.getResult();
}

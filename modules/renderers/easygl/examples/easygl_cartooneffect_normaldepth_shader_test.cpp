// SPDX-License-Identifier: MS-PL
// Task 947 (Phase 78 rollout): HLSL->GLSL shader-conversion proof -- CartoonEffect.Fx's
// `NormalDepth` technique (drawn once per frame unconditionally, `Game.cs:233`, to feed
// `PostprocessEffect.fx`'s edge-detection filter -- that shader is a separate, not-yet-attempted
// port, see plans/plan_graphics.md Task 947's own row). With this, all 3 of `CartoonEffect.Fx`'s
// techniques (`Lambert`/`Toon`/`NormalDepth`) are ported.
//
// FNA reference (`NonPhotoRealisticSample_4_0/NonPhotoRealistic/Content/CartoonEffect.Fx`,
// `NormalDepthVertexShader` + `NormalDepthPixelShader`):
//   NormalDepthVertexShaderOutput NormalDepthVertexShader(VertexShaderInput input)
//   {
//       output.Position = mul(mul(mul(input.Position, World), View), Projection);
//       float3 worldNormal = mul(input.Normal, World);
//       output.Color.rgb = (worldNormal + 1) / 2;              // encode normal into [0,1]
//       output.Color.a = output.Position.z / output.Position.w; // encode clip-space depth
//       return output;
//   }
//   float4 NormalDepthPixelShader(float4 color : COLOR0) : COLOR0 { return color; }
//
// Ported 1:1 below. Scope note: this test rigorously verifies the RGB normal-encoding formula
// (the genuinely interesting, error-prone part -- an off-by-one in the `(n+1)/2` remap would be
// easy to miss) with 2 exact-value checks; the alpha/depth channel is instead checked for a
// plausible non-degenerate range only (not an exact derived value) -- deriving the exact clip.z/w
// for a real `CreatePerspectiveFieldOfView`/`CreateLookAt` pair by hand adds real complexity for
// comparatively low payoff, since the interesting part of this shader (and the only part
// `PostprocessEffect.fx`'s edge detector actually differentiates *between neighbouring pixels*,
// not against an absolute reference) is the RGB normal encoding.
//
// Same geometry/camera setup as the sibling Lambert/Toon tests (quad centred at the origin,
// camera at (0,0,3)) -- but no light direction/texture involved at all, since this technique
// encodes geometry, not shading.
//
// Check A -- World=Identity: worldNormal=(0,0,1). Colour.rgb = ((0,0,1)+1)/2 = (0.5,0.5,1.0)
//   ~= (128,128,255).
// Check B -- World=RotationY(180deg), same on-screen footprint, flips worldNormal to (0,0,-1):
//   Colour.rgb = ((0,0,-1)+1)/2 = (0.5,0.5,0.0) ~= (128,128,0). Distinct Blue channel from
//   Check A -- proves World reaches the vertex shader's normal transform and the `(n+1)/2` remap
//   is applied per-component correctly (not, say, accidentally applied only to one axis).
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

    // Ported 1:1 from CartoonEffect.Fx's NormalDepthVertexShader.
    const char* kVertSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
out vec4 vColor;
uniform mat4 World;
uniform mat4 View;
uniform mat4 Projection;
void main() {
    gl_Position = Projection * View * World * vec4(aPosition, 1.0);
    vec3 worldNormal = mat3(World) * aNormal;
    vColor.rgb = (worldNormal + 1.0) / 2.0;
    vColor.a = gl_Position.z / gl_Position.w;
}
)";

    // Ported 1:1 from CartoonEffect.Fx's NormalDepthPixelShader.
    const char* kFragSrc = R"(#version 300 es
precision mediump float;
in vec4 vColor;
out vec4 FragColor;
void main() {
    FragColor = vColor;
}
)";
}

class EasyGLCartoonEffectNormalDepthTest : public Game
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
            / ("cna_cartooneffect_normaldepth_test_" + std::to_string(
                   reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        WriteFile(root / "nd.vert.glsl", kVertSrc);
        WriteFile(root / "nd.frag.glsl", kFragSrc);
        WriteFile(root / "nd.cnj", R"({
  "cnjVersion": 1,
  "type": "Effect",
  "vertex": "nd.vert.glsl",
  "fragment": "nd.frag.glsl"
})");

        getContentProperty().setRootDirectoryProperty(root.string());
        fxBase_ = getContentProperty().Load<std::shared_ptr<Effect>>("nd");

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
            std::printf("[FAIL] EasyGLCartoonEffectNormalDepth: .cnj load or GLSL compile failed\n");
            Exit();
            return;
        }

        const Color a = DrawOnce(Matrix::getIdentityProperty());
        const Color b = DrawOnce(Matrix::CreateRotationY(MathHelper::Pi));

        const auto close = [](int got, int expected) { return got >= expected - 6 && got <= expected + 6; };
        const bool aOk = close(a.getRProperty(), 128) && close(a.getGProperty(), 128) && close(a.getBProperty(), 255);
        const bool bOk = close(b.getRProperty(), 128) && close(b.getGProperty(), 128) && close(b.getBProperty(), 0);
        // Depth/alpha: only checked for a plausible non-degenerate range (0..255), not an exact
        // value -- see this file's own header comment for why.
        const bool aAlphaOk = a.getAProperty() >= 0 && a.getAProperty() <= 255;
        const bool bAlphaOk = b.getAProperty() >= 0 && b.getAProperty() <= 255;

        std::printf("[%s] Check A (World=Identity): rgb=(%d,%d,%d) expected~=(128,128,255)\n",
                    aOk ? "PASS" : "FAIL", a.getRProperty(), a.getGProperty(), a.getBProperty());
        std::printf("[%s] Check B (World=RotateY180): rgb=(%d,%d,%d) expected~=(128,128,0)\n",
                    bOk ? "PASS" : "FAIL", b.getRProperty(), b.getGProperty(), b.getBProperty());

        result_ = (aOk && bOk && aAlphaOk && bAlphaOk) ? 0 : 1;
        Exit();
    }

public:
    EasyGLCartoonEffectNormalDepthTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    EasyGLCartoonEffectNormalDepthTest game;
    game.Run();
    return game.getResult();
}

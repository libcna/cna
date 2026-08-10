// SPDX-License-Identifier: MS-PL
// Task 947 (Phase 78 rollout): HLSL->GLSL shader-conversion proof -- ShipGame's own
// `AnimSprite.fx` (sprite-sheet frame cross-fade). ShipGame is a larger sample with 4 distinct
// custom shaders (`AnimSprite.fx`, `NormalMapping.fx`, `Blur.fx`, `Particle.fx`) -- confirmed via
// `diff` that its own `NormalMapping.fx`/`Particle.fx` are NOT the same files already ported for
// `NormalMappingSample`/`Particles3DSample` (different parameter sets, different techniques,
// different rendering approach -- ShipGame's `Particle.fx` uses real GPU point sprites (`PSIZE`),
// not the quad-corner billboard technique `ParticleEffect.fx` uses). This is the first of the 4,
// picked for using only `VertexPositionTexture` (stride 20, already supported by the existing 3D
// draw path -- no Task 1080 custom layout needed here).
//
// FNA reference (`ShipGame_4_0/ShipGame/Content/shaders/AnimSprite.fx`):
//   void AnimSpriteVS(in float4 InPosition : POSITION0, in float2 InTexCoord : TEXCOORD0,
//                     out float4 OutPosition : POSITION0, out float2 OutTexCoord : TEXCOORD0)
//   {
//       OutPosition = mul(InPosition, ViewProj);
//       OutTexCoord = InTexCoord;
//   }
//   float4 AnimSpritePS(in float2 TexCoord : TEXCOORD0) : COLOR0
//   {
//       float2 tx1 = FrameSize * (FrameOffset.xy + TexCoord);
//       float2 tx2 = FrameSize * (FrameOffset.zw + TexCoord);
//       float4 color1 = tex2D(TextureSampler, tx1);
//       float4 color2 = tex2D(TextureSampler, tx2);
//       float4 blend_color = lerp(color1, color2, FrameBlend.x);
//       blend_color.w *= FrameBlend.y;
//       return blend_color;
//   }
//
// Ported 1:1 below. Like `ShatterEffect.fx`, this shader takes a single, already-combined
// `ViewProj` uniform (no separate `World` at all -- `InPosition` is already world-space, same
// "no World matrix" pattern `Billboard.fx`/`ParticleEffect.fx` also used) -- computed host-side
// as `View*Projection` and uploaded via `SetUniformMat4`, same approach as `ShatterEffect.fx`'s
// `WorldViewProjection`.
//
// Test setup: standard `VertexPositionTexture` quad centred at the origin (World=Identity,
// corners +-0.5, standard 0..1 UVs -- sampled at the viewport centre, TexCoord=(0.5,0.5) by this
// session's own established quad-symmetry trick), camera eye=(0,0,3)/target=origin/up=(0,1,0).
// A 2-texel-wide fixture texture: texel0=`(200,100,50,255)`, texel1=`(50,150,200,255)`.
// `FrameSize=(0.5,1.0)` (a 2-column, 1-row sprite sheet). `FrameOffset=(0,0,1.0,0)` chosen so
// both `tx1`/`tx2` land exactly at their own texel's centre (`u=0.25`/`u=0.75`), avoiding any
// bilinear-blend ambiguity near a texel boundary without needing an explicit `PointClamp`
// override, matching this session's own established sampling convention (e.g.
// `easygl_shadowmapping_drawwithshadowmap_shader_test.cpp`'s own centre-of-texel picks):
//   tx1 = (0.5,1.0)*((0,0)+(0.5,0.5)) = (0.25,0.5) -- texel0's own centre.
//   tx2 = (0.5,1.0)*((1.0,0)+(0.5,0.5)) = (0.75,0.5) -- texel1's own centre.
//
// Check A -- `FrameBlend=(0,1)` (pure frame 1, no alpha scale): `blend_color =
//   lerp(color1,color2,0) = color1` -> byte `(200,100,50,255)`.
// Check B -- `FrameBlend=(1,1)` (pure frame 2): `blend_color = color2` -> byte
//   `(50,150,200,255)` -- distinct from Check A, proving `tx2`/`FrameOffset.zw` genuinely reach a
//   different sample.
// Check C -- `FrameBlend=(0.5,0.5)` (an even cross-fade, alpha halved): `blend_color =
//   lerp(color1,color2,0.5) = (0.4902,0.4902,0.4902,1.0)`, then `.w *= 0.5` -> `(0.4902,0.4902,
//   0.4902,0.5)` -> byte `(125,125,125,128)` -- a genuinely distinct third value from both A and
//   B, proving the `lerp`/alpha-scale combination, not just that either frame alone renders.
//
// **Discriminating power verified by mutation**: dropped the `blend_color.w *= FrameBlend.y;`
// alpha-scale line entirely -- Check C correctly failed (alpha stayed `255` instead of the
// predicted `128`, clearly outside this test's own +-6 tolerance) while Checks A/B (whose own
// `FrameBlend.y=1` makes this multiply a no-op regardless) were correctly unaffected. Reverted,
// reconfirmed 3/3 PASS.
//
// Exit code 0 = all 3 PASS, 1 = any FAIL.

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
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <cstdint>
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

    const char* kVertSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;
out vec2 vTexCoord;
uniform mat4 ViewProj;
void main() {
    gl_Position = ViewProj * vec4(aPosition, 1.0);
    vTexCoord = aTexCoord;
}
)";

    const char* kFragSrc = R"(#version 300 es
precision highp float;
in vec2 vTexCoord;
out vec4 FragColor;
uniform sampler2D TextureSampler;
uniform vec2 FrameSize;
uniform vec4 FrameOffset;
uniform vec2 FrameBlend;
void main() {
    vec2 tx1 = FrameSize * (FrameOffset.xy + vTexCoord);
    vec2 tx2 = FrameSize * (FrameOffset.zw + vTexCoord);

    vec4 color1 = texture(TextureSampler, tx1);
    vec4 color2 = texture(TextureSampler, tx2);

    vec4 blendColor = mix(color1, color2, FrameBlend.x);
    blendColor.w *= FrameBlend.y;

    FragColor = blendColor;
}
)";
}

class EasyGLAnimSpriteTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::shared_ptr<Effect> fxBase_;
    std::unique_ptr<VertexBuffer> vb_;
    std::unique_ptr<IndexBuffer> ib_;
    Texture2D frameTex_;
    bool done_   = false;
    int result_  = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        const auto root = std::filesystem::temp_directory_path()
            / ("cna_animsprite_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        WriteFile(root / "as.vert.glsl", kVertSrc);
        WriteFile(root / "as.frag.glsl", kFragSrc);
        WriteFile(root / "as.cnj", R"({
  "cnjVersion": 1,
  "type": "Effect",
  "vertex": "as.vert.glsl",
  "fragment": "as.frag.glsl"
})");

        getContentProperty().setRootDirectoryProperty(root.string());
        fxBase_ = getContentProperty().Load<std::shared_ptr<Effect>>("as");

        const VertexPositionTexture verts[4] = {
            { Vector3(-0.5f,  0.5f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3(-0.5f, -0.5f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3( 0.5f, -0.5f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3( 0.5f,  0.5f, 0.0f), Vector2(1.0f, 1.0f) },
        };
        vb_ = std::make_unique<VertexBuffer>(device, 4);
        vb_->SetData(verts, 4);

        const std::uint16_t indices[6] = { 0, 1, 2, 0, 2, 3 };
        ib_ = std::make_unique<IndexBuffer>(device, 6);
        ib_->SetData(indices, 6);

        frameTex_ = Texture2D::CreateFromPixels(device, 2, 1, std::vector<std::uint8_t>{
            200, 100, 50, 255,   // texel 0 (frame 1)
            50, 150, 200, 255,   // texel 1 (frame 2)
        });
    }

    Color DrawOnce(float blendT, float alphaScale)
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(Color(10, 10, 10, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        const Matrix view = Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero,
                                                  Vector3(0.0f, 1.0f, 0.0f));
        const Matrix projection = Matrix::CreatePerspectiveFieldOfView(
            MathHelper::PiOver4, vp.getAspectRatioProperty(), 0.1f, 100.0f);

        auto* fx = dynamic_cast<ShaderEffect*>(fxBase_.get());
        fx->Apply();

        const Matrix viewProj = view * projection;
        float viewProjColMajor[16];
        viewProj.ToColumnMajor(viewProjColMajor);
        fx->SetUniformMat4("ViewProj", viewProjColMajor);

        fx->SetTexture(0, frameTex_);
        fx->SetUniformInt("TextureSampler", 0);
        fx->SetUniformVec2("FrameSize", 0.5f, 1.0f);
        fx->SetUniformVec4("FrameOffset", 0.0f, 0.0f, 1.0f, 0.0f);
        fx->SetUniformVec2("FrameBlend", blendT, alphaScale);

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
            std::printf("[FAIL] EasyGLAnimSprite: .cnj load or GLSL compile failed\n");
            Exit();
            return;
        }

        const Color a = DrawOnce(0.0f, 1.0f);
        const Color b = DrawOnce(1.0f, 1.0f);
        const Color c = DrawOnce(0.5f, 0.5f);

        const auto close = [](int got, int expected) { return got >= expected - 6 && got <= expected + 6; };
        const bool aOk = close(a.getRProperty(), 200) && close(a.getGProperty(), 100) &&
                        close(a.getBProperty(), 50) && close(a.getAProperty(), 255);
        const bool bOk = close(b.getRProperty(), 50) && close(b.getGProperty(), 150) &&
                        close(b.getBProperty(), 200) && close(b.getAProperty(), 255);
        const bool cOk = close(c.getRProperty(), 125) && close(c.getGProperty(), 125) &&
                        close(c.getBProperty(), 125) && close(c.getAProperty(), 128);

        std::printf("[%s] Check A (FrameBlend=(0,1)): rgba=(%d,%d,%d,%d) expected~=(200,100,50,255)\n",
                    aOk ? "PASS" : "FAIL", a.getRProperty(), a.getGProperty(), a.getBProperty(), a.getAProperty());
        std::printf("[%s] Check B (FrameBlend=(1,1)): rgba=(%d,%d,%d,%d) expected~=(50,150,200,255)\n",
                    bOk ? "PASS" : "FAIL", b.getRProperty(), b.getGProperty(), b.getBProperty(), b.getAProperty());
        std::printf("[%s] Check C (FrameBlend=(0.5,0.5)): rgba=(%d,%d,%d,%d) expected~=(125,125,125,128)\n",
                    cOk ? "PASS" : "FAIL", c.getRProperty(), c.getGProperty(), c.getBProperty(), c.getAProperty());

        result_ = (aOk && bOk && cOk) ? 0 : 1;
        Exit();
    }

public:
    EasyGLAnimSpriteTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    EasyGLAnimSpriteTest game;
    game.Run();
    return game.getResult();
}

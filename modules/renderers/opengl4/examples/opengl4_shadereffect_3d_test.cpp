// SPDX-License-Identifier: MS-PL
// plans/plan_opengl4.md GL4-30: real custom ShaderEffect (CreateEffectRenderer) for the OpenGL4 graphics
// renderer -- CreateEffectRenderer() previously wasn't overridden (default returns nullptr), so a
// caller-supplied GLSL vertex/fragment source pair had no way to compile on this renderer at all.
// Added OpenGL4EffectRenderer (a thin IEffectRenderer wrapper around one OpenGL4RawProgram, modeled
// on EasyGLEffectRenderer's identical shape) plus a customEffectRenderer check at the top of
// DrawPrimitivesEx/DrawIndexedPrimitivesEx (ported from EasyGLRenderer's own
// BindCustomEffectMatrices helper): when ShaderEffect::FillGpuDrawParams() sets
// GpuDrawParams::customEffectRenderer, the compiled program is bound directly and its
// World/View/Projection uniforms are set, bypassing BindProgramForStride's built-in
// stride-dispatched shaders entirely.
//
// Mirrors easygl_shadereffect_3d_test.cpp's own scene, methodology, and expected values exactly
// (desktop GLSL 410 core translation only -- no ES precision qualifiers, otherwise identical),
// constructing ShaderEffect directly from source strings (no ContentManager/.cnj indirection
// needed, since ShaderEffect's own constructor already accepts vertSrc/fragSrc strings).
//
// Scope (deliberate, matching the EasyGL precedent this ports from): proves the wiring for a
// vertex format this renderer already supports (VertexPositionNormalTexture, stride 32) -- the
// vertex ATTRIBUTE layout itself (locations 0/1/2 = Position/Normal/TexCoord) is unchanged,
// already proven by every existing stride-32 test in this repo. SpriteBatch::SetCustomEffect
// integration (a separate, larger feature) is out of scope here, same as every other renderer that
// has landed CreateEffectRenderer without it.
//
// Check A -- World=Identity (surface faces the camera/light head-on): world normal stays
//            (0,0,1), N.L = dot((0,0,1),(0,0,1)) = 1 -> full diffuseColor (200,100,50).
// Check B -- World=RotationY(180deg), deliberately NOT 90deg (which would turn the quad edge-on,
//            making "renders nothing" and "renders black" indistinguishable): keeps the exact
//            same on-screen footprint (X-symmetric quad) while flipping the world normal to
//            (0,0,-1), N.L = dot((0,0,-1),(0,0,1)) = -1, clamped to 0 -> genuinely lit black
//            (0,0,0), proving the World matrix reaches the vertex shader and affects real,
//            visible world-space lighting -- not merely that some fixed shader renders something.
//
// Exit code 0 = both PASS, 1 = either FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    // Position (loc0) + Normal (loc1) + TexCoord (loc2) -- matches the existing stride-32
    // VertexPositionNormalTexture attribute layout exactly (OpenGL4VertexBufferRenderer::ApplyLayout
    // case 32), not a new layout.
    const char* kVertSrc = R"GLSL(
#version 410 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
out vec3 vWorldNormal;
out vec2 vTexCoord;
uniform mat4 World;
uniform mat4 View;
uniform mat4 Projection;
void main()
{
    vec4 worldPos = World * vec4(aPosition, 1.0);
    gl_Position = Projection * View * worldPos;
    vWorldNormal = mat3(World) * aNormal;
    vTexCoord = aTexCoord;
}
)GLSL";

    const char* kFragSrc = R"GLSL(
#version 410 core
in vec3 vWorldNormal;
in vec2 vTexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform vec3 uLightDir;
uniform vec3 uDiffuseColor;
void main()
{
    float nDotL = max(dot(normalize(vWorldNormal), uLightDir), 0.0);
    vec3 tex = texture(texture1, vTexCoord).rgb;
    FragColor = vec4(uDiffuseColor * tex * nDotL, 1.0);
}
)GLSL";
}

class OpenGL4ShaderEffect3DTest : public Game
{
    std::unique_ptr<ShaderEffect> fx_;
    std::unique_ptr<VertexBuffer> vb_;
    std::unique_ptr<IndexBuffer> ib_;
    std::unique_ptr<Texture2D> whiteTex_;
    bool done_  = false;
    int result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        fx_ = std::make_unique<ShaderEffect>(device, kVertSrc, kFragSrc);

        const std::vector<uint8_t> white = { 255, 255, 255, 255 };
        whiteTex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, white));

        // A 2-triangle quad in the local XY plane, local normal (0,0,1) (facing local +Z).
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

        fx_->setWorldProperty(world);
        fx_->setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero,
                                                   Vector3(0.0f, 1.0f, 0.0f)));
        fx_->setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(
            MathHelper::PiOver4, vp.getAspectRatioProperty(), 0.1f, 100.0f));
        // Apply() first (binds the compiled program) -- SetTexture()/SetUniformXxx() write
        // directly to whatever program is currently bound, matching the EasyGL precedent's own
        // proven call order.
        fx_->Apply();
        fx_->SetTexture(0, *whiteTex_);
        fx_->SetUniformVec3("uLightDir", 0.0f, 0.0f, 1.0f);
        fx_->SetUniformVec3("uDiffuseColor", 200.0f / 255.0f, 100.0f / 255.0f, 50.0f / 255.0f);

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

        if (!fx_->IsEffectValid())
        {
            std::printf("[FAIL] OpenGL4ShaderEffect3D: GLSL compile failed\n");
            Exit();
            return;
        }

        const Color a = DrawOnce(Matrix::getIdentityProperty());
        const Color b = DrawOnce(Matrix::CreateRotationY(MathHelper::Pi));

        const auto close = [](int got, int expected) { return got >= expected - 10 && got <= expected + 10; };
        const bool aOk = close(a.getRProperty(), 200) && close(a.getGProperty(), 100) && close(a.getBProperty(), 50);
        const bool bOk = b.getRProperty() <= 10 && b.getGProperty() <= 10 && b.getBProperty() <= 10;

        std::printf("[%s] Check A (World=Identity, facing light): (%d,%d,%d) expected~=(200,100,50)\n",
                    aOk ? "PASS" : "FAIL", a.getRProperty(), a.getGProperty(), a.getBProperty());
        std::printf("[%s] Check B (World=RotateY180, same footprint, facing away): (%d,%d,%d) expected~=(0,0,0)\n",
                    bOk ? "PASS" : "FAIL", b.getRProperty(), b.getGProperty(), b.getBProperty());

        result_ = (aOk && bOk) ? 0 : 1;
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    OpenGL4ShaderEffect3DTest game;
    game.Run();
    return game.getResult();
}

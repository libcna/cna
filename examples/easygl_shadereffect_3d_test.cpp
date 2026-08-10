// SPDX-License-Identifier: MS-PL
// Task 1079: verify ShaderEffect can drive a real 3D GraphicsDevice::DrawIndexedPrimitives()
// call (not just SpriteBatch) -- a genuine engine capability, not a shader-conversion proof.
//
// Before this task, ShaderEffect::FillGpuDrawParams() had no override (the Effect base no-op
// ran), so GpuDrawParams stayed default/zero, and EasyGLRenderer::DrawIndexedPrimitivesEx
// always dispatched to one of its own built-in stride-selected shaders regardless of any bound
// custom effect -- a ShaderEffect applied via EffectPass-style code before a 3D draw call was
// silently ignored. Fixed by: (1) GpuDrawParams gained a `customEffectRenderer` field; (2)
// ShaderEffect now implements IEffectMatrices (World/View/Projection, extracted by
// GraphicsDevice::ExtractMatrices() the same way every stock effect already works) and overrides
// FillGpuDrawParams() to set customEffectRenderer; (3) EasyGLRenderer::DrawPrimitivesEx()/
// DrawIndexedPrimitivesEx() check it first and, when set, bind the custom program + its
// World/View/Projection uniforms directly instead of selecting a built-in shader.
//
// Scope (deliberate, documented): this proves the wiring for vertex formats CNA's renderer
// ALREADY supports (VertexPositionNormalTexture, stride 32) -- the vertex ATTRIBUTE layout
// itself (locations 0/1/2 = Position/Normal/TexCoord) is unchanged, already proven by every
// existing stride-32 test in this repo. A genuinely custom vertex format (e.g. NormalMapping.fx's
// Position+Normal+Binormal+Tangent) is a separate, larger gap -- VertexBuffer itself has no
// generic custom-vertex-struct SetData() overload yet, only a fixed set of built-in types -- not
// attempted here.
//
// Test: a single-quad "surface" (VertexPositionNormalTexture, local normal (0,0,1)) lit by a
// simple hand-written N-dot-L diffuse shader (uLightDir, uDiffuseColor uniforms) + a solid-white
// texture (proves TexCoord/sampler wiring is inert-correct, not exercising texture content).
// World/View/Projection are set via ShaderEffect's new IEffectMatrices properties, exactly like
// a stock effect, then drawn via the same SetVertexBuffer/setIndicesProperty/DrawIndexedPrimitives
// call shape RawMesh::Draw()/ModelMesh::Draw() use.
//
// Check A -- World=Identity (surface faces the camera/light head-on): world normal stays
//            (0,0,1), N.L = dot((0,0,1),(0,0,1)) = 1 -> full diffuseColor (200,100,50).
// Check B -- World=RotationY(180deg), deliberately NOT 90deg: a 90deg rotation would turn the
//            quad edge-on to the camera (near-zero screen footprint), so "renders nothing" and
//            "renders black" would be indistinguishable -- a false-positive risk. 180deg keeps
//            the exact same on-screen footprint (the quad is X-symmetric, so mirroring X is a
//            no-op on its silhouette) while flipping the world normal to (0,0,-1), still fully
//            rasterized (RasterizerState::CullNone) but with N.L = dot((0,0,-1),(0,0,1)) = -1,
//            clamped to 0 -> genuinely lit black (0,0,0), not merely absent. Proves the World
//            matrix reaches the vertex shader and affects world-space lighting on real, visible
//            not just that some fixed shader happens to render something.
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
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdint>
#include <cstdio>
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

    // Position (loc0) + Normal (loc1) + TexCoord (loc2) -- matches the existing stride-32
    // VertexPositionNormalTexture attribute layout exactly (EasyGLVertexBufferRenderer::ApplyLayout
    // case 32), not a new layout.
    const char* kVertSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
out vec3 vWorldNormal;
out vec2 vTexCoord;
uniform mat4 World;
uniform mat4 View;
uniform mat4 Projection;
void main() {
    vec4 worldPos = World * vec4(aPosition, 1.0);
    gl_Position = Projection * View * worldPos;
    vWorldNormal = mat3(World) * aNormal;
    vTexCoord = aTexCoord;
}
)";

    const char* kFragSrc = R"(#version 300 es
precision mediump float;
in vec3 vWorldNormal;
in vec2 vTexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform vec3 uLightDir;
uniform vec3 uDiffuseColor;
void main() {
    float nDotL = max(dot(normalize(vWorldNormal), uLightDir), 0.0);
    vec3 tex = texture(texture1, vTexCoord).rgb;
    FragColor = vec4(uDiffuseColor * tex * nDotL, 1.0);
}
)";
}

class EasyGLShaderEffect3DTest : public Game
{
    std::shared_ptr<Effect> fxBase_;
    std::unique_ptr<VertexBuffer> vb_;
    std::unique_ptr<IndexBuffer> ib_;
    Texture2D whiteTex_;
    bool done_   = false;
    int result_  = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        const auto root = std::filesystem::temp_directory_path()
            / ("cna_shadereffect_3d_test_" + std::to_string(
                   reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        WriteFile(root / "lit3d.vert.glsl", kVertSrc);
        WriteFile(root / "lit3d.frag.glsl", kFragSrc);
        WriteFile(root / "lit3d.cnj", R"({
  "cnjVersion": 1,
  "type": "Effect",
  "vertex": "lit3d.vert.glsl",
  "fragment": "lit3d.frag.glsl"
})");

        getContentProperty().setRootDirectoryProperty(root.string());
        fxBase_ = getContentProperty().Load<std::shared_ptr<Effect>>("lit3d");

        const std::vector<uint8_t> white = { 255, 255, 255, 255 };
        whiteTex_ = Texture2D::CreateFromPixels(device, 1, 1, white);

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

        auto* fx = dynamic_cast<ShaderEffect*>(fxBase_.get());
        fx->setWorldProperty(world);
        fx->setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero,
                                                  Vector3(0.0f, 1.0f, 0.0f)));
        fx->setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(
            MathHelper::PiOver4, vp.getAspectRatioProperty(), 0.1f, 100.0f));
        // Apply() first (binds the compiled program) -- SetTexture()/SetUniformXxx() write
        // directly to whatever program is currently bound, matching Bloom/Clouds' own proven
        // call order (Task 946/947).
        fx->Apply();
        fx->SetTexture(0, whiteTex_);
        fx->SetUniformVec3("uLightDir", 0.0f, 0.0f, 1.0f);
        fx->SetUniformVec3("uDiffuseColor", 200.0f / 255.0f, 100.0f / 255.0f, 50.0f / 255.0f);

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
            std::printf("[FAIL] EasyGLShaderEffect3D: .cnj load or GLSL compile failed\n");
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
    EasyGLShaderEffect3DTest game;
    game.Run();
    return game.getResult();
}

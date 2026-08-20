// SPDX-License-Identifier: MS-PL
// Task 1080: proves EasyGL's 3D `ShaderEffect` draw path (Task 1079) can bind a genuinely
// custom vertex layout -- not just the 5 fixed byte-strides (16/20/24/32/52) the renderer's
// pre-existing `ApplyLayout()` switch recognizes (one stock `VertexPositionXxx` type per case).
//
// Background: Task 1079 wired `ShaderEffect` into the 3D draw path, but scoped it to those 5
// existing strides (see plans/plan_graphics.md Task 1079's own row). Several remaining Task 947
// samples (`NormalMapping.fx`'s Position+Normal+Binormal+Tangent+TexCoord,
// `BillboardSample`/`InstancedModel`/particle per-vertex data) use vertex layouts that don't
// match any of those 5 -- this was split out as Task 1080 rather than attempted alongside 1079.
//
// This test does NOT port a real sample shader (that's separate, future Task 947 work, same
// shape as Task 1079's own proof -- `easygl_shadereffect_3d_test.cpp` -- which wasn't a real
// sample shader either). It proves the *capability*: a `VertexDeclaration` with 5 elements at
// non-standard offsets, spanning 3 different `VertexElementFormat`s (`Vector3`/`Vector2`/`Color`)
// and a total stride (48 bytes) that matches none of the 5 built-in cases, binds and reads back
// correctly through `ShaderEffect`'s custom-program 3D draw path.
//
// Implementation (see EasyGLRenderer.cpp): `VertexBuffer::SetDataRaw()` now pushes its
// own `VertexDeclaration`'s element list down to the renderer via the new
// `IVertexBufferRenderer::SetVertexDeclaration()` (default no-op, so Vulkan/Bgfx/SDL_RENDERER
// need zero changes). `EasyGLVertexBufferRenderer::ApplyLayout()` binds generically from that
// list when non-empty: GLSL attribute location = the element's own index within the declaration
// (matching this session's established "layout(location=N) == Nth field of the ported HLSL
// input struct" convention), and a `VertexElementFormat`->GL-attribdescription table covers
// every format XNA defines (`Single`/`Vector2`/`Vector3`/`Vector4`/`Color`/`Byte4`/`Short2`/
// `Short4`/`NormalizedShort2`/`NormalizedShort4`/`HalfVector2`/`HalfVector4`), reusing the exact
// integer-vs-float attribute-pointer distinction the pre-existing skinned-vertex case (stride 52,
// `BlendIndices` as `Byte4`) already established. When no declaration was supplied (every
// pre-existing typed `VertexBuffer::SetData(VertexPositionXxx*, ...)` overload never populates
// one), `ApplyLayout()` falls straight through to the original stride-keyed switch, unchanged --
// this is a purely additive capability, not a replacement of the existing path.
//
// Custom vertex layout under test (test-local struct, not a new stock XNA vertex type -- adding
// one is deferred to whichever future Task 947 shader actually needs it):
//   offset  0: Position     (Vector3)
//   offset 12: Normal       (Vector3)
//   offset 24: Tangent      (Vector3)
//   offset 36: TextureCoord (Vector2)
//   offset 44: Color        (Color -- 4 normalized bytes)
//   stride = 48 bytes -- matches none of the 5 fixed cases (16/20/24/32/52).
//
// The fragment shader diagnostically encodes 3 of the 4 non-Position attributes directly into
// the output pixel so a readback can verify each one landed at the right offset with the right
// data, undisturbed by its neighbours: FragColor = vec4(Normal.x, Tangent.y, TexCoord.x, Color.r).
// TexCoord.x is sampled at the quad's own on-screen centre, which (standard 0..1 corner UVs,
// World=Identity, camera on-axis -- same convention as every other 3D test this session) bilinear-
// interpolates to exactly 0.5 by symmetry, independent of which check is running.
//
// Check A -- Normal=(0.2,0.4,0.6), Tangent=(0.8,0.3,0.1), Color=(200,100,50,255):
//   R=0.2*255~=51, G=0.3*255~=77, B~=128 (TexCoord.x=0.5), A=200 (Color.r passes through exactly).
// Check B -- Normal=(0.9,0.1,0.5), Tangent=(0.05,0.85,0.5), Color=(30,220,10,255):
//   R=0.9*255~=230, G=0.85*255~=217, B~=128 (same TexCoord symmetry), A=30.
// Distinct R/G/A between A and B (drawn from 2 separately-uploaded vertex buffers) proves Normal,
// Tangent, and Color are each read from their own correct offset -- not aliased to each other or
// to Position, and not a hardcoded/fluke value.
//
// Exit code 0 = both PASS, 1 = either FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"

#include <cstdio>
#include <cstdint>
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

    // Test-local packed layout, matching the VertexDeclaration below field-for-field.
#pragma pack(push, 1)
    struct CustomVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float tx, ty, tz;
        float u, v;
        std::uint8_t cr, cg, cb, ca;
    };
#pragma pack(pop)
    static_assert(sizeof(CustomVertex) == 48);

    const char* kVertSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aTangent;
layout(location = 3) in vec2 aTexCoord;
layout(location = 4) in vec4 aColor;
out vec3 vNormal;
out vec3 vTangent;
out vec2 vTexCoord;
out vec4 vColor;
uniform mat4 World;
uniform mat4 View;
uniform mat4 Projection;
void main() {
    gl_Position = Projection * View * World * vec4(aPosition, 1.0);
    vNormal = aNormal;
    vTangent = aTangent;
    vTexCoord = aTexCoord;
    vColor = aColor;
}
)";

    const char* kFragSrc = R"(#version 300 es
precision highp float;
in vec3 vNormal;
in vec3 vTangent;
in vec2 vTexCoord;
in vec4 vColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(vNormal.x, vTangent.y, vTexCoord.x, vColor.r);
}
)";
}

class EasyGLCustomVertexLayoutTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::shared_ptr<Effect> fxBase_;
    std::unique_ptr<IndexBuffer> ib_;
    bool done_   = false;
    int result_  = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();

        const auto root = std::filesystem::temp_directory_path()
            / ("cna_custom_vertex_layout_test_" + std::to_string(
                   reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        WriteFile(root / "cvl.vert.glsl", kVertSrc);
        WriteFile(root / "cvl.frag.glsl", kFragSrc);
        WriteFile(root / "cvl.cnj", R"({
  "cnjVersion": 1,
  "type": "Effect",
  "vertex": "cvl.vert.glsl",
  "fragment": "cvl.frag.glsl"
})");

        getContentProperty().setRootDirectoryProperty(root.string());
        fxBase_ = getContentProperty().Load<std::shared_ptr<Effect>>("cvl");

        const std::uint16_t indices[6] = { 0, 1, 2, 0, 2, 3 };
        ib_ = std::make_unique<IndexBuffer>(getGraphicsDeviceProperty(), 6);
        ib_->SetData(indices, 6);
    }

    std::unique_ptr<VertexBuffer> MakeQuad(Vector3 normal, Vector3 tangent, Color color)
    {
        auto& device = getGraphicsDeviceProperty();

        const VertexDeclaration decl(48, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector3, VertexElementUsage::Tangent, 0),
            VertexElement(36, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            VertexElement(44, VertexElementFormat::Color,   VertexElementUsage::Color, 0),
        });

        auto vb = std::make_unique<VertexBuffer>(device, decl, 4, BufferUsage::None);

        const CustomVertex verts[4] = {
            { -0.5f,  0.5f, 0.0f, normal.X, normal.Y, normal.Z,
              tangent.X, tangent.Y, tangent.Z, 0.0f, 1.0f,
              color.getRProperty(), color.getGProperty(), color.getBProperty(), color.getAProperty() },
            { -0.5f, -0.5f, 0.0f, normal.X, normal.Y, normal.Z,
              tangent.X, tangent.Y, tangent.Z, 0.0f, 0.0f,
              color.getRProperty(), color.getGProperty(), color.getBProperty(), color.getAProperty() },
            {  0.5f, -0.5f, 0.0f, normal.X, normal.Y, normal.Z,
              tangent.X, tangent.Y, tangent.Z, 1.0f, 0.0f,
              color.getRProperty(), color.getGProperty(), color.getBProperty(), color.getAProperty() },
            {  0.5f,  0.5f, 0.0f, normal.X, normal.Y, normal.Z,
              tangent.X, tangent.Y, tangent.Z, 1.0f, 1.0f,
              color.getRProperty(), color.getGProperty(), color.getBProperty(), color.getAProperty() },
        };

        vb->SetDataRaw(verts, 4, sizeof(CustomVertex));
        return vb;
    }

    Color DrawOnce(VertexBuffer& vb)
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
        fx->setWorldProperty(Matrix::getIdentityProperty());
        fx->setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero,
                                                  Vector3(0.0f, 1.0f, 0.0f)));
        fx->setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(
            MathHelper::PiOver4, vp.getAspectRatioProperty(), 0.1f, 100.0f));
        fx->Apply();

        device.SetVertexBuffer(&vb);
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
            std::printf("[FAIL] EasyGLCustomVertexLayout: .cnj load or GLSL compile failed\n");
            Exit();
            return;
        }

        auto vbA = MakeQuad(Vector3(0.2f, 0.4f, 0.6f), Vector3(0.8f, 0.3f, 0.1f),
                            Color(200, 100, 50, 255));
        auto vbB = MakeQuad(Vector3(0.9f, 0.1f, 0.5f), Vector3(0.05f, 0.85f, 0.5f),
                            Color(30, 220, 10, 255));

        const Color a = DrawOnce(*vbA);
        const Color b = DrawOnce(*vbB);

        const auto close = [](int got, int expected) { return got >= expected - 6 && got <= expected + 6; };
        const bool aOk = close(a.getRProperty(), 51) && close(a.getGProperty(), 77) &&
                        close(a.getBProperty(), 128) && close(a.getAProperty(), 200);
        const bool bOk = close(b.getRProperty(), 230) && close(b.getGProperty(), 217) &&
                        close(b.getBProperty(), 128) && close(b.getAProperty(), 30);

        std::printf("[%s] Check A: rgba=(%d,%d,%d,%d) expected~=(51,77,128,200)\n",
                    aOk ? "PASS" : "FAIL", a.getRProperty(), a.getGProperty(), a.getBProperty(), a.getAProperty());
        std::printf("[%s] Check B: rgba=(%d,%d,%d,%d) expected~=(230,217,128,30)\n",
                    bOk ? "PASS" : "FAIL", b.getRProperty(), b.getGProperty(), b.getBProperty(), b.getAProperty());

        result_ = (aOk && bOk) ? 0 : 1;
        Exit();
    }

public:
    EasyGLCustomVertexLayoutTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    EasyGLCustomVertexLayoutTest game;
    game.Run();
    return game.getResult();
}

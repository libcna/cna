// SPDX-License-Identifier: MS-PL
// plans/plan_opengl4.md GL4-33: real hardware instancing (DrawInstancedPrimitivesEx) for the OpenGL4
// graphics renderer -- OpenGL4Renderer previously didn't override DrawInstancedPrimitivesEx
// at all (inherited IGraphicsRenderer's default, which unconditionally throws
// std::runtime_error("DrawInstancedPrimitives is not supported on this graphics renderer.")).
// GraphicsDevice::DrawInstancedPrimitives/SetVertexBuffers/VertexBufferBinding were already fully
// wired at the XNA API layer; the only missing piece was this renderer's own implementation.
//
// This required first adding a generic VertexElement-driven attribute mapper
// (OpenGL4VertexBufferRenderer::SetVertexDeclaration/GetDeclarationElements + ApplyLayout's new
// generic path) -- previously this renderer only recognized a fixed set of byte-strides (16/20/24/
// 32/48/52/56/68), which a per-instance attribute buffer never matches. With a custom
// ShaderEffect (params.customEffectRenderer), DrawInstancedPrimitivesEx now binds the per-instance
// buffer's own attributes generically into the mesh buffer's VAO, continuing at locations right
// after the mesh buffer's own declared attributes, each with glVertexAttribDivisor(location, 1)
// (advance once per instance, not once per vertex) -- then calls the real GL 3.1 core
// glDrawElementsInstanced.
//
// Ports easygl_instancedmodel_shader_test.cpp's own scene, packing derivation, and expected
// values exactly (desktop GLSL 410 core translation only -- no ES precision qualifiers,
// otherwise identical): a small mesh (one quad) is drawn twice in a SINGLE
// DrawInstancedPrimitives call, driven by 2 instances' own 4x4 transform matrices supplied as 4
// consecutive per-instance vec4 attributes (the classic D3D9 hardware-instancing convention this
// project's InstancedModel.fx HLSL->GLSL port already established) -- see that file's own header
// comment for the full row/column-major packing derivation (this test reuses it unchanged, since
// the math is renderer-agnostic).
//
// Check A -- instance 0 (pure translation): local normal unrotated, faces the light head-on,
//   diffuseAmount=1 -- expect pure WHITE (255,255,255,255) at its own on-screen position.
// Check B -- instance 1 (180-degree Y-rotation, THEN translation): world normal flipped away from
//   the light, diffuseAmount=0 -- expect dim gray (~64,64,64,255) at its own, separate on-screen
//   position. Two DIFFERENT colors at two DIFFERENT positions from ONE draw call, driven by real
//   per-instance data (not just "some instance renders somewhere") -- a decisive proof that the
//   per-instance buffer's attributes are genuinely read per-instance (glVertexAttribDivisor=1),
//   not per-vertex (=0, which would read out-of-bounds/garbage data for vertices 2 and 3 of a
//   2-instance, 2-vertex-worth buffer) and not left constant across every instance.
//
// Exit code 0 = both PASS, 1 = either FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"

#include <cstdio>
#include <cstdint>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
#pragma pack(push, 1)
    struct MeshVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
    };
    struct InstanceVertex
    {
        float r0[4], r1[4], r2[4], r3[4];
    };
#pragma pack(pop)
    static_assert(sizeof(MeshVertex) == 32);
    static_assert(sizeof(InstanceVertex) == 64);

    // Packs Matrix::Transpose(instanceModelMatrix)'s own 4 rows as the 4 per-instance vertex
    // attributes -- see easygl_instancedmodel_shader_test.cpp's own header comment for the full
    // derivation (renderer-agnostic, reused verbatim here).
    InstanceVertex PackInstance(const Matrix& instanceModelMatrix)
    {
        const Matrix t = Matrix::Transpose(instanceModelMatrix);
        InstanceVertex iv{};
        iv.r0[0] = t.M11; iv.r0[1] = t.M12; iv.r0[2] = t.M13; iv.r0[3] = t.M14;
        iv.r1[0] = t.M21; iv.r1[1] = t.M22; iv.r1[2] = t.M23; iv.r1[3] = t.M24;
        iv.r2[0] = t.M31; iv.r2[1] = t.M32; iv.r2[2] = t.M33; iv.r2[3] = t.M34;
        iv.r3[0] = t.M41; iv.r3[1] = t.M42; iv.r3[2] = t.M43; iv.r3[3] = t.M44;
        return iv;
    }

    const char* kVertSrc = R"GLSL(
#version 410 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aInstRow0;
layout(location = 4) in vec4 aInstRow1;
layout(location = 5) in vec4 aInstRow2;
layout(location = 6) in vec4 aInstRow3;
out vec4 vColor;
uniform mat4 World;
uniform mat4 View;
uniform mat4 Projection;
uniform vec3 LightDirection;
uniform vec3 DiffuseLight;
uniform vec3 AmbientLight;
void main()
{
    mat4 instanceTransform = transpose(mat4(aInstRow0, aInstRow1, aInstRow2, aInstRow3));
    vec4 worldPosition = instanceTransform * (World * vec4(aPosition, 1.0));
    vec4 viewPosition = View * worldPosition;
    gl_Position = Projection * viewPosition;
    vec3 worldNormal = mat3(instanceTransform) * (mat3(World) * aNormal);
    float diffuseAmount = max(-dot(worldNormal, LightDirection), 0.0);
    vec3 lightingResult = clamp(diffuseAmount * DiffuseLight + AmbientLight, 0.0, 1.0);
    vColor = vec4(lightingResult, 1.0);
}
)GLSL";

    const char* kFragSrc = R"GLSL(
#version 410 core
in vec4 vColor;
out vec4 FragColor;
void main()
{
    FragColor = vColor;
}
)GLSL";
}

class OpenGL4InstancedModelTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<ShaderEffect> fx_;
    std::unique_ptr<IndexBuffer> ib_;
    std::unique_ptr<VertexBuffer> meshVb_;
    std::unique_ptr<VertexBuffer> instVb_;
    bool done_  = false;
    int result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        fx_ = std::make_unique<ShaderEffect>(device, kVertSrc, kFragSrc);

        const VertexDeclaration meshDecl(32, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        });

        meshVb_ = std::make_unique<VertexBuffer>(device, meshDecl, 4, BufferUsage::None);
        const MeshVertex verts[4] = {
            { -0.15f,  0.15f, 0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f },
            { -0.15f, -0.15f, 0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f },
            {  0.15f, -0.15f, 0.0f,  0.0f, 0.0f, 1.0f,  1.0f, 0.0f },
            {  0.15f,  0.15f, 0.0f,  0.0f, 0.0f, 1.0f,  1.0f, 1.0f },
        };
        meshVb_->SetDataRaw(verts, 4, sizeof(MeshVertex));

        const std::uint16_t indices[6] = { 0, 1, 2, 0, 2, 3 };
        ib_ = std::make_unique<IndexBuffer>(device, 6);
        ib_->SetData(indices, 6);

        const VertexDeclaration instDecl(64, {
            VertexElement(0,  VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0),
            VertexElement(16, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 1),
            VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 2),
            VertexElement(48, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 3),
        });

        instVb_ = std::make_unique<VertexBuffer>(device, instDecl, 2, BufferUsage::None);

        const Matrix instance0 = Matrix::CreateTranslation(-0.3f, 0.0f, 0.0f);
        const Matrix instance1 = Matrix::CreateRotationY(MathHelper::Pi) *
                                  Matrix::CreateTranslation(0.3f, 0.0f, 0.0f);

        const InstanceVertex instData[2] = { PackInstance(instance0), PackInstance(instance1) };
        instVb_->SetDataRaw(instData, 2, sizeof(InstanceVertex));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        if (!fx_->IsEffectValid())
        {
            std::printf("[FAIL] OpenGL4InstancedModel: GLSL compile failed\n");
            Exit();
            return;
        }

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int H = vp.getHeightProperty();

        device.Clear(Color(10, 10, 10, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        fx_->setWorldProperty(Matrix::getIdentityProperty());
        fx_->setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero,
                                                   Vector3(0.0f, 1.0f, 0.0f)));
        fx_->setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(
            MathHelper::PiOver4, vp.getAspectRatioProperty(), 0.1f, 100.0f));
        fx_->Apply();
        fx_->SetUniformVec3("LightDirection", 0.0f, 0.0f, -1.0f);
        fx_->SetUniformVec3("DiffuseLight", 1.25f, 1.25f, 1.25f);
        fx_->SetUniformVec3("AmbientLight", 0.25f, 0.25f, 0.25f);

        device.SetVertexBuffers({
            VertexBufferBinding(meshVb_.get()),
            VertexBufferBinding(instVb_.get(), 0, 1),
        });
        device.setIndicesProperty(ib_.get());
        device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, 2);

        Color left(0, 0, 0, 0);
        Color right(0, 0, 0, 0);
        const Rectangle leftRect(24, H / 2, 1, 1);
        const Rectangle rightRect(40, H / 2, 1, 1);
        device.GetBackBufferData(&leftRect, &left, 0, 1);
        device.GetBackBufferData(&rightRect, &right, 0, 1);

        const auto close = [](int got, int expected) { return got >= expected - 6 && got <= expected + 6; };
        const bool leftOk = close(left.getRProperty(), 255) && close(left.getGProperty(), 255) &&
                           close(left.getBProperty(), 255) && close(left.getAProperty(), 255);
        const bool rightOk = close(right.getRProperty(), 64) && close(right.getGProperty(), 64) &&
                            close(right.getBProperty(), 64) && close(right.getAProperty(), 255);

        std::printf("[%s] Check A (instance 0, translate-only): rgba=(%d,%d,%d,%d) expected~=(255,255,255,255)\n",
                    leftOk ? "PASS" : "FAIL", left.getRProperty(), left.getGProperty(),
                    left.getBProperty(), left.getAProperty());
        std::printf("[%s] Check B (instance 1, rotate+translate): rgba=(%d,%d,%d,%d) expected~=(64,64,64,255)\n",
                    rightOk ? "PASS" : "FAIL", right.getRProperty(), right.getGProperty(),
                    right.getBProperty(), right.getAProperty());

        result_ = (leftOk && rightOk) ? 0 : 1;
        Exit();
    }

public:
    OpenGL4InstancedModelTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    OpenGL4InstancedModelTest game;
    game.Run();
    return game.getResult();
}

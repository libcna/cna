// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: real GPU hardware-instancing proof via DrawInstancedPrimitivesEx -- adapts
// examples/easygl_instancedmodel_shader_test.cpp's own scene, math derivation, and expected
// values verbatim to GLSL 1.10 syntax (`attribute`/`varying`, no `#version 300 es`, no explicit
// `layout(location=N)` -- this renderer binds custom-effect attributes by NAME via
// glGetAttribLocation, same as every other custom ShaderEffect test in this renderer).
//
// Until this task, IGraphicsRenderer::DrawInstancedPrimitivesEx() was not overridden on OpenGL2 --
// the shared base-class default threw std::runtime_error unconditionally (the correct, honest
// behavior for a genuinely unsupported feature, not a bug). GL 2.1 has no instancing in core, but
// GL_ARB_draw_instanced (glDrawElementsInstancedARB) + GL_ARB_instanced_arrays
// (glVertexAttribDivisorARB) are widely-supported extensions on real GL2.1-era desktop
// drivers/GPUs -- the per-instance-attribute-divisor technique needs no gl_InstanceID, so plain
// GLSL 1.10 is sufficient. Implemented for the custom-ShaderEffect path only (mirrors
// EasyGLRenderer::DrawInstancedPrimitivesEx's own identical, documented scope reduction: a
// non-1 VertexBufferBinding.InstanceFrequency is not threaded through here either).
//
// Scene: a single small quad mesh drawn TWICE in one DrawInstancedPrimitives call, with a second,
// per-instance vertex stream supplying each instance's own world transform (the classic D3D9/XNA
// hardware-instancing convention: 4 consecutive Vector4 attributes packed as
// transpose(instanceTransform)'s own 4 rows, reconstructed in the shader). A per-instance-varying
// readback (different lighting result per instance, from ONE draw call) proves real per-instance
// data is being read, not just "some instance renders somewhere":
//   Instance 0: pure translation (-0.3,0,0) -- local normal (0,0,1) is unrotated by a pure
//     translation, so worldNormal=(0,0,1); with LightDirection=(0,0,-1), diffuseAmount=1,
//     saturating the lighting result to exactly 1 -- expect pure WHITE (255,255,255,255).
//   Instance 1: CreateRotationY(Pi) * CreateTranslation(0.3,0,0) -- flips the quad's world normal
//     to (0,0,-1), facing away from the light: diffuseAmount=0, lightingResult=saturate(0.25) --
//     expect dim gray (~64,64,64,255). RasterizerState::CullNone is required: the 180 degree
//     Y-rotation flips the quad's winding as seen by the camera.
//
// Exit code 0 = both PASS (or SKIP if GL_ARB_draw_instanced/GL_ARB_instanced_arrays are genuinely
// unavailable on this driver -- checked via GraphicsDevice::SupportsCapability first, matching
// this capability's own documented device/driver-dependent nature), 1 = either FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "CNA/GraphicsCapability.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include "common/PixelTestGame.hpp"

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
    static_assert(sizeof(MeshVertex) == 32, "MeshVertex must be 32 bytes");
    static_assert(sizeof(InstanceVertex) == 64, "InstanceVertex must be 64 bytes");

    // Packs Matrix::Transpose(instanceModelMatrix)'s own 4 rows as the 4 per-instance vertex
    // attributes -- see easygl_instancedmodel_shader_test.cpp's own header comment for the full
    // row/column algebra derivation this reuses verbatim.
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

    // GLSL 1.10: per-instance attributes are aBoneWeight/aBoneWeight1/aBoneWeight2/aBoneWeight3
    // (VertexElementUsage::BlendWeight, usage index 0-3 -- this renderer's own established
    // attribute-name convention for that usage: usage index 0 gets the bare base name, no numeric
    // suffix, see VertexElementAttribName() in OpenGL2Renderer.cpp; matches XNA/D3D9's own
    // BLENDWEIGHT0-3 hardware-instancing semantic that easygl_instancedmodel_shader_test.cpp's own
    // header comment derives this packing from).
    const char* kVertSrc =
        "attribute vec3 aPosition;attribute vec3 aNormal;attribute vec2 aTexCoord;"
        "attribute vec4 aBoneWeight;attribute vec4 aBoneWeight1;"
        "attribute vec4 aBoneWeight2;attribute vec4 aBoneWeight3;"
        "varying vec4 vColor;"
        "uniform mat4 World;uniform mat4 View;uniform mat4 Projection;"
        "uniform vec3 LightDirection;uniform vec3 DiffuseLight;uniform vec3 AmbientLight;"
        "void main(){"
        "mat4 instanceTransform=mat4(aBoneWeight,aBoneWeight1,aBoneWeight2,aBoneWeight3);"
        "instanceTransform=mat4("
        "instanceTransform[0][0],instanceTransform[1][0],instanceTransform[2][0],instanceTransform[3][0],"
        "instanceTransform[0][1],instanceTransform[1][1],instanceTransform[2][1],instanceTransform[3][1],"
        "instanceTransform[0][2],instanceTransform[1][2],instanceTransform[2][2],instanceTransform[3][2],"
        "instanceTransform[0][3],instanceTransform[1][3],instanceTransform[2][3],instanceTransform[3][3]);"
        "vec4 worldPosition=instanceTransform*(World*vec4(aPosition,1.0));"
        "vec4 viewPosition=View*worldPosition;"
        "gl_Position=Projection*viewPosition;"
        // mat3(mat4) truncation requires GLSL 1.20 (this file targets 1.10, matching every other
        // shader in this renderer's own identical mat3(m[0].xyz,m[1].xyz,m[2].xyz) idiom).
        "mat3 instanceTransform3=mat3(instanceTransform[0].xyz,instanceTransform[1].xyz,instanceTransform[2].xyz);"
        "mat3 world3=mat3(World[0].xyz,World[1].xyz,World[2].xyz);"
        "vec3 worldNormal=instanceTransform3*(world3*aNormal);"
        "float diffuseAmount=max(-dot(worldNormal,LightDirection),0.0);"
        "vec3 lightingResult=clamp(diffuseAmount*DiffuseLight+AmbientLight,0.0,1.0);"
        "vColor=vec4(lightingResult,1.0);"
        "}";
    const char* kFragSrc =
        "varying vec4 vColor;"
        "void main(){gl_FragData[0]=vColor;}";
}

class OpenGL2InstancedModelTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<ShaderEffect> fx_;
    std::unique_ptr<IndexBuffer> ib_;
    std::unique_ptr<VertexBuffer> meshVb_;
    std::unique_ptr<VertexBuffer> instVb_;
    bool done_  = false;
    int  result_ = 1;

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

        auto& device = getGraphicsDeviceProperty();

        if (!device.SupportsCapability(CNA::GraphicsCapability::Instancing))
        {
            std::printf("[SKIP] GL_ARB_draw_instanced/GL_ARB_instanced_arrays unavailable on this driver\n");
            result_ = 0;
            Exit();
            return;
        }

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
    OpenGL2InstancedModelTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    OpenGL2InstancedModelTest game;
    game.Run();
    return game.getResult();
}

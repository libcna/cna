// SPDX-License-Identifier: MS-PL
// Task 947 (final sample) + Task 1082: HLSL->GLSL shader-conversion proof for
// InstancedModelSample's `InstancedModel.fx`, AND the real GPU hardware-instancing capability
// it depends on -- the last of the 13 Task 947 samples blocked purely by DEFERRED.md #11.
//
// FNA reference (`InstancedModelSample_4_0/InstancedModelSample/Content/InstancedModel.fx`):
//   VertexShaderOutput VertexShaderCommon(VertexShaderInput input, float4x4 instanceTransform)
//   {
//       float4 worldPosition = mul(input.Position, instanceTransform);
//       float4 viewPosition = mul(worldPosition, View);
//       output.Position = mul(viewPosition, Projection);
//       float3 worldNormal = mul(input.Normal, instanceTransform);
//       float diffuseAmount = max(-dot(worldNormal, LightDirection), 0);
//       output.Color = float4(saturate(diffuseAmount * DiffuseLight + AmbientLight), 1);
//   }
//   VertexShaderOutput HardwareInstancingVertexShader(VertexShaderInput input,
//                                                     float4x4 instanceTransform : BLENDWEIGHT)
//   { return VertexShaderCommon(input, mul(World, transpose(instanceTransform))); }
//
// Unlike every prior shader this session, `instanceTransform` is not an uploaded uniform -- it is
// read from a SECOND, per-instance vertex stream (4 consecutive Vector4 attributes at
// BLENDWEIGHT0-3, the classic D3D9 hardware-instancing convention: HLSL reconstructs these 4
// stream elements into a float4x4 parameter's 4 ROWS, in declaration order). Task 1082 closes the
// renderer gap for this (see EasyGLRenderer.cpp's `DrawInstancedPrimitivesEx`). REMED-GFX-122
// additionally proves that the custom-effect branch keeps the complete public range: the mesh
// stream begins at VertexOffset=4, baseVertex=1 selects the real quad after one more decoy vertex,
// startIndex=3 skips an index-prefix triangle, and the instance stream begins at VertexOffset=1.
// Its attributes continue at the GLSL locations immediately after the mesh declaration and use
// the exact public InstanceFrequency divisor.
//
// Packing derivation (this test controls both the upload and the shader, so it is free to choose
// any packing that reproduces the correct math -- see plans/plan_graphics.md Task 1082 for the full
// derivation): let C = mul(World, transpose(instanceTransform)) be the HLSL-side combined matrix
// used for both position and normal. Working the row/column algebra through this session's
// established `mul(v,M)[row-vector] == M^T * v[GLSL column-vector]` identity gives:
//   worldPosition_glsl = IT * (World_glsl * Position)
// where `World_glsl` is `World` uploaded via the usual `ToColumnMajor()` uniform convention (used
// as-is, no extra transpose), and `IT` is a GLSL mat4 constructed directly from the 4 raw
// per-instance vertex attributes such that IT's ROWS equal those 4 attributes (i.e.
// `transpose(mat4(row0,row1,row2,row3))`, since GLSL's `mat4(...)` constructor takes its 4 vec4
// arguments as COLUMNS, not rows). This test therefore uploads, as the 4 per-instance attributes,
// the 4 ROWS of `Matrix::Transpose(instanceModelMatrix)` for each instance's desired world
// transform -- verified algebraically and by hand-tracing a concrete translation example before
// writing this file. The same construction, truncated to mat3, is used for the normal.
//
// Check design -- 2 instances drawn in ONE nonzero-offset DrawInstancedPrimitives call, from a
// single small (0.3x0.3 world unit) quad mesh, so a per-instance-varying readback proves real
// per-instance data (not just "some instance renders somewhere"):
//   Instance 0: pure translation (-0.3,0,0). Local normal (0,0,1) is unrotated by a pure
//     translation, so worldNormal=(0,0,1); with LightDirection=(0,0,-1) (this test's own simpler
//     substitute for XNA's normalize(-1,-1,-1), chosen since only alignment with a flat quad's
//     normal is being tested, matching this session's established practice of substituting a
//     convenient concrete light for the sample's own default), diffuseAmount=1, saturating
//     lightingResult to exactly 1 -- expect pure WHITE (255,255,255,255) at its on-screen position.
//   Instance 1: CreateRotationY(Pi) * CreateTranslation(0.3,0,0) (rotate 180 degrees about its own
//     centre, THEN translate -- flips the quad's world normal to (0,0,-1), facing away from the
//     light: diffuseAmount=0, lightingResult=saturate(0.25)=0.25 -- expect dim gray (~64,64,64,255)
//     at its own, separate on-screen position. RasterizerState::CullNone is required: the 180
//     degree Y-rotation flips the quad's winding as seen by the camera.
// The mesh's lighting is computed per-vertex (Gouraud, like ShatterEffect.fx's own shader) but
// every vertex of a given instance shares the same local normal and the same instanceTransform, so
// (unlike ShatterEffect.fx) there is no per-vertex-varying-normal interpolation to introduce
// precision drift -- the two expected colors are exact, not approximate.
// Texturing is intentionally dropped (FragColor = vColor only, no tex2D sample) -- texture
// sampling through this same custom-vertex-layout path is already proven by
// `easygl_shadereffect_custom_vertex_layout_test.cpp` and several other shaders this session; this
// test isolates the one genuinely new thing, per-instance vertex-stream reads.
//
// Mutation test performed manually during development (not re-run automatically by this binary):
// changing the new `vao.set_attribute_divisor(location, 1)` in `DrawInstancedPrimitivesEx` to `0`
// makes the per-instance attributes advance per-VERTEX instead of per-INSTANCE, reading
// out-of-bounds records from the 3-record (192-byte) buffer for later vertices -- confirmed
// both checks fail (garbage/degenerate transform) before reverting.
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
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
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
    // attributes -- see this file's header comment for the full derivation.
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

    const char* kVertSrc = R"(#version 300 es
precision highp float;
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
void main() {
    mat4 instanceTransform = transpose(mat4(aInstRow0, aInstRow1, aInstRow2, aInstRow3));
    vec4 worldPosition = instanceTransform * (World * vec4(aPosition, 1.0));
    vec4 viewPosition = View * worldPosition;
    gl_Position = Projection * viewPosition;
    vec3 worldNormal = mat3(instanceTransform) * (mat3(World) * aNormal);
    float diffuseAmount = max(-dot(worldNormal, LightDirection), 0.0);
    vec3 lightingResult = clamp(diffuseAmount * DiffuseLight + AmbientLight, 0.0, 1.0);
    vColor = vec4(lightingResult, 1.0);
}
)";

    const char* kFragSrc = R"(#version 300 es
precision highp float;
in vec4 vColor;
out vec4 FragColor;
void main() {
    FragColor = vColor;
}
)";
}

class EasyGLInstancedModelTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::shared_ptr<Effect> fxBase_;
    std::unique_ptr<IndexBuffer> ib_;
    std::unique_ptr<VertexBuffer> meshVb_;
    std::unique_ptr<VertexBuffer> instVb_;
    bool done_  = false;
    int result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();

        const auto root = std::filesystem::temp_directory_path()
            / ("cna_instancedmodel_test_" + std::to_string(
                   reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        WriteFile(root / "im.vert.glsl", kVertSrc);
        WriteFile(root / "im.frag.glsl", kFragSrc);
        WriteFile(root / "im.cnj", R"({
  "cnjVersion": 1,
  "type": "Effect",
  "vertex": "im.vert.glsl",
  "fragment": "im.frag.glsl"
})");

        getContentProperty().setRootDirectoryProperty(root.string());
        fxBase_ = getContentProperty().Load<std::shared_ptr<Effect>>("im");

        auto& device = getGraphicsDeviceProperty();

        const VertexDeclaration meshDecl(32, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        });

        // Five asymmetric decoy records precede the real quad. VertexOffset=4 and baseVertex=1
        // below must both be applied exactly once; dropping either consumes a decoy or a mixed
        // decoy/real triangle instead of the centred quad.
        meshVb_ = std::make_unique<VertexBuffer>(device, meshDecl, 9, BufferUsage::None);
        const MeshVertex verts[9] = {
            {  3.0f,  3.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f },
            {  3.2f,  3.0f, 0.0f,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f },
            {  3.0f,  3.2f, 0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f },
            { -3.0f, -3.0f, 0.0f, -1.0f, 0.0f, 0.0f,  0.0f, 0.0f },
            { -3.2f, -3.0f, 0.0f,  0.0f,-1.0f, 0.0f,  0.0f, 0.0f },
            { -0.15f,  0.15f, 0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f },
            { -0.15f, -0.15f, 0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f },
            {  0.15f, -0.15f, 0.0f,  0.0f, 0.0f, 1.0f,  1.0f, 0.0f },
            {  0.15f,  0.15f, 0.0f,  0.0f, 0.0f, 1.0f,  1.0f, 1.0f },
        };
        meshVb_->SetDataRaw(verts, 9, sizeof(MeshVertex));

        // The first three index elements form a decoy triangle. startIndex=3 selects only the
        // following quad range and therefore distinguishes an element offset from a byte offset.
        const std::uint16_t indices[9] = { 0, 0, 0, 0, 1, 2, 0, 2, 3 };
        ib_ = std::make_unique<IndexBuffer>(device, 9);
        ib_->SetData(indices, 9);

        const VertexDeclaration instDecl(64, {
            VertexElement(0,  VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0),
            VertexElement(16, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 1),
            VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 2),
            VertexElement(48, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 3),
        });

        instVb_ = std::make_unique<VertexBuffer>(device, instDecl, 3, BufferUsage::None);

        const Matrix decoyInstance = Matrix::CreateTranslation(0.0f, 4.0f, 0.0f);
        const Matrix instance0 = Matrix::CreateTranslation(-0.3f, 0.0f, 0.0f);
        const Matrix instance1 = Matrix::CreateRotationY(MathHelper::Pi) *
                                  Matrix::CreateTranslation(0.3f, 0.0f, 0.0f);

        const InstanceVertex instData[3] = {
            PackInstance(decoyInstance), PackInstance(instance0), PackInstance(instance1) };
        instVb_->SetDataRaw(instData, 3, sizeof(InstanceVertex));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto* fx = dynamic_cast<ShaderEffect*>(fxBase_.get());
        if (!fx || !fx->IsEffectValid())
        {
            std::printf("[FAIL] EasyGLInstancedModel: .cnj load or GLSL compile failed\n");
            Exit();
            return;
        }

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(Color(10, 10, 10, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        fx->setWorldProperty(Matrix::getIdentityProperty());
        fx->setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero,
                                                  Vector3(0.0f, 1.0f, 0.0f)));
        fx->setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(
            MathHelper::PiOver4, vp.getAspectRatioProperty(), 0.1f, 100.0f));
        fx->Apply();
        fx->SetUniformVec3("LightDirection", 0.0f, 0.0f, -1.0f);
        fx->SetUniformVec3("DiffuseLight", 1.25f, 1.25f, 1.25f);
        fx->SetUniformVec3("AmbientLight", 0.25f, 0.25f, 0.25f);

        device.SetVertexBuffers({
            VertexBufferBinding(meshVb_.get(), 4, 0),
            VertexBufferBinding(instVb_.get(), 1, 1),
        });
        device.setIndicesProperty(ib_.get());
        device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 1, 0, 4, 3, 2, 2);

        Color left(0, 0, 0, 0);
        Color right(0, 0, 0, 0);
        const Rectangle leftRect(24, H / 2, 1, 1);
        const Rectangle rightRect(40, H / 2, 1, 1);
        device.GetBackBufferData(&leftRect, &left, 0, 1);
        device.GetBackBufferData(&rightRect, &right, 0, 1);
        (void)W;

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
    EasyGLInstancedModelTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    EasyGLInstancedModelTest game;
    game.Run();
    return game.getResult();
}

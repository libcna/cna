// SPDX-License-Identifier: MS-PL
// Task 947 (Phase 78 rollout): HLSL->GLSL shader-conversion proof -- NormalMappingEffect's
// `NormalMapping.fx` (tangent-space normal mapping with Phong specular). This sample's own
// vertex format (Position+TexCoord+Normal+Binormal+Tangent, stride 56) matches none of CNA's 5
// built-in strides -- unblocked by Task 1080's genuinely-custom-vertex-layout capability
// (see easygl_shadereffect_custom_vertex_layout_test.cpp for that capability's own proof).
//
// FNA reference (`NormalMappingSample_4_0/NormalMappingEffect/Content/NormalMapping.fx`):
//   VS_OUTPUT VertexShaderFunction(VS_INPUT input)
//   {
//       float4 worldSpacePos = mul(input.position, World);
//       output.position = mul(worldSpacePos, View);
//       output.position = mul(output.position, Projection);
//       output.lightDirection = LightPosition - worldSpacePos;
//       float3 eyePosition = mul(-View._m30_m31_m32, transpose(View));
//       output.viewDirection = worldSpacePos - eyePosition;
//       output.tangentToWorld[0] = mul(input.tangent,  World);
//       output.tangentToWorld[1] = mul(input.binormal, World);
//       output.tangentToWorld[2] = mul(input.normal,   World);
//       output.texCoord = input.texCoord;
//       return output;
//   }
//   float4 PixelShaderFunction(VS_OUTPUT input) : COLOR0
//   {
//       float3 normalFromMap = tex2D(NormalMapSampler, input.texCoord);
//       normalFromMap = mul(normalFromMap, input.tangentToWorld);
//       normalFromMap = normalize(normalFromMap);
//       input.viewDirection = normalize(input.viewDirection);
//       input.lightDirection = normalize(input.lightDirection);
//       float nDotL = max(dot(normalFromMap, input.lightDirection), 0);
//       float4 diffuse = LightColor * nDotL;
//       float3 reflectedLight = reflect(input.lightDirection, normalFromMap);
//       float rDotV = max(dot(reflectedLight, input.viewDirection), 0);
//       float4 specular = Shininess * LightColor * pow(rDotV, SpecularPower);
//       float4 diffuseTexture = tex2D(DiffuseTextureSampler, input.texCoord);
//       return (diffuse + AmbientLightColor) * diffuseTexture + specular;
//   }
//
// Ported 1:1 below, including 2 non-obvious HLSL behaviours preserved verbatim, not "fixed":
//
// (1) No `[0,1]->[-1,1]` remap on the sampled normal map (`normalFromMap = tex2D(...)` used
// directly, just `normalize()`d afterwards) -- unlike the usual normal-mapping convention of
// unpacking a `*2-1` bias. This is the HLSL source exactly as written; ported faithfully. A
// practical consequence: since a normalized-ubyte texture sample is always componentwise >= 0,
// the tangent/binormal (X/Y) contributions to `normalFromMap` can only ever be non-negative in
// this port too -- irrelevant to this test's own correctness, just documenting the inherited
// constraint.
//
// (2) `mul(-View._m30_m31_m32, transpose(View))` extracts the camera's world-space eye position
// from the View matrix without a full 4x4 inverse (a standard D3D/XNA identity: the row-vector
// form `mul(w, transpose(V))` is algebraically `V * w` as a column-vector product). Given this
// project's own `Matrix::ToColumnMajor()` convention (GLSL's uploaded `View` already equals HLSL's
// `View` *transposed*, established and verified across every 3D shader ported this session), the
// GLSL port is `transpose(mat3(View)) * (-View[3].xyz)` -- `transpose(mat3(View))` recovers HLSL's
// original (non-transposed) View, and `View[3]` (GLSL column 3) equals HLSL's row 3
// (`_m30_m31_m32`), by the same already-established row<->column correspondence. Hand-verified
// against this test's own camera (eye=(0,0,3), target=origin, up=(0,1,0), an on-axis, rotation-
// free View) to reproduce eyePosition=(0,0,3) exactly. **Scope note**: this specific camera makes
// View's rotation part the identity matrix, so the `transpose()` in this formula is not
// independently exercised by this test (a rotation-free View is its own transpose) -- documented,
// not attempted to fix, since proving the *rest* of this substantially larger shader (TBN
// construction from 3 real Normal/Binormal/Tangent attributes, 2-texture normal+diffuse sampling,
// combined diffuse+specular+ambient) is this test's real target.
//
// `mul(v, tangentToWorld)` (row-vector times a `float3x3` whose rows are Tangent/Binormal/Normal,
// assigned via `tangentToWorld[0..2]`) is ported as the equivalent weighted sum
// `v.x*tangent + v.y*binormal + v.z*normal` (verified algebraically identical to the row-vector
// matrix product) rather than constructing an actual GLSL `mat3`, avoiding GLSL/HLSL matrix
// row/column ambiguity for a matrix that (unlike World/View/Projection) is never itself an
// uploaded uniform -- a natural GLSL expression of the same math, not a behavioural deviation.
//
// Custom vertex layout (Task 1080), matching VS_INPUT's own field order exactly:
//   location 0: Position (Vector3, offset  0)      location 1: TexCoord (Vector2, offset 12)
//   location 2: Normal   (Vector3, offset 20)       location 3: Binormal (Vector3, offset 32)
//   location 4: Tangent  (Vector3, offset 44)        -- stride 56, matches none of CNA's 5 built-ins.
//
// Test setup: quad centred at the origin (corners +-0.5, Z=0, standard 0..1 UVs), local
// Normal=(0,0,1)/Tangent=(1,0,0)/Binormal=(0,1,0) (a +Z-facing quad's natural orthonormal basis),
// `World=Identity` (so `worldSpacePos` at the quad's own on-screen centre is exactly the origin,
// and `tangentToWorld`'s rows are exactly Tangent/Binormal/Normal unchanged -- this project's
// standard "quad centred at the origin" hand-derivation trick, used throughout this session).
// Camera as above; `LightPosition=(0,0,5)` gives `lightDirection=(0,0,5)`, normalized `(0,0,1)`.
// `LightColor=(0.6,0.6,0.6,0.5)`, `AmbientLightColor=(0.05,0.05,0.05,0)`, `Shininess=0.1`,
// **`SpecularPower=1`** (a deliberately simple test-only exponent -- keeps `pow()` an exact
// identity rather than risking float-precision drift from a large realistic exponent; a test
// parameter choice, not a shader-logic change). `DiffuseTextureSampler` texel = solid
// `(200,100,50,255)`.
//
// Check A -- NormalMap texel = `(0,0,255,255)` (tangent-space normal straight along +Z, the local
//   geometric normal exactly): with `World=Identity`'s trivial TBN, `normalFromMap` = `(0,0,1)`
//   unchanged. `nDotL=1`, `reflectedLight=(0,0,-1)`, `rDotV=1`, `pow(1,1)=1`.
//   diffuse=(0.6,0.6,0.6,0.5); +ambient=(0.65,0.65,0.65,0.5); *texture=(0.5098,0.2549,0.1275,0.5);
//   specular=0.1*(0.6,0.6,0.6,0.5)=(0.06,0.06,0.06,0.05).
//   Final ~= (0.5698,0.3149,0.1875,0.55) -> byte (145,80,48,140).
// Check B -- NormalMap texel = `(128,128,255,255)` (a tilted tangent-space normal, exercising the
//   tangent/binormal contributions the trivial texel in Check A cannot): local normal before
//   normalize = (0.502,0.502,1.0), magnitude 1.2263 -> normalized (0.4093,0.4093,0.8155).
//   nDotL=0.8155 (=normal.z, since lightDir=(0,0,1)); reflectedLight=(-0.6677,-0.6677,-0.3300);
//   rDotV=0.3300 (=-reflectedLight.z, since viewDir=(0,0,-1)); pow(0.3300,1)=0.3300.
//   diffuse=(0.4893,0.4893,0.4893,0.4077); +ambient=(0.5393,...,0.4077);
//   *texture=(0.4230,0.2115,0.1057,0.4077); specular=0.1*(0.6,0.6,0.6,0.5)*0.3300=(0.0198,...,0.0165).
//   Final ~= (0.4428,0.2313,0.1255,0.4243) -> byte (113,59,32,108). Distinct R/G/B/A from Check A
//   proves the normal map genuinely reaches the TBN transform and the whole lighting formula, not
//   a fluke value at the trivial texel.
//
// **Discriminating power verified by 2 mutations**: (1) swapped the Binormal/Normal roles in the
// TBN weighted sum (`normalFromMap.y*vNormalWorld + normalFromMap.z*vBinormalWorld` instead of the
// correct `.y*vBinormalWorld + .z*vNormalWorld`) -- both checks failed with drastically different
// (wrong-basis) colours, confirming the weighted sum genuinely uses the right vertex attribute in
// the right slot; (2) dropped `AmbientLightColor` from the sum entirely -- both checks' R channel
// alone shifted by ~10/255 (`0.05 * diffuseTexture.r`), outside this test's own +-6 tolerance,
// reliably failing both. Both mutations reverted, reconfirmed 2/2 PASS.
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
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"

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

    // Matches VS_INPUT's own field order: Position, TexCoord, Normal, Binormal, Tangent.
#pragma pack(push, 1)
    struct NormalMappingVertex
    {
        float px, py, pz;
        float u, v;
        float nx, ny, nz;
        float bx, by, bz;
        float tx, ty, tz;
    };
#pragma pack(pop)
    static_assert(sizeof(NormalMappingVertex) == 56);

    const char* kVertSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in vec3 aBinormal;
layout(location = 4) in vec3 aTangent;
out vec2 vTexCoord;
out vec3 vLightDirection;
out vec3 vViewDirection;
out vec3 vTangentWorld;
out vec3 vBinormalWorld;
out vec3 vNormalWorld;
uniform mat4 World;
uniform mat4 View;
uniform mat4 Projection;
uniform vec3 LightPosition;
void main() {
    vec4 worldSpacePos = World * vec4(aPosition, 1.0);
    gl_Position = Projection * View * worldSpacePos;

    vLightDirection = LightPosition - worldSpacePos.xyz;

    vec3 eyePosition = transpose(mat3(View)) * (-View[3].xyz);
    vViewDirection = worldSpacePos.xyz - eyePosition;

    vTangentWorld  = mat3(World) * aTangent;
    vBinormalWorld = mat3(World) * aBinormal;
    vNormalWorld   = mat3(World) * aNormal;

    vTexCoord = aTexCoord;
}
)";

    const char* kFragSrc = R"(#version 300 es
precision highp float;
in vec2 vTexCoord;
in vec3 vLightDirection;
in vec3 vViewDirection;
in vec3 vTangentWorld;
in vec3 vBinormalWorld;
in vec3 vNormalWorld;
out vec4 FragColor;
uniform sampler2D NormalMapSampler;
uniform sampler2D DiffuseTextureSampler;
uniform vec4 LightColor;
uniform vec4 AmbientLightColor;
uniform float Shininess;
uniform float SpecularPower;
void main() {
    vec3 normalFromMap = texture(NormalMapSampler, vTexCoord).rgb;
    normalFromMap = normalFromMap.x * vTangentWorld
                  + normalFromMap.y * vBinormalWorld
                  + normalFromMap.z * vNormalWorld;
    normalFromMap = normalize(normalFromMap);

    vec3 viewDir = normalize(vViewDirection);
    vec3 lightDir = normalize(vLightDirection);

    float nDotL = max(dot(normalFromMap, lightDir), 0.0);
    vec4 diffuse = LightColor * nDotL;

    vec3 reflectedLight = reflect(lightDir, normalFromMap);
    float rDotV = max(dot(reflectedLight, viewDir), 0.0);
    vec4 specular = Shininess * LightColor * pow(rDotV, SpecularPower);

    vec4 diffuseTexture = texture(DiffuseTextureSampler, vTexCoord);

    FragColor = (diffuse + AmbientLightColor) * diffuseTexture + specular;
}
)";
}

class EasyGLNormalMappingTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::shared_ptr<Effect> fxBase_;
    std::unique_ptr<VertexBuffer> vb_;
    std::unique_ptr<IndexBuffer> ib_;
    Texture2D diffuseTex_;
    Texture2D normalMapA_;
    Texture2D normalMapB_;
    bool done_   = false;
    int result_  = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        const auto root = std::filesystem::temp_directory_path()
            / ("cna_normalmapping_test_" + std::to_string(
                   reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        WriteFile(root / "nm.vert.glsl", kVertSrc);
        WriteFile(root / "nm.frag.glsl", kFragSrc);
        WriteFile(root / "nm.cnj", R"({
  "cnjVersion": 1,
  "type": "Effect",
  "vertex": "nm.vert.glsl",
  "fragment": "nm.frag.glsl"
})");

        getContentProperty().setRootDirectoryProperty(root.string());
        fxBase_ = getContentProperty().Load<std::shared_ptr<Effect>>("nm");

        const VertexDeclaration decl(56, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            VertexElement(20, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(32, VertexElementFormat::Vector3, VertexElementUsage::Binormal, 0),
            VertexElement(44, VertexElementFormat::Vector3, VertexElementUsage::Tangent, 0),
        });

        const float n[3] = { 0.0f, 0.0f, 1.0f };
        const float b[3] = { 0.0f, 1.0f, 0.0f };
        const float t[3] = { 1.0f, 0.0f, 0.0f };
        const NormalMappingVertex verts[4] = {
            { -0.5f,  0.5f, 0.0f,  0.0f, 1.0f,  n[0],n[1],n[2],  b[0],b[1],b[2],  t[0],t[1],t[2] },
            { -0.5f, -0.5f, 0.0f,  0.0f, 0.0f,  n[0],n[1],n[2],  b[0],b[1],b[2],  t[0],t[1],t[2] },
            {  0.5f, -0.5f, 0.0f,  1.0f, 0.0f,  n[0],n[1],n[2],  b[0],b[1],b[2],  t[0],t[1],t[2] },
            {  0.5f,  0.5f, 0.0f,  1.0f, 1.0f,  n[0],n[1],n[2],  b[0],b[1],b[2],  t[0],t[1],t[2] },
        };

        vb_ = std::make_unique<VertexBuffer>(device, decl, 4, BufferUsage::None);
        vb_->SetDataRaw(verts, 4, sizeof(NormalMappingVertex));

        const std::uint16_t indices[6] = { 0, 1, 2, 0, 2, 3 };
        ib_ = std::make_unique<IndexBuffer>(device, 6);
        ib_->SetData(indices, 6);

        diffuseTex_ = Texture2D::CreateFromPixels(device, 1, 1,
            std::vector<std::uint8_t>{ 200, 100, 50, 255 });
        normalMapA_ = Texture2D::CreateFromPixels(device, 1, 1,
            std::vector<std::uint8_t>{ 0, 0, 255, 255 });
        normalMapB_ = Texture2D::CreateFromPixels(device, 1, 1,
            std::vector<std::uint8_t>{ 128, 128, 255, 255 });
    }

    Color DrawOnce(Texture2D& normalMap)
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

        fx->SetTexture(0, normalMap);
        fx->SetUniformInt("NormalMapSampler", 0);
        fx->SetTexture(1, diffuseTex_);
        fx->SetUniformInt("DiffuseTextureSampler", 1);
        fx->SetUniformVec3("LightPosition", 0.0f, 0.0f, 5.0f);
        fx->SetUniformVec4("LightColor", 0.6f, 0.6f, 0.6f, 0.5f);
        fx->SetUniformVec4("AmbientLightColor", 0.05f, 0.05f, 0.05f, 0.0f);
        fx->SetUniformFloat("Shininess", 0.1f);
        fx->SetUniformFloat("SpecularPower", 1.0f);

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
            std::printf("[FAIL] EasyGLNormalMapping: .cnj load or GLSL compile failed\n");
            Exit();
            return;
        }

        const Color a = DrawOnce(normalMapA_);
        const Color b = DrawOnce(normalMapB_);

        const auto close = [](int got, int expected) { return got >= expected - 6 && got <= expected + 6; };
        const bool aOk = close(a.getRProperty(), 145) && close(a.getGProperty(), 80) &&
                        close(a.getBProperty(), 48) && close(a.getAProperty(), 140);
        const bool bOk = close(b.getRProperty(), 113) && close(b.getGProperty(), 59) &&
                        close(b.getBProperty(), 32) && close(b.getAProperty(), 108);

        std::printf("[%s] Check A (NormalMap=(0,0,255)): rgba=(%d,%d,%d,%d) expected~=(145,80,48,140)\n",
                    aOk ? "PASS" : "FAIL", a.getRProperty(), a.getGProperty(), a.getBProperty(), a.getAProperty());
        std::printf("[%s] Check B (NormalMap=(128,128,255)): rgba=(%d,%d,%d,%d) expected~=(113,59,32,108)\n",
                    bOk ? "PASS" : "FAIL", b.getRProperty(), b.getGProperty(), b.getBProperty(), b.getAProperty());

        result_ = (aOk && bOk) ? 0 : 1;
        Exit();
    }

public:
    EasyGLNormalMappingTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    EasyGLNormalMappingTest game;
    game.Run();
    return game.getResult();
}

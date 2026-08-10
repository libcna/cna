// SPDX-License-Identifier: MS-PL
// Task 947 (Phase 78 rollout): HLSL->GLSL shader-conversion proof -- ShatterEffectSample's
// `ShatterEffect.fx` (per-triangle rotation-and-fall shatter animation + Phong lighting). Its
// own vertex format (Position+Normal+TexCoords+TriangleCenter+RotationalVelocity, stride 56)
// matches none of CNA's 5 built-in strides -- the 3rd shader ported using Task 1080's
// custom-vertex-layout capability.
//
// FNA reference (`ShatterEffectSample_4_0/ShatterEffect/Content/ShatterEffect.fx`):
//   float4x4 CreateYawPitchRollMatrix(float x, float y, float z) { ...explicit element assignment... }
//   VertexShaderOutput ShatterVS(InputVS input)
//   {
//       input.RotationalVelocity *= RotationAmount;
//       float4x4 rotMatrix = CreateYawPitchRollMatrix(input.RotationalVelocity.x,
//                                                     input.RotationalVelocity.y,
//                                                     input.RotationalVelocity.z);
//       float3 position = input.TriangleCenter + mul(input.Position - input.TriangleCenter, rotMatrix);
//       position += input.Normal * TranslationAmount;
//       position.y -= time*time * 200;
//       output.Position = mul(float4(position,1.0), WorldViewProjection);
//       output.WorldNormal = mul(mul(input.Normal, rotMatrix), World);
//       float4 worldPosition = mul(float4(position,1.0), World);
//       output.WorldPosition = worldPosition / worldPosition.w;
//       output.texCoords = input.TexCoords;
//       float3 directionToLight = normalize(lightPosition - output.WorldPosition);
//       float diffuseIntensity = saturate(dot(directionToLight, output.WorldNormal));
//       output.Color = diffuseColor * diffuseIntensity + ambientColor;
//       return output;
//   }
//   float4 PhongPS(PixelShaderInput input) : COLOR
//   {
//       float3 directionToLight = normalize(lightPosition - input.WorldPosition);
//       float3 reflectionVector = normalize(reflect(-directionToLight, input.WorldNormal));
//       float3 directionToCamera = normalize(eyePosition - input.WorldPosition);
//       float4 specular = specularColor * pow(saturate(dot(reflectionVector, directionToCamera)), specularPower);
//       float4 color = tex2D(TextureSampler, input.TexCoords) * input.Color + specular;
//       color.a = 1.0;
//       return color;
//   }
//
// Ported 1:1 below, including 2 non-obvious behaviours preserved verbatim, not "fixed":
//
// (1) `WorldNormal` is used raw in both `dot()` calls (VS diffuse, PS specular reflect) -- never
// re-normalized after the rotation+`World` transform, unlike e.g. `NormalMapping.fx`'s own careful
// `normalize()` calls. Ported faithfully (this test's own chosen values happen to keep it unit
// length regardless, so the quirk isn't numerically exercised here, but no `normalize()` was added
// that the source doesn't have).
// (2) `CreateYawPitchRollMatrix` is a matrix built entirely *inside* the vertex shader from
// per-vertex attributes -- never an uploaded uniform (unlike World/View/Projection), so
// `mul(v, rotMatrix)` (row-vector) is ported as a small GLSL helper function computing the exact
// same 9 element formulas and the equivalent weighted-sum dot products directly (matching this
// session's `NormalMapping.fx`/`tangentToWorld` precedent for locally-constructed matrices),
// rather than risking GLSL/HLSL row-major-vs-column-major confusion by trying to build an actual
// GLSL `mat3` from the same element assignments.
//
// **Documented porting adaptation**: unlike every other shader ported this session, this one
// takes a single, already-combined `WorldViewProjection` uniform (not separate World/View/
// Projection multiplied in-shader) -- `ShaderEffect`'s automatic World/View/Projection binding
// (Task 1079) only knows those 3 matrices individually, so this test computes
// `World * View * Projection` host-side (ordinary matrix multiplication -- associative regardless
// of row/column-vector interpretation, so this reproduces the same combined transform the real
// sample's own C# code would build) and uploads it explicitly via `SetUniformMat4`, exactly
// mirroring what the real sample's own `Effect.Parameters["WorldViewProjection"]` update would do.
// `World` is *also* uploaded separately (this shader legitimately declares its own `World`
// uniform too, used only for the normal transform) via the normal automatic binding.
//
// **Documented test simplification**: `TriangleCenter` is set equal to each vertex's own
// `Position` (not a real triangle centroid) so the rotation term's *position* contribution is
// provably zero (`Position - TriangleCenter = 0`) regardless of the rotation angle, letting this
// test isolate the rotation's effect on `WorldNormal` (and hence lighting) without simultaneously
// having to hand-derive a rotated vertex position -- real per-sample content would set
// `TriangleCenter` to the mesh's actual triangle centroids.
//
// Test setup: quad centred at the origin (World=Identity, standard corners/UVs, matching this
// session's convention), local `Normal=(0,0,1)` for every vertex, `RotationalVelocity=(0,pi/2,0)`
// for every vertex (constant vertex data -- toggled entirely via the `RotationAmount` *uniform*
// between checks, since `input.RotationalVelocity *= RotationAmount` scales it to `(0,0,0)` when
// `RotationAmount=0`). Camera eye=(0,0,3), target=origin, up=(0,1,0); `eyePosition=(0,0,3)`
// (matching the camera, a natural/simple choice). `TranslationAmount=0`, `time=0` throughout
// (isolating the rotation term specifically).
//
// **A real finding, empirically diagnosed, not a porting bug**: this shader computes
// `diffuseIntensity`/`Color` in the *vertex* shader (Gouraud-style), not the pixel shader --
// unlike every diffuse term ported earlier this session. With `lightPosition` close to the quad
// (originally tried at `(0,0,5)`), each of the 4 corners has a *meaningfully different* own
// `directionToLight` (since each corner sits up to 0.5 units off-centre relative to a light only
// ~5 units away), and because `diffuseIntensity` is `saturate()`d *per vertex* before
// interpolation, the interpolated fragment value is the average of 4 already-clamped values, not
// a fresh evaluation at the interpolated position -- for Check B's `WorldNormal=(-1,0,0)`, this
// meant left-edge corners (small positive `dot`) and right-edge corners (small negative `dot`,
// clamped to 0) averaged to a small but genuinely nonzero residual (~0.05, confirmed by direct
// GLSL instrumentation), instead of the idealized exact 0 this test originally expected. Moving
// `lightPosition` to `(0,0,5000)` (effectively "at infinity" relative to the quad's own ~1-unit
// size) makes `directionToLight` uniform to ~5 significant figures across all 4 corners, making
// the Gouraud-vs-per-pixel distinction numerically negligible and letting the hand-derived values
// below hold to the same tight tolerance as every other shader ported this session. (The specular
// term, computed in the *pixel* shader from the smoothly interpolated `WorldPosition` directly,
// never had this issue.)
//
// **`CreateYawPitchRollMatrix(0, pi/2, 0)` hand-derivation** (a 90-degree rotation about Y only,
// `x=z=0` so every `sin(x)`/`cos(z)`-style cross term collapses to `0`/`1` exactly, keeping the
// whole matrix exact rather than an irrational approximation): `m00=0,m01=0,m02=1, m10=0,m11=1,
// m12=0, m20=-1,m21=0,m22=0`. Applying this session's row-vector weighted-sum port,
// `rotate((x,y,z)) = (-z, y, x)` exactly -- e.g. `rotate((0,0,1)) = (-1,0,0)`.
//
// Check A -- `RotationAmount=0` (rotation collapses to Identity, `WorldNormal=(0,0,1)` unchanged):
//   `directionToLight ~= normalize((0,0,5000)-(0,0,0)) ~= (0,0,1)`; `diffuseIntensity =
//   dot((0,0,1),(0,0,1)) = 1`. With `diffuseColor=(0.8,0.8,0.8,1)`, `ambientColor=(0.1,0.1,0.1,0)`:
//   `Color = (0.9,0.9,0.9,1.0)`. Specular: `reflect((0,0,-1),(0,0,1))=(0,0,1)`,
//   `directionToCamera=normalize((0,0,3))=(0,0,1)`, `dot=1`, `pow(1,specularPower)=1` (any
//   exponent -- chosen deliberately to sidestep `pow()` precision risk, same trick as this
//   session's `NormalMapping.fx` port), `specular=specularColor=(0.15,0.15,0.15,0.15)`. With solid
//   diffuse texture `(200,100,50,255)`: `color = texture*Color + specular
//   ~= (0.7843*0.9,0.3922*0.9,0.1961*0.9,-)+(0.15,0.15,0.15,-) ~= (0.8559,0.5029,0.3265,-)`,
//   `color.a` forced to `1.0` -> byte `(218,128,83,255)`.
// Check B -- `RotationAmount=1` (`RotationalVelocity*1=(0,pi/2,0)` reaches
//   `CreateYawPitchRollMatrix` unchanged): `WorldNormal = rotate((0,0,1)) = (-1,0,0)` (perpendicular
//   to the camera-facing light direction). `diffuseIntensity = dot((0,0,1),(-1,0,0)) = 0` ->
//   `Color = ambientColor = (0.1,0.1,0.1,0)`. Specular: `reflect((0,0,-1),(-1,0,0))`: `dot((0,0,-1),
//   (-1,0,0))=0`, so `reflect=(0,0,-1)` unchanged; `dot(reflectionVector,directionToCamera) =
//   dot((0,0,-1),(0,0,1)) = -1`, `saturate(-1)=0`, `pow(0,specularPower)=0` -> `specular=(0,0,0,0)`.
//   `color = texture*Color + 0 ~= (0.7843*0.1,0.3922*0.1,0.1961*0.1,-) ~= (0.0784,0.0392,0.0196,-)`,
//   `color.a` forced to `1.0` -> byte `(20,10,5,255)`. Dramatically different R/G/B from Check A --
//   proves `CreateYawPitchRollMatrix`/the rotation genuinely reaches and changes `WorldNormal`
//   (and therefore both the diffuse and specular terms), not a fluke value at the identity case.
//
// **Discriminating power verified by 2 mutations**: (1) corrupted `rotateByYawPitchRoll()`'s
// Z-output-component to read `m20` instead of `m22` -- two earlier mutation attempts (swapping
// `m01`/`m02`, then swapping `m20`/`m21`) were tried first and both turned out to be *invisible*
// to this test's own checks, a genuine blind spot worth recording: with this test's light and
// camera both aligned along world Z, any `WorldNormal` lying entirely in the XY-plane (zero
// Z-component) gives identical (zero) diffuse *and* specular contributions regardless of its
// exact X/Y direction, so mutations confined to the X/Y output components alone go undetected.
// The `m20`-for-`m22` swap instead corrupts the Z-component directly, and (since `m22=1` at
// `RotationAmount=0` but `m20=0` there) this actually shows up via **Check A**, not Check B --
// Check A dropped from `(218,128,83,255)` to `(20,10,5,255)`, a dramatic, unambiguous failure,
// while Check B (whose own `RotationAmount=1` case happens to route through `m20` in the
// *unmutated* code already) was unaffected by this particular corruption. (2) Dropping
// `+ ambientColor` from `vColor` correctly failed *both* checks -- Check A's R channel dropped by
// the predicted `~20/255`, Check B collapsed to pure black since ambient was its only surviving
// contribution. Both mutations reverted, reconfirmed 2/2 PASS.
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

    // Matches InputVS's own field order: Position, Normal, TexCoords, TriangleCenter,
    // RotationalVelocity.
#pragma pack(push, 1)
    struct ShatterVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
        float tcx, tcy, tcz;
        float rvx, rvy, rvz;
    };
#pragma pack(pop)
    static_assert(sizeof(ShatterVertex) == 56);

    const char* kVertSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoords;
layout(location = 3) in vec3 aTriangleCenter;
layout(location = 4) in vec3 aRotationalVelocity;
out vec3 vWorldNormal;
out vec3 vWorldPosition;
out vec2 vTexCoords;
out vec4 vColor;
uniform mat4 WorldViewProjection;
uniform mat4 World;
uniform float RotationAmount;
uniform float TranslationAmount;
uniform float uTime;
uniform vec3 lightPosition;
uniform vec4 ambientColor;
uniform vec4 diffuseColor;

vec3 rotateByYawPitchRoll(vec3 v, float x, float y, float z) {
    float m00 = cos(z)*cos(y) + sin(z)*sin(x)*sin(y);
    float m01 = -sin(z)*cos(y) + cos(z)*sin(x)*sin(y);
    float m02 = cos(x)*sin(y);
    float m10 = sin(z)*cos(x);
    float m11 = cos(z)*cos(x);
    float m12 = -sin(x);
    float m20 = cos(z)*-sin(y) + sin(z)*sin(x)*cos(y);
    float m21 = sin(z)*sin(y) + cos(z)*sin(x)*cos(y);
    float m22 = cos(x)*cos(y);
    return vec3(
        v.x*m00 + v.y*m10 + v.z*m20,
        v.x*m01 + v.y*m11 + v.z*m21,
        v.x*m02 + v.y*m12 + v.z*m22
    );
}

void main() {
    vec3 rotVel = aRotationalVelocity * RotationAmount;

    vec3 position = aTriangleCenter
        + rotateByYawPitchRoll(aPosition - aTriangleCenter, rotVel.x, rotVel.y, rotVel.z);
    position += aNormal * TranslationAmount;
    position.y -= uTime * uTime * 200.0;

    gl_Position = WorldViewProjection * vec4(position, 1.0);

    vWorldNormal = mat3(World) * rotateByYawPitchRoll(aNormal, rotVel.x, rotVel.y, rotVel.z);
    vec4 worldPos4 = World * vec4(position, 1.0);
    vWorldPosition = worldPos4.xyz / worldPos4.w;
    vTexCoords = aTexCoords;

    vec3 directionToLight = normalize(lightPosition - vWorldPosition);
    float diffuseIntensity = clamp(dot(directionToLight, vWorldNormal), 0.0, 1.0);
    vec4 diffuse = diffuseColor * diffuseIntensity;

    vColor = diffuse + ambientColor;
}
)";

    const char* kFragSrc = R"(#version 300 es
precision highp float;
in vec3 vWorldNormal;
in vec3 vWorldPosition;
in vec2 vTexCoords;
in vec4 vColor;
out vec4 FragColor;
uniform sampler2D TextureSampler;
uniform vec3 lightPosition;
uniform vec3 eyePosition;
uniform vec4 specularColor;
uniform float specularPower;
void main() {
    vec3 directionToLight = normalize(lightPosition - vWorldPosition);
    vec3 reflectionVector = normalize(reflect(-directionToLight, vWorldNormal));
    vec3 directionToCamera = normalize(eyePosition - vWorldPosition);

    vec4 specular = specularColor
        * pow(clamp(dot(reflectionVector, directionToCamera), 0.0, 1.0), specularPower);

    vec4 textureColor = texture(TextureSampler, vTexCoords);

    vec4 color = textureColor * vColor + specular;
    color.a = 1.0;

    FragColor = color;
}
)";
}

class EasyGLShatterEffectTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::shared_ptr<Effect> fxBase_;
    std::unique_ptr<VertexBuffer> vb_;
    std::unique_ptr<IndexBuffer> ib_;
    Texture2D diffuseTex_;
    bool done_   = false;
    int result_  = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        const auto root = std::filesystem::temp_directory_path()
            / ("cna_shattereffect_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        WriteFile(root / "se.vert.glsl", kVertSrc);
        WriteFile(root / "se.frag.glsl", kFragSrc);
        WriteFile(root / "se.cnj", R"({
  "cnjVersion": 1,
  "type": "Effect",
  "vertex": "se.vert.glsl",
  "fragment": "se.frag.glsl"
})");

        getContentProperty().setRootDirectoryProperty(root.string());
        fxBase_ = getContentProperty().Load<std::shared_ptr<Effect>>("se");

        const VertexDeclaration decl(56, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            VertexElement(32, VertexElementFormat::Vector3, VertexElementUsage::TextureCoordinate, 1),
            VertexElement(44, VertexElementFormat::Vector3, VertexElementUsage::TextureCoordinate, 2),
        });

        // TriangleCenter == Position for every corner (documented test simplification -- see
        // this file's own header comment).
        const ShatterVertex verts[4] = {
            { -0.5f,  0.5f, 0.0f,  0,0,1,  0.0f,1.0f,  -0.5f, 0.5f,0.0f,  0.0f, MathHelper::PiOver2, 0.0f },
            { -0.5f, -0.5f, 0.0f,  0,0,1,  0.0f,0.0f,  -0.5f,-0.5f,0.0f,  0.0f, MathHelper::PiOver2, 0.0f },
            {  0.5f, -0.5f, 0.0f,  0,0,1,  1.0f,0.0f,   0.5f,-0.5f,0.0f,  0.0f, MathHelper::PiOver2, 0.0f },
            {  0.5f,  0.5f, 0.0f,  0,0,1,  1.0f,1.0f,   0.5f, 0.5f,0.0f,  0.0f, MathHelper::PiOver2, 0.0f },
        };

        vb_ = std::make_unique<VertexBuffer>(device, decl, 4, BufferUsage::None);
        vb_->SetDataRaw(verts, 4, sizeof(ShatterVertex));

        const std::uint16_t indices[6] = { 0, 1, 2, 0, 2, 3 };
        ib_ = std::make_unique<IndexBuffer>(device, 6);
        ib_->SetData(indices, 6);

        diffuseTex_ = Texture2D::CreateFromPixels(device, 1, 1,
            std::vector<std::uint8_t>{ 200, 100, 50, 255 });
    }

    Color DrawOnce(float rotationAmount)
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(Color(10, 10, 10, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        const Matrix world = Matrix::getIdentityProperty();
        const Matrix view = Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero,
                                                  Vector3(0.0f, 1.0f, 0.0f));
        const Matrix projection = Matrix::CreatePerspectiveFieldOfView(
            MathHelper::PiOver4, vp.getAspectRatioProperty(), 0.1f, 100.0f);

        auto* fx = dynamic_cast<ShaderEffect*>(fxBase_.get());
        fx->setWorldProperty(world);
        fx->setViewProperty(view);
        fx->setProjectionProperty(projection);
        fx->Apply();

        const Matrix wvp = world * view * projection;
        float wvpColMajor[16];
        wvp.ToColumnMajor(wvpColMajor);
        fx->SetUniformMat4("WorldViewProjection", wvpColMajor);

        fx->SetTexture(0, diffuseTex_);
        fx->SetUniformInt("TextureSampler", 0);
        fx->SetUniformVec3("lightPosition", 0.0f, 0.0f, 5000.0f);
        fx->SetUniformVec3("eyePosition", 0.0f, 0.0f, 3.0f);
        fx->SetUniformVec4("diffuseColor", 0.8f, 0.8f, 0.8f, 1.0f);
        fx->SetUniformVec4("ambientColor", 0.1f, 0.1f, 0.1f, 0.0f);
        fx->SetUniformVec4("specularColor", 0.15f, 0.15f, 0.15f, 0.15f);
        fx->SetUniformFloat("specularPower", 8.0f);
        fx->SetUniformFloat("RotationAmount", rotationAmount);
        fx->SetUniformFloat("TranslationAmount", 0.0f);
        fx->SetUniformFloat("uTime", 0.0f);

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
            std::printf("[FAIL] EasyGLShatterEffect: .cnj load or GLSL compile failed\n");
            Exit();
            return;
        }

        const Color a = DrawOnce(0.0f);
        const Color b = DrawOnce(1.0f);

        const auto close = [](int got, int expected) { return got >= expected - 6 && got <= expected + 6; };
        const bool aOk = close(a.getRProperty(), 218) && close(a.getGProperty(), 128) &&
                        close(a.getBProperty(), 83) && close(a.getAProperty(), 255);
        const bool bOk = close(b.getRProperty(), 20) && close(b.getGProperty(), 10) &&
                        close(b.getBProperty(), 5) && close(b.getAProperty(), 255);

        std::printf("[%s] Check A (RotationAmount=0): rgba=(%d,%d,%d,%d) expected~=(218,128,83,255)\n",
                    aOk ? "PASS" : "FAIL", a.getRProperty(), a.getGProperty(), a.getBProperty(), a.getAProperty());
        std::printf("[%s] Check B (RotationAmount=1, 90deg): rgba=(%d,%d,%d,%d) expected~=(20,10,5,255)\n",
                    bOk ? "PASS" : "FAIL", b.getRProperty(), b.getGProperty(), b.getBProperty(), b.getAProperty());

        result_ = (aOk && bOk) ? 0 : 1;
        Exit();
    }

public:
    EasyGLShatterEffectTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    EasyGLShatterEffectTest game;
    game.Run();
    return game.getResult();
}

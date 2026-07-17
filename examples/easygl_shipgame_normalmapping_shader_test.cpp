// SPDX-License-Identifier: MS-PL
// Task 947 (Phase 78 rollout): HLSL->GLSL shader-conversion proof -- ShipGame's own
// `NormalMapping.fx` (distinct from `NormalMappingSample`'s same-named file -- see
// `easygl_animsprite_shader_test.cpp`'s header for the full background). With this, all 4 of
// ShipGame's distinct custom shaders (`AnimSprite.fx`, `Blur.fx`, `NormalMapping.fx`,
// `Particle.fx`) are accounted for -- 3 ported, `Particle.fx` still pending real GPU point-sprite
// support. Uses Task 1081's new `TextureCube` sampling capability for the reflection map.
//
// FNA reference (`ShipGame_4_0/ShipGame/Content/shaders/NormalMapping.fx`), 3 techniques:
//   void PlainMappingVS(in float4 InPosition:POSITION0, in float2 InTexCoord:TEXCOORD0,
//                      out float4 OutPosition:POSITION0, out float2 OutTexCoord:TEXCOORD0)
//   { OutPosition = mul(InPosition, WorldViewProj); OutTexCoord = InTexCoord; }
//   float4 PlainMappingPS(in float2 TexCoord:TEXCOORD0):COLOR0
//   { return tex2D(TextureSampler, TexCoord); }
//
//   void NormalMappingVS(in float4 InPosition:POSITION0, in float2 InTexCoord:TEXCOORD0,
//                       in float3 InNormal:NORMAL0, in float3 InBinormal:BINORMAL0,
//                       in float3 InTangent:TANGENT0, out float4 OutPosition:POSITION0,
//                       out float2 OutTexCoord:TEXCOORD0, out float3 OutLightDir:TEXCOORD1,
//                       out float3 OutViewDir:TEXCOORD2, out float3 OutReflectDir:TEXCOORD3)
//   {
//       OutPosition = mul(InPosition, WorldViewProj);
//       OutTexCoord = InTexCoord;
//       float3x3 tangent_space = float3x3(InTangent, InBinormal, InNormal);
//       OutLightDir = mul(tangent_space, LightPosition.xyz - InPosition.xyz);
//       OutViewDir = mul(tangent_space, CameraPosition - InPosition.xyz);
//       OutReflectDir = reflect(InPosition.xyz - CameraPosition, InNormal);
//   }
//   float4 NormalMappingPS(...) : COLOR0
//   {
//       float4 diffuse = tex2D(TextureSampler, TexCoord);
//       float4 specular = tex2D(SpecularSampler, TexCoord);
//       float4 normal = tex2D(NormalSampler, TexCoord);
//       float3 reflect = texCUBE(ReflectSampler, ReflectDir);
//       float4 glow = tex2D(GlowMapSampler, TexCoord);
//       float3 n = normalize(normal.xyz - 0.5);
//       float3 l = normalize(LightDir); float3 v = normalize(ViewDir); float3 h = normalize(l+v);
//       float ndotl = saturate(dot(n,l)); float ndoth = saturate(dot(n,h));
//       if (ndotl == 0) ndoth = 0;
//       float3 ambient = LightAmbient * diffuse.xyz;
//       specular.xyz *= LightColor * pow(ndoth, specular.w * 255);
//       diffuse.xyz *= LightColor * ndotl;
//       reflect *= 1 - normal.w;
//       float glow_intensity = saturate(dot(glow.xyz, 1.0) + dot(specular.xyz, 1.0));
//       float4 color;
//       color.xyz = ambient + glow.xyz + diffuse.xyz + specular.xyz + reflect.xyz;
//       color.w = glow_intensity;
//       return color;
//   }
//
//   void ViewMappingVS(in float4 InPosition:POSITION0, in float3 InNormal:NORMAL0,
//                     out float4 OutPosition:POSITION0, out float OutFacing:TEXCOORD0)
//   {
//       OutPosition = mul(InPosition, WorldViewProj);
//       float3 view = normalize(CameraPosition - InPosition.xyz);
//       OutFacing = saturate(dot(view, InNormal));
//   }
//   float4 ViewMappingPS(in float Facing:TEXCOORD0):COLOR0
//   {
//       Facing *= Facing; Facing *= Facing;
//       float4 tex = tex2D(TextureSamplerClamp, float2(Facing, 0));
//       tex.w = 1.0f;
//       return tex;
//   }
//
// Ported 1:1 below, as one GLSL program parameterized by a runtime `mode` uniform (0=PlainMapping,
// 1=NormalMapping, 2=ViewMapping -- same documented adaptation as `PostprocessEffect.Fx`/
// `Blur.fx`), sharing one vertex shader declaring the union of every technique's own attributes
// (Position/TexCoord/Normal/Binormal/Tangent, stride 56, Task 1080's custom-vertex-layout
// capability -- matching `NormalMappingSample`'s own `NormalMapping.fx` field order exactly,
// though this is a genuinely different shader). Like `ShatterEffect.fx`/`AnimSprite.fx`/`Blur.fx`,
// this shader takes a single pre-combined `WorldViewProj` (no separate `World` at all) -- unlike
// every other "no World" shader ported this session, `InPosition` here is *also* used directly as
// world-space for the lighting math (`LightPosition.xyz - InPosition.xyz`, etc.) with no separate
// World transform applied to it at all -- ported faithfully, not "fixed" to add one.
//
// 2 non-obvious HLSL constructs, ported precisely, not "fixed":
//
// (1) `mul(tangent_space, v)` -- **matrix-times-vector**, the *opposite* operand order from every
// other shader ported this session (`mul(v, M)`, vector-times-matrix). HLSL's `mul` is genuinely
// overloaded: `mul(vector, matrix)` = row-vector * matrix; `mul(matrix, vector)` = matrix *
// column-vector -- distinct operations. With `tangent_space`'s own rows = Tangent/Binormal/Normal
// (from the `float3x3(InTangent, InBinormal, InNormal)` constructor, HLSL-row-major so each ctor
// argument becomes one row), `tangent_space * v` (column-vector) = `(dot(T,v), dot(B,v), dot(N,v))`
// -- the standard *world-to-tangent-space* projection (transforming into the TBN basis by dotting
// against each axis), the natural inverse of every earlier shader's *tangent-to-world* `v.x*T+
// v.y*B+v.z*N` weighted-sum pattern. Ported directly as those 3 dot products, avoiding any
// GLSL/HLSL matrix row/column ambiguity for a matrix that (like `NormalMapping.fx`'s
// `tangentToWorld` earlier this session) is never itself an uploaded uniform.
//
// (2) The sampled normal map is unpacked via `normal.xyz - 0.5` -- **not** the far more common
// `*2-1` remap (`NormalMappingSample`'s own `NormalMapping.fx`, ported earlier this session, used
// *no* remap at all; this shader uses a *third*, distinct convention). Ported exactly as written --
// a genuinely different, deliberately-not-"corrected" unpacking formula from either precedent.
//
// Test setup: standard quad centred at the origin (corners +-0.5, standard UVs), local
// `Normal=(0,0,1)`/`Tangent=(1,0,0)`/`Binormal=(0,1,0)` (a +Z-facing quad's natural orthonormal
// basis, making `tangent_space` the identity mapping and keeping the world-to-tangent-space
// projection numerically simple). Camera eye=(0,0,3)/target=origin/up=(0,1,0); `WorldViewProj =
// View * Projection` (no `World` factor at all, computed host-side, same pattern as
// `ShatterEffect.fx`). `LightPosition=(0,0,5,1)`, `CameraPosition=(0,0,3)` (matching the camera),
// `LightColor=(0.5,0.5,0.5)`, `LightAmbient=(0.1,0.1,0.1)`.
//
// **A real finding, different from every earlier "no World" shader ported this session**: unlike
// `Billboard.fx`/`ParticleEffect.fx`/`ShatterEffect.fx` (all of which expand a single shared
// per-instance *anchor* position into a quad, keeping every corner's own lighting-relevant vertex
// data identical), this shader's vertex data is a genuine per-vertex mesh position -- each of the
// quad's 4 corners legitimately has its *own*, different `(x,y,0)` coordinate, and
// `LightDir`/`ViewDir`/`ReflectDir`/`Facing` are all computed from that per-corner position
// directly, not from a shared constant. The screen-centre pixel's interpolated value is therefore
// a genuine weighted *average* of 4 distinct per-corner values, not an exact single value -- close
// to (but not identically) what a hand-derivation using the quad's local origin `(0,0,0)` would
// predict, since the light/camera sit much farther away (`Z=5`/`Z=3`) than the quad's own `+-0.5`
// extent. Empirically measured: `vFacing ~= 0.973` at the sample pixel (not exactly `1.0`).
// `Check A`/`B`'s formulas are forgiving enough (linear-ish, moderate tolerance) that this ~3%
// deviation doesn't matter, but `Check C`'s `Facing^4` (an aggressive fourth power) and its
// original 2-texel gradient fixture were *not* forgiving of it -- fixed the same way as this
// session's own `Blur.fx` precedent: a wide, flat 10-texel gradient region (not a single texel)
// plus `SamplerState::PointClamp` for that draw, so landing anywhere solidly past the halfway
// point is sufficient, without needing `Facing` to be bit-exact.
//
// Using the quad's own local origin `(0,0,0)` as the representative (approximately-correct) point
// for hand-derivation: `LightDir = LightPosition.xyz-(0,0,0) = (0,0,5)`, normalized `(0,0,1)`.
// `ViewDir = CameraPosition-(0,0,0) = (0,0,3)`, normalized `(0,0,1)`. `ReflectDir =
// reflect((0,0,0)-(0,0,3),(0,0,1)) = reflect((0,0,-3),(0,0,1)) = (0,0,-3)-2*(-3)*(0,0,1) = (0,0,3)`
// -- samples the cube's own `PositiveZ` face (dominant +Z component).
//
// Check A (`mode=0`, `PlainMapping`) -- a solid diffuse texture `(200,100,50,255)`: direct
//   pass-through, unaffected by the per-corner-position subtlety above (no lighting math at all)
//   -> byte `(200,100,50,255)` exactly.
// Check B (`mode=1`, `NormalMapping`) -- diffuse `(200,100,50,255)`, specular `(100,100,100,128)`
//   (alpha drives a specular exponent of `128`), normal-map texel `(128,128,255,128)` (unpacks to
//   `n~=(0,0,1)`, the geometric normal, and its own alpha half-scales the reflection term), the
//   cube's `PositiveZ` face `(10,20,30,255)`, glow `(30,20,10,255)`. With `n=l=v=h~=(0,0,1)`:
//   `ndotl=ndoth~=1` (`pow(~1,128)~=1`, sidestepping `pow()` precision risk, same trick used
//   throughout this session). `ambient=(0.1,0.1,0.1)*diffuse=(0.0784,0.0392,0.0196)`;
//   `specular*=LightColor*1=(0.1961,0.1961,0.1961)`; `diffuse*=LightColor*1=(0.3922,0.1961,
//   0.0980)`; `reflect*=(1-128/255)~=0.498=(0.0195,0.0390,0.0585)`; `glow_intensity=
//   saturate(dot(glow,1)+dot(specular,1))~=saturate(0.2353+0.5882)=0.8235`. Summed RGB
//   `~=(0.804,0.549,0.412)` -> byte `~=(205,140,105,210)`, matched within this test's usual +-6
//   tolerance (the ~3% `Facing`-style deviation described above applies equally to `LightDir`/
//   `ViewDir` here, but this formula's own sensitivity to it is mild).
// Check C (`mode=2`, `ViewMapping`) -- `Facing=saturate(dot(~(0,0,1),(0,0,1)))~=1`, `Facing^4~=1`
//   (via 2 successive squarings, exactly as written): samples `TextureSamplerClamp` deep in the
//   gradient fixture's own upper half, landing solidly on the right-hand colour `(50,60,70,-)`,
//   alpha forced to `1.0` by the shader itself -> byte `(50,60,70,255)`.
//
// **Discriminating power verified by mutation, with a genuinely undetectable one found and
// documented, not swept aside**: an initial attempt mutated the normal-map remap from `-0.5` to
// the more common `*2-1` -- this **cannot ever** be caught by any check downstream of `n`, because
// `x*2-1` is *identically* `2*(x-0.5)` for every `x`: the two remaps differ only by a positive
// scalar multiple, which `normalize()` always cancels out exactly, for *any* texel value. This
// shader's own choice between the two conventions is provably behaviourally inert (verified
// algebraically, not just empirically) -- an interesting, honestly-recorded fact about this
// specific shader, not a coverage gap to paper over. A genuinely meaningful mutation instead:
// swapped the `Tangent`/`Normal` roles in `vLightDir`'s own world-to-tangent-space projection
// (`dot(aNormal,...)` where `dot(aTangent,...)` belongs, and vice versa) -- Check B correctly
// collapsed (`(55,40,30,60)` instead of `(205,140,105,210)`, since the light direction effectively
// rotated 90 degrees) while Check A/C (neither of which touches `vLightDir`) were correctly
// unaffected. Reverted, reconfirmed 3/3 PASS.
//
// Exit code 0 = all 3 PASS, 1 = any FAIL.

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
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
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

    // Matches NormalMappingVS's own field order: Position, TexCoord, Normal, Binormal, Tangent.
#pragma pack(push, 1)
    struct ShipNormalMapVertex
    {
        float px, py, pz;
        float u, v;
        float nx, ny, nz;
        float bx, by, bz;
        float tx, ty, tz;
    };
#pragma pack(pop)
    static_assert(sizeof(ShipNormalMapVertex) == 56);

    const char* kVertSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in vec3 aBinormal;
layout(location = 4) in vec3 aTangent;
out vec2 vTexCoord;
out vec3 vLightDir;
out vec3 vViewDir;
out vec3 vReflectDir;
out float vFacing;
uniform mat4 WorldViewProj;
uniform vec4 LightPosition;
uniform vec3 CameraPosition;
void main() {
    gl_Position = WorldViewProj * vec4(aPosition, 1.0);
    vTexCoord = aTexCoord;

    vec3 lightVec = LightPosition.xyz - aPosition;
    vec3 viewVec = CameraPosition - aPosition;
    vLightDir = vec3(dot(aTangent, lightVec), dot(aBinormal, lightVec), dot(aNormal, lightVec));
    vViewDir  = vec3(dot(aTangent, viewVec),  dot(aBinormal, viewVec),  dot(aNormal, viewVec));

    vReflectDir = reflect(aPosition - CameraPosition, aNormal);

    vec3 viewN = normalize(CameraPosition - aPosition);
    vFacing = clamp(dot(viewN, aNormal), 0.0, 1.0);
}
)";

    const char* kFragSrc = R"(#version 300 es
precision highp float;
in vec2 vTexCoord;
in vec3 vLightDir;
in vec3 vViewDir;
in vec3 vReflectDir;
in float vFacing;
out vec4 FragColor;
uniform sampler2D TextureSampler;
uniform sampler2D TextureSamplerClamp;
uniform sampler2D NormalSampler;
uniform sampler2D SpecularSampler;
uniform sampler2D GlowMapSampler;
uniform samplerCube ReflectSampler;
uniform vec3 LightColor;
uniform vec3 LightAmbient;
uniform int mode;

void main() {
    if (mode == 0) {
        FragColor = texture(TextureSampler, vTexCoord);
        return;
    }
    if (mode == 2) {
        float facing = vFacing;
        facing *= facing;
        facing *= facing;
        vec4 tex = texture(TextureSamplerClamp, vec2(facing, 0.0));
        tex.w = 1.0;
        FragColor = tex;
        return;
    }
    vec4 diffuse = texture(TextureSampler, vTexCoord);
    vec4 specular = texture(SpecularSampler, vTexCoord);
    vec4 normalTex = texture(NormalSampler, vTexCoord);
    vec3 reflectColor = texture(ReflectSampler, vReflectDir).rgb;
    vec4 glow = texture(GlowMapSampler, vTexCoord);

    vec3 n = normalize(normalTex.xyz - 0.5);
    vec3 l = normalize(vLightDir);
    vec3 v = normalize(vViewDir);
    vec3 h = normalize(l + v);

    float ndotl = clamp(dot(n, l), 0.0, 1.0);
    float ndoth = clamp(dot(n, h), 0.0, 1.0);
    if (ndotl == 0.0) ndoth = 0.0;

    vec3 ambient = LightAmbient * diffuse.xyz;

    specular.xyz *= LightColor * pow(ndoth, specular.w * 255.0);
    diffuse.xyz *= LightColor * ndotl;
    reflectColor *= (1.0 - normalTex.w);

    float glowIntensity = clamp(dot(glow.xyz, vec3(1.0)) + dot(specular.xyz, vec3(1.0)), 0.0, 1.0);

    vec4 color;
    color.xyz = ambient + glow.xyz + diffuse.xyz + specular.xyz + reflectColor;
    color.w = glowIntensity;
    FragColor = color;
}
)";
}

class EasyGLShipGameNormalMappingTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::shared_ptr<Effect> fxBase_;
    std::unique_ptr<VertexBuffer> vb_;
    std::unique_ptr<IndexBuffer> ib_;
    Texture2D diffuseTex_;
    Texture2D specularTex_;
    Texture2D normalTex_;
    Texture2D glowTex_;
    Texture2D gradientTex_;
    std::unique_ptr<TextureCube> cubeTex_;
    bool done_   = false;
    int result_  = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        const auto root = std::filesystem::temp_directory_path()
            / ("cna_shipgame_normalmapping_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        WriteFile(root / "snm.vert.glsl", kVertSrc);
        WriteFile(root / "snm.frag.glsl", kFragSrc);
        WriteFile(root / "snm.cnj", R"({
  "cnjVersion": 1,
  "type": "Effect",
  "vertex": "snm.vert.glsl",
  "fragment": "snm.frag.glsl"
})");

        getContentProperty().setRootDirectoryProperty(root.string());
        fxBase_ = getContentProperty().Load<std::shared_ptr<Effect>>("snm");

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
        const ShipNormalMapVertex verts[4] = {
            { -0.5f,  0.5f, 0.0f,  0.0f, 1.0f,  n[0],n[1],n[2],  b[0],b[1],b[2],  t[0],t[1],t[2] },
            { -0.5f, -0.5f, 0.0f,  0.0f, 0.0f,  n[0],n[1],n[2],  b[0],b[1],b[2],  t[0],t[1],t[2] },
            {  0.5f, -0.5f, 0.0f,  1.0f, 0.0f,  n[0],n[1],n[2],  b[0],b[1],b[2],  t[0],t[1],t[2] },
            {  0.5f,  0.5f, 0.0f,  1.0f, 1.0f,  n[0],n[1],n[2],  b[0],b[1],b[2],  t[0],t[1],t[2] },
        };

        vb_ = std::make_unique<VertexBuffer>(device, decl, 4, BufferUsage::None);
        vb_->SetDataRaw(verts, 4, sizeof(ShipNormalMapVertex));

        const std::uint16_t indices[6] = { 0, 1, 2, 0, 2, 3 };
        ib_ = std::make_unique<IndexBuffer>(device, 6);
        ib_->SetData(indices, 6);

        diffuseTex_ = Texture2D::CreateFromPixels(device, 1, 1,
            std::vector<std::uint8_t>{ 200, 100, 50, 255 });
        specularTex_ = Texture2D::CreateFromPixels(device, 1, 1,
            std::vector<std::uint8_t>{ 100, 100, 100, 128 });
        normalTex_ = Texture2D::CreateFromPixels(device, 1, 1,
            std::vector<std::uint8_t>{ 128, 128, 255, 128 });
        glowTex_ = Texture2D::CreateFromPixels(device, 1, 1,
            std::vector<std::uint8_t>{ 30, 20, 10, 255 });
        // 20-texel-wide, 2 flat halves -- wide margins make this robust to Facing not landing
        // at exactly 1.0 (see this file's own header comment for why).
        {
            std::vector<std::uint8_t> px;
            px.reserve(20 * 4);
            for (int i = 0; i < 10; ++i) { px.insert(px.end(), { 10, 10, 10, 255 }); }
            for (int i = 0; i < 10; ++i) { px.insert(px.end(), { 50, 60, 70, 255 }); }
            gradientTex_ = Texture2D::CreateFromPixels(device, 20, 1, px);
        }

        cubeTex_ = std::make_unique<TextureCube>(device, 1, false, SurfaceFormat::Color);
        const Color black(0, 0, 0, 255);
        const Color reflectFace(10, 20, 30, 255);
        cubeTex_->SetData(CubeMapFace::PositiveX, &black, 1);
        cubeTex_->SetData(CubeMapFace::NegativeX, &black, 1);
        cubeTex_->SetData(CubeMapFace::PositiveY, &black, 1);
        cubeTex_->SetData(CubeMapFace::NegativeY, &black, 1);
        cubeTex_->SetData(CubeMapFace::PositiveZ, &reflectFace, 1);
        cubeTex_->SetData(CubeMapFace::NegativeZ, &black, 1);
    }

    Color DrawOnce(int mode, Texture2D& diffuseOrGradient)
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

        const Matrix worldViewProj = view * projection;
        float wvpColMajor[16];
        worldViewProj.ToColumnMajor(wvpColMajor);
        fx->SetUniformMat4("WorldViewProj", wvpColMajor);

        fx->SetTexture(0, diffuseOrGradient);
        fx->SetUniformInt("TextureSampler", 0);
        fx->SetUniformInt("TextureSamplerClamp", 0);
        if (mode == 2) device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        fx->SetTexture(1, specularTex_);
        fx->SetUniformInt("SpecularSampler", 1);
        fx->SetTexture(2, normalTex_);
        fx->SetUniformInt("NormalSampler", 2);
        fx->SetTexture(3, glowTex_);
        fx->SetUniformInt("GlowMapSampler", 3);
        fx->SetTexture(4, *cubeTex_);
        fx->SetUniformInt("ReflectSampler", 4);

        fx->SetUniformVec4("LightPosition", 0.0f, 0.0f, 5.0f, 1.0f);
        fx->SetUniformVec3("CameraPosition", 0.0f, 0.0f, 3.0f);
        fx->SetUniformVec3("LightColor", 0.5f, 0.5f, 0.5f);
        fx->SetUniformVec3("LightAmbient", 0.1f, 0.1f, 0.1f);
        fx->SetUniformInt("mode", mode);

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
            std::printf("[FAIL] EasyGLShipGameNormalMapping: .cnj load or GLSL compile failed\n");
            Exit();
            return;
        }

        const Color a = DrawOnce(0, diffuseTex_);
        const Color b = DrawOnce(1, diffuseTex_);
        const Color c = DrawOnce(2, gradientTex_);

        const auto close = [](int got, int expected) { return got >= expected - 6 && got <= expected + 6; };
        const bool aOk = close(a.getRProperty(), 200) && close(a.getGProperty(), 100) &&
                        close(a.getBProperty(), 50) && close(a.getAProperty(), 255);
        const bool bOk = close(b.getRProperty(), 205) && close(b.getGProperty(), 140) &&
                        close(b.getBProperty(), 105) && close(b.getAProperty(), 210);
        const bool cOk = close(c.getRProperty(), 50) && close(c.getGProperty(), 60) &&
                        close(c.getBProperty(), 70) && close(c.getAProperty(), 255);

        std::printf("[%s] Check A (PlainMapping): rgba=(%d,%d,%d,%d) expected~=(200,100,50,255)\n",
                    aOk ? "PASS" : "FAIL", a.getRProperty(), a.getGProperty(), a.getBProperty(), a.getAProperty());
        std::printf("[%s] Check B (NormalMapping): rgba=(%d,%d,%d,%d) expected~=(205,140,105,210)\n",
                    bOk ? "PASS" : "FAIL", b.getRProperty(), b.getGProperty(), b.getBProperty(), b.getAProperty());
        std::printf("[%s] Check C (ViewMapping): rgba=(%d,%d,%d,%d) expected~=(50,60,70,255)\n",
                    cOk ? "PASS" : "FAIL", c.getRProperty(), c.getGProperty(), c.getBProperty(), c.getAProperty());

        result_ = (aOk && bOk && cOk) ? 0 : 1;
        Exit();
    }

public:
    EasyGLShipGameNormalMappingTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    EasyGLShipGameNormalMappingTest game;
    game.Run();
    return game.getResult();
}

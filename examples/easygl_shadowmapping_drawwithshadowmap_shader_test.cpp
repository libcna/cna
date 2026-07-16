// SPDX-License-Identifier: MS-PL
// Task 947 (Phase 78 rollout): HLSL->GLSL shader-conversion proof -- DrawModel.fx's
// `DrawWithShadowMap` technique (ShadowMappingSample_4_0). Draws the scene with per-pixel diffuse
// lighting, darkened wherever a shadow-map lookup shows a closer occluder recorded from the
// light's point of view. Sibling of `CreateShadowMap`
// (easygl_shadowmapping_createshadowmap_shader_test.cpp -- see that file's header for why this
// sample needs 2 separate `ShaderEffect`s instead of one parameterized program like
// PostprocessEffect.Fx). With both ported, ShadowMapping sample's shader gap (DEFERRED.md #11)
// is fully cleared.
//
// FNA reference (`ShadowMappingSample_4_0/ShadowMapping/Content/DrawModel.fx`):
//   DrawWithShadowMap_VSOut DrawWithShadowMap_VertexShader(DrawWithShadowMap_VSIn input)
//   {
//       float4x4 WorldViewProj = mul(mul(World, View), Projection);
//       Output.Position = mul(input.Position, WorldViewProj);
//       Output.Normal =  normalize(mul(input.Normal, World));
//       Output.TexCoord = input.TexCoord;
//       Output.WorldPos = mul(input.Position, World);
//       return Output;
//   }
//   float4 DrawWithShadowMap_PixelShader(DrawWithShadowMap_VSOut input) : COLOR
//   {
//       float4 diffuseColor = tex2D(TextureSampler, input.TexCoord);
//       float diffuseIntensity = saturate(dot(LightDirection, input.Normal));
//       float4 diffuse = diffuseIntensity * diffuseColor + AmbientColor;
//       float4 lightingPosition = mul(input.WorldPos, LightViewProj);
//       float2 ShadowTexCoord = 0.5 * lightingPosition.xy / lightingPosition.w + float2(0.5, 0.5);
//       ShadowTexCoord.y = 1.0f - ShadowTexCoord.y;
//       float shadowdepth = tex2D(ShadowMapSampler, ShadowTexCoord).r;
//       float ourdepth = (lightingPosition.z / lightingPosition.w) - DepthBias;
//       if (shadowdepth < ourdepth) { diffuse *= float4(0.5,0.5,0.5,0); };
//       return diffuse;
//   }
//
// Ported 1:1 below, including `mul(input.Normal, World)`'s implicit 4x4->3x3 matrix truncation
// (`mat3(World) * aNormal` in GLSL, same pattern as this session's other lit shaders).
//
// **Documented porting adaptation**: `AmbientColor` (`float4(0.15,0.15,0.15,0)`) and `DepthBias`
// (`0.001f`) are declared in the HLSL source with default initial values, and the real sample's own
// `DrawModel()` helper (`ShadowMapping.cs`) never overrides them via `effect.Parameters[...]`,
// relying on those XNA-effect-compiled defaults. `ShaderEffect` does not parse HLSL default-value
// initializers (a general, already-known limitation, not new to this shader) -- this test instead
// sets both uniforms explicitly to the HLSL source's own documented default values.
//
// **A real, faithfully-preserved HLSL quirk found and documented, not fixed**: the shadow branch
// (`diffuse *= float4(0.5,0.5,0.5,0)`) also zeroes the output alpha -- but since the real sample
// (and this test) always draws with `BlendState.Opaque` (src=One, dst=Zero, alpha ignored), this is
// silently benign in practice, never actually making a shadowed pixel "transparent". Preserved
// verbatim rather than "corrected" to `float4(0.5,0.5,0.5,1)`.
//
// Camera (View/Projection): camera at (0,0,3) looking at the origin, matching this session's other
// 3D shader tests. `World=Translate(0,0,-5)` (pure translation, no rotation) is used for every
// check below -- it moves the quad further from the camera along the view axis (still X=Y=0, so
// its on-screen footprint stays centred) while giving `LightViewProj` (see below) a non-trivial,
// exact-fraction world-space Z to project, and lets `mat3(World)=Identity` so the diffuse-lighting
// derivation stays simple (Normal unaffected by a pure translation).
//
// `LightViewProj` (a synthetic test matrix, `Matrix::CreateOrthographic(10,10,-10,10)`, optionally
// pre-multiplied by an X-translation -- see below) is NOT the real sample's own
// `CreateLightViewProjectionMatrix()`; see the sibling CreateShadowMap test's own header for why.
// Reusing that same base matrix and the same `World=Translate(0,0,-5)`, this test's own hand
// derivation gives `lightingPosition.z/w = 0.75` (identical to the CreateShadowMap test's own
// Check B -- documenting, not literally chaining, the two techniques' real relationship), so
// `ourdepth = 0.75 - 0.001 = 0.749`.
//
// LightDirection=(0,0,1), and (World being a pure translation) the quad's world-space normal stays
// (0,0,1), so `dot(LightDirection,Normal)=1`, `diffuseIntensity=saturate(1)=1`. With a solid
// diffuse Texture texel (200,100,50,255) ~= (0.7843,0.3922,0.1961,1.0) and
// AmbientColor=(0.15,0.15,0.15,0):
//   unshadowed diffuse = 1*(0.7843,0.3922,0.1961,1.0) + (0.15,0.15,0.15,0)
//                       = (0.9343,0.5422,0.3461,1.0) ~= byte (238,138,88,255)
//   shadowed   diffuse = unshadowed * (0.5,0.5,0.5,0) = (0.4672,0.2711,0.1731,0.0)
//                       ~= byte (119,69,44,0)
//
// Check A -- ShadowMap is a solid white (255,255,255,255) 1x1 texture (matching the real sample's
//   own `Clear(Color.White)` "furthest possible" convention): shadowdepth=1.0 >= ourdepth=0.749,
//   so NOT shadowed. Expect the unshadowed colour above.
// Check B -- ShadowMap is a solid black (0,0,0,0) 1x1 texture (a near occluder): shadowdepth=0.0
//   < ourdepth=0.749, so shadowed. Expect the shadowed colour above -- proves the branch itself,
//   distinct RGB *and* alpha from Check A.
// Check C/D -- prove `ShadowTexCoord` genuinely samples a *different* location depending on
//   `lightingPosition.xy`, not a hardcoded corner: a 2x1 ShadowMap texture, left texel = white
//   (unshadowed), right texel = black (shadowed). `LightViewProj` is pre-multiplied by
//   `Translate(+-2.5,0,0)` (baked into the light matrix itself, not the quad's own World, so the
//   quad's on-screen footprint and `ourdepth` both stay exactly as derived above) --
//   `Matrix::CreateOrthographic(10,...)` gives `M11=0.2`, so `clip.x = +-2.5*0.2 = +-0.5`, giving
//   `ShadowTexCoord.x = 0.5*(+-0.5)+0.5` = 0.25 (Check C, deep in the left texel's own centre) or
//   0.75 (Check D, deep in the right texel's own centre) -- sampled exactly at each texel's centre,
//   so default XNA/GL *linear* filtering resolves to that texel's own colour with zero blend
//   weight from its neighbour, without needing an explicit `SamplerState::PointClamp` override
//   (unlike the real sample's own `GraphicsDevice.SamplerStates[1] = SamplerState.PointClamp;`,
//   which exists for the same "avoid a blended read" reason but isn't needed here given this exact
//   choice of sample point -- a documented, deliberate test simplification, not an engine gap).
//   `ShadowTexCoord.y`'s `1.0-y` flip is not independently exercised (both checks use `clip.y=0`,
//   a value the flip maps to itself) since the texture is only 1 texel tall -- a documented scope
//   reduction, same spirit as the sibling CreateShadowMap test's own.
//   Check C (left, white) expects the same unshadowed colour as Check A; Check D (right, black)
//   expects the same shadowed colour as Check B.
//
// **Discriminating power verified by 2 mutations**: (1) disabling the shadow-darkening branch
// entirely correctly failed Checks B and D (both degenerated to their own unshadowed value) while
// leaving A and C unaffected; (2) hardcoding `shadowTexCoord` to `(0.5,0.5)` (the exact texel
// boundary, ignoring `lightingPosition.xy` entirely) correctly failed Check C -- its own hardcoded
// 50/50-blended sample (~127/255 gray) reads below `ourdepth`, landing in the *shadowed* branch, so
// Check C degenerates to Check D's own shadowed value instead of its distinct expected unshadowed
// one. (Check D's own PASS becomes coincidental under this specific mutation -- both C and D now
// sample the identical blended midpoint and land in the same branch -- but Check C's mismatch alone
// is sufficient evidence the mutation was caught: `shadowTexCoord` no longer depends on
// `lightingPosition.xy`, which is exactly what broke.) Both mutations reverted, reconfirmed 4/4
// PASS.
//
// Exit code 0 = all 4 PASS, 1 = any FAIL.

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
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

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

    // Ported 1:1 from DrawModel.fx's DrawWithShadowMap_VertexShader.
    const char* kVertSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
out vec3 vNormal;
out vec2 vTexCoord;
out vec4 vWorldPos;
uniform mat4 World;
uniform mat4 View;
uniform mat4 Projection;
void main() {
    gl_Position = Projection * View * World * vec4(aPosition, 1.0);
    vNormal = normalize(mat3(World) * aNormal);
    vTexCoord = aTexCoord;
    vWorldPos = World * vec4(aPosition, 1.0);
}
)";

    // Ported 1:1 from DrawModel.fx's DrawWithShadowMap_PixelShader.
    const char* kFragSrc = R"(#version 300 es
precision highp float;
in vec3 vNormal;
in vec2 vTexCoord;
in vec4 vWorldPos;
out vec4 FragColor;
uniform sampler2D TextureSampler;
uniform sampler2D ShadowMapSampler;
uniform vec3 LightDirection;
uniform vec4 AmbientColor;
uniform float DepthBias;
uniform mat4 LightViewProj;
void main() {
    vec4 diffuseColor = texture(TextureSampler, vTexCoord);
    float diffuseIntensity = clamp(dot(LightDirection, vNormal), 0.0, 1.0);
    vec4 diffuse = diffuseIntensity * diffuseColor + AmbientColor;

    vec4 lightingPosition = LightViewProj * vWorldPos;

    vec2 shadowTexCoord = 0.5 * lightingPosition.xy / lightingPosition.w + vec2(0.5, 0.5);
    shadowTexCoord.y = 1.0 - shadowTexCoord.y;

    float shadowdepth = texture(ShadowMapSampler, shadowTexCoord).r;

    float ourdepth = (lightingPosition.z / lightingPosition.w) - DepthBias;

    if (shadowdepth < ourdepth) {
        diffuse *= vec4(0.5, 0.5, 0.5, 0.0);
    }

    FragColor = diffuse;
}
)";
}

class EasyGLShadowMappingDrawWithShadowMapTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::shared_ptr<Effect> fxBase_;
    std::unique_ptr<VertexBuffer> vb_;
    std::unique_ptr<IndexBuffer> ib_;
    Texture2D diffuseTex_;
    Texture2D shadowWhite_;
    Texture2D shadowBlack_;
    Texture2D shadowSplit_;
    bool done_   = false;
    int result_  = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        const auto root = std::filesystem::temp_directory_path()
            / ("cna_shadowmapping_drawwithshadowmap_test_" + std::to_string(
                   reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        WriteFile(root / "dwsm.vert.glsl", kVertSrc);
        WriteFile(root / "dwsm.frag.glsl", kFragSrc);
        WriteFile(root / "dwsm.cnb", R"({
  "cnbVersion": 1,
  "type": "Effect",
  "vertex": "dwsm.vert.glsl",
  "fragment": "dwsm.frag.glsl"
})");

        getContentProperty().setRootDirectoryProperty(root.string());
        fxBase_ = getContentProperty().Load<std::shared_ptr<Effect>>("dwsm");

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

        diffuseTex_ = Texture2D::CreateFromPixels(device, 1, 1,
            std::vector<std::uint8_t>{ 200, 100, 50, 255 });

        shadowWhite_ = Texture2D::CreateFromPixels(device, 1, 1,
            std::vector<std::uint8_t>{ 255, 255, 255, 255 });

        shadowBlack_ = Texture2D::CreateFromPixels(device, 1, 1,
            std::vector<std::uint8_t>{ 0, 0, 0, 0 });

        shadowSplit_ = Texture2D::CreateFromPixels(device, 2, 1, std::vector<std::uint8_t>{
            255, 255, 255, 255,   // texel 0 (left, u in [0,0.5))  -- white, unshadowed
            0,   0,   0,   0,     // texel 1 (right, u in [0.5,1)) -- black, shadowed
        });
    }

    Color DrawOnce(Texture2D& shadowMap, const Matrix& lightViewProj)
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(Color::CornflowerBlue);
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        auto* fx = dynamic_cast<ShaderEffect*>(fxBase_.get());
        fx->setWorldProperty(Matrix::CreateTranslation(0.0f, 0.0f, -5.0f));
        fx->setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero,
                                                  Vector3(0.0f, 1.0f, 0.0f)));
        fx->setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(
            MathHelper::PiOver4, vp.getAspectRatioProperty(), 0.1f, 100.0f));
        fx->Apply();

        fx->SetTexture(0, diffuseTex_);
        fx->SetUniformInt("TextureSampler", 0);
        fx->SetTexture(1, shadowMap);
        fx->SetUniformInt("ShadowMapSampler", 1);
        fx->SetUniformVec3("LightDirection", 0.0f, 0.0f, 1.0f);
        fx->SetUniformVec4("AmbientColor", 0.15f, 0.15f, 0.15f, 0.0f);
        fx->SetUniformFloat("DepthBias", 0.001f);

        float lightViewProjColMajor[16];
        lightViewProj.ToColumnMajor(lightViewProjColMajor);
        fx->SetUniformMat4("LightViewProj", lightViewProjColMajor);

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
            std::printf("[FAIL] EasyGLShadowMappingDrawWithShadowMap: .cnb load or GLSL compile failed\n");
            Exit();
            return;
        }

        const Matrix lightViewProjBase = Matrix::CreateOrthographic(10.0f, 10.0f, -10.0f, 10.0f);
        const Matrix lightViewProjLeft  = Matrix::CreateTranslation(-2.5f, 0.0f, 0.0f) * lightViewProjBase;
        const Matrix lightViewProjRight = Matrix::CreateTranslation( 2.5f, 0.0f, 0.0f) * lightViewProjBase;

        const Color a = DrawOnce(shadowWhite_, lightViewProjBase);
        const Color b = DrawOnce(shadowBlack_, lightViewProjBase);
        const Color c = DrawOnce(shadowSplit_, lightViewProjLeft);
        const Color d = DrawOnce(shadowSplit_, lightViewProjRight);

        const auto close = [](int got, int expected) { return got >= expected - 6 && got <= expected + 6; };
        const auto checkUnshadowed = [&](const Color& col) {
            return close(col.getRProperty(), 238) && close(col.getGProperty(), 138) &&
                   close(col.getBProperty(), 88) && close(col.getAProperty(), 255);
        };
        const auto checkShadowed = [&](const Color& col) {
            return close(col.getRProperty(), 119) && close(col.getGProperty(), 69) &&
                   close(col.getBProperty(), 44) && close(col.getAProperty(), 0);
        };

        const bool aOk = checkUnshadowed(a);
        const bool bOk = checkShadowed(b);
        const bool cOk = checkUnshadowed(c);
        const bool dOk = checkShadowed(d);

        std::printf("[%s] Check A (ShadowMap=white, unshadowed): rgba=(%d,%d,%d,%d) expected~=(238,138,88,255)\n",
                    aOk ? "PASS" : "FAIL", a.getRProperty(), a.getGProperty(), a.getBProperty(), a.getAProperty());
        std::printf("[%s] Check B (ShadowMap=black, shadowed): rgba=(%d,%d,%d,%d) expected~=(119,69,44,0)\n",
                    bOk ? "PASS" : "FAIL", b.getRProperty(), b.getGProperty(), b.getBProperty(), b.getAProperty());
        std::printf("[%s] Check C (split map, left/unshadowed): rgba=(%d,%d,%d,%d) expected~=(238,138,88,255)\n",
                    cOk ? "PASS" : "FAIL", c.getRProperty(), c.getGProperty(), c.getBProperty(), c.getAProperty());
        std::printf("[%s] Check D (split map, right/shadowed): rgba=(%d,%d,%d,%d) expected~=(119,69,44,0)\n",
                    dOk ? "PASS" : "FAIL", d.getRProperty(), d.getGProperty(), d.getBProperty(), d.getAProperty());

        result_ = (aOk && bOk && cOk && dOk) ? 0 : 1;
        Exit();
    }

public:
    EasyGLShadowMappingDrawWithShadowMapTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    EasyGLShadowMappingDrawWithShadowMapTest game;
    game.Run();
    return game.getResult();
}

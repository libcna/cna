// SPDX-License-Identifier: MS-PL
// Task 947 (Phase 78 rollout): HLSL->GLSL shader-conversion proof -- DrawModel.fx's
// `CreateShadowMap` technique (ShadowMappingSample_4_0). Renders the depth of a model from the
// light's own point of view into (what the real sample stores as) a floating-point render target,
// for later sampling by the `DrawWithShadowMap` technique (ported separately, see
// easygl_shadowmapping_drawwithshadowmap_shader_test.cpp -- the two techniques have entirely
// different vertex/fragment shader pairs, unlike PostprocessEffect.Fx's 5 techniques which shared
// one parameterized program, so this sample needs 2 separate `ShaderEffect`s, same shape as the
// `VertexLighting` sample's `VertexLighting.fx`+`FlatShaded.fx` pair).
//
// FNA reference (`ShadowMappingSample_4_0/ShadowMapping/Content/DrawModel.fx`):
//   CreateShadowMap_VSOut CreateShadowMap_VertexShader(float4 Position: POSITION)
//   {
//       CreateShadowMap_VSOut Out;
//       Out.Position = mul(Position, mul(World, LightViewProj));
//       Out.Depth = Out.Position.z / Out.Position.w;
//       return Out;
//   }
//   float4 CreateShadowMap_PixelShader(CreateShadowMap_VSOut input) : COLOR
//   { return float4(input.Depth, 0, 0, 0); }
//
// Ported 1:1 below. The real vertex shader input struct declares only `float4 Position`, so the
// GLSL vertex shader below likewise declares only `aPosition` -- the real dude/grid models (and
// this test's own vertex buffer, `VertexPositionNormalTexture`, stride 32, matching
// `DrawWithShadowMap`'s own input needs) carry Normal/TexCoord too, but this technique's own GLSL
// program simply never declares those attribute locations, which is legal (unused bound vertex
// attributes are simply ignored by the linked program).
//
// Scope note: the real sample renders this technique into an actual `SurfaceFormat.Single`
// (32-bit float, single channel) `RenderTarget2D` -- CNA's `RenderTarget2D`/`Texture2D` float
// surface-format support is untouched by this conversion proof (out of scope, same spirit as the
// NormalDepth test's own documented scope reduction); this test instead renders straight to the
// normal 8-bit backbuffer and reads the R channel back as a byte, which is sufficient to verify
// the depth-encoding *formula* itself (this technique's only genuinely interesting content) since
// the chosen test depths stay safely inside [0,1] and never need the extra range/precision a real
// float target would provide.
//
// `LightViewProj` here is a synthetic, hand-chosen test matrix (a plain `CreateOrthographic`, no
// separate light `View` factored in) -- not derived from the real sample's own
// `CreateLightViewProjectionMatrix()` (which fits an orthographic box around the camera's visible
// frustum), since that derivation is sample application logic, not part of this shader itself.
//
// Matrix::CreateOrthographic(10,10,-10,10) (FNA formula: M33=1/(zn-zf), M43=zn/(zn-zf)) gives
// M33=-0.05, M43=0.5. For a vertex at local origin with World=Translate(0,0,z), WorldPos.z=z
// (translation doesn't touch x/y here) and clip.w=1 (orthographic, no perspective division),
// so Depth = clip.z = z*(-0.05) + 0.5.
//
// Check A -- World=Identity (z=0): Depth = 0.5 -> byte R ~= 128, encoded as (128,0,0,0).
// Check B -- World=Translate(0,0,-5) (z=-5): Depth = -5*(-0.05)+0.5 = 0.75 -> byte R ~= 191,
//   encoded as (191,0,0,0). Distinct from Check A -- proves World genuinely reaches the light-space
//   transform (not just that the formula is well-formed at the origin). This same z=-5 translation
//   and its Depth=0.75 result is reused as the "ourdepth" reference point in the sibling
//   DrawWithShadowMap test, documenting (not literally chaining) the two techniques' relationship.
//
// Exit code 0 = both PASS, 1 = either FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdio>
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

    // Ported 1:1 from DrawModel.fx's CreateShadowMap_VertexShader.
    const char* kVertSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
out float vDepth;
uniform mat4 World;
uniform mat4 LightViewProj;
void main() {
    gl_Position = LightViewProj * World * vec4(aPosition, 1.0);
    vDepth = gl_Position.z / gl_Position.w;
}
)";

    // Ported 1:1 from DrawModel.fx's CreateShadowMap_PixelShader.
    const char* kFragSrc = R"(#version 300 es
precision highp float;
in float vDepth;
out vec4 FragColor;
void main() {
    FragColor = vec4(vDepth, 0.0, 0.0, 0.0);
}
)";
}

class EasyGLShadowMappingCreateShadowMapTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::shared_ptr<Effect> fxBase_;
    std::unique_ptr<VertexBuffer> vb_;
    std::unique_ptr<IndexBuffer> ib_;
    bool done_   = false;
    int result_  = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        const auto root = std::filesystem::temp_directory_path()
            / ("cna_shadowmapping_createshadowmap_test_" + std::to_string(
                   reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        WriteFile(root / "csm.vert.glsl", kVertSrc);
        WriteFile(root / "csm.frag.glsl", kFragSrc);
        WriteFile(root / "csm.cnb", R"({
  "cnbVersion": 1,
  "type": "Effect",
  "vertex": "csm.vert.glsl",
  "fragment": "csm.frag.glsl"
})");

        getContentProperty().setRootDirectoryProperty(root.string());
        fxBase_ = getContentProperty().Load<std::shared_ptr<Effect>>("csm");

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

    Color DrawOnce(const Matrix& world, const Matrix& lightViewProj)
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(Color::White);
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        auto* fx = dynamic_cast<ShaderEffect*>(fxBase_.get());
        fx->setWorldProperty(world);
        fx->Apply();

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
            std::printf("[FAIL] EasyGLShadowMappingCreateShadowMap: .cnb load or GLSL compile failed\n");
            Exit();
            return;
        }

        const Matrix lightViewProj = Matrix::CreateOrthographic(10.0f, 10.0f, -10.0f, 10.0f);

        const Color a = DrawOnce(Matrix::getIdentityProperty(), lightViewProj);
        const Color b = DrawOnce(Matrix::CreateTranslation(0.0f, 0.0f, -5.0f), lightViewProj);

        const auto close = [](int got, int expected) { return got >= expected - 4 && got <= expected + 4; };
        const bool aOk = close(a.getRProperty(), 128) && a.getGProperty() == 0 && a.getBProperty() == 0;
        const bool bOk = close(b.getRProperty(), 191) && b.getGProperty() == 0 && b.getBProperty() == 0;

        std::printf("[%s] Check A (World=Identity, z=0): R=%d expected~=128\n",
                    aOk ? "PASS" : "FAIL", a.getRProperty());
        std::printf("[%s] Check B (World=Translate z=-5): R=%d expected~=191\n",
                    bOk ? "PASS" : "FAIL", b.getRProperty());

        result_ = (aOk && bOk) ? 0 : 1;
        Exit();
    }

public:
    EasyGLShadowMappingCreateShadowMapTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    EasyGLShadowMappingCreateShadowMapTest game;
    game.Run();
    return game.getResult();
}

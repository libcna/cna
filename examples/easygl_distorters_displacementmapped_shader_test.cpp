// SPDX-License-Identifier: MS-PL
// Task 947 (Phase 78 rollout): HLSL->GLSL shader-conversion proof -- Distorters.fx's
// `DisplacementMapped` technique (used by DistortionSample's "Window" distorter,
// `Game.cs:97-103`). A genuinely custom 3D shader (not SpriteBatch), using Task 1079's 3D
// ShaderEffect capability with `VertexPositionTexture` (stride 20 -- Position+TexCoord, already
// an existing supported CNA vertex format, matching this shader's own declared vertex input
// exactly: no Normal/Color at all).
//
// FNA reference (`DistortionSample_4_0/Distortion/Content/Distorters.fx`):
//   struct PositionTextured { float4 Position : POSITION; float2 TexCoord : TEXCOORD; };
//   PositionTextured TransformAndTexture_VertexShader(PositionTextured input)
//   {
//       output.Position = mul(input.Position, WorldViewProjection);
//       output.TexCoord = input.TexCoord;
//       return output;
//   }
//   float4 Textured_PixelShader(float2 texCoord : TEXCOORD) : COLOR
//   {
//       float4 color = tex2D(DisplacementMapSampler, texCoord);
//       return float4(color.rg, 0, color.a);   // blue channel deliberately dropped
//   }
//
// Distorters.fx declares `WorldViewProjection`/`WorldView` as single PRE-COMBINED float4x4
// uniforms (shared across all 4 techniques in the file), unlike this task's earlier ports which
// use separate World/View/Projection matrices via ShaderEffect's IEffectMatrices. Matched here
// exactly: the combined matrix is computed on the C++ side (`World * View * Projection`, same
// XNA row-vector semantics as every other port in this task) and passed as one `WorldViewProjection`
// uniform -- proven mathematically equivalent to the separate-matrix pattern (transpose of a
// product equals the product of transposes in reverse order), not just assumed.
//
// Test: a solid-color DisplacementMap texel (0.6,0.4,0.8,1.0) with a real, non-trivial Blue
// component specifically to prove it gets dropped, not accidentally passed through.
//
// Check A -- World=View=Projection=Identity (quad's local -0.5..0.5 coordinates already land
//   directly in NDC, no camera needed): output = (0.6,0.4,0.0,1.0) ~= (153,102,0,255) -- Blue
//   channel forced to 0 regardless of the source texture's real (204) Blue value.
//
// Exit code 0 = PASS, 1 = FAIL.

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
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

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

    // Ported 1:1 from Distorters.fx's TransformAndTexture_VertexShader -- a single pre-combined
    // WorldViewProjection uniform, matching the original's own declared shape exactly.
    const char* kVertSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;
out vec2 TexCoord;
uniform mat4 WorldViewProjection;
void main() {
    gl_Position = WorldViewProjection * vec4(aPosition, 1.0);
    TexCoord = aTexCoord;
}
)";

    // Ported 1:1 from Distorters.fx's Textured_PixelShader.
    const char* kFragSrc = R"(#version 300 es
precision mediump float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D uDisplacementMap;
void main() {
    vec4 color = texture(uDisplacementMap, TexCoord);
    FragColor = vec4(color.rg, 0.0, color.a);
}
)";
}

class EasyGLDistortersDisplacementMappedTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::shared_ptr<Effect> fxBase_;
    std::unique_ptr<VertexBuffer> vb_;
    std::unique_ptr<IndexBuffer> ib_;
    Texture2D displacementMap_;
    bool done_   = false;
    int result_  = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        const auto root = std::filesystem::temp_directory_path()
            / ("cna_distorters_displacementmapped_test_" + std::to_string(
                   reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        WriteFile(root / "dm.vert.glsl", kVertSrc);
        WriteFile(root / "dm.frag.glsl", kFragSrc);
        WriteFile(root / "dm.cnb", R"({
  "cnbVersion": 1,
  "type": "Effect",
  "vertex": "dm.vert.glsl",
  "fragment": "dm.frag.glsl"
})");

        getContentProperty().setRootDirectoryProperty(root.string());
        fxBase_ = getContentProperty().Load<std::shared_ptr<Effect>>("dm");

        const std::vector<uint8_t> pixels = { 153, 102, 204, 255 };
        displacementMap_ = Texture2D::CreateFromPixels(device, 1, 1, pixels);

        const VertexPositionTexture verts[4] = {
            { Vector3(-0.5f,  0.5f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3(-0.5f, -0.5f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3( 0.5f, -0.5f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3( 0.5f,  0.5f, 0.0f), Vector2(1.0f, 1.0f) },
        };
        const std::uint16_t indices[6] = { 0, 1, 2, 0, 2, 3 };

        vb_ = std::make_unique<VertexBuffer>(device, 4);
        vb_->SetData(verts, 4);
        ib_ = std::make_unique<IndexBuffer>(device, 6);
        ib_->SetData(indices, 6);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        auto* fx = dynamic_cast<ShaderEffect*>(fxBase_.get());
        if (!fx || !fx->IsEffectValid())
        {
            std::printf("[FAIL] EasyGLDistortersDisplacementMapped: .cnb load or GLSL compile failed\n");
            Exit();
            return;
        }

        device.Clear(Color(10, 10, 10, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        fx->Apply();
        float wvpCM[16];
        Matrix::getIdentityProperty().ToColumnMajor(wvpCM);
        fx->SetUniformMat4("WorldViewProjection", wvpCM);
        fx->SetTexture(0, displacementMap_);
        fx->SetUniformInt("uDisplacementMap", 0);

        device.SetVertexBuffer(vb_.get());
        device.setIndicesProperty(ib_.get());
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);

        Color c(0, 0, 0, 0);
        const Rectangle centre(W / 2, H / 2, 1, 1);
        device.GetBackBufferData(&centre, &c, 0, 1);

        const bool ok = c.getRProperty() >= 147 && c.getRProperty() <= 159
                     && c.getGProperty() >= 96 && c.getGProperty() <= 108
                     && c.getBProperty() == 0;

        std::printf("[%s] DisplacementMapped: (%d,%d,%d) expected~=(153,102,0)\n",
                    ok ? "PASS" : "FAIL", c.getRProperty(), c.getGProperty(), c.getBProperty());

        result_ = ok ? 0 : 1;
        Exit();
    }

public:
    EasyGLDistortersDisplacementMappedTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    EasyGLDistortersDisplacementMappedTest game;
    game.Run();
    return game.getResult();
}

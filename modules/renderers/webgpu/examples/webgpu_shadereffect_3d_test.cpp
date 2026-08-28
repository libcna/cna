// SPDX-License-Identifier: MS-PL
// WEBGPU-76: a custom-WGSL ShaderEffect on the 3D DrawPrimitives route.
//
// Draws a screen-facing VertexPositionNormalTexture quad through a ShaderEffect whose WGSL vertex
// and fragment source this test supplies directly (dialect Wgsl). The shader is a Lambert
// texture*diffuse*N.L, with World/View/Projection + uLightDir + uDiffuseColor in one uniform block
// declared via DeclareUniformBlockEXT. Only pure-channel colours are asserted, so the backbuffer's
// colour space cannot shift the result.
//
// Check A -- World=Identity, uLightDir faces the +Z normal, uDiffuseColor=red: the quad is fully
//   lit and renders RED. Proves the custom WGSL genuinely runs (a stock effect would ignore these
//   uniforms), that N.L lighting works, and that a texture is sampled.
// Check B -- World=RotateY(180 deg): the vertex shader rotates the normal to -Z, so N.L<=0 and the
//   quad renders BLACK. Proves the `World` matrix reaches the WGSL vertex stage (if it did not, the
//   normal would stay +Z and this would still be red).
// Check C -- World=Identity, uDiffuseColor=blue: the quad renders BLUE. Proves the uDiffuseColor
//   uniform genuinely drives the pixel (a value, not a baked constant).
//
// The background is cleared to GREEN, distinct in every channel from red/black/blue, so a quad that
// failed to render (e.g. a mis-built pipeline) would read green and fail rather than pass silently.
//
// Exit code 0 = all checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    // Two independent WGSL modules (entry points vs_main / fs_main). The uniform block layout
    // matches kOffsets below exactly; @group(0) @binding(0) is the uniform block, and because the
    // fragment samples a texture it also declares the reserved sampler/texture at bindings 1/2.
    const char* const kVertexWgsl = R"WGSL(
struct Uniforms {
    World: mat4x4f,
    View: mat4x4f,
    Projection: mat4x4f,
    uLightDir: vec3f,
    uDiffuseColor: vec3f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;
struct VOut {
    @builtin(position) position: vec4f,
    @location(0) worldNormal: vec3f,
    @location(1) uv: vec2f,
};
@vertex fn vs_main(
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f
) -> VOut {
    var out: VOut;
    out.position = u.Projection * u.View * u.World * vec4f(position, 1.0);
    out.worldNormal = (u.World * vec4f(normal, 0.0)).xyz;
    out.uv = uv;
    return out;
}
)WGSL";

    const char* const kFragmentWgsl = R"WGSL(
struct Uniforms {
    World: mat4x4f,
    View: mat4x4f,
    Projection: mat4x4f,
    uLightDir: vec3f,
    uDiffuseColor: vec3f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var tex: texture_2d<f32>;
@fragment fn fs_main(
    @location(0) worldNormal: vec3f,
    @location(1) uv: vec2f
) -> @location(0) vec4f {
    let n = normalize(worldNormal);
    let ndotl = max(dot(n, normalize(u.uLightDir)), 0.0);
    let texel = textureSample(tex, texSampler, uv).rgb;
    return vec4f(texel * u.uDiffuseColor * ndotl, 1.0);
}
)WGSL";

    // WGSL/std140 uniform block: three mat4x4 (64 B each) then two 16-aligned vec3.
    const char* const kUniformNames[] = {"World", "View", "Projection", "uLightDir", "uDiffuseColor"};
    const int kUniformOffsets[] = {0, 64, 128, 192, 208};
    constexpr int kUniformBlockSize = 224;

    bool colorNear(Color a, Color b, int tol = 20)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) <= tol &&
               std::abs(a.getGProperty() - b.getGProperty()) <= tol &&
               std::abs(a.getBProperty() - b.getBProperty()) <= tol;
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const Rectangle region(kSize / 2, kSize / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    VertexBuffer MakeQuad(GraphicsDevice& dev)
    {
        VertexBuffer vb(dev, VertexPositionNormalTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
        const Vector3 n(0.0f, 0.0f, 1.0f);  // +Z, toward the camera at (0,0,3)
        const VertexPositionNormalTexture verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 0.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 1.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 1.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
        };
        vb.SetData(verts, 0, 6);
        return vb;
    }
}

class WebGpuShaderEffect3DTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D whiteTex_;
    bool done_ = false;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

protected:
    void LoadContent() override
    {
        whiteTex_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                                                std::vector<std::uint8_t>{255, 255, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);

        ShaderEffect fx(dev, kVertexWgsl, kFragmentWgsl);
        if (!fx.IsEffectValid())
        {
            std::printf("[FAIL] ShaderEffect failed to compile: %s\n", fx.GetCompileErrorEXT().c_str());
            std::printf("=== 0/3 PASS ===\n");
            result_ = 1;
            Exit();
            return;
        }
        fx.DeclareUniformBlockEXT(kUniformBlockSize, kUniformNames, kUniformOffsets, 5);

        const Matrix view = Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero, Vector3::Up);
        const Matrix projection =
            Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f);

        auto drawQuad = [&](const Matrix& world, const Vector3& diffuse)
        {
            dev.Clear(Color::Green);
            VertexBuffer vb = MakeQuad(dev);
            fx.setWorldProperty(world);
            fx.setViewProperty(view);
            fx.setProjectionProperty(projection);
            fx.Apply();
            fx.SetTexture(0, whiteTex_);
            fx.SetUniformVec3("uLightDir", 0.0f, 0.0f, 1.0f);
            fx.SetUniformVec3("uDiffuseColor", diffuse.X, diffuse.Y, diffuse.Z);
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            return readCenter(dev);
        };

        // Check A: fully-lit red proves the custom WGSL runs, lights and samples.
        check(colorNear(drawQuad(Matrix::getIdentityProperty(), Vector3(1.0f, 0.0f, 0.0f)), Color::Red),
              "custom WGSL, World=Identity, light faces normal, uDiffuseColor=red -> red");

        // Check B: World=RotateY(180) flips the normal in the vertex stage, so N.L<=0 -> black.
        check(colorNear(drawQuad(Matrix::CreateRotationY(MathHelper::Pi), Vector3(1.0f, 0.0f, 0.0f)),
                        Color::Black),
              "World=RotateY(180) reaches the vertex shader (normal flips) -> black");

        // Check C: a different uDiffuseColor drives a different pixel -> blue, not red.
        check(colorNear(drawQuad(Matrix::getIdentityProperty(), Vector3(0.0f, 0.0f, 1.0f)), Color::Blue),
              "uDiffuseColor uniform drives the fragment output -> blue");

        std::printf("=== %d/3 PASS ===\n", passCount_);
        result_ = (passCount_ == 3) ? 0 : 1;
        Exit();
    }

public:
    WebGpuShaderEffect3DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    WebGpuShaderEffect3DTest game;
    game.Run();
    return game.getResult();
}

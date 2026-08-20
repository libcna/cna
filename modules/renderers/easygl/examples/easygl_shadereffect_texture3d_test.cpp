// SPDX-License-Identifier: MS-PL
// plans/plan_graphics.md Task 863: proves `ShaderEffect::SetTexture(int, Texture3D&)` -- binding a
// genuine volume (3D) texture to a custom shader's own `sampler3D` uniform. Mirrors
// `easygl_shadereffect_texturecube_test.cpp` (Task 1081)'s own structure and false-positive-
// avoidance technique exactly, adapted from a `samplerCube`/direction-vector proof to a
// `sampler3D`/UVW-coordinate proof.
//
// Background: before Task 863, `Texture3D` inherited `GraphicsResource` directly (not `Texture`),
// so it could not be stored in a `TextureCollection` (`GraphicsDevice.Textures[slot]`) at all --
// and separately, `IEffectRenderer` had no texture-binding API whatsoever for a volume texture
// (only `BindTexture()` for `ITextureRenderer` and, since Task 1081, `BindTextureCube()` for
// `ITextureCubeRenderer`), so a custom shader's own `sampler3D` uniform had no way to ever receive
// real texture data. Implementation (3 small, additive changes, EasyGL only, matching Task 1081's
// established renderer scope and this session's own explicit deferral of the other 9 CNA
// renderers): (1) `ITexture3DRenderer` gained a `BindGL()` virtual (defaulting to a no-op, mirroring
// `ITextureCubeRenderer::BindGL()`); `IEffectRenderer` gained `BindTexture3D(int unit,
// ITexture3DRenderer*)`, also defaulting to a no-op; (2) `EasyGLTexture3DRenderer::BindGL()` binds
// `GL_TEXTURE_3D` and `EasyGLEffectRenderer::BindTexture3D()` mirrors `BindTextureCube()` exactly
// (bind the unit, call the renderer's own `BindGL()`, restore unit 0); (3) `ShaderEffect::
// SetTexture()` gained a `Texture3D&` overload alongside the existing `Texture2D&`/`TextureCube&`
// ones. GL itself allows a 2D/cube/3D texture bound to the *same* unit simultaneously (each
// occupies a distinct binding target), so this needed no changes to either existing path.
//
// This test: a 1x1x2 `Texture3D` (a single voxel column along Z) with a distinct solid colour per
// Z-slice (slice 0 = red, slice 1 = blue). A trivial fragment shader samples the volume directly
// along a uniform `vec3 coord` (no real ray-marching/volumetric geometry needed for this
// capability proof -- the vertex shader just projects a screen-covering quad). Both the min and
// mag filters are GL_LINEAR (`EasyGLTexture3DRenderer`'s own fixed setup), but sampling exactly at
// each slice's texel centre (Z = 0.25 for slice 0, Z = 0.75 for slice 1, matching
// `(sliceIndex + 0.5) / depth`) still reads back that slice's exact colour: linear filtering
// exactly at a texel centre puts 100% of the interpolation weight on that one texel.
//
// Check A -- `coord=(0.5,0.5,0.25)`: samples Z-slice 0 exactly -> `(255,0,0,255)`.
// Check B -- `coord=(0.5,0.5,0.75)`: samples Z-slice 1 exactly -> `(0,0,255,255)` -- distinct from
//   Check A, proving the volume texture is genuinely bound (not reading a stale/default value) and
//   that different Z-coordinates genuinely select different slices (not hardcoded to always read
//   one slice).
//
// False-positive guard (same technique Task 1081's own test needed): a second, all-black decoy
// `Texture3D` is created and uploaded AFTER the real one, so an unmutated `SetTexture()`/
// `BindTexture3D()` call is required each `DrawOnce()` to override GL's own texture-unit-0/
// `GL_TEXTURE_3D` binding back to the intended texture before the shader reads it -- disabling
// `BindTexture3D()` would otherwise still read back the real texture's residual upload-time
// binding and pass for the wrong reason.
//
// Exit code 0 = both PASS, 1 = either FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

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

    const char* kVertSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;
void main() {
    gl_Position = vec4(aPosition, 1.0);
}
)";

    const char* kFragSrc = R"(#version 300 es
precision highp float;
precision highp sampler3D;
out vec4 FragColor;
uniform sampler3D VolumeSampler;
uniform vec3 coord;
void main() {
    FragColor = texture(VolumeSampler, coord);
}
)";
}

class EasyGLTexture3DShaderTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::shared_ptr<Effect> fxBase_;
    std::unique_ptr<VertexBuffer> vb_;
    std::unique_ptr<IndexBuffer> ib_;
    std::unique_ptr<Texture3D> volumeTex_;
    std::unique_ptr<Texture3D> decoyTex_;
    bool done_   = false;
    int result_  = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        const auto root = std::filesystem::temp_directory_path()
            / ("cna_texture3d_shader_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        WriteFile(root / "t3.vert.glsl", kVertSrc);
        WriteFile(root / "t3.frag.glsl", kFragSrc);
        WriteFile(root / "t3.cnj", R"({
  "cnjVersion": 1,
  "type": "Effect",
  "vertex": "t3.vert.glsl",
  "fragment": "t3.frag.glsl"
})");

        getContentProperty().setRootDirectoryProperty(root.string());
        fxBase_ = getContentProperty().Load<std::shared_ptr<Effect>>("t3");

        const VertexPositionTexture verts[4] = {
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), Vector2(1.0f, 0.0f) },
        };
        vb_ = std::make_unique<VertexBuffer>(device, 4);
        vb_->SetData(verts, 4);

        const std::uint16_t indices[6] = { 0, 1, 2, 0, 2, 3 };
        ib_ = std::make_unique<IndexBuffer>(device, 6);
        ib_->SetData(indices, 6);

        // 1x1x2 volume: slice Z=0 red, slice Z=1 blue.
        volumeTex_ = std::make_unique<Texture3D>(device, 1, 1, 2, false, SurfaceFormat::Color);
        const Color slices[2] = {
            Color(255, 0, 0, 255), // Z=0
            Color(0, 0, 255, 255), // Z=1
        };
        volumeTex_->SetData(slices, 2);

        // A second, all-black decoy volume texture, created and uploaded AFTER volumeTex_ -- see
        // this file's header comment for why this rules out a residual-GL-binding false positive.
        decoyTex_ = std::make_unique<Texture3D>(device, 1, 1, 2, false, SurfaceFormat::Color);
        const Color black[2] = { Color(0, 0, 0, 255), Color(0, 0, 0, 255) };
        decoyTex_->SetData(black, 2);
    }

    Color DrawOnce(float u, float v, float w)
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
        fx->Apply();

        fx->SetTexture(0, *volumeTex_);
        fx->SetUniformInt("VolumeSampler", 0);
        fx->SetUniformVec3("coord", u, v, w);

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
            std::printf("[FAIL] EasyGLTexture3DShader: .cnj load or GLSL compile failed\n");
            Exit();
            return;
        }

        const Color a = DrawOnce(0.5f, 0.5f, 0.25f); // Z-slice 0 texel centre
        const Color b = DrawOnce(0.5f, 0.5f, 0.75f); // Z-slice 1 texel centre

        const auto close = [](int got, int expected) { return got >= expected - 6 && got <= expected + 6; };
        const bool aOk = close(a.getRProperty(), 255) && close(a.getGProperty(), 0) &&
                        close(a.getBProperty(), 0) && close(a.getAProperty(), 255);
        const bool bOk = close(b.getRProperty(), 0) && close(b.getGProperty(), 0) &&
                        close(b.getBProperty(), 255) && close(b.getAProperty(), 255);

        std::printf("[%s] Check A (coord=(0.5,0.5,0.25), Z-slice 0): rgba=(%d,%d,%d,%d) expected~=(255,0,0,255)\n",
                    aOk ? "PASS" : "FAIL", a.getRProperty(), a.getGProperty(), a.getBProperty(), a.getAProperty());
        std::printf("[%s] Check B (coord=(0.5,0.5,0.75), Z-slice 1): rgba=(%d,%d,%d,%d) expected~=(0,0,255,255)\n",
                    bOk ? "PASS" : "FAIL", b.getRProperty(), b.getGProperty(), b.getBProperty(), b.getAProperty());

        result_ = (aOk && bOk) ? 0 : 1;
        Exit();
    }

public:
    EasyGLTexture3DShaderTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    EasyGLTexture3DShaderTest game;
    game.Run();
    return game.getResult();
}

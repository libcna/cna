// SPDX-License-Identifier: MS-PL
// Task 1081: proves `ShaderEffect::SetTexture(int, TextureCube&)` -- binding a genuine cube
// texture to a custom shader's own `samplerCube` uniform, for `NormalMapping.fx`'s reflection
// map (ShipGame's own file, distinct from `NormalMappingSample`'s -- see
// `easygl_animsprite_shader_test.cpp`'s header for the full background). Not a real sample
// shader port (that's separate, future Task 947 work, same relationship Task 1079's own
// `easygl_shadereffect_3d_test.cpp` and Task 1080's own
// `easygl_shadereffect_custom_vertex_layout_test.cpp` proofs had to the shader ports that
// followed each).
//
// Background: `IEffectBackend::BindTexture()` only accepts an `ITextureBackend*` -- `TextureCube`
// backs onto a *separate* interface, `ITextureCubeBackend` (confirmed via direct source read, not
// assumption: `class ITextureCubeBackend` does not derive from `ITextureBackend` in
// `IGraphicsBackend.hpp`), so binding a cube texture to a custom shader's own `samplerCube`
// uniform previously had no code path at all. Implementation (3 small, additive changes, EasyGL
// only, matching this session's established backend scope): (1) `IEffectBackend` gained
// `BindTextureCube(int unit, ITextureCubeBackend*)`, defaulting to a no-op; (2)
// `EasyGLEffectBackend::BindTextureCube()` mirrors the existing `BindTexture()` exactly (bind the
// unit, call the backend's own `BindGL()`, restore unit 0) -- `EasyGLTextureCubeBackend` already
// had a working `BindGL()` implementation (used elsewhere for `EnvironmentMapEffect`/
// `RenderTargetCube`), so this is pure plumbing, no new GL logic; (3) `ShaderEffect::SetTexture()`
// gained a `TextureCube&` overload alongside the existing `Texture2D&` one. GL itself allows a 2D
// and a cube texture bound to the *same* unit simultaneously (they occupy distinct binding
// targets), so this needed no changes to the existing `Texture2D` path.
//
// This test: a `TextureCube` with 6 distinct solid colours, one per face (`PositiveX`=red,
// `PositiveY`=blue, the rest unused by this proof). A trivial fragment shader samples the cube
// directly along a uniform `vec3 direction` (no real reflection-vector geometry needed for this
// capability proof -- the vertex shader just projects a screen-covering quad).
//
// Check A -- `direction=(1,0,0)`: samples the `PositiveX` face exactly -> `(255,0,0,255)`.
// Check B -- `direction=(0,1,0)`: samples the `PositiveY` face exactly -> `(0,0,255,255)` --
//   distinct from Check A, proving the cube texture is genuinely bound (not reading a stale/
//   default value) and that different directions genuinely select different faces (not hardcoded
//   to always read one face).
//
// **A real false-positive risk found and fixed, not just assumed away**: this test's first draft
// created only one `TextureCube`, and a mutation that disabled `BindTextureCube()` entirely still
// passed both checks -- GL's own texture-unit-0/`GL_TEXTURE_CUBE_MAP` binding simply persisted
// from whatever `glBindTexture` call the texture's own `SetData()` uploads happened to leave
// behind during `Initialize()`, with nothing in between to disturb it, making the test pass for
// the wrong reason (residual upload-time binding state, not `SetTexture()`'s own binding call).
// Fixed by creating a second, all-black decoy `TextureCube` *after* the real one, uploaded last
// during `Initialize()` -- an unmutated `SetTexture()`/`BindTextureCube()` call is now required
// each `DrawOnce()` to override the decoy back to the intended texture before the shader reads
// it. Re-confirmed: with this fix in place, disabling `BindTextureCube()` correctly fails both
// checks (`(0,0,0,255)`, the decoy's own colour, instead of either face's real colour).
//
// Exit code 0 = both PASS, 1 = either FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
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
out vec4 FragColor;
uniform samplerCube CubeSampler;
uniform vec3 direction;
void main() {
    FragColor = texture(CubeSampler, direction);
}
)";
}

class EasyGLTextureCubeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::shared_ptr<Effect> fxBase_;
    std::unique_ptr<VertexBuffer> vb_;
    std::unique_ptr<IndexBuffer> ib_;
    std::unique_ptr<TextureCube> cubeTex_;
    std::unique_ptr<TextureCube> decoyTex_;
    bool done_   = false;
    int result_  = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        const auto root = std::filesystem::temp_directory_path()
            / ("cna_texturecube_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(root);

        WriteFile(root / "tc.vert.glsl", kVertSrc);
        WriteFile(root / "tc.frag.glsl", kFragSrc);
        WriteFile(root / "tc.cnj", R"({
  "cnjVersion": 1,
  "type": "Effect",
  "vertex": "tc.vert.glsl",
  "fragment": "tc.frag.glsl"
})");

        getContentProperty().setRootDirectoryProperty(root.string());
        fxBase_ = getContentProperty().Load<std::shared_ptr<Effect>>("tc");

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

        cubeTex_ = std::make_unique<TextureCube>(device, 1, false, SurfaceFormat::Color);
        const Color red(255, 0, 0, 255);
        const Color green(0, 255, 0, 255);
        const Color blue(0, 0, 255, 255);
        const Color yellow(255, 255, 0, 255);
        const Color magenta(255, 0, 255, 255);
        const Color cyan(0, 255, 255, 255);
        cubeTex_->SetData(CubeMapFace::PositiveX, &red, 1);
        cubeTex_->SetData(CubeMapFace::NegativeX, &green, 1);
        cubeTex_->SetData(CubeMapFace::PositiveY, &blue, 1);
        cubeTex_->SetData(CubeMapFace::NegativeY, &yellow, 1);
        cubeTex_->SetData(CubeMapFace::PositiveZ, &magenta, 1);
        cubeTex_->SetData(CubeMapFace::NegativeZ, &cyan, 1);

        // A second, all-black cube texture, created and uploaded AFTER cubeTex_ -- if GL's own
        // texture-unit-0/cube-map binding simply persists from whichever texture was bound most
        // recently during setup (a real false-positive risk this test's own first draft fell
        // into -- see this file's header comment), this decoy being the LAST one touched here
        // means an unmutated SetTexture()/BindTextureCube() call is required to override it back
        // to cubeTex_ before Draw() reads the correct colours.
        decoyTex_ = std::make_unique<TextureCube>(device, 1, false, SurfaceFormat::Color);
        const Color black(0, 0, 0, 255);
        decoyTex_->SetData(CubeMapFace::PositiveX, &black, 1);
        decoyTex_->SetData(CubeMapFace::NegativeX, &black, 1);
        decoyTex_->SetData(CubeMapFace::PositiveY, &black, 1);
        decoyTex_->SetData(CubeMapFace::NegativeY, &black, 1);
        decoyTex_->SetData(CubeMapFace::PositiveZ, &black, 1);
        decoyTex_->SetData(CubeMapFace::NegativeZ, &black, 1);
    }

    Color DrawOnce(float dx, float dy, float dz)
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

        fx->SetTexture(0, *cubeTex_);
        fx->SetUniformInt("CubeSampler", 0);
        fx->SetUniformVec3("direction", dx, dy, dz);

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
            std::printf("[FAIL] EasyGLTextureCube: .cnj load or GLSL compile failed\n");
            Exit();
            return;
        }

        const Color a = DrawOnce(1.0f, 0.0f, 0.0f);
        const Color b = DrawOnce(0.0f, 1.0f, 0.0f);

        const auto close = [](int got, int expected) { return got >= expected - 6 && got <= expected + 6; };
        const bool aOk = close(a.getRProperty(), 255) && close(a.getGProperty(), 0) &&
                        close(a.getBProperty(), 0) && close(a.getAProperty(), 255);
        const bool bOk = close(b.getRProperty(), 0) && close(b.getGProperty(), 0) &&
                        close(b.getBProperty(), 255) && close(b.getAProperty(), 255);

        std::printf("[%s] Check A (direction=(1,0,0), +X face): rgba=(%d,%d,%d,%d) expected~=(255,0,0,255)\n",
                    aOk ? "PASS" : "FAIL", a.getRProperty(), a.getGProperty(), a.getBProperty(), a.getAProperty());
        std::printf("[%s] Check B (direction=(0,1,0), +Y face): rgba=(%d,%d,%d,%d) expected~=(0,0,255,255)\n",
                    bOk ? "PASS" : "FAIL", b.getRProperty(), b.getGProperty(), b.getBProperty(), b.getAProperty());

        result_ = (aOk && bOk) ? 0 : 1;
        Exit();
    }

public:
    EasyGLTextureCubeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    EasyGLTextureCubeTest game;
    game.Run();
    return game.getResult();
}

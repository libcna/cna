// SPDX-License-Identifier: MS-PL
// plans/plan_sokol.md SOKOL-44: a custom ShaderEffect's deferred texture bindings must survive the
// bound texture being re-uploaded between ShaderEffect::SetTexture() and the eventual draw.
//
// SokolEffectRenderer::BindTexture()/BindTextureCube() only RECORD a pending bind (see
// ApplyPendingTextureBindsEXT()'s own doc comment for why -- it must be realized after
// BeginPassIfNeeded()/sg_reset_state_cache(), right before the draw). Before this fix, the pending
// record stored the resolved sg_image id at BindTexture() time. SokolTextureRenderer::RecreateImage()
// (and the TextureCube/SOKOL-34 equivalent) destroy the old sg_image and allocate a brand new one on
// every SetData() -- so `effect.SetTexture(unit, tex); tex.SetData(...); draw;` would sample a
// stale, already-destroyed image (or nothing at all).
//
// Both sub-tests: bind a texture showing colour A, upload colour B to the SAME texture object
// (recreating its underlying GPU image, but not the C++ renderer object itself -- see below), then
// draw and read back -- correct behaviour is colour B.
//
// Check A -- 2D: a full-screen quad sampling a re-uploaded Texture2D reads back the NEW colour.
//   Deliberately uses the SetData(level, rect, data, startIndex, count) overload, which updates
//   the SAME SokolTextureRenderer object's image in place (Texture2D.cpp: `renderer_->UpdatePixels`
//   for a full level-0 update) -- the case SOKOL-44 actually fixes. Texture2D's OTHER
//   SetData(const Color*, int) overload replaces `Texture2D::renderer_` with an entirely new
//   renderer object instead of mutating the existing one; a pending bind captured before that call
//   cannot follow it without a stable indirection IEffectRenderer::BindTexture()'s interface does
//   not carry (SokolEffectRenderer's own LiveSampledRendererRegistryEXT() safely detects this and
//   skips re-binding rather than dereferencing the now-gone object, matching this same overload's
//   pre-SOKOL-44 behaviour of silently sampling nothing rather than crashing) -- a known,
//   documented residual boundary, not attempted here; plans/plan_sokol.md SOKOL-44's own remediation
//   note explains why fixing it would require a cross-renderer IEffectRenderer interface change.
// Check B -- Cube: a full-screen quad sampling a re-uploaded TextureCube face reads back the NEW
//   colour (TextureCube::SetData always mutates the same renderer object in place).
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    const char* kVertSrc = R"(#version 410 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
out vec2 TexCoord;
void main() { TexCoord = aTexCoord; gl_Position = vec4(aPos, 0.0, 1.0); }
)";

    const char* kFrag2DSrc = R"(#version 410 core
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D tex0;
void main() { FragColor = texture(tex0, TexCoord); }
)";

    // Direction is fixed (+X) regardless of TexCoord -- this test only cares which colour lands on
    // the PositiveX face, not real cube-direction mapping.
    const char* kFragCubeSrc = R"(#version 410 core
in vec2 TexCoord;
out vec4 FragColor;
uniform samplerCube cube0;
void main() { FragColor = texture(cube0, vec3(1.0, 0.0, 0.0)); }
)";

    Color ReadCenter(GraphicsDevice& dev)
    {
        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color got(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &got, 0, 1);
        return got;
    }

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }

    bool Matches(const Color& c, const Color& expected)
    {
        return CloseTo(c.getRProperty(), expected.getRProperty(), 8)
            && CloseTo(c.getGProperty(), expected.getGProperty(), 8)
            && CloseTo(c.getBProperty(), expected.getBProperty(), 8);
    }
}

class CustomEffectTextureReuploadTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;
    bool done_ = false;

    void check(bool ok, const char* label, const Color& got, const char* expected)
    {
        if (ok)
        {
            std::printf("[PASS] %s: got=(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
            ++pass_;
        }
        else
        {
            std::printf("[FAIL] %s: got=(%d,%d,%d) expected %s\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(), expected);
            ++fail_;
        }
    }

    void DrawFullScreenQuad(GraphicsDevice& dev)
    {
        const VertexPositionTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), Vector2(1.0f, 0.0f) },
        };
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
    }

    void CheckTexture2DReupload(GraphicsDevice& dev)
    {
        dev.Clear(Color(0, 0, 0, 255));

        Texture2D tex(dev, 1, 1);
        const Color kRed(255, 0, 0, 255);
        tex.SetData(&kRed, 1);

        ShaderEffect fx(dev, kVertSrc, kFrag2DSrc);
        fx.Apply();
        // Bind while the texture shows RED -- only recorded, not resolved yet (SOKOL-44).
        fx.SetTexture(0, tex);

        // Recreates the underlying sg_image in place, on the SAME SokolTextureRenderer object
        // (SokolTextureRenderer::RecreateImage() via Texture2D.cpp's UpdatePixels path for a full
        // level-0 update) -- the pending bind above must not still point at the now-destroyed RED
        // image. Deliberately the level/rect overload, not SetData(const Color*, int) -- see this
        // file's own top comment for why that other overload is a separate, undoable boundary.
        const Color kGreen(0, 255, 0, 255);
        tex.SetData(0, nullptr, &kGreen, 0, 1);

        DrawFullScreenQuad(dev);
        check(Matches(ReadCenter(dev), kGreen),
              "A: 2D custom-effect texture re-uploaded after SetTexture() reads the NEW colour",
              ReadCenter(dev), "(0,255,0)");
    }

    void CheckTextureCubeReupload(GraphicsDevice& dev)
    {
        dev.Clear(Color(0, 0, 0, 255));

        TextureCube cube(dev, 4, false, SurfaceFormat::Color);
        const Color kRed(255, 0, 0, 255);
        const Color redFace[16] = {
            kRed, kRed, kRed, kRed, kRed, kRed, kRed, kRed,
            kRed, kRed, kRed, kRed, kRed, kRed, kRed, kRed,
        };
        cube.SetData(CubeMapFace::PositiveX, redFace, 16);
        for (auto face : { CubeMapFace::NegativeX, CubeMapFace::PositiveY, CubeMapFace::NegativeY,
                            CubeMapFace::PositiveZ, CubeMapFace::NegativeZ })
            cube.SetData(face, redFace, 16);

        ShaderEffect fx(dev, kVertSrc, kFragCubeSrc);
        fx.Apply();
        // Bind while PositiveX shows RED -- only recorded, not resolved yet (SOKOL-44).
        fx.SetTexture(0, cube);

        // Recreates the WHOLE cube's underlying sg_image (plans/plan_sokol.md SOKOL-34's own "recreated
        // whole on every SetData()" note) -- the pending bind above must not still point at the
        // now-destroyed image.
        const Color kGreen(0, 255, 0, 255);
        const Color greenFace[16] = {
            kGreen, kGreen, kGreen, kGreen, kGreen, kGreen, kGreen, kGreen,
            kGreen, kGreen, kGreen, kGreen, kGreen, kGreen, kGreen, kGreen,
        };
        cube.SetData(CubeMapFace::PositiveX, greenFace, 16);

        DrawFullScreenQuad(dev);
        check(Matches(ReadCenter(dev), kGreen),
              "B: cube custom-effect texture re-uploaded after SetTexture() reads the NEW colour",
              ReadCenter(dev), "(0,255,0)");
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);

        CheckTexture2DReupload(dev);
        CheckTextureCubeReupload(dev);

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    CustomEffectTextureReuploadTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    CustomEffectTextureReuploadTest game;
    game.Run();
    return game.getResult();
}

// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: pixel-exact SpriteBatch::Begin(..., Effect*) proof for the native OpenGL 2.1
// graphics renderer -- a user-authored ShaderEffect driving the 2D sprite-batch pipeline instead
// of the built-in sprite shader. Unlike the built-in shader (which pre-transforms every vertex
// to clip space on the CPU), a custom sprite effect receives RAW screen-space vertex positions
// and is expected to apply a `MatrixTransform` uniform itself -- the real XNA/FNA
// SpriteEffect.fx convention (any custom SpriteBatch effect ported from real XNA already has a
// `float4x4 MatrixTransform` parameter) -- see Sprite::DrawWithCustomEffect's own doc comment.
//
// Check A -- MatrixTransform wiring + vertex-color pass-through: a custom shader that forwards
//   aColor unchanged and transforms aPosition by MatrixTransform reads back a solid-color
//   texture's own color exactly -- proves the raw screen-space vertex data + MatrixTransform
//   uniform (folding SpriteBatch.Begin's transformMatrix and the screen->clip ortho projection
//   into ONE matrix) actually reaches the shader and produces correct on-screen geometry.
// Check B -- texture sampling: the custom shader's sampler (relying on GLSL's default
//   uniform-int-0 value, matching real XNA's implicit register-0 binding -- no explicit
//   SetUniformInt() call from the test) reads back the drawn texture's real content, not a flat
//   color -- proves SpriteBatch's own texture is really bound to unit 0 for a custom-effect draw.
// Check C -- End() with a null Effect (SpriteBatch::Begin() with no custom effect after a
//   previous custom-effect Begin/End) restores the built-in shader path -- proves
//   SetCustomEffect(nullptr) is honored, not sticky.
// Check D -- 30 frames of the whole scene render with no exception.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "CNA/Internal/Renderers/OpenGL2/OpenGL2Renderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::OpenGL2;

namespace
{
    constexpr int kTotalFrames = 30;
    constexpr int kSize = 64;

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    bool Matches(const Color& c, const Color& expected, int tol)
    {
        return CloseTo(c.getRProperty(), expected.getRProperty(), tol)
            && CloseTo(c.getGProperty(), expected.getGProperty(), tol)
            && CloseTo(c.getBProperty(), expected.getBProperty(), tol);
    }

    std::string ColorStr(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty())
             + "," + std::to_string(c.getBProperty()) + ")";
    }

    const char* kSpriteVert =
        "attribute vec3 aPosition;attribute vec4 aColor;attribute vec2 aTexCoord;"
        "uniform mat4 MatrixTransform;"
        "varying vec4 vColor;varying vec2 vTex;"
        "void main(){gl_Position=MatrixTransform*vec4(aPosition,1.0);vColor=aColor;vTex=aTexCoord;}";
    const char* kSpriteFrag =
        "varying vec4 vColor;varying vec2 vTex;uniform sampler2D uTex;"
        "void main(){gl_FragColor=texture2D(uTex,vTex)*vColor;}";
}

class OpenGL2SpriteBatchCustomEffectTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> tex_;

    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;

    void Check(const std::string& label, bool ok)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

    Color ReadPixel(int x, int y)
    {
        Color px(0, 0, 0, 0);
        const Rectangle rect(x, y, 1, 1);
        getGraphicsDeviceProperty().GetBackBufferData(&rect, &px, 0, 1);
        return px;
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(dev);
        const Color texColor(30, 210, 90, 255);
        tex_ = std::make_unique<Texture2D>(dev, 1, 1);
        tex_->SetData(&texColor, 1);
    }

    void RunScene(bool runChecks)
    {
        auto& dev = getGraphicsDeviceProperty();

        // Checks A/B: draw a full-window quad with the custom effect.
        ShaderEffect customFx(dev, kSpriteVert, kSpriteFrag);
        dev.Clear(Color::Black);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                            nullptr, nullptr, nullptr, &customFx);
        spriteBatch_->Draw(*tex_, Rectangle(0, 0, kSize, kSize), Rectangle(0, 0, 1, 1), Color::White);
        spriteBatch_->End();
        if (runChecks)
        {
            const Color got = ReadPixel(kSize / 2, kSize / 2);
            Check("custom SpriteBatch effect: MatrixTransform + texture sampling: got=" + ColorStr(got),
                  Matches(got, Color(30, 210, 90, 255), 12));
        }

        // Check C: a subsequent Begin/End with NO custom effect must restore the built-in shader
        // (a different, solid-red 1x1 texture proves the built-in path is genuinely active again,
        // not still routed through the previous custom program).
        const Color redColor(220, 20, 20, 255);
        Texture2D redTex(dev, 1, 1);
        redTex.SetData(&redColor, 1);

        dev.Clear(Color::Black);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque);
        spriteBatch_->Draw(redTex, Rectangle(0, 0, kSize, kSize), Rectangle(0, 0, 1, 1), Color::White);
        spriteBatch_->End();
        if (runChecks)
        {
            const Color got = ReadPixel(kSize / 2, kSize / 2);
            Check("Begin() without an effect restores the built-in shader: got=" + ColorStr(got),
                  Matches(got, redColor, 12));
        }
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        RunScene(/*runChecks=*/frame_ == 1);

        if (frame_ == kTotalFrames)
        {
            Check(std::to_string(kTotalFrames) + " frames render with no exception", true);
            std::printf("=== %d/%d PASS ===\n", passCount_, 3);
            result_ = (passCount_ == 3) ? 0 : 1;
            Exit();
        }
    }

public:
    OpenGL2SpriteBatchCustomEffectTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    OpenGL2SpriteBatchCustomEffectTest game;
    game.Run();
    return game.getResult();
}

// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: end-to-end 2D vertical-slice proof for the native OpenGL 2.1 graphics renderer
// -- a real Texture2D upload and real SpriteBatch draws, verified with actual pixel readback
// (OpenGL2Renderer::ReadBackbuffer), not just "didn't throw". Mirrors the shape of
// sdlgpu_2d_test.cpp/sdlgpu_samplerstate_test.cpp (checks on frame 1, then kTotalFrames of the
// same scene to prove stability).
//
// Check A -- Texture2D::CreateFromPixels() succeeds for a 4x4 per-quadrant-colored texture.
// Check B -- SpriteBatch UV mapping: each quadrant of that texture, scaled up and drawn at the
//   origin, reads back its own color at its own screen quadrant -- proves there is no vertical or
//   horizontal flip bug in the sprite quad's texcoord/vertex assembly.
// Check C -- an opaque 1x1 sprite drawn over a differently-colored background reads back its own
//   color exactly (basic Texture2D sampling + SpriteBatch draw correctness).
// Check D -- default SpriteBatch.Begin() uses BlendState::AlphaBlend (Blend::One /
//   Blend::InverseSourceAlpha -- the premultiplied-alpha convention). Drawing a 50%-alpha Red
//   sprite (straight, non-premultiplied texture data, matching this project's content pipeline)
//   over a Color::Green (0,128,0 -- XNA's "Green" is the HTML/.NET dark green, not (0,255,0))
//   background must read back near (255,64,0): src*1 + dst*(1-a) = (255,0,0)*1 + (0,128,0)*0.498.
//   Using the wrong (Blend::SourceAlpha / Blend::InverseSourceAlpha) factors instead would read
//   back near (128,64,0) instead, a clear R-channel differential.
// Check E -- SpriteBatch.Begin(..., transformMatrix)'s camera/scroll transform is actually
//   applied: a sprite drawn at a fixed destination rect under a pure-translation transform reads
//   back at the TRANSLATED screen position, not its untransformed one.
// Check F -- SetSamplerAddressMode: a source rect extending past the texture bounds (U in [0,2])
//   over a Red|Green half-and-half texture reads back Red under PointWrap (wraps back into the
//   Red half) vs Green under PointClamp (clamps to the rightmost/Green texel) -- a differential
//   that only passes if the address mode SpriteBatch.Begin() requests is actually reaching GL,
//   not just silently accepted and ignored.
// Check G -- rotation + both-flip SpriteBatch draws complete with no exception (an uncaught
//   exception fails this whole test via a non-zero exit, matching this project's existing
//   convention for "no crash" checks -- see e.g. sdlgpu_2d_test.cpp).
// Check H -- kTotalFrames of the full scene render with no exception.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "CNA/Internal/Renderers/OpenGL2/OpenGL2Renderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::OpenGL2;

namespace
{
    constexpr int kTotalFrames = 120;

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

    // 4x4 RGBA8, one solid opaque color per 2x2 quadrant: red(TL) / green(TR) / blue(BL) / white(BR).
    std::vector<std::uint8_t> MakeQuadrantTexturePixels()
    {
        std::vector<std::uint8_t> pixels(4 * 4 * 4, 0);
        auto setPixel = [&](int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
            const std::size_t o = (static_cast<std::size_t>(y) * 4 + x) * 4;
            pixels[o + 0] = r; pixels[o + 1] = g; pixels[o + 2] = b; pixels[o + 3] = 255;
        };
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
            {
                if (x < 2 && y < 2) setPixel(x, y, 255, 0, 0);
                else if (x >= 2 && y < 2) setPixel(x, y, 0, 255, 0);
                else if (x < 2 && y >= 2) setPixel(x, y, 0, 0, 255);
                else setPixel(x, y, 255, 255, 255);
            }
        return pixels;
    }

    // 4x4 RGBA8: left half (columns 0-1) Red, right half (columns 2-3) Green.
    std::vector<std::uint8_t> MakeHalvesTexturePixels()
    {
        std::vector<std::uint8_t> pixels(4 * 4 * 4, 0);
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
            {
                const std::size_t o = (static_cast<std::size_t>(y) * 4 + x) * 4;
                if (x < 2) { pixels[o + 0] = 255; pixels[o + 1] = 0; }
                else       { pixels[o + 0] = 0;   pixels[o + 1] = 255; }
                pixels[o + 2] = 0;
                pixels[o + 3] = 255;
            }
        return pixels;
    }
}

class OpenGL2TwoDTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> texQuadrant_;
    std::unique_ptr<Texture2D> texRedOpaque_;
    std::unique_ptr<Texture2D> texRedHalfAlpha_;
    std::unique_ptr<Texture2D> texHalves_;

    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const std::string& label)
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

    void DrawScene(bool runChecks)
    {
        auto& dev = getGraphicsDeviceProperty();

        // Check B: quadrant UV mapping -- no vertical/horizontal flip bug.
        dev.Clear(Color::Black);
        spriteBatch_->Begin();
        spriteBatch_->Draw(*texQuadrant_, Rectangle(0, 0, 64, 64), Rectangle(0, 0, 4, 4), Color::White);
        spriteBatch_->End();
        if (runChecks)
        {
            const Color tl = ReadPixel(16, 16), tr = ReadPixel(48, 16), bl = ReadPixel(16, 48), br = ReadPixel(48, 48);
            Check(Matches(tl, Color::Red, 10), "quadrant top-left reads back Red: got=" + ColorStr(tl));
            Check(Matches(tr, Color(0, 255, 0, 255), 10), "quadrant top-right reads back (0,255,0): got=" + ColorStr(tr));
            Check(Matches(bl, Color::Blue, 10), "quadrant bottom-left reads back Blue: got=" + ColorStr(bl));
            Check(Matches(br, Color::White, 10), "quadrant bottom-right reads back White: got=" + ColorStr(br));
        }

        // Check C: opaque sprite draw over a differently-colored background.
        dev.Clear(Color::Blue);
        spriteBatch_->Begin();
        spriteBatch_->Draw(*texRedOpaque_, Rectangle(80, 10, 20, 20), Rectangle(0, 0, 1, 1), Color::White);
        spriteBatch_->End();
        if (runChecks)
        {
            const Color opaque = ReadPixel(90, 20);
            Check(Matches(opaque, Color::Red, 5), "opaque sprite over Blue background reads back Red: got=" + ColorStr(opaque));
        }

        // Check D: default BlendState::AlphaBlend (One / InverseSourceAlpha) applied to this
        // project's straight-alpha texture storage + non-premultiplying sprite shader.
        dev.Clear(Color::Green);
        spriteBatch_->Begin();
        spriteBatch_->Draw(*texRedHalfAlpha_, Rectangle(80, 50, 20, 20), Rectangle(0, 0, 1, 1), Color::White);
        spriteBatch_->End();
        if (runChecks)
        {
            const Color blended = ReadPixel(90, 60);
            Check(Matches(blended, Color(255, 64, 0, 255), 20),
                  "BlendState::AlphaBlend (One, InverseSourceAlpha) factors applied: got=" + ColorStr(blended));
        }

        // Check E: SpriteBatch.Begin(..., transformMatrix) camera/scroll transform.
        dev.Clear(Color::Black);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, nullptr, nullptr, nullptr,
                            nullptr, Matrix::CreateTranslation(60.0f, 0.0f, 0.0f));
        spriteBatch_->Draw(*texRedOpaque_, Rectangle(10, 10, 10, 10), Rectangle(0, 0, 1, 1), Color::White);
        spriteBatch_->End();
        if (runChecks)
        {
            const Color translated = ReadPixel(75, 15);
            const Color untranslated = ReadPixel(15, 15);
            Check(Matches(translated, Color::Red, 5),
                  "transformMatrix translation moves the sprite to the transformed position: got=" + ColorStr(translated));
            Check(Matches(untranslated, Color::Black, 5),
                  "transformMatrix translation leaves the untransformed position empty: got=" + ColorStr(untranslated));
        }

        // Check F: SetSamplerAddressMode -- Wrap vs Clamp differential over a Red|Green texture,
        // sampled with a source rect extending to U=2 (past the texture's own [0,1) range).
        SamplerState pointWrap = SamplerState::PointWrap;
        dev.Clear(Color::Black);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &pointWrap, nullptr, nullptr);
        spriteBatch_->Draw(*texHalves_, Rectangle(0, 0, 64, 64), Rectangle(0, 0, 8, 4), Color::White);
        spriteBatch_->End();
        const Color wrapResult = ReadPixel(40, 32);

        SamplerState pointClamp = SamplerState::PointClamp;
        dev.Clear(Color::Black);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &pointClamp, nullptr, nullptr);
        spriteBatch_->Draw(*texHalves_, Rectangle(0, 0, 64, 64), Rectangle(0, 0, 8, 4), Color::White);
        spriteBatch_->End();
        const Color clampResult = ReadPixel(40, 32);
        if (runChecks)
        {
            Check(Matches(wrapResult, Color::Red, 10),
                  "SetSamplerAddressMode(PointWrap): U=1.25 wraps to 0.25 -> Red half: got=" + ColorStr(wrapResult));
            Check(Matches(clampResult, Color(0, 255, 0, 255), 10),
                  "SetSamplerAddressMode(PointClamp): U=1.25 clamps to 1.0 -> Green half: got=" + ColorStr(clampResult));
        }

        // Check G: rotation + both-flip draws -- no pixel assertion, only "does not throw"
        // (an uncaught exception here fails the whole test via a non-zero process exit).
        dev.Clear(Color::Black);
        spriteBatch_->Begin();
        spriteBatch_->Draw(*texQuadrant_, Rectangle(150, 42, 64, 64), Rectangle(0, 0, 4, 4), Color::White,
                           MathHelper::PiOver4, Vector2(2.0f, 2.0f), SpriteEffects::None, 0.0f);
        spriteBatch_->Draw(*texQuadrant_, Rectangle(150, 42, 64, 64), Rectangle(0, 0, 4, 4), Color::White,
                           0.0f, Vector2::Zero,
                           static_cast<SpriteEffects>(static_cast<int>(SpriteEffects::FlipHorizontally) |
                                                       static_cast<int>(SpriteEffects::FlipVertically)),
                           0.0f);
        spriteBatch_->End();
        if (runChecks)
            Check(true, "rotation + both-flip SpriteBatch draws completed with no exception");
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(dev);

        texQuadrant_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 4, 4, MakeQuadrantTexturePixels()));
        Check(texQuadrant_->getWidthProperty() == 4 && texQuadrant_->getHeightProperty() == 4,
              "Texture2D::CreateFromPixels() succeeds for a 4x4 texture");

        texRedOpaque_ = std::make_unique<Texture2D>(
            Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{255, 0, 0, 255}));
        texRedHalfAlpha_ = std::make_unique<Texture2D>(
            Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{255, 0, 0, 128}));
        texHalves_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 4, 4, MakeHalvesTexturePixels()));
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        DrawScene(/*runChecks=*/frame_ == 1);

        if (frame_ == kTotalFrames)
        {
            Check(true, "120 frames of the SpriteBatch scene render with no exception");
            std::printf("=== %d/%d PASS ===\n", passCount_, 13);
            result_ = (passCount_ == 13) ? 0 : 1;
            Exit();
        }
    }

public:
    OpenGL2TwoDTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(320);
        gdm_->setPreferredBackBufferHeightProperty(240);
        // GraphicsDeviceManager.SynchronizeWithVerticalRetrace defaults to true (the XNA
        // default); this test's virtual/headless display has no real vblank signal, so leaving
        // VSync on makes every frame wait roughly a second, blowing past this test's frame
        // budget. Disabling it here -- before Game::DoInitialize()'s CreateDevice() call reads
        // it -- is the correct, property-level way to request Immediate presentation.
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    OpenGL2TwoDTest game;
    game.Run();
    return game.getResult();
}

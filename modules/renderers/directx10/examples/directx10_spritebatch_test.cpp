// SPDX-License-Identifier: MS-PL
// plans/plan_d3d10.md: SpriteBatch tests for the D3D10 renderer's real GPU-quad 2D compositor
// (D3D10SpriteBatchRenderer -- a real vs_4_0/ps_4_0 shader, orthographic screen-to-clip transform,
// real ID3D10BlendState).
//
// Check A -- Draw() before Begin() and End() without Begin() both throw.
// Check B -- Identity draw (BlendState::Opaque) exactly reproduces the source texture.
// Check C -- SpriteEffects::FlipHorizontally swaps sampled texels left-right.
// Check D -- Scale (2x2 texture -> 8x8 dest) samples the correct texel per quadrant.
// Check E -- Rotation by pi around the center origin maps top-left <-> bottom-right.
// Check F -- SetTransformMatrix() (translation) shifts the draw by exactly the offset.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kCanvasSize = 96;
static constexpr float kPi = 3.14159265358979323846f;

namespace
{
    int g_passCount = 0;
    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++g_passCount;
    }
    bool SameColor(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty() && a.getAProperty() == b.getAProperty();
    }
    Color ReadPixel(GraphicsDevice& dev, int x, int y)
    {
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }
}

class D3D10SpriteBatchTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int result_ = 1;
    static constexpr int kTotal = 6;

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        SpriteBatch sb(dev);

        // Check A: Draw()/End() misuse throws.
        {
            bool drawBeforeBeginThrew = false;
            Texture2D tex(dev, 2, 2);
            try { sb.Draw(tex, 0.0f, 0.0f); } catch (const std::exception&) { drawBeforeBeginThrew = true; }
            bool endWithoutBeginThrew = false;
            try { sb.End(); } catch (const std::exception&) { endWithoutBeginThrew = true; }
            check(drawBeforeBeginThrew && endWithoutBeginThrew,
                  "Draw() before Begin() and End() without Begin() both throw");
        }

        dev.Clear(Color(1, 1, 1, 255));

        // Check B: identity draw exactly reproduces the source.
        {
            Texture2D tex(dev, 2, 2);
            std::vector<Color> px = {
                Color(255, 0, 0, 255), Color(0, 255, 0, 255),
                Color(0, 0, 255, 255), Color(255, 255, 0, 255),
            };
            tex.SetData(px.data(), static_cast<int>(px.size()));

            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque);
            sb.Draw(tex, Rectangle(0, 0, 2, 2), Rectangle(0, 0, 2, 2), Color(255, 255, 255, 255));
            sb.End();

            check(SameColor(ReadPixel(dev, 0, 0), Color(255, 0, 0, 255)) &&
                  SameColor(ReadPixel(dev, 1, 0), Color(0, 255, 0, 255)) &&
                  SameColor(ReadPixel(dev, 0, 1), Color(0, 0, 255, 255)) &&
                  SameColor(ReadPixel(dev, 1, 1), Color(255, 255, 0, 255)),
                  "Identity draw (BlendState::Opaque) exactly reproduces the source texture");
        }

        dev.Clear(Color(1, 1, 1, 255));

        // Check C: FlipHorizontally swaps sampled texels.
        {
            Texture2D tex(dev, 2, 1);
            std::vector<Color> px = { Color(255, 0, 0, 255), Color(0, 255, 0, 255) };
            tex.SetData(px.data(), static_cast<int>(px.size()));

            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque);
            sb.Draw(tex, Rectangle(40, 20, 2, 1), Rectangle(0, 0, 2, 1), Color(255, 255, 255, 255),
                    0.0f, Vector2(0.0f, 0.0f), SpriteEffects::FlipHorizontally, 0.0f);
            sb.End();

            check(SameColor(ReadPixel(dev, 40, 20), Color(0, 255, 0, 255)) &&
                  SameColor(ReadPixel(dev, 41, 20), Color(255, 0, 0, 255)),
                  "SpriteEffects::FlipHorizontally swaps sampled texels left-right");
        }

        dev.Clear(Color(1, 1, 1, 255));

        // Check D: scale samples the correct texel per quadrant.
        {
            Texture2D tex(dev, 2, 2);
            std::vector<Color> px = {
                Color(255, 0, 0, 255), Color(0, 255, 0, 255),
                Color(0, 0, 255, 255), Color(255, 255, 0, 255),
            };
            tex.SetData(px.data(), static_cast<int>(px.size()));

            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque);
            sb.Draw(tex, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 2, 2), Color(255, 255, 255, 255));
            sb.End();

            check(SameColor(ReadPixel(dev, 1, 1), Color(255, 0, 0, 255)) &&
                  SameColor(ReadPixel(dev, 6, 1), Color(0, 255, 0, 255)) &&
                  SameColor(ReadPixel(dev, 1, 6), Color(0, 0, 255, 255)) &&
                  SameColor(ReadPixel(dev, 6, 6), Color(255, 255, 0, 255)),
                  "Scale (2x2 texture -> 8x8 dest) samples the correct texel per quadrant");
        }

        dev.Clear(Color(1, 1, 1, 255));

        // Check E: rotation by pi around the texture's center origin. origin=(1,1) is specified
        // in SOURCE-rectangle pixel units (the 2x2 texture's own center), NOT destination units --
        // matches DIRECTX8/DIRECTX1..DIRECTX7's own established rotation-test convention exactly.
        {
            Texture2D tex(dev, 2, 2);
            std::vector<Color> px = {
                Color(255, 0, 0, 255), Color(0, 255, 0, 255),
                Color(0, 0, 255, 255), Color(255, 255, 0, 255),
            };
            tex.SetData(px.data(), static_cast<int>(px.size()));

            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque);
            sb.Draw(tex, Rectangle(60, 40, 8, 8), Rectangle(0, 0, 2, 2), Color(255, 255, 255, 255),
                    kPi, Vector2(1.0f, 1.0f), SpriteEffects::None, 0.0f);
            sb.End();

            // origin=(1,1) is the texture's center, so the drawn quad is centered ON (60,40): the
            // screen bounding box is [56,64]x[36,44]. Un-rotated, texel(0,0)=Red would land in the
            // top-left quadrant and texel(1,1)=Yellow in the bottom-right one; after a pi rotation
            // about that same center, the quadrants swap. Sample points are offset 1px in from
            // each respective bbox corner -- with only a 2x2 source texture and the default
            // bilinear (Linear) sampler, only pixels immediately adjacent to a corner (where Clamp
            // addressing saturates all 4 bilinear taps to the same texel) read back a pure,
            // unblended color.
            check(SameColor(ReadPixel(dev, 62, 42), Color(255, 0, 0, 255)) &&
                  SameColor(ReadPixel(dev, 57, 37), Color(255, 255, 0, 255)),
                  "Rotation by pi around the center origin maps top-left <-> bottom-right");
        }

        // Check F: SetTransformMatrix (translation) shifts the draw by exactly the offset.
        dev.Clear(Color(1, 1, 1, 255));
        {
            Texture2D tex(dev, 1, 1);
            std::vector<Color> px = { Color(200, 50, 25, 255) };
            tex.SetData(px.data(), 1);

            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr, nullptr,
                     Matrix::CreateTranslation(10.0f, 5.0f, 0.0f));
            sb.Draw(tex, 0.0f, 0.0f);
            sb.End();

            check(SameColor(ReadPixel(dev, 10, 5), Color(200, 50, 25, 255)),
                  "SetTransformMatrix() (translation) shifts the draw by exactly the offset");
        }

        std::printf("=== %d/%d PASS ===\n", g_passCount, kTotal);
        result_ = (g_passCount == kTotal) ? 0 : 1;
        Exit();
    }

public:
    D3D10SpriteBatchTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kCanvasSize);
        gdm_->setPreferredBackBufferHeightProperty(kCanvasSize);
    }
    int getResult() const { return result_; }
};

int main()
{
    D3D10SpriteBatchTest game;
    game.Run();
    return game.getResult();
}

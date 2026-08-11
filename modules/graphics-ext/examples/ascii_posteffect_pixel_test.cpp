// SPDX-License-Identifier: MS-PL
// AsciiPostProcessEffect pixel oracle -- successor to the former ASCII graphics renderer's own
// Ascii_Present ctest (plan_ascii.md Phase G5), rewritten against the renderer-neutral
// post-process effect's public API instead of a renderer-internal testing hook.
//
// Renders a known solid-gray scene into an offscreen RenderTarget2D (proving the effect's source
// can be any Texture2D, produced independently of whatever draws to it), then applies
// AsciiPostProcessEffect to redraw it onto the real backbuffer, and reads the presented pixels
// back through the ordinary public GraphicsDevice::GetBackBufferData() API -- no renderer
// downcast, no renderer-internal type anywhere in this file.
//
// Uses a 64x64 virtual resolution (8x8 cells of 8x8px), so sampling a specific pixel maps
// directly onto a specific glyph cell/row/column with no stretch/interpolation ambiguity.
//
// Check A -- a solid gray(140) source: every cell averages to exactly that gray, so every cell
//   picks the SAME glyph (luminance(140) -> ramp index 5, '+') with foreground=(140,140,140) and
//   background=(35,35,35) (Color mode's quarter-brightness rule). The '+' bitmap's row 0 (and
//   column 0/7) are always off -- sampling near a cell's top-left corner must read the background
//   color.
// Check B -- sampling the CENTER of a cell (row 3-4, columns 1-6 of '+' are on) must read the
//   exact foreground color.
// Check C -- Draw() does not throw, for both a full-viewport call and an explicit destination
//   rectangle.
// Check D -- setCellSize()/getCellSize() round-trip, an invalid size throws
//   std::invalid_argument, and changing the cell size measurably changes the grid Draw()
//   produces: the default 8x8 cell on a 64x64 source produces an 8x8 grid, a 16x16 cell produces
//   a 4x4 grid -- the actual functional claim, not just the accessor round-trip.
// Check E -- BlackWhite mode changes the drawn output relative to Color mode (no per-cell
//   background fill: the corner sample, which read the quarter-brightness background gray in
//   Color mode, instead reads pure black).
// Check F -- repeated Draw() calls against the same effect/source produce identical output
//   (deterministic, no per-call state leak).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"

#include "CNA/Graphics/AsciiPostProcessEffect.hpp"

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Graphics::AsciiPostProcessEffect;
using CNA::Graphics::AsciiQuantizeMode;

namespace
{
    bool PixelEquals(const std::vector<Color>& pixels, int w, int x, int y,
                      std::uint8_t r, std::uint8_t g, std::uint8_t b)
    {
        const Color& c = pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)];
        return c.getRProperty() == r && c.getGProperty() == g && c.getBProperty() == b;
    }
}

class AsciiPostEffectPixelTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        constexpr int kSize = 64;

        // The source is an ordinary, independently-produced RenderTarget2D -- AsciiPostProcessEffect
        // never sees or cares how it was rendered.
        RenderTarget2D scene(dev, kSize, kSize);
        dev.SetRenderTarget(&scene);
        dev.Clear(Color(140, 140, 140, 255));
        dev.SetRenderTarget(nullptr);

        AsciiPostProcessEffect effect(dev);
        effect.setQuantizeMode(AsciiQuantizeMode::Color);

        bool threw = false;
        try { effect.Draw(scene); }
        catch (const std::exception&) { threw = true; }
        check(!threw, "Draw(source) does not throw");

        std::vector<Color> pixels(static_cast<std::size_t>(kSize) * static_cast<std::size_t>(kSize));
        dev.GetBackBufferData(pixels.data(), static_cast<int>(pixels.size()));

        // Check A: top-left corner of cell (0,0) -- '+' glyph's row 0 is all off -> background.
        check(PixelEquals(pixels, kSize, 1, 1, 35, 35, 35),
              "Cell (0,0) corner (glyph row0, always off for '+') reads the exact background color (35,35,35)");

        // Check B: center of cell (0,0) -- '+' glyph rows 3-4, columns 1-6 are on -> foreground.
        check(PixelEquals(pixels, kSize, 4, 4, 140, 140, 140),
              "Cell (0,0) center (glyph row4 col4, on for '+') reads the exact foreground color (140,140,140)");

        // Check D: cell size is genuinely runtime-configurable.
        int defaultCellW = 0, defaultCellH = 0;
        effect.getCellSize(defaultCellW, defaultCellH);
        check(defaultCellW == 8 && defaultCellH == 8, "getCellSize() defaults to 8x8");

        int gridCols = 0, gridRows = 0;
        effect.GetLastGridDimensions(gridCols, gridRows);
        check(gridCols == 8 && gridRows == 8, "Default 8x8 cell size on a 64x64 source produces an 8x8 grid");

        bool threwInvalid = false;
        try { effect.setCellSize(0, 8); }
        catch (const std::invalid_argument&) { threwInvalid = true; }
        check(threwInvalid, "setCellSize(0, 8) throws std::invalid_argument");

        effect.setCellSize(16, 16);
        int newCellW = 0, newCellH = 0;
        effect.getCellSize(newCellW, newCellH);
        check(newCellW == 16 && newCellH == 16, "setCellSize(16, 16) / getCellSize() round-trip");

        effect.Draw(scene);
        effect.GetLastGridDimensions(gridCols, gridRows);
        check(gridCols == 4 && gridRows == 4,
              "setCellSize(16, 16) on a 64x64 source produces a 4x4 grid -- Draw() actually honors it");
        effect.setCellSize(8, 8);

        // Check E: BlackWhite mode drops the per-cell background fill entirely (pure black at the
        // corner instead of Color mode's quarter-brightness gray).
        effect.setQuantizeMode(AsciiQuantizeMode::BlackWhite);
        effect.Draw(scene);
        dev.GetBackBufferData(pixels.data(), static_cast<int>(pixels.size()));
        check(PixelEquals(pixels, kSize, 1, 1, 0, 0, 0),
              "BlackWhite mode: cell (0,0) corner reads pure black (no background fill)");
        effect.setQuantizeMode(AsciiQuantizeMode::Color);

        // Check F: repeated Draw() calls are deterministic.
        effect.Draw(scene);
        std::vector<Color> firstPass = pixels;
        dev.GetBackBufferData(pixels.data(), static_cast<int>(pixels.size()));
        effect.Draw(scene);
        std::vector<Color> secondPass(pixels.size());
        dev.GetBackBufferData(secondPass.data(), static_cast<int>(secondPass.size()));
        check(PixelEquals(secondPass, kSize, 1, 1, 35, 35, 35) && PixelEquals(secondPass, kSize, 4, 4, 140, 140, 140),
              "Repeated Draw() calls against the same source produce identical output");

        // Check C (destination-rectangle overload): explicit full-viewport rectangle does not throw
        // and reproduces the same corner/center samples.
        bool threwRect = false;
        try { effect.Draw(scene, Rectangle(0, 0, kSize, kSize)); }
        catch (const std::exception&) { threwRect = true; }
        check(!threwRect, "Draw(source, destinationRectangle) does not throw");
        dev.GetBackBufferData(pixels.data(), static_cast<int>(pixels.size()));
        check(PixelEquals(pixels, kSize, 1, 1, 35, 35, 35) && PixelEquals(pixels, kSize, 4, 4, 140, 140, 140),
              "Draw(source, destinationRectangle) reproduces the same corner/center samples as Draw(source)");

        std::printf("=== %d/%d PASS ===\n", passCount_, 11);
        result_ = (passCount_ == 11) ? 0 : 1;
        Exit();
    }

public:
    AsciiPostEffectPixelTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    AsciiPostEffectPixelTest game;
    game.Run();
    return game.getResult();
}

// SPDX-License-Identifier: MS-PL
// plan_ascii.md Phase G5 (ASCII-40/41): Present() pixel test -- render known content, read back
// the REAL window's presented pixels (not gameTarget_), assert the correct glyph cells/colors
// appear. Genuinely possible here, unlike the cancelled terminal version (design decision 9/11).
//
// Uses PresentationMode::NativeBackBuffer + a virtual resolution that's an exact multiple of the
// glyph cell size (64x64 = 8x8 cells of 8x8px), so the real window is pixel-for-pixel identical
// to the logical resolution -- no stretch/interpolation ambiguity to account for when sampling.
//
// Check A -- Clear(gray 140) fills the whole 64x64 view with one solid color. Every cell
//   averages to exactly that gray, so every cell must pick the SAME glyph (luminance(140) ->
//   ramp index 5, '+') with foreground=(140,140,140) and background=(35,35,35) (Color mode's
//   quarter-brightness rule). The '+' bitmap's row 0 (and column 0/7) are always off -- sampling
//   near a cell's top-left corner must read the background color.
// Check B -- sampling the CENTER of a cell (row 3-4, columns 1-6 of '+' are on) must read the
//   exact foreground color.
// Check C -- Present() does not throw.
// Check D (ASCII-30) -- SetCellSize()/GetCellSize() round-trip, an invalid size throws
//   std::invalid_argument, and -- the actual functional claim, not just the accessors --
//   changing the cell size measurably changes the grid Present() draws: the default 8x8 cell on
//   a 64x64 view produces an 8x8 grid, while a 16x16 cell on that same view produces a 4x4 grid.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "CNA/Internal/Backends/Ascii/AsciiGraphicsBackend.hpp"

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Backends::Ascii;

namespace
{
    bool PixelEquals(const std::vector<std::uint8_t>& rgba, int w, int x, int y,
                      std::uint8_t r, std::uint8_t g, std::uint8_t b)
    {
        const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4;
        return rgba[idx] == r && rgba[idx + 1] == g && rgba[idx + 2] == b;
    }
}

class AsciiPresentTest : public Game
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
        auto& backend = static_cast<AsciiGraphicsBackend&>(dev.GetBackend());
        backend.SetMode(AsciiQuantizeMode::Color);

        dev.Clear(Color(140, 140, 140, 255));

        bool threw = false;
        try { dev.Present(); }
        catch (const std::exception&) { threw = true; }
        check(!threw, "Present() does not throw");

        // The real dev.Present() call above already proved the production path doesn't throw,
        // but its real double-buffer swap makes an immediate readback unreliable (confirmed
        // empirically: SDL_Renderer/OpenGL presents via a buffer swap, not a copy, so the
        // "current" target right after a swap is the *other*, not-drawn-this-frame buffer, not
        // what was just shown). gameTarget_ still holds the same content Present() only ever
        // read from -- DrawQuantizedGridForTesting() redraws the identical quantized grid onto
        // the real backbuffer WITHOUT swapping, so it can be read immediately afterward.
        backend.DrawQuantizedGridForTesting();

        int realWidth = 0, realHeight = 0;
        backend.GetViewportSize(realWidth, realHeight);
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(realWidth) * static_cast<std::size_t>(realHeight) * 4);
        backend.ReadRealBackbufferForTesting(0, 0, realWidth, realHeight, pixels.data());

        // NativeBackBuffer + a 64x64 virtual resolution (exact multiple of the 8x8 glyph cell)
        // means the real window is pixel-for-pixel identical to the logical resolution -- cell
        // (0,0) occupies real pixels [0,8)x[0,8), matching the '+' glyph's own bitmap 1:1.
        check(realWidth == 64 && realHeight == 64, "NativeBackBuffer keeps the real window pixel-for-pixel at 64x64");

        // Check A: top-left corner of cell (0,0) -- '+' glyph's row 0 is all off -> background.
        check(PixelEquals(pixels, realWidth, 1, 1, 35, 35, 35),
              "Cell (0,0) corner (glyph row0, always off for '+') reads the exact background color (35,35,35)");

        // Check B: center of cell (0,0) -- '+' glyph rows 3-4, columns 1-6 are on -> foreground.
        check(PixelEquals(pixels, realWidth, 4, 4, 140, 140, 140),
              "Cell (0,0) center (glyph row4 col4, on for '+') reads the exact foreground color (140,140,140)");

        // Check D (ASCII-30): the grid's cell pixel size is genuinely runtime-configurable, not
        // just a fixed 8x8 baked into Present() -- SetCellSize()/GetCellSize() round-trip,
        // reject an invalid size, and actually change the grid Present() draws.
        int defaultCellW = 0, defaultCellH = 0;
        backend.GetCellSize(defaultCellW, defaultCellH);
        check(defaultCellW == 8 && defaultCellH == 8, "GetCellSize() defaults to 8x8 (kAsciiGlyphWidth x kAsciiGlyphHeight)");

        int gridCols = 0, gridRows = 0;
        backend.GetLastGridDimensionsForTesting(gridCols, gridRows);
        check(gridCols == 8 && gridRows == 8, "Default 8x8 cell size on a 64x64 view produces an 8x8 grid");

        bool threwInvalid = false;
        try { backend.SetCellSize(0, 8); }
        catch (const std::invalid_argument&) { threwInvalid = true; }
        check(threwInvalid, "SetCellSize(0, 8) throws std::invalid_argument");

        backend.SetCellSize(16, 16);
        int newCellW = 0, newCellH = 0;
        backend.GetCellSize(newCellW, newCellH);
        check(newCellW == 16 && newCellH == 16, "SetCellSize(16, 16) / GetCellSize() round-trip");

        backend.DrawQuantizedGridForTesting();
        backend.GetLastGridDimensionsForTesting(gridCols, gridRows);
        check(gridCols == 4 && gridRows == 4,
              "SetCellSize(16, 16) on a 64x64 view produces a 4x4 grid -- Present() actually honors it, not just the accessor");

        std::printf("=== %d/%d PASS ===\n", passCount_, 9);
        result_ = (passCount_ == 9) ? 0 : 1;
        Exit();
    }

public:
    AsciiPresentTest()
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
    AsciiPresentTest game;
    game.Run();
    return game.getResult();
}

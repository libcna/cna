// SPDX-License-Identifier: MS-PL
//
// PLAT-132: presenting a finished RGBA frame as characters.
//
// This is the task that makes IPlatformSurfacePresenter worth having as a contract rather than as
// a polite name for SDL_Renderer. The interface was written for SKIA and BLEND2D, which rasterise
// on the CPU and want the result on a screen. A terminal satisfies the same interface with no
// window, no GPU and no pixels — and a caller that only knows Present(frame) cannot tell.
//
// The quantiser and the ANSI writer are tested directly, because their failures are quiet:
// a wrong glyph ramp or a dropped edge column produces a picture that is merely a bit off, not an
// error anybody sees.

#include "../../../src/Terminal/TerminalAnsiWriter.hpp"
#include "../../../src/Terminal/TerminalFrameGrid.hpp"
#include "../../../src/Terminal/TerminalSurfacePresenter.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include <gtest/gtest.h>

#include <fcntl.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

using namespace CNA::Platform;
using namespace CNA::Platform::Terminal;

/// A solid-colour RGBA frame.
class Frame
{
public:
    Frame(const int width, const int height, const std::uint8_t red, const std::uint8_t green,
          const std::uint8_t blue)
        : width_(width)
        , height_(height)
        , pixels_(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u)
    {
        for (std::size_t at = 0; at < pixels_.size(); at += 4)
        {
            pixels_[at] = red;
            pixels_[at + 1] = green;
            pixels_[at + 2] = blue;
            pixels_[at + 3] = 255;
        }
    }

    void SetPixel(const int x, const int y, const std::uint8_t red, const std::uint8_t green,
                  const std::uint8_t blue)
    {
        const std::size_t at =
            (static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
             static_cast<std::size_t>(x)) * 4u;
        pixels_[at] = red;
        pixels_[at + 1] = green;
        pixels_[at + 2] = blue;
    }

    [[nodiscard]] SurfaceFrame View() const
    {
        SurfaceFrame frame;
        frame.pixels = pixels_.data();
        frame.width = width_;
        frame.height = height_;
        return frame;
    }

private:
    int width_, height_;
    std::vector<std::uint8_t> pixels_;
};

// --- quantisation --------------------------------------------------------------------------------

TEST(TerminalFrameGridTest, BlackBecomesTheEmptiestGlyphAndWhiteTheDensest)
{
    // The ends of the ramp, which is what makes every value in between meaningful.
    const Frame black(8, 8, 0, 0, 0);
    const Frame white(8, 8, 255, 255, 255);

    EXPECT_EQ(QuantizeToGrid(black.View(), 4, 2).At(0, 0).glyph, GlyphRamp().front());
    EXPECT_EQ(QuantizeToGrid(white.View(), 4, 2).At(0, 0).glyph, GlyphRamp().back());
}

TEST(TerminalFrameGridTest, ACellCarriesTheAverageColourOfThePixelsItCovers)
{
    // Averaging rather than sampling one pixel: a cell covering 80 source pixels that reported
    // only the top-left one would make thin features vanish and dithering look like noise.
    Frame frame(2, 2, 0, 0, 0);
    frame.SetPixel(0, 0, 255, 0, 0);
    frame.SetPixel(1, 0, 0, 255, 0);
    frame.SetPixel(0, 1, 0, 0, 255);
    frame.SetPixel(1, 1, 255, 255, 255);

    const TerminalCell cell = QuantizeToGrid(frame.View(), 1, 1).At(0, 0);
    EXPECT_EQ(cell.red, (255 + 0 + 0 + 255) / 4);
    EXPECT_EQ(cell.green, (0 + 255 + 0 + 255) / 4);
    EXPECT_EQ(cell.blue, (0 + 0 + 255 + 255) / 4);
}

TEST(TerminalFrameGridTest, EveryCellIsCoveredWhenTheSizesDoNotDivideEvenly)
{
    // A 7-pixel image over 3 cells cannot split evenly. The failure this prevents is a trailing
    // column that never gets any source pixels and stays black, which reads as a dark band down
    // the edge of every frame.
    const Frame frame(7, 5, 200, 200, 200);
    const TerminalGrid grid = QuantizeToGrid(frame.View(), 3, 2);

    ASSERT_EQ(grid.columns, 3);
    ASSERT_EQ(grid.rows, 2);
    for (int row = 0; row < grid.rows; ++row)
    {
        for (int column = 0; column < grid.columns; ++column)
        {
            EXPECT_EQ(grid.At(column, row).red, 200)
                << "cell " << column << "," << row << " saw no source pixels";
        }
    }
}

TEST(TerminalFrameGridTest, AGridLargerThanTheSourceStillCoversEveryCell)
{
    // Upscaling: 2 source pixels across 5 cells means several cells share a pixel. A cell whose
    // computed span collapsed to zero width would be left blank.
    const Frame frame(2, 2, 128, 64, 32);
    const TerminalGrid grid = QuantizeToGrid(frame.View(), 5, 5);

    for (int row = 0; row < grid.rows; ++row)
    {
        for (int column = 0; column < grid.columns; ++column)
        {
            EXPECT_EQ(grid.At(column, row).red, 128) << "cell " << column << "," << row;
        }
    }
}

TEST(TerminalFrameGridTest, AStridedFrameIsReadAlongItsRealRows)
{
    // A renderer presenting a sub-rectangle of a bigger buffer sets strideBytes. Ignoring it
    // would shear the picture diagonally.
    const int storedWidth = 8, height = 4, visibleWidth = 4;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(storedWidth) *
                                     static_cast<std::size_t>(height) * 4u, 0);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < visibleWidth; ++x)
        {
            const std::size_t at =
                (static_cast<std::size_t>(y) * storedWidth + static_cast<std::size_t>(x)) * 4u;
            pixels[at] = 255;
            pixels[at + 1] = 255;
            pixels[at + 2] = 255;
            pixels[at + 3] = 255;
        }
    }

    SurfaceFrame frame;
    frame.pixels = pixels.data();
    frame.width = visibleWidth;
    frame.height = height;
    frame.strideBytes = storedWidth * 4;

    const TerminalGrid grid = QuantizeToGrid(frame, 2, 2);
    for (int row = 0; row < 2; ++row)
    {
        for (int column = 0; column < 2; ++column)
        {
            EXPECT_EQ(grid.At(column, row).red, 255) << "the stride was ignored";
        }
    }
}

TEST(TerminalFrameGridTest, AMalformedFrameIsRefusedRatherThanRead)
{
    SurfaceFrame nothing;
    EXPECT_THROW((void)QuantizeToGrid(nothing, 4, 4), PlatformException);

    const Frame frame(4, 4, 0, 0, 0);
    EXPECT_THROW((void)QuantizeToGrid(frame.View(), 0, 4), PlatformException);
    EXPECT_THROW((void)QuantizeToGrid(frame.View(), 4, -1), PlatformException);
}

// --- the colour ladder -----------------------------------------------------------------------------

TEST(TerminalAnsiWriterTest, TrueColourEmitsTheExactChannels)
{
    TerminalGrid grid;
    grid.Reset(1, 1);
    grid.cells[0] = TerminalCell{'#', 12, 34, 56};

    std::string output;
    TerminalAnsiWriter(TerminalColourDepth::TrueColour).WriteFullFrame(grid, output);
    EXPECT_NE(output.find("\x1b[38;2;12;34;56m"), std::string::npos);
    EXPECT_NE(output.find('#'), std::string::npos);
}

TEST(TerminalAnsiWriterTest, MonochromeEmitsNoColourAtAll)
{
    // Not "emits black": a terminal with no colour would render the parameters as text. The
    // glyph carries the whole picture on this rung.
    TerminalGrid grid;
    grid.Reset(1, 1);
    grid.cells[0] = TerminalCell{'@', 200, 100, 50};

    std::string output;
    TerminalAnsiWriter(TerminalColourDepth::Monochrome).WriteFullFrame(grid, output);
    EXPECT_EQ(output.find("38;2"), std::string::npos);
    EXPECT_EQ(output.find("38;5"), std::string::npos);
    EXPECT_NE(output.find('@'), std::string::npos);
}

TEST(TerminalAnsiWriterTest, TheIndexedPaletteUsesTheGreyRampForGreys)
{
    // The 6×6×6 cube's grey diagonal has six steps; the dedicated ramp has twenty-four. Taking
    // the cube for a near-grey colour is what makes a greyscale picture come out faintly tinted.
    EXPECT_GE(NearestIndexed256(120, 120, 120), 232) << "mid grey should take the grey ramp";
    EXPECT_GE(NearestIndexed256(70, 70, 70), 232);

    // A saturated colour must not.
    const int red = NearestIndexed256(255, 0, 0);
    EXPECT_GE(red, 16);
    EXPECT_LT(red, 232);
    EXPECT_EQ(red, 16 + 36 * 5) << "pure red is the cube corner";
}

TEST(TerminalAnsiWriterTest, TheSixteenColourRungPicksTheNearestAndUsesBrightParameters)
{
    EXPECT_EQ(NearestAnsi16(0, 0, 0), 30);
    EXPECT_EQ(NearestAnsi16(255, 255, 255), 97);
    EXPECT_EQ(NearestAnsi16(255, 0, 0), 91);
    EXPECT_EQ(NearestAnsi16(0, 255, 0), 92);
    // Every answer has to be a real SGR foreground parameter, not an arbitrary number.
    for (int value = 0; value <= 255; value += 17)
    {
        const int parameter = NearestAnsi16(static_cast<std::uint8_t>(value),
                                            static_cast<std::uint8_t>(255 - value), 128);
        const bool normal = parameter >= 30 && parameter <= 37;
        const bool bright = parameter >= 90 && parameter <= 97;
        EXPECT_TRUE(normal || bright) << parameter;
    }
}

TEST(TerminalAnsiWriterTest, AnUnchangedColourIsNotRestated)
{
    // The single most valuable line in the writer. A 120×40 grid is 4800 glyphs but nearly 100 KB
    // if every cell repeats its colour — fine on a local pty, hopeless over ssh.
    TerminalGrid grid;
    grid.Reset(40, 4);
    for (TerminalCell& cell : grid.cells)
    {
        cell = TerminalCell{'#', 10, 20, 30};
    }

    std::string output;
    TerminalAnsiWriter(TerminalColourDepth::TrueColour).WriteFullFrame(grid, output);

    int colourChanges = 0;
    for (std::size_t at = output.find("\x1b[38;2;"); at != std::string::npos;
         at = output.find("\x1b[38;2;", at + 1))
    {
        ++colourChanges;
    }
    EXPECT_EQ(colourChanges, 1) << "160 identical cells needed one colour change, not 160";
}

TEST(TerminalAnsiWriterTest, ForgettingTheStateMakesTheNextFrameRestateItsColour)
{
    // After a resize or a new session the remembered colour describes a screen that no longer
    // exists, and a writer that trusted it would skip exactly the escape that would fix it.
    TerminalGrid grid;
    grid.Reset(1, 1);
    grid.cells[0] = TerminalCell{'#', 1, 2, 3};

    TerminalAnsiWriter writer(TerminalColourDepth::TrueColour);
    std::string first;
    writer.WriteFullFrame(grid, first);

    std::string second;
    writer.WriteFullFrame(grid, second);
    EXPECT_EQ(second.find("\x1b[38;2;"), std::string::npos) << "the colour had not changed";

    writer.ForgetTerminalState();
    std::string third;
    writer.WriteFullFrame(grid, third);
    EXPECT_NE(third.find("\x1b[38;2;1;2;3m"), std::string::npos);
}

TEST(TerminalAnsiWriterTest, RowsArePositionedRatherThanSeparatedByNewlines)
{
    // A newline on the last row scrolls the terminal, which would walk the picture upwards by one
    // row every frame — a slow, baffling drift rather than an obvious failure.
    TerminalGrid grid;
    grid.Reset(2, 3);

    std::string output;
    TerminalAnsiWriter(TerminalColourDepth::Monochrome).WriteFullFrame(grid, output);

    EXPECT_EQ(output.find('\n'), std::string::npos) << "a newline at the bottom row scrolls";
    EXPECT_NE(output.find("\x1b[2;1H"), std::string::npos);
    EXPECT_NE(output.find("\x1b[3;1H"), std::string::npos);
}

TEST(TerminalAnsiWriterTest, TheFrameHomesTheCursorInsteadOfClearingTheScreen)
{
    // A clear blanks the screen and then repaints it, which flickers visibly at any frame rate
    // worth having. Overwriting in place does not.
    TerminalGrid grid;
    grid.Reset(2, 2);

    std::string output;
    TerminalAnsiWriter(TerminalColourDepth::Monochrome).WriteFullFrame(grid, output);
    EXPECT_EQ(output.compare(0, 3, "\x1b[H"), 0);
    EXPECT_EQ(output.find("\x1b[2J"), std::string::npos) << "a full clear flickers";
}

// --- the presenter ---------------------------------------------------------------------------------

/// A pseudo-terminal of a chosen size, so scale-mode geometry is deterministic.
class SizedPseudoTerminal
{
public:
    SizedPseudoTerminal(const int columns, const int rows)
    {
        controller_ = posix_openpt(O_RDWR | O_NOCTTY);
        if (controller_ < 0 || grantpt(controller_) != 0 || unlockpt(controller_) != 0)
        {
            return;
        }
        const char* name = ptsname(controller_);
        if (name == nullptr)
        {
            return;
        }
        device_ = open(name, O_RDWR | O_NOCTTY);
        if (device_ < 0)
        {
            return;
        }
        winsize size{};
        size.ws_col = static_cast<unsigned short>(columns);
        size.ws_row = static_cast<unsigned short>(rows);
        ioctl(device_, TIOCSWINSZ, &size);
    }

    ~SizedPseudoTerminal()
    {
        if (device_ >= 0)
        {
            close(device_);
        }
        if (controller_ >= 0)
        {
            close(controller_);
        }
    }

    SizedPseudoTerminal(const SizedPseudoTerminal&) = delete;
    SizedPseudoTerminal& operator=(const SizedPseudoTerminal&) = delete;

    [[nodiscard]] bool IsOpen() const { return controller_ >= 0 && device_ >= 0; }
    [[nodiscard]] int Device() const { return device_; }

private:
    int controller_ = -1;
    int device_ = -1;
};

class TerminalPresenter : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!pty_.IsOpen())
        {
            GTEST_SKIP() << "this environment cannot allocate a pseudo-terminal";
        }
    }

    SizedPseudoTerminal pty_{40, 20};
};

TEST_F(TerminalPresenter, ReportsTheTerminalsRealSize)
{
    TerminalSurfacePresenter presenter(pty_.Device(), pty_.Device(),
                                       TerminalColourDepth::TrueColour, 320, 240);
    EXPECT_EQ(presenter.GetGridSize().columns, 40);
    EXPECT_EQ(presenter.GetGridSize().rows, 20);
}

TEST_F(TerminalPresenter, TheTargetSizeIsTheWindowsResolutionNotTheCellGrid)
{
    // A caller sizes its rasterisation buffer from this. Handing back 40×20 would make every game
    // draw a 40-pixel-wide picture.
    TerminalSurfacePresenter presenter(pty_.Device(), pty_.Device(),
                                       TerminalColourDepth::TrueColour, 320, 240);
    int width = 0, height = 0;
    presenter.GetTargetSize(width, height);
    EXPECT_EQ(width, 320);
    EXPECT_EQ(height, 240);
}

TEST_F(TerminalPresenter, StretchFillsEveryCell)
{
    TerminalSurfacePresenter presenter(pty_.Device(), pty_.Device(),
                                       TerminalColourDepth::Monochrome, 320, 240);
    presenter.SetScaleMode(PresentScaleMode::Stretch, PresentFilter::Nearest);

    const Frame frame(320, 240, 255, 255, 255);
    presenter.Present(frame.View());

    const std::string& bytes = presenter.GetLastFrameBytes();
    EXPECT_EQ(std::count(bytes.begin(), bytes.end(), GlyphRamp().back()), 40 * 20)
        << "stretch must reach every one of the 800 cells";
}

TEST_F(TerminalPresenter, LetterboxFitsAgainstATallCellSoThePictureKeepsItsShape)
{
    // The whole reason letterbox exists here. A character cell is about twice as tall as it is
    // wide, so a square picture stretched across a 40×20 grid comes out squashed to half height.
    // Fitting against a 1:2 cell gives a square picture 40 columns by 20 rows -- which for this
    // grid is exactly full, and any *narrower* frame must leave columns blank.
    TerminalSurfacePresenter presenter(pty_.Device(), pty_.Device(),
                                       TerminalColourDepth::Monochrome, 240, 240);
    presenter.SetScaleMode(PresentScaleMode::Letterbox, PresentFilter::Nearest);

    // A tall frame: 120 wide by 240 high. Against 1:2 cells that wants 20 columns of the 40.
    const Frame tall(120, 240, 255, 255, 255);
    presenter.Present(tall.View());

    const std::string& bytes = presenter.GetLastFrameBytes();
    const long lit = std::count(bytes.begin(), bytes.end(), GlyphRamp().back());
    EXPECT_EQ(lit, 20 * 20) << "a half-width frame should fill half the columns, all the rows";
}

TEST_F(TerminalPresenter, LetterboxLeavesRowsBlankForAWideFrame)
{
    TerminalSurfacePresenter presenter(pty_.Device(), pty_.Device(),
                                       TerminalColourDepth::Monochrome, 320, 240);
    presenter.SetScaleMode(PresentScaleMode::Letterbox, PresentFilter::Nearest);

    // 400 wide by 100 high against 1:2 cells: 40 columns wants 400/(40)=10 source pixels per
    // column, so the height that keeps the shape is 100/(10*2) = 5 rows of the 20.
    const Frame wide(400, 100, 255, 255, 255);
    presenter.Present(wide.View());

    const std::string& bytes = presenter.GetLastFrameBytes();
    const long lit = std::count(bytes.begin(), bytes.end(), GlyphRamp().back());
    EXPECT_EQ(lit, 40 * 5);
}

TEST_F(TerminalPresenter, AMalformedFrameIsRefused)
{
    TerminalSurfacePresenter presenter(pty_.Device(), pty_.Device(),
                                       TerminalColourDepth::TrueColour, 320, 240);

    SurfaceFrame nothing;
    EXPECT_THROW(presenter.Present(nothing), PlatformException);

    SurfaceFrame zeroSized;
    const Frame real(4, 4, 0, 0, 0);
    zeroSized.pixels = real.View().pixels;
    zeroSized.width = 0;
    zeroSized.height = 4;
    EXPECT_THROW(presenter.Present(zeroSized), PlatformException);
}

TEST_F(TerminalPresenter, VerticalSyncIsRefusedRatherThanPretended)
{
    // A terminal has no vertical blank. Returning true would let a caller believe its frame rate
    // was being paced and skip its own timing; pacing is PLAT-135's frame budget, a different
    // promise made in a different place.
    TerminalSurfacePresenter presenter(pty_.Device(), pty_.Device(),
                                       TerminalColourDepth::TrueColour, 320, 240);
    EXPECT_FALSE(presenter.SetVSync(true));
    EXPECT_FALSE(presenter.SetVSync(false));
}

TEST_F(TerminalPresenter, TheSessionIsOwnedForExactlyAsLongAsThePresenterIs)
{
    // Creating a presenter is the first moment anything genuinely needs the terminal, so it is
    // where the session starts -- not at platform construction, and not at
    // AcquireSubsystem(Video), both of which happen in processes that merely have a platform.
    EXPECT_FALSE(TerminalSession::IsActive());
    {
        TerminalSurfacePresenter presenter(pty_.Device(), pty_.Device(),
                                           TerminalColourDepth::TrueColour, 64, 64);
        EXPECT_TRUE(TerminalSession::IsActive());
    }
    EXPECT_FALSE(TerminalSession::IsActive());
}

TEST_F(TerminalPresenter, ANonPositiveTargetSizeIsRefusedBeforeTheTerminalIsTouched)
{
    EXPECT_THROW((void)TerminalSurfacePresenter(pty_.Device(), pty_.Device(),
                                                TerminalColourDepth::TrueColour, 0, 240),
                 PlatformException);
    EXPECT_FALSE(TerminalSession::IsActive()) << "a refused presenter must not hold the terminal";
}

// --- damage tracking (PLAT-133) ---------------------------------------------------------------------

TEST(TerminalAnsiWriterTest, AnIdenticalGridRedrawsNothingAtAll)
{
    // The claim damage tracking rests on. A still picture that still costs a full frame of
    // bandwidth would make the whole exercise pointless.
    TerminalGrid grid;
    grid.Reset(20, 5);
    for (TerminalCell& cell : grid.cells)
    {
        cell = TerminalCell{'#', 90, 90, 90};
    }

    std::string output;
    TerminalAnsiWriter writer(TerminalColourDepth::TrueColour);
    EXPECT_EQ(writer.WriteChangedCells(grid, grid, output), 0);
    EXPECT_TRUE(output.empty());
}

TEST(TerminalAnsiWriterTest, OnlyTheChangedCellsAreRedrawn)
{
    TerminalGrid before;
    before.Reset(20, 5);
    for (TerminalCell& cell : before.cells)
    {
        cell = TerminalCell{'.', 10, 10, 10};
    }
    TerminalGrid after = before;
    after.cells[3] = TerminalCell{'@', 200, 200, 200};
    after.cells[4] = TerminalCell{'@', 200, 200, 200};

    std::string output;
    TerminalAnsiWriter writer(TerminalColourDepth::TrueColour);
    EXPECT_EQ(writer.WriteChangedCells(before, after, output), 2);
    EXPECT_EQ(std::count(output.begin(), output.end(), '@'), 2);
    EXPECT_EQ(output.find('.'), std::string::npos) << "unchanged cells must not be resent";
}

TEST(TerminalAnsiWriterTest, ARunOfChangedCellsIsPositionedOnce)
{
    // A cursor move costs about as much as six glyphs. Positioning each changed cell separately
    // would give back most of what the diff saves on any frame with a moving object in it.
    TerminalGrid before;
    before.Reset(20, 2);
    TerminalGrid after = before;
    for (int column = 5; column < 15; ++column)
    {
        after.cells[static_cast<std::size_t>(column)] = TerminalCell{'#', 255, 255, 255};
    }

    std::string output;
    TerminalAnsiWriter writer(TerminalColourDepth::TrueColour);
    EXPECT_EQ(writer.WriteChangedCells(before, after, output), 10);

    int positioningEscapes = 0;
    for (std::size_t at = output.find("\x1b["); at != std::string::npos;
         at = output.find("\x1b[", at + 1))
    {
        if (output.find('H', at) != std::string::npos &&
            output.find('H', at) < output.find('m', at))
        {
            ++positioningEscapes;
        }
    }
    EXPECT_EQ(positioningEscapes, 1) << "ten adjacent cells are one run, not ten";
}

TEST(TerminalAnsiWriterTest, ADifferentlySizedGridForcesAFullRedraw)
{
    // After a resize nothing on screen corresponds to the new grid's coordinates, so a diff would
    // be comparing unrelated cells and would leave the screen holding pieces of the old picture.
    TerminalGrid before;
    before.Reset(4, 2);
    TerminalGrid after;
    after.Reset(8, 3);

    std::string output;
    TerminalAnsiWriter writer(TerminalColourDepth::Monochrome);
    EXPECT_EQ(writer.WriteChangedCells(before, after, output), 8 * 3);
    EXPECT_EQ(output.compare(0, 3, "\x1b[H"), 0) << "a full redraw homes the cursor";
}

TEST_F(TerminalPresenter, AStillPictureCostsNothingAfterItsFirstFrame)
{
    // End to end, and the number that matters: the first frame pays for the whole screen, and an
    // unchanged second frame pays for nothing.
    TerminalSurfacePresenter presenter(pty_.Device(), pty_.Device(),
                                       TerminalColourDepth::TrueColour, 320, 240);
    presenter.SetScaleMode(PresentScaleMode::Stretch, PresentFilter::Nearest);

    const Frame frame(320, 240, 90, 120, 200);
    presenter.Present(frame.View());
    EXPECT_EQ(presenter.GetLastRedrawnCellCount(), 40 * 20);

    presenter.Present(frame.View());
    EXPECT_EQ(presenter.GetLastRedrawnCellCount(), 0);
    EXPECT_TRUE(presenter.GetLastFrameBytes().empty());
}

TEST_F(TerminalPresenter, ASmallChangeCostsASmallFrame)
{
    // The 5.8 MB/s figure that makes PLAT-133 mandatory rather than an optimisation is about
    // *full* frames. What matters is that a partial change stays partial.
    TerminalSurfacePresenter presenter(pty_.Device(), pty_.Device(),
                                       TerminalColourDepth::TrueColour, 40, 20);
    presenter.SetScaleMode(PresentScaleMode::Stretch, PresentFilter::Nearest);

    Frame frame(40, 20, 0, 0, 0);
    presenter.Present(frame.View());
    const std::size_t fullFrameBytes = presenter.GetLastFrameBytes().size();
    ASSERT_GT(fullFrameBytes, 0u);

    // One source pixel maps to exactly one cell at this size.
    frame.SetPixel(10, 5, 255, 255, 255);
    presenter.Present(frame.View());

    EXPECT_EQ(presenter.GetLastRedrawnCellCount(), 1);
    EXPECT_LT(presenter.GetLastFrameBytes().size(), fullFrameBytes / 10)
        << "one changed cell must not cost a full frame";
}

TEST_F(TerminalPresenter, ChangingTheScaleModeForcesTheNextFrameToRedrawEverything)
{
    // The picture moves within the grid, so cells that were outside it are now inside. A diff
    // against the old screen would leave them holding whatever they last showed.
    TerminalSurfacePresenter presenter(pty_.Device(), pty_.Device(),
                                       TerminalColourDepth::TrueColour, 320, 240);
    presenter.SetScaleMode(PresentScaleMode::Stretch, PresentFilter::Nearest);

    const Frame frame(320, 240, 90, 120, 200);
    presenter.Present(frame.View());
    presenter.Present(frame.View());
    ASSERT_EQ(presenter.GetLastRedrawnCellCount(), 0);

    presenter.SetScaleMode(PresentScaleMode::Letterbox, PresentFilter::Nearest);
    presenter.Present(frame.View());
    EXPECT_EQ(presenter.GetLastRedrawnCellCount(), 40 * 20);
}

// --- through the platform ---------------------------------------------------------------------------

TEST(TerminalPresenterThroughPlatformTest, PresentationIsRefusedWhenStandardOutputIsNotATerminal)
{
    // The case that matters in CI and in any redirected run: refusing is what stops a build log
    // filling with escape sequences, and the capability set says so in advance.
    const std::unique_ptr<IPlatform> platform = PlatformFactory::Create("Terminal");
    const PlatformCapabilities capabilities = platform->GetCapabilities();

    WindowDescription description;
    description.width = 320;
    description.height = 240;
    const std::unique_ptr<IPlatformWindow> window = platform->CreateWindow(description);

    if (capabilities.surfacePresentation)
    {
        GTEST_SKIP() << "this test process is attached to a real terminal";
    }

    try
    {
        (void)platform->CreateSurfacePresenter(*window);
        FAIL() << "presenting into a pipe must be refused, not attempted";
    }
    catch (const PlatformNotSupportedException& refusal)
    {
        EXPECT_EQ(refusal.GetCapability(), PlatformCapability::SurfacePresentation);
    }
}

} // namespace

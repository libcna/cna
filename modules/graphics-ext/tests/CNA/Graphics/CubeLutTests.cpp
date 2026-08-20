// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2130: reading the `.cube` grading exchange format.
//
// `ColorGradePass` has always taken a table. What the layer had no way to accept was the file a
// colourist delivers, so a grade produced in a grading tool needed a conversion step outside CNA to
// get here. These tests are mostly about the two things a lenient parser gets silently wrong --
// entry count and entry order -- because a table with the right number of wrong entries produces a
// frame that looks graded rather than one that looks broken.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "EngineTestSupport.hpp"

#include "CNA/Graphics/ColorGradePass.hpp"
#include "CNA/Graphics/CubeLut.hpp"
#include "CNA/Graphics/EngineException.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "CNA/GraphicsCapability.hpp"

#include <cmath>
#include <sstream>
#include <string>
#include <vector>

namespace {

using CNA::Graphics::ColorGradePass;
using CNA::Graphics::CubeLut;
using CNA::Graphics::EngineException;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::Texture3D;

/// A table of the given size whose every entry names its own index, so a reader that walks the
/// entries in the wrong order produces visibly wrong values rather than plausible ones.
std::string IndexTable(const int size)
{
    std::ostringstream out;
    out << "TITLE \"index table\"\n";
    out << "LUT_3D_SIZE " << size << "\n";
    const float last = static_cast<float>(size - 1);
    // Red fastest, then green, then blue: the format's order.
    for (int blue = 0; blue < size; ++blue)
        for (int green = 0; green < size; ++green)
            for (int red = 0; red < size; ++red)
                out << static_cast<float>(red) / last << " "
                    << static_cast<float>(green) / last << " "
                    << static_cast<float>(blue) / last << "\n";
    return out.str();
}

// ── The format ──────────────────────────────────────────────────────────────

TEST(CubeLutTest, ATableIsReadWithItsSizeAndTitle)
{
    const CubeLut lut = CubeLut::parse(IndexTable(4));
    EXPECT_EQ(lut.getSize(), 4);
    EXPECT_EQ(lut.getTitle(), "index table");
}

TEST(CubeLutTest, EntriesAreReadRedFastest)
{
    // The one mistake that produces a table of exactly the right size holding exactly the wrong
    // values. A parser that loops blue-fastest reads a transposed cube, and the frame it grades
    // looks like a strong creative choice rather than a bug.
    const CubeLut lut = CubeLut::parse(IndexTable(4));
    const Vector3 entry = lut.getEntry(3, 1, 0);
    EXPECT_NEAR(entry.X, 1.0f, 1e-5f);
    EXPECT_NEAR(entry.Y, 1.0f / 3.0f, 1e-5f);
    EXPECT_NEAR(entry.Z, 0.0f, 1e-5f);
}

TEST(CubeLutTest, CommentsBlankLinesAndUnknownKeywordsAreIgnored)
{
    std::string text = "# a comment\n\nLUT_3D_SIZE 2\nSOMETHING_NEW 4\n";
    for (int i = 0; i < 8; ++i) text += "0.25 0.5 0.75\n";
    const CubeLut lut = CubeLut::parse(text);
    EXPECT_EQ(lut.getSize(), 2);
    EXPECT_NEAR(lut.getEntry(1, 1, 1).Y, 0.5f, 1e-5f);
}

TEST(CubeLutTest, WindowsLineEndingsAreRead)
{
    // A carriage return left on the end of a line stops a stream extraction rather than failing it,
    // so a file written on Windows parses to almost the right thing -- which is worse than not
    // parsing at all.
    std::string text = "LUT_3D_SIZE 2\r\n";
    for (int i = 0; i < 8; ++i) text += "0.25 0.5 0.75\r\n";
    const CubeLut lut = CubeLut::parse(text);
    EXPECT_EQ(lut.getSize(), 2);
    EXPECT_NEAR(lut.getEntry(0, 0, 0).Z, 0.75f, 1e-5f);
}

TEST(CubeLutTest, TheDomainIsReadAndTheUnitCubeIsRecognised)
{
    std::string text = "LUT_3D_SIZE 2\nDOMAIN_MIN 0.0 0.0 0.0\nDOMAIN_MAX 1.0 1.0 1.0\n";
    for (int i = 0; i < 8; ++i) text += "0 0 0\n";
    const CubeLut unit = CubeLut::parse(text);
    EXPECT_TRUE(unit.isUnitDomain());

    std::string other = "LUT_3D_SIZE 2\nDOMAIN_MIN 0.0 0.0 0.0\nDOMAIN_MAX 4.0 4.0 4.0\n";
    for (int i = 0; i < 8; ++i) other += "0 0 0\n";
    const CubeLut wide = CubeLut::parse(other);
    EXPECT_FALSE(wide.isUnitDomain());
    EXPECT_NEAR(wide.getDomainMax().X, 4.0f, 1e-5f);
}

TEST(CubeLutTest, ADefaultedDomainIsTheUnitCube)
{
    EXPECT_TRUE(CubeLut::parse(IndexTable(2)).isUnitDomain());
}

// ── What is refused, and why ────────────────────────────────────────────────

TEST(CubeLutTest, AWrongEntryCountIsRefusedRatherThanPadded)
{
    // The refusal that matters most. A table one entry short, padded or truncated, grades every
    // colour past the missing entry with the wrong neighbour -- and the frame still looks like a
    // frame.
    std::string text = "LUT_3D_SIZE 2\n";
    for (int i = 0; i < 7; ++i) text += "0 0 0\n";
    EXPECT_THROW(CubeLut::parse(text), EngineException);
}

TEST(CubeLutTest, ADocumentWithNoSizeIsRefused)
{
    EXPECT_THROW(CubeLut::parse("0 0 0\n1 1 1\n"), EngineException);
}

TEST(CubeLutTest, AOneDimensionalTableIsRefusedByName)
{
    // A 1D table is a per-channel curve. It has entries and a size and would load as something,
    // and that something would not be the grade the file describes.
    std::string text = "LUT_1D_SIZE 4\n0 0 0\n1 1 1\n";
    try
    {
        CubeLut::parse(text);
        FAIL() << "a 1D table was accepted";
    }
    catch (const EngineException& e)
    {
        EXPECT_NE(std::string(e.what()).find("1D"), std::string::npos) << e.what();
    }
}

TEST(CubeLutTest, ASizeOutsideTheAcceptedRangeIsRefused)
{
    std::string tooSmall = "LUT_3D_SIZE 1\n0 0 0\n";
    EXPECT_THROW(CubeLut::parse(tooSmall), EngineException);

    std::string tooLarge = "LUT_3D_SIZE 128\n";
    EXPECT_THROW(CubeLut::parse(tooLarge), EngineException);
}

TEST(CubeLutTest, AMissingFileIsRefusedByName)
{
    EXPECT_THROW(CubeLut::loadFromFile("no/such/grade.cube"), EngineException);
}

TEST(CubeLutTest, AnIndexOutsideTheTableThrows)
{
    const CubeLut lut = CubeLut::parse(IndexTable(2));
    EXPECT_THROW((void)lut.getEntry(2, 0, 0), std::out_of_range);
    EXPECT_THROW((void)lut.getEntry(0, -1, 0), std::out_of_range);
}

// ── The textures it builds ──────────────────────────────────────────────────

TEST(CubeLutTest, TheStripItBuildsIsTheOneTheGradePassAccepts)
{
    GraphicsDevice gd;
    const CubeLut lut = CubeLut::parse(IndexTable(8));
    auto strip = lut.createStripTexture(gd);

    ASSERT_NE(strip, nullptr);
    EXPECT_EQ(strip->getWidthProperty(), 64);
    EXPECT_EQ(strip->getHeightProperty(), 8);
    EXPECT_EQ(ColorGradePass::lutSizeForStrip(strip->getWidthProperty(),
                                              strip->getHeightProperty()), 8);

    ColorGradePass pass(gd);
    EXPECT_NO_THROW(pass.setLut(strip.get()));
    EXPECT_EQ(pass.getLut(), strip.get());
}

TEST(CubeLutTest, TheStripHoldsEachEntryWhereTheShaderLooksForIt)
{
    // The strip's layout in one assertion: x carries the blue slice and the red index within it,
    // y carries green. Read back rather than argued, because the shader's addressing and this
    // writer's addressing are two separate pieces of arithmetic that have to agree.
    GraphicsDevice gd;
    constexpr int size = 4;
    const CubeLut lut = CubeLut::parse(IndexTable(size));
    auto strip = lut.createStripTexture(gd);

    std::vector<Color> texels(static_cast<std::size_t>(size) * size * size, Color::Black);
    try { strip->GetData(texels.data(), static_cast<int>(texels.size())); }
    catch (...) { GTEST_SKIP() << "this renderer does not read texture data back"; }

    for (int blue = 0; blue < size; ++blue)
        for (int green = 0; green < size; ++green)
            for (int red = 0; red < size; ++red)
            {
                const int x = blue * size + red;
                const Color texel = texels[static_cast<std::size_t>(green) * size * size + x];
                const Vector3 entry = lut.getEntry(red, green, blue);
                EXPECT_NEAR(static_cast<float>(texel.getRProperty()) / 255.0f, entry.X, 0.01f)
                    << "at (" << red << ", " << green << ", " << blue << ")";
                EXPECT_NEAR(static_cast<float>(texel.getBProperty()) / 255.0f, entry.Z, 0.01f)
                    << "at (" << red << ", " << green << ", " << blue << ")";
            }
}

TEST(CubeLutTest, TheVolumeItBuildsIsACubeOfTheTablesSize)
{
    GraphicsDevice gd;
    if (!gd.SupportsCapability(CNA::GraphicsCapability::Texture3D))
        GTEST_SKIP() << "this renderer has no volume textures, so there is nothing to build";

    const CubeLut lut = CubeLut::parse(IndexTable(8));
    auto volume = lut.createVolumeTexture(gd);
    ASSERT_NE(volume, nullptr);
    EXPECT_EQ(volume->getWidthProperty(), 8);
    EXPECT_EQ(volume->getDepthProperty(), 8);

    ColorGradePass pass(gd);
    EXPECT_NO_THROW(pass.setVolumeLut(volume.get()));
    EXPECT_EQ(pass.getVolumeLut(), volume.get());
    pass.setVolumeLut(nullptr);
    EXPECT_EQ(pass.getVolumeLut(), nullptr);
}

} // namespace

#endif // CNA_CNAEXT

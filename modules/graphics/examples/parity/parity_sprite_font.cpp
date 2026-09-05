// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-179 (harness: WEBGPU-207): a `SpriteFont` string reaches pixels.
//
// Glyph placement, spacing, newline handling and the default character are shared, renderer-neutral
// `SpriteFont`/`SpriteBatch` logic and are already covered by `SpriteFontTests`. What no test
// covered is that any of it reaches WebGPU's own pixels at all, so this fixture is deliberately
// narrow: it draws one string and reads the glyph cells, which is enough to catch the
// renderer-level failures the row names -- a wrong source rectangle, a wrong row order, a dropped
// batch flush.
//
// THE ATLAS IS THREE DIFFERENT COLOURS, one per glyph, not three copies of white. A white atlas
// would let a renderer sample the wrong glyph, or the same glyph three times, and still produce a
// picture that looks exactly right; with 'A' red, 'B' green and 'C' blue, each drawn cell says
// which source rectangle it came from. Each glyph is also painted with its TOP-LEFT texel darkened,
// so a vertically flipped atlas -- the classic render-target/texture row-order bug -- puts the dark
// texel at the bottom and is caught by a probe that a solid glyph could not make.
//
// The string is "AB\nCX": two glyphs, a newline, a glyph, and a character the font does not have.
// The font's default character is 'A', so the last cell must render RED -- and because 'A' is a
// different colour from 'C' beside it, a renderer that dropped the substituted glyph, or drew the
// previous one twice, lands somewhere this fixture can see.

#include "parity/ParityFixture.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <array>
#include <cstdio>
#include <optional>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWidth = 64;
    constexpr int kHeight = 48;

    const Color kClearColor(9, 13, 17, 255);
    const Color kGlyphA(220, 40, 40, 255);
    const Color kGlyphB(40, 200, 60, 255);
    const Color kGlyphC(50, 70, 220, 255);
    /// The marker painted into each glyph's own top-left texel: dark enough to be unmistakable and
    /// unlike any glyph colour, so "which way up did the atlas arrive" is a pixel question.
    const Color kCornerMark(0, 0, 0, 255);

    constexpr int kGlyph = 8;        ///< Each glyph is 8x8 in the atlas and on screen.
    constexpr int kAtlasWidth = kGlyph * 3;
    constexpr int kLineSpacing = 12;
    /// The string's origin. Integer, so no cell straddles a pixel boundary.
    constexpr int kOriginX = 8;
    constexpr int kOriginY = 6;
}

/// WEBGPU-179: a SpriteFont string, a newline and a default-character substitution reach pixels.
class SpriteFontParityFixture : public CNA::Parity::ParityFixture
{
public:
    SpriteFontParityFixture() : ParityFixture(kWidth, kHeight) {}

protected:
    void RunFixture() override
    {
        auto& device = getGraphicsDeviceProperty();

        Texture2D atlas(device, kAtlasWidth, kGlyph, false, SurfaceFormat::Color);
        std::vector<Color> texels(static_cast<std::size_t>(kAtlasWidth * kGlyph));
        const std::array<Color, 3> glyphColors{kGlyphA, kGlyphB, kGlyphC};
        for (int y = 0; y < kGlyph; ++y)
        {
            for (int x = 0; x < kAtlasWidth; ++x)
            {
                const int glyph = x / kGlyph;
                const bool topLeftTexel = (x % kGlyph) == 0 && y == 0;
                texels[static_cast<std::size_t>(y * kAtlasWidth + x)] =
                    topLeftTexel ? kCornerMark : glyphColors[static_cast<std::size_t>(glyph)];
            }
        }
        atlas.SetData(texels.data(), static_cast<int>(texels.size()));

        std::vector<Rectangle> glyphBounds{Rectangle(0, 0, kGlyph, kGlyph),
                                           Rectangle(kGlyph, 0, kGlyph, kGlyph),
                                           Rectangle(2 * kGlyph, 0, kGlyph, kGlyph)};
        std::vector<Rectangle> cropping{Rectangle(0, 0, kGlyph, kGlyph),
                                        Rectangle(0, 0, kGlyph, kGlyph),
                                        Rectangle(0, 0, kGlyph, kGlyph)};
        std::vector<SharpRuntime::charcs> characters{u'A', u'B', u'C'};
        std::vector<Vector3> kerning{Vector3(0.0f, static_cast<float>(kGlyph), 0.0f),
                                     Vector3(0.0f, static_cast<float>(kGlyph), 0.0f),
                                     Vector3(0.0f, static_cast<float>(kGlyph), 0.0f)};
        SpriteFont font(atlas, glyphBounds, cropping, characters, kLineSpacing, 0.0f, kerning,
                        std::optional<SharpRuntime::charcs>(u'A'));

        const SamplerState pointClamp = SamplerState::PointClamp;
        device.setBlendStateProperty(BlendState::Opaque);
        device.Clear(kClearColor);

        SpriteBatch batch(device);
        batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr);
        batch.DrawString(font, "AB\nCX",
                         Vector2(static_cast<float>(kOriginX), static_cast<float>(kOriginY)),
                         Color::White);
        batch.End();

        /// The interior of the glyph cell at (column, line), inset past the corner marker.
        const auto glyphCell = [](int column, int line) {
            return Rectangle(kOriginX + column * kGlyph + 2, kOriginY + line * kLineSpacing + 2,
                             kGlyph - 3, kGlyph - 3);
        };
        /// The glyph cell's own top-left pixel, where the atlas's marker must land.
        const auto glyphCorner = [](int column, int line) {
            return Rectangle(kOriginX + column * kGlyph, kOriginY + line * kLineSpacing, 1, 1);
        };

        const auto describe = [this](const Rectangle& r) {
            const Color c = Average(r);
            std::printf("(%d,%d,%d)", c.getRProperty(), c.getGProperty(), c.getBProperty());
            return c;
        };
        std::printf("[info] line 0: ");
        describe(glyphCell(0, 0)); std::printf(" ");
        describe(glyphCell(1, 0)); std::printf("\n[info] line 1: ");
        describe(glyphCell(0, 1)); std::printf(" ");
        describe(glyphCell(1, 1)); std::printf("\n");

        // Each glyph came from its OWN source rectangle: three different colours, in the order the
        // string names them. A single wrong source rect changes exactly one of these.
        ExpectAverage("'A' renders the atlas's first glyph", glyphCell(0, 0), kGlyphA, 4);
        ExpectAverage("'B' renders the SECOND glyph, one glyph-width to the right",
                      glyphCell(1, 0), kGlyphB, 4);
        // The newline moved the pen down by lineSpacing and back to the string's x origin. Both
        // halves matter: a renderer that only moved down leaves the second line indented, and one
        // that only reset x draws it over the first.
        ExpectAverage("the newline put 'C' on the second line, at the string's own x origin",
                      glyphCell(0, 1), kGlyphC, 4);
        ExpectAverage("...and the cell to the RIGHT of 'A' on line 0 is 'B', not 'C' -- the second "
                      "line did not run on from the first",
                      glyphCell(1, 0), kGlyphB, 4);
        // 'X' is not in the font, so the default character 'A' is substituted -- and 'A' is red,
        // a colour neither of its neighbours has.
        ExpectAverage("'X' is absent from the font, so the default character 'A' renders instead",
                      glyphCell(1, 1), kGlyphA, 4);
        ExpectDistinct("and that substitution is not simply the previous glyph drawn twice",
                       glyphCell(0, 1), glyphCell(1, 1), 60);
        // Row order: the atlas's top-left texel must arrive at each glyph cell's top-left pixel.
        ExpectAverage("the atlas arrives the right way up: its top-left texel is the glyph's "
                      "top-left pixel, not its bottom-left",
                      glyphCorner(0, 0), kCornerMark, 6);
        ExpectAverage("...on the second line too", glyphCorner(0, 1), kCornerMark, 6);
        // Non-vacuity: the string really painted something, and did not paint everything.
        ExpectAverage("the area above the string is untouched", Rectangle(1, 1, 4, 4), kClearColor,
                      4);
        ExpectAverage("and so is the area past its last glyph",
                      Rectangle(kOriginX + 2 * kGlyph + 4, kOriginY + 2, 4, 4), kClearColor, 4);
    }
};

CNA_PARITY_FIXTURE_MAIN(SpriteFontParityFixture)

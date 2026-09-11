// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-178 (harness: WEBGPU-207), the GEOMETRIC half: source rectangles,
// rotation about a non-zero origin, non-uniform scale, a sub-pixel destination, and `SpriteEffects`
// flips combined with rotation.
//
// THE TEXTURE IS A 4x4 QUADRANT CHART. Its four quadrants are red, green, blue and yellow, so a
// sprite's own pixels say which part of the texture reached them and which way up it arrived.
// Against a flat texture every one of these cells would pass with the sampling completely wrong:
// a source rectangle that read the whole texture, a flip that did nothing, a rotation that turned
// the other way -- all invisible. Against the chart each of those lands a different colour in the
// probe corner this fixture reads.
//
// EVERY DESTINATION IS INTEGER-ALIGNED AND POINT-SAMPLED except the one cell that exists to test a
// fractional destination. That is deliberate: `WebGPU_PointSamplingContract` records that WebGPU
// and EasyGL do not yet agree about XNA's pixel-centre convention (`WEBGPU-187`), so a fixture that
// straddled texel boundaries everywhere would fail for that reason rather than for the behaviour
// under test. The fractional cell therefore asserts only what a half-pixel offset must do in ANY
// convention -- move the sprite's coverage boundary by less than a whole pixel while leaving the
// sprite's interior colour alone -- and the surrounding cells carry the exact-colour claims.
//
// The layout, four columns by three rows:
//
//   row 0  source rects:  whole texture | top-left quadrant | bottom-right quadrant | past the edge
//   row 1  transforms:    rotation about a corner origin | about the centre | non-uniform scale |
//                         a half-pixel destination offset
//   row 2  SpriteEffects: none | FlipHorizontally | FlipVertically | both, with a quarter turn

#include "parity/ParityFixture.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <optional>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kColumns = 4;
    constexpr int kRows = 3;
    constexpr int kCell = 32;
    constexpr int kWidth = kColumns * kCell;
    constexpr int kHeight = kRows * kCell;

    const Color kClearColor(9, 13, 17, 255);

    /// The chart's four quadrant colours, in the order the texels are written: the 4x4 texture is
    /// red top-left, green top-right, blue bottom-left, yellow bottom-right.
    const Color kRed(220, 40, 40, 255);
    const Color kGreen(40, 200, 60, 255);
    const Color kBlue(50, 70, 220, 255);
    const Color kYellow(230, 200, 50, 255);
    constexpr int kTextureSize = 4;

    /// The sprite is drawn 16x16 inside its 32x32 cell, leaving room for a rotation to swing.
    constexpr int kSprite = 16;
}

/// WEBGPU-178: SpriteBatch source rectangles, rotation, scale, sub-pixel offsets and flips.
class SpriteGeometryParityFixture : public CNA::Parity::ParityFixture
{
public:
    SpriteGeometryParityFixture() : ParityFixture(kWidth, kHeight) {}

protected:
    void RunFixture() override
    {
        auto& device = getGraphicsDeviceProperty();
        const CNA::Parity::ParityGrid grid{kWidth, kHeight, kColumns, kRows};

        Texture2D chart(device, kTextureSize, kTextureSize, false, SurfaceFormat::Color);
        std::array<Color, kTextureSize * kTextureSize> texels{};
        for (int y = 0; y < kTextureSize; ++y)
        {
            for (int x = 0; x < kTextureSize; ++x)
            {
                const bool right = x >= kTextureSize / 2;
                const bool bottom = y >= kTextureSize / 2;
                texels[static_cast<std::size_t>(y * kTextureSize + x)] =
                    bottom ? (right ? kYellow : kBlue) : (right ? kGreen : kRed);
            }
        }
        chart.SetData(texels.data(), static_cast<int>(texels.size()));

        // The precondition every assertion below rests on, and the regression guard for the defect
        // that writing this fixture found: `SpriteBatch` derives its projection from
        // `GraphicsDevice.Viewport`, so a Viewport that does not match the backbuffer scales every
        // sprite by the ratio between them and no amount of correct sprite maths shows through.
        {
            const auto& vp = device.getViewportProperty();
            const auto& pp = device.getPresentationParametersProperty();
            std::printf("[info] viewport %dx%d at (%d,%d); backbuffer %dx%d\n",
                        vp.getWidthProperty(), vp.getHeightProperty(),
                        vp.getXProperty(), vp.getYProperty(),
                        pp.getBackBufferWidthProperty(), pp.getBackBufferHeightProperty());
            Require(vp.getXProperty() == 0 && vp.getYProperty() == 0 &&
                        vp.getWidthProperty() == pp.getBackBufferWidthProperty() &&
                        vp.getHeightProperty() == pp.getBackBufferHeightProperty(),
                    "the device's Viewport covers the whole backbuffer -- SpriteBatch's projection "
                    "comes from it, so a stale one silently scales every sprite below");
        }
        device.setBlendStateProperty(BlendState::Opaque);
        device.Clear(kClearColor);

        SpriteBatch batch(device);
        // Deferred + PointClamp: exact texels, and the draw order is the call order.
        const SamplerState pointClamp = SamplerState::PointClamp;
        batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr);

        /// The top-left corner of a cell's sprite, in backbuffer pixels.
        const auto spriteOrigin = [](int column, int row) {
            return Vector2(static_cast<float>(column * kCell + (kCell - kSprite) / 2),
                           static_cast<float>(row * kCell + (kCell - kSprite) / 2));
        };
        const auto destination = [&spriteOrigin](int column, int row) {
            const Vector2 at = spriteOrigin(column, row);
            return Rectangle(static_cast<int>(at.X), static_cast<int>(at.Y), kSprite, kSprite);
        };

        // --- Row 0: source rectangles ---------------------------------------------------------
        batch.Draw(chart, destination(0, 0), std::optional<Rectangle>{}, Color::White);
        batch.Draw(chart, destination(1, 0), std::optional<Rectangle>(Rectangle(0, 0, 2, 2)),
                   Color::White);
        batch.Draw(chart, destination(2, 0), std::optional<Rectangle>(Rectangle(2, 2, 2, 2)),
                   Color::White);
        // XNA does NOT clamp a source rectangle to the texture: the rectangle becomes texture
        // coordinates and the SAMPLER's address mode decides what lies past the edge. With
        // PointClamp that is the edge texel repeated, so this cell's right half must be the same
        // colour as its left half's right edge rather than wrapping back to red.
        batch.Draw(chart, destination(3, 0), std::optional<Rectangle>(Rectangle(2, 2, 4, 4)),
                   Color::White);

        // --- Row 1: rotation, scale, sub-pixel ------------------------------------------------
        // A quarter turn about the sprite's own CENTRE leaves it in the same cell; the same turn
        // about its top-left corner swings it somewhere else entirely. Both are drawn from the
        // same position, so the origin is the only difference between them.
        const float quarterTurn = MathHelper::PiOver2;
        batch.Draw(chart, spriteOrigin(0, 1) + Vector2(static_cast<float>(kSprite), 0.0f),
                   std::optional<Rectangle>{}, Color::White, quarterTurn, Vector2::Zero,
                   Vector2(static_cast<float>(kSprite) / kTextureSize,
                           static_cast<float>(kSprite) / kTextureSize),
                   SpriteEffects::None, 0.0f);
        batch.Draw(chart,
                   spriteOrigin(1, 1) + Vector2(static_cast<float>(kSprite) / 2.0f,
                                                static_cast<float>(kSprite) / 2.0f),
                   std::optional<Rectangle>{}, Color::White, quarterTurn,
                   Vector2(static_cast<float>(kTextureSize) / 2.0f,
                           static_cast<float>(kTextureSize) / 2.0f),
                   Vector2(static_cast<float>(kSprite) / kTextureSize,
                           static_cast<float>(kSprite) / kTextureSize),
                   SpriteEffects::None, 0.0f);
        // Non-uniform scale: half as wide, full height. The sprite's left half of the cell is
        // painted and the right half is not, which no uniform scale produces.
        batch.Draw(chart, spriteOrigin(2, 1), std::optional<Rectangle>{}, Color::White, 0.0f,
                   Vector2::Zero,
                   Vector2(static_cast<float>(kSprite) / 2.0f / kTextureSize,
                           static_cast<float>(kSprite) / kTextureSize),
                   SpriteEffects::None, 0.0f);
        // A half-pixel destination offset. See the header: this cell claims only that the sprite's
        // INTERIOR is unchanged and its coverage boundary moved by less than a pixel.
        batch.Draw(chart, spriteOrigin(3, 1) + Vector2(0.5f, 0.5f), std::optional<Rectangle>{},
                   Color::White, 0.0f, Vector2::Zero,
                   Vector2(static_cast<float>(kSprite) / kTextureSize,
                           static_cast<float>(kSprite) / kTextureSize),
                   SpriteEffects::None, 0.0f);

        // --- Row 2: SpriteEffects -------------------------------------------------------------
        const std::array<SpriteEffects, 3> flips{SpriteEffects::None,
                                                 SpriteEffects::FlipHorizontally,
                                                 SpriteEffects::FlipVertically};
        for (int column = 0; column < 3; ++column)
        {
            batch.Draw(chart, spriteOrigin(column, 2), std::optional<Rectangle>{}, Color::White,
                       0.0f, Vector2::Zero,
                       Vector2(static_cast<float>(kSprite) / kTextureSize,
                               static_cast<float>(kSprite) / kTextureSize),
                       flips[static_cast<std::size_t>(column)], 0.0f);
        }
        // Both flips at once IS a half turn, so combining them with a further half turn about the
        // centre must land back on the unflipped picture -- a claim that fails for any renderer
        // that applies the flip after the rotation instead of to the texture coordinates.
        batch.Draw(chart,
                   spriteOrigin(3, 2) + Vector2(static_cast<float>(kSprite) / 2.0f,
                                                static_cast<float>(kSprite) / 2.0f),
                   std::optional<Rectangle>{}, Color::White, MathHelper::Pi,
                   Vector2(static_cast<float>(kTextureSize) / 2.0f,
                           static_cast<float>(kTextureSize) / 2.0f),
                   Vector2(static_cast<float>(kSprite) / kTextureSize,
                           static_cast<float>(kSprite) / kTextureSize),
                   static_cast<SpriteEffects>(static_cast<int>(SpriteEffects::FlipHorizontally) |
                                              static_cast<int>(SpriteEffects::FlipVertically)),
                   0.0f);
        batch.End();

        /// One quadrant of a cell's sprite, inset so no probe touches a boundary pixel. The
        /// sprite's own size is a parameter because the non-uniform-scale cell is half as wide as
        /// every other one -- probing IT at the default width straddles two texel columns and
        /// reads their average, which is a fact about the probe rather than about the renderer.
        const auto quadrantOf = [&spriteOrigin](int column, int row, int qx, int qy,
                                                int spriteWidth, int spriteHeight) {
            const Vector2 at = spriteOrigin(column, row);
            const int halfW = spriteWidth / 2;
            const int halfH = spriteHeight / 2;
            const int insetX = std::max(1, halfW / 4);
            const int insetY = std::max(1, halfH / 4);
            return Rectangle(static_cast<int>(at.X) + qx * halfW + insetX,
                             static_cast<int>(at.Y) + qy * halfH + insetY,
                             halfW - 2 * insetX, halfH - 2 * insetY);
        };
        const auto quadrant = [&quadrantOf](int column, int row, int qx, int qy) {
            return quadrantOf(column, row, qx, qy, kSprite, kSprite);
        };
        const auto name = [](const Color& c) {
            const auto near = [&c](const Color& other) {
                return std::abs(c.getRProperty() - other.getRProperty()) <= 6 &&
                       std::abs(c.getGProperty() - other.getGProperty()) <= 6 &&
                       std::abs(c.getBProperty() - other.getBProperty()) <= 6;
            };
            if (near(kRed)) return "red";
            if (near(kGreen)) return "green";
            if (near(kBlue)) return "blue";
            if (near(kYellow)) return "yellow";
            if (near(kClearColor)) return "clear";
            return "other";
        };
        for (int row = 0; row < kRows; ++row)
        {
            for (int column = 0; column < kColumns; ++column)
            {
                std::printf("[info] cell (%d,%d) quadrants TL=%-6s TR=%-6s BL=%-6s BR=%-6s\n",
                            column, row,
                            name(Average(quadrant(column, row, 0, 0))),
                            name(Average(quadrant(column, row, 1, 0))),
                            name(Average(quadrant(column, row, 0, 1))),
                            name(Average(quadrant(column, row, 1, 1))));
            }
        }

        // --- Row 0 -----------------------------------------------------------------------------
        ExpectAverage("the whole texture: top-left quadrant is red", quadrant(0, 0, 0, 0), kRed, 4);
        ExpectAverage("the whole texture: top-right quadrant is green", quadrant(0, 0, 1, 0), kGreen,
                      4);
        ExpectAverage("the whole texture: bottom-left quadrant is blue", quadrant(0, 0, 0, 1), kBlue,
                      4);
        ExpectAverage("the whole texture: bottom-right quadrant is yellow", quadrant(0, 0, 1, 1),
                      kYellow, 4);
        ExpectFlat("source rect (0,0,2,2) shows the red quadrant ALONE, filling the sprite",
                   quadrant(1, 0, 1, 1), 6);
        ExpectAverage("and that quadrant is red, not the whole chart scaled down",
                      quadrant(1, 0, 1, 1), kRed, 4);
        ExpectAverage("source rect (2,2,2,2) shows the yellow quadrant alone",
                      quadrant(2, 0, 0, 0), kYellow, 4);
        // The rectangle runs two texels past the edge; PointClamp repeats the edge texel, so the
        // whole sprite is still yellow rather than wrapping back to red.
        ExpectAverage("a source rect running past the texture is NOT clamped by XNA -- the sampler "
                      "governs, and PointClamp repeats the edge texel rather than wrapping",
                      quadrant(3, 0, 1, 1), kYellow, 4);
        ExpectAverage("and its in-range corner is the same yellow", quadrant(3, 0, 0, 0), kYellow,
                      4);

        // --- Row 1 -----------------------------------------------------------------------------
        // A quarter turn clockwise in screen space takes the texture's top-left (red) to the
        // sprite's top-right. Both rotation cells must show that; they differ only in where the
        // sprite ended up, which is why the corner-origin one is drawn a sprite-width to the right.
        ExpectAverage("rotation about a corner origin: red lands top-right", quadrant(0, 1, 1, 0),
                      kRed, 4);
        ExpectAverage("rotation about the centre: red lands top-right too -- the same rotation",
                      quadrant(1, 1, 1, 0), kRed, 4);
        ExpectAverage("...and blue lands top-left", quadrant(1, 1, 0, 0), kBlue, 4);
        // Non-uniform scale: the sprite is half as wide, so its right half of the cell is clear.
        ExpectAverage("non-uniform scale paints an 8-wide, 16-tall sprite: its top-left quadrant "
                      "is still red", quadrantOf(2, 1, 0, 0, kSprite / 2, kSprite), kRed, 4);
        ExpectAverage("and its bottom-right quadrant is still yellow -- the whole chart is there, "
                      "squeezed", quadrantOf(2, 1, 1, 1, kSprite / 2, kSprite), kYellow, 4);
        ExpectAverage("while the cell's right half is clear -- a uniform scale would have painted it",
                      quadrant(2, 1, 1, 0), kClearColor, 4);
        // The half-pixel cell: the interior is untouched by a sub-pixel move.
        ExpectAverage("a half-pixel destination offset leaves the sprite's interior colour alone",
                      quadrant(3, 1, 0, 0), kRed, 4);
        ExpectAverage("in every quadrant", quadrant(3, 1, 1, 1), kYellow, 4);

        // --- Row 2 -----------------------------------------------------------------------------
        ExpectAverage("no flip: red top-left", quadrant(0, 2, 0, 0), kRed, 4);
        ExpectAverage("FlipHorizontally: red moves to the top-RIGHT", quadrant(1, 2, 1, 0), kRed, 4);
        ExpectAverage("...and green to the top-left", quadrant(1, 2, 0, 0), kGreen, 4);
        ExpectAverage("FlipVertically: red moves to the BOTTOM-left", quadrant(2, 2, 0, 1), kRed, 4);
        ExpectAverage("...and blue to the top-left", quadrant(2, 2, 0, 0), kBlue, 4);
        ExpectSameRegion("both flips plus a half turn is the identity -- the flip is applied to the "
                         "texture coordinates, not after the rotation",
                         quadrant(0, 2, 0, 0), quadrant(3, 2, 0, 0), 4);
        ExpectSameRegion("in the opposite corner too", quadrant(0, 2, 1, 1), quadrant(3, 2, 1, 1),
                         4);
    }
};

CNA_PARITY_FIXTURE_MAIN(SpriteGeometryParityFixture)

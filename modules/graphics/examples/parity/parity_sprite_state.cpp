// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-178 (harness: WEBGPU-207), the STATE half: `layerDepth` ordering under
// each `SpriteSortMode`, the `Begin` transform matrix, blend-state leakage between batches, and a
// destination inside a `RenderTarget2D`.
//
// HOW FOUR SORT MODES ARE TOLD APART BY TWO SPRITES. They cannot be: `Deferred` and `Immediate`
// produce the same pixels by design (they differ in WHEN the batch flushes, not in what order the
// sprites land), and any single draw-order/depth pairing makes one of the two sorting modes agree
// with them by coincidence. So each cell draws TWO overlapping pairs whose depth and draw order are
// reversed relative to each other:
//
//   pair 1: red at depth 0.9 drawn first, blue at depth 0.1 drawn second
//   pair 2: red at depth 0.1 drawn FIRST, blue at depth 0.9 drawn second
//
// The two pairs disagree about whether the sprite drawn second is also the front-most one, and that
// disagreement is the whole point. A first attempt at this fixture reversed both the depth AND the
// draw order between the pairs, which keeps `Deferred` and `BackToFront` in lockstep -- they
// produced identical signatures and could not be told apart. With pair 2 as written, the colour in
// each pair's OVERLAP gives the four modes three distinct signatures, which is exactly as many as
// there are distinct behaviours (XNA's layerDepth runs 0 = front to 1 = back):
//
//   Deferred     (blue, blue)  -- draw order wins in both pairs
//   Immediate    (blue, blue)  -- the same, and that agreement is itself asserted
//   BackToFront  (blue, red)   -- the larger depth is drawn first, so the 0.1 sprite ends on top
//   FrontToBack  (red,  blue)  -- its mirror: the smaller depth is drawn first
//
// Each pair also carries two CONTROL probes, on the parts of the two sprites that do not overlap.
// Those must always read their own colour whatever the sort mode does, so a cell that failed by
// drawing nothing, or by drawing one sprite over the whole band, cannot be mistaken for a cell that
// failed by ordering.
//
// ROW 1 is three separate claims:
//   * the `Begin` transform matrix -- a sprite drawn at the origin lands where the matrix puts it,
//     at the scale the matrix gives it; the untransformed twin beside it is what makes that a
//     measurement rather than a picture;
//   * blend-state leakage -- an `Additive` batch followed by an `Opaque` one, with the second
//     sprite over the first. Opaque must REPLACE, so the overlap reads the second sprite's colour
//     exactly. A renderer that let the first batch's blend state survive its `End()` reads the sum;
//   * a destination inside a `RenderTarget2D` -- `REMED-GFX-019`: sprite coordinates inside a bound
//     target are the TARGET's own, not the backbuffer's. The sprite is placed at an offset that
//     would land somewhere else entirely if the backbuffer's coordinate system leaked in.

#include "parity/ParityFixture.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <array>
#include <cstdio>
#include <optional>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kColumns = 4;
    constexpr int kRows = 2;
    constexpr int kCell = 48;
    constexpr int kWidth = kColumns * kCell;
    constexpr int kHeight = kRows * kCell;

    const Color kClearColor(9, 13, 17, 255);
    const Color kRed(220, 40, 40, 255);
    const Color kBlue(50, 70, 220, 255);
    const Color kGreen(40, 200, 60, 255);

    /// The overlapping pair: two 20x16 sprites 12 pixels apart, so 8 pixels of each overlap.
    constexpr int kSpriteW = 20;
    constexpr int kSpriteH = 16;
    constexpr int kPairOffset = 12;
}

/// WEBGPU-178: sort modes, the Begin transform, blend-state leakage and render-target coordinates.
class SpriteStateParityFixture : public CNA::Parity::ParityFixture
{
public:
    SpriteStateParityFixture() : ParityFixture(kWidth, kHeight) {}

protected:
    void RunFixture() override
    {
        auto& device = getGraphicsDeviceProperty();

        const auto makeTexture = [&device](const Color& texel) {
            Texture2D texture(device, 2, 2, false, SurfaceFormat::Color);
            const std::array<Color, 4> texels{texel, texel, texel, texel};
            texture.SetData(texels.data(), static_cast<int>(texels.size()));
            return texture;
        };
        Texture2D red = makeTexture(kRed);
        Texture2D blue = makeTexture(kBlue);
        Texture2D green = makeTexture(kGreen);

        const SamplerState pointClamp = SamplerState::PointClamp;
        device.setBlendStateProperty(BlendState::Opaque);
        device.Clear(kClearColor);

        // --- Row 0: the four sort modes -------------------------------------------------------
        const std::array<SpriteSortMode, kColumns> modes{
            SpriteSortMode::Deferred, SpriteSortMode::Immediate,
            SpriteSortMode::BackToFront, SpriteSortMode::FrontToBack};
        const auto pairRect = [](int column, int pair, int which) {
            const int x = column * kCell + 4 + which * kPairOffset;
            const int y = pair * (kCell / 2) + 6;
            return Rectangle(x, y, kSpriteW, kSpriteH);
        };
        for (int column = 0; column < kColumns; ++column)
        {
            SpriteBatch batch(device);
            batch.Begin(modes[static_cast<std::size_t>(column)], BlendState::Opaque, &pointClamp,
                        nullptr, nullptr);
            // Pair 0: red at 0.9 first, blue at 0.1 second.
            batch.Draw(red, pairRect(column, 0, 0), std::optional<Rectangle>{}, Color::White, 0.0f,
                       Vector2::Zero, SpriteEffects::None, 0.9f);
            batch.Draw(blue, pairRect(column, 0, 1), std::optional<Rectangle>{}, Color::White, 0.0f,
                       Vector2::Zero, SpriteEffects::None, 0.1f);
            // Pair 1: the FRONT sprite is drawn first this time, so draw order and depth order
            // disagree the other way round -- see the header for why that is what separates
            // Deferred from BackToFront.
            batch.Draw(red, pairRect(column, 1, 0), std::optional<Rectangle>{}, Color::White, 0.0f,
                       Vector2::Zero, SpriteEffects::None, 0.1f);
            batch.Draw(blue, pairRect(column, 1, 1), std::optional<Rectangle>{}, Color::White, 0.0f,
                       Vector2::Zero, SpriteEffects::None, 0.9f);
            batch.End();
        }

        // --- Row 1 -----------------------------------------------------------------------------
        const int row1Y = kCell;
        // (0) The Begin transform matrix: the sprite is drawn at the ORIGIN, and the matrix is the
        // only thing that puts it anywhere. Translate to the cell and scale by two.
        {
            SpriteBatch batch(device);
            const Matrix transform =
                Matrix::CreateScale(2.0f, 2.0f, 1.0f) *
                Matrix::CreateTranslation(static_cast<float>(0 * kCell + 8),
                                          static_cast<float>(row1Y + 8), 0.0f);
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr,
                        nullptr, transform);
            batch.Draw(green, Rectangle(0, 0, 8, 8), std::optional<Rectangle>{}, Color::White);
            batch.End();
        }
        // (1) Blend-state leakage: an Additive batch, then an Opaque one over it. Opaque REPLACES,
        // so the overlap must read the second sprite exactly rather than the sum of the two.
        {
            SpriteBatch additive(device);
            additive.Begin(SpriteSortMode::Deferred, BlendState::Additive, &pointClamp, nullptr,
                           nullptr);
            additive.Draw(red, Rectangle(1 * kCell + 6, row1Y + 6, 24, 24),
                          std::optional<Rectangle>{}, Color::White);
            additive.End();

            SpriteBatch opaque(device);
            opaque.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr,
                         nullptr);
            opaque.Draw(blue, Rectangle(1 * kCell + 14, row1Y + 14, 24, 24),
                        std::optional<Rectangle>{}, Color::White);
            opaque.End();
        }
        // (2) A destination inside a RenderTarget2D. The sprite is placed at the TARGET's own
        // (10,10); if the backbuffer's coordinates leaked in, a target bound at cell 2 would put it
        // somewhere else entirely once the target is blitted back at the cell's origin.
        {
            RenderTarget2D target(device, kCell, kCell, false, SurfaceFormat::Color,
                                  DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
            device.SetRenderTarget(&target);
            device.Clear(kClearColor);
            SpriteBatch inTarget(device);
            inTarget.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr,
                           nullptr);
            inTarget.Draw(green, Rectangle(10, 10, 16, 16), std::optional<Rectangle>{},
                          Color::White);
            inTarget.End();
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            SpriteBatch blit(device);
            blit.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr);
            blit.Draw(target, Rectangle(2 * kCell, row1Y, kCell, kCell),
                      std::optional<Rectangle>{}, Color::White);
            blit.End();
        }
        // (3) The same target-local claim with the sprite in the OPPOSITE corner, so "10,10" cannot
        // be passing because everything happens to land top-left.
        {
            RenderTarget2D target(device, kCell, kCell, false, SurfaceFormat::Color,
                                  DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
            device.SetRenderTarget(&target);
            device.Clear(kClearColor);
            SpriteBatch inTarget(device);
            inTarget.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr,
                           nullptr);
            inTarget.Draw(green, Rectangle(kCell - 26, kCell - 26, 16, 16),
                          std::optional<Rectangle>{}, Color::White);
            inTarget.End();
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            SpriteBatch blit(device);
            blit.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr);
            blit.Draw(target, Rectangle(3 * kCell, row1Y, kCell, kCell),
                      std::optional<Rectangle>{}, Color::White);
            blit.End();
        }

        // --- Reading row 0 ---------------------------------------------------------------------
        const auto overlapProbe = [](int column, int pair) {
            const Rectangle first = Rectangle(column * kCell + 4, pair * (kCell / 2) + 6, kSpriteW,
                                              kSpriteH);
            return Rectangle(first.X + kPairOffset + 2, first.Y + 4, 4, 8);
        };
        const auto firstOnlyProbe = [](int column, int pair) {
            return Rectangle(column * kCell + 6, pair * (kCell / 2) + 10, 4, 8);
        };
        const auto secondOnlyProbe = [](int column, int pair) {
            return Rectangle(column * kCell + 4 + kPairOffset + kSpriteW - 6,
                             pair * (kCell / 2) + 10, 4, 8);
        };
        const auto nameOf = [](const Color& c) {
            const auto near = [&c](const Color& other) {
                return std::abs(c.getRProperty() - other.getRProperty()) <= 6 &&
                       std::abs(c.getGProperty() - other.getGProperty()) <= 6 &&
                       std::abs(c.getBProperty() - other.getBProperty()) <= 6;
            };
            if (near(kRed)) return "red";
            if (near(kBlue)) return "blue";
            if (near(kGreen)) return "green";
            if (near(kClearColor)) return "clear";
            return "other";
        };
        static const char* kModeNames[kColumns] = {"Deferred", "Immediate", "BackToFront",
                                                   "FrontToBack"};
        for (int column = 0; column < kColumns; ++column)
        {
            std::printf("[info] %-12s overlap pair0=%-5s pair1=%-5s\n", kModeNames[column],
                        nameOf(Average(overlapProbe(column, 0))),
                        nameOf(Average(overlapProbe(column, 1))));
        }

        // The controls first: whatever the ordering does, each sprite's own exclusive area must
        // carry its own colour, so an ordering verdict is never read off an empty or overdrawn cell.
        for (int column = 0; column < kColumns; ++column)
        {
            ExpectAverage("pair 0's red-only strip is red", firstOnlyProbe(column, 0), kRed, 4);
            ExpectAverage("pair 0's blue-only strip is blue", secondOnlyProbe(column, 0), kBlue, 4);
            ExpectAverage("pair 1's red-only strip is red", firstOnlyProbe(column, 1), kRed, 4);
            ExpectAverage("pair 1's blue-only strip is blue", secondOnlyProbe(column, 1), kBlue, 4);
        }
        // Deferred: draw order wins in both pairs.
        ExpectAverage("Deferred pair 0: the second sprite drawn is on top", overlapProbe(0, 0),
                      kBlue, 4);
        ExpectAverage("Deferred pair 1: the second sprite drawn is on top, even though it is the "
                      "BACK-most one -- Deferred does not sort", overlapProbe(0, 1), kBlue, 4);
        // Immediate must agree with Deferred: the two differ in WHEN the batch flushes, not in the
        // order the sprites land, and asserting that keeps a renderer from "fixing" one of them.
        ExpectSameRegion("Immediate produces the same pixels as Deferred, pair 0",
                         overlapProbe(0, 0), overlapProbe(1, 0), 4);
        ExpectSameRegion("Immediate produces the same pixels as Deferred, pair 1",
                         overlapProbe(0, 1), overlapProbe(1, 1), 4);
        // BackToFront draws the larger depth FIRST, so the 0.1 sprite ends on top in both pairs.
        ExpectAverage("BackToFront pair 0: the layerDepth-0.1 sprite is on top", overlapProbe(2, 0),
                      kBlue, 4);
        ExpectAverage("BackToFront pair 1: the layerDepth-0.1 sprite is on top -- the RED one "
                      "here, which is also the one drawn first, so this is where sorting shows",
                      overlapProbe(2, 1), kRed, 4);
        // FrontToBack is its mirror: the 0.9 sprite ends on top in both.
        ExpectAverage("FrontToBack pair 0: the layerDepth-0.9 sprite is on top", overlapProbe(3, 0),
                      kRed, 4);
        ExpectAverage("FrontToBack pair 1: the layerDepth-0.9 sprite is on top -- the BLUE one",
                      overlapProbe(3, 1), kBlue, 4);
        // The claim a single-pair fixture cannot make: Deferred and BackToFront agree on pair 0 and
        // differ on pair 1, so neither is passing by resembling the other.
        ExpectSameRegion("Deferred and BackToFront agree on pair 0", overlapProbe(0, 0),
                         overlapProbe(2, 0), 4);
        ExpectDistinct("...and disagree on pair 1, which is what separates sorting from draw order",
                       overlapProbe(0, 1), overlapProbe(2, 1), 60);
        // And the two sorting modes really are opposites, which no single-pair test can show.
        ExpectDistinct("BackToFront and FrontToBack disagree on pair 0", overlapProbe(2, 0),
                       overlapProbe(3, 0), 60);
        ExpectDistinct("and on pair 1, in the other direction", overlapProbe(2, 1),
                       overlapProbe(3, 1), 60);

        // --- Reading row 1 ---------------------------------------------------------------------
        // The transform put an 8x8 sprite at (8,8) of its cell, scaled by two: 16x16 at
        // (0*kCell + 8, row1Y + 8).
        ExpectAverage("the Begin transform matrix placed and scaled the sprite",
                      Rectangle(0 * kCell + 12, row1Y + 12, 8, 8), kGreen, 4);
        ExpectAverage("...and the matrix's scale really doubled it: the pixel just past the "
                      "un-scaled 8x8 extent is inside the sprite too",
                      Rectangle(0 * kCell + 19, row1Y + 19, 3, 3), kGreen, 4);
        ExpectAverage("...while beyond the scaled extent the cell is clear",
                      Rectangle(0 * kCell + 26, row1Y + 26, 4, 4), kClearColor, 4);
        // Blend-state leakage: Opaque replaces, so the overlap is the second sprite exactly.
        ExpectAverage("an Opaque batch after an Additive one REPLACES -- the blend state did not "
                      "survive the first batch's End()",
                      Rectangle(1 * kCell + 18, row1Y + 18, 6, 6), kBlue, 4);
        // And the first batch really was Additive: red ADDED to the background, not replacing it.
        // A renderer that ignored the first batch's blend state reads the flat red instead.
        ExpectAverage("the Additive batch's own area is red ADDED to the background, and untouched "
                      "by the second batch",
                      Rectangle(1 * kCell + 8, row1Y + 8, 4, 4),
                      Color(static_cast<SharpRuntime::bytecs>(kRed.getRProperty() +
                                                              kClearColor.getRProperty()),
                            static_cast<SharpRuntime::bytecs>(kRed.getGProperty() +
                                                              kClearColor.getGProperty()),
                            static_cast<SharpRuntime::bytecs>(kRed.getBProperty() +
                                                              kClearColor.getBProperty()), 255),
                      4);
        // Render-target-local coordinates, both corners.
        ExpectAverage("a sprite drawn inside a RenderTarget2D uses the TARGET's coordinates",
                      Rectangle(2 * kCell + 14, row1Y + 14, 8, 8), kGreen, 4);
        ExpectAverage("...and not the backbuffer's: the target's own top-left stays clear",
                      Rectangle(2 * kCell + 2, row1Y + 2, 4, 4), kClearColor, 4);
        ExpectAverage("the same in the target's opposite corner",
                      Rectangle(3 * kCell + kCell - 22, row1Y + kCell - 22, 8, 8), kGreen, 4);
        ExpectAverage("...with that target's top-left clear",
                      Rectangle(3 * kCell + 2, row1Y + 2, 4, 4), kClearColor, 4);
    }
};

CNA_PARITY_FIXTURE_MAIN(SpriteStateParityFixture)

// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-190 (harness: WEBGPU-207), the BLEND family: factors, functions,
// separate colour and alpha, `BlendFactor`, and `ColorWriteChannels`.
//
// Blending is closed-form integer arithmetic, so every cell's expected colour is COMPUTED from the
// source, the destination and the state -- never copied from a passing run. Each cell clears to a
// known destination, draws one quad of a known source, and the result must be what the equation
// says. That is what separates this from a golden image: a renderer whose factors are subtly wrong
// produces a plausible picture, and only an equation notices.
//
// THE SOURCE AND DESTINATION ARE CHOSEN SO NO FACTOR CAN BE CONFUSED WITH ANOTHER. Source
// (200, 60, 30) at alpha 128 and destination (40, 90, 220): the three channels are ordered
// differently in the two colours, the alpha is neither 0 nor 255, and no channel of either is 0 or
// 255. So `SourceColor` and `SourceAlpha`, `DestinationColor` and `InverseSourceColor`, and every
// other near-miss land on different numbers.
//
// The nine cells:
//
//   Opaque                  src
//   Additive                src + dst
//   AlphaBlend              src + dst*(1-srcA)          -- XNA's premultiplied default
//   NonPremultiplied        src*srcA + dst*(1-srcA)
//   Modulate                src*dst                      -- DestinationColor / Zero
//   ReverseSubtract         dst - src
//   Separate                colour additive, alpha keeps the destination's
//   BlendFactor             src * blendFactor
//   ColorWriteChannels      red from src, green and blue from dst

#include "parity/ParityFixture.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kColumns = 3;
    constexpr int kRows = 3;
    constexpr int kCell = 32;
    constexpr int kWidth = kColumns * kCell;
    constexpr int kHeight = kRows * kCell;

    /// The destination each cell is cleared to, and the source each cell draws. See the header for
    /// why these particular numbers.
    const Color kDestination(40, 90, 220, 255);
    const Color kSource(200, 60, 30, 128);
    /// The BlendFactor cell's constant.
    const Color kFactor(64, 128, 192, 255);

    [[nodiscard]] int Clamp255(int v) { return std::clamp(v, 0, 255); }

    /// One channel of `src * srcFactor (+/-) dst * dstFactor`, rounded the way an 8-bit blend does.
    [[nodiscard]] int BlendChannel(int src, float srcFactor, int dst, float dstFactor, int sign)
    {
        const float value = static_cast<float>(src) * srcFactor +
                            static_cast<float>(sign) * static_cast<float>(dst) * dstFactor;
        return Clamp255(static_cast<int>(value + (value >= 0.0f ? 0.5f : -0.5f)));
    }
}

/// WEBGPU-190 (blend): every factor, function, separate channel and write mask, against an equation.
class BlendStatesParityFixture : public CNA::Parity::ParityFixture
{
public:
    BlendStatesParityFixture() : ParityFixture(kWidth, kHeight) {}

protected:
    void RunFixture() override
    {
        auto& device = getGraphicsDeviceProperty();
        const CNA::Parity::ParityGrid grid{kWidth, kHeight, kColumns, kRows};

        Texture2D source(device, 2, 2, false, SurfaceFormat::Color);
        const std::array<Color, 4> texels{kSource, kSource, kSource, kSource};
        source.SetData(texels.data(), static_cast<int>(texels.size()));

        SamplerState pointClamp;
        pointClamp.setFilterProperty(TextureFilter::Point);
        pointClamp.setAddressUProperty(TextureAddressMode::Clamp);
        pointClamp.setAddressVProperty(TextureAddressMode::Clamp);

        // Each cell is cleared to the destination on its own, so a cell's result never depends on
        // its neighbours' order.
        device.setBlendStateProperty(BlendState::Opaque);
        device.Clear(kDestination);

        struct Cell
        {
            const char* label;
            Blend colorSource;
            Blend colorDestination;
            BlendFunction colorFunction;
            Blend alphaSource;
            Blend alphaDestination;
            BlendFunction alphaFunction;
            ColorWriteChannels write;
            bool useBlendFactor;
            Color expected;
        };

        const float srcA = static_cast<float>(kSource.getAProperty()) / 255.0f;
        const auto expect = [&](float srcFactorR, float srcFactorG, float srcFactorB,
                                float dstFactorR, float dstFactorG, float dstFactorB, int sign) {
            return Color(
                static_cast<SharpRuntime::bytecs>(BlendChannel(kSource.getRProperty(), srcFactorR,
                                                               kDestination.getRProperty(),
                                                               dstFactorR, sign)),
                static_cast<SharpRuntime::bytecs>(BlendChannel(kSource.getGProperty(), srcFactorG,
                                                               kDestination.getGProperty(),
                                                               dstFactorG, sign)),
                static_cast<SharpRuntime::bytecs>(BlendChannel(kSource.getBProperty(), srcFactorB,
                                                               kDestination.getBProperty(),
                                                               dstFactorB, sign)),
                255);
        };

        const std::array<Cell, kColumns * kRows> cells{{
            {"Opaque: src", Blend::One, Blend::Zero, BlendFunction::Add, Blend::One, Blend::Zero,
             BlendFunction::Add, ColorWriteChannels::All, false,
             expect(1, 1, 1, 0, 0, 0, +1)},
            {"Additive: src + dst", Blend::One, Blend::One, BlendFunction::Add, Blend::One,
             Blend::One, BlendFunction::Add, ColorWriteChannels::All, false,
             expect(1, 1, 1, 1, 1, 1, +1)},
            {"AlphaBlend: src + dst*(1-srcA)", Blend::One, Blend::InverseSourceAlpha,
             BlendFunction::Add, Blend::One, Blend::InverseSourceAlpha, BlendFunction::Add,
             ColorWriteChannels::All, false,
             expect(1, 1, 1, 1.0f - srcA, 1.0f - srcA, 1.0f - srcA, +1)},
            {"NonPremultiplied: src*srcA + dst*(1-srcA)", Blend::SourceAlpha,
             Blend::InverseSourceAlpha, BlendFunction::Add, Blend::SourceAlpha,
             Blend::InverseSourceAlpha, BlendFunction::Add, ColorWriteChannels::All, false,
             expect(srcA, srcA, srcA, 1.0f - srcA, 1.0f - srcA, 1.0f - srcA, +1)},
            {"Modulate: src*dst", Blend::DestinationColor, Blend::Zero, BlendFunction::Add,
             Blend::One, Blend::Zero, BlendFunction::Add, ColorWriteChannels::All, false,
             expect(static_cast<float>(kDestination.getRProperty()) / 255.0f,
                    static_cast<float>(kDestination.getGProperty()) / 255.0f,
                    static_cast<float>(kDestination.getBProperty()) / 255.0f, 0, 0, 0, +1)},
            {"ReverseSubtract: dst - src", Blend::One, Blend::One, BlendFunction::ReverseSubtract,
             Blend::One, Blend::One, BlendFunction::Add, ColorWriteChannels::All, false,
             Color(static_cast<SharpRuntime::bytecs>(
                       Clamp255(kDestination.getRProperty() - kSource.getRProperty())),
                   static_cast<SharpRuntime::bytecs>(
                       Clamp255(kDestination.getGProperty() - kSource.getGProperty())),
                   static_cast<SharpRuntime::bytecs>(
                       Clamp255(kDestination.getBProperty() - kSource.getBProperty())),
                   255)},
            {"Separate: colour additive, alpha keeps dst", Blend::One, Blend::One,
             BlendFunction::Add, Blend::Zero, Blend::One, BlendFunction::Add,
             ColorWriteChannels::All, false, expect(1, 1, 1, 1, 1, 1, +1)},
            {"BlendFactor: src * factor", Blend::BlendFactor, Blend::Zero, BlendFunction::Add,
             Blend::One, Blend::Zero, BlendFunction::Add, ColorWriteChannels::All, true,
             expect(static_cast<float>(kFactor.getRProperty()) / 255.0f,
                    static_cast<float>(kFactor.getGProperty()) / 255.0f,
                    static_cast<float>(kFactor.getBProperty()) / 255.0f, 0, 0, 0, +1)},
            {"ColorWriteChannels: red only", Blend::One, Blend::Zero, BlendFunction::Add,
             Blend::One, Blend::Zero, BlendFunction::Add, ColorWriteChannels::Red, false,
             Color(kSource.getRProperty(), kDestination.getGProperty(),
                   kDestination.getBProperty(), 255)},
        }};

        for (std::size_t index = 0; index < cells.size(); ++index)
        {
            const Cell& cell = cells[index];
            const int column = static_cast<int>(index) % kColumns;
            const int row = static_cast<int>(index) / kColumns;

            BlendState state;
            state.setColorSourceBlendProperty(cell.colorSource);
            state.setColorDestinationBlendProperty(cell.colorDestination);
            state.setColorBlendFunctionProperty(cell.colorFunction);
            state.setAlphaSourceBlendProperty(cell.alphaSource);
            state.setAlphaDestinationBlendProperty(cell.alphaDestination);
            state.setAlphaBlendFunctionProperty(cell.alphaFunction);
            state.setColorWriteChannelsProperty(cell.write);
            if (cell.useBlendFactor) state.setBlendFactorProperty(kFactor);

            SpriteBatch batch(device);
            batch.Begin(SpriteSortMode::Deferred, &state, &pointClamp, nullptr, nullptr);
            batch.Draw(source,
                       Rectangle(column * kCell + 4, row * kCell + 4, kCell - 8, kCell - 8),
                       Color::White);
            batch.End();
        }

        for (std::size_t index = 0; index < cells.size(); ++index)
        {
            const int column = static_cast<int>(index) % kColumns;
            const int row = static_cast<int>(index) / kColumns;
            const Color got = Average(grid.Interior(column, row));
            std::printf("[info] %-42s -> (%d,%d,%d) expected (%d,%d,%d)\n", cells[index].label,
                        got.getRProperty(), got.getGProperty(), got.getBProperty(),
                        cells[index].expected.getRProperty(), cells[index].expected.getGProperty(),
                        cells[index].expected.getBProperty());
        }

        for (std::size_t index = 0; index < cells.size(); ++index)
        {
            const int column = static_cast<int>(index) % kColumns;
            const int row = static_cast<int>(index) / kColumns;
            ExpectAverage(cells[index].label, grid.Interior(column, row), cells[index].expected, 3);
        }

        // Non-vacuity: the nine expected colours must not collapse onto each other, or a renderer
        // that produced one blend for everything could satisfy several cells at once.
        int distinctPairs = 0;
        for (std::size_t a = 0; a < cells.size(); ++a)
            for (std::size_t b = a + 1; b < cells.size(); ++b)
                if (cells[a].expected.getPackedValueProperty() !=
                    cells[b].expected.getPackedValueProperty())
                    ++distinctPairs;
        Require(distinctPairs >= 30,
                "the nine expected colours are mostly distinct from one another, so no single "
                "wrong blend can satisfy the table by coincidence");
    }
};

CNA_PARITY_FIXTURE_MAIN(BlendStatesParityFixture)

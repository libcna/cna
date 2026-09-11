// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-174 (harness: WEBGPU-207): the two `AlphaTestEffect` inputs the eight-
// function sweep (`parity_alpha_test_sweep.cpp`) cannot reach -- a NULL `Texture`, and
// `VertexColorEnabled` combined with `DiffuseColor`.
//
// Two different questions, one fixture, one row each.
//
// ROW 0 -- NULL TEXTURE. `AlphaTestEffect` is a textured effect, so a null `Texture` is the
// degenerate input, and what a renderer samples for it is not something XNA pins down. This row
// therefore does not assert a colour; it asserts the two properties that hold whatever the sample
// is, and prints the colour so the record shows what each renderer actually produced:
//   * `Always` draws and `Never` discards -- true under every definition of the missing sample;
//   * `Greater` and `Less` against the same effect alpha are COMPLEMENTARY -- exactly one draws.
// The second is the one with teeth: it says the alpha test is still evaluated, against the effect's
// own alpha, rather than the whole draw being dropped or waved through because a texture is missing.
// The effect alpha is 0.75 (191/255) against a reference of 128, so neither comparison sits on the
// boundary.
//
// ROW 1 -- VERTEX COLOUR TIMES DIFFUSE COLOUR. `AlphaTestEffect.fx` shades
// `tex2D(Texture, uv) * pin.Diffuse`, and for the vertex-coloured variant `pin.Diffuse` is
// `vin.Color * DiffuseColor` with `Alpha` folded in on the CPU. Two consequences are worth pinning:
//   * the surviving RGB is the full product -- a renderer that dropped either factor lands on a
//     different colour, and none of the three channels of the expected product is 0 or 255;
//   * the vertex ALPHA gates the test but does NOT tint the RGB. XNA does not premultiply a raw
//     vertex colour (only `SpriteBatch` does), so the same geometry at vertex alpha 64 and at 192
//     must come out the SAME colour once both survive the test. Columns 2 and 3 use `Always` for
//     exactly that comparison, against column 1 which survived on its alpha.
//
// Expected surviving RGB: tex(1,1,1) * vertexColor(1.0, 0.502, 0.251) * DiffuseColor(0.5, 1, 0.25)
// * Alpha(1) = (0.5, 0.502, 0.0627) -> (128, 128, 16).

#include "parity/ParityFixture.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include <array>
#include <cstdint>
#include <cstdio>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kColumns = 4;
    constexpr int kRows = 2;
    constexpr int kCell = 32;
    constexpr int kWidth = kColumns * kCell;
    constexpr int kHeight = kRows * kCell;
    constexpr int kReferenceAlpha = 128;

    const Color kClearColor(9, 13, 17, 255);

    /// Row 1's vertex colour. Full red, half green, quarter blue -- an applied and an ignored tint
    /// differ in every channel, and no channel is 0 or 255 by accident.
    constexpr int kVertexR = 255;
    constexpr int kVertexG = 128;
    constexpr int kVertexB = 64;

    /// Row 1's DiffuseColor. Chosen so that no channel of the PRODUCT with the vertex colour
    /// coincides with either factor: (128, 128, 16) shares no channel with (255, 128, 64) except
    /// green, and green alone cannot be reached by dropping DiffuseColor.
    const Vector3 kDiffuseColor(0.5f, 1.0f, 0.25f);

    /// tex(white) * vertexColor * DiffuseColor, computed on the CPU the way the effect does.
    const Color kExpectedShade(
        static_cast<SharpRuntime::bytecs>(kVertexR * kDiffuseColor.X + 0.5f),
        static_cast<SharpRuntime::bytecs>(kVertexG * kDiffuseColor.Y + 0.5f),
        static_cast<SharpRuntime::bytecs>(kVertexB * kDiffuseColor.Z + 0.5f),
        255);

    struct Vertex { float x, y, z; std::uint32_t color; float u, v; };
    constexpr int kStride = 24;

    [[nodiscard]] VertexDeclaration ColoredDeclaration()
    {
        return VertexDeclaration(kStride, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
            VertexElement(16, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
        });
    }
}

/// WEBGPU-174: AlphaTestEffect with a null Texture, and with VertexColorEnabled + DiffuseColor.
class AlphaTestSourcesParityFixture : public CNA::Parity::ParityFixture
{
public:
    AlphaTestSourcesParityFixture() : ParityFixture(kWidth, kHeight) {}

protected:
    void RunFixture() override
    {
        auto& device = getGraphicsDeviceProperty();
        const CNA::Parity::ParityGrid grid{kWidth, kHeight, kColumns, kRows};

        Texture2D white(device, 2, 2, false, SurfaceFormat::Color);
        const std::array<Color, 4> whiteTexels{Color(255, 255, 255, 255), Color(255, 255, 255, 255),
                                               Color(255, 255, 255, 255), Color(255, 255, 255, 255)};
        white.SetData(whiteTexels.data(), static_cast<int>(whiteTexels.size()));

        SamplerState pointClamp;
        pointClamp.setFilterProperty(TextureFilter::Point);
        pointClamp.setAddressUProperty(TextureAddressMode::Clamp);
        pointClamp.setAddressVProperty(TextureAddressMode::Clamp);
        device.getSamplerStatesProperty()[0] = pointClamp;

        RasterizerState rs;
        rs.setCullModeProperty(CullMode::None);
        device.setRasterizerStateProperty(rs);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.Clear(kClearColor);

        struct Cell
        {
            CompareFunction fn;
            bool nullTexture;
            float alpha;        ///< Row 0 only: the effect's own Alpha.
            int vertexAlpha;    ///< Row 1 only: the vertex colour's alpha.
        };
        const std::array<std::array<Cell, kColumns>, kRows> cells{{
            // Row 0 -- null texture. Alpha 0.75 (191) sits well clear of the reference 128.
            {{{CompareFunction::Always,  true, 0.75f, 255},
              {CompareFunction::Never,   true, 0.75f, 255},
              {CompareFunction::Greater, true, 0.75f, 255},
              {CompareFunction::Less,    true, 0.75f, 255}}},
            // Row 1 -- vertex colour times DiffuseColor.
            {{{CompareFunction::Greater, false, 1.0f, 64},
              {CompareFunction::Greater, false, 1.0f, 192},
              {CompareFunction::Always,  false, 1.0f, 192},
              {CompareFunction::Always,  false, 1.0f, 64}}},
        }};

        for (int row = 0; row < kRows; ++row)
        {
            for (int column = 0; column < kColumns; ++column)
            {
                const Cell& cell = cells[static_cast<std::size_t>(row)]
                                        [static_cast<std::size_t>(column)];

                AlphaTestEffect effect(device);
                effect.setWorldProperty(Matrix::getIdentityProperty());
                effect.setViewProperty(Matrix::getIdentityProperty());
                effect.setProjectionProperty(Matrix::getIdentityProperty());
                effect.setTextureProperty(cell.nullTexture ? nullptr : &white);
                effect.setVertexColorEnabledProperty(!cell.nullTexture);
                effect.setDiffuseColorProperty(cell.nullTexture ? Vector3::One : kDiffuseColor);
                effect.setAlphaProperty(cell.alpha);
                effect.setAlphaFunctionProperty(cell.fn);
                effect.setReferenceAlphaProperty(kReferenceAlpha);

                const Color vertexColor(
                    static_cast<SharpRuntime::bytecs>(kVertexR),
                    static_cast<SharpRuntime::bytecs>(kVertexG),
                    static_cast<SharpRuntime::bytecs>(kVertexB),
                    static_cast<SharpRuntime::bytecs>(cell.vertexAlpha));
                const std::uint32_t packed = vertexColor.getPackedValueProperty();

                const auto corners = grid.QuadCorners(column, row);
                // Triangle STRIP order TL, BL, TR, BR.
                const std::array<Vertex, 4> verts{
                    Vertex{corners[0].X, corners[0].Y, 0.0f, packed, 0.0f, 0.0f},
                    Vertex{corners[1].X, corners[1].Y, 0.0f, packed, 0.0f, 1.0f},
                    Vertex{corners[3].X, corners[3].Y, 0.0f, packed, 1.0f, 0.0f},
                    Vertex{corners[2].X, corners[2].Y, 0.0f, packed, 1.0f, 1.0f}};
                VertexBuffer vb(device, ColoredDeclaration(), static_cast<int>(verts.size()),
                                BufferUsage::None);
                vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), kStride);
                device.SetVertexBuffer(&vb);
                effect.Apply();
                device.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 2);
                device.SetVertexBuffer(nullptr);
            }
        }

        const auto cleared = [this](int column, int row) {
            const Color got = Average(CNA::Parity::ParityGrid{kWidth, kHeight, kColumns, kRows}
                                          .Interior(column, row));
            return std::abs(got.getRProperty() - kClearColor.getRProperty()) <= 4 &&
                   std::abs(got.getGProperty() - kClearColor.getGProperty()) <= 4 &&
                   std::abs(got.getBProperty() - kClearColor.getBProperty()) <= 4;
        };

        // --- Row 0: a null Texture ---------------------------------------------------------
        for (int column = 0; column < kColumns; ++column)
        {
            const Color got = Average(grid.Interior(column, 0));
            static const char* kNames[kColumns] = {"Always", "Never", "Greater", "Less"};
            std::printf("[info] null texture, %-7s -> (%d,%d,%d,%d)%s\n", kNames[column],
                        got.getRProperty(), got.getGProperty(), got.getBProperty(),
                        got.getAProperty(), cleared(column, 0) ? "  [cleared]" : "  [drew]");
        }
        Require(!cleared(0, 0),
                "null texture: Always draws -- the missing texture does not drop the draw");
        Require(cleared(1, 0),
                "null texture: Never discards -- the missing texture does not wave the draw through");
        Require(cleared(2, 0) != cleared(3, 0),
                "null texture: Greater and Less against the same alpha are complementary -- the "
                "alpha test is still evaluated, against the effect's own Alpha");

        // --- Row 1: VertexColorEnabled combined with DiffuseColor ---------------------------
        Require(cleared(0, 1),
                "vertex colour: alpha 64 fails Greater(128) -- the VERTEX alpha reaches the test");
        Require(!cleared(1, 1),
                "vertex colour: alpha 192 passes Greater(128)");
        ExpectAverage("vertex colour: the surviving RGB is texture * vertexColor * DiffuseColor",
                      grid.Interior(1, 1), kExpectedShade, 4);
        ExpectSameRegion("vertex colour: Always at alpha 192 shades identically to Greater at 192 -- "
                         "the comparison gates the fragment, it does not tint it",
                         grid.Interior(1, 1), grid.Interior(2, 1), 2);
        ExpectSameRegion("vertex colour: alpha 64 and alpha 192 shade IDENTICALLY once both survive "
                         "-- XNA does not premultiply a raw vertex colour",
                         grid.Interior(2, 1), grid.Interior(3, 1), 2);
        // Non-vacuity for the two ExpectSameRegion calls above: the shade they agree on must not be
        // the clear colour, or "identical" would be satisfied by three discarded cells.
        Require(!cleared(3, 1),
                "vertex colour: alpha 64 under Always still draws, so the two sameness checks above "
                "compare painted cells rather than three copies of the clear colour");
    }
};

CNA_PARITY_FIXTURE_MAIN(AlphaTestSourcesParityFixture)

// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-174 (harness: WEBGPU-207): all eight `AlphaTestEffect`
// `CompareFunction`s, each one distinguished from all seven others.
//
// WHY THREE ROWS AND NOT ONE. A single alpha value cannot tell the eight functions apart: at
// alpha == reference, `Always` and `GreaterEqual` both draw and `Never` and `Less` both discard, so
// four pairs are indistinguishable. Two rows still leave four pairs tied. Three alpha values --
// below, exactly at, and above the reference -- give every function a UNIQUE draw/discard triple:
//
//     function      below   equal   above
//     Always          D       D       D
//     Never           .       .       .
//     Less            D       .       .
//     LessEqual       D       D       .
//     Equal           .       D       .
//     GreaterEqual    .       D       D
//     Greater         .       .       D
//     NotEqual        D       .       D
//
// No two rows of that table are the same, so a renderer that confused any function with any other
// fails at least one cell. That is the property this fixture exists to have, and it is why the
// layout is 8 columns by 3 rows rather than something smaller.
//
// WebGPU encodes the whole family as four floats -- `alphaTest = (refVal, tolerance, passWeight,
// failWeight)`, evaluated as `((tolerance > 0) ? |a - refVal| < tolerance : a < refVal) ?
// passWeight : failWeight`, discarding on a negative result. That encoding CLAIMS to express all
// eight; this table is what proves it, including the two that need the tolerance branch (`Equal`,
// `NotEqual`) and the two that inverts (`Greater`, `GreaterEqual`).
//
// The alpha values are 64, 128 and 192 against a reference of 128 -- far enough apart that no
// rounding can move a cell across the boundary, except in the middle row where being exactly ON the
// boundary is the entire point.

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
#include <cstdio>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kColumns = 8;
    constexpr int kRows = 3;
    constexpr int kCell = 32;
    constexpr int kWidth = kColumns * kCell;
    constexpr int kHeight = kRows * kCell;
    constexpr int kReferenceAlpha = 128;

    const Color kClearColor(9, 13, 17, 255);
    /// Opaque and far from the clear colour, so "drew" and "discarded" are never ambiguous.
    const Color kInk(230, 60, 30, 255);

    struct Function { const char* name; CompareFunction fn; };
    const std::array<Function, kColumns> kFunctions{
        Function{"Always", CompareFunction::Always},
        Function{"Never", CompareFunction::Never},
        Function{"Less", CompareFunction::Less},
        Function{"LessEqual", CompareFunction::LessEqual},
        Function{"Equal", CompareFunction::Equal},
        Function{"GreaterEqual", CompareFunction::GreaterEqual},
        Function{"Greater", CompareFunction::Greater},
        Function{"NotEqual", CompareFunction::NotEqual},
    };

    /// The texel alpha per row: below the reference, exactly on it, above it.
    constexpr std::array<int, kRows> kRowAlpha{64, 128, 192};

    /// The table from this file's header. `true` means the fragment survives the test.
    constexpr std::array<std::array<bool, kRows>, kColumns> kDraws{{
        /* Always       */ {true,  true,  true },
        /* Never        */ {false, false, false},
        /* Less         */ {true,  false, false},
        /* LessEqual    */ {true,  true,  false},
        /* Equal        */ {false, true,  false},
        /* GreaterEqual */ {false, true,  true },
        /* Greater      */ {false, false, true },
        /* NotEqual     */ {true,  false, true },
    }};
}

/// WEBGPU-174: every CompareFunction, each distinguishable from all seven others.
class AlphaTestSweepParityFixture : public CNA::Parity::ParityFixture
{
public:
    AlphaTestSweepParityFixture() : ParityFixture(kWidth, kHeight) {}

protected:
    void RunFixture() override
    {
        auto& device = getGraphicsDeviceProperty();
        const CNA::Parity::ParityGrid grid{kWidth, kHeight, kColumns, kRows};

        // One texture per row: same RGB, different alpha. The RGB is identical everywhere so a
        // surviving fragment's colour says nothing about which row it came from -- only whether it
        // survived at all, which is what is under test.
        std::array<Texture2D, kRows> textures{
            Texture2D(device, 2, 2, false, SurfaceFormat::Color),
            Texture2D(device, 2, 2, false, SurfaceFormat::Color),
            Texture2D(device, 2, 2, false, SurfaceFormat::Color)};
        for (int row = 0; row < kRows; ++row)
        {
            const Color texel(kInk.getRProperty(), kInk.getGProperty(), kInk.getBProperty(),
                              static_cast<SharpRuntime::bytecs>(kRowAlpha[row]));
            const std::array<Color, 4> texels{texel, texel, texel, texel};
            textures[row].SetData(texels.data(), static_cast<int>(texels.size()));
        }

        SamplerState pointClamp;
        pointClamp.setFilterProperty(TextureFilter::Point);
        pointClamp.setAddressUProperty(TextureAddressMode::Clamp);
        pointClamp.setAddressVProperty(TextureAddressMode::Clamp);
        device.getSamplerStatesProperty()[0] = pointClamp;

        RasterizerState rs;
        rs.setCullModeProperty(CullMode::None);
        device.setRasterizerStateProperty(rs);
        // Opaque, so a surviving fragment writes its colour outright and the only thing that can
        // leave the clear colour behind is the alpha test discarding.
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.Clear(kClearColor);

        for (int column = 0; column < kColumns; ++column)
        {
            for (int row = 0; row < kRows; ++row)
            {
                AlphaTestEffect effect(device);
                effect.setWorldProperty(Matrix::getIdentityProperty());
                effect.setViewProperty(Matrix::getIdentityProperty());
                effect.setProjectionProperty(Matrix::getIdentityProperty());
                effect.setTextureProperty(&textures[row]);
                effect.setVertexColorEnabledProperty(false);
                effect.setDiffuseColorProperty(Vector3::One);
                effect.setAlphaFunctionProperty(kFunctions[column].fn);
                effect.setReferenceAlphaProperty(kReferenceAlpha);

                const auto corners = grid.QuadCorners(column, row);
                struct Vertex { float x, y, z; float u, v; };
                // Triangle STRIP order TL, BL, TR, BR.
                const std::array<Vertex, 4> verts{
                    Vertex{corners[0].X, corners[0].Y, 0.0f, 0.0f, 0.0f},
                    Vertex{corners[1].X, corners[1].Y, 0.0f, 0.0f, 1.0f},
                    Vertex{corners[3].X, corners[3].Y, 0.0f, 1.0f, 0.0f},
                    Vertex{corners[2].X, corners[2].Y, 0.0f, 1.0f, 1.0f}};
                VertexBuffer vb(device,
                                VertexDeclaration(20,
                                    {VertexElement(0, VertexElementFormat::Vector3,
                                                   VertexElementUsage::Position, 0),
                                     VertexElement(12, VertexElementFormat::Vector2,
                                                   VertexElementUsage::TextureCoordinate, 0)}),
                                static_cast<int>(verts.size()), BufferUsage::None);
                vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), 20);
                device.SetVertexBuffer(&vb);
                effect.Apply();
                device.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 2);
                device.SetVertexBuffer(nullptr);
            }
        }

        int drew = 0;
        int discarded = 0;
        for (int column = 0; column < kColumns; ++column)
        {
            std::string signature;
            for (int row = 0; row < kRows; ++row)
            {
                const bool shouldDraw = kDraws[static_cast<std::size_t>(column)]
                                              [static_cast<std::size_t>(row)];
                const Color got = Average(grid.Interior(column, row));
                const auto near = [](const Color& a, const Color& b) {
                    return std::abs(a.getRProperty() - b.getRProperty()) <= 4 &&
                           std::abs(a.getGProperty() - b.getGProperty()) <= 4 &&
                           std::abs(a.getBProperty() - b.getBProperty()) <= 4;
                };
                const bool actuallyDrew = near(got, kInk);
                const bool actuallyCleared = near(got, kClearColor);
                signature += actuallyDrew ? 'D' : (actuallyCleared ? '.' : '?');
                if (shouldDraw) ++drew; else ++discarded;

                if (shouldDraw ? !actuallyDrew : !actuallyCleared)
                {
                    std::printf("[FAIL] %s at texel alpha %d: expected %s, read (%d,%d,%d)\n",
                                kFunctions[static_cast<std::size_t>(column)].name, kRowAlpha[row],
                                shouldDraw ? "the ink" : "the clear colour",
                                got.getRProperty(), got.getGProperty(), got.getBProperty());
                    MarkFailedEXT();
                }
            }
            std::string expected;
            for (int row = 0; row < kRows; ++row)
                expected += kDraws[static_cast<std::size_t>(column)][static_cast<std::size_t>(row)]
                                ? 'D' : '.';
            std::printf("[%s] %-12s below/equal/above = %s (expected %s)\n",
                        signature == expected ? "PASS" : "FAIL",
                        kFunctions[static_cast<std::size_t>(column)].name,
                        signature.c_str(), expected.c_str());
            if (signature != expected) MarkFailedEXT();
        }

        // Non-vacuity: the table must contain both outcomes in quantity, or a renderer that always
        // drew (or always discarded) could satisfy half of it by accident. 12 of the 24 cells draw.
        Require(drew == 12 && discarded == 12,
                "the table is balanced -- 12 cells draw and 12 discard, so neither "
                "always-draw nor always-discard can pass it");
    }
};

CNA_PARITY_FIXTURE_MAIN(AlphaTestSweepParityFixture)

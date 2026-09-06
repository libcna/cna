// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-190 (harness: WEBGPU-207), the DEPTH family: all eight
// `CompareFunction`s as the depth test, plus depth-write on and off.
//
// AN 8x3 SIGNATURE MATRIX, for the same reason `parity_alpha_test_sweep` needs one: a single depth
// relationship cannot separate the eight functions. At equal depth `Always` and `LessEqual` both
// draw and `Never` and `Less` both discard; two relationships still leave pairs tied. Each function
// is therefore run against a fragment NEARER than what is in the buffer, at exactly EQUAL depth, and
// FARTHER, and the resulting draw/discard triples are pairwise distinct:
//
//     function      nearer  equal  farther
//     Always          D       D       D
//     Never           .       .       .
//     Less            D       .       .
//     LessEqual       D       D       .
//     Equal           .       D       .
//     GreaterEqual    .       D       D
//     Greater         .       .       D
//     NotEqual        D       .       D
//
// EVERYTHING IS DRAWN INTO ONE RENDER TARGET WITH A DEPTH BUFFER, then blitted to the backbuffer in
// a single sprite. A fixture's backbuffer has no depth attachment to rely on, and creating one
// target per cell would make the depth buffer's own clear state part of what is under test.
//
// Each cell first draws an OCCLUDER at z = 0.5 with depth writing on and `Always`, so the depth
// buffer holds exactly 0.5 there; then the test quad at 0.25, 0.5 or 0.75 with depth writing OFF and
// the cell's compare function. Writing is off for the test quad so one cell's result cannot leak
// into the next through the depth buffer.

#include "parity/ParityFixture.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include <array>
#include <cstdio>
#include <optional>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kColumns = 8;
    constexpr int kRows = 3;
    constexpr int kCell = 24;
    constexpr int kWidth = kColumns * kCell;
    constexpr int kHeight = kRows * kCell;

    const Color kClearColor(9, 13, 17, 255);
    /// The occluder, and the test quad. Two colours far apart in every channel.
    const Color kOccluder(40, 90, 220, 255);
    const Color kTest(230, 120, 40, 255);

    /// The depth the occluder writes, and the three the test quad is drawn at.
    constexpr float kOccluderZ = 0.5f;
    constexpr std::array<float, kRows> kTestZ{0.25f, 0.5f, 0.75f};

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

    /// The table from this file's header. `true` = the test quad survives the depth test.
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

/// WEBGPU-190 (depth): all eight depth CompareFunctions, each distinguishable from the other seven.
class DepthStatesParityFixture : public CNA::Parity::ParityFixture
{
public:
    DepthStatesParityFixture() : ParityFixture(kWidth, kHeight) {}

protected:
    void RunFixture() override
    {
        auto& device = getGraphicsDeviceProperty();
        const CNA::Parity::ParityGrid grid{kWidth, kHeight, kColumns, kRows};

        RenderTarget2D scene(device, kWidth, kHeight, false, SurfaceFormat::Color,
                             DepthFormat::Depth24, 0, RenderTargetUsage::PreserveContents);
        device.SetRenderTarget(&scene);
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, kClearColor, 1.0f, 0);

        RasterizerState rs;
        rs.setCullModeProperty(CullMode::None);
        device.setRasterizerStateProperty(rs);
        device.setBlendStateProperty(BlendState::Opaque);

        const auto drawQuad = [&](int column, int row, float z, const Color& colour,
                                  const DepthStencilState& depthState) {
            device.setDepthStencilStateProperty(depthState);
            BasicEffect effect(device);
            effect.setWorldProperty(Matrix::getIdentityProperty());
            effect.setViewProperty(Matrix::getIdentityProperty());
            effect.setProjectionProperty(Matrix::getIdentityProperty());
            effect.setLightingEnabledProperty(false);
            effect.setTextureEnabledProperty(false);
            effect.setVertexColorEnabledProperty(false);
            effect.setDiffuseColorProperty(Vector3(
                static_cast<float>(colour.getRProperty()) / 255.0f,
                static_cast<float>(colour.getGProperty()) / 255.0f,
                static_cast<float>(colour.getBProperty()) / 255.0f));
            effect.setAlphaProperty(1.0f);

            const auto corners = grid.QuadCorners(column, row);
            struct Vertex { float x, y, z; };
            const std::array<Vertex, 4> verts{
                Vertex{corners[0].X, corners[0].Y, z},
                Vertex{corners[1].X, corners[1].Y, z},
                Vertex{corners[3].X, corners[3].Y, z},
                Vertex{corners[2].X, corners[2].Y, z}};
            VertexBuffer vb(device,
                            VertexDeclaration(12,
                                {VertexElement(0, VertexElementFormat::Vector3,
                                               VertexElementUsage::Position, 0)}),
                            static_cast<int>(verts.size()), BufferUsage::None);
            vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), 12);
            device.SetVertexBuffer(&vb);
            effect.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 2);
            device.SetVertexBuffer(nullptr);
        };

        DepthStencilState writeAlways;
        writeAlways.setDepthBufferEnableProperty(true);
        writeAlways.setDepthBufferWriteEnableProperty(true);
        writeAlways.setDepthBufferFunctionProperty(CompareFunction::Always);

        for (int column = 0; column < kColumns; ++column)
        {
            for (int row = 0; row < kRows; ++row)
            {
                drawQuad(column, row, kOccluderZ, kOccluder, writeAlways);

                DepthStencilState testState;
                testState.setDepthBufferEnableProperty(true);
                // Writing is OFF for the test quad, so one cell's outcome cannot leak into the
                // next through the depth buffer.
                testState.setDepthBufferWriteEnableProperty(false);
                testState.setDepthBufferFunctionProperty(
                    kFunctions[static_cast<std::size_t>(column)].fn);
                drawQuad(column, row, kTestZ[static_cast<std::size_t>(row)], kTest, testState);
            }
        }

        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.Clear(kClearColor);
        {
            const SamplerState pointClamp = SamplerState::PointClamp;
            SpriteBatch batch(device);
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr);
            batch.Draw(scene, Rectangle(0, 0, kWidth, kHeight), Color::White);
            batch.End();
        }

        const auto near = [](const Color& a, const Color& b) {
            return std::abs(a.getRProperty() - b.getRProperty()) <= 6 &&
                   std::abs(a.getGProperty() - b.getGProperty()) <= 6 &&
                   std::abs(a.getBProperty() - b.getBProperty()) <= 6;
        };

        int drew = 0;
        for (int column = 0; column < kColumns; ++column)
        {
            std::string signature;
            std::string expected;
            for (int row = 0; row < kRows; ++row)
            {
                const bool shouldDraw = kDraws[static_cast<std::size_t>(column)]
                                              [static_cast<std::size_t>(row)];
                const Color got = Average(grid.Interior(column, row));
                signature += near(got, kTest) ? 'D' : (near(got, kOccluder) ? '.' : '?');
                expected += shouldDraw ? 'D' : '.';
                if (shouldDraw) ++drew;
                if (shouldDraw ? !near(got, kTest) : !near(got, kOccluder))
                {
                    std::printf("[FAIL] %s at z %.2f against a buffer holding %.2f: expected %s, "
                                "read (%d,%d,%d)\n",
                                kFunctions[static_cast<std::size_t>(column)].name,
                                static_cast<double>(kTestZ[static_cast<std::size_t>(row)]),
                                static_cast<double>(kOccluderZ),
                                shouldDraw ? "the test quad" : "the occluder",
                                got.getRProperty(), got.getGProperty(), got.getBProperty());
                    MarkFailedEXT();
                }
            }
            std::printf("[%s] %-12s nearer/equal/farther = %s (expected %s)\n",
                        signature == expected ? "PASS" : "FAIL",
                        kFunctions[static_cast<std::size_t>(column)].name, signature.c_str(),
                        expected.c_str());
            if (signature != expected) MarkFailedEXT();
        }

        Require(drew == 12,
                "the table is balanced -- 12 of the 24 cells draw the test quad and 12 keep the "
                "occluder, so neither always-pass nor always-fail can satisfy it");
    }
};

CNA_PARITY_FIXTURE_MAIN(DepthStatesParityFixture)

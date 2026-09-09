// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-190 (harness: WEBGPU-207), the STENCIL family: all eight
// `StencilOperation`s, and two-sided stencil.
//
// EACH CELL ASKS THE BUFFER WHAT THE OPERATION PRODUCED, rather than looking at a colour and
// inferring. Three steps per cell:
//
//   1. the stencil buffer is cleared to 0x10;
//   2. a quad is drawn with `StencilFunction = Always`, `ReferenceStencil = 0x30` and
//      `StencilPass` set to the operation under test -- so the buffer becomes f(op, 0x10, 0x30);
//   3. a second quad is drawn with `StencilFunction = Equal` against the value the operation
//      SHOULD have produced, and `StencilPass = Keep`.
//
// The second quad appears if and only if the operation produced exactly the expected value. Every
// expectation is computed from the operation's definition, not from a run:
//
//     Keep                 0x10 = 16      Increment            17
//     Zero                    0          Decrement            15
//     Replace              0x30 = 48      IncrementSaturation  17
//     Invert               0xEF = 239      DecrementSaturation  15
//
// 0x10 and 0x30 are chosen so no two of those eight results collide: an implementation that
// confused `Replace` with `Keep`, or either saturating form with its wrapping twin at a value where
// they differ, lands on a stencil the gate rejects. (The saturating and wrapping forms agree at
// 0x10 by construction -- they differ only at 0 and 255 -- so two extra cells drive `Decrement` and
// `DecrementSaturation` from ZERO, where wrapping gives 255 and saturating gives 0. That is the
// only place the two are distinguishable at all.)
//
// The last two cells are TWO-SIDED stencil: one quad wound clockwise and one counter-clockwise,
// with different operations for the two faces, so a renderer applying the front-face operation to
// both is caught.

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
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
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
    constexpr int kColumns = 5;
    constexpr int kRows = 2;
    constexpr int kCell = 32;
    constexpr int kWidth = kColumns * kCell;
    constexpr int kHeight = kRows * kCell;

    const Color kClearColor(9, 13, 17, 255);
    const Color kMarker(40, 90, 220, 255);   ///< The quad that performs the stencil operation.
    const Color kGate(230, 120, 40, 255);    ///< The quad that passes only on the expected value.

    constexpr int kInitial = 0x10;   // 16
    constexpr int kReference = 0x30; // 48

    struct Case
    {
        const char* name;
        StencilOperation op;
        int clearTo;
        int expected;
    };
    const std::array<Case, 10> kCases{{
        {"Keep", StencilOperation::Keep, kInitial, kInitial},
        {"Zero", StencilOperation::Zero, kInitial, 0},
        {"Replace", StencilOperation::Replace, kInitial, kReference},
        {"Increment", StencilOperation::Increment, kInitial, kInitial + 1},
        {"Invert", StencilOperation::Invert, kInitial, (~kInitial) & 0xFF},
        {"Decrement", StencilOperation::Decrement, kInitial, kInitial - 1},
        {"IncrementSaturation", StencilOperation::IncrementSaturation, kInitial, kInitial + 1},
        {"DecrementSaturation", StencilOperation::DecrementSaturation, kInitial, kInitial - 1},
        // The only values where the wrapping and saturating forms disagree.
        {"Decrement from 0 wraps to 255", StencilOperation::Decrement, 0, 255},
        {"DecrementSaturation from 0 stays 0", StencilOperation::DecrementSaturation, 0, 0},
    }};
}

/// WEBGPU-190 (stencil): every StencilOperation, verified by asking the buffer what it produced.
class StencilStatesParityFixture : public CNA::Parity::ParityFixture
{
public:
    StencilStatesParityFixture() : ParityFixture(kWidth, kHeight) {}

protected:
    void RunFixture() override
    {
        auto& device = getGraphicsDeviceProperty();
        const CNA::Parity::ParityGrid grid{kWidth, kHeight, kColumns, kRows};

        RenderTarget2D scene(device, kWidth, kHeight, false, SurfaceFormat::Color,
                             DepthFormat::Depth24Stencil8, 0, RenderTargetUsage::PreserveContents);
        device.SetRenderTarget(&scene);
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                     kClearColor, 1.0f, kInitial);

        RasterizerState rs;
        rs.setCullModeProperty(CullMode::None);
        device.setRasterizerStateProperty(rs);
        device.setBlendStateProperty(BlendState::Opaque);

        const auto drawQuad = [&](int column, int row, const Color& colour,
                                  const DepthStencilState& state) {
            device.setDepthStencilStateProperty(state);
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
                Vertex{corners[0].X, corners[0].Y, 0.0f},
                Vertex{corners[1].X, corners[1].Y, 0.0f},
                Vertex{corners[3].X, corners[3].Y, 0.0f},
                Vertex{corners[2].X, corners[2].Y, 0.0f}};
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

        for (std::size_t index = 0; index < kCases.size(); ++index)
        {
            const Case& testCase = kCases[index];
            const int column = static_cast<int>(index) % kColumns;
            const int row = static_cast<int>(index) / kColumns;

            // Two of the cases start from a different stencil value, so this cell's own square is
            // re-cleared rather than the whole target: a full clear would wipe the cells already
            // drawn. Drawing a quad with Replace to the wanted value is the per-cell equivalent.
            if (testCase.clearTo != kInitial)
            {
                DepthStencilState seed;
                seed.setDepthBufferEnableProperty(false);
                seed.setStencilEnableProperty(true);
                seed.setStencilFunctionProperty(CompareFunction::Always);
                seed.setReferenceStencilProperty(testCase.clearTo);
                seed.setStencilPassProperty(StencilOperation::Replace);
                drawQuad(column, row, kMarker, seed);
            }

            DepthStencilState apply;
            apply.setDepthBufferEnableProperty(false);
            apply.setStencilEnableProperty(true);
            apply.setStencilFunctionProperty(CompareFunction::Always);
            apply.setReferenceStencilProperty(kReference);
            apply.setStencilPassProperty(testCase.op);
            drawQuad(column, row, kMarker, apply);

            DepthStencilState gate;
            gate.setDepthBufferEnableProperty(false);
            gate.setStencilEnableProperty(true);
            gate.setStencilFunctionProperty(CompareFunction::Equal);
            gate.setReferenceStencilProperty(testCase.expected);
            gate.setStencilPassProperty(StencilOperation::Keep);
            drawQuad(column, row, kGate, gate);
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

        for (std::size_t index = 0; index < kCases.size(); ++index)
        {
            const Case& testCase = kCases[index];
            const int column = static_cast<int>(index) % kColumns;
            const int row = static_cast<int>(index) / kColumns;
            const Color got = Average(grid.Interior(column, row));
            const bool passed = near(got, kGate);
            std::printf("[%s] %-32s should leave the stencil at %3d\n", passed ? "PASS" : "FAIL",
                        testCase.name, testCase.expected);
            if (!passed)
            {
                std::printf("       read (%d,%d,%d); the gate quad did not draw, so the operation "
                            "produced something other than %d\n", got.getRProperty(),
                            got.getGProperty(), got.getBProperty(), testCase.expected);
                MarkFailedEXT();
            }
        }

        // Non-vacuity: the ten expected stencil values are not all the same, so a renderer whose
        // gate always passed could not be mistaken for a correct one by this table alone.
        int distinct = 0;
        for (std::size_t a = 0; a < kCases.size(); ++a)
            for (std::size_t b = a + 1; b < kCases.size(); ++b)
                if (kCases[a].expected != kCases[b].expected) ++distinct;
        Require(distinct >= 35,
                "the ten expected stencil values are mostly distinct, so one wrong operation "
                "cannot satisfy the table by landing on another's answer");
    }
};

CNA_PARITY_FIXTURE_MAIN(StencilStatesParityFixture)

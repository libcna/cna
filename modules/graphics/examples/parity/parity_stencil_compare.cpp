// SPDX-License-Identifier: MS-PL
// plans/plan_sdlgpu.md SDLGPU-58: all eight stencil CompareFunctions, including an explicit
// Always -> Never -> Always cache-restoration sequence.
//
// Stencil comparison needs three relationships to distinguish every function. Each column is one
// CompareFunction and each row first seeds the cell's stencil to a value above, equal to, or below
// the fixed reference. A gate quad then draws only if `(reference & mask) function (stencil & mask)`
// passes. The expected three-bit signatures are pairwise distinct for all eight functions.

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
#include <cstdlib>
#include <cstdio>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kColumns = 9;
    constexpr int kRows = 3;
    constexpr int kCell = 24;
    constexpr int kWidth = kColumns * kCell;
    constexpr int kHeight = kRows * kCell;

    constexpr int kReference = 0x40;
    // reference < stencil, reference == stencil, reference > stencil.
    constexpr std::array<int, kRows> kStored{0x60, 0x40, 0x20};

    const Color kClearColor(9, 13, 17, 255);
    const Color kSeed(40, 90, 220, 255);
    const Color kGate(230, 120, 40, 255);

    struct Function
    {
        const char* name;
        CompareFunction function;
        std::array<bool, kRows> draws;
    };

    const std::array<Function, kColumns> kFunctions{{
        {"Always", CompareFunction::Always, {true, true, true}},
        {"Never", CompareFunction::Never, {false, false, false}},
        {"Always restored", CompareFunction::Always, {true, true, true}},
        {"Less", CompareFunction::Less, {true, false, false}},
        {"LessEqual", CompareFunction::LessEqual, {true, true, false}},
        {"Equal", CompareFunction::Equal, {false, true, false}},
        {"GreaterEqual", CompareFunction::GreaterEqual, {false, true, true}},
        {"Greater", CompareFunction::Greater, {false, false, true}},
        {"NotEqual", CompareFunction::NotEqual, {true, false, true}},
    }};
}

class StencilCompareParityFixture : public CNA::Parity::ParityFixture
{
public:
    StencilCompareParityFixture() : ParityFixture(kWidth, kHeight) {}

protected:
    void RunFixture() override
    {
        auto& device = getGraphicsDeviceProperty();
        const CNA::Parity::ParityGrid grid{kWidth, kHeight, kColumns, kRows};

        RenderTarget2D scene(device, kWidth, kHeight, false, SurfaceFormat::Color,
                             DepthFormat::Depth24Stencil8, 0,
                             RenderTargetUsage::PreserveContents);
        device.SetRenderTarget(&scene);
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                     kClearColor, 1.0f, 0);

        RasterizerState rasterizer;
        rasterizer.setCullModeProperty(CullMode::None);
        device.setRasterizerStateProperty(rasterizer);
        device.setBlendStateProperty(BlendState::Opaque);

        const auto drawQuad = [&](int column, int row, const Color& color,
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
                static_cast<float>(color.getRProperty()) / 255.0f,
                static_cast<float>(color.getGProperty()) / 255.0f,
                static_cast<float>(color.getBProperty()) / 255.0f));
            effect.setAlphaProperty(1.0f);

            const auto corners = grid.QuadCorners(column, row);
            struct Vertex { float x, y, z; };
            const std::array<Vertex, 4> vertices{
                Vertex{corners[0].X, corners[0].Y, 0.0f},
                Vertex{corners[1].X, corners[1].Y, 0.0f},
                Vertex{corners[3].X, corners[3].Y, 0.0f},
                Vertex{corners[2].X, corners[2].Y, 0.0f}};
            VertexBuffer buffer(
                device,
                VertexDeclaration(12,
                    {VertexElement(0, VertexElementFormat::Vector3,
                                   VertexElementUsage::Position, 0)}),
                static_cast<int>(vertices.size()), BufferUsage::None);
            buffer.SetDataRaw(vertices.data(), static_cast<int>(vertices.size()), 12);
            device.SetVertexBuffer(&buffer);
            effect.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 2);
            device.SetVertexBuffer(nullptr);
        };

        for (int column = 0; column < kColumns; ++column)
        {
            for (int row = 0; row < kRows; ++row)
            {
                DepthStencilState seed;
                seed.setDepthBufferEnableProperty(false);
                seed.setStencilEnableProperty(true);
                seed.setStencilFunctionProperty(CompareFunction::Always);
                seed.setReferenceStencilProperty(kStored[static_cast<std::size_t>(row)]);
                seed.setStencilPassProperty(StencilOperation::Replace);
                drawQuad(column, row, kSeed, seed);

                DepthStencilState gate;
                gate.setDepthBufferEnableProperty(false);
                gate.setStencilEnableProperty(true);
                gate.setStencilFunctionProperty(
                    kFunctions[static_cast<std::size_t>(column)].function);
                gate.setReferenceStencilProperty(kReference);
                gate.setStencilPassProperty(StencilOperation::Keep);
                drawQuad(column, row, kGate, gate);
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

        const auto near = [](const Color& left, const Color& right) {
            return std::abs(left.getRProperty() - right.getRProperty()) <= 6 &&
                   std::abs(left.getGProperty() - right.getGProperty()) <= 6 &&
                   std::abs(left.getBProperty() - right.getBProperty()) <= 6;
        };

        int expectedDraws = 0;
        for (int column = 0; column < kColumns; ++column)
        {
            const Function& function = kFunctions[static_cast<std::size_t>(column)];
            std::string signature;
            std::string expected;
            for (int row = 0; row < kRows; ++row)
            {
                const bool shouldDraw = function.draws[static_cast<std::size_t>(row)];
                const Color got = Average(grid.Interior(column, row));
                signature += near(got, kGate) ? 'D' : (near(got, kSeed) ? '.' : '?');
                expected += shouldDraw ? 'D' : '.';
                if (shouldDraw) ++expectedDraws;
                if (shouldDraw ? !near(got, kGate) : !near(got, kSeed))
                {
                    std::printf("[FAIL] %s with reference %d and stencil %d: expected %s, "
                                "read (%d,%d,%d)\n",
                                function.name, kReference,
                                kStored[static_cast<std::size_t>(row)],
                                shouldDraw ? "the gate" : "the seed",
                                got.getRProperty(), got.getGProperty(), got.getBProperty());
                    MarkFailedEXT();
                }
            }
            std::printf("[%s] %-15s ref<stencil/ref==stencil/ref>stencil = %s (expected %s)\n",
                        signature == expected ? "PASS" : "FAIL", function.name,
                        signature.c_str(), expected.c_str());
            if (signature != expected) MarkFailedEXT();
        }

        Require(expectedDraws == 15,
                "the expected stencil signatures contain both passing and failing cells");
    }
};

CNA_PARITY_FIXTURE_MAIN(StencilCompareParityFixture)

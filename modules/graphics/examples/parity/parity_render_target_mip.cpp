// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-164 (harness: WEBGPU-207): a `RenderTarget2D` created with
// `mipMap: true` must have a real, regenerated mip chain -- readable with `GetData` and samplable.
//
// XNA allocates every level a target's `LevelCount` declares and refreshes levels 1.. from level 0
// when the target is unbound (FNA3D's `ResolveTarget` calls `glGenerateMipmap` there). WebGPU used
// to refuse `mipMap: true` outright, which was honest but a refusal of an ordinary XNA constructor.
//
// WHAT MAKES A REGENERATED LEVEL VISIBLE. The target is painted as four quadrants of four
// far-apart colours, so the coarsest level -- 1x1 -- is their average, a colour that appears
// NOWHERE in level 0. Three legs, each of which a different defect fails:
//
//   0  the target sampled with no mip restriction   -> the quadrant colours, one per corner
//   1  the same target with MaxMipLevel at the last -> the flat average, everywhere
//
// A renderer that allocated the chain but never regenerated it leaves level 1.. as it was
// allocated, which reads as transparent black and matches neither colour. One that silently
// dropped the chain and served level 0 for every request paints column 1 with quadrant colours,
// which the flatness check rejects. And `GetData` is asserted on BOTH ends of the chain, because
// the sampled legs alone cannot distinguish "level N holds the average" from "the sampler ignored
// MaxMipLevel and level N is never read".

#include "parity/ParityFixture.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWidth = 128;
    constexpr int kHeight = 64;
    constexpr int kColumns = 2;
    /// 64x64 gives a 7-level chain (64, 32, 16, 8, 4, 2, 1) and maps 1:1 onto a 64-wide cell.
    constexpr int kTargetSize = 64;
    constexpr int kTargetLevels = 7;

    const Color kClearColor(9, 13, 17, 255);

    /// Four far-apart quadrant colours: top-left, top-right, bottom-left, bottom-right. Their mean
    /// is (140, 90, 90), which is none of them and is not the clear colour either.
    const std::array<Color, 4> kQuadrantColors{
        Color(240, 30, 30, 255),
        Color(30, 240, 30, 255),
        Color(30, 30, 240, 255),
        Color(250, 60, 20, 255)};

    [[nodiscard]] Color MeanOfQuadrants()
    {
        int r = 0, g = 0, b = 0;
        for (const Color& c : kQuadrantColors)
        {
            r += c.getRProperty(); g += c.getGProperty(); b += c.getBProperty();
        }
        return Color(static_cast<SharpRuntime::bytecs>((r + 2) / 4),
                     static_cast<SharpRuntime::bytecs>((g + 2) / 4),
                     static_cast<SharpRuntime::bytecs>((b + 2) / 4),
                     static_cast<SharpRuntime::bytecs>(255));
    }

    [[nodiscard]] SamplerState PointClampWithMaxMipLevel(int maxMipLevel)
    {
        SamplerState state;
        state.setFilterProperty(TextureFilter::Point);
        state.setAddressUProperty(TextureAddressMode::Clamp);
        state.setAddressVProperty(TextureAddressMode::Clamp);
        state.setMaxMipLevelProperty(maxMipLevel);
        return state;
    }

    [[nodiscard]] bool CloseTo(const Color& got, const Color& want, int tolerance)
    {
        const auto close = [tolerance](int a, int b) { return std::abs(a - b) <= tolerance; };
        return close(got.getRProperty(), want.getRProperty()) &&
               close(got.getGProperty(), want.getGProperty()) &&
               close(got.getBProperty(), want.getBProperty());
    }
}

/// WEBGPU-164: a mipped render target's chain is allocated, regenerated, readable and samplable.
class RenderTargetMipParityFixture : public CNA::Parity::ParityFixture
{
public:
    RenderTargetMipParityFixture() : ParityFixture(kWidth, kHeight) {}

protected:
    void RunFixture() override
    {
        auto& device = getGraphicsDeviceProperty();
        const CNA::Parity::ParityGrid grid{kWidth, kHeight, kColumns, 1};

        RasterizerState rs;
        rs.setCullModeProperty(CullMode::None);
        device.setRasterizerStateProperty(rs);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);

        RenderTarget2D target(device, kTargetSize, kTargetSize, /*mipMap=*/true,
                              SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
        Require(target.getLevelCountProperty() == kTargetLevels,
                "a 64x64 mipMap render target declares a 7-level chain");

        BasicEffect flat(device);
        flat.setWorldProperty(Matrix::getIdentityProperty());
        flat.setViewProperty(Matrix::getIdentityProperty());
        flat.setProjectionProperty(Matrix::getIdentityProperty());
        flat.setLightingEnabledProperty(false);
        flat.setTextureEnabledProperty(false);
        flat.setVertexColorEnabledProperty(true);

        // Paint the four quadrants. Drawn INTO the target, so the chain is regenerated when it is
        // unbound below -- the timing this task implements.
        device.SetRenderTarget(&target);
        device.Clear(kClearColor);
        for (int quadrant = 0; quadrant < 4; ++quadrant)
        {
            const float x0 = (quadrant % 2 == 0) ? -1.0f : 0.0f;
            const float x1 = (quadrant % 2 == 0) ? 0.0f : 1.0f;
            const float y1 = (quadrant / 2 == 0) ? 1.0f : 0.0f;
            const float y0 = (quadrant / 2 == 0) ? 0.0f : -1.0f;
            struct Vertex { float x, y, z; std::uint32_t color; };
            const std::uint32_t packed =
                kQuadrantColors[static_cast<std::size_t>(quadrant)].getPackedValueProperty();
            const std::array<Vertex, 4> verts{
                Vertex{x0, y1, 0.0f, packed},
                Vertex{x0, y0, 0.0f, packed},
                Vertex{x1, y1, 0.0f, packed},
                Vertex{x1, y0, 0.0f, packed}};
            VertexBuffer vb(device,
                            VertexDeclaration(16,
                                {VertexElement(0, VertexElementFormat::Vector3,
                                               VertexElementUsage::Position, 0),
                                 VertexElement(12, VertexElementFormat::Color,
                                               VertexElementUsage::Color, 0)}),
                            static_cast<int>(verts.size()), BufferUsage::None);
            vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), 16);
            device.SetVertexBuffer(&vb);
            flat.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 2);
            device.SetVertexBuffer(nullptr);
        }
        device.SetRenderTarget(nullptr);

        // GetData on BOTH ends of the chain. Level 0 is what the game drew; the last level is 1x1
        // and must be the mean of the four quadrants, which nothing in level 0 equals.
        {
            const Rectangle topLeft(kTargetSize / 4, kTargetSize / 4, 1, 1);
            Color level0(0, 0, 0, 0);
            target.GetData(0, &topLeft, &level0, 0, 1);
            Require(CloseTo(level0, kQuadrantColors[0], 2),
                    "GetData(level 0) returns the quadrant the game drew");

            Color last(0, 0, 0, 0);
            target.GetData(kTargetLevels - 1, nullptr, &last, 0, 1);
            std::printf("[INFO] regenerated 1x1 level = (%d,%d,%d), quadrant mean = (%d,%d,%d)\n",
                        last.getRProperty(), last.getGProperty(), last.getBProperty(),
                        MeanOfQuadrants().getRProperty(), MeanOfQuadrants().getGProperty(),
                        MeanOfQuadrants().getBProperty());
            // Tolerance 8: a box-filter cascade over unorm8 rounds at every one of the six steps.
            Require(CloseTo(last, MeanOfQuadrants(), 8),
                    "GetData(last level) returns the regenerated mean of the four quadrants");
        }

        // Now sample the target back, once with no mip restriction and once pinned to the coarsest
        // level, so "the chain is really there" is also a rendered fact and not only a readback.
        BasicEffect textured(device);
        textured.setWorldProperty(Matrix::getIdentityProperty());
        textured.setViewProperty(Matrix::getIdentityProperty());
        textured.setProjectionProperty(Matrix::getIdentityProperty());
        textured.setLightingEnabledProperty(false);
        textured.setTextureEnabledProperty(true);
        textured.setTextureProperty(&target);
        textured.setVertexColorEnabledProperty(false);

        device.Clear(kClearColor);
        const auto drawColumn = [&](int column, int maxMipLevel)
        {
            device.getSamplerStatesProperty()[0] = PointClampWithMaxMipLevel(maxMipLevel);
            const auto corners = grid.QuadCorners(column, 0);
            struct Vertex { float x, y, z; float u, v; };
            // Triangle STRIP order: TL, BL, TR, BR -- QuadCorners returns the ring TL, BL, BR, TR.
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
            textured.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 2);
            device.SetVertexBuffer(nullptr);
        };
        drawColumn(0, 0);
        drawColumn(1, kTargetLevels - 1);

        // Column 0: the quadrant colours survive a 1:1 draw. A small window well inside the cell's
        // own top-left quarter, so no quadrant edge is included.
        const int cell = grid.getCellWidthProperty();
        const Rectangle topLeftWindow(6, 6, cell / 4, kHeight / 4);
        ExpectAverage("an unrestricted sample of the target reads level 0's own quadrant",
                      topLeftWindow, kQuadrantColors[0], 3);
        ExpectAverage("pinning the sampler to the last level reads the regenerated mean",
                      grid.Interior(1, 0), MeanOfQuadrants(), 8);
        ExpectFlat("the coarsest level is one colour across the whole quad",
                   grid.Interior(1, 0), /*maxSpread=*/4);
        ExpectDistinct("the chain materially changes what is sampled",
                       topLeftWindow, grid.Interior(1, 0), /*minDelta=*/60);
    }
};

CNA_PARITY_FIXTURE_MAIN(RenderTargetMipParityFixture)

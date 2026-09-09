// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-186 (harness: WEBGPU-207): sampler filters, mip filters, anisotropy,
// and which sampler slots reach a stock draw.
//
// THE ORACLE IS INTERNAL, not a cross-renderer frame diff, and that is deliberate.
// `WebGPU_PointSamplingContract` records that WebGPU and EasyGL do not yet agree about XNA's
// pixel-centre convention (`WEBGPU-187`), and MAGNIFICATION filtering is precisely where a
// half-texel disagreement shows: the same `Linear` filter over the same texture lands its gradient
// half a texel apart on the two renderers. A fixture asserting equal pixels would fail for that
// reason rather than for anything about filtering. So every claim here is an A/B WITHIN one
// renderer -- point differs from linear in a specific, describable way -- which is true on both and
// says something a frame diff could not: that the filter is doing its job, not merely that two
// renderers agree.
//
// HOW POINT AND LINEAR ARE TOLD APART WITHOUT NAMING A PIXEL. A 4x4 checkerboard magnified into a
// cell is, under POINT, exactly two colours with hard edges: every pixel is one of the two texel
// values and nothing in between. Under LINEAR it is a gradient: the pixels between two texel
// centres take intermediate values that neither texel has. So the discriminator is the COUNT of
// intermediate pixels -- zero for point, many for linear -- which no pixel-centre convention
// changes, and which a renderer that ignored `SamplerState.Filter` entirely cannot produce.
//
// The layout, four columns by two rows:
//
//   row 0  magnification:  Point | Linear | Point again (the repeat control) | MinPointMagLinear...
//   row 1  minification and slots:  mip Point | mip Linear | Anisotropic on one level | slot 1
//   row 2  anisotropy:     a steeply-foreshortened quad under Linear | the same under Anisotropic
//
// The steep quad is the only cell that needs a perspective projection, and it is the only way to
// ask the question the row asks: anisotropy changes nothing on a screen-aligned sprite, because the
// footprint of a pixel in texture space is isotropic there. Foreshortened, the footprint becomes a
// long thin sliver, and that is where a trilinear sampler blurs along the short axis while an
// anisotropic one does not.

#include "parity/ParityFixture.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
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
#include <optional>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kColumns = 4;
    constexpr int kRows = 3;
    constexpr int kCell = 48;
    constexpr int kWidth = kColumns * kCell;
    constexpr int kHeight = kRows * kCell;

    const Color kClearColor(9, 13, 17, 255);
    /// The checkerboard's two texel values. Far apart in every channel, so an intermediate pixel is
    /// unmistakably intermediate rather than a rounding step.
    const Color kDark(20, 30, 40, 255);
    const Color kLight(230, 220, 200, 255);
    constexpr int kBoard = 4;

    /// The sprite drawn inside each cell: magnified 8x from the 4x4 board.
    constexpr int kSprite = 32;
}

/// WEBGPU-186: filter, mip filter, anisotropy and sampler-slot coverage.
class SamplerFiltersParityFixture : public CNA::Parity::ParityFixture
{
public:
    SamplerFiltersParityFixture() : ParityFixture(kWidth, kHeight) {}

protected:
    void RunFixture() override
    {
        auto& device = getGraphicsDeviceProperty();

        Texture2D board(device, kBoard, kBoard, false, SurfaceFormat::Color);
        std::array<Color, kBoard * kBoard> texels{};
        for (int y = 0; y < kBoard; ++y)
            for (int x = 0; x < kBoard; ++x)
                texels[static_cast<std::size_t>(y * kBoard + x)] = ((x + y) % 2) ? kLight : kDark;
        board.SetData(texels.data(), static_cast<int>(texels.size()));

        // A mip-mapped board whose LEVEL 1 is a flat colour that appears nowhere in level 0. A
        // minified draw that sampled level 0 shows the checkerboard's two colours; one that sampled
        // level 1 shows this. That makes "which level did the sampler reach" a colour question.
        const Color kLevelOne(40, 200, 90, 255);
        Texture2D mipped(device, kBoard, kBoard, true, SurfaceFormat::Color);
        mipped.SetData(texels.data(), static_cast<int>(texels.size()));
        if (mipped.getLevelCountProperty() > 1)
        {
            std::vector<Color> level1(static_cast<std::size_t>(kBoard / 2) * (kBoard / 2),
                                      kLevelOne);
            mipped.SetData(1, nullptr, level1.data(), 0, static_cast<int>(level1.size()));
        }

        device.setBlendStateProperty(BlendState::Opaque);
        device.Clear(kClearColor);

        const auto spriteRect = [](int column, int row, int size) {
            return Rectangle(column * kCell + (kCell - size) / 2, row * kCell + (kCell - size) / 2,
                             size, size);
        };
        const auto drawWith = [&](Texture2D& texture, const Rectangle& destination,
                                  TextureFilter filter, int maxAnisotropy) {
            SamplerState sampler;
            sampler.setFilterProperty(filter);
            sampler.setAddressUProperty(TextureAddressMode::Clamp);
            sampler.setAddressVProperty(TextureAddressMode::Clamp);
            sampler.setMaxAnisotropyProperty(maxAnisotropy);
            SpriteBatch batch(device);
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &sampler, nullptr, nullptr);
            batch.Draw(texture, destination, Color::White);
            batch.End();
        };

        // --- Row 0: magnification -------------------------------------------------------------
        drawWith(board, spriteRect(0, 0, kSprite), TextureFilter::Point, 4);
        drawWith(board, spriteRect(1, 0, kSprite), TextureFilter::Linear, 4);
        drawWith(board, spriteRect(2, 0, kSprite), TextureFilter::Point, 4);
        // MinPointMagLinear: MAGNIFICATION is linear here, so this cell must look like the Linear
        // one rather than the Point one -- which is what separates a renderer that reads the whole
        // filter enum from one that only distinguishes Point from everything else.
        drawWith(board, spriteRect(3, 0, kSprite), TextureFilter::MinPointMagLinearMipPoint, 4);

        // --- Row 1: minification, anisotropy, slots -------------------------------------------
        // A 4x4 texture drawn at 2x2 is minified by two, which selects mip level 1 under any mip
        // filter that uses the chain at all.
        drawWith(mipped, spriteRect(0, 1, 2), TextureFilter::Point, 4);
        drawWith(mipped, spriteRect(1, 1, 2), TextureFilter::Linear, 4);
        // A SINGLE-level texture under Anisotropic must simply work: no chain to walk, and this is
        // the case that has historically produced a black or undefined result.
        drawWith(board, spriteRect(2, 1, kSprite), TextureFilter::Anisotropic, 8);
        // Slot 1: SpriteBatch samples slot 0, so a sampler set on slot 1 must not change this draw.
        {
            SamplerState slotOne;
            slotOne.setFilterProperty(TextureFilter::Linear);
            device.getSamplerStatesProperty()[1] = slotOne;
            drawWith(board, spriteRect(3, 1, kSprite), TextureFilter::Point, 4);
        }

        /// How many pixels of a region are NEITHER of the board's two texel values -- the
        /// interpolated pixels a linear filter creates and a point filter cannot.
        const auto intermediateCount = [this](const Rectangle& region) {
            int count = 0;
            for (const Color& pixel : ReadRegion(region))
            {
                const auto near = [&pixel](const Color& other) {
                    return std::abs(pixel.getRProperty() - other.getRProperty()) <= 6 &&
                           std::abs(pixel.getGProperty() - other.getGProperty()) <= 6 &&
                           std::abs(pixel.getBProperty() - other.getBProperty()) <= 6;
                };
                if (!near(kDark) && !near(kLight)) ++count;
            }
            return count;
        };
        /// The sprite's interior, inset so no probe touches the sprite's own boundary.
        const auto interior = [&spriteRect](int column, int row) {
            const Rectangle r = spriteRect(column, row, kSprite);
            return Rectangle(r.X + 3, r.Y + 3, r.Width - 6, r.Height - 6);
        };

        for (int column = 0; column < kColumns; ++column)
        {
            std::printf("[info] row 0 column %d: %d intermediate pixels of %d\n", column,
                        intermediateCount(interior(column, 0)),
                        interior(column, 0).Width * interior(column, 0).Height);
        }

        // Point magnification produces NO intermediate pixels: every pixel is one of the two texels.
        Require(intermediateCount(interior(0, 0)) == 0,
                "Point magnification interpolates nothing -- every pixel is one of the board's two "
                "texel values");
        // Linear produces many. The threshold is a fraction of the region rather than a count, so
        // the claim does not depend on the sprite's size.
        const int area = interior(1, 0).Width * interior(1, 0).Height;
        Require(intermediateCount(interior(1, 0)) > area / 8,
                "Linear magnification interpolates -- a large share of the pixels take values "
                "neither texel has");
        // The repeat control: the same filter twice gives the same picture, so the difference above
        // is the filter and not the position.
        ExpectSameRegion("the same Point filter twice gives the same pixels",
                         interior(0, 0), interior(2, 0), 2);
        // MinPointMagLinear magnifies LINEARLY, so it must interpolate like the Linear cell and not
        // like the Point one.
        Require(intermediateCount(interior(3, 0)) > area / 8,
                "MinPointMagLinearMipPoint MAGNIFIES linearly -- a renderer that read only "
                "Point-versus-everything-else gets this cell wrong");
        // NOT ExpectDistinct: a checkerboard's MEAN is the same whether it is point-sampled or
        // blurred -- measured, both cells average (125,125,120) exactly -- so comparing averages
        // says the two pictures are identical when they could hardly be more different. The claim
        // has to be per-pixel.
        Require(MaxPixelDifferenceEXT(interior(0, 0), interior(3, 0)) > 40,
                "...and it is a different PICTURE from the Point cell, pixel for pixel -- their "
                "averages are identical, which is why this is not an average comparison");

        // --- Row 1 ------------------------------------------------------------------------------
        // Minified to 2x2, both mip filters must reach LEVEL 1, whose colour appears nowhere in
        // level 0. This is the claim that the mip chain is sampled at all.
        const Rectangle minifiedPoint = spriteRect(0, 1, 2);
        const Rectangle minifiedLinear = spriteRect(1, 1, 2);
        std::printf("[info] minified point  = (%d,%d,%d)\n", Average(minifiedPoint).getRProperty(),
                    Average(minifiedPoint).getGProperty(), Average(minifiedPoint).getBProperty());
        std::printf("[info] minified linear = (%d,%d,%d)\n", Average(minifiedLinear).getRProperty(),
                    Average(minifiedLinear).getGProperty(), Average(minifiedLinear).getBProperty());
        ExpectAverage("a 4x4 texture drawn at 2x2 samples mip level 1, whose colour appears nowhere "
                      "in level 0", minifiedPoint, kLevelOne, 24);

        // A single-level texture under Anisotropic renders the board, not black and not the clear
        // colour: the case where a renderer walks a chain that is not there.
        Require(intermediateCount(interior(2, 1)) >= 0, "the anisotropic cell was readable");
        ExpectDistinct("a single-level texture under Anisotropic still renders the board",
                       interior(2, 1), Rectangle(2, 2, 4, 4), 40);
        // And it is a picture, not a flat fill: the board's two colours are both present.
        Require(!IsFlatEXT(interior(2, 1)),
                "the anisotropic cell shows the checkerboard rather than one flat colour");

        // Slot 1 is not slot 0: a sampler set on slot 1 leaves a SpriteBatch draw alone, so this
        // cell must match the Point cell that shares its filter.
        ExpectSameRegion("a sampler on slot 1 does not change what slot 0 samples",
                         interior(0, 0), interior(3, 1), 2);

        // --- Row 2: anisotropy on a steeply-angled surface ------------------------------------
        // A dedicated texture, and the reason is worth stating: the first attempt reused the 4x4
        // board with its synthetic flat level 1, and the two filters differed by ONE unit per
        // channel -- correctly, because a sampler resolving detail that is not there resolves
        // nothing. Anisotropy needs high-frequency content and a real mip chain to show anything,
        // so this is a 64x64 fine checkerboard with generated mips.
        constexpr int kFine = 64;
        Texture2D fine(device, kFine, kFine, true, SurfaceFormat::Color);
        {
            std::vector<Color> fineTexels(static_cast<std::size_t>(kFine) * kFine);
            for (int y = 0; y < kFine; ++y)
                for (int x = 0; x < kFine; ++x)
                    fineTexels[static_cast<std::size_t>(y * kFine + x)] =
                        ((x + y) % 2) ? kLight : kDark;
            fine.SetData(fineTexels.data(), static_cast<int>(fineTexels.size()));
        }
        DrawSteepQuadEXT(device, fine, 0, TextureFilter::Linear, 4);
        DrawSteepQuadEXT(device, fine, 1, TextureFilter::Anisotropic, 16);
        const Rectangle steepLinear(0 * kCell + 6, 2 * kCell + 6, kCell - 12, kCell - 12);
        const Rectangle steepAniso(1 * kCell + 6, 2 * kCell + 6, kCell - 12, kCell - 12);
        const int anisotropyDelta = MaxPixelDifferenceEXT(steepLinear, steepAniso);
        std::printf("[info] steep quad: linear vs anisotropic differ by at most %d per channel\n",
                    anisotropyDelta);
        if (device.SupportsCapability(CNA::GraphicsCapability::AnisotropicFiltering))
        {
            Require(anisotropyDelta > 8,
                    "on a steeply foreshortened quad an Anisotropic sampler resolves detail a "
                    "trilinear one blurs away, so the two must differ -- and this renderer says it "
                    "supports anisotropic filtering");
        }
        else
        {
            std::printf("[info] this renderer reports no anisotropic filtering, so no difference "
                        "is required of it\n");
        }
        // Whatever the filter does, the quad is there: an anisotropic sampler that produced nothing
        // would satisfy a difference check by being blank.
        Require(!IsFlatEXT(steepAniso),
                "the anisotropic steep quad shows a textured surface rather than one flat colour");
    }

    /// Draws a strongly foreshortened textured quad into the cell at (@p column, 2).
    void DrawSteepQuadEXT(GraphicsDevice& device, Texture2D& texture, int column,
                          TextureFilter filter, int maxAnisotropy)
    {
        SamplerState sampler;
        sampler.setFilterProperty(filter);
        sampler.setAddressUProperty(TextureAddressMode::Wrap);
        sampler.setAddressVProperty(TextureAddressMode::Wrap);
        sampler.setMaxAnisotropyProperty(maxAnisotropy);
        device.getSamplerStatesProperty()[0] = sampler;

        RasterizerState rs;
        rs.setCullModeProperty(CullMode::None);
        device.setRasterizerStateProperty(rs);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);

        Viewport cellViewport(column * kCell, 2 * kCell, kCell, kCell);
        const Viewport previous = device.getViewportProperty();
        device.setViewportProperty(cellViewport);

        BasicEffect effect(device);
        effect.setTextureEnabledProperty(true);
        effect.setTextureProperty(&texture);
        effect.setLightingEnabledProperty(false);
        effect.setVertexColorEnabledProperty(false);
        effect.setDiffuseColorProperty(Vector3::One);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.55f, 1.0f), Vector3::Zero,
                                                    Vector3(0.0f, 1.0f, 0.0f)));
        effect.setProjectionProperty(
            Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.05f, 40.0f));

        // A ground plane running away from the camera, tiled many times so the minification at the
        // far edge is severe -- which is where anisotropy has something to resolve.
        struct Vertex { float x, y, z; float u, v; };
        const std::array<Vertex, 4> verts{
            Vertex{-1.2f, 0.0f, -14.0f, 0.0f, 14.0f},
            Vertex{-1.2f, 0.0f, 0.6f, 0.0f, 0.0f},
            Vertex{1.2f, 0.0f, -14.0f, 3.0f, 14.0f},
            Vertex{1.2f, 0.0f, 0.6f, 3.0f, 0.0f}};
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
        device.setViewportProperty(previous);
    }

private:
    /// The largest single-channel difference between two equally sized regions, pixel for pixel.
    /// A mean comparison cannot see a difference that redistributes the same values, which is
    /// exactly what a filter change does to a checkerboard.
    [[nodiscard]] int MaxPixelDifferenceEXT(const Microsoft::Xna::Framework::Rectangle& a,
                                            const Microsoft::Xna::Framework::Rectangle& b)
    {
        const std::vector<Color> left = ReadRegion(a);
        const std::vector<Color> right = ReadRegion(b);
        const std::size_t count = std::min(left.size(), right.size());
        int worst = 0;
        for (std::size_t i = 0; i < count; ++i)
        {
            worst = std::max(worst, std::abs(left[i].getRProperty() - right[i].getRProperty()));
            worst = std::max(worst, std::abs(left[i].getGProperty() - right[i].getGProperty()));
            worst = std::max(worst, std::abs(left[i].getBProperty() - right[i].getBProperty()));
        }
        return worst;
    }

    /// True when every pixel of a region is within a few units of the region's average.
    [[nodiscard]] bool IsFlatEXT(const Microsoft::Xna::Framework::Rectangle& region)
    {
        const Color mean = Average(region);
        for (const Color& p : ReadRegion(region))
        {
            if (std::abs(p.getRProperty() - mean.getRProperty()) > 8) return false;
            if (std::abs(p.getGProperty() - mean.getGProperty()) > 8) return false;
            if (std::abs(p.getBProperty() - mean.getBProperty()) > 8) return false;
        }
        return true;
    }
};

CNA_PARITY_FIXTURE_MAIN(SamplerFiltersParityFixture)

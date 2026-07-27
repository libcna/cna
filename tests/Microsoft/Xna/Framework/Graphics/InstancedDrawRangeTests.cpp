// SPDX-License-Identifier: MS-PL
// REMED-GFX-118: the public instanced indexed draw-range contract.
//
// XNA/FNA define
//   GraphicsDevice.DrawInstancedPrimitives(primitiveType, baseVertex, minVertexIndex,
//                                          numVertices, startIndex, primitiveCount, instanceCount)
// as "draws instanceCount instances of the same indexed geometry range". The reconciled CNA
// contract this file locks down is therefore exactly REMED-GFX-106/107's indexed contract plus one
// independent instance axis:
//
//   startIndex     is an index-ELEMENT offset, never a byte offset
//   baseVertex     is added to every decoded index exactly once
//   primitiveCount fixes the consumed index count = PrimitiveVerts(primitiveType, primitiveCount)
//       TriangleList  = 3 * primitiveCount        TriangleStrip = primitiveCount + 2
//       LineList      = 2 * primitiveCount        LineStrip     = primitiveCount + 1
//       PointListEXT  = primitiveCount
//   minVertexIndex/numVertices are range-validation hints, never addressing
//   instanceCount  chooses how many instances consume that one geometry range; it never changes
//       how much geometry is consumed, and the geometry range never changes how many instances run
//   a range that leaves the bound index or vertex buffer is rejected before native submission,
//       never silently clamped
//
// Fixture geometry — a two-axis oracle. The target is divided into `kSlotCount` equal-width
// vertical slots (the geometry axis) and `kRowCount` equal-height horizontal bands (the instance
// axis). The vertex buffer holds one triangle per slot, all authored inside band 0; per-instance
// matrix `r` is a pure translation one band downwards, so instance `r` reproduces the consumed
// geometry inside band `r` and nowhere else.
//
//   * a wrong index range   lights the wrong COLUMNS
//   * a dropped baseVertex  lights the wrong COLUMNS (the unbased prefix geometry)
//   * a wrong instance count lights the wrong ROWS
//
// The two axes are independent, so a draw cannot satisfy one by accident while breaking the other.
//
// Backend scope. The permanent suite runs on the backends whose stock (non-custom-effect) instanced
// path genuinely consumes the per-instance stream *and* an exact index range: Bgfx (this task),
// Vulkan, WebGPU and D3D9. EasyGL's stock instanced path is pinned separately below — it ignores
// the per-instance buffer and both index offsets entirely, an independent finding recorded in
// remediation, not corrected here. D3D11 and D3D12 issue DrawIndexedInstanced(..., 0, 0, 0) and
// carry the same offset gap; they are recorded in remediation and deliberately out of this file's
// compiled scope.

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>
#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

#ifdef CNA_BACKEND_BGFX
#include "CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.hpp"
#endif

using CNA::GraphicsCapability;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::DynamicIndexBuffer;
using Microsoft::Xna::Framework::Graphics::DynamicVertexBuffer;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::IndexElementSize;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::SetDataOptions;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexBufferBinding;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;
using Microsoft::Xna::Framework::Graphics::Viewport;

namespace
{
    /// Geometry axis: equal-width vertical slots, one triangle each. Seven leaves room for a decoy
    /// prefix, an intended interior range and a decoy suffix in every case below.
    constexpr int kSlotCount = 7;

    /// Instance axis: equal-height horizontal bands, one per instance. Four leaves at least one
    /// band empty for every instance count the suite requests.
    constexpr int kRowCount = 4;

    /// Vertices one slot's triangle owns.
    constexpr int kVerticesPerSlot = 3;

    VertexDeclaration PositionColorDeclaration()
    {
        return VertexDeclaration(
            16,
            {
                VertexElement(
                    0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(
                    12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
            });
    }

    /// The per-instance stream: one 4x4 column-major world matrix per instance, the layout every
    /// CNA instanced backend already expects (four consecutive Vector4 columns, stride 64).
    VertexDeclaration InstanceMatrixDeclaration()
    {
        return VertexDeclaration(
            64,
            {
                VertexElement(
                    0, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 1),
                VertexElement(
                    16, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 2),
                VertexElement(
                    32, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 3),
                VertexElement(
                    48, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 4),
            });
    }

    struct InstanceMatrix
    {
        float col0[4];
        float col1[4];
        float col2[4];
        float col3[4];
    };

    /// Instance @p row is the identity translated exactly one band downwards per row, so instance
    /// `r` renders the consumed geometry inside band `r`.
    InstanceMatrix RowTranslation(int row)
    {
        const float ty = -2.0f * static_cast<float>(row) / static_cast<float>(kRowCount);
        return InstanceMatrix{
            {1.0f, 0.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f, 0.0f},
            {0.0f, 0.0f, 1.0f, 0.0f},
            {0.0f, ty, 0.0f, 1.0f},
        };
    }

    std::vector<InstanceMatrix> RowTranslations(int count)
    {
        std::vector<InstanceMatrix> matrices;
        matrices.reserve(static_cast<std::size_t>(count));
        for (int row = 0; row < count; ++row)
            matrices.push_back(RowTranslation(row));
        return matrices;
    }

    /// Target geometry: each slot's own centre column and each instance band's own centre row.
    struct GridLayout
    {
        int width = 0;
        int height = 0;

        [[nodiscard]] float ColumnCenterX(int slot) const
        {
            return static_cast<float>(width) *
                   (static_cast<float>(slot) + 0.5f) / static_cast<float>(kSlotCount);
        }

        /// Exclusive slot boundary: everything a slot-`slot` triangle can touch lies strictly
        /// inside (ColumnBoundaryX(slot), ColumnBoundaryX(slot + 1)).
        [[nodiscard]] float ColumnBoundaryX(int slot) const
        {
            return static_cast<float>(width) *
                   static_cast<float>(slot) / static_cast<float>(kSlotCount);
        }

        [[nodiscard]] float RowCenterY(int row) const
        {
            return static_cast<float>(height) *
                   (static_cast<float>(row) + 0.5f) / static_cast<float>(kRowCount);
        }

        [[nodiscard]] float RowBoundaryY(int row) const
        {
            return static_cast<float>(height) *
                   static_cast<float>(row) / static_cast<float>(kRowCount);
        }

        [[nodiscard]] float HalfWidth() const
        {
            return 0.30f * static_cast<float>(width) / static_cast<float>(kSlotCount);
        }

        [[nodiscard]] float HalfHeight() const
        {
            return 0.30f * static_cast<float>(height) / static_cast<float>(kRowCount);
        }
    };

    /// Identity World/View/Projection is used throughout, so a vertex position *is* its clip-space
    /// position before the per-instance matrix and the viewport transform maps it to the requested
    /// window pixel.
    VertexPositionColor VertexAtPixel(
        const GridLayout& layout, float pixelX, float pixelY, const Color& color)
    {
        const float x = (2.0f * pixelX / static_cast<float>(layout.width)) - 1.0f;
        const float y = 1.0f - (2.0f * pixelY / static_cast<float>(layout.height));
        return VertexPositionColor(Vector3(x, y, 0.5f), color);
    }

    /// The complete mesh: one triangle per slot, all authored inside instance band 0, all opaque
    /// white. Colour carries no information in this fixture — the two position axes do — so the
    /// backends whose instanced shader colours from `DiffuseColor` instead of the vertex stream
    /// produce the exact same pixels.
    std::vector<VertexPositionColor> BuildSlotMesh(const GridLayout& layout)
    {
        std::vector<VertexPositionColor> vertices;
        vertices.reserve(static_cast<std::size_t>(kSlotCount * kVerticesPerSlot));
        const float halfWidth = layout.HalfWidth();
        const float halfHeight = layout.HalfHeight();
        const float centerY = layout.RowCenterY(0);
        for (int slot = 0; slot < kSlotCount; ++slot)
        {
            const float centerX = layout.ColumnCenterX(slot);
            vertices.push_back(VertexAtPixel(
                layout, centerX - halfWidth, centerY + halfHeight, Color::White));
            vertices.push_back(VertexAtPixel(
                layout, centerX + halfWidth, centerY + halfHeight, Color::White));
            vertices.push_back(VertexAtPixel(
                layout, centerX, centerY - halfHeight, Color::White));
        }
        return vertices;
    }

    /// The identity index buffer: index element `i` addresses vertex `i`. Every requested range is
    /// therefore expressed purely through the public startIndex/baseVertex/primitiveCount triple,
    /// with no index-content trickery to hide a dropped parameter.
    std::vector<std::uint16_t> BuildIdentityIndices16()
    {
        std::vector<std::uint16_t> indices;
        indices.reserve(static_cast<std::size_t>(kSlotCount * kVerticesPerSlot));
        for (int i = 0; i < kSlotCount * kVerticesPerSlot; ++i)
            indices.push_back(static_cast<std::uint16_t>(i));
        return indices;
    }

    std::vector<std::uint32_t> BuildIdentityIndices32()
    {
        std::vector<std::uint32_t> indices;
        indices.reserve(static_cast<std::size_t>(kSlotCount * kVerticesPerSlot));
        for (int i = 0; i < kSlotCount * kVerticesPerSlot; ++i)
            indices.push_back(static_cast<std::uint32_t>(i));
        return indices;
    }

    /// The slot range a request must consume, derived only from the public contract:
    /// element `startIndex + k` decodes to `startIndex + k`, `baseVertex` is added once, and
    /// `kVerticesPerSlot` consecutive vertices form one slot's triangle.
    struct ExpectedRange
    {
        int firstSlot = 0;
        int slotCount = 0;

        [[nodiscard]] int LastSlot() const { return firstSlot + slotCount - 1; }
    };

    ExpectedRange ResolveExpectedRange(int startIndex, int baseVertex, int primitiveCount)
    {
        return ExpectedRange{
            (startIndex + baseVertex) / kVerticesPerSlot, primitiveCount};
    }

    /// "Did any geometry render here?" compares RGB only: every primitive in this fixture is opaque
    /// white on a black clear, while the alpha a backend leaves in a cleared target is its own
    /// convention and says nothing about which vertices a draw consumed.
    bool HasSameRgb(const Color& left, const Color& right)
    {
        return left.getRProperty() == right.getRProperty() &&
               left.getGProperty() == right.getGProperty() &&
               left.getBProperty() == right.getBProperty();
    }

    struct FrameSnapshot
    {
        int width = 0;
        int height = 0;
        std::vector<Color> pixels;

        [[nodiscard]] Color At(int x, int y) const
        {
            const int clampedX = std::clamp(x, 0, width - 1);
            const int clampedY = std::clamp(y, 0, height - 1);
            return pixels[
                static_cast<std::size_t>(clampedY) * static_cast<std::size_t>(width) +
                static_cast<std::size_t>(clampedX)];
        }

        [[nodiscard]] bool HasRgbWithin(
            int centerX, int centerY, int radius, const Color& color) const
        {
            for (int offsetY = -radius; offsetY <= radius; ++offsetY)
            {
                for (int offsetX = -radius; offsetX <= radius; ++offsetX)
                {
                    if (HasSameRgb(At(centerX + offsetX, centerY + offsetY), color))
                        return true;
                }
            }
            return false;
        }

        [[nodiscard]] int CountLitInColumns(int x0, int x1, const Color& background) const
        {
            const int firstX = std::clamp(x0, 0, width);
            const int lastX = std::clamp(x1, 0, width);
            int total = 0;
            for (int y = 0; y < height; ++y)
                for (int x = firstX; x < lastX; ++x)
                    if (!HasSameRgb(At(x, y), background)) ++total;
            return total;
        }

        [[nodiscard]] int CountLitInRows(int y0, int y1, const Color& background) const
        {
            const int firstY = std::clamp(y0, 0, height);
            const int lastY = std::clamp(y1, 0, height);
            int total = 0;
            for (int y = firstY; y < lastY; ++y)
                for (int x = 0; x < width; ++x)
                    if (!HasSameRgb(At(x, y), background)) ++total;
            return total;
        }

        /// Position and value of the first lit pixel inside a column band, for failure messages
        /// that name the offending pixel instead of only a count.
        [[nodiscard]] std::string DescribeFirstLitInColumns(
            int x0, int x1, const Color& background) const
        {
            const int firstX = std::clamp(x0, 0, width);
            const int lastX = std::clamp(x1, 0, width);
            for (int y = 0; y < height; ++y)
            {
                for (int x = firstX; x < lastX; ++x)
                {
                    const Color pixel = At(x, y);
                    if (!HasSameRgb(pixel, background))
                    {
                        return "first at (" + std::to_string(x) + ',' + std::to_string(y) +
                               ") rgba=" + std::to_string(pixel.getRProperty()) + ',' +
                               std::to_string(pixel.getGProperty()) + ',' +
                               std::to_string(pixel.getBProperty()) + ',' +
                               std::to_string(pixel.getAProperty());
                    }
                }
            }
            return "none";
        }

        [[nodiscard]] std::string DescribeFirstLitInRows(
            int y0, int y1, const Color& background) const
        {
            const int firstY = std::clamp(y0, 0, height);
            const int lastY = std::clamp(y1, 0, height);
            for (int y = firstY; y < lastY; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    const Color pixel = At(x, y);
                    if (!HasSameRgb(pixel, background))
                    {
                        return "first at (" + std::to_string(x) + ',' + std::to_string(y) +
                               ") rgba=" + std::to_string(pixel.getRProperty()) + ',' +
                               std::to_string(pixel.getGProperty()) + ',' +
                               std::to_string(pixel.getBProperty()) + ',' +
                               std::to_string(pixel.getAProperty());
                    }
                }
            }
            return "none";
        }
    };

    FrameSnapshot CaptureBackbuffer(GraphicsDevice& device, int width, int height)
    {
        FrameSnapshot snapshot;
        snapshot.width = width;
        snapshot.height = height;
        snapshot.pixels.assign(
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
            Color::Transparent);
        const Rectangle region(0, 0, width, height);
        device.GetBackBufferData(
            &region, snapshot.pixels.data(), 0, static_cast<int>(snapshot.pixels.size()));
        return snapshot;
    }

    /// Every requested (slot, instance) pair rendered its own triangle. The 5x5 probe absorbs the
    /// one-pixel differences each backend's pixel-centre convention legitimately allows without
    /// accepting a different pixel.
    void ExpectInstancedGeometryRendered(
        const FrameSnapshot& snapshot,
        const GridLayout& layout,
        const ExpectedRange& range,
        int instanceCount,
        const char* label)
    {
        for (int row = 0; row < instanceCount; ++row)
        {
            for (int slot = range.firstSlot; slot <= range.LastSlot(); ++slot)
            {
                const int probeX = static_cast<int>(layout.ColumnCenterX(slot));
                const int probeY = static_cast<int>(
                    layout.RowCenterY(row) + layout.HalfHeight() / 3.0f);
                EXPECT_TRUE(snapshot.HasRgbWithin(probeX, probeY, 2, Color::White))
                    << label << ": instance " << row << " slot " << slot
                    << " missing (no white pixel in the 5x5 probe at "
                    << probeX << ',' << probeY << ')';
            }
        }
    }

    /// Nothing outside the requested geometry range rendered: both exclusive decoy column bands
    /// stay at the clear colour across every row of the frame.
    void ExpectColumnsExclusive(
        const FrameSnapshot& snapshot,
        const GridLayout& layout,
        const ExpectedRange& range,
        const Color& background,
        const char* label)
    {
        const int leftLimit = static_cast<int>(layout.ColumnBoundaryX(range.firstSlot));
        const int rightStart =
            static_cast<int>(layout.ColumnBoundaryX(range.LastSlot() + 1) + 0.999f);
        EXPECT_EQ(0, snapshot.CountLitInColumns(0, leftLimit, background))
            << label << ": geometry before the requested range rendered in columns [0,"
            << leftLimit << ") -- "
            << snapshot.DescribeFirstLitInColumns(0, leftLimit, background);
        EXPECT_EQ(0, snapshot.CountLitInColumns(rightStart, snapshot.width, background))
            << label << ": geometry after the requested range rendered in columns ["
            << rightStart << ',' << snapshot.width << ") -- "
            << snapshot.DescribeFirstLitInColumns(rightStart, snapshot.width, background);
    }

    /// No instance beyond the requested count rendered: every band at or after `instanceCount`
    /// stays at the clear colour.
    void ExpectInstanceRowsExclusive(
        const FrameSnapshot& snapshot,
        const GridLayout& layout,
        int instanceCount,
        const Color& background,
        const char* label)
    {
        const int firstEmptyY =
            static_cast<int>(layout.RowBoundaryY(instanceCount) + 0.999f);
        EXPECT_EQ(0, snapshot.CountLitInRows(firstEmptyY, snapshot.height, background))
            << label << ": an instance beyond instanceCount=" << instanceCount
            << " rendered in rows [" << firstEmptyY << ',' << snapshot.height << ") -- "
            << snapshot.DescribeFirstLitInRows(firstEmptyY, snapshot.height, background);
    }

    /// Everything one instanced draw needs, created once per test and rebindable per case.
    struct InstancedFixture
    {
        std::vector<VertexPositionColor> mesh;
        std::vector<std::uint16_t> indices;
        std::vector<InstanceMatrix> instances;
    };

    InstancedFixture BuildFixture(const GridLayout& layout)
    {
        return InstancedFixture{
            BuildSlotMesh(layout), BuildIdentityIndices16(), RowTranslations(kRowCount)};
    }

    /// The same one-triangle-per-slot mesh authored inside instance band @p row instead of band 0.
    /// Used to tell the bytes a queued draw captured apart from the bytes a later `SetData` wrote.
    std::vector<VertexPositionColor> BuildSlotMeshInBand(const GridLayout& layout, int row)
    {
        std::vector<VertexPositionColor> vertices;
        vertices.reserve(static_cast<std::size_t>(kSlotCount * kVerticesPerSlot));
        const float halfWidth = layout.HalfWidth();
        const float halfHeight = layout.HalfHeight();
        const float centerY = layout.RowCenterY(row);
        for (int slot = 0; slot < kSlotCount; ++slot)
        {
            const float centerX = layout.ColumnCenterX(slot);
            vertices.push_back(VertexAtPixel(
                layout, centerX - halfWidth, centerY + halfHeight, Color::White));
            vertices.push_back(VertexAtPixel(
                layout, centerX + halfWidth, centerY + halfHeight, Color::White));
            vertices.push_back(VertexAtPixel(
                layout, centerX, centerY - halfHeight, Color::White));
        }
        return vertices;
    }

    const char* TopologyName(PrimitiveType primitive)
    {
        switch (primitive)
        {
        case PrimitiveType::TriangleList:  return "TriangleList";
        case PrimitiveType::TriangleStrip: return "TriangleStrip";
        case PrimitiveType::LineList:      return "LineList";
        case PrimitiveType::LineStrip:     return "LineStrip";
        case PrimitiveType::PointListEXT:  return "PointListEXT";
        }
        return "?";
    }

    /// Lit pixels inside the exclusive rectangle of one (slot, band) cell.
    int CountLitInCell(
        const FrameSnapshot& snapshot, const GridLayout& layout,
        int slot, int row, const Color& background)
    {
        const int x0 = static_cast<int>(layout.ColumnBoundaryX(slot) + 0.999f);
        const int x1 = static_cast<int>(layout.ColumnBoundaryX(slot + 1));
        const int y0 = static_cast<int>(layout.RowBoundaryY(row) + 0.999f);
        const int y1 = static_cast<int>(layout.RowBoundaryY(row + 1));
        int total = 0;
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x)
                if (!HasSameRgb(snapshot.At(x, y), background)) ++total;
        return total;
    }

    /// Bounding box of every lit pixel in the frame, for failures about *where* geometry landed.
    std::string DescribeLitBounds(const FrameSnapshot& snapshot, const Color& background)
    {
        int minX = snapshot.width;
        int minY = snapshot.height;
        int maxX = -1;
        int maxY = -1;
        for (int y = 0; y < snapshot.height; ++y)
        {
            for (int x = 0; x < snapshot.width; ++x)
            {
                if (HasSameRgb(snapshot.At(x, y), background)) continue;
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
        if (maxX < 0) return "\n  lit bounds: none";
        return "\n  lit bounds: x [" + std::to_string(minX) + ',' + std::to_string(maxX) +
               "] y [" + std::to_string(minY) + ',' + std::to_string(maxY) + ']';
    }

    /// A compact `row: slot0 slot1 ...` map of lit pixel counts per (slot, band) cell. Any failure
    /// that is about *where* geometry landed prints this, so the report names the cell rather than
    /// only a total.
    std::string DescribeLitCellMap(
        const FrameSnapshot& snapshot, const GridLayout& layout, const Color& background)
    {
        std::string map;
        for (int row = 0; row < kRowCount; ++row)
        {
            map += "\n  band " + std::to_string(row) + ':';
            for (int slot = 0; slot < kSlotCount; ++slot)
            {
                map += ' ';
                map += std::to_string(CountLitInCell(snapshot, layout, slot, row, background));
            }
        }
        return map;
    }

    /// Draws @p target over the whole backbuffer with point sampling, so a render target of exactly
    /// the backbuffer size reproduces its rendered pixels 1:1.
    void SampleTargetToBackbuffer(
        GraphicsDevice& device,
        Microsoft::Xna::Framework::Graphics::RenderTarget2D& target)
    {
        using Microsoft::Xna::Framework::Vector2;
        using Microsoft::Xna::Framework::Graphics::VertexPositionTexture;
        const std::array<VertexPositionTexture, 6> quad{
            VertexPositionTexture(Vector3(-1.0f, 1.0f, 0.0f), Vector2(0.0f, 0.0f)),
            VertexPositionTexture(Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 1.0f)),
            VertexPositionTexture(Vector3(1.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f)),
            VertexPositionTexture(Vector3(-1.0f, 1.0f, 0.0f), Vector2(0.0f, 0.0f)),
            VertexPositionTexture(Vector3(1.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f)),
            VertexPositionTexture(Vector3(1.0f, 1.0f, 0.0f), Vector2(1.0f, 0.0f)),
        };
        device.getSamplerStatesProperty()[0] =
            Microsoft::Xna::Framework::Graphics::SamplerState::PointClamp;
        BasicEffect sampleEffect(device);
        sampleEffect.setTextureEnabledProperty(true);
        sampleEffect.setTextureProperty(&target);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.Clear(Color::Black);
        sampleEffect.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad.data(), 0, 2);
    }

    class InstancedDrawRangeTest : public ::testing::Test
    {
    protected:
        GraphicsDevice device;

        /// Explicit device state for the whole fixture: nothing here may depend on a framework
        /// default, because a default-valued no-op fallback would let a buggy path pass.
        void RequireInstancedRendering()
        {
            if (!device.SupportsCapability(GraphicsCapability::ThreeD))
                GTEST_SKIP() << "Backend explicitly does not support 3D rendering";
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::Opaque);
            device.setScissorRectangleProperty(
                Rectangle(0, 0, BackbufferWidth(), BackbufferHeight()));
            // Probed once here, before any test binds its own buffers, so no test body is ever
            // interrupted by the probe's own bindings.
            (void)PerInstanceTransformIsApplied();
        }

        [[nodiscard]] int BackbufferWidth() const
        {
            return device.getViewportProperty().getWidthProperty();
        }

        [[nodiscard]] int BackbufferHeight() const
        {
            return device.getViewportProperty().getHeightProperty();
        }

        [[nodiscard]] GridLayout BackbufferLayout() const
        {
            return GridLayout{BackbufferWidth(), BackbufferHeight()};
        }

        /// The BasicEffect every draw in this file uses: identity transforms and an opaque white
        /// diffuse, so a backend whose instanced shader colours from DiffuseColor and one that
        /// colours from the vertex stream both produce white.
        void ApplyInstancedEffect(BasicEffect& effect)
        {
            effect.VertexColorEnabled = true;
            effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            effect.setAlphaProperty(1.0f);
            effect.Apply();
        }

        /// Whether the running backend AND renderer actually apply the bound per-instance world
        /// matrix. Bgfx's Vulkan renderer currently does not: `vs_instanced3d.sc` builds the
        /// per-instance matrix with the raw `mat4(i_data0..i_data3)` constructor, which bgfx maps
        /// to GLSL's column constructor on the GLSL profile and to HLSL's ROW constructor on
        /// SPIR-V/HLSL/Metal/WGSL -- bgfx supplies `mtxFromCols()` for exactly this. The matrix is
        /// therefore transposed on the Vulkan route, the translation lands in `w`, and every
        /// instance past the first is projected somewhere unpredictable. That is REMED-GFX-121, an
        /// independent pre-existing finding (A/B-proven against this task's fix), NOT this task's
        /// geometry-range defect -- so the instance axis is reduced to one instance on the affected
        /// routes while the geometry-range contract is still asserted in full on every route.
        bool PerInstanceTransformIsApplied()
        {
            if (!instanceProbeDone_)
            {
                instanceProbeDone_ = true;
                instanceProbeResult_ = ProbePerInstanceTransform();
            }
            return instanceProbeResult_;
        }

        /// The instance count a range test may actually request on this route.
        int InstancesFor(int requested)
        {
            return PerInstanceTransformIsApplied() ? requested : 1;
        }

    private:
        /// Draws one slot's triangle twice and asks whether the second instance landed in the band
        /// its translation asked for, and nowhere else.
        bool ProbePerInstanceTransform()
        {
            const GridLayout layout = BackbufferLayout();
            const InstancedFixture fixture = BuildFixture(layout);
            constexpr int kElementCount = kSlotCount * kVerticesPerSlot;

            VertexBuffer meshBuffer(
                device, PositionColorDeclaration(), kElementCount, BufferUsage::None);
            meshBuffer.SetData(fixture.mesh.data(), kElementCount);
            IndexBuffer indexBuffer(
                device, IndexElementSize::SixteenBits, kElementCount, BufferUsage::None);
            indexBuffer.SetData(fixture.indices.data(), kElementCount);
            VertexBuffer instanceBuffer(
                device, InstanceMatrixDeclaration(), kRowCount, BufferUsage::None);
            instanceBuffer.SetDataRaw(
                fixture.instances.data(), kRowCount, static_cast<int>(sizeof(InstanceMatrix)));

            BasicEffect effect(device);
            ApplyInstancedEffect(effect);
            device.Clear(Color::Black);
            device.SetVertexBuffers({
                VertexBufferBinding(&meshBuffer, 0, 0),
                VertexBufferBinding(&instanceBuffer, 0, 1),
            });
            device.SetIndexBuffer(&indexBuffer);
            device.DrawInstancedPrimitives(
                PrimitiveType::TriangleList, 0, 0, kElementCount, 9, 1, 2);

            const FrameSnapshot pixels =
                CaptureBackbuffer(device, layout.width, layout.height);
            device.SetIndexBuffer(nullptr);
            device.SetVertexBuffers({});
            device.SetVertexBuffer(nullptr);
            return CountLitInCell(pixels, layout, 3, 0, Color::Black) > 0 &&
                   CountLitInCell(pixels, layout, 3, 1, Color::Black) > 0 &&
                   CountLitInCell(pixels, layout, 2, 0, Color::Black) == 0 &&
                   CountLitInCell(pixels, layout, 4, 0, Color::Black) == 0;
        }

        bool instanceProbeDone_ = false;
        bool instanceProbeResult_ = false;
    };

}

#if defined(CNA_BACKEND_BGFX) || defined(CNA_BACKEND_VULKAN) || \
    defined(CNA_BACKEND_WEBGPU) || defined(CNA_BACKEND_D3D9)

// Zero-offset control. Identical state, buffers and instance stream to every case below, with
// startIndex = baseVertex = 0 and the geometry range covering the complete first three slots. It
// isolates shader, topology, instance-stream, culling, target and readback problems from the range
// contract: if this fails, nothing else in the file is interpretable.
TEST_F(InstancedDrawRangeTest, InstancedDrawAtZeroOffsetsRendersTheRequestedRangeAndInstances)
{
    RequireInstancedRendering();

    const GridLayout layout = BackbufferLayout();
    const InstancedFixture fixture = BuildFixture(layout);

    VertexBuffer meshBuffer(
        device, PositionColorDeclaration(),
        static_cast<int>(fixture.mesh.size()), BufferUsage::None);
    meshBuffer.SetData(fixture.mesh.data(), static_cast<int>(fixture.mesh.size()));

    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits,
        static_cast<int>(fixture.indices.size()), BufferUsage::None);
    indexBuffer.SetData(fixture.indices.data(), static_cast<int>(fixture.indices.size()));

    VertexBuffer instanceBuffer(
        device, InstanceMatrixDeclaration(), kRowCount, BufferUsage::None);
    instanceBuffer.SetDataRaw(
        fixture.instances.data(), kRowCount, static_cast<int>(sizeof(InstanceMatrix)));

    BasicEffect effect(device);
    ApplyInstancedEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffers({
        VertexBufferBinding(&meshBuffer, 0, 0),
        VertexBufferBinding(&instanceBuffer, 0, 1),
    });
    device.SetIndexBuffer(&indexBuffer);
    const int instances = InstancesFor(2);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 0, 0, kSlotCount * kVerticesPerSlot, 0, 3, instances);

    const FrameSnapshot pixels = CaptureBackbuffer(device, layout.width, layout.height);
    const ExpectedRange range = ResolveExpectedRange(0, 0, 3);
    ExpectInstancedGeometryRendered(pixels, layout, range, instances, "zero-offset control");
    ExpectColumnsExclusive(pixels, layout, range, Color::Black, "zero-offset control");
    ExpectInstanceRowsExclusive(pixels, layout, instances, Color::Black, "zero-offset control");
}

// Proof 1, isolated: primitiveCount alone must limit the consumed index range. Both offsets are
// zero, so nothing here depends on either offset half of the contract; the four decoy triangles all
// live strictly after the requested range. A path that binds the complete index buffer lights them.
TEST_F(InstancedDrawRangeTest, InstancedDrawHonorsPrimitiveCountAtZeroOffsets)
{
    RequireInstancedRendering();

    const GridLayout layout = BackbufferLayout();
    const InstancedFixture fixture = BuildFixture(layout);

    VertexBuffer meshBuffer(
        device, PositionColorDeclaration(),
        static_cast<int>(fixture.mesh.size()), BufferUsage::None);
    meshBuffer.SetData(fixture.mesh.data(), static_cast<int>(fixture.mesh.size()));

    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits,
        static_cast<int>(fixture.indices.size()), BufferUsage::None);
    indexBuffer.SetData(fixture.indices.data(), static_cast<int>(fixture.indices.size()));

    VertexBuffer instanceBuffer(
        device, InstanceMatrixDeclaration(), kRowCount, BufferUsage::None);
    instanceBuffer.SetDataRaw(
        fixture.instances.data(), kRowCount, static_cast<int>(sizeof(InstanceMatrix)));

    BasicEffect effect(device);
    ApplyInstancedEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffers({
        VertexBufferBinding(&meshBuffer, 0, 0),
        VertexBufferBinding(&instanceBuffer, 0, 1),
    });
    device.SetIndexBuffer(&indexBuffer);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 0, 0, kSlotCount * kVerticesPerSlot, 0, 1, 1);

    const FrameSnapshot pixels = CaptureBackbuffer(device, layout.width, layout.height);
    const ExpectedRange range = ResolveExpectedRange(0, 0, 1);
    ExpectInstancedGeometryRendered(pixels, layout, range, 1, "primitiveCount-only range");
    ExpectColumnsExclusive(pixels, layout, range, Color::Black, "primitiveCount-only range");
    ExpectInstanceRowsExclusive(pixels, layout, 1, Color::Black, "primitiveCount-only range");
}

// Proof 2, isolated: startIndex alone must move the first consumed index element. baseVertex is
// zero, so nothing here depends on the base-addend half of the contract; every decoy triangle lives
// strictly before the requested range.
TEST_F(InstancedDrawRangeTest, InstancedDrawHonorsNonzeroStartIndex)
{
    RequireInstancedRendering();

    const GridLayout layout = BackbufferLayout();
    const InstancedFixture fixture = BuildFixture(layout);

    VertexBuffer meshBuffer(
        device, PositionColorDeclaration(),
        static_cast<int>(fixture.mesh.size()), BufferUsage::None);
    meshBuffer.SetData(fixture.mesh.data(), static_cast<int>(fixture.mesh.size()));

    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits,
        static_cast<int>(fixture.indices.size()), BufferUsage::None);
    indexBuffer.SetData(fixture.indices.data(), static_cast<int>(fixture.indices.size()));

    VertexBuffer instanceBuffer(
        device, InstanceMatrixDeclaration(), kRowCount, BufferUsage::None);
    instanceBuffer.SetDataRaw(
        fixture.instances.data(), kRowCount, static_cast<int>(sizeof(InstanceMatrix)));

    BasicEffect effect(device);
    ApplyInstancedEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffers({
        VertexBufferBinding(&meshBuffer, 0, 0),
        VertexBufferBinding(&instanceBuffer, 0, 1),
    });
    device.SetIndexBuffer(&indexBuffer);
    // Index elements [12, 21): slots 4, 5 and 6, the exact end boundary of the buffer.
    const int instances = InstancesFor(2);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 0, 0, kSlotCount * kVerticesPerSlot, 12, 3, instances);

    const FrameSnapshot pixels = CaptureBackbuffer(device, layout.width, layout.height);
    const ExpectedRange range = ResolveExpectedRange(12, 0, 3);
    ASSERT_EQ(4, range.firstSlot);
    ExpectInstancedGeometryRendered(pixels, layout, range, instances, "startIndex-only range");
    ExpectColumnsExclusive(pixels, layout, range, Color::Black, "startIndex-only range");
    ExpectInstanceRowsExclusive(
        pixels, layout, instances, Color::Black, "startIndex-only range");
}

// Proof 3, isolated: baseVertex alone must be added to every decoded index exactly once. startIndex
// is zero, so the consumed index elements are the buffer's first nine — whose *unbased* vertices are
// the three decoy triangles in slots 0, 1 and 2. A path that drops baseVertex renders those; a path
// that applies it twice would leave the buffer entirely.
TEST_F(InstancedDrawRangeTest, InstancedDrawHonorsNonzeroBaseVertex)
{
    RequireInstancedRendering();

    const GridLayout layout = BackbufferLayout();
    const InstancedFixture fixture = BuildFixture(layout);

    VertexBuffer meshBuffer(
        device, PositionColorDeclaration(),
        static_cast<int>(fixture.mesh.size()), BufferUsage::None);
    meshBuffer.SetData(fixture.mesh.data(), static_cast<int>(fixture.mesh.size()));

    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits,
        static_cast<int>(fixture.indices.size()), BufferUsage::None);
    indexBuffer.SetData(fixture.indices.data(), static_cast<int>(fixture.indices.size()));

    VertexBuffer instanceBuffer(
        device, InstanceMatrixDeclaration(), kRowCount, BufferUsage::None);
    instanceBuffer.SetDataRaw(
        fixture.instances.data(), kRowCount, static_cast<int>(sizeof(InstanceMatrix)));

    BasicEffect effect(device);
    ApplyInstancedEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffers({
        VertexBufferBinding(&meshBuffer, 0, 0),
        VertexBufferBinding(&instanceBuffer, 0, 1),
    });
    device.SetIndexBuffer(&indexBuffer);
    // baseVertex 12 rebases index elements 0..8 onto vertices 12..20: slots 4, 5 and 6.
    const int instances = InstancesFor(2);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 12, 0, kSlotCount * kVerticesPerSlot - 12, 0, 3, instances);

    const FrameSnapshot pixels = CaptureBackbuffer(device, layout.width, layout.height);
    const ExpectedRange range = ResolveExpectedRange(0, 12, 3);
    ASSERT_EQ(4, range.firstSlot);
    ExpectInstancedGeometryRendered(pixels, layout, range, instances, "baseVertex-only range");
    ExpectColumnsExclusive(pixels, layout, range, Color::Black, "baseVertex-only range");
    ExpectInstanceRowsExclusive(
        pixels, layout, instances, Color::Black, "baseVertex-only range");
}

// Proofs 2 and 3 combined, with decoy geometry on BOTH sides. The three possible partial
// implementations each land on their own distinct slot range: startIndex-only would render slots
// 1-2, baseVertex-only slots 2-3, neither slots 0-1 — none of which is the correct 3-4.
TEST_F(InstancedDrawRangeTest, InstancedDrawHonorsStartIndexAndBaseVertexTogether)
{
    RequireInstancedRendering();

    const GridLayout layout = BackbufferLayout();
    const InstancedFixture fixture = BuildFixture(layout);

    VertexBuffer meshBuffer(
        device, PositionColorDeclaration(),
        static_cast<int>(fixture.mesh.size()), BufferUsage::None);
    meshBuffer.SetData(fixture.mesh.data(), static_cast<int>(fixture.mesh.size()));

    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits,
        static_cast<int>(fixture.indices.size()), BufferUsage::None);
    indexBuffer.SetData(fixture.indices.data(), static_cast<int>(fixture.indices.size()));

    VertexBuffer instanceBuffer(
        device, InstanceMatrixDeclaration(), kRowCount, BufferUsage::None);
    instanceBuffer.SetDataRaw(
        fixture.instances.data(), kRowCount, static_cast<int>(sizeof(InstanceMatrix)));

    BasicEffect effect(device);
    ApplyInstancedEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffers({
        VertexBufferBinding(&meshBuffer, 0, 0),
        VertexBufferBinding(&instanceBuffer, 0, 1),
    });
    device.SetIndexBuffer(&indexBuffer);
    const int instances = InstancesFor(3);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 6, 0, kSlotCount * kVerticesPerSlot - 6, 3, 2, instances);

    const FrameSnapshot pixels = CaptureBackbuffer(device, layout.width, layout.height);
    const ExpectedRange range = ResolveExpectedRange(3, 6, 2);
    ASSERT_EQ(3, range.firstSlot);
    ExpectInstancedGeometryRendered(pixels, layout, range, instances, "startIndex + baseVertex");
    ExpectColumnsExclusive(pixels, layout, range, Color::Black, "startIndex + baseVertex");
    ExpectInstanceRowsExclusive(
        pixels, layout, instances, Color::Black, "startIndex + baseVertex");
}

// instanceCount is independent of the geometry range in both directions: the same requested range
// renders the same columns for one, two and four instances, and each instance count lights exactly
// its own bands. A path that multiplied the geometry count by instanceCount, or derived one from
// the other, cannot satisfy all three cases.
TEST_F(InstancedDrawRangeTest, InstanceCountIsIndependentOfTheGeometryRange)
{
    RequireInstancedRendering();
    if (!PerInstanceTransformIsApplied())
        GTEST_SKIP() << "REMED-GFX-121: this renderer does not apply the per-instance world "
                        "matrix, so the instance axis carries no information here";

    const GridLayout layout = BackbufferLayout();
    const InstancedFixture fixture = BuildFixture(layout);

    VertexBuffer meshBuffer(
        device, PositionColorDeclaration(),
        static_cast<int>(fixture.mesh.size()), BufferUsage::None);
    meshBuffer.SetData(fixture.mesh.data(), static_cast<int>(fixture.mesh.size()));

    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits,
        static_cast<int>(fixture.indices.size()), BufferUsage::None);
    indexBuffer.SetData(fixture.indices.data(), static_cast<int>(fixture.indices.size()));

    VertexBuffer instanceBuffer(
        device, InstanceMatrixDeclaration(), kRowCount, BufferUsage::None);
    instanceBuffer.SetDataRaw(
        fixture.instances.data(), kRowCount, static_cast<int>(sizeof(InstanceMatrix)));

    BasicEffect effect(device);
    for (const int instanceCount : {1, 2, kRowCount})
    {
        const std::string label =
            "instanceCount=" + std::to_string(instanceCount);
        ApplyInstancedEffect(effect);
        device.Clear(Color::Black);
        device.SetVertexBuffers({
            VertexBufferBinding(&meshBuffer, 0, 0),
            VertexBufferBinding(&instanceBuffer, 0, 1),
        });
        device.SetIndexBuffer(&indexBuffer);
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 3, 0, kSlotCount * kVerticesPerSlot - 3, 6,
            2, instanceCount);

        const FrameSnapshot pixels = CaptureBackbuffer(device, layout.width, layout.height);
        const ExpectedRange range = ResolveExpectedRange(6, 3, 2);
        ASSERT_EQ(3, range.firstSlot);
        ExpectInstancedGeometryRendered(
            pixels, layout, range, instanceCount, label.c_str());
        ExpectColumnsExclusive(pixels, layout, range, Color::Black, label.c_str());
        ExpectInstanceRowsExclusive(
            pixels, layout, instanceCount, Color::Black, label.c_str());
    }
}

// The first/middle/final boundary sweep. Every case reads its own frame, so no result can be
// produced by a later draw, and every one of the four public parameters varies independently.
TEST_F(InstancedDrawRangeTest, InstancedDrawHonorsFirstMiddleAndFinalRanges)
{
    RequireInstancedRendering();

    const GridLayout layout = BackbufferLayout();
    const InstancedFixture fixture = BuildFixture(layout);

    VertexBuffer meshBuffer(
        device, PositionColorDeclaration(),
        static_cast<int>(fixture.mesh.size()), BufferUsage::None);
    meshBuffer.SetData(fixture.mesh.data(), static_cast<int>(fixture.mesh.size()));

    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits,
        static_cast<int>(fixture.indices.size()), BufferUsage::None);
    indexBuffer.SetData(fixture.indices.data(), static_cast<int>(fixture.indices.size()));

    VertexBuffer instanceBuffer(
        device, InstanceMatrixDeclaration(), kRowCount, BufferUsage::None);
    instanceBuffer.SetDataRaw(
        fixture.instances.data(), kRowCount, static_cast<int>(sizeof(InstanceMatrix)));

    struct RangeCase
    {
        int baseVertex;
        int startIndex;
        int primitiveCount;
        int instanceCount;
        const char* label;
    };
    constexpr std::array<RangeCase, 6> cases{{
        {0, 0, 1, 1, "first single primitive, one instance"},
        {0, 0, kSlotCount, kRowCount, "complete buffer, every instance"},
        {0, 18, 1, 1, "final single primitive via startIndex"},
        {18, 0, 1, 2, "final single primitive via baseVertex"},
        {3, 6, 2, 3, "interior range with decoys on both sides"},
        {9, 3, 3, 1, "interior range reaching the buffer end"},
    }};

    BasicEffect effect(device);
    for (const RangeCase& rangeCase : cases)
    {
        ApplyInstancedEffect(effect);
        device.Clear(Color::Black);
        device.SetVertexBuffers({
            VertexBufferBinding(&meshBuffer, 0, 0),
            VertexBufferBinding(&instanceBuffer, 0, 1),
        });
        device.SetIndexBuffer(&indexBuffer);
        const int instances = InstancesFor(rangeCase.instanceCount);
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, rangeCase.baseVertex, 0,
            kSlotCount * kVerticesPerSlot - rangeCase.baseVertex,
            rangeCase.startIndex, rangeCase.primitiveCount, instances);

        const FrameSnapshot pixels = CaptureBackbuffer(device, layout.width, layout.height);
        const ExpectedRange range = ResolveExpectedRange(
            rangeCase.startIndex, rangeCase.baseVertex, rangeCase.primitiveCount);
        ExpectInstancedGeometryRendered(
            pixels, layout, range, instances, rangeCase.label);
        ExpectColumnsExclusive(pixels, layout, range, Color::Black, rangeCase.label);
        ExpectInstanceRowsExclusive(
            pixels, layout, instances, Color::Black, rangeCase.label);
    }
}

// Dynamic vertex and index buffers take the same public contract as the static pair above.
TEST_F(InstancedDrawRangeTest, InstancedDynamicBuffersHonorRangeAndInstances)
{
    RequireInstancedRendering();

    const GridLayout layout = BackbufferLayout();
    const InstancedFixture fixture = BuildFixture(layout);

    DynamicVertexBuffer meshBuffer(
        device, PositionColorDeclaration(),
        static_cast<int>(fixture.mesh.size()), BufferUsage::None);
    meshBuffer.SetData(
        fixture.mesh.data(), 0, static_cast<int>(fixture.mesh.size()), SetDataOptions::None);

    DynamicIndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits,
        static_cast<int>(fixture.indices.size()), BufferUsage::None);
    indexBuffer.SetData(
        fixture.indices.data(), 0, static_cast<int>(fixture.indices.size()),
        SetDataOptions::None);

    VertexBuffer instanceBuffer(
        device, InstanceMatrixDeclaration(), kRowCount, BufferUsage::None);
    instanceBuffer.SetDataRaw(
        fixture.instances.data(), kRowCount, static_cast<int>(sizeof(InstanceMatrix)));

    BasicEffect effect(device);
    ApplyInstancedEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffers({
        VertexBufferBinding(&meshBuffer, 0, 0),
        VertexBufferBinding(&instanceBuffer, 0, 1),
    });
    device.SetIndexBuffer(&indexBuffer);
    const int instances = InstancesFor(2);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 6, 0, kSlotCount * kVerticesPerSlot - 6, 3, 2, instances);

    const FrameSnapshot pixels = CaptureBackbuffer(device, layout.width, layout.height);
    const ExpectedRange range = ResolveExpectedRange(3, 6, 2);
    ExpectInstancedGeometryRendered(pixels, layout, range, instances, "dynamic buffers");
    ExpectColumnsExclusive(pixels, layout, range, Color::Black, "dynamic buffers");
    ExpectInstanceRowsExclusive(pixels, layout, instances, Color::Black, "dynamic buffers");
}

// A 32-bit index buffer (REMED-GFX-108's public native creation) decodes the same range: startIndex
// still counts index ELEMENTS, so the wider element width must not be mistaken for a byte offset.
TEST_F(InstancedDrawRangeTest, InstancedThirtyTwoBitIndicesHonorTheSameElementRange)
{
    RequireInstancedRendering();

    const GridLayout layout = BackbufferLayout();
    const InstancedFixture fixture = BuildFixture(layout);
    const std::vector<std::uint32_t> wideIndices = BuildIdentityIndices32();

    VertexBuffer meshBuffer(
        device, PositionColorDeclaration(),
        static_cast<int>(fixture.mesh.size()), BufferUsage::None);
    meshBuffer.SetData(fixture.mesh.data(), static_cast<int>(fixture.mesh.size()));

    IndexBuffer indexBuffer(
        device, IndexElementSize::ThirtyTwoBits,
        static_cast<int>(wideIndices.size()), BufferUsage::None);
    indexBuffer.SetData(wideIndices.data(), static_cast<int>(wideIndices.size()));

    VertexBuffer instanceBuffer(
        device, InstanceMatrixDeclaration(), kRowCount, BufferUsage::None);
    instanceBuffer.SetDataRaw(
        fixture.instances.data(), kRowCount, static_cast<int>(sizeof(InstanceMatrix)));

    BasicEffect effect(device);
    ApplyInstancedEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffers({
        VertexBufferBinding(&meshBuffer, 0, 0),
        VertexBufferBinding(&instanceBuffer, 0, 1),
    });
    device.SetIndexBuffer(&indexBuffer);
    const int instances = InstancesFor(2);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 6, 0, kSlotCount * kVerticesPerSlot - 6, 3, 2, instances);

    const FrameSnapshot pixels = CaptureBackbuffer(device, layout.width, layout.height);
    const ExpectedRange range = ResolveExpectedRange(3, 6, 2);
    ExpectInstancedGeometryRendered(pixels, layout, range, instances, "32-bit indices");
    ExpectColumnsExclusive(pixels, layout, range, Color::Black, "32-bit indices");
    ExpectInstanceRowsExclusive(pixels, layout, instances, Color::Black, "32-bit indices");
}

// A -> B -> A in one frame: three queued instanced draws with three different ranges and three
// different instance counts, then one readback. Each draw must keep its own parameters by value; a
// backend that resolved either from live state at flush time would render the last one three times.
TEST_F(InstancedDrawRangeTest, DeferredInstancedDrawsAtoBtoAKeepTheirOwnParameters)
{
    RequireInstancedRendering();

    const GridLayout layout = BackbufferLayout();
    const InstancedFixture fixture = BuildFixture(layout);

    VertexBuffer meshBuffer(
        device, PositionColorDeclaration(),
        static_cast<int>(fixture.mesh.size()), BufferUsage::None);
    meshBuffer.SetData(fixture.mesh.data(), static_cast<int>(fixture.mesh.size()));

    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits,
        static_cast<int>(fixture.indices.size()), BufferUsage::None);
    indexBuffer.SetData(fixture.indices.data(), static_cast<int>(fixture.indices.size()));

    VertexBuffer instanceBuffer(
        device, InstanceMatrixDeclaration(), kRowCount, BufferUsage::None);
    instanceBuffer.SetDataRaw(
        fixture.instances.data(), kRowCount, static_cast<int>(sizeof(InstanceMatrix)));

    BasicEffect effect(device);
    ApplyInstancedEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffers({
        VertexBufferBinding(&meshBuffer, 0, 0),
        VertexBufferBinding(&instanceBuffer, 0, 1),
    });
    device.SetIndexBuffer(&indexBuffer);
    // A: slot 0 only, one instance.  B: slots 3-4, three instances.  A again: slot 6, one instance.
    const int middleInstances = InstancesFor(3);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 0, 0, kSlotCount * kVerticesPerSlot, 0, 1, 1);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 6, 0, kSlotCount * kVerticesPerSlot - 6, 3, 2,
        middleInstances);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 18, 0, kSlotCount * kVerticesPerSlot - 18, 0, 1, 1);

    const FrameSnapshot pixels = CaptureBackbuffer(device, layout.width, layout.height);
    ExpectInstancedGeometryRendered(pixels, layout, ExpectedRange{0, 1}, 1, "A (first)");
    ExpectInstancedGeometryRendered(
        pixels, layout, ExpectedRange{3, 2}, middleInstances, "B");
    ExpectInstancedGeometryRendered(pixels, layout, ExpectedRange{6, 1}, 1, "A (again)");
    // Slots 1, 2 and 5 belong to no draw at all, in any band.
    for (const int emptySlot : {1, 2, 5})
    {
        const int x0 = static_cast<int>(layout.ColumnBoundaryX(emptySlot) + 0.999f);
        const int x1 = static_cast<int>(layout.ColumnBoundaryX(emptySlot + 1));
        EXPECT_EQ(0, pixels.CountLitInColumns(x0, x1, Color::Black))
            << "A->B->A: slot " << emptySlot << " belongs to no queued draw but rendered -- "
            << pixels.DescribeFirstLitInColumns(x0, x1, Color::Black);
    }
    // Only draw B asked for more than one instance, so bands 1 and 2 may only contain its slots.
    const int bandOneY = static_cast<int>(layout.RowBoundaryY(1) + 0.999f);
    const int bandThreeY =
        static_cast<int>(layout.RowBoundaryY(middleInstances) + 0.999f);
    EXPECT_EQ(0, pixels.CountLitInRows(bandThreeY, layout.height, Color::Black))
        << "A->B->A: an instance beyond the largest requested count rendered -- "
        << pixels.DescribeFirstLitInRows(bandThreeY, layout.height, Color::Black);
    for (const int slot : {0, 6})
    {
        const int x0 = static_cast<int>(layout.ColumnBoundaryX(slot) + 0.999f);
        const int x1 = static_cast<int>(layout.ColumnBoundaryX(slot + 1));
        int lit = 0;
        for (int y = bandOneY; y < layout.height; ++y)
            for (int x = x0; x < x1; ++x)
                if (!HasSameRgb(pixels.At(x, y), Color::Black)) ++lit;
        EXPECT_EQ(0, lit)
            << "A->B->A: single-instance draw at slot " << slot
            << " leaked into another draw's instance bands";
    }
}

// The public entry point rejects every out-of-contract request before it can reach a native draw,
// and never clamps one into a smaller valid range.
TEST_F(InstancedDrawRangeTest, InvalidInstancedRangesAreRejectedNotClamped)
{
    RequireInstancedRendering();

    const GridLayout layout = BackbufferLayout();
    const InstancedFixture fixture = BuildFixture(layout);

    VertexBuffer meshBuffer(
        device, PositionColorDeclaration(),
        static_cast<int>(fixture.mesh.size()), BufferUsage::None);
    meshBuffer.SetData(fixture.mesh.data(), static_cast<int>(fixture.mesh.size()));

    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits,
        static_cast<int>(fixture.indices.size()), BufferUsage::None);
    indexBuffer.SetData(fixture.indices.data(), static_cast<int>(fixture.indices.size()));

    VertexBuffer instanceBuffer(
        device, InstanceMatrixDeclaration(), kRowCount, BufferUsage::None);
    instanceBuffer.SetDataRaw(
        fixture.instances.data(), kRowCount, static_cast<int>(sizeof(InstanceMatrix)));

    BasicEffect effect(device);
    ApplyInstancedEffect(effect);
    device.SetVertexBuffers({
        VertexBufferBinding(&meshBuffer, 0, 0),
        VertexBufferBinding(&instanceBuffer, 0, 1),
    });
    device.SetIndexBuffer(&indexBuffer);

    constexpr int kIndexCount = kSlotCount * kVerticesPerSlot;
    // Negative and non-positive scalars.
    EXPECT_THROW(
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 0, 0, kIndexCount, -1, 1, 1),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, -1, 0, kIndexCount, 0, 1, 1),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 0, -1, kIndexCount, 0, 1, 1),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 0, 0, 0, 0, 1, 1),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 0, 0, kIndexCount, 0, 0, 1),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 0, 0, kIndexCount, 0, 1, 0),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 0, 0, kIndexCount, 0, 1, -2),
        System::ArgumentOutOfRangeException);

    // Index range leaving the logical index buffer, both by offset and by count.
    EXPECT_THROW(
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 0, 0, kIndexCount, kIndexCount, 1, 1),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 0, 0, kIndexCount, 19, 1, 1),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 0, 0, kIndexCount, 0, kSlotCount + 1, 1),
        System::ArgumentOutOfRangeException);

    // Declared vertex range leaving the logical vertex buffer after baseVertex.
    EXPECT_THROW(
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, kIndexCount + 1, 0, 1, 0, 1, 1),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 18, 0, kIndexCount, 0, 1, 1),
        System::ArgumentOutOfRangeException);

    // More instances than the bound per-instance stream can supply.
    EXPECT_THROW(
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 0, 0, kIndexCount, 0, 1, kRowCount + 1),
        System::ArgumentOutOfRangeException);

    // Topology-count overflow: 3 * primitiveCount must be computed in checked arithmetic and
    // rejected, never wrapped into a small valid-looking count.
    EXPECT_THROW(
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 0, 0, kIndexCount, 0,
            std::numeric_limits<int>::max(), 1),
        System::ArgumentOutOfRangeException);

    // A rejected request must leave the device able to draw the valid range that follows it.
    device.Clear(Color::Black);
    const int instances = InstancesFor(2);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 6, 0, kIndexCount - 6, 3, 2, instances);
    const FrameSnapshot pixels = CaptureBackbuffer(device, layout.width, layout.height);
    const ExpectedRange range = ResolveExpectedRange(3, 6, 2);
    ExpectInstancedGeometryRendered(
        pixels, layout, range, instances, "after rejected requests");
    ExpectColumnsExclusive(pixels, layout, range, Color::Black, "after rejected requests");
}

// The range contract holds on a RenderTarget2D exactly as on the backbuffer, the target-only draw
// never reaches the backbuffer, and the backbuffer draw that follows the target keeps its own range.
TEST_F(InstancedDrawRangeTest, InstancedDrawIntoRenderTargetKeepsItsRangeAndInstances)
{
    RequireInstancedRendering();

    const GridLayout layout = BackbufferLayout();
    const InstancedFixture fixture = BuildFixture(layout);

    VertexBuffer meshBuffer(
        device, PositionColorDeclaration(),
        static_cast<int>(fixture.mesh.size()), BufferUsage::None);
    meshBuffer.SetData(fixture.mesh.data(), static_cast<int>(fixture.mesh.size()));

    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits,
        static_cast<int>(fixture.indices.size()), BufferUsage::None);
    indexBuffer.SetData(fixture.indices.data(), static_cast<int>(fixture.indices.size()));

    VertexBuffer instanceBuffer(
        device, InstanceMatrixDeclaration(), kRowCount, BufferUsage::None);
    instanceBuffer.SetDataRaw(
        fixture.instances.data(), kRowCount, static_cast<int>(sizeof(InstanceMatrix)));

    Microsoft::Xna::Framework::Graphics::RenderTarget2D target(
        device, layout.width, layout.height, false,
        Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color,
        Microsoft::Xna::Framework::Graphics::DepthFormat::None, 0,
        Microsoft::Xna::Framework::Graphics::RenderTargetUsage::PreserveContents);

    BasicEffect effect(device);
    device.SetVertexBuffers({
        VertexBufferBinding(&meshBuffer, 0, 0),
        VertexBufferBinding(&instanceBuffer, 0, 1),
    });
    device.SetIndexBuffer(&indexBuffer);
    device.Clear(Color::Black);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyInstancedEffect(effect);
    const int instances = InstancesFor(2);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 6, 0, kSlotCount * kVerticesPerSlot - 6, 3, 2, instances);
    device.SetRenderTarget(
        static_cast<Microsoft::Xna::Framework::Graphics::RenderTarget2D*>(nullptr));

    // Read the backbuffer first: the target-only draw must not have reached it, and the readback
    // finishes the producing frame -- a render target must not be sampled in the frame that wrote
    // it.
    const FrameSnapshot untouchedBackbuffer =
        CaptureBackbuffer(device, layout.width, layout.height);
    EXPECT_EQ(0, untouchedBackbuffer.CountLitInColumns(0, layout.width, Color::Black))
        << "a render-target-only instanced draw reached the backbuffer";

    device.SetVertexBuffer(nullptr);
    SampleTargetToBackbuffer(device, target);
    const FrameSnapshot fromTarget =
        CaptureBackbuffer(device, layout.width, layout.height);
    const ExpectedRange range = ResolveExpectedRange(3, 6, 2);
    ExpectInstancedGeometryRendered(
        fromTarget, layout, range, instances, "render-target range");
    ExpectColumnsExclusive(fromTarget, layout, range, Color::Black, "render-target range");
    ExpectInstanceRowsExclusive(
        fromTarget, layout, instances, Color::Black, "render-target range");

    // Back on the backbuffer, with a different range: target -> backbuffer -> target must not leak
    // either draw's parameters into the other.
    device.setDepthStencilStateProperty(DepthStencilState::None);
    device.SetVertexBuffers({
        VertexBufferBinding(&meshBuffer, 0, 0),
        VertexBufferBinding(&instanceBuffer, 0, 1),
    });
    device.SetIndexBuffer(&indexBuffer);
    ApplyInstancedEffect(effect);
    device.Clear(Color::Black);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 0, 0, kSlotCount * kVerticesPerSlot, 0, 1, 1);
    const FrameSnapshot backAgain = CaptureBackbuffer(device, layout.width, layout.height);
    const ExpectedRange backRange = ResolveExpectedRange(0, 0, 1);
    ExpectInstancedGeometryRendered(backAgain, layout, backRange, 1, "backbuffer after target");
    ExpectColumnsExclusive(backAgain, layout, backRange, Color::Black, "backbuffer after target");
    ExpectInstanceRowsExclusive(backAgain, layout, 1, Color::Black, "backbuffer after target");
}

// Viewport and scissor apply to the instanced draw exactly as to any other, and neither changes
// which geometry the draw consumed.
TEST_F(InstancedDrawRangeTest, InstancedDrawRespectsViewportAndScissor)
{
    RequireInstancedRendering();

    const GridLayout layout = BackbufferLayout();
    const InstancedFixture fixture = BuildFixture(layout);

    VertexBuffer meshBuffer(
        device, PositionColorDeclaration(),
        static_cast<int>(fixture.mesh.size()), BufferUsage::None);
    meshBuffer.SetData(fixture.mesh.data(), static_cast<int>(fixture.mesh.size()));

    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits,
        static_cast<int>(fixture.indices.size()), BufferUsage::None);
    indexBuffer.SetData(fixture.indices.data(), static_cast<int>(fixture.indices.size()));

    VertexBuffer instanceBuffer(
        device, InstanceMatrixDeclaration(), kRowCount, BufferUsage::None);
    instanceBuffer.SetDataRaw(
        fixture.instances.data(), kRowCount, static_cast<int>(sizeof(InstanceMatrix)));

    BasicEffect effect(device);

    // A half-size top-left viewport maps the whole clip-space fixture into that quadrant.
    const int halfWidth = layout.width / 2;
    const int halfHeight = layout.height / 2;
    const int scissorX = static_cast<int>(layout.ColumnBoundaryX(4));
    const Rectangle fullTarget(0, 0, layout.width, layout.height);
    const int instances = InstancesFor(2);

    RasterizerState scissored;
    scissored.setCullModeProperty(Microsoft::Xna::Framework::Graphics::CullMode::None);
    scissored.setScissorTestEnableProperty(true);

    /// One frame under one narrowed rectangle. Clearing happens with the FULL viewport and the
    /// scissor test off, because XNA's Clear -- and D3D9's native Clear with it -- is scoped to
    /// exactly those rectangles: narrowing first would leave the previous frame's pixels alive
    /// outside them and every "nothing rendered here" assertion below would be reading them.
    const auto renderUnder = [&](bool narrowViewport, bool narrowScissor, bool instanced) {
        device.setViewportProperty(Viewport(0, 0, layout.width, layout.height));
        device.setScissorRectangleProperty(fullTarget);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        ApplyInstancedEffect(effect);
        device.Clear(Color::Black);

        if (narrowViewport)
            device.setViewportProperty(Viewport(0, 0, halfWidth, halfHeight));
        if (narrowScissor)
        {
            device.setScissorRectangleProperty(
                Rectangle(scissorX, 0, layout.width - scissorX, layout.height));
            device.setRasterizerStateProperty(scissored);
        }
        if (instanced)
        {
            device.SetVertexBuffers({
                VertexBufferBinding(&meshBuffer, 0, 0),
                VertexBufferBinding(&instanceBuffer, 0, 1),
            });
        }
        else
        {
            device.SetVertexBuffer(&meshBuffer);
        }
        device.SetIndexBuffer(&indexBuffer);
        ApplyInstancedEffect(effect);
        if (instanced)
        {
            device.DrawInstancedPrimitives(
                PrimitiveType::TriangleList, 6, 0, kSlotCount * kVerticesPerSlot - 6, 3, 2,
                instances);
        }
        else
        {
            device.DrawIndexedPrimitives(
                PrimitiveType::TriangleList, 6, 0, kSlotCount * kVerticesPerSlot - 6, 3, 2);
        }
        const FrameSnapshot captured =
            CaptureBackbuffer(device, layout.width, layout.height);
        device.setViewportProperty(Viewport(0, 0, layout.width, layout.height));
        device.setScissorRectangleProperty(fullTarget);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        return captured;
    };

    // Control first: the ORDINARY indexed draw, same buffers and same requested range, under the
    // same rectangles. This test is about whether the *instanced* path inherits them; a backend
    // that does not apply them to any 3D draw is a separate matter, so establish that the backend
    // can do it at all before holding the instanced path to it.
    const FrameSnapshot viewportControl = renderUnder(true, false, false);
    if (viewportControl.CountLitInColumns(halfWidth, layout.width, Color::Black) != 0 ||
        viewportControl.CountLitInRows(halfHeight, layout.height, Color::Black) != 0)
    {
        GTEST_SKIP()
            << "this backend does not clip an ordinary indexed 3D draw to Viewport either, so the "
               "instanced path cannot be held to it here -- "
            << viewportControl.DescribeFirstLitInColumns(halfWidth, layout.width, Color::Black);
    }

    const FrameSnapshot viewportPixels = renderUnder(true, false, true);
    EXPECT_EQ(0, viewportPixels.CountLitInColumns(halfWidth, layout.width, Color::Black))
        << "the instanced draw rendered outside its viewport -- "
        << viewportPixels.DescribeFirstLitInColumns(halfWidth, layout.width, Color::Black);
    EXPECT_EQ(0, viewportPixels.CountLitInRows(halfHeight, layout.height, Color::Black))
        << "the instanced draw rendered below its viewport -- "
        << viewportPixels.DescribeFirstLitInRows(halfHeight, layout.height, Color::Black);
    // Slots 3 and 4 of every requested instance, all scaled into the top-left quadrant.
    for (int row = 0; row < instances; ++row)
    {
        for (int slot = 3; slot <= 4; ++slot)
        {
            const int probeX = static_cast<int>(layout.ColumnCenterX(slot) / 2.0f);
            const int probeY = static_cast<int>(
                (layout.RowCenterY(row) + layout.HalfHeight() / 3.0f) / 2.0f);
            EXPECT_TRUE(viewportPixels.HasRgbWithin(probeX, probeY, 2, Color::White))
                << "viewport: instance " << row << " slot " << slot
                << " missing at " << probeX << ',' << probeY;
        }
    }

    // A right-half scissor clips the same range without changing which slots it consumed.
    const FrameSnapshot scissorControl = renderUnder(false, true, false);
    if (scissorControl.CountLitInColumns(0, scissorX, Color::Black) != 0)
    {
        GTEST_SKIP()
            << "this backend does not clip an ordinary indexed 3D draw to ScissorRectangle either, "
               "so the instanced path cannot be held to it here -- "
            << scissorControl.DescribeFirstLitInColumns(0, scissorX, Color::Black);
    }

    const FrameSnapshot scissorPixels = renderUnder(false, true, true);
    EXPECT_EQ(0, scissorPixels.CountLitInColumns(0, scissorX, Color::Black))
        << "the instanced draw rendered outside its scissor rectangle -- "
        << scissorPixels.DescribeFirstLitInColumns(0, scissorX, Color::Black);
    for (int row = 0; row < instances; ++row)
    {
        const int probeX = static_cast<int>(layout.ColumnCenterX(4));
        const int probeY = static_cast<int>(
            layout.RowCenterY(row) + layout.HalfHeight() / 3.0f);
        EXPECT_TRUE(scissorPixels.HasRgbWithin(probeX, probeY, 2, Color::White))
            << "scissor: instance " << row << " slot 4 missing at " << probeX << ',' << probeY;
    }
    EXPECT_EQ(0, CountLitInCell(scissorPixels, layout, 5, 0, Color::Black))
        << "the scissored instanced draw consumed geometry outside its requested range"
        << DescribeLitCellMap(scissorPixels, layout, Color::Black)
        << DescribeLitBounds(scissorPixels, Color::Black);
}

// Explicit non-default rasterizer, blend and depth state reaches the instanced draw, and none of it
// changes which geometry the draw consumed. Culling is proven orientation-sensitive without
// hardcoding a winding convention: exactly one of the two cull modes must remove the fixture.
TEST_F(InstancedDrawRangeTest, InstancedDrawAppliesExplicitStateWithoutChangingItsRange)
{
    RequireInstancedRendering();

    const GridLayout layout = BackbufferLayout();
    const InstancedFixture fixture = BuildFixture(layout);

    VertexBuffer meshBuffer(
        device, PositionColorDeclaration(),
        static_cast<int>(fixture.mesh.size()), BufferUsage::None);
    meshBuffer.SetData(fixture.mesh.data(), static_cast<int>(fixture.mesh.size()));

    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits,
        static_cast<int>(fixture.indices.size()), BufferUsage::None);
    indexBuffer.SetData(fixture.indices.data(), static_cast<int>(fixture.indices.size()));

    VertexBuffer instanceBuffer(
        device, InstanceMatrixDeclaration(), kRowCount, BufferUsage::None);
    instanceBuffer.SetDataRaw(
        fixture.instances.data(), kRowCount, static_cast<int>(sizeof(InstanceMatrix)));

    BasicEffect effect(device);
    const int instances = InstancesFor(2);
    const auto drawWithState = [&](const RasterizerState& raster) {
        device.setRasterizerStateProperty(raster);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.SetVertexBuffers({
            VertexBufferBinding(&meshBuffer, 0, 0),
            VertexBufferBinding(&instanceBuffer, 0, 1),
        });
        device.SetIndexBuffer(&indexBuffer);
        ApplyInstancedEffect(effect);
        device.Clear(Color::Black);
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 6, 0, kSlotCount * kVerticesPerSlot - 6, 3, 2,
            instances);
        return CaptureBackbuffer(device, layout.width, layout.height);
    };

    const FrameSnapshot clockwise = drawWithState(RasterizerState::CullClockwise);
    const FrameSnapshot counterClockwise =
        drawWithState(RasterizerState::CullCounterClockwise);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::None);

    const int clockwiseLit = clockwise.CountLitInColumns(0, layout.width, Color::Black);
    const int counterLit = counterClockwise.CountLitInColumns(0, layout.width, Color::Black);
    EXPECT_TRUE((clockwiseLit == 0) != (counterLit == 0))
        << "CullMode does not reach the instanced draw: clockwise lit " << clockwiseLit
        << ", counter-clockwise lit " << counterLit
        << " (exactly one winding must be culled)";

    // The surviving winding still consumed exactly the requested range under explicit depth,
    // blend and rasterizer state.
    const FrameSnapshot& survivor = (clockwiseLit == 0) ? counterClockwise : clockwise;
    const ExpectedRange range = ResolveExpectedRange(3, 6, 2);
    ExpectInstancedGeometryRendered(survivor, layout, range, instances, "explicit state");
    ExpectColumnsExclusive(survivor, layout, range, Color::Black, "explicit state");
    ExpectInstanceRowsExclusive(survivor, layout, instances, Color::Black, "explicit state");
}

// A queued instanced draw owns the bytes its buffers held at draw time. Updating the vertex and
// index buffers afterwards must feed only the draws that follow the update.
TEST_F(InstancedDrawRangeTest, SourceUpdatesAfterAQueuedInstancedDrawDoNotAlterIt)
{
    RequireInstancedRendering();

    const GridLayout layout = BackbufferLayout();
    const InstancedFixture fixture = BuildFixture(layout);
    // The replacement mesh puts every slot's triangle in band 3 instead of band 0, so a draw that
    // read the wrong version lands in a different band, not merely a different shade.
    const std::vector<VertexPositionColor> replacementMesh =
        BuildSlotMeshInBand(layout, 3);

    DynamicVertexBuffer meshBuffer(
        device, PositionColorDeclaration(),
        static_cast<int>(fixture.mesh.size()), BufferUsage::None);
    meshBuffer.SetData(
        fixture.mesh.data(), 0, static_cast<int>(fixture.mesh.size()), SetDataOptions::None);

    DynamicIndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits,
        static_cast<int>(fixture.indices.size()), BufferUsage::None);
    indexBuffer.SetData(
        fixture.indices.data(), 0, static_cast<int>(fixture.indices.size()),
        SetDataOptions::None);

    VertexBuffer instanceBuffer(
        device, InstanceMatrixDeclaration(), kRowCount, BufferUsage::None);
    instanceBuffer.SetDataRaw(
        fixture.instances.data(), kRowCount, static_cast<int>(sizeof(InstanceMatrix)));

    BasicEffect effect(device);
    ApplyInstancedEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffers({
        VertexBufferBinding(&meshBuffer, 0, 0),
        VertexBufferBinding(&instanceBuffer, 0, 1),
    });
    device.SetIndexBuffer(&indexBuffer);
    // Queued against the band-0 mesh: slot 0, one instance -> cell (0, band 0).
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 0, 0, kSlotCount * kVerticesPerSlot, 0, 1, 1);

    meshBuffer.SetData(
        replacementMesh.data(), 0, static_cast<int>(replacementMesh.size()),
        SetDataOptions::None);
    // Queued against the band-3 mesh: slot 6, one instance -> cell (6, band 3).
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 18, 0, kSlotCount * kVerticesPerSlot - 18, 0, 1, 1);

    const FrameSnapshot pixels = CaptureBackbuffer(device, layout.width, layout.height);
    EXPECT_GT(CountLitInCell(pixels, layout, 0, 0, Color::Black), 0)
        << "the draw queued before SetData lost the bytes it captured";
    EXPECT_GT(CountLitInCell(pixels, layout, 6, 3, Color::Black), 0)
        << "the draw queued after SetData did not see the updated bytes";
    EXPECT_EQ(0, CountLitInCell(pixels, layout, 0, 3, Color::Black))
        << "the draw queued before SetData was rewritten by the later update";
    EXPECT_EQ(0, CountLitInCell(pixels, layout, 6, 0, Color::Black))
        << "the draw queued after SetData still used the pre-update bytes";
}

// The public buffers may be disposed while draws that referenced them are still queued for the
// frame, including the per-instance stream.
TEST_F(InstancedDrawRangeTest, DisposingAfterQueuedInstancedDrawsIsSafe)
{
    RequireInstancedRendering();

    const GridLayout layout = BackbufferLayout();
    const InstancedFixture fixture = BuildFixture(layout);

    {
        DynamicVertexBuffer meshBuffer(
            device, PositionColorDeclaration(),
            static_cast<int>(fixture.mesh.size()), BufferUsage::None);
        meshBuffer.SetData(
            fixture.mesh.data(), 0, static_cast<int>(fixture.mesh.size()),
            SetDataOptions::None);

        DynamicIndexBuffer indexBuffer(
            device, IndexElementSize::SixteenBits,
            static_cast<int>(fixture.indices.size()), BufferUsage::None);
        indexBuffer.SetData(
            fixture.indices.data(), 0, static_cast<int>(fixture.indices.size()),
            SetDataOptions::None);

        VertexBuffer instanceBuffer(
            device, InstanceMatrixDeclaration(), kRowCount, BufferUsage::None);
        instanceBuffer.SetDataRaw(
            fixture.instances.data(), kRowCount, static_cast<int>(sizeof(InstanceMatrix)));

        BasicEffect effect(device);
        ApplyInstancedEffect(effect);
        device.Clear(Color::Black);
        device.SetVertexBuffers({
            VertexBufferBinding(&meshBuffer, 0, 0),
            VertexBufferBinding(&instanceBuffer, 0, 1),
        });
        device.SetIndexBuffer(&indexBuffer);
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 0, 0, kSlotCount * kVerticesPerSlot, 0, 1, 2);
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 6, 0, kSlotCount * kVerticesPerSlot - 6, 3, 2, 3);
        // Instance counts here only have to be legal -- this case is about lifetime, not pixels.
        meshBuffer.SetData(
            fixture.mesh.data(), 0, static_cast<int>(fixture.mesh.size()),
            SetDataOptions::None);
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 18, 0, kSlotCount * kVerticesPerSlot - 18, 0, 1, 1);

        device.SetIndexBuffer(nullptr);
        device.SetVertexBuffers({});
        device.SetVertexBuffer(nullptr);
        instanceBuffer.Dispose();
        indexBuffer.Dispose();
        meshBuffer.Dispose();
    }

    EXPECT_NO_THROW(device.Present());
    EXPECT_NO_THROW(device.Present());
}
#endif

#ifdef CNA_BACKEND_BGFX
// REMED-GFX-121, pinned: `vs_instanced3d.sc` builds the per-instance world matrix with the raw
// `mat4(i_data0, i_data1, i_data2, i_data3)` constructor. bgfx maps `mat4()` to GLSL's COLUMN
// constructor on the GLSL profile and to HLSL's `float4x4` ROW constructor on SPIR-V/HLSL/Metal/
// WGSL -- which is exactly why bgfx supplies `mtxFromCols()` and uses it in its own instancing
// example. The matrix is therefore transposed on every non-GLSL Bgfx renderer: the translation
// lands in `w`, so instance 0 (identity) is correct and every later instance is projected
// somewhere unpredictable. Proven pre-existing by A/B against this task's fix and NOT corrected
// here -- REMED-GFX-118 is a geometry-range defect and correcting this one means changing a shader
// source and regenerating bgfx_shaders.hpp. Pinning the current per-renderer behaviour keeps the
// boundary explicit and makes the follow-up fix an obvious, deliberate change.
TEST_F(InstancedDrawRangeTest, BgfxPerInstanceWorldMatrixIsAppliedOnGlslRenderersOnly)
{
    RequireInstancedRendering();

    const bgfx::RendererType::Enum renderer = bgfx::getRendererType();
    if (renderer == bgfx::RendererType::OpenGL || renderer == bgfx::RendererType::OpenGLES)
    {
        EXPECT_TRUE(PerInstanceTransformIsApplied())
            << "the GLSL profile builds the per-instance matrix from columns and must apply it";
    }
    else if (renderer == bgfx::RendererType::Vulkan)
    {
        EXPECT_FALSE(PerInstanceTransformIsApplied())
            << "REMED-GFX-121 appears fixed on the Vulkan renderer -- flip this pin, switch "
               "vs_instanced3d.sc to mtxFromCols() for every profile and close the finding";
    }
    else
    {
        GTEST_SKIP() << "renderer " << bgfx::getRendererName(renderer)
                     << " is outside this pin's verified routes";
    }
}

// The exact native bindings, not just their pixels. bgfx offers no way to read a submitted draw's
// stream ranges back, so BgfxGraphicsBackend records the (startVertex, numVertices),
// (firstIndex, numIndices) and instance-count triple it handed to bgfx. The whole-buffer overloads
// this replaced carried none of the public range at all.
TEST_F(InstancedDrawRangeTest, BgfxInstancedBindingsAreTheExactPublicRanges)
{
    RequireInstancedRendering();

    auto* backend =
        dynamic_cast<CNA::Internal::Backends::Bgfx::BgfxGraphicsBackend*>(&device.GetBackend());
    ASSERT_NE(nullptr, backend);

    const GridLayout layout = BackbufferLayout();
    const InstancedFixture fixture = BuildFixture(layout);
    constexpr int kElementCount = kSlotCount * kVerticesPerSlot;

    VertexBuffer meshBuffer(
        device, PositionColorDeclaration(), kElementCount, BufferUsage::None);
    meshBuffer.SetData(fixture.mesh.data(), kElementCount);

    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kElementCount, BufferUsage::None);
    indexBuffer.SetData(fixture.indices.data(), kElementCount);

    VertexBuffer instanceBuffer(
        device, InstanceMatrixDeclaration(), kRowCount, BufferUsage::None);
    instanceBuffer.SetDataRaw(
        fixture.instances.data(), kRowCount, static_cast<int>(sizeof(InstanceMatrix)));

    struct BindingCase
    {
        PrimitiveType primitive;
        int baseVertex;
        int startIndex;
        int primitiveCount;
        int instanceCount;
        std::uint32_t expectedIndexCount;
    };
    // Every topology the Bgfx instanced path maps (REMED-GFX-111's BGFX_STATE_PT_* table), each
    // with its own offsets so no case can pass on another's numbers.
    constexpr std::array<BindingCase, 8> cases{{
        {PrimitiveType::TriangleList, 0, 0, 1, 1, 3},
        {PrimitiveType::TriangleList, 0, 12, 3, 2, 9},
        {PrimitiveType::TriangleList, 12, 0, 3, kRowCount, 9},
        {PrimitiveType::TriangleList, 6, 3, 2, 3, 6},
        {PrimitiveType::TriangleStrip, 3, 2, 2, 2, 4},
        {PrimitiveType::LineList, 6, 4, 3, 1, 6},
        {PrimitiveType::LineStrip, 9, 2, 3, 2, 4},
        {PrimitiveType::PointListEXT, 12, 5, 4, 3, 4},
    }};

    BasicEffect effect(device);
    for (const BindingCase& bindingCase : cases)
    {
        ApplyInstancedEffect(effect);
        device.Clear(Color::Black);
        device.SetVertexBuffers({
            VertexBufferBinding(&meshBuffer, 0, 0),
            VertexBufferBinding(&instanceBuffer, 0, 1),
        });
        device.SetIndexBuffer(&indexBuffer);
        device.DrawInstancedPrimitives(
            bindingCase.primitive, bindingCase.baseVertex, 0,
            kElementCount - bindingCase.baseVertex,
            bindingCase.startIndex, bindingCase.primitiveCount, bindingCase.instanceCount);

        EXPECT_EQ(
            static_cast<std::uint32_t>(bindingCase.startIndex),
            backend->lastInstancedIndexBindStartEXT_)
            << TopologyName(bindingCase.primitive)
            << ": native firstIndex is not the public startIndex element offset";
        EXPECT_EQ(bindingCase.expectedIndexCount, backend->lastInstancedIndexBindCountEXT_)
            << TopologyName(bindingCase.primitive)
            << ": native numIndices is not the topology-derived consumed count";
        EXPECT_LE(
            backend->lastInstancedIndexBindStartEXT_ +
                backend->lastInstancedIndexBindCountEXT_,
            static_cast<std::uint32_t>(kElementCount))
            << TopologyName(bindingCase.primitive)
            << ": native index binding leaves the logical index buffer";
        EXPECT_EQ(
            static_cast<std::uint32_t>(bindingCase.baseVertex),
            backend->lastInstancedVertexBindStartEXT_)
            << TopologyName(bindingCase.primitive)
            << ": native startVertex is not the public baseVertex addend";
        EXPECT_EQ(
            static_cast<std::uint32_t>(kElementCount - bindingCase.baseVertex),
            backend->lastInstancedVertexBindCountEXT_)
            << TopologyName(bindingCase.primitive)
            << ": native numVertices is not the safe remainder after baseVertex";
        EXPECT_EQ(
            static_cast<std::uint32_t>(bindingCase.instanceCount),
            backend->lastInstancedInstanceCountEXT_)
            << TopologyName(bindingCase.primitive)
            << ": native instance count is not the public instanceCount";
    }
}

// FillMode.WireFrame re-expands the requested triangles into an absolute-index line list, so that
// path owns the range through its own indices and must keep binding the vertex stream from element
// zero -- while still drawing only the requested triangles' edges, for every instance.
TEST_F(InstancedDrawRangeTest, BgfxInstancedWireframeKeepsItsRangeWithAZeroBasedVertexBinding)
{
    RequireInstancedRendering();

    auto* backend =
        dynamic_cast<CNA::Internal::Backends::Bgfx::BgfxGraphicsBackend*>(&device.GetBackend());
    ASSERT_NE(nullptr, backend);

    const GridLayout layout = BackbufferLayout();
    const InstancedFixture fixture = BuildFixture(layout);
    constexpr int kElementCount = kSlotCount * kVerticesPerSlot;

    VertexBuffer meshBuffer(
        device, PositionColorDeclaration(), kElementCount, BufferUsage::None);
    meshBuffer.SetData(fixture.mesh.data(), kElementCount);

    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kElementCount, BufferUsage::None);
    indexBuffer.SetData(fixture.indices.data(), kElementCount);

    VertexBuffer instanceBuffer(
        device, InstanceMatrixDeclaration(), kRowCount, BufferUsage::None);
    instanceBuffer.SetDataRaw(
        fixture.instances.data(), kRowCount, static_cast<int>(sizeof(InstanceMatrix)));

    RasterizerState wireframe;
    wireframe.setCullModeProperty(Microsoft::Xna::Framework::Graphics::CullMode::None);
    wireframe.setFillModeProperty(
        Microsoft::Xna::Framework::Graphics::FillMode::WireFrame);
    device.setRasterizerStateProperty(wireframe);

    BasicEffect effect(device);
    ApplyInstancedEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffers({
        VertexBufferBinding(&meshBuffer, 0, 0),
        VertexBufferBinding(&instanceBuffer, 0, 1),
    });
    device.SetIndexBuffer(&indexBuffer);
    const int instances = InstancesFor(2);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 6, 0, kElementCount - 6, 3, 2, instances);

    // The expanded indices are absolute (startIndex + local, plus baseVertex), so the stream must
    // start at zero and keep every vertex those indices can address bound.
    EXPECT_EQ(0u, backend->lastInstancedVertexBindStartEXT_)
        << "the wireframe path's absolute expanded indices need a zero-based vertex binding";
    EXPECT_EQ(
        static_cast<std::uint32_t>(kElementCount),
        backend->lastInstancedVertexBindCountEXT_)
        << "the wireframe path must keep every vertex its expanded indices can address bound";
    // Two triangles, three edges each, two indices per edge.
    EXPECT_EQ(12u, backend->lastInstancedIndexBindCountEXT_)
        << "the wireframe path expanded the wrong number of edges for the requested range";
    EXPECT_EQ(
        static_cast<std::uint32_t>(instances), backend->lastInstancedInstanceCountEXT_);

    const FrameSnapshot pixels = CaptureBackbuffer(device, layout.width, layout.height);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    const ExpectedRange range = ResolveExpectedRange(3, 6, 2);
    SCOPED_TRACE(DescribeLitCellMap(pixels, layout, Color::Black) +
                 DescribeLitBounds(pixels, Color::Black));
    ExpectColumnsExclusive(pixels, layout, range, Color::Black, "wireframe range");
    ExpectInstanceRowsExclusive(pixels, layout, instances, Color::Black, "wireframe range");
}

// Narrowing the bindings must stay free: no repacking, no per-draw handle and no extra native
// buffer version. REMED-GFX-109's cardinality therefore stays at the created-object baseline across
// many different ranges and instance counts, and returns to the process baseline after disposal.
TEST_F(InstancedDrawRangeTest, BgfxInstancedRangesAllocateNoPerDrawNativeResources)
{
    RequireInstancedRendering();

    device.Present();
    device.Present();
    const bgfx::Stats* stats = bgfx::getStats();
    ASSERT_NE(nullptr, stats);
    const std::uint16_t processVertexBaseline = stats->numDynamicVertexBuffers;
    const std::uint16_t processIndexBaseline = stats->numDynamicIndexBuffers;

    const GridLayout layout = BackbufferLayout();
    const InstancedFixture fixture = BuildFixture(layout);
    constexpr int kElementCount = kSlotCount * kVerticesPerSlot;

    VertexBuffer meshBuffer(
        device, PositionColorDeclaration(), kElementCount, BufferUsage::None);
    meshBuffer.SetData(fixture.mesh.data(), kElementCount);

    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kElementCount, BufferUsage::None);
    indexBuffer.SetData(fixture.indices.data(), kElementCount);

    VertexBuffer instanceBuffer(
        device, InstanceMatrixDeclaration(), kRowCount, BufferUsage::None);
    instanceBuffer.SetDataRaw(
        fixture.instances.data(), kRowCount, static_cast<int>(sizeof(InstanceMatrix)));

    BasicEffect effect(device);
    ApplyInstancedEffect(effect);
    device.SetVertexBuffers({
        VertexBufferBinding(&meshBuffer, 0, 0),
        VertexBufferBinding(&instanceBuffer, 0, 1),
    });
    device.SetIndexBuffer(&indexBuffer);

    device.Present();
    device.Present();
    stats = bgfx::getStats();
    ASSERT_NE(nullptr, stats);
    const std::uint16_t liveVertexBaseline = stats->numDynamicVertexBuffers;
    const std::uint16_t liveIndexBaseline = stats->numDynamicIndexBuffers;
    EXPECT_EQ(processVertexBaseline + 2u, liveVertexBaseline);
    EXPECT_EQ(processIndexBaseline + 1u, liveIndexBaseline);

    for (int frame = 0; frame < 24; ++frame)
    {
        device.Clear(Color::Black);
        for (int slot = 0; slot < kSlotCount; ++slot)
            device.DrawInstancedPrimitives(
                PrimitiveType::TriangleList, slot * kVerticesPerSlot, 0,
                kElementCount - slot * kVerticesPerSlot, 0, 1,
                1 + (slot % kRowCount));
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 0, 0, kElementCount, 0, kSlotCount, kRowCount);
        device.DrawInstancedPrimitives(
            PrimitiveType::LineList, 6, 4, 3, 0, 1, 2);
        device.Present();

        stats = bgfx::getStats();
        ASSERT_NE(nullptr, stats);
        EXPECT_LE(stats->numDynamicVertexBuffers, liveVertexBaseline);
        EXPECT_LE(stats->numDynamicIndexBuffers, liveIndexBaseline);
    }

    device.SetIndexBuffer(nullptr);
    device.SetVertexBuffers({});
    device.SetVertexBuffer(nullptr);
    instanceBuffer.Dispose();
    indexBuffer.Dispose();
    meshBuffer.Dispose();
    device.Present();
    device.Present();

    stats = bgfx::getStats();
    ASSERT_NE(nullptr, stats);
    EXPECT_EQ(processVertexBaseline, stats->numDynamicVertexBuffers);
    EXPECT_EQ(processIndexBaseline, stats->numDynamicIndexBuffers);
}
#endif

#ifdef CNA_BACKEND_EASYGL
// EasyGL's stock (non-custom-effect) instanced branch calls
// draw_elements_instanced(topology, indexCount, type, nullptr, instanceCount) and never touches
// params.startIndex, params.baseVertex or params.instanceVb at all: the range starts at element
// zero and every instance renders the same untransformed geometry. That is REMED-GFX-118's defect
// class in a different backend, recorded as its own finding and NOT corrected here. Pinning the
// current (wrong) result keeps the boundary explicit and makes the follow-up fix an obvious,
// deliberate change.
TEST_F(InstancedDrawRangeTest, EasyGLStockInstancedPathIgnoresOffsetsAndTheInstanceStream)
{
    RequireInstancedRendering();

    const GridLayout layout = BackbufferLayout();
    const InstancedFixture fixture = BuildFixture(layout);
    constexpr int kElementCount = kSlotCount * kVerticesPerSlot;

    VertexBuffer meshBuffer(
        device, PositionColorDeclaration(), kElementCount, BufferUsage::None);
    meshBuffer.SetData(fixture.mesh.data(), kElementCount);

    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kElementCount, BufferUsage::None);
    indexBuffer.SetData(fixture.indices.data(), kElementCount);

    VertexBuffer instanceBuffer(
        device, InstanceMatrixDeclaration(), kRowCount, BufferUsage::None);
    instanceBuffer.SetDataRaw(
        fixture.instances.data(), kRowCount, static_cast<int>(sizeof(InstanceMatrix)));

    BasicEffect effect(device);
    ApplyInstancedEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffers({
        VertexBufferBinding(&meshBuffer, 0, 0),
        VertexBufferBinding(&instanceBuffer, 0, 1),
    });
    device.SetIndexBuffer(&indexBuffer);
    // Requests slots 3-4 in bands 0-2; the stock path renders slots 0-1 in band 0 only.
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 6, 0, kElementCount - 6, 3, 2, 3);

    const FrameSnapshot pixels = CaptureBackbuffer(device, layout.width, layout.height);
    EXPECT_GT(CountLitInCell(pixels, layout, 0, 0, Color::Black), 0)
        << "EasyGL instanced draws now honour startIndex/baseVertex -- flip this pin and open "
           "the EasyGL follow-up";
    EXPECT_EQ(0, CountLitInCell(pixels, layout, 3, 0, Color::Black))
        << "EasyGL instanced draws now honour startIndex/baseVertex -- flip this pin and open "
           "the EasyGL follow-up";
    EXPECT_EQ(
        0, pixels.CountLitInRows(
               static_cast<int>(layout.RowBoundaryY(1) + 0.999f), layout.height, Color::Black))
        << "EasyGL instanced draws now consume the per-instance stream -- flip this pin and open "
           "the EasyGL follow-up";
}
#endif

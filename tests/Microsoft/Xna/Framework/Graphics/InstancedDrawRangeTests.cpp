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
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

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
    };

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
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 0, 0, kSlotCount * kVerticesPerSlot, 0, 3, 2);

    const FrameSnapshot pixels = CaptureBackbuffer(device, layout.width, layout.height);
    const ExpectedRange range = ResolveExpectedRange(0, 0, 3);
    ExpectInstancedGeometryRendered(pixels, layout, range, 2, "zero-offset control");
    ExpectColumnsExclusive(pixels, layout, range, Color::Black, "zero-offset control");
    ExpectInstanceRowsExclusive(pixels, layout, 2, Color::Black, "zero-offset control");
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
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 0, 0, kSlotCount * kVerticesPerSlot, 12, 3, 2);

    const FrameSnapshot pixels = CaptureBackbuffer(device, layout.width, layout.height);
    const ExpectedRange range = ResolveExpectedRange(12, 0, 3);
    ASSERT_EQ(4, range.firstSlot);
    ExpectInstancedGeometryRendered(pixels, layout, range, 2, "startIndex-only range");
    ExpectColumnsExclusive(pixels, layout, range, Color::Black, "startIndex-only range");
    ExpectInstanceRowsExclusive(pixels, layout, 2, Color::Black, "startIndex-only range");
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
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 12, 0, kSlotCount * kVerticesPerSlot - 12, 0, 3, 2);

    const FrameSnapshot pixels = CaptureBackbuffer(device, layout.width, layout.height);
    const ExpectedRange range = ResolveExpectedRange(0, 12, 3);
    ASSERT_EQ(4, range.firstSlot);
    ExpectInstancedGeometryRendered(pixels, layout, range, 2, "baseVertex-only range");
    ExpectColumnsExclusive(pixels, layout, range, Color::Black, "baseVertex-only range");
    ExpectInstanceRowsExclusive(pixels, layout, 2, Color::Black, "baseVertex-only range");
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
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 6, 0, kSlotCount * kVerticesPerSlot - 6, 3, 2, 3);

    const FrameSnapshot pixels = CaptureBackbuffer(device, layout.width, layout.height);
    const ExpectedRange range = ResolveExpectedRange(3, 6, 2);
    ASSERT_EQ(3, range.firstSlot);
    ExpectInstancedGeometryRendered(pixels, layout, range, 3, "startIndex + baseVertex");
    ExpectColumnsExclusive(pixels, layout, range, Color::Black, "startIndex + baseVertex");
    ExpectInstanceRowsExclusive(pixels, layout, 3, Color::Black, "startIndex + baseVertex");
}

// instanceCount is independent of the geometry range in both directions: the same requested range
// renders the same columns for one, two and four instances, and each instance count lights exactly
// its own bands. A path that multiplied the geometry count by instanceCount, or derived one from
// the other, cannot satisfy all three cases.
TEST_F(InstancedDrawRangeTest, InstanceCountIsIndependentOfTheGeometryRange)
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
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, rangeCase.baseVertex, 0,
            kSlotCount * kVerticesPerSlot - rangeCase.baseVertex,
            rangeCase.startIndex, rangeCase.primitiveCount, rangeCase.instanceCount);

        const FrameSnapshot pixels = CaptureBackbuffer(device, layout.width, layout.height);
        const ExpectedRange range = ResolveExpectedRange(
            rangeCase.startIndex, rangeCase.baseVertex, rangeCase.primitiveCount);
        ExpectInstancedGeometryRendered(
            pixels, layout, range, rangeCase.instanceCount, rangeCase.label);
        ExpectColumnsExclusive(pixels, layout, range, Color::Black, rangeCase.label);
        ExpectInstanceRowsExclusive(
            pixels, layout, rangeCase.instanceCount, Color::Black, rangeCase.label);
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
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 6, 0, kSlotCount * kVerticesPerSlot - 6, 3, 2, 2);

    const FrameSnapshot pixels = CaptureBackbuffer(device, layout.width, layout.height);
    const ExpectedRange range = ResolveExpectedRange(3, 6, 2);
    ExpectInstancedGeometryRendered(pixels, layout, range, 2, "dynamic buffers");
    ExpectColumnsExclusive(pixels, layout, range, Color::Black, "dynamic buffers");
    ExpectInstanceRowsExclusive(pixels, layout, 2, Color::Black, "dynamic buffers");
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
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 6, 0, kSlotCount * kVerticesPerSlot - 6, 3, 2, 2);

    const FrameSnapshot pixels = CaptureBackbuffer(device, layout.width, layout.height);
    const ExpectedRange range = ResolveExpectedRange(3, 6, 2);
    ExpectInstancedGeometryRendered(pixels, layout, range, 2, "32-bit indices");
    ExpectColumnsExclusive(pixels, layout, range, Color::Black, "32-bit indices");
    ExpectInstanceRowsExclusive(pixels, layout, 2, Color::Black, "32-bit indices");
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
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 0, 0, kSlotCount * kVerticesPerSlot, 0, 1, 1);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 6, 0, kSlotCount * kVerticesPerSlot - 6, 3, 2, 3);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 18, 0, kSlotCount * kVerticesPerSlot - 18, 0, 1, 1);

    const FrameSnapshot pixels = CaptureBackbuffer(device, layout.width, layout.height);
    ExpectInstancedGeometryRendered(pixels, layout, ExpectedRange{0, 1}, 1, "A (first)");
    ExpectInstancedGeometryRendered(pixels, layout, ExpectedRange{3, 2}, 3, "B");
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
    const int bandThreeY = static_cast<int>(layout.RowBoundaryY(3) + 0.999f);
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
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 6, 0, kIndexCount - 6, 3, 2, 2);
    const FrameSnapshot pixels = CaptureBackbuffer(device, layout.width, layout.height);
    const ExpectedRange range = ResolveExpectedRange(3, 6, 2);
    ExpectInstancedGeometryRendered(pixels, layout, range, 2, "after rejected requests");
    ExpectColumnsExclusive(pixels, layout, range, Color::Black, "after rejected requests");
}
#endif

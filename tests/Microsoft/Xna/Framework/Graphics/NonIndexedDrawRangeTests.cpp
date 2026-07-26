// SPDX-License-Identifier: MS-PL
// REMED-GFX-113: the public non-indexed draw-range contract.
//
// XNA/FNA define GraphicsDevice.DrawPrimitives(primitiveType, vertexStart, primitiveCount) as
// "renders primitiveCount primitives from the currently bound vertex buffer, beginning at vertex
// element vertexStart". The reconciled CNA contract this file locks down is therefore:
//
//   vertexStart is a vertex-ELEMENT offset, never a byte offset
//   consumed vertices = PrimitiveVerts(primitiveType, primitiveCount)
//       TriangleList  = 3 * primitiveCount        TriangleStrip = primitiveCount + 2
//       LineList      = 2 * primitiveCount        LineStrip     = primitiveCount + 1
//       PointListEXT  = primitiveCount
//   the draw consumes NOTHING before vertexStart and nothing at or after
//       vertexStart + consumed vertices
//   a queued/deferred draw captures vertexStart and primitiveCount by value
//   a range that leaves the bound vertex buffer is rejected before native submission,
//       never silently clamped
//
// Geometry layout. The backbuffer is divided into `kSlotCount` equal-width vertical slots. Every
// vertex in the fixture sits on the centre line of its own slot, so a "slot" is simultaneously one
// vertex position and one exclusive screen region. The intended range occupies the middle slots in
// distinctive colours; the slots before and after it hold valid magenta decoy geometry. A draw that
// keeps its exact range therefore leaves both outer regions at the clear colour, while a draw that
// binds a wider vertex range necessarily lights them.
//
// Backend scope. Bgfx, EasyGL, WebGPU, Vulkan, D3D9 and D3D11 render 3D triangles and support
// backbuffer readback, so they carry the permanent TriangleList coverage. The full five-topology
// sweep additionally needs PointListEXT, which Vulkan/D3D9/D3D11/D3D12 still route through their
// triangle-list default (the independent REMED-GFX-114), so that sweep runs on Bgfx, EasyGL and
// WebGPU. Software raster keeps its documented TriangleList-only v1 boundary and is covered by its
// own control below.

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
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
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
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::DynamicVertexBuffer;
using Microsoft::Xna::Framework::Graphics::FillMode;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::RenderTargetUsage;
using Microsoft::Xna::Framework::Graphics::SamplerState;
using Microsoft::Xna::Framework::Graphics::SetDataOptions;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;
using Microsoft::Xna::Framework::Graphics::VertexPositionTexture;
using Microsoft::Xna::Framework::Graphics::Viewport;

namespace
{
    /// Number of equal-width vertical slots the target is divided into. Seven gives two prefix
    /// decoy slots, three intended slots and two suffix decoy slots for the list topologies, and
    /// leaves the strip topologies room for bridging geometry to become visible when a draw
    /// consumes vertices outside its requested range.
    constexpr int kSlotCount = 7;

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

    /// Target geometry: every slot's own centre line and the shared vertical extents.
    struct SlotLayout
    {
        int width = 0;
        int height = 0;

        [[nodiscard]] float CenterX(int slot) const
        {
            return static_cast<float>(width) *
                   (static_cast<float>(slot) + 0.5f) / static_cast<float>(kSlotCount);
        }

        /// Exclusive slot boundary: everything a slot-`slot` primitive can touch lies strictly
        /// inside (Boundary(slot), Boundary(slot + 1)).
        [[nodiscard]] float Boundary(int slot) const
        {
            return static_cast<float>(width) *
                   static_cast<float>(slot) / static_cast<float>(kSlotCount);
        }

        [[nodiscard]] float HalfWidth() const
        {
            return 0.30f * static_cast<float>(width) / static_cast<float>(kSlotCount);
        }

        [[nodiscard]] float MidY() const { return 0.5f * static_cast<float>(height); }
        [[nodiscard]] float HalfHeight() const { return 0.25f * static_cast<float>(height); }
    };

    /// Identity World/View/Projection is used throughout, so a vertex position *is* its clip-space
    /// position and the viewport transform maps it to the requested window pixel.
    VertexPositionColor VertexAtPixel(
        const SlotLayout& layout, float pixelX, float pixelY, const Color& color, float depth)
    {
        const float x = (2.0f * pixelX / static_cast<float>(layout.width)) - 1.0f;
        const float y = 1.0f - (2.0f * pixelY / static_cast<float>(layout.height));
        return VertexPositionColor(Vector3(x, y, depth), color);
    }

    struct ProbePoint
    {
        float pixelX = 0.0f;
        float pixelY = 0.0f;
        Color color = Color::White;
    };

    /// One topology's fixture: the complete vertex buffer, the pixels each intended primitive must
    /// light, and the two exclusive decoy regions that must stay at the clear colour.
    struct RangePlan
    {
        std::vector<VertexPositionColor> vertices;
        std::vector<ProbePoint> probes;
        int decoyLeftLimitX = 0;   ///< columns [0, decoyLeftLimitX) must be background
        int decoyRightStartX = 0;  ///< columns [decoyRightStartX, width) must be background
    };

    /// Vertices consumed by `primitiveCount` primitives of `primitive` — the exact public formula
    /// the native binding owes.
    int ConsumedVertices(PrimitiveType primitive, int primitiveCount)
    {
        switch (primitive)
        {
        case PrimitiveType::TriangleList:  return primitiveCount * 3;
        case PrimitiveType::TriangleStrip: return primitiveCount + 2;
        case PrimitiveType::LineList:      return primitiveCount * 2;
        case PrimitiveType::LineStrip:     return primitiveCount + 1;
        case PrimitiveType::PointListEXT:  return primitiveCount;
        }
        return 0;
    }

    /// True when consecutive primitives of this topology share vertices, so one flat colour per
    /// primitive is impossible and the whole intended range carries a single colour instead.
    bool IsStripTopology(PrimitiveType primitive)
    {
        return primitive == PrimitiveType::TriangleStrip ||
               primitive == PrimitiveType::LineStrip;
    }

    /// Vertices a single primitive of a list topology owns exclusively.
    int VerticesPerListPrimitive(PrimitiveType primitive)
    {
        switch (primitive)
        {
        case PrimitiveType::TriangleList: return 3;
        case PrimitiveType::LineList:     return 2;
        case PrimitiveType::PointListEXT: return 1;
        default:                          return 0;
        }
    }

    /// Emits one list primitive inside slot `slot`, plus the pixel that proves it rendered.
    void AppendListPrimitive(
        const SlotLayout& layout,
        PrimitiveType primitive,
        int slot,
        const Color& color,
        float depth,
        std::vector<VertexPositionColor>& vertices,
        ProbePoint* probe)
    {
        const float cx = layout.CenterX(slot);
        const float hw = layout.HalfWidth();
        const float midY = layout.MidY();
        const float hh = layout.HalfHeight();
        switch (primitive)
        {
        case PrimitiveType::TriangleList:
            vertices.push_back(VertexAtPixel(layout, cx - hw, midY + hh, color, depth));
            vertices.push_back(VertexAtPixel(layout, cx + hw, midY + hh, color, depth));
            vertices.push_back(VertexAtPixel(layout, cx, midY - hh, color, depth));
            if (probe) *probe = ProbePoint{cx, midY + hh / 3.0f, color};
            break;
        case PrimitiveType::LineList:
            vertices.push_back(VertexAtPixel(layout, cx - hw, midY, color, depth));
            vertices.push_back(VertexAtPixel(layout, cx + hw, midY, color, depth));
            if (probe) *probe = ProbePoint{cx, midY, color};
            break;
        case PrimitiveType::PointListEXT:
            vertices.push_back(VertexAtPixel(layout, cx, midY, color, depth));
            if (probe) *probe = ProbePoint{cx, midY, color};
            break;
        default:
            break;
        }
    }

    /// Emits one strip vertex in slot `slot`. Consecutive strip vertices alternate above and below
    /// the centre line so every strip primitive covers real area (triangles) or crosses the centre
    /// line (line segments).
    void AppendStripVertex(
        const SlotLayout& layout,
        PrimitiveType primitive,
        int slot,
        const Color& color,
        float depth,
        std::vector<VertexPositionColor>& vertices)
    {
        const float offset = (primitive == PrimitiveType::TriangleStrip)
            ? layout.HalfHeight()
            : layout.HalfHeight() * 0.5f;
        const float y = layout.MidY() + ((slot % 2 == 0) ? -offset : offset);
        vertices.push_back(VertexAtPixel(layout, layout.CenterX(slot), y, color, depth));
    }

    /// Builds the complete decoy/intended/decoy fixture for one topology and one requested range.
    /// `vertexStart` is the first consumed vertex; the slots before it and after the consumed range
    /// hold magenta decoy geometry.
    RangePlan BuildRangePlan(
        const SlotLayout& layout,
        PrimitiveType primitive,
        int vertexStart,
        int primitiveCount,
        float depth = 0.5f)
    {
        // Built per call rather than as a file- or function-scope constant: Color is not a literal
        // type, and a namespace-scope Color constant would depend on static-initialisation order.
        const std::array<Color, 5> kWantedColors{
            Color::Red, Color::Lime, Color::Blue, Color::Yellow, Color::Cyan,
        };

        RangePlan plan;
        const int consumed = ConsumedVertices(primitive, primitiveCount);
        // Slot indices, not vertex indices: a strip vertex owns one slot, while a list primitive
        // owns one slot with several vertices in it.
        const int firstSlot = IsStripTopology(primitive)
            ? vertexStart
            : vertexStart / VerticesPerListPrimitive(primitive);
        const int lastSlot = IsStripTopology(primitive)
            ? vertexStart + consumed - 1
            : firstSlot + primitiveCount - 1;

        if (IsStripTopology(primitive))
        {
            // One vertex per slot; the whole intended range shares one colour so its interior is
            // flat and no result depends on interpolation between two intended colours.
            for (int slot = 0; slot < kSlotCount; ++slot)
            {
                const bool intended = slot >= firstSlot && slot <= lastSlot;
                AppendStripVertex(
                    layout, primitive, slot,
                    intended ? Color::Lime : Color::Magenta, depth, plan.vertices);
            }
            for (int i = 0; i < primitiveCount; ++i)
            {
                const int first = vertexStart + i;
                if (primitive == PrimitiveType::TriangleStrip)
                {
                    // Centroid of the three strip vertices this triangle spans. Their vertical
                    // offsets alternate (+h, -h, +h), so the three sum to the first one's offset.
                    const float cx = (layout.CenterX(first) + layout.CenterX(first + 1) +
                                      layout.CenterX(first + 2)) / 3.0f;
                    const float firstOffset =
                        ((first % 2 == 0) ? -1.0f : 1.0f) * layout.HalfHeight();
                    const float cy = layout.MidY() + firstOffset / 3.0f;
                    plan.probes.push_back(ProbePoint{cx, cy, Color::Lime});
                }
                else
                {
                    // Midpoint of the segment: its two endpoints straddle the centre line.
                    const float cx =
                        0.5f * (layout.CenterX(first) + layout.CenterX(first + 1));
                    plan.probes.push_back(ProbePoint{cx, layout.MidY(), Color::Lime});
                }
            }
        }
        else
        {
            int wantedIndex = 0;
            for (int slot = 0; slot < kSlotCount; ++slot)
            {
                const bool intended = slot >= firstSlot && slot <= lastSlot;
                ProbePoint probe;
                AppendListPrimitive(
                    layout, primitive, slot,
                    intended ? kWantedColors[static_cast<std::size_t>(wantedIndex) %
                                             kWantedColors.size()]
                             : Color::Magenta,
                    depth, plan.vertices, intended ? &probe : nullptr);
                if (intended)
                {
                    plan.probes.push_back(probe);
                    ++wantedIndex;
                }
            }
        }

        plan.decoyLeftLimitX = static_cast<int>(layout.Boundary(firstSlot));
        plan.decoyRightStartX =
            static_cast<int>(layout.Boundary(lastSlot + 1) + 0.999f);
        return plan;
    }

    /// "Did any geometry render here?" compares RGB only. Every primitive in this fixture has a
    /// non-black colour, so RGB alone answers the question — while the alpha a backend leaves in a
    /// *cleared* render target it later samples back is its own convention (Vulkan resolves the
    /// black clear to alpha 0, the OpenGL-family backends to alpha 255) and has nothing to do with
    /// which vertices a draw consumed. The intended primitives are still matched on exact RGBA.
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

        [[nodiscard]] int CountExact(const Color& color) const
        {
            int total = 0;
            for (const Color& pixel : pixels)
            {
                if (pixel == color)
                    ++total;
            }
            return total;
        }

        [[nodiscard]] bool HasExactWithin(
            int centerX, int centerY, int radius, const Color& color) const
        {
            for (int offsetY = -radius; offsetY <= radius; ++offsetY)
            {
                for (int offsetX = -radius; offsetX <= radius; ++offsetX)
                {
                    if (At(centerX + offsetX, centerY + offsetY) == color)
                        return true;
                }
            }
            return false;
        }

        /// Position and value of the first pixel in columns [x0, x1) that differs from
        /// @p background, for failure messages that name the offending pixel instead of a count.
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

        /// Number of pixels in columns [x0, x1) whose RGB differs from @p background.
        [[nodiscard]] int CountLitInColumns(int x0, int x1, const Color& background) const
        {
            const int firstX = std::clamp(x0, 0, width);
            const int lastX = std::clamp(x1, 0, width);
            int total = 0;
            for (int y = 0; y < height; ++y)
            {
                for (int x = firstX; x < lastX; ++x)
                {
                    if (!HasSameRgb(At(x, y), background))
                        ++total;
                }
            }
            return total;
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
            &region,
            snapshot.pixels.data(),
            0,
            static_cast<int>(snapshot.pixels.size()));
        return snapshot;
    }

    FrameSnapshot CaptureRenderTarget(RenderTarget2D& target, int width, int height)
    {
        FrameSnapshot snapshot;
        snapshot.width = width;
        snapshot.height = height;
        snapshot.pixels.assign(
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
            Color::Transparent);
        const Rectangle region(0, 0, width, height);
        target.GetData(
            0, &region, snapshot.pixels.data(), 0,
            static_cast<int>(snapshot.pixels.size()));
        return snapshot;
    }

    /// Every intended primitive rendered at its own pixel. The 5x5 probe absorbs the one-pixel
    /// differences each backend's own pixel-centre convention legitimately allows without
    /// accepting a different pixel.
    void ExpectIntendedPrimitivesRendered(
        const FrameSnapshot& snapshot, const RangePlan& plan, const char* label)
    {
        for (std::size_t i = 0; i < plan.probes.size(); ++i)
        {
            const ProbePoint& probe = plan.probes[i];
            EXPECT_TRUE(snapshot.HasExactWithin(
                static_cast<int>(probe.pixelX), static_cast<int>(probe.pixelY), 2, probe.color))
                << label << ": intended primitive " << i
                << " missing (no exact RGBA pixel in the 5x5 probe at "
                << static_cast<int>(probe.pixelX) << ',' << static_cast<int>(probe.pixelY) << ')';
        }
    }

    /// Nothing outside the requested range rendered: both exclusive decoy regions are untouched and
    /// the decoy colour appears nowhere at all.
    void ExpectRangeExclusive(
        const FrameSnapshot& snapshot,
        const RangePlan& plan,
        const Color& background,
        const char* label)
    {
        EXPECT_EQ(0, snapshot.CountLitInColumns(0, plan.decoyLeftLimitX, background))
            << label << ": geometry before vertexStart rendered in columns [0,"
            << plan.decoyLeftLimitX << ") -- "
            << snapshot.DescribeFirstLitInColumns(0, plan.decoyLeftLimitX, background);
        EXPECT_EQ(
            0,
            snapshot.CountLitInColumns(plan.decoyRightStartX, snapshot.width, background))
            << label << ": geometry after the requested range rendered in columns ["
            << plan.decoyRightStartX << ',' << snapshot.width << ") -- "
            << snapshot.DescribeFirstLitInColumns(
                   plan.decoyRightStartX, snapshot.width, background);
        EXPECT_EQ(0, snapshot.CountExact(Color::Magenta))
            << label << ": decoy colour must not appear anywhere in the frame";
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

    /// Draws @p target over the whole backbuffer with point sampling, so a render target of exactly
    /// the backbuffer size reproduces its rendered pixels 1:1.
    void SampleTargetToBackbuffer(GraphicsDevice& device, RenderTarget2D& target)
    {
        const std::array<VertexPositionTexture, 6> quad{
            VertexPositionTexture(Vector3(-1.0f, 1.0f, 0.0f), Vector2(0.0f, 0.0f)),
            VertexPositionTexture(Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 1.0f)),
            VertexPositionTexture(Vector3(1.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f)),
            VertexPositionTexture(Vector3(-1.0f, 1.0f, 0.0f), Vector2(0.0f, 0.0f)),
            VertexPositionTexture(Vector3(1.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f)),
            VertexPositionTexture(Vector3(1.0f, 1.0f, 0.0f), Vector2(1.0f, 0.0f)),
        };
        device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        BasicEffect sampleEffect(device);
        sampleEffect.setTextureEnabledProperty(true);
        sampleEffect.setTextureProperty(&target);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.Clear(Color::Black);
        sampleEffect.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad.data(), 0, 2);
    }

    class NonIndexedDrawRangeTest : public ::testing::Test
    {
    protected:
        GraphicsDevice device;

        /// Explicit device state for the whole fixture: nothing here may depend on a framework
        /// default, because a default-valued no-op fallback would let a buggy path pass.
        void RequireRangeRendering()
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

        [[nodiscard]] SlotLayout BackbufferLayout() const
        {
            return SlotLayout{BackbufferWidth(), BackbufferHeight()};
        }

        void ApplyVertexColorEffect(BasicEffect& effect)
        {
            effect.VertexColorEnabled = true;
            effect.Apply();
        }
    };
}

#if defined(CNA_BACKEND_BGFX) || defined(CNA_BACKEND_EASYGL) || \
    defined(CNA_BACKEND_WEBGPU) || defined(CNA_BACKEND_VULKAN) || \
    defined(CNA_BACKEND_D3D9) || defined(CNA_BACKEND_D3D11)

// Proof 1 of the range contract, isolated: primitiveCount alone must limit the consumed vertex
// range. vertexStart is zero, so nothing here depends on the offset half of the contract; every
// magenta primitive lives strictly after the requested range.
TEST_F(NonIndexedDrawRangeTest, PersistentDrawHonorsPrimitiveCountAtVertexStartZero)
{
    RequireRangeRendering();

    const SlotLayout layout = BackbufferLayout();
    const RangePlan plan =
        BuildRangePlan(layout, PrimitiveType::TriangleList, 0, 3);
    const int vertexCount = static_cast<int>(plan.vertices.size());

    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), vertexCount, BufferUsage::None);
    vertexBuffer.SetData(plan.vertices.data(), vertexCount);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);
    device.DrawPrimitives(PrimitiveType::TriangleList, 0, 3);

    const FrameSnapshot pixels =
        CaptureBackbuffer(device, layout.width, layout.height);
    ExpectIntendedPrimitivesRendered(pixels, plan, "primitiveCount-only range");
    ExpectRangeExclusive(pixels, plan, Color::Black, "primitiveCount-only range");
}

// Proof 2, isolated: vertexStart alone must move the first consumed vertex. primitiveCount here
// covers the complete remainder of the buffer, so nothing depends on the count half of the
// contract; every magenta primitive lives strictly before the requested range.
TEST_F(NonIndexedDrawRangeTest, PersistentDrawHonorsNonzeroVertexStartToEndOfBuffer)
{
    RequireRangeRendering();

    const SlotLayout layout = BackbufferLayout();
    // Five of seven triangles, starting at vertex 6 — the exact end boundary of the buffer.
    const RangePlan plan =
        BuildRangePlan(layout, PrimitiveType::TriangleList, 6, 5);
    const int vertexCount = static_cast<int>(plan.vertices.size());
    ASSERT_EQ(kSlotCount * 3, vertexCount);

    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), vertexCount, BufferUsage::None);
    vertexBuffer.SetData(plan.vertices.data(), vertexCount);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);
    device.DrawPrimitives(PrimitiveType::TriangleList, 6, 5);

    const FrameSnapshot pixels =
        CaptureBackbuffer(device, layout.width, layout.height);
    ExpectIntendedPrimitivesRendered(pixels, plan, "vertexStart-only range");
    ExpectRangeExclusive(pixels, plan, Color::Black, "vertexStart-only range");
}

// Proofs 1 and 2 combined, plus the first/middle/final boundary sweep. Every draw reads its own
// frame, so no result can be produced by a later draw.
TEST_F(NonIndexedDrawRangeTest, PersistentDrawHonorsFirstMiddleAndFinalRanges)
{
    RequireRangeRendering();

    const SlotLayout layout = BackbufferLayout();
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), kSlotCount * 3, BufferUsage::None);
    BasicEffect effect(device);

    struct RangeCase
    {
        int vertexStart;
        int primitiveCount;
        const char* label;
    };
    constexpr std::array<RangeCase, 5> cases{{
        {0, 1, "first single primitive"},
        {0, 7, "complete buffer"},
        {9, 3, "middle range"},
        {18, 1, "final single primitive"},
        {3, 5, "interior range with decoys on both sides"},
    }};

    for (const RangeCase& rangeCase : cases)
    {
        const RangePlan plan = BuildRangePlan(
            layout, PrimitiveType::TriangleList,
            rangeCase.vertexStart, rangeCase.primitiveCount);
        vertexBuffer.SetData(
            plan.vertices.data(), static_cast<int>(plan.vertices.size()));

        ApplyVertexColorEffect(effect);
        device.Clear(Color::Black);
        device.SetVertexBuffer(&vertexBuffer);
        device.DrawPrimitives(
            PrimitiveType::TriangleList,
            rangeCase.vertexStart,
            rangeCase.primitiveCount);

        const FrameSnapshot pixels =
            CaptureBackbuffer(device, layout.width, layout.height);
        ExpectIntendedPrimitivesRendered(pixels, plan, rangeCase.label);
        ExpectRangeExclusive(pixels, plan, Color::Black, rangeCase.label);
    }
}

// DynamicVertexBuffer takes the same public contract as the static buffer above.
TEST_F(NonIndexedDrawRangeTest, PersistentDynamicDrawHonorsRangeAndCount)
{
    RequireRangeRendering();

    const SlotLayout layout = BackbufferLayout();
    const RangePlan plan =
        BuildRangePlan(layout, PrimitiveType::TriangleList, 6, 3);
    const int vertexCount = static_cast<int>(plan.vertices.size());

    DynamicVertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), vertexCount, BufferUsage::None);
    vertexBuffer.SetData(plan.vertices.data(), 0, vertexCount, SetDataOptions::None);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);
    device.DrawPrimitives(PrimitiveType::TriangleList, 6, 3);

    const FrameSnapshot pixels =
        CaptureBackbuffer(device, layout.width, layout.height);
    ExpectIntendedPrimitivesRendered(pixels, plan, "dynamic buffer range");
    ExpectRangeExclusive(pixels, plan, Color::Black, "dynamic buffer range");
}

// A -> B -> A in one frame with three different ranges, then one readback. Each queued draw must
// keep its own vertexStart/primitiveCount; a backend that resolves either from live state at flush
// time would render the last range three times.
TEST_F(NonIndexedDrawRangeTest, DeferredNonIndexedRangesAtoBtoAKeepTheirOwnRange)
{
    RequireRangeRendering();

    const SlotLayout layout = BackbufferLayout();
    // Three primitives that never overlap: slot 0 (A), slot 3 (B), slot 6 (A again).
    std::vector<VertexPositionColor> vertices;
    ProbePoint first;
    ProbePoint middle;
    ProbePoint last;
    AppendListPrimitive(
        layout, PrimitiveType::TriangleList, 0, Color::Red, 0.5f, vertices, &first);
    for (int slot = 1; slot <= 2; ++slot)
        AppendListPrimitive(
            layout, PrimitiveType::TriangleList, slot, Color::Magenta, 0.5f, vertices, nullptr);
    AppendListPrimitive(
        layout, PrimitiveType::TriangleList, 3, Color::Lime, 0.5f, vertices, &middle);
    for (int slot = 4; slot <= 5; ++slot)
        AppendListPrimitive(
            layout, PrimitiveType::TriangleList, slot, Color::Magenta, 0.5f, vertices, nullptr);
    AppendListPrimitive(
        layout, PrimitiveType::TriangleList, 6, Color::Blue, 0.5f, vertices, &last);

    const int vertexCount = static_cast<int>(vertices.size());
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), vertexCount, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), vertexCount);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);
    device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);   // A
    device.DrawPrimitives(PrimitiveType::TriangleList, 9, 1);   // B
    device.DrawPrimitives(PrimitiveType::TriangleList, 18, 1);  // A again

    const FrameSnapshot pixels =
        CaptureBackbuffer(device, layout.width, layout.height);
    EXPECT_TRUE(pixels.HasExactWithin(
        static_cast<int>(first.pixelX), static_cast<int>(first.pixelY), 2, Color::Red))
        << "A->B->A: the first queued range lost its own vertexStart";
    EXPECT_TRUE(pixels.HasExactWithin(
        static_cast<int>(middle.pixelX), static_cast<int>(middle.pixelY), 2, Color::Lime))
        << "A->B->A: the second queued range lost its own vertexStart";
    EXPECT_TRUE(pixels.HasExactWithin(
        static_cast<int>(last.pixelX), static_cast<int>(last.pixelY), 2, Color::Blue))
        << "A->B->A: the third queued range lost its own vertexStart";
    EXPECT_EQ(0, pixels.CountExact(Color::Magenta))
        << "A->B->A: a queued draw consumed vertices outside its own range";
}

// A later SetData must not retroactively change an already-queued draw's data, and each queued
// draw must still keep its own range across the buffer-version change (REMED-GFX-109).
TEST_F(NonIndexedDrawRangeTest, DeferredRangesSurviveBufferVersionChangesBetweenDraws)
{
    RequireRangeRendering();

    const SlotLayout layout = BackbufferLayout();
    std::vector<VertexPositionColor> versionA;
    std::vector<VertexPositionColor> versionB;
    ProbePoint probeA;
    ProbePoint probeB;
    // Version A puts red in slot 0 and magenta everywhere else; version B puts lime in slot 6.
    AppendListPrimitive(
        layout, PrimitiveType::TriangleList, 0, Color::Red, 0.5f, versionA, &probeA);
    for (int slot = 1; slot < kSlotCount; ++slot)
        AppendListPrimitive(
            layout, PrimitiveType::TriangleList, slot, Color::Magenta, 0.5f, versionA, nullptr);
    for (int slot = 0; slot < kSlotCount - 1; ++slot)
        AppendListPrimitive(
            layout, PrimitiveType::TriangleList, slot, Color::Magenta, 0.5f, versionB, nullptr);
    AppendListPrimitive(
        layout, PrimitiveType::TriangleList, kSlotCount - 1, Color::Lime, 0.5f,
        versionB, &probeB);

    const int vertexCount = static_cast<int>(versionA.size());
    DynamicVertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), vertexCount, BufferUsage::None);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);

    vertexBuffer.SetData(versionA.data(), 0, vertexCount, SetDataOptions::None);
    device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
    vertexBuffer.SetData(versionB.data(), 0, vertexCount, SetDataOptions::None);
    device.DrawPrimitives(PrimitiveType::TriangleList, (kSlotCount - 1) * 3, 1);

    const FrameSnapshot pixels =
        CaptureBackbuffer(device, layout.width, layout.height);
    EXPECT_TRUE(pixels.HasExactWithin(
        static_cast<int>(probeA.pixelX), static_cast<int>(probeA.pixelY), 2, Color::Red))
        << "the first queued draw lost its own buffer version or range";
    EXPECT_TRUE(pixels.HasExactWithin(
        static_cast<int>(probeB.pixelX), static_cast<int>(probeB.pixelY), 2, Color::Lime))
        << "the second queued draw lost its own buffer version or range";
    EXPECT_EQ(0, pixels.CountExact(Color::Magenta))
        << "a queued draw consumed vertices outside its own range across a SetData";
}

// The same exact range must hold on a RenderTarget2D and after returning to the backbuffer, so no
// result depends on which target the range was requested against.
TEST_F(NonIndexedDrawRangeTest, NonIndexedRangeHoldsOnRenderTargetAndBackbuffer)
{
    RequireRangeRendering();

    const SlotLayout layout = BackbufferLayout();
    const RangePlan plan =
        BuildRangePlan(layout, PrimitiveType::TriangleList, 6, 3);
    const int vertexCount = static_cast<int>(plan.vertices.size());

    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), vertexCount, BufferUsage::None);
    vertexBuffer.SetData(plan.vertices.data(), vertexCount);

    RenderTarget2D target(
        device, layout.width, layout.height, false, SurfaceFormat::Color,
        DepthFormat::None, 0, RenderTargetUsage::PreserveContents);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.SetVertexBuffer(&vertexBuffer);
    device.Clear(Color::Black);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    effect.Apply();
    device.DrawPrimitives(PrimitiveType::TriangleList, 6, 3);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    // Read the backbuffer first: the target-only draw must not have reached it, and the readback
    // finishes the producing frame -- a render target must not be sampled in the frame that wrote
    // it.
    const FrameSnapshot untouchedBackbuffer =
        CaptureBackbuffer(device, layout.width, layout.height);
    EXPECT_EQ(
        0, untouchedBackbuffer.CountLitInColumns(0, layout.width, Color::Black))
        << "a render-target-only draw reached the backbuffer";

    // Rendered target pixels live only on the GPU; sample the finished target through the ordinary
    // texture path.
    device.SetVertexBuffer(nullptr);
    SampleTargetToBackbuffer(device, target);
    const FrameSnapshot fromTarget =
        CaptureBackbuffer(device, layout.width, layout.height);
    ExpectIntendedPrimitivesRendered(fromTarget, plan, "render-target range");
    ExpectRangeExclusive(fromTarget, plan, Color::Black, "render-target range");

    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::None);
    device.setBlendStateProperty(BlendState::Opaque);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);
    device.DrawPrimitives(PrimitiveType::TriangleList, 6, 3);

    const FrameSnapshot fromBackbuffer =
        CaptureBackbuffer(device, layout.width, layout.height);
    ExpectIntendedPrimitivesRendered(
        fromBackbuffer, plan, "backbuffer range after a render target");
    ExpectRangeExclusive(
        fromBackbuffer, plan, Color::Black, "backbuffer range after a render target");
}

// DrawUserPrimitives is the semantic comparison: it copies exactly the requested range into its own
// temporary buffer, so vertexOffset/primitiveCount already select the intended geometry out of a
// larger caller array. This must keep working unchanged.
TEST_F(NonIndexedDrawRangeTest, DrawUserPrimitivesKeepsItsCopiedExactRange)
{
    RequireRangeRendering();

    const SlotLayout layout = BackbufferLayout();
    const RangePlan plan =
        BuildRangePlan(layout, PrimitiveType::TriangleList, 6, 3);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(nullptr);
    device.DrawUserPrimitives(
        PrimitiveType::TriangleList, plan.vertices.data(), 6, 3);

    const FrameSnapshot pixels =
        CaptureBackbuffer(device, layout.width, layout.height);
    ExpectIntendedPrimitivesRendered(pixels, plan, "DrawUserPrimitives copied range");
    ExpectRangeExclusive(pixels, plan, Color::Black, "DrawUserPrimitives copied range");
}


// Nothing a rejected range requested may reach the target: after a clean frame, every invalid draw
// must leave the framebuffer exactly as the clear left it.
TEST_F(NonIndexedDrawRangeTest, RejectedNonIndexedRangesRenderNothing)
{
    RequireRangeRendering();

    const SlotLayout layout = BackbufferLayout();
    const RangePlan plan =
        BuildRangePlan(layout, PrimitiveType::TriangleList, 0, 1);
    const int vertexCount = static_cast<int>(plan.vertices.size());
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), vertexCount, BufferUsage::None);
    vertexBuffer.SetData(plan.vertices.data(), vertexCount);
    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);

    EXPECT_THROW(
        device.DrawPrimitives(PrimitiveType::TriangleList, 19, 1),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 8),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawPrimitives(PrimitiveType::TriangleList, vertexCount + 1, 1),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawPrimitives(
            PrimitiveType::TriangleList, 0, std::numeric_limits<int>::max()),
        System::ArgumentOutOfRangeException);

    const FrameSnapshot pixels =
        CaptureBackbuffer(device, layout.width, layout.height);
    EXPECT_EQ(0, pixels.CountLitInColumns(0, layout.width, Color::Black))
        << "a rejected non-indexed draw still reached the backend -- "
        << pixels.DescribeFirstLitInColumns(0, layout.width, Color::Black);
}

#endif

// The public non-indexed range contract is owed by every backend, including the ones that
// render nothing: an out-of-buffer range must be rejected deterministically, before anything
// reaches a backend at all. Deliberately unguarded, exactly like its indexed sibling in
// IndexedDrawDeferredTests.cpp. The rendered counterpart -- a rejected draw leaves the frame
// untouched -- is RejectedNonIndexedRangesRenderNothing above.
TEST_F(NonIndexedDrawRangeTest, PublicContractValidatesEveryNonIndexedRangeBeforeSubmission)
{
    RequireRangeRendering();

    EXPECT_EQ(9, GraphicsDevice::PrimitiveVerts(PrimitiveType::TriangleList, 3));
    EXPECT_EQ(5, GraphicsDevice::PrimitiveVerts(PrimitiveType::TriangleStrip, 3));
    EXPECT_EQ(6, GraphicsDevice::PrimitiveVerts(PrimitiveType::LineList, 3));
    EXPECT_EQ(4, GraphicsDevice::PrimitiveVerts(PrimitiveType::LineStrip, 3));
    EXPECT_EQ(3, GraphicsDevice::PrimitiveVerts(PrimitiveType::PointListEXT, 3));

    const SlotLayout layout = BackbufferLayout();
    const RangePlan plan =
        BuildRangePlan(layout, PrimitiveType::TriangleList, 0, 1);
    const int vertexCount = static_cast<int>(plan.vertices.size());
    ASSERT_EQ(21, vertexCount);

    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), vertexCount, BufferUsage::None);
    vertexBuffer.SetData(plan.vertices.data(), vertexCount);
    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);

    // Legal ranges: first, middle, exact end boundary, and the complete buffer.
    EXPECT_NO_THROW(device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1));
    EXPECT_NO_THROW(device.DrawPrimitives(PrimitiveType::TriangleList, 9, 1));
    EXPECT_NO_THROW(device.DrawPrimitives(PrimitiveType::TriangleList, 18, 1));
    EXPECT_NO_THROW(device.DrawPrimitives(PrimitiveType::TriangleList, 0, 7));

    // Exactly one primitive too many for every topology, measured from the end boundary.
    struct CountCase
    {
        PrimitiveType primitive;
        int primitiveCount;
        int consumedVertices;
    };
    constexpr std::array<CountCase, 5> countCases{{
        {PrimitiveType::TriangleList, 3, 9},
        {PrimitiveType::TriangleStrip, 3, 5},
        {PrimitiveType::LineList, 3, 6},
        {PrimitiveType::LineStrip, 3, 4},
        {PrimitiveType::PointListEXT, 3, 3},
    }};
    for (const CountCase& countCase : countCases)
    {
        EXPECT_THROW(
            device.DrawPrimitives(
                countCase.primitive,
                vertexCount - countCase.consumedVertices,
                countCase.primitiveCount + 1),
            System::ArgumentOutOfRangeException)
            << TopologyName(countCase.primitive)
            << ": one primitive past the end boundary was accepted";
    }

    // Negative and non-positive arguments.
    EXPECT_THROW(
        device.DrawPrimitives(PrimitiveType::TriangleList, -1, 1),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 0),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, -1),
        System::ArgumentOutOfRangeException);
    // vertexStart past the buffer, and a range that starts inside but ends outside.
    EXPECT_THROW(
        device.DrawPrimitives(PrimitiveType::TriangleList, vertexCount + 1, 1),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawPrimitives(PrimitiveType::TriangleList, vertexCount, 1),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawPrimitives(PrimitiveType::TriangleList, 19, 1),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 8),
        System::ArgumentOutOfRangeException);
    // Arithmetic overflow in the topology count itself, and in vertexStart + consumed.
    EXPECT_THROW(
        device.DrawPrimitives(
            PrimitiveType::TriangleList, 0, std::numeric_limits<int>::max()),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawPrimitives(
            PrimitiveType::PointListEXT,
            std::numeric_limits<int>::max(),
            std::numeric_limits<int>::max()),
        System::ArgumentOutOfRangeException);
}


#if defined(CNA_BACKEND_BGFX) || defined(CNA_BACKEND_EASYGL) || \
    defined(CNA_BACKEND_WEBGPU)
// Every supported topology owes the same exact range, with valid decoy geometry before and after
// the requested vertices. The per-topology vertex counts are the public formulas themselves, so a
// backend that consumed 3*primitiveCount vertices for a strip, or bound the whole buffer for any
// topology, fails here.
TEST_F(NonIndexedDrawRangeTest, EverySupportedTopologyHonorsVertexStartAndExactCount)
{
    RequireRangeRendering();

    const SlotLayout layout = BackbufferLayout();
    struct TopologyCase
    {
        PrimitiveType primitive;
        int vertexStart;
        int primitiveCount;
    };
    constexpr std::array<TopologyCase, 5> cases{{
        {PrimitiveType::TriangleList, 6, 3},    //  9 vertices, 6..14 of 21
        {PrimitiveType::TriangleStrip, 2, 2},   //  4 vertices, 2..5 of 7
        {PrimitiveType::LineList, 4, 3},        //  6 vertices, 4..9 of 14
        {PrimitiveType::LineStrip, 2, 3},       //  4 vertices, 2..5 of 7
        {PrimitiveType::PointListEXT, 2, 3},    //  3 vertices, 2..4 of 7
    }};

    BasicEffect effect(device);
    for (const TopologyCase& topologyCase : cases)
    {
        const RangePlan plan = BuildRangePlan(
            layout, topologyCase.primitive,
            topologyCase.vertexStart, topologyCase.primitiveCount);
        const int vertexCount = static_cast<int>(plan.vertices.size());
        ASSERT_EQ(
            topologyCase.primitiveCount,
            static_cast<int>(plan.probes.size()))
            << TopologyName(topologyCase.primitive);

        VertexBuffer vertexBuffer(
            device, PositionColorDeclaration(), vertexCount, BufferUsage::None);
        vertexBuffer.SetData(plan.vertices.data(), vertexCount);

        ApplyVertexColorEffect(effect);
        device.Clear(Color::Black);
        device.SetVertexBuffer(&vertexBuffer);
        device.DrawPrimitives(
            topologyCase.primitive,
            topologyCase.vertexStart,
            topologyCase.primitiveCount);

        const FrameSnapshot pixels =
            CaptureBackbuffer(device, layout.width, layout.height);
        ExpectIntendedPrimitivesRendered(
            pixels, plan, TopologyName(topologyCase.primitive));
        ExpectRangeExclusive(
            pixels, plan, Color::Black, TopologyName(topologyCase.primitive));
        device.SetVertexBuffer(nullptr);
    }
}

// The same range, drawn once per topology into one frame at non-overlapping slots. A backend that
// let one draw's topology or range leak into another would put geometry in a neighbour's region.
TEST_F(NonIndexedDrawRangeTest, TopologySwitchesKeepTheirOwnRangesInOneFrame)
{
    RequireRangeRendering();

    const SlotLayout layout = BackbufferLayout();
    // Slots 0/1 a point pair, slot 3 a triangle, slots 5/6 a line — with magenta filler between.
    std::vector<VertexPositionColor> vertices;
    ProbePoint pointProbe;
    ProbePoint triangleProbe;
    AppendListPrimitive(
        layout, PrimitiveType::PointListEXT, 0, Color::Red, 0.5f, vertices, &pointProbe);
    AppendListPrimitive(
        layout, PrimitiveType::PointListEXT, 1, Color::Magenta, 0.5f, vertices, nullptr);
    AppendListPrimitive(
        layout, PrimitiveType::TriangleList, 3, Color::Lime, 0.5f, vertices, &triangleProbe);
    // Two magenta points that must never be consumed by the triangle or line draws.
    AppendListPrimitive(
        layout, PrimitiveType::PointListEXT, 2, Color::Magenta, 0.5f, vertices, nullptr);
    AppendListPrimitive(
        layout, PrimitiveType::PointListEXT, 4, Color::Magenta, 0.5f, vertices, nullptr);
    const float lineY = layout.MidY();
    vertices.push_back(VertexAtPixel(
        layout, layout.CenterX(5) - layout.HalfWidth(), lineY, Color::Blue, 0.5f));
    vertices.push_back(VertexAtPixel(
        layout, layout.CenterX(6) + layout.HalfWidth(), lineY, Color::Blue, 0.5f));

    const int vertexCount = static_cast<int>(vertices.size());
    ASSERT_EQ(9, vertexCount);
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), vertexCount, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), vertexCount);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);
    device.DrawPrimitives(PrimitiveType::PointListEXT, 0, 1);
    device.DrawPrimitives(PrimitiveType::TriangleList, 2, 1);
    device.DrawPrimitives(PrimitiveType::LineList, 7, 1);

    const FrameSnapshot pixels =
        CaptureBackbuffer(device, layout.width, layout.height);
    EXPECT_TRUE(pixels.HasExactWithin(
        static_cast<int>(pointProbe.pixelX), static_cast<int>(pointProbe.pixelY),
        2, Color::Red))
        << "the point draw lost its own single-vertex range";
    EXPECT_TRUE(pixels.HasExactWithin(
        static_cast<int>(triangleProbe.pixelX), static_cast<int>(triangleProbe.pixelY),
        2, Color::Lime))
        << "the triangle draw lost its own three-vertex range";
    EXPECT_TRUE(pixels.HasExactWithin(
        static_cast<int>(layout.CenterX(6)), static_cast<int>(lineY), 2, Color::Blue))
        << "the line draw lost its own two-vertex range";
    EXPECT_EQ(0, pixels.CountExact(Color::Magenta))
        << "one topology's draw consumed another topology's vertices";
}
#endif

#ifdef CNA_BACKEND_SOFTWARE
// Software raster's documented v1 boundary is TriangleList only, and its non-indexed paths address
// the bound buffer from element zero: they enforce primitiveCount against the buffer size but never
// apply vertexStart at all. That is the Software counterpart of this task's Bgfx defect and is
// recorded as its own finding (REMED-GFX-119), not corrected here. Pinning it keeps the boundary
// explicit and makes the follow-up fix an obvious, deliberate change.
TEST_F(NonIndexedDrawRangeTest, SoftwareNonIndexedVertexStartIsCurrentlyIgnored)
{
    RequireRangeRendering();

    const SlotLayout layout = BackbufferLayout();
    const RangePlan plan =
        BuildRangePlan(layout, PrimitiveType::TriangleList, 6, 3);
    const int vertexCount = static_cast<int>(plan.vertices.size());

    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), vertexCount, BufferUsage::None);
    vertexBuffer.SetData(plan.vertices.data(), vertexCount);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);
    device.DrawPrimitives(PrimitiveType::TriangleList, 6, 3);

    const FrameSnapshot pixels =
        CaptureBackbuffer(device, layout.width, layout.height);
    // REMED-GFX-119: the draw renders the buffer's first three triangles instead of the requested
    // ones, so the magenta prefix decoys are on screen and the intended range is not.
    EXPECT_GT(pixels.CountExact(Color::Magenta), 0)
        << "REMED-GFX-119: Software non-indexed draws now honour vertexStart -- flip this test";

    // Non-TriangleList topologies stay explicitly rejected rather than approximated.
    EXPECT_THROW(
        device.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 1), std::runtime_error);
    EXPECT_THROW(
        device.DrawPrimitives(PrimitiveType::LineList, 0, 1), std::runtime_error);
    EXPECT_THROW(
        device.DrawPrimitives(PrimitiveType::LineStrip, 0, 1), std::runtime_error);
    EXPECT_THROW(
        device.DrawPrimitives(PrimitiveType::PointListEXT, 0, 1), std::runtime_error);
}
#endif

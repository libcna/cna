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
// Renderer scope. Bgfx, EasyGL, WebGPU, Vulkan, D3D9, D3D11 and Software raster render 3D triangles
// and support backbuffer readback, so they carry the permanent TriangleList coverage. The full
// five-topology sweep additionally needs PointListEXT, which Vulkan/D3D9/D3D11/D3D12 still route
// through their triangle-list default (the independent REMED-GFX-114), so that sweep runs on Bgfx,
// EasyGL and WebGPU. Software raster keeps its documented TriangleList-only v1 boundary, so its
// explicit rejection of the other four topologies is asserted in its own section below
// (REMED-GFX-119) rather than in the shared sweep.

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"

// Lets CNA_RENDERER_IS name identities bare, matching the guards it replaced.
using namespace CNA::Testing::Renderers;

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
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

// plans/plan_runtimerenderer.md RTR-P9-9: this file's bgfx blocks call bgfx:: directly and hold a
// BgfxRenderer pointer, so they stay COMPILE-time -- no runtime predicate makes a type exist. The
// condition widens from the DEFAULT renderer's macro to "compiled into this build", so a
// multi-renderer build holding bgfx without selecting it still compiles them; each test inside then
// checks at runtime that bgfx is the ACTIVE renderer.
#if defined(CNA_RENDERER_BGFX) || defined(CNA_RENDERER_PRESENT_BGFX)
#define CNA_TEST_BGFX_AVAILABLE 1
#endif

#ifdef CNA_TEST_BGFX_AVAILABLE
#include "CNA/Internal/Renderers/Bgfx/BgfxRenderer.hpp"
#endif

using CNA::GraphicsCapability;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::CullMode;
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
using Microsoft::Xna::Framework::Graphics::VertexPositionColorTexture;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTexture;
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
    /// non-black colour, so RGB alone answers the question — while the alpha a renderer leaves in a
    /// *cleared* render target it later samples back is its own convention (Vulkan resolves the
    /// black clear to alpha 0, the OpenGL-family renderers to alpha 255) and has nothing to do with
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
        // REMED-GFX-124: NOT Color::Transparent. A renderer whose render-target readback does
        // nothing still returns a fully written all-zero frame, because Texture2D::GetData hands
        // the renderer a scratch buffer it zero-initialized itself -- so a transparent-black
        // pre-fill is byte-identical to that fabricated result, and the exclusivity assertions
        // below (which treat RGB 0,0,0 as "not lit" and look for an absent decoy colour) would all
        // pass on an empty frame. A pre-fill that matches no rendered colour makes an unwritten
        // readback fail instead.
        snapshot.pixels.assign(
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
            Color(0xCD, 0xCD, 0xCD, 0xCD));
        const Rectangle region(0, 0, width, height);
        target.GetData(
            0, &region, snapshot.pixels.data(), 0,
            static_cast<int>(snapshot.pixels.size()));
        return snapshot;
    }

    /// Every intended primitive rendered at its own pixel. The 5x5 probe absorbs the one-pixel
    /// differences each renderer's own pixel-centre convention legitimately allows without
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

    /// Slots that hold rendered geometry, as a comma-separated list. Because every slot is an
    /// exclusive screen region owned by exactly one list primitive, the lit slots *are* the
    /// primitives a draw consumed — so a failure message names the actual consumed range instead of
    /// only reporting that some pixel was wrong. "none" when the frame is still at @p background.
    std::string DescribeLitSlots(
        const FrameSnapshot& snapshot, const SlotLayout& layout, const Color& background)
    {
        std::string lit;
        for (int slot = 0; slot < kSlotCount; ++slot)
        {
            const int x0 = static_cast<int>(layout.Boundary(slot));
            const int x1 = static_cast<int>(layout.Boundary(slot + 1) + 0.999f);
            if (snapshot.CountLitInColumns(x0, x1, background) > 0)
            {
                if (!lit.empty())
                    lit += ',';
                lit += std::to_string(slot);
            }
        }
        return lit.empty() ? "none" : lit;
    }

    /// The list-topology slots a draw is supposed to consume, in the same notation, so a failure
    /// message can print "expected 2,3,4 / actual 0,1,2" rather than a pixel coordinate alone.
    std::string DescribeExpectedSlots(
        PrimitiveType primitive, int vertexStart, int primitiveCount)
    {
        const int perPrimitive = VerticesPerListPrimitive(primitive);
        if (perPrimitive == 0)
            return "?";
        const int firstSlot = vertexStart / perPrimitive;
        std::string expected;
        for (int i = 0; i < primitiveCount; ++i)
        {
            if (!expected.empty())
                expected += ',';
            expected += std::to_string(firstSlot + i);
        }
        return expected.empty() ? "none" : expected;
    }

    /// Clip-space corners of one slot's triangle, so several vertex layouts can be built from the
    /// same geometry and the only thing that differs between them is the declared stride.
    std::array<Vector3, 3> SlotTrianglePositions(const SlotLayout& layout, int slot)
    {
        const float cx = layout.CenterX(slot);
        const float hw = layout.HalfWidth();
        const float midY = layout.MidY();
        const float hh = layout.HalfHeight();
        const auto toClip = [&](float pixelX, float pixelY) {
            return Vector3(
                (2.0f * pixelX / static_cast<float>(layout.width)) - 1.0f,
                1.0f - (2.0f * pixelY / static_cast<float>(layout.height)),
                0.5f);
        };
        return {
            toClip(cx - hw, midY + hh),
            toClip(cx + hw, midY + hh),
            toClip(cx, midY - hh),
        };
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

        // GTEST_SKIP() only unwinds the function it is called from; called from an ordinary
        // member function like RequireRangeRendering() below it cannot skip the test body that
        // invokes it. SetUp() is where GoogleTest itself checks for a skip, so the capability gate
        // has to run here too -- RequireRangeRendering() keeps its own copy for the state-setup
        // calls that follow it, which only run once SetUp() has already let the test proceed.
        void SetUp() override
        {
            if (!device.SupportsCapability(GraphicsCapability::ThreeD))
                GTEST_SKIP() << "Renderer explicitly does not support 3D rendering";
        }

        /// Explicit device state for the whole fixture: nothing here may depend on a framework
        /// default, because a default-valued no-op fallback would let a buggy path pass.
        void RequireRangeRendering()
        {
            if (!device.SupportsCapability(GraphicsCapability::ThreeD))
                GTEST_SKIP() << "Renderer explicitly does not support 3D rendering";
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


// Proof 1 of the range contract, isolated: primitiveCount alone must limit the consumed vertex
// range. vertexStart is zero, so nothing here depends on the offset half of the contract; every
// magenta primitive lives strictly after the requested range.
TEST_F(NonIndexedDrawRangeTest, PersistentDrawHonorsPrimitiveCountAtVertexStartZero)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, WebGPU, Vulkan, DirectX9, DirectX11, Software);
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
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, WebGPU, Vulkan, DirectX9, DirectX11, Software);
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
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, WebGPU, Vulkan, DirectX9, DirectX11, Software);
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
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, WebGPU, Vulkan, DirectX9, DirectX11, Software);
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
// keep its own vertexStart/primitiveCount; a renderer that resolves either from live state at flush
// time would render the last range three times.
TEST_F(NonIndexedDrawRangeTest, DeferredNonIndexedRangesAtoBtoAKeepTheirOwnRange)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, WebGPU, Vulkan, DirectX9, DirectX11, Software);
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
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, WebGPU, Vulkan, DirectX9, DirectX11, Software);
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
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, WebGPU, Vulkan, DirectX9, DirectX11, Software);
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
    // texture path. REMED-GFX-124 restored this half on Software too -- the target's colour storage
    // is now reachable through the same capability every other texture is sampled through, so this
    // is no longer a per-renderer carve-out.
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
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, WebGPU, Vulkan, DirectX9, DirectX11, Software);
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

// The untyped DrawUserPrimitives overload is the only caller of the renderer's
// DrawColoredPrimitives entry point, which takes no vertexStart at all. It owes the same result
// through a different mechanism: the temporary buffer it uploads holds exactly
// PrimitiveVerts(type, primitiveCount) vertices copied from vertexOffset, so binding that whole
// buffer already is the exact range. This is why REMED-GFX-113 left that path untouched.
TEST_F(NonIndexedDrawRangeTest, UntypedDrawUserPrimitivesUploadsOnlyTheRequestedRange)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, WebGPU, Vulkan, DirectX9, DirectX11, Software);
    RequireRangeRendering();

    const SlotLayout layout = BackbufferLayout();
    const RangePlan plan =
        BuildRangePlan(layout, PrimitiveType::TriangleList, 6, 3);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(nullptr);
    device.DrawUserPrimitives(
        PrimitiveType::TriangleList,
        static_cast<const void*>(plan.vertices.data()),
        6,
        3);

    const FrameSnapshot pixels =
        CaptureBackbuffer(device, layout.width, layout.height);
    ExpectIntendedPrimitivesRendered(pixels, plan, "untyped DrawUserPrimitives copied range");
    ExpectRangeExclusive(
        pixels, plan, Color::Black, "untyped DrawUserPrimitives copied range");
}


// Nothing a rejected range requested may reach the target: after a clean frame, every invalid draw
// must leave the framebuffer exactly as the clear left it.
TEST_F(NonIndexedDrawRangeTest, RejectedNonIndexedRangesRenderNothing)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, WebGPU, Vulkan, DirectX9, DirectX11, Software);
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
        << "a rejected non-indexed draw still reached the renderer -- "
        << pixels.DescribeFirstLitInColumns(0, layout.width, Color::Black);
}


// The public non-indexed range contract is owed by every renderer, including the ones that
// render nothing: an out-of-buffer range must be rejected deterministically, before anything
// reaches a renderer at all. Deliberately unguarded, exactly like its indexed sibling in
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


// Every supported topology owes the same exact range, with valid decoy geometry before and after
// the requested vertices. The per-topology vertex counts are the public formulas themselves, so a
// renderer that consumed 3*primitiveCount vertices for a strip, or bound the whole buffer for any
// topology, fails here.
TEST_F(NonIndexedDrawRangeTest, EverySupportedTopologyHonorsVertexStartAndExactCount)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, WebGPU);
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

// The same range, drawn once per topology into one frame at non-overlapping slots. A renderer that
// let one draw's topology or range leak into another would put geometry in a neighbour's region.
TEST_F(NonIndexedDrawRangeTest, TopologySwitchesKeepTheirOwnRangesInOneFrame)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Bgfx, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, WebGPU);
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

#ifdef CNA_TEST_BGFX_AVAILABLE
// The exact native binding, not just its pixels. bgfx offers no way to read a submitted draw's
// stream range back, so BgfxRenderer records the (startVertex, numVertices) pair it handed
// to bgfx::setVertexBuffer; this asserts that pair equals the public element offset and the
// topology-derived vertex count for every topology. The whole-buffer overload this replaced passed
// (0, UINT32_MAX) and let bgfx clamp to the buffer's own allocated size.
TEST_F(NonIndexedDrawRangeTest, BgfxNonIndexedBindingIsTheExactElementRange)
{
    // plans/plan_runtimerenderer.md RTR-P9-9: compiled whenever bgfx is in the build,
    // run only when bgfx is the active renderer.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::Bgfx);
    RequireRangeRendering();

    auto* renderer =
        dynamic_cast<CNA::Internal::Renderers::Bgfx::BgfxRenderer*>(&device.GetRenderer());
    ASSERT_NE(nullptr, renderer);

    const SlotLayout layout = BackbufferLayout();
    struct BindingCase
    {
        PrimitiveType primitive;
        int vertexStart;
        int primitiveCount;
        std::uint32_t expectedCount;
    };
    constexpr std::array<BindingCase, 8> cases{{
        {PrimitiveType::TriangleList, 0, 1, 3},
        {PrimitiveType::TriangleList, 6, 3, 9},
        {PrimitiveType::TriangleList, 18, 1, 3},
        {PrimitiveType::TriangleStrip, 2, 2, 4},
        {PrimitiveType::LineList, 4, 3, 6},
        {PrimitiveType::LineStrip, 2, 3, 4},
        {PrimitiveType::PointListEXT, 2, 3, 3},
        {PrimitiveType::PointListEXT, 6, 1, 1},
    }};

    BasicEffect effect(device);
    for (const BindingCase& bindingCase : cases)
    {
        const RangePlan plan = BuildRangePlan(
            layout, bindingCase.primitive,
            bindingCase.vertexStart, bindingCase.primitiveCount);
        const int vertexCount = static_cast<int>(plan.vertices.size());
        VertexBuffer vertexBuffer(
            device, PositionColorDeclaration(), vertexCount, BufferUsage::None);
        vertexBuffer.SetData(plan.vertices.data(), vertexCount);

        ApplyVertexColorEffect(effect);
        device.Clear(Color::Black);
        device.SetVertexBuffer(&vertexBuffer);
        device.DrawPrimitives(
            bindingCase.primitive, bindingCase.vertexStart, bindingCase.primitiveCount);

        EXPECT_EQ(
            static_cast<std::uint32_t>(bindingCase.vertexStart),
            renderer->lastNonIndexedBindStartEXT_)
            << TopologyName(bindingCase.primitive)
            << ": native startVertex is not the public vertexStart element offset";
        EXPECT_EQ(bindingCase.expectedCount, renderer->lastNonIndexedBindCountEXT_)
            << TopologyName(bindingCase.primitive)
            << ": native numVertices is not the topology-derived consumed count";
        EXPECT_LE(
            renderer->lastNonIndexedBindStartEXT_ + renderer->lastNonIndexedBindCountEXT_,
            static_cast<std::uint32_t>(vertexCount))
            << TopologyName(bindingCase.primitive)
            << ": native binding leaves the logical vertex buffer";
        device.SetVertexBuffer(nullptr);
    }
}

// FillMode.WireFrame re-expands triangles into an absolute-index line list, so that path owns its
// range through its own indices and must keep binding from element zero -- while still drawing only
// the requested triangles' edges.
TEST_F(NonIndexedDrawRangeTest, BgfxWireframeNonIndexedRangeStillHonorsVertexStart)
{
    // plans/plan_runtimerenderer.md RTR-P9-9: compiled whenever bgfx is in the build,
    // run only when bgfx is the active renderer.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::Bgfx);
    RequireRangeRendering();

    auto* renderer =
        dynamic_cast<CNA::Internal::Renderers::Bgfx::BgfxRenderer*>(&device.GetRenderer());
    ASSERT_NE(nullptr, renderer);

    const SlotLayout layout = BackbufferLayout();
    const RangePlan plan =
        BuildRangePlan(layout, PrimitiveType::TriangleList, 6, 3);
    const int vertexCount = static_cast<int>(plan.vertices.size());
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), vertexCount, BufferUsage::None);
    vertexBuffer.SetData(plan.vertices.data(), vertexCount);

    RasterizerState wireframe;
    wireframe.setCullModeProperty(
        Microsoft::Xna::Framework::Graphics::CullMode::None);
    wireframe.setFillModeProperty(FillMode::WireFrame);
    device.setRasterizerStateProperty(wireframe);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);
    device.DrawPrimitives(PrimitiveType::TriangleList, 6, 3);

    // The expanded indices are absolute (vertexStart + local), so the stream must start at zero.
    EXPECT_EQ(0u, renderer->lastNonIndexedBindStartEXT_)
        << "the wireframe path's absolute expanded indices need a zero-based vertex binding";
    EXPECT_EQ(
        static_cast<std::uint32_t>(vertexCount), renderer->lastNonIndexedBindCountEXT_)
        << "the wireframe path must keep every vertex its expanded indices can address bound";

    const FrameSnapshot pixels =
        CaptureBackbuffer(device, layout.width, layout.height);
    ExpectRangeExclusive(pixels, plan, Color::Black, "wireframe range");
    device.setRasterizerStateProperty(RasterizerState::CullNone);
}

// Narrowing the binding must stay free: no repacking, no per-draw handle and no extra native
// buffer version. REMED-GFX-109's cardinality therefore stays at the one-object baseline across
// many different ranges and returns to the process baseline after disposal.
TEST_F(NonIndexedDrawRangeTest, BgfxNonIndexedRangesAllocateNoPerDrawNativeResources)
{
    // plans/plan_runtimerenderer.md RTR-P9-9: compiled whenever bgfx is in the build,
    // run only when bgfx is the active renderer.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::Bgfx);
    RequireRangeRendering();

    device.Present();
    device.Present();
    const bgfx::Stats* stats = bgfx::getStats();
    ASSERT_NE(nullptr, stats);
    const std::uint16_t processVertexBaseline = stats->numDynamicVertexBuffers;

    const SlotLayout layout = BackbufferLayout();
    const RangePlan plan =
        BuildRangePlan(layout, PrimitiveType::TriangleList, 0, 1);
    const int vertexCount = static_cast<int>(plan.vertices.size());
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), vertexCount, BufferUsage::None);
    vertexBuffer.SetData(plan.vertices.data(), vertexCount);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.SetVertexBuffer(&vertexBuffer);

    device.Present();
    device.Present();
    stats = bgfx::getStats();
    ASSERT_NE(nullptr, stats);
    const std::uint16_t liveVertexBaseline = stats->numDynamicVertexBuffers;
    EXPECT_EQ(processVertexBaseline + 1u, liveVertexBaseline);

    for (int frame = 0; frame < 24; ++frame)
    {
        device.Clear(Color::Black);
        for (int slot = 0; slot < kSlotCount; ++slot)
            device.DrawPrimitives(PrimitiveType::TriangleList, slot * 3, 1);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, kSlotCount);
        device.DrawPrimitives(PrimitiveType::LineList, 4, 3);
        device.DrawPrimitives(PrimitiveType::PointListEXT, 5, 4);
        device.Present();

        stats = bgfx::getStats();
        ASSERT_NE(nullptr, stats);
        EXPECT_LE(stats->numDynamicVertexBuffers, liveVertexBaseline);
    }

    device.SetVertexBuffer(nullptr);
    vertexBuffer.Dispose();
    device.Present();
    device.Present();

    stats = bgfx::getStats();
    ASSERT_NE(nullptr, stats);
    EXPECT_EQ(processVertexBaseline, stats->numDynamicVertexBuffers);
}

// The public buffer may be disposed while draws that referenced it are still queued for the frame.
TEST_F(NonIndexedDrawRangeTest, BgfxDisposingAfterQueuedRangedDrawsIsSafe)
{
    // plans/plan_runtimerenderer.md RTR-P9-9: compiled whenever bgfx is in the build,
    // run only when bgfx is the active renderer.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::Bgfx);
    RequireRangeRendering();

    const SlotLayout layout = BackbufferLayout();
    const RangePlan plan =
        BuildRangePlan(layout, PrimitiveType::TriangleList, 0, 1);
    const int vertexCount = static_cast<int>(plan.vertices.size());

    {
        DynamicVertexBuffer vertexBuffer(
            device, PositionColorDeclaration(), vertexCount, BufferUsage::None);
        vertexBuffer.SetData(plan.vertices.data(), 0, vertexCount, SetDataOptions::None);

        BasicEffect effect(device);
        ApplyVertexColorEffect(effect);
        device.Clear(Color::Black);
        device.SetVertexBuffer(&vertexBuffer);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        device.DrawPrimitives(PrimitiveType::TriangleList, 9, 1);
        vertexBuffer.SetData(plan.vertices.data(), 0, vertexCount, SetDataOptions::None);
        device.DrawPrimitives(PrimitiveType::TriangleList, 18, 1);
        device.SetVertexBuffer(nullptr);
        vertexBuffer.Dispose();
    }

    EXPECT_NO_THROW(device.Present());
    EXPECT_NO_THROW(device.Present());
}
#endif

// REMED-GFX-119. Software raster's non-indexed paths address the bound buffer with the raw loop
// ordinal, so `vertexStart` never selects the first consumed vertex. The three tests below separate
// the two halves of the public contract from each other and from every render state that could
// plausibly produce the same picture, so the classification does not rest on the finding's title.

// The offset half of the contract, isolated per case. The first two cases request vertexStart 0, so
// they exercise only the count half and hold both before and after the correction; every later case
// keeps the same primitiveCount as one that passed, changing nothing but the offset. A failure
// prints the slots that actually rendered, and a list slot is one primitive's exclusive screen
// region, so the message names the consumed vertex range rather than a single wrong pixel.
TEST_F(NonIndexedDrawRangeTest, SoftwareNonIndexedDrawConsumesExactlyTheRequestedVertexRange)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software);
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
    constexpr std::array<RangeCase, 7> cases{{
        {0, 1, "count only: first single primitive"},
        {0, 3, "count only: first three primitives"},
        {0, 7, "complete buffer"},
        {9, 3, "offset only: middle range, same count as case 2"},
        {6, 3, "offset only: interior range, same count as case 2"},
        {18, 1, "offset only: final single primitive, same count as case 1"},
        {3, 5, "offset and count: decoys on both sides"},
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
        const std::string consumed = "vertexStart=" +
            std::to_string(rangeCase.vertexStart) + " primitiveCount=" +
            std::to_string(rangeCase.primitiveCount) + " expected slots " +
            DescribeExpectedSlots(
                PrimitiveType::TriangleList, rangeCase.vertexStart,
                rangeCase.primitiveCount) +
            ", actual " + DescribeLitSlots(pixels, layout, Color::Black);
        SCOPED_TRACE(consumed);
        ExpectIntendedPrimitivesRendered(pixels, plan, rangeCase.label);
        ExpectRangeExclusive(pixels, plan, Color::Black, rangeCase.label);
    }
}

// The same requested range under render states that could each independently produce a wrong
// picture: both cull modes against a fixed winding, depth testing on and off, an explicitly
// restated full viewport and scissor, and a readback from a render target instead of the
// backbuffer. Software raster's own regressions own these individually (REMED-GFX-030 depth,
// REMED-GFX-079 viewport, REMED-GFX-080 scissor, REMED-GFX-082 fill mode); here they exist only to
// rule every one of them out as the cause of the consumed range.
TEST_F(NonIndexedDrawRangeTest, SoftwareNonIndexedRangeIsIndependentOfRenderStateAndTarget)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software);
    RequireRangeRendering();

    const SlotLayout layout = BackbufferLayout();
    const RangePlan plan =
        BuildRangePlan(layout, PrimitiveType::TriangleList, 6, 3);
    const int vertexCount = static_cast<int>(plan.vertices.size());
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), vertexCount, BufferUsage::None);
    vertexBuffer.SetData(plan.vertices.data(), vertexCount);
    BasicEffect effect(device);

    struct StateCase
    {
        const char* label;
        CullMode cullMode;
        bool depthEnabled;
        bool alphaBlend;
        /// Draws the same range into a RenderTarget2D first, then measures the backbuffer draw.
        /// The round trip is what this case is about; the target's own content and the
        /// producer-to-consumer sampling assertion live in
        /// NonIndexedRangeHoldsOnRenderTargetAndBackbuffer, which REMED-GFX-124 restored on every
        /// renderer including Software.
        bool renderTargetRoundTrip;
        /// False only for the cull mode that removes this fixture's winding outright. Stating it
        /// per case is what stops a state that silently renders nothing from passing the range
        /// assertions vacuously.
        bool expectsGeometry;
    };
    constexpr std::array<StateCase, 6> stateCases{{
        {"CullNone, no depth, opaque, backbuffer",
         CullMode::None, false, false, false, true},
        {"CullClockwise", CullMode::CullClockwiseFace, false, false, false, true},
        {"CullCounterClockwise",
         CullMode::CullCounterClockwiseFace, false, false, false, false},
        {"depth test and write enabled", CullMode::None, true, false, false, true},
        {"AlphaBlend", CullMode::None, false, true, false, true},
        {"after a render-target round trip", CullMode::None, false, false, true, true},
    }};

    for (const StateCase& stateCase : stateCases)
    {
        SCOPED_TRACE(stateCase.label);
        RasterizerState rasterizerState;
        rasterizerState.setCullModeProperty(stateCase.cullMode);
        rasterizerState.setFillModeProperty(FillMode::Solid);
        rasterizerState.setScissorTestEnableProperty(false);
        device.setRasterizerStateProperty(rasterizerState);
        device.setDepthStencilStateProperty(
            stateCase.depthEnabled ? DepthStencilState::Default : DepthStencilState::None);
        device.setBlendStateProperty(
            stateCase.alphaBlend ? BlendState::AlphaBlend : BlendState::Opaque);
        device.setViewportProperty(Viewport(0, 0, layout.width, layout.height));
        device.setScissorRectangleProperty(Rectangle(0, 0, layout.width, layout.height));

        if (stateCase.renderTargetRoundTrip)
        {
            RenderTarget2D target(
                device, layout.width, layout.height, false, SurfaceFormat::Color,
                DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
            device.SetRenderTarget(&target);
            ApplyVertexColorEffect(effect);
            device.Clear(Color::Black);
            device.SetVertexBuffer(&vertexBuffer);
            device.DrawPrimitives(PrimitiveType::TriangleList, 6, 3);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        }

        ApplyVertexColorEffect(effect);
        device.Clear(Color::Black);
        device.SetVertexBuffer(&vertexBuffer);
        device.DrawPrimitives(PrimitiveType::TriangleList, 6, 3);

        const FrameSnapshot pixels =
            CaptureBackbuffer(device, layout.width, layout.height);

        const int lit = pixels.CountLitInColumns(0, layout.width, Color::Black);
        if (!stateCase.expectsGeometry)
        {
            // The control that proves the cull mode really is applied: the same draw under the
            // opposite winding rule must remove the fixture outright.
            EXPECT_EQ(0, lit)
                << stateCase.label << ": the culled winding still rendered (actual slots "
                << DescribeLitSlots(pixels, layout, Color::Black) << ')';
            continue;
        }
        EXPECT_GT(lit, 0)
            << stateCase.label
            << ": nothing rendered at all, so this state proves nothing about the range";
        EXPECT_EQ(0, pixels.CountExact(Color::Magenta))
            << stateCase.label << ": decoy geometry outside the requested range rendered ("
            << "actual slots " << DescribeLitSlots(pixels, layout, Color::Black) << ')';
        ExpectIntendedPrimitivesRendered(pixels, plan, stateCase.label);
        ExpectRangeExclusive(pixels, plan, Color::Black, stateCase.label);
    }

    device.setRasterizerStateProperty(RasterizerState::CullNone);
}

// Software raster's documented v1 boundary is TriangleList only. The other four topologies stay
// explicitly rejected rather than approximated through the triangle path, and a rejected draw must
// leave the frame exactly as the clear left it.
TEST_F(NonIndexedDrawRangeTest, SoftwareRejectsUnsupportedNonIndexedTopologiesWithoutRendering)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software);
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

    constexpr std::array<PrimitiveType, 4> unsupported{
        PrimitiveType::TriangleStrip,
        PrimitiveType::LineList,
        PrimitiveType::LineStrip,
        PrimitiveType::PointListEXT,
    };
    for (const PrimitiveType primitive : unsupported)
    {
        // Both a zero and a nonzero offset, so the rejection cannot depend on the range either.
        EXPECT_THROW(device.DrawPrimitives(primitive, 0, 1), std::runtime_error)
            << TopologyName(primitive) << " at vertexStart 0 was not rejected";
        EXPECT_THROW(device.DrawPrimitives(primitive, 6, 1), std::runtime_error)
            << TopologyName(primitive) << " at vertexStart 6 was not rejected";
    }

    const FrameSnapshot pixels =
        CaptureBackbuffer(device, layout.width, layout.height);
    EXPECT_EQ(0, pixels.CountLitInColumns(0, layout.width, Color::Black))
        << "a rejected topology still rasterized -- "
        << pixels.DescribeFirstLitInColumns(0, layout.width, Color::Black);
}

// A rejected range must leave nothing partially committed on the CPU raster path: the draw that
// follows it renders exactly what the identical draw before it rendered. Reading each of the three
// frames separately is what makes "the valid draw still works" a real assertion rather than a
// side effect of the last draw overwriting the frame.
TEST_F(NonIndexedDrawRangeTest, SoftwareValidInvalidValidNonIndexedSequenceKeepsRendering)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software);
    RequireRangeRendering();

    const SlotLayout layout = BackbufferLayout();
    const RangePlan plan =
        BuildRangePlan(layout, PrimitiveType::TriangleList, 9, 3);
    const int vertexCount = static_cast<int>(plan.vertices.size());
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), vertexCount, BufferUsage::None);
    vertexBuffer.SetData(plan.vertices.data(), vertexCount);

    BasicEffect effect(device);
    const auto drawValidRange = [&]() {
        ApplyVertexColorEffect(effect);
        device.Clear(Color::Black);
        device.SetVertexBuffer(&vertexBuffer);
        device.DrawPrimitives(PrimitiveType::TriangleList, 9, 3);
        return CaptureBackbuffer(device, layout.width, layout.height);
    };

    const FrameSnapshot before = drawValidRange();
    ExpectIntendedPrimitivesRendered(before, plan, "valid draw before the rejected range");
    ExpectRangeExclusive(before, plan, Color::Black, "valid draw before the rejected range");

    // Every rejected form: past the end, one primitive too many, a topology-count overflow, a
    // byte-offset-scale overflow and an unsupported topology. None may render or corrupt state.
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);
    EXPECT_THROW(
        device.DrawPrimitives(PrimitiveType::TriangleList, vertexCount, 1),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawPrimitives(PrimitiveType::TriangleList, 19, 1),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawPrimitives(PrimitiveType::TriangleList, -1, 1),
        System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 0),
        System::ArgumentOutOfRangeException);
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
    EXPECT_THROW(
        device.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 1), std::runtime_error);

    const FrameSnapshot rejected =
        CaptureBackbuffer(device, layout.width, layout.height);
    EXPECT_EQ(0, rejected.CountLitInColumns(0, layout.width, Color::Black))
        << "a rejected non-indexed range reached Software storage -- "
        << rejected.DescribeFirstLitInColumns(0, layout.width, Color::Black);

    const FrameSnapshot after = drawValidRange();
    ExpectIntendedPrimitivesRendered(after, plan, "valid draw after the rejected range");
    ExpectRangeExclusive(after, plan, Color::Black, "valid draw after the rejected range");
    EXPECT_EQ(
        before.CountLitInColumns(0, layout.width, Color::Black),
        after.CountLitInColumns(0, layout.width, Color::Black))
        << "the valid draw rendered a different number of pixels after a rejected range";
}

// vertexStart is an element offset, so its byte position is the element index times the buffer's
// own declared stride. Every test above uses the 16-byte VertexPositionColor layout alone, which a
// hardcoded stride would satisfy just as well; these three layouts (20, 24 and 32 bytes) request
// the identical element range and must light the identical slots. Colour is deliberately not
// asserted -- two of these layouts carry none -- because the slot a triangle lands in is what the
// stride scaling determines.
TEST_F(NonIndexedDrawRangeTest, SoftwareNonIndexedVertexStartScalesByTheDeclaredStride)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software);
    RequireRangeRendering();

    const SlotLayout layout = BackbufferLayout();
    BasicEffect effect(device);
    // Untextured on purpose: two of the layouts below have no colour channel, so the fixed
    // white diffuse colour is what renders, and no sampler state can influence the result.
    effect.setTextureEnabledProperty(false);
    effect.VertexColorEnabled = false;

    // Slots 3, 4 and 5 -- element range [9, 18) of the seven-triangle fixture.
    constexpr int kVertexStart = 9;
    constexpr int kPrimitiveCount = 3;
    const char* const expectedSlots = "3,4,5";

    std::vector<VertexPositionTexture> positionTexture;
    std::vector<VertexPositionColorTexture> positionColorTexture;
    std::vector<VertexPositionNormalTexture> positionNormalTexture;
    for (int slot = 0; slot < kSlotCount; ++slot)
    {
        for (const Vector3& corner : SlotTrianglePositions(layout, slot))
        {
            positionTexture.emplace_back(corner, Vector2(0.0f, 0.0f));
            positionColorTexture.emplace_back(corner, Color::Lime, Vector2(0.0f, 0.0f));
            positionNormalTexture.emplace_back(
                corner, Vector3(0.0f, 0.0f, 1.0f), Vector2(0.0f, 0.0f));
        }
    }
    const int vertexCount = kSlotCount * 3;

    const auto drawAndDescribe = [&](VertexBuffer& vertexBuffer) {
        effect.Apply();
        device.Clear(Color::Black);
        device.SetVertexBuffer(&vertexBuffer);
        device.DrawPrimitives(
            PrimitiveType::TriangleList, kVertexStart, kPrimitiveCount);
        const FrameSnapshot pixels =
            CaptureBackbuffer(device, layout.width, layout.height);
        return DescribeLitSlots(pixels, layout, Color::Black);
    };

    {
        VertexBuffer buffer(
            device, VertexPositionTexture::getVertexDeclarationStatic(), vertexCount,
            BufferUsage::None);
        buffer.SetData(positionTexture.data(), vertexCount);
        EXPECT_EQ(expectedSlots, drawAndDescribe(buffer))
            << "stride 20 (VertexPositionTexture) consumed a different element range";
    }
    {
        VertexBuffer buffer(
            device, VertexPositionColorTexture::getVertexDeclarationStatic(), vertexCount,
            BufferUsage::None);
        buffer.SetData(positionColorTexture.data(), vertexCount);
        EXPECT_EQ(expectedSlots, drawAndDescribe(buffer))
            << "stride 24 (VertexPositionColorTexture) consumed a different element range";
    }
    {
        VertexBuffer buffer(
            device, VertexPositionNormalTexture::getVertexDeclarationStatic(), vertexCount,
            BufferUsage::None);
        buffer.SetData(positionNormalTexture.data(), vertexCount);
        EXPECT_EQ(expectedSlots, drawAndDescribe(buffer))
            << "stride 32 (VertexPositionNormalTexture) consumed a different element range";
    }

    // The explicit-VertexDeclaration DrawUserPrimitives overload (REMED-GFX-043) reaches the same
    // renderer entry point with a rebased copy and vertexStart 0, so it must light the same slots
    // from the same source offset -- the double-apply guard at a non-16-byte stride.
    //
    // Its source array is packed by hand rather than reusing positionColorTexture above: this
    // overload reads raw bytes at the declared stride, and the XNA vertex structs all carry a
    // vtable from their IVertexType virtual base, so sizeof(VertexPositionColorTexture) -- and
    // therefore its static VertexDeclaration's VertexStride -- is larger than the 24-byte GPU
    // layout. The persistent VertexBuffer cases above are unaffected because VertexBuffer::SetData
    // repacks into the GPU layout and uploads its own stride. That mismatch is its own finding
    // (REMED-GFX-125), not a draw-range question.
    struct PackedPositionColorTexture
    {
        float x, y, z;
        std::uint8_t r, g, b, a;
        float u, v;
    };
    static_assert(sizeof(PackedPositionColorTexture) == 24);
    const VertexDeclaration packedDeclaration(
        24,
        {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
            VertexElement(
                16, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        });
    std::vector<PackedPositionColorTexture> packedSource;
    packedSource.reserve(static_cast<std::size_t>(vertexCount));
    for (const VertexPositionColorTexture& vertex : positionColorTexture)
    {
        packedSource.push_back(PackedPositionColorTexture{
            vertex.Position.X, vertex.Position.Y, vertex.Position.Z,
            vertex.Color.getRProperty(), vertex.Color.getGProperty(),
            vertex.Color.getBProperty(), vertex.Color.getAProperty(),
            vertex.TextureCoordinate.X, vertex.TextureCoordinate.Y});
    }

    device.SetVertexBuffer(nullptr);
    effect.Apply();
    device.Clear(Color::Black);
    device.DrawUserPrimitives(
        PrimitiveType::TriangleList,
        static_cast<const void*>(packedSource.data()),
        kVertexStart,
        kPrimitiveCount,
        packedDeclaration);
    const FrameSnapshot userPixels =
        CaptureBackbuffer(device, layout.width, layout.height);
    EXPECT_EQ(expectedSlots, DescribeLitSlots(userPixels, layout, Color::Black))
        << "the explicit-declaration DrawUserPrimitives overload consumed a different range";
}

// Two buffers, alternating A -> B -> A in one frame with a different range each time. Software
// executes every draw immediately, so this pins that no range or buffer identity leaks from one
// draw into the next and that the two buffers stay independent.
TEST_F(NonIndexedDrawRangeTest, SoftwareNonIndexedRangesStayIndependentAcrossTwoBuffers)
{
    // plans/plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this group,
    // so on every other renderer these tests did not exist and reported nothing.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software);
    RequireRangeRendering();

    const SlotLayout layout = BackbufferLayout();
    std::vector<VertexPositionColor> bufferAVertices;
    std::vector<VertexPositionColor> bufferBVertices;
    ProbePoint firstProbe;
    ProbePoint middleProbe;
    ProbePoint lastProbe;
    // Buffer A owns slots 0 and 6 in distinct colours; buffer B owns slot 3. Everything else in
    // either buffer is a magenta decoy, so any range or buffer that leaks shows up as magenta.
    AppendListPrimitive(
        layout, PrimitiveType::TriangleList, 0, Color::Red, 0.5f, bufferAVertices, &firstProbe);
    for (int slot = 1; slot <= 5; ++slot)
        AppendListPrimitive(
            layout, PrimitiveType::TriangleList, slot, Color::Magenta, 0.5f,
            bufferAVertices, nullptr);
    AppendListPrimitive(
        layout, PrimitiveType::TriangleList, 6, Color::Blue, 0.5f, bufferAVertices, &lastProbe);
    for (int slot = 0; slot < kSlotCount; ++slot)
        AppendListPrimitive(
            layout, PrimitiveType::TriangleList, slot,
            slot == 3 ? Color::Lime : Color::Magenta, 0.5f, bufferBVertices,
            slot == 3 ? &middleProbe : nullptr);

    const int vertexCount = kSlotCount * 3;
    VertexBuffer bufferA(
        device, PositionColorDeclaration(), vertexCount, BufferUsage::None);
    bufferA.SetData(bufferAVertices.data(), vertexCount);
    DynamicVertexBuffer bufferB(
        device, PositionColorDeclaration(), vertexCount, BufferUsage::None);
    bufferB.SetData(bufferBVertices.data(), 0, vertexCount, SetDataOptions::None);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);

    device.SetVertexBuffer(&bufferA);
    device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);   // A, first range
    device.SetVertexBuffer(&bufferB);
    device.DrawPrimitives(PrimitiveType::TriangleList, 9, 1);   // B, middle range
    device.SetVertexBuffer(&bufferA);
    device.DrawPrimitives(PrimitiveType::TriangleList, 18, 1);  // A again, final range

    const FrameSnapshot pixels =
        CaptureBackbuffer(device, layout.width, layout.height);
    EXPECT_TRUE(pixels.HasExactWithin(
        static_cast<int>(firstProbe.pixelX), static_cast<int>(firstProbe.pixelY), 2, Color::Red))
        << "the first draw lost its own buffer or range";
    EXPECT_TRUE(pixels.HasExactWithin(
        static_cast<int>(middleProbe.pixelX), static_cast<int>(middleProbe.pixelY), 2,
        Color::Lime))
        << "the second draw lost its own buffer or range";
    EXPECT_TRUE(pixels.HasExactWithin(
        static_cast<int>(lastProbe.pixelX), static_cast<int>(lastProbe.pixelY), 2, Color::Blue))
        << "the third draw lost its own buffer or range";
    EXPECT_EQ(0, pixels.CountExact(Color::Magenta))
        << "a draw consumed vertices outside its own range (actual slots "
        << DescribeLitSlots(pixels, layout, Color::Black) << ')';
}

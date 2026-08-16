// SPDX-License-Identifier: MS-PL
// REMED-GFX-201: the public multi-stream contract on the ORDINARY (non-instanced) draw routes.
//
// XNA 4.0's GraphicsDevice.SetVertexBuffers binds up to 16 VertexBufferBindings, and a vertex's
// elements may be split across several of them -- that is the entire reason the overload takes an
// array. FNA hands its driver one (buffer, declaration, stride, element offset, instance
// frequency) tuple per slot for EVERY draw route, ordinary and instanced alike
// (GraphicsDevice.PrepareVertexBindingArray -> FNA3D_ApplyVertexBufferBindings), and each driver
// converts `vertexOffset * stride` with THAT stream's own stride, binds it at ITS own input slot,
// and lets the native draw's start-vertex advance every stream by the same element count -- again
// each by its own stride. The reconciled CNA contract this file locks down is therefore:
//
//   every bound per-vertex stream reaches the renderer, not only binding 0
//   each stream keeps its OWN slot, declaration, stride and VertexOffset
//   VertexOffset is a vertex-ELEMENT offset, converted with that stream's own stride, exactly once
//   vertexStart advances EVERY per-vertex stream, each by that many of its own elements
//   baseVertex + the decoded index address EVERY per-vertex stream the same way
//   startIndex still selects index elements only, and no binding offset ever displaces it
//   a stream too short for the requested window is rejected even when stream 0 is long enough
//   the streams are independent: changing one binding's offset must not move the other stream
//
// Fixture geometry -- a three-axis oracle. The target is divided into `kSlotCount` equal-width
// vertical slots and `kBandCount` equal-height horizontal bands, exactly as REMED-GFX-200's
// OrdinaryDrawBindingOffsetTests.cpp does, and the two axes carry the same meaning:
//
//   * the COLUMN axis says WHICH records STREAM 0 (position) supplied
//   * the BAND axis says WHICH BYTES those records were
//   * the COLOUR axis says WHICH records STREAM 1 (colour) supplied
//
// The colour axis is what makes this file a multi-stream oracle rather than another offset oracle:
// it is produced by a buffer stream 0 does not contain a single byte of. Stream 0 is POSITION
// ONLY, 12 bytes per record; stream 1 is COLOUR ONLY, 4 bytes per record. Their concatenation is
// byte-for-byte CNA::Internal::Graphics::PositionColorStream, the packed 16-byte layout every
// renderer already recognizes, so the combined vertex needs no new shader anywhere -- but no
// rendered pixel of it can be produced from stream 0 alone, whose 12-byte stride is not a layout
// any renderer has.
//
// Every buffer begins with a three-record prefix decoy, so a binding offset of `kPrefixOffset` is
// exactly "skip the decoy" and each defect lights a different cell or paints a different colour:
//
//   * stream 1 never bound              -> stream 0's 12-byte stride matches no layout: nothing
//                                          renders at all, and the colour assertion cannot pass
//   * stream 1's VertexOffset dropped   -> the colour stream's prefix decoy colour (green)
//   * stream 0's VertexOffset dropped   -> the position prefix decoy's cell (slot 6, band 3)
//   * one stream's offset applied to    -> a colour one slot away from the requested one
//     both
//   * stream 0's stride used for        -> the colour records land 3x too far into the buffer,
//     stream 1                             which is past its end: no colour code matches
//   * a byte-for-element multiplication -> outside both buffers, so nothing lights
//   * baseVertex or startIndex applied  -> slot 6 band 0
//     twice
//   * a stale binding set after a       -> the previous leg's colour or slot
//     rebind
//
// Colour codes are the six {0,255} RGB corners, so they are 255 apart in at least one channel and
// a nearest-corner classifier is exact rather than tolerant. Black is the background and is never
// a code.
//
// Renderer scope. The RenderTarget2D group is the portable one: every renderer that rasterizes 3D
// triangles and implements RenderTarget2D::GetData, which is REMED-GFX-200's own oracle set.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"

// Lets CNA_RENDERER_IS name identities bare, matching the compile-time guard it replaced.
using namespace CNA::Testing::Renderers;

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

using CNA::GraphicsCapability;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::DynamicVertexBuffer;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::IndexElementSize;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::RenderTargetUsage;
using Microsoft::Xna::Framework::Graphics::SetDataOptions;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexBufferBinding;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

// The portable pixel oracle: rasterizes 3D triangles and implements RenderTarget2D::GetData.
// REMED-GFX-200's own set, which this file's geometry and readback path are taken from.
/// plan_runtimerenderer.md RTR-P9-5: the same renderer set, evaluated at runtime so this
/// describes the ACTIVE renderer rather than the build default.
[[nodiscard]] inline bool OrdinaryMultiStream()
{
    return CNA_RENDERER_IS(Bgfx, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, WebGPU, Vulkan, DirectX9, DirectX11, 
                            DirectX12, Software, SdlGpu);
}

namespace
{
    /// Geometry axis: equal-width vertical slots, one triangle each.
    constexpr int kSlotCount = 7;

    /// Provenance axis: equal-height horizontal bands.
    constexpr int kBandCount = 4;

    /// Vertices one slot's triangle owns.
    constexpr int kVerticesPerSlot = 3;

    /// Records the live geometry owns, one triangle per slot.
    constexpr int kMeshElementCount = kSlotCount * kVerticesPerSlot;

    /// Records a complete buffer owns: the prefix decoy plus the live geometry.
    constexpr int kBufferElementCount = kVerticesPerSlot + kMeshElementCount;

    /// The binding offset that means "skip the prefix decoy".
    constexpr int kPrefixOffset = kVerticesPerSlot;

    /// The slot the position stream's prefix decoy occupies.
    constexpr int kPrefixSlot = kSlotCount - 1;

    /// The band the position stream's prefix decoy occupies.
    constexpr int kPrefixBand = 3;

    /// The band the position stream's live geometry occupies.
    constexpr int kLiveBand = 0;

    /// The band a mid-sequence position rewrite moves the live geometry to.
    constexpr int kRewrittenBand = 1;

    /// The band an alternative position buffer occupies.
    constexpr int kAlternateBand = 2;

    /// Square render target, sized so both axes divide exactly: 224 = 7 * 32 = 4 * 56.
    constexpr int kTargetSize = 224;

    /// Stream 0's record: position only. Byte-for-byte the leading 12 bytes of
    /// CNA::Internal::Graphics::PositionColorStream.
    struct PositionRecord
    {
        float x, y, z;
    };
    static_assert(sizeof(PositionRecord) == 12);

    /// Stream 1's record: colour only. Byte-for-byte the trailing 4 bytes of the same stream, in
    /// the R8G8B8A8 memory order every renderer's `Color` VertexElementFormat reads.
    struct ColorRecord
    {
        std::uint8_t r, g, b, a;
    };
    static_assert(sizeof(ColorRecord) == 4);

    /// Stream 2's record for the three-stream case: one texture coordinate.
    struct TexCoordRecord
    {
        float u, v;
    };
    static_assert(sizeof(TexCoordRecord) == 8);

    constexpr int kPositionStride = static_cast<int>(sizeof(PositionRecord));
    constexpr int kColorStride    = static_cast<int>(sizeof(ColorRecord));
    constexpr int kTexCoordStride = static_cast<int>(sizeof(TexCoordRecord));

    /// Position-only declaration for stream 0. Its 12-byte stride deliberately matches no
    /// single-stream layout any renderer has, so a draw that reaches a renderer with stream 0 alone
    /// cannot accidentally render the expected picture.
    VertexDeclaration PositionOnlyDeclaration()
    {
        return VertexDeclaration(
            kPositionStride,
            {VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0)});
    }

    /// Colour-only declaration for stream 1. Its 4-byte stride is deliberately different from
    /// stream 0's, so a renderer that converts this stream's element offset with stream 0's stride
    /// lands three records too far in.
    VertexDeclaration ColorOnlyDeclaration()
    {
        return VertexDeclaration(
            kColorStride,
            {VertexElement(0, VertexElementFormat::Color, VertexElementUsage::Color, 0)});
    }

    /// Texture-coordinate-only declaration for stream 2, a third distinct stride.
    VertexDeclaration TexCoordOnlyDeclaration()
    {
        return VertexDeclaration(
            kTexCoordStride,
            {VertexElement(
                0, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0)});
    }

    /// The six {0,255} RGB corners. Every pair differs by 255 in at least one channel, so the
    /// nearest-corner classifier below is exact, not tolerant.
    enum class ColorCode
    {
        Background,   ///< the cleared target
        LiveA,        ///< live colour records for slots 0, 3, 6
        LiveB,        ///< live colour records for slots 1, 4
        LiveC,        ///< live colour records for slots 2, 5
        Prefix,       ///< the colour stream's own prefix decoy
        Rewritten,    ///< the colour stream after a mid-sequence rewrite
        Alternate,    ///< a second, separately bound colour buffer
        Unknown,      ///< something that is none of the above
    };

    ColorRecord RecordForCode(ColorCode code)
    {
        switch (code)
        {
        case ColorCode::LiveA:     return ColorRecord{255, 0, 0, 255};      // red
        case ColorCode::LiveB:     return ColorRecord{0, 255, 255, 255};    // cyan
        case ColorCode::LiveC:     return ColorRecord{255, 0, 255, 255};    // magenta
        case ColorCode::Prefix:    return ColorRecord{0, 255, 0, 255};      // green
        case ColorCode::Rewritten: return ColorRecord{255, 255, 0, 255};    // yellow
        case ColorCode::Alternate: return ColorRecord{0, 0, 255, 255};      // blue
        default:                   return ColorRecord{0, 0, 0, 255};
        }
    }

    /// The live colour a slot's records carry. Cycling three codes across seven slots means a
    /// displacement of one or two slots always changes the colour, so a colour-stream offset that
    /// is dropped, doubled or taken from the wrong stream is visible rather than merely possible.
    ColorCode LiveCodeForSlot(int slot)
    {
        switch (slot % 3)
        {
        case 0:  return ColorCode::LiveA;
        case 1:  return ColorCode::LiveB;
        default: return ColorCode::LiveC;
        }
    }

    const char* CodeName(ColorCode code)
    {
        switch (code)
        {
        case ColorCode::Background: return "background";
        case ColorCode::LiveA:      return "liveA(red)";
        case ColorCode::LiveB:      return "liveB(cyan)";
        case ColorCode::LiveC:      return "liveC(magenta)";
        case ColorCode::Prefix:     return "prefix(green)";
        case ColorCode::Rewritten:  return "rewritten(yellow)";
        case ColorCode::Alternate:  return "alternate(blue)";
        default:                    return "unknown";
        }
    }

    /// Classifies a rendered pixel by nearest {0,255} corner. A channel is "high" only above the
    /// midpoint, so no pair of codes can be confused; anything that is not a corner at all (an
    /// interpolated or resampled pixel) is Unknown and fails the assertion rather than passing it.
    ColorCode ClassifyPixel(const Color& pixel)
    {
        const int r = pixel.getRProperty();
        const int g = pixel.getGProperty();
        const int b = pixel.getBProperty();
        const bool nearCorner =
            (r <= 24 || r >= 231) && (g <= 24 || g >= 231) && (b <= 24 || b >= 231);
        if (!nearCorner)
            return ColorCode::Unknown;
        const bool hi_r = r >= 128;
        const bool hi_g = g >= 128;
        const bool hi_b = b >= 128;
        if (!hi_r && !hi_g && !hi_b) return ColorCode::Background;
        if (hi_r && !hi_g && !hi_b)  return ColorCode::LiveA;
        if (!hi_r && hi_g && hi_b)   return ColorCode::LiveB;
        if (hi_r && !hi_g && hi_b)   return ColorCode::LiveC;
        if (!hi_r && hi_g && !hi_b)  return ColorCode::Prefix;
        if (hi_r && hi_g && !hi_b)   return ColorCode::Rewritten;
        if (!hi_r && !hi_g && hi_b)  return ColorCode::Alternate;
        return ColorCode::Unknown;   // white: no code claims it
    }

    /// Target geometry: each slot's own centre column and each band's own centre row.
    struct GridLayout
    {
        int width = 0;
        int height = 0;

        [[nodiscard]] float ColumnCenterX(int slot) const
        {
            return static_cast<float>(width) *
                   (static_cast<float>(slot) + 0.5f) / static_cast<float>(kSlotCount);
        }

        [[nodiscard]] float ColumnBoundaryX(int slot) const
        {
            return static_cast<float>(width) *
                   static_cast<float>(slot) / static_cast<float>(kSlotCount);
        }

        [[nodiscard]] float RowCenterY(int band) const
        {
            return static_cast<float>(height) *
                   (static_cast<float>(band) + 0.5f) / static_cast<float>(kBandCount);
        }

        [[nodiscard]] float RowBoundaryY(int band) const
        {
            return static_cast<float>(height) *
                   static_cast<float>(band) / static_cast<float>(kBandCount);
        }

        [[nodiscard]] float HalfWidth() const
        {
            return 0.30f * static_cast<float>(width) / static_cast<float>(kSlotCount);
        }

        [[nodiscard]] float HalfHeight() const
        {
            return 0.30f * static_cast<float>(height) / static_cast<float>(kBandCount);
        }
    };

    /// Identity World/View/Projection throughout, so a vertex position IS its clip-space position
    /// and the viewport transform maps it to the requested target pixel.
    PositionRecord PositionAtPixel(const GridLayout& layout, float pixelX, float pixelY)
    {
        return PositionRecord{
            (2.0f * pixelX / static_cast<float>(layout.width)) - 1.0f,
            1.0f - (2.0f * pixelY / static_cast<float>(layout.height)),
            0.5f};
    }

    std::array<PositionRecord, kVerticesPerSlot> SlotTriangle(
        const GridLayout& layout, int slot, int band)
    {
        const float halfWidth = layout.HalfWidth();
        const float halfHeight = layout.HalfHeight();
        const float centerX = layout.ColumnCenterX(slot);
        const float centerY = layout.RowCenterY(band);
        return {
            PositionAtPixel(layout, centerX - halfWidth, centerY + halfHeight),
            PositionAtPixel(layout, centerX + halfWidth, centerY + halfHeight),
            PositionAtPixel(layout, centerX, centerY - halfHeight),
        };
    }

    /// A complete position buffer: the prefix decoy (slot `kPrefixSlot`, band `kPrefixBand`)
    /// followed by one triangle per slot authored in @p band.
    std::vector<PositionRecord> BuildPositionStream(const GridLayout& layout, int band)
    {
        std::vector<PositionRecord> records;
        records.reserve(static_cast<std::size_t>(kBufferElementCount));
        const auto prefix = SlotTriangle(layout, kPrefixSlot, kPrefixBand);
        records.insert(records.end(), prefix.begin(), prefix.end());
        for (int slot = 0; slot < kSlotCount; ++slot)
        {
            const auto triangle = SlotTriangle(layout, slot, band);
            records.insert(records.end(), triangle.begin(), triangle.end());
        }
        return records;
    }

    /// A complete colour buffer: `kPrefixOffset` records of @p prefixCode followed by one flat
    /// triangle's worth of `LiveCodeForSlot(slot)` per slot. When @p liveOverride is not Unknown
    /// every live record carries that code instead, which is how the rewritten and alternative
    /// buffers stay one colour.
    std::vector<ColorRecord> BuildColorStream(
        ColorCode prefixCode = ColorCode::Prefix,
        ColorCode liveOverride = ColorCode::Unknown)
    {
        std::vector<ColorRecord> records;
        records.reserve(static_cast<std::size_t>(kBufferElementCount));
        for (int i = 0; i < kPrefixOffset; ++i)
            records.push_back(RecordForCode(prefixCode));
        for (int slot = 0; slot < kSlotCount; ++slot)
        {
            const ColorCode code =
                liveOverride == ColorCode::Unknown ? LiveCodeForSlot(slot) : liveOverride;
            for (int i = 0; i < kVerticesPerSlot; ++i)
                records.push_back(RecordForCode(code));
        }
        return records;
    }

    /// The identity index buffer over the WHOLE buffer, prefix decoy included: index element `i`
    /// addresses stream record `i`. Every requested range is therefore expressed purely through
    /// the public startIndex/baseVertex/VertexOffset triple.
    std::vector<std::uint16_t> BuildIdentityIndices16()
    {
        std::vector<std::uint16_t> indices;
        indices.reserve(static_cast<std::size_t>(kBufferElementCount));
        for (int i = 0; i < kBufferElementCount; ++i)
            indices.push_back(static_cast<std::uint16_t>(i));
        return indices;
    }

    std::vector<std::uint32_t> BuildIdentityIndices32()
    {
        std::vector<std::uint32_t> indices;
        indices.reserve(static_cast<std::size_t>(kBufferElementCount));
        for (int i = 0; i < kBufferElementCount; ++i)
            indices.push_back(static_cast<std::uint32_t>(i));
        return indices;
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

        [[nodiscard]] int CountLit() const
        {
            int total = 0;
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                    if (ClassifyPixel(At(x, y)) != ColorCode::Background) ++total;
            return total;
        }
    };

    /// Lit pixels inside the exclusive rectangle of one (slot, band) cell.
    int CountLitInCell(const FrameSnapshot& snapshot, const GridLayout& layout, int slot, int band)
    {
        const int x0 = static_cast<int>(layout.ColumnBoundaryX(slot) + 0.999f);
        const int x1 = static_cast<int>(layout.ColumnBoundaryX(slot + 1));
        const int y0 = static_cast<int>(layout.RowBoundaryY(band) + 0.999f);
        const int y1 = static_cast<int>(layout.RowBoundaryY(band + 1));
        int total = 0;
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x)
                if (ClassifyPixel(snapshot.At(x, y)) != ColorCode::Background) ++total;
        return total;
    }

    /// The dominant colour code inside one cell, counting only lit pixels. An interpolated or
    /// resampled edge pixel classifies Unknown and is counted as such, so a cell whose interior is
    /// not one flat code cannot report a code.
    ColorCode DominantCodeInCell(
        const FrameSnapshot& snapshot, const GridLayout& layout, int slot, int band, int& litCount)
    {
        const int x0 = static_cast<int>(layout.ColumnBoundaryX(slot) + 0.999f);
        const int x1 = static_cast<int>(layout.ColumnBoundaryX(slot + 1));
        const int y0 = static_cast<int>(layout.RowBoundaryY(band) + 0.999f);
        const int y1 = static_cast<int>(layout.RowBoundaryY(band + 1));
        std::array<int, 8> counts{};
        litCount = 0;
        for (int y = y0; y < y1; ++y)
        {
            for (int x = x0; x < x1; ++x)
            {
                const ColorCode code = ClassifyPixel(snapshot.At(x, y));
                if (code == ColorCode::Background)
                    continue;
                ++litCount;
                ++counts[static_cast<std::size_t>(code)];
            }
        }
        int best = 0;
        std::size_t bestIndex = static_cast<std::size_t>(ColorCode::Unknown);
        for (std::size_t i = 0; i < counts.size(); ++i)
        {
            if (counts[i] > best)
            {
                best = counts[i];
                bestIndex = i;
            }
        }
        return static_cast<ColorCode>(bestIndex);
    }

    /// A compact `band: slot0 slot1 ...` map of lit pixel counts and their colour codes. Every
    /// failure in this file is about which records which stream supplied, so every failure prints
    /// both axes at once.
    std::string DescribeFrame(const FrameSnapshot& snapshot, const GridLayout& layout)
    {
        std::string map = "\n  lit cells (band: slot0..slot6, count/code):";
        for (int band = 0; band < kBandCount; ++band)
        {
            map += "\n    band " + std::to_string(band) + ':';
            for (int slot = 0; slot < kSlotCount; ++slot)
            {
                int lit = 0;
                const ColorCode code = DominantCodeInCell(snapshot, layout, slot, band, lit);
                map += ' ';
                map += std::to_string(lit);
                if (lit > 0)
                {
                    map += '/';
                    map += CodeName(code);
                }
            }
        }
        map += "\n    total lit: " + std::to_string(snapshot.CountLit());
        return map;
    }

    class OrdinaryDrawMultiStreamTest : public ::testing::Test
    {
    protected:
        GraphicsDevice device;

        // GTEST_SKIP() only unwinds the function it is called from; called from an ordinary
        // member function like RequireOrdinaryRendering() below it cannot skip the test body that
        // invokes it. SetUp() is where GoogleTest itself checks for a skip, so the capability gate
        // has to run here too -- RequireOrdinaryRendering() keeps its own copy for the state-setup
        // calls that follow it, which only run once SetUp() has already let the test proceed.
        void SetUp() override
        {
            if (!device.SupportsCapability(GraphicsCapability::ThreeD))
                GTEST_SKIP() << "Renderer explicitly does not support 3D rendering";
        }

        void RequireOrdinaryRendering()
        {
            if (!device.SupportsCapability(GraphicsCapability::ThreeD))
                GTEST_SKIP() << "Renderer explicitly does not support 3D rendering";
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::Opaque);
            device.setScissorRectangleProperty(Rectangle(0, 0, kTargetSize, kTargetSize));
        }

        [[nodiscard]] static GridLayout TargetLayout()
        {
            return GridLayout{kTargetSize, kTargetSize};
        }

        [[nodiscard]] RenderTarget2D MakeTarget()
        {
            return RenderTarget2D(
                device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
                DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        }

        static FrameSnapshot CaptureTarget(RenderTarget2D& target)
        {
            FrameSnapshot snapshot;
            snapshot.width = kTargetSize;
            snapshot.height = kTargetSize;
            snapshot.pixels.assign(
                static_cast<std::size_t>(kTargetSize) * static_cast<std::size_t>(kTargetSize),
                Color::Transparent);
            const Rectangle region(0, 0, kTargetSize, kTargetSize);
            target.GetData(
                0, &region, snapshot.pixels.data(), 0,
                static_cast<int>(snapshot.pixels.size()));
            return snapshot;
        }

        /// The combined vertex is POSITION + COLOR, so the stock path must colour from the vertex
        /// stream: DiffuseColor is white and lighting/texturing are off, leaving the vertex colour
        /// -- which only stream 1 can supply -- as the entire output.
        static void ApplyMeshEffect(BasicEffect& effect)
        {
            effect.VertexColorEnabled = true;
            effect.setLightingEnabledProperty(false);
            effect.setTextureEnabledProperty(false);
            effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            effect.setAlphaProperty(1.0f);
            effect.Apply();
        }

        /// The whole oracle in one assertion: the requested cell rendered in the requested colour,
        /// and NOTHING else in the frame did. Comparing the cell's own lit count against the frame
        /// total also rejects geometry that landed between cells.
        static void ExpectOnlyCellLitWithCode(
            const FrameSnapshot& snapshot, const GridLayout& layout,
            int slot, int band, ColorCode expected, const char* label)
        {
            int inCell = 0;
            const ColorCode actual = DominantCodeInCell(snapshot, layout, slot, band, inCell);
            const int total = snapshot.CountLit();
            EXPECT_GT(inCell, 0)
                << label << ": nothing rendered in slot " << slot << " band " << band
                << DescribeFrame(snapshot, layout);
            EXPECT_EQ(inCell, total)
                << label << ": geometry rendered outside slot " << slot << " band " << band
                << DescribeFrame(snapshot, layout);
            EXPECT_EQ(static_cast<int>(actual), static_cast<int>(expected))
                << label << ": slot " << slot << " band " << band << " carried "
                << CodeName(actual) << ", expected " << CodeName(expected)
                << " -- the colour axis is supplied ONLY by binding slot 1"
                << DescribeFrame(snapshot, layout);
        }
    };
}

/// REMED-GFX-201: a DECLARED boundary, not a silent skip. A renderer that has not yet been taught
/// to re-slot its stride-derived input elements across several bindings reports the capability as
/// false and is rejected deterministically by GraphicsDevice;
/// UnsupportedRendererRejectsMultiStreamDeterministically below asserts exactly that on every
/// renderer and flips to the positive assertion the moment one claims the capability, so this skip
/// cannot outlive the gap it describes. A macro rather than a helper because GTEST_SKIP() returns
/// from the function it is written in -- inside a helper it would mark the test skipped and then
/// let it run on anyway.
#define CNA_REQUIRE_MULTI_STREAM_INPUT()                                                     \
    do {                                                                                     \
        if (!device.SupportsCapability(GraphicsCapability::MultiStreamVertexInput))           \
        {                                                                                    \
            GTEST_SKIP()                                                                     \
                << "Renderer reports GraphicsCapability::MultiStreamVertexInput = false: "     \
                   "ordinary multi-stream vertex input is not implemented on this renderer "   \
                   "(REMED-GFX-201). The draw is rejected with System::NotSupportedException "\
                   "rather than rendered from stream 0 alone -- see "                         \
                   "UnsupportedRendererRejectsMultiStreamDeterministically.";                  \
        }                                                                                    \
    } while (false)


// ---------------------------------------------------------------------------
// Coverage item 1: non-indexed, BOTH offsets zero. The baseline that proves the split layout
// renders at all -- and, because it asserts the prefix decoy's own cell and the colour stream's
// own prefix code, it fails if the fixture ever stops rendering rather than silently passing.
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawMultiStreamTest, NonIndexedBothOffsetsZeroRendersBothPrefixDecoys)
{
    // plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryMultiStream())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireOrdinaryRendering();
    CNA_REQUIRE_MULTI_STREAM_INPUT();

    const GridLayout layout = TargetLayout();
    const std::vector<PositionRecord> positions = BuildPositionStream(layout, kLiveBand);
    const std::vector<ColorRecord> colors = BuildColorStream();

    VertexBuffer positionBuffer(
        device, PositionOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    positionBuffer.SetDataRaw(positions.data(), kBufferElementCount, kPositionStride);
    VertexBuffer colorBuffer(
        device, ColorOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    colorBuffer.SetDataRaw(colors.data(), kBufferElementCount, kColorStride);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    device.SetVertexBuffers({
        VertexBufferBinding(&positionBuffer, 0, 0),
        VertexBufferBinding(&colorBuffer, 0, 0),
    });
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
    device.SetRenderTarget(nullptr);

    const FrameSnapshot snapshot = CaptureTarget(target);
    ExpectOnlyCellLitWithCode(
        snapshot, layout, kPrefixSlot, kPrefixBand, ColorCode::Prefix,
        "both offsets zero consume record 0 of both streams");
}

// ---------------------------------------------------------------------------
// Coverage item 2: non-indexed, STREAM 0's offset nonzero and stream 1's zero. The two offsets are
// independent, so the position stream skips its decoy while the colour stream does not: the live
// geometry's cell painted in the COLOUR STREAM'S PREFIX code. A renderer that applies one binding's
// offset to both streams paints liveA here instead.
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawMultiStreamTest, NonIndexedStream0OffsetOnlyLeavesStream1OnItsDecoy)
{
    // plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryMultiStream())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireOrdinaryRendering();
    CNA_REQUIRE_MULTI_STREAM_INPUT();

    const GridLayout layout = TargetLayout();
    const std::vector<PositionRecord> positions = BuildPositionStream(layout, kLiveBand);
    const std::vector<ColorRecord> colors = BuildColorStream();

    VertexBuffer positionBuffer(
        device, PositionOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    positionBuffer.SetDataRaw(positions.data(), kBufferElementCount, kPositionStride);
    VertexBuffer colorBuffer(
        device, ColorOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    colorBuffer.SetDataRaw(colors.data(), kBufferElementCount, kColorStride);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    device.SetVertexBuffers({
        VertexBufferBinding(&positionBuffer, kPrefixOffset, 0),
        VertexBufferBinding(&colorBuffer, 0, 0),
    });
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
    device.SetRenderTarget(nullptr);

    const FrameSnapshot snapshot = CaptureTarget(target);
    ExpectOnlyCellLitWithCode(
        snapshot, layout, 0, kLiveBand, ColorCode::Prefix,
        "stream 0's offset must not move stream 1");
}

// ---------------------------------------------------------------------------
// Coverage item 3: non-indexed, STREAM 1's offset nonzero and stream 0's zero -- the mirror image.
// The position stream stays on its decoy (slot 6, band 3) while the colour stream supplies slot
// 0's live colour. Only a renderer that binds stream 1 with its OWN offset can produce this frame.
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawMultiStreamTest, NonIndexedStream1OffsetOnlyLeavesStream0OnItsDecoy)
{
    // plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryMultiStream())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireOrdinaryRendering();
    CNA_REQUIRE_MULTI_STREAM_INPUT();

    const GridLayout layout = TargetLayout();
    const std::vector<PositionRecord> positions = BuildPositionStream(layout, kLiveBand);
    const std::vector<ColorRecord> colors = BuildColorStream();

    VertexBuffer positionBuffer(
        device, PositionOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    positionBuffer.SetDataRaw(positions.data(), kBufferElementCount, kPositionStride);
    VertexBuffer colorBuffer(
        device, ColorOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    colorBuffer.SetDataRaw(colors.data(), kBufferElementCount, kColorStride);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    device.SetVertexBuffers({
        VertexBufferBinding(&positionBuffer, 0, 0),
        VertexBufferBinding(&colorBuffer, kPrefixOffset, 0),
    });
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
    device.SetRenderTarget(nullptr);

    const FrameSnapshot snapshot = CaptureTarget(target);
    ExpectOnlyCellLitWithCode(
        snapshot, layout, kPrefixSlot, kPrefixBand, LiveCodeForSlot(0),
        "stream 1's offset must not move stream 0");
}

// ---------------------------------------------------------------------------
// Coverage item 4: non-indexed, BOTH offsets nonzero and DIFFERENT. Stream 0 skips its decoy;
// stream 1 skips its decoy AND one slot, so the requested cell is painted with the NEXT slot's
// colour. Reproducing this needs both offsets carried separately, each multiplied by its own
// stride: 3 * 12 bytes and 6 * 4 bytes are not related by any single factor.
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawMultiStreamTest, NonIndexedDifferentNonzeroOffsetsSelectDifferentRecords)
{
    // plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryMultiStream())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireOrdinaryRendering();
    CNA_REQUIRE_MULTI_STREAM_INPUT();

    const GridLayout layout = TargetLayout();
    const std::vector<PositionRecord> positions = BuildPositionStream(layout, kLiveBand);
    const std::vector<ColorRecord> colors = BuildColorStream();

    VertexBuffer positionBuffer(
        device, PositionOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    positionBuffer.SetDataRaw(positions.data(), kBufferElementCount, kPositionStride);
    VertexBuffer colorBuffer(
        device, ColorOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    colorBuffer.SetDataRaw(colors.data(), kBufferElementCount, kColorStride);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    device.SetVertexBuffers({
        VertexBufferBinding(&positionBuffer, kPrefixOffset, 0),
        VertexBufferBinding(&colorBuffer, kPrefixOffset + kVerticesPerSlot, 0),
    });
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
    device.SetRenderTarget(nullptr);

    const FrameSnapshot snapshot = CaptureTarget(target);
    ExpectOnlyCellLitWithCode(
        snapshot, layout, 0, kLiveBand, LiveCodeForSlot(1),
        "each stream converts its own offset with its own stride");
}

// ---------------------------------------------------------------------------
// Coverage item 5: non-indexed, nonzero vertexStart PLUS both offsets. vertexStart advances every
// per-vertex stream by the same element count -- each by its OWN stride -- so it selects the same
// slot in both streams and the requested cell carries its own live colour.
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawMultiStreamTest, NonIndexedVertexStartAdvancesEveryStream)
{
    // plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryMultiStream())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireOrdinaryRendering();
    CNA_REQUIRE_MULTI_STREAM_INPUT();

    const GridLayout layout = TargetLayout();
    const std::vector<PositionRecord> positions = BuildPositionStream(layout, kLiveBand);
    const std::vector<ColorRecord> colors = BuildColorStream();

    VertexBuffer positionBuffer(
        device, PositionOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    positionBuffer.SetDataRaw(positions.data(), kBufferElementCount, kPositionStride);
    VertexBuffer colorBuffer(
        device, ColorOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    colorBuffer.SetDataRaw(colors.data(), kBufferElementCount, kColorStride);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    constexpr int kRequestedSlot = 4;
    device.SetVertexBuffers({
        VertexBufferBinding(&positionBuffer, kPrefixOffset, 0),
        VertexBufferBinding(&colorBuffer, kPrefixOffset, 0),
    });
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    device.DrawPrimitives(
        PrimitiveType::TriangleList, kRequestedSlot * kVerticesPerSlot, 1);
    device.SetRenderTarget(nullptr);

    const FrameSnapshot snapshot = CaptureTarget(target);
    ExpectOnlyCellLitWithCode(
        snapshot, layout, kRequestedSlot, kLiveBand, LiveCodeForSlot(kRequestedSlot),
        "vertexStart advances every stream by its own elements");
}

// ---------------------------------------------------------------------------
// Coverage item 6: indexed 16-bit, both offsets zero. The identity index buffer means index
// element `i` addresses record `i` of EVERY stream, so this is the indexed counterpart of item 1.
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawMultiStreamTest, Indexed16BothOffsetsZeroRendersBothPrefixDecoys)
{
    // plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryMultiStream())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireOrdinaryRendering();
    CNA_REQUIRE_MULTI_STREAM_INPUT();

    const GridLayout layout = TargetLayout();
    const std::vector<PositionRecord> positions = BuildPositionStream(layout, kLiveBand);
    const std::vector<ColorRecord> colors = BuildColorStream();
    const std::vector<std::uint16_t> indices = BuildIdentityIndices16();

    VertexBuffer positionBuffer(
        device, PositionOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    positionBuffer.SetDataRaw(positions.data(), kBufferElementCount, kPositionStride);
    VertexBuffer colorBuffer(
        device, ColorOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    colorBuffer.SetDataRaw(colors.data(), kBufferElementCount, kColorStride);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kBufferElementCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kBufferElementCount);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    device.SetVertexBuffers({
        VertexBufferBinding(&positionBuffer, 0, 0),
        VertexBufferBinding(&colorBuffer, 0, 0),
    });
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, 0, 0, kBufferElementCount, 0, 1);
    device.SetRenderTarget(nullptr);

    const FrameSnapshot snapshot = CaptureTarget(target);
    ExpectOnlyCellLitWithCode(
        snapshot, layout, kPrefixSlot, kPrefixBand, ColorCode::Prefix,
        "indexed, both offsets zero");
}

// ---------------------------------------------------------------------------
// Coverage item 7: indexed 16-bit, both offsets nonzero and different. Same independence proof as
// item 4, through the indexed route: the offset displaces every decoded index of its OWN stream.
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawMultiStreamTest, Indexed16DifferentNonzeroOffsetsSelectDifferentRecords)
{
    // plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryMultiStream())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireOrdinaryRendering();
    CNA_REQUIRE_MULTI_STREAM_INPUT();

    const GridLayout layout = TargetLayout();
    const std::vector<PositionRecord> positions = BuildPositionStream(layout, kLiveBand);
    const std::vector<ColorRecord> colors = BuildColorStream();
    const std::vector<std::uint16_t> indices = BuildIdentityIndices16();

    VertexBuffer positionBuffer(
        device, PositionOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    positionBuffer.SetDataRaw(positions.data(), kBufferElementCount, kPositionStride);
    VertexBuffer colorBuffer(
        device, ColorOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    colorBuffer.SetDataRaw(colors.data(), kBufferElementCount, kColorStride);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kBufferElementCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kBufferElementCount);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    device.SetVertexBuffers({
        VertexBufferBinding(&positionBuffer, kPrefixOffset, 0),
        VertexBufferBinding(&colorBuffer, kPrefixOffset + kVerticesPerSlot, 0),
    });
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    // The declared window is the three records this draw actually references. Declaring the whole
    // mesh here would be untruthful for the colour stream, whose own offset is one slot further
    // in -- and REMED-GFX-201's per-stream range gate rejects exactly that.
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, 0, 0, kVerticesPerSlot, 0, 1);
    device.SetRenderTarget(nullptr);

    const FrameSnapshot snapshot = CaptureTarget(target);
    ExpectOnlyCellLitWithCode(
        snapshot, layout, 0, kLiveBand, LiveCodeForSlot(1),
        "indexed, each stream converts its own offset with its own stride");
}

// ---------------------------------------------------------------------------
// Coverage item 8: indexed 16-bit, nonzero startIndex AND nonzero baseVertex, with both offsets
// nonzero. startIndex selects index elements only; baseVertex advances every per-vertex stream.
// Applying either twice lands on slot 6 band 0, and applying baseVertex to only stream 0 paints
// the wrong colour -- three distinct, separately visible failures.
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawMultiStreamTest, Indexed16StartIndexAndBaseVertexApplyToEveryStreamOnce)
{
    // plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryMultiStream())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireOrdinaryRendering();
    CNA_REQUIRE_MULTI_STREAM_INPUT();

    const GridLayout layout = TargetLayout();
    const std::vector<PositionRecord> positions = BuildPositionStream(layout, kLiveBand);
    const std::vector<ColorRecord> colors = BuildColorStream();
    const std::vector<std::uint16_t> indices = BuildIdentityIndices16();

    VertexBuffer positionBuffer(
        device, PositionOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    positionBuffer.SetDataRaw(positions.data(), kBufferElementCount, kPositionStride);
    VertexBuffer colorBuffer(
        device, ColorOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    colorBuffer.SetDataRaw(colors.data(), kBufferElementCount, kColorStride);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kBufferElementCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kBufferElementCount);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    // slot 1 through startIndex, then slot 2 through baseVertex: the requested slot is 3.
    constexpr int kStartIndex = 1 * kVerticesPerSlot;
    constexpr int kBaseVertex = 2 * kVerticesPerSlot;
    constexpr int kRequestedSlot = 3;
    device.SetVertexBuffers({
        VertexBufferBinding(&positionBuffer, kPrefixOffset, 0),
        VertexBufferBinding(&colorBuffer, kPrefixOffset, 0),
    });
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, kBaseVertex, 0,
        kMeshElementCount - kBaseVertex, kStartIndex, 1);
    device.SetRenderTarget(nullptr);

    const FrameSnapshot snapshot = CaptureTarget(target);
    ExpectOnlyCellLitWithCode(
        snapshot, layout, kRequestedSlot, kLiveBand, LiveCodeForSlot(kRequestedSlot),
        "startIndex is index-only; baseVertex advances every stream once");
}

// ---------------------------------------------------------------------------
// Coverage item 9: the 32-bit index equivalent of item 8. A renderer that computes its index-buffer
// byte offset with the wrong element size lands on a different slot.
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawMultiStreamTest, Indexed32StartIndexAndBaseVertexApplyToEveryStreamOnce)
{
    // plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryMultiStream())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireOrdinaryRendering();
    CNA_REQUIRE_MULTI_STREAM_INPUT();

    const GridLayout layout = TargetLayout();
    const std::vector<PositionRecord> positions = BuildPositionStream(layout, kLiveBand);
    const std::vector<ColorRecord> colors = BuildColorStream();
    const std::vector<std::uint32_t> indices = BuildIdentityIndices32();

    VertexBuffer positionBuffer(
        device, PositionOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    positionBuffer.SetDataRaw(positions.data(), kBufferElementCount, kPositionStride);
    VertexBuffer colorBuffer(
        device, ColorOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    colorBuffer.SetDataRaw(colors.data(), kBufferElementCount, kColorStride);
    IndexBuffer indexBuffer(
        device, IndexElementSize::ThirtyTwoBits, kBufferElementCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kBufferElementCount);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    constexpr int kStartIndex = 1 * kVerticesPerSlot;
    constexpr int kBaseVertex = 2 * kVerticesPerSlot;
    constexpr int kRequestedSlot = 3;
    device.SetVertexBuffers({
        VertexBufferBinding(&positionBuffer, kPrefixOffset, 0),
        VertexBufferBinding(&colorBuffer, kPrefixOffset, 0),
    });
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList, kBaseVertex, 0,
        kMeshElementCount - kBaseVertex, kStartIndex, 1);
    device.SetRenderTarget(nullptr);

    const FrameSnapshot snapshot = CaptureTarget(target);
    ExpectOnlyCellLitWithCode(
        snapshot, layout, kRequestedSlot, kLiveBand, LiveCodeForSlot(kRequestedSlot),
        "32-bit indices, startIndex and baseVertex");
}

// ---------------------------------------------------------------------------
// Coverage item 11: streams with DIFFERENT vertex counts and a range valid in both. The colour
// stream is deliberately shorter than the position stream, which is legal as long as the requested
// window fits it -- a renderer that sizes any native binding from stream 0's count reads past this
// buffer's end and a validation layer reports it.
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawMultiStreamTest, StreamsWithDifferentVertexCountsRenderTheValidRange)
{
    // plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryMultiStream())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireOrdinaryRendering();
    CNA_REQUIRE_MULTI_STREAM_INPUT();

    const GridLayout layout = TargetLayout();
    const std::vector<PositionRecord> positions = BuildPositionStream(layout, kLiveBand);
    std::vector<ColorRecord> colors = BuildColorStream();
    // Exactly enough for the prefix decoy plus slots 0 and 1.
    constexpr int kShortColorCount = kPrefixOffset + 2 * kVerticesPerSlot;
    colors.resize(static_cast<std::size_t>(kShortColorCount));

    VertexBuffer positionBuffer(
        device, PositionOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    positionBuffer.SetDataRaw(positions.data(), kBufferElementCount, kPositionStride);
    VertexBuffer colorBuffer(
        device, ColorOnlyDeclaration(), kShortColorCount, BufferUsage::None);
    colorBuffer.SetDataRaw(colors.data(), kShortColorCount, kColorStride);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    device.SetVertexBuffers({
        VertexBufferBinding(&positionBuffer, kPrefixOffset, 0),
        VertexBufferBinding(&colorBuffer, kPrefixOffset, 0),
    });
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    device.DrawPrimitives(PrimitiveType::TriangleList, kVerticesPerSlot, 1);
    device.SetRenderTarget(nullptr);

    const FrameSnapshot snapshot = CaptureTarget(target);
    ExpectOnlyCellLitWithCode(
        snapshot, layout, 1, kLiveBand, LiveCodeForSlot(1),
        "a shorter secondary stream is legal while the range fits it");
}

// ---------------------------------------------------------------------------
// Coverage item 12: an invalid range in STREAM 0 still throws, exactly as REMED-GFX-113 defined.
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawMultiStreamTest, RangeLeavingStream0IsRejected)
{
    // plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryMultiStream())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireOrdinaryRendering();
     const GridLayout layout = TargetLayout();
    const std::vector<PositionRecord> positions = BuildPositionStream(layout, kLiveBand);
    const std::vector<ColorRecord> colors = BuildColorStream();

    VertexBuffer positionBuffer(
        device, PositionOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    positionBuffer.SetDataRaw(positions.data(), kBufferElementCount, kPositionStride);
    VertexBuffer colorBuffer(
        device, ColorOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    colorBuffer.SetDataRaw(colors.data(), kBufferElementCount, kColorStride);

    BasicEffect effect(device);
    device.SetVertexBuffers({
        VertexBufferBinding(&positionBuffer, kPrefixOffset, 0),
        VertexBufferBinding(&colorBuffer, 0, 0),
    });
    ApplyMeshEffect(effect);

    EXPECT_THROW(
        device.DrawPrimitives(
            PrimitiveType::TriangleList, kMeshElementCount - kVerticesPerSlot + 1, 1),
        System::ArgumentOutOfRangeException);
}

// ---------------------------------------------------------------------------
// Coverage item 13: an invalid range in STREAM 1 ONLY. This is the validation case that cannot be
// expressed at all without per-stream ranges: stream 0 is long enough for the whole request, and
// only the secondary stream is short. A gate that validates stream 0 alone lets the draw through
// and the renderer reads past the colour buffer's end.
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawMultiStreamTest, RangeLeavingOnlyStream1IsRejected)
{
    // plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryMultiStream())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireOrdinaryRendering();
     const GridLayout layout = TargetLayout();
    const std::vector<PositionRecord> positions = BuildPositionStream(layout, kLiveBand);
    std::vector<ColorRecord> colors = BuildColorStream();
    constexpr int kShortColorCount = kPrefixOffset + 2 * kVerticesPerSlot;
    colors.resize(static_cast<std::size_t>(kShortColorCount));

    VertexBuffer positionBuffer(
        device, PositionOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    positionBuffer.SetDataRaw(positions.data(), kBufferElementCount, kPositionStride);
    VertexBuffer colorBuffer(
        device, ColorOnlyDeclaration(), kShortColorCount, BufferUsage::None);
    colorBuffer.SetDataRaw(colors.data(), kShortColorCount, kColorStride);

    BasicEffect effect(device);
    device.SetVertexBuffers({
        VertexBufferBinding(&positionBuffer, kPrefixOffset, 0),
        VertexBufferBinding(&colorBuffer, kPrefixOffset, 0),
    });
    ApplyMeshEffect(effect);

    // Slot 2 exists in the position stream and does not exist in the colour stream.
    EXPECT_THROW(
        device.DrawPrimitives(PrimitiveType::TriangleList, 2 * kVerticesPerSlot, 1),
        System::ArgumentOutOfRangeException);
}

// ---------------------------------------------------------------------------
// Coverage item 13b: the indexed counterpart -- the declared window leaves the secondary stream
// only. REMED-GFX-113's gate is per-stream on both routes or on neither.
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawMultiStreamTest, IndexedRangeLeavingOnlyStream1IsRejected)
{
    // plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryMultiStream())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireOrdinaryRendering();
     const GridLayout layout = TargetLayout();
    const std::vector<PositionRecord> positions = BuildPositionStream(layout, kLiveBand);
    std::vector<ColorRecord> colors = BuildColorStream();
    constexpr int kShortColorCount = kPrefixOffset + 2 * kVerticesPerSlot;
    colors.resize(static_cast<std::size_t>(kShortColorCount));
    const std::vector<std::uint16_t> indices = BuildIdentityIndices16();

    VertexBuffer positionBuffer(
        device, PositionOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    positionBuffer.SetDataRaw(positions.data(), kBufferElementCount, kPositionStride);
    VertexBuffer colorBuffer(
        device, ColorOnlyDeclaration(), kShortColorCount, BufferUsage::None);
    colorBuffer.SetDataRaw(colors.data(), kShortColorCount, kColorStride);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kBufferElementCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kBufferElementCount);

    BasicEffect effect(device);
    device.SetVertexBuffers({
        VertexBufferBinding(&positionBuffer, kPrefixOffset, 0),
        VertexBufferBinding(&colorBuffer, kPrefixOffset, 0),
    });
    device.SetIndexBuffer(&indexBuffer);
    ApplyMeshEffect(effect);

    EXPECT_THROW(
        device.DrawIndexedPrimitives(
            PrimitiveType::TriangleList, 2 * kVerticesPerSlot, 0, kVerticesPerSlot, 0, 1),
        System::ArgumentOutOfRangeException);
}

// ---------------------------------------------------------------------------
// Coverage items 14, 15 and 19: a state change affecting ONLY stream 1, a return to the previous
// binding set, and three frames in one target. Each leg must carry its own complete binding set:
// a renderer that caches an input layout or a native binding across the change repeats a colour.
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawMultiStreamTest, ReplacingOnlyStream1AndReturningKeepsEachLegsOwnBindings)
{
    // plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryMultiStream())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireOrdinaryRendering();
    CNA_REQUIRE_MULTI_STREAM_INPUT();

    const GridLayout layout = TargetLayout();
    const std::vector<PositionRecord> positions = BuildPositionStream(layout, kLiveBand);
    const std::vector<ColorRecord> colors = BuildColorStream();
    const std::vector<ColorRecord> alternateColors =
        BuildColorStream(ColorCode::Prefix, ColorCode::Alternate);

    VertexBuffer positionBuffer(
        device, PositionOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    positionBuffer.SetDataRaw(positions.data(), kBufferElementCount, kPositionStride);
    VertexBuffer colorBuffer(
        device, ColorOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    colorBuffer.SetDataRaw(colors.data(), kBufferElementCount, kColorStride);
    VertexBuffer alternateColorBuffer(
        device, ColorOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    alternateColorBuffer.SetDataRaw(
        alternateColors.data(), kBufferElementCount, kColorStride);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    const auto drawSlot = [&](VertexBuffer& colours, int slot) {
        device.SetVertexBuffers({
            VertexBufferBinding(&positionBuffer, kPrefixOffset, 0),
            VertexBufferBinding(&colours, kPrefixOffset, 0),
        });
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        ApplyMeshEffect(effect);
        device.DrawPrimitives(PrimitiveType::TriangleList, slot * kVerticesPerSlot, 1);
        device.SetRenderTarget(nullptr);
        return CaptureTarget(target);
    };

    const FrameSnapshot first = drawSlot(colorBuffer, 2);
    ExpectOnlyCellLitWithCode(
        first, layout, 2, kLiveBand, LiveCodeForSlot(2), "A: original colour stream");

    const FrameSnapshot second = drawSlot(alternateColorBuffer, 2);
    ExpectOnlyCellLitWithCode(
        second, layout, 2, kLiveBand, ColorCode::Alternate,
        "B: only binding slot 1 changed");

    const FrameSnapshot third = drawSlot(colorBuffer, 2);
    ExpectOnlyCellLitWithCode(
        third, layout, 2, kLiveBand, LiveCodeForSlot(2),
        "A again: returning to a previous binding set must not reuse B's stream");
}

// ---------------------------------------------------------------------------
// Coverage item 16: interleaving a SINGLE-stream draw between two multi-stream draws. The
// single-stream leg binds a packed 16-byte buffer through SetVertexBuffer, which is the exact
// pre-REMED-GFX-201 path, and must both render correctly itself and leave no stale second stream
// bound for the leg after it.
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawMultiStreamTest, MultiStreamSingleStreamMultiStreamEachRendersItsOwnLayout)
{
    // plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryMultiStream())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireOrdinaryRendering();
    CNA_REQUIRE_MULTI_STREAM_INPUT();

    const GridLayout layout = TargetLayout();
    const std::vector<PositionRecord> positions = BuildPositionStream(layout, kLiveBand);
    const std::vector<ColorRecord> colors = BuildColorStream();

    // The packed single-stream buffer: the same geometry in the alternate band, one flat colour.
    const std::vector<PositionRecord> packedPositions =
        BuildPositionStream(layout, kAlternateBand);
    std::vector<std::uint8_t> packed(
        static_cast<std::size_t>(kBufferElementCount) * 16u, 0u);
    for (int i = 0; i < kBufferElementCount; ++i)
    {
        const auto byteBase = static_cast<std::size_t>(i) * 16u;
        std::memcpy(packed.data() + byteBase, &packedPositions[static_cast<std::size_t>(i)], 12u);
        const ColorRecord code = RecordForCode(ColorCode::Rewritten);
        std::memcpy(packed.data() + byteBase + 12u, &code, 4u);
    }

    VertexBuffer positionBuffer(
        device, PositionOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    positionBuffer.SetDataRaw(positions.data(), kBufferElementCount, kPositionStride);
    VertexBuffer colorBuffer(
        device, ColorOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    colorBuffer.SetDataRaw(colors.data(), kBufferElementCount, kColorStride);
    VertexBuffer packedBuffer(
        device,
        VertexDeclaration(
            16,
            {VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
             VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0)}),
        kBufferElementCount, BufferUsage::None);
    packedBuffer.SetDataRaw(packed.data(), kBufferElementCount, 16);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    const auto capture = [&]() {
        device.SetRenderTarget(nullptr);
        return CaptureTarget(target);
    };

    device.SetVertexBuffers({
        VertexBufferBinding(&positionBuffer, kPrefixOffset, 0),
        VertexBufferBinding(&colorBuffer, kPrefixOffset, 0),
    });
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    device.DrawPrimitives(PrimitiveType::TriangleList, 1 * kVerticesPerSlot, 1);
    const FrameSnapshot multiFirst = capture();
    ExpectOnlyCellLitWithCode(
        multiFirst, layout, 1, kLiveBand, LiveCodeForSlot(1), "multi-stream leg 1");

    device.SetVertexBuffer(&packedBuffer, kPrefixOffset);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    device.DrawPrimitives(PrimitiveType::TriangleList, 1 * kVerticesPerSlot, 1);
    const FrameSnapshot single = capture();
    ExpectOnlyCellLitWithCode(
        single, layout, 1, kAlternateBand, ColorCode::Rewritten,
        "single-stream leg must not see a stale second stream");

    device.SetVertexBuffers({
        VertexBufferBinding(&positionBuffer, kPrefixOffset, 0),
        VertexBufferBinding(&colorBuffer, kPrefixOffset, 0),
    });
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    device.DrawPrimitives(PrimitiveType::TriangleList, 1 * kVerticesPerSlot, 1);
    const FrameSnapshot multiSecond = capture();
    ExpectOnlyCellLitWithCode(
        multiSecond, layout, 1, kLiveBand, LiveCodeForSlot(1),
        "multi-stream leg 2 must not reuse the single-stream layout");
}

// ---------------------------------------------------------------------------
// Coverage item 17: THREE per-vertex streams. Position(12) + Colour(4) + TexCoord(8) concatenate
// to the packed 24-byte VertexPositionColorTexture layout every renderer recognizes. The texture is
// two texels wide, so the third stream's own records -- and its own VertexOffset -- decide which
// texel every pixel samples, and its contribution cannot be produced by the other two.
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawMultiStreamTest, ThreePerVertexStreamsEachSupplyTheirOwnElements)
{
    // plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryMultiStream())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireOrdinaryRendering();
    CNA_REQUIRE_MULTI_STREAM_INPUT();

    const GridLayout layout = TargetLayout();
    const std::vector<PositionRecord> positions = BuildPositionStream(layout, kLiveBand);
    // White live colours: the sampled texel alone decides the rendered code.
    const std::vector<ColorRecord> colors =
        BuildColorStream(ColorCode::Prefix, ColorCode::Unknown);
    std::vector<ColorRecord> whiteColors;
    whiteColors.reserve(static_cast<std::size_t>(kBufferElementCount));
    for (int i = 0; i < kPrefixOffset; ++i)
        whiteColors.push_back(RecordForCode(ColorCode::Prefix));
    for (int i = kPrefixOffset; i < kBufferElementCount; ++i)
        whiteColors.push_back(ColorRecord{255, 255, 255, 255});

    // The prefix decoy samples the RIGHT texel; every live record samples the LEFT one.
    std::vector<TexCoordRecord> texCoords;
    texCoords.reserve(static_cast<std::size_t>(kBufferElementCount));
    for (int i = 0; i < kPrefixOffset; ++i)
        texCoords.push_back(TexCoordRecord{0.75f, 0.5f});
    for (int i = kPrefixOffset; i < kBufferElementCount; ++i)
        texCoords.push_back(TexCoordRecord{0.25f, 0.5f});

    VertexBuffer positionBuffer(
        device, PositionOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    positionBuffer.SetDataRaw(positions.data(), kBufferElementCount, kPositionStride);
    VertexBuffer colorBuffer(
        device, ColorOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    colorBuffer.SetDataRaw(whiteColors.data(), kBufferElementCount, kColorStride);
    VertexBuffer texCoordBuffer(
        device, TexCoordOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    texCoordBuffer.SetDataRaw(texCoords.data(), kBufferElementCount, kTexCoordStride);

    Texture2D texture(device, 2, 1);
    const std::array<Color, 2> texels = {
        Color(RecordForCode(ColorCode::LiveA).r, RecordForCode(ColorCode::LiveA).g,
              RecordForCode(ColorCode::LiveA).b, std::uint8_t{255}),
        Color(RecordForCode(ColorCode::Alternate).r, RecordForCode(ColorCode::Alternate).g,
              RecordForCode(ColorCode::Alternate).b, std::uint8_t{255}),
    };
    texture.SetData(texels.data(), 2);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    device.SetVertexBuffers({
        VertexBufferBinding(&positionBuffer, kPrefixOffset, 0),
        VertexBufferBinding(&colorBuffer, kPrefixOffset, 0),
        VertexBufferBinding(&texCoordBuffer, kPrefixOffset, 0),
    });
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    effect.VertexColorEnabled = true;
    effect.setLightingEnabledProperty(false);
    effect.setTextureEnabledProperty(true);
    effect.setTextureProperty(&texture);
    effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
    effect.setAlphaProperty(1.0f);
    effect.Apply();
    device.DrawPrimitives(PrimitiveType::TriangleList, 2 * kVerticesPerSlot, 1);
    device.SetRenderTarget(nullptr);

    (void)colors;
    const FrameSnapshot snapshot = CaptureTarget(target);
    ExpectOnlyCellLitWithCode(
        snapshot, layout, 2, kLiveBand, ColorCode::LiveA,
        "the third stream's own records and offset decide the sampled texel");
}

// ---------------------------------------------------------------------------
// Coverage item 20: destroying the secondary stream's public wrapper and letting a new buffer take
// its handle/address. The new binding set must be the one that renders -- a renderer that kept a
// pointer or a cached layout keyed on the old address paints the old colour or crashes.
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawMultiStreamTest, DestroyingAndRecreatingStream1RebindsTheNewBuffer)
{
    // plan_runtimerenderer.md RTR-P9-5: reports a skip instead of not existing.
    if (!OrdinaryMultiStream())
        GTEST_SKIP() << "this renderer has no rasterizing/readback oracle for this draw path";
    RequireOrdinaryRendering();
    CNA_REQUIRE_MULTI_STREAM_INPUT();

    const GridLayout layout = TargetLayout();
    const std::vector<PositionRecord> positions = BuildPositionStream(layout, kLiveBand);
    const std::vector<ColorRecord> colors = BuildColorStream();
    const std::vector<ColorRecord> replacementColors =
        BuildColorStream(ColorCode::Prefix, ColorCode::Rewritten);

    VertexBuffer positionBuffer(
        device, PositionOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    positionBuffer.SetDataRaw(positions.data(), kBufferElementCount, kPositionStride);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    {
        auto colorBuffer = std::make_unique<VertexBuffer>(
            device, ColorOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
        colorBuffer->SetDataRaw(colors.data(), kBufferElementCount, kColorStride);
        device.SetVertexBuffers({
            VertexBufferBinding(&positionBuffer, kPrefixOffset, 0),
            VertexBufferBinding(colorBuffer.get(), kPrefixOffset, 0),
        });
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        ApplyMeshEffect(effect);
        device.DrawPrimitives(PrimitiveType::TriangleList, 3 * kVerticesPerSlot, 1);
        device.SetRenderTarget(nullptr);
        const FrameSnapshot snapshot = CaptureTarget(target);
        ExpectOnlyCellLitWithCode(
            snapshot, layout, 3, kLiveBand, LiveCodeForSlot(3), "before destruction");
        // The readback above has already flushed every deferred renderer, so the wrapper below is
        // destroyed with no draw of its own outstanding -- the lifetime this leg tests is the
        // CACHED one, not an in-flight command's.
        device.SetVertexBuffer(nullptr);
    }

    auto replacement = std::make_unique<VertexBuffer>(
        device, ColorOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    replacement->SetDataRaw(replacementColors.data(), kBufferElementCount, kColorStride);
    device.SetVertexBuffers({
        VertexBufferBinding(&positionBuffer, kPrefixOffset, 0),
        VertexBufferBinding(replacement.get(), kPrefixOffset, 0),
    });
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    device.DrawPrimitives(PrimitiveType::TriangleList, 3 * kVerticesPerSlot, 1);
    device.SetRenderTarget(nullptr);

    const FrameSnapshot snapshot = CaptureTarget(target);
    ExpectOnlyCellLitWithCode(
        snapshot, layout, 3, kLiveBand, ColorCode::Rewritten,
        "a recycled handle must not resurrect the destroyed stream");
}


// ---------------------------------------------------------------------------
// The capability boundary itself, asserted on EVERY renderer and in BOTH directions. A renderer that
// reports GraphicsCapability::MultiStreamVertexInput = false must reject an ordinary multi-stream
// draw with a deterministic public exception before any native submission -- never render from
// stream 0 alone, which would look like a correct draw of the wrong data. A renderer that reports
// true must accept it. This is what stops the skips above from outliving the gap they describe:
// the moment a renderer claims the capability, this test switches to the positive assertion and
// every skipped pixel case starts running.
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawMultiStreamTest, UnsupportedRendererRejectsMultiStreamDeterministically)
{
    if (!device.SupportsCapability(GraphicsCapability::ThreeD))
        GTEST_SKIP() << "Renderer explicitly does not support 3D rendering";

    const GridLayout layout = GridLayout{kTargetSize, kTargetSize};
    const std::vector<PositionRecord> positions = BuildPositionStream(layout, kLiveBand);
    const std::vector<ColorRecord> colors = BuildColorStream();

    VertexBuffer positionBuffer(
        device, PositionOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    positionBuffer.SetDataRaw(positions.data(), kBufferElementCount, kPositionStride);
    VertexBuffer colorBuffer(
        device, ColorOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    colorBuffer.SetDataRaw(colors.data(), kBufferElementCount, kColorStride);

    BasicEffect effect(device);
    device.SetVertexBuffers({
        VertexBufferBinding(&positionBuffer, kPrefixOffset, 0),
        VertexBufferBinding(&colorBuffer, kPrefixOffset, 0),
    });
    ApplyMeshEffect(effect);

    // Deliberately NOT inside a render target: this asserts the shared gate, which runs before any
    // renderer work, so it is meaningful even on a renderer with no readback.
    if (device.SupportsCapability(GraphicsCapability::MultiStreamVertexInput))
    {
        EXPECT_NO_THROW(device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1))
            << "a renderer that claims MultiStreamVertexInput must accept a two-stream draw";
    }
    else
    {
        EXPECT_THROW(
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1),
            System::NotSupportedException)
            << "a renderer that does not claim MultiStreamVertexInput must reject a two-stream "
               "draw deterministically, not render binding 0 alone";
    }
}

// ---------------------------------------------------------------------------
// Coverage item 18: a missing required stream. Runs on every renderer -- it is a public-API
// contract, not a pixel one. XNA's SetVertexBuffers rejects a null binding outright, so a
// half-described vertex can never reach a draw in the first place.
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawMultiStreamTest, NullSecondaryStreamBindingIsAcceptedAsUnusedSlot)
{
    VertexBuffer positionBuffer(
        device, PositionOnlyDeclaration(), kBufferElementCount, BufferUsage::None);

    // FNA performs no per-element null check in SetVertexBuffers: a null-buffer binding is a
    // legal unused slot (FNA itself stores VertexBufferBinding.None), and the draw dispatch
    // skips it. REMED-GFX-222 removed the bind-time rejection this test formerly asserted.
    EXPECT_NO_THROW(device.SetVertexBuffers({
        VertexBufferBinding(&positionBuffer, kPrefixOffset, 0),
        VertexBufferBinding(),
    }));
    const auto bound = device.GetVertexBuffers();
    ASSERT_EQ(bound.size(), 2u);
    EXPECT_EQ(bound[0].getVertexBufferProperty(), &positionBuffer);
    EXPECT_EQ(bound[1].getVertexBufferProperty(), nullptr);
}

// ---------------------------------------------------------------------------
// Coverage item 10 / the public binding-state contract, asserted without a rasterizer so every
// renderer runs it: GetVertexBuffers returns every slot with its own buffer, offset and frequency,
// SetVertexBuffer collapses the set to one, and the 16-slot ceiling is enforced.
// ---------------------------------------------------------------------------
TEST_F(OrdinaryDrawMultiStreamTest, BindingStateKeepsEverySlotsOwnBufferAndOffset)
{
    VertexBuffer positionBuffer(
        device, PositionOnlyDeclaration(), kBufferElementCount, BufferUsage::None);
    VertexBuffer colorBuffer(
        device, ColorOnlyDeclaration(), kBufferElementCount, BufferUsage::None);

    device.SetVertexBuffers({
        VertexBufferBinding(&positionBuffer, kPrefixOffset, 0),
        VertexBufferBinding(&colorBuffer, kPrefixOffset + kVerticesPerSlot, 0),
    });

    const auto bindings = device.GetVertexBuffers();
    ASSERT_EQ(bindings.size(), 2u);
    EXPECT_EQ(bindings[0].getVertexBufferProperty(), &positionBuffer);
    EXPECT_EQ(bindings[0].getVertexOffsetProperty(), kPrefixOffset);
    EXPECT_EQ(bindings[1].getVertexBufferProperty(), &colorBuffer);
    EXPECT_EQ(bindings[1].getVertexOffsetProperty(), kPrefixOffset + kVerticesPerSlot);
    EXPECT_EQ(device.GetVertexBuffer(), &positionBuffer);

    device.SetVertexBuffer(&colorBuffer, 1);
    const auto collapsed = device.GetVertexBuffers();
    ASSERT_EQ(collapsed.size(), 1u);
    EXPECT_EQ(collapsed[0].getVertexBufferProperty(), &colorBuffer);
    EXPECT_EQ(collapsed[0].getVertexOffsetProperty(), 1);

    std::vector<VertexBufferBinding> tooMany;
    for (int i = 0; i < 17; ++i)
        tooMany.emplace_back(&positionBuffer, 0, 0);
    EXPECT_THROW(
        device.SetVertexBuffers(tooMany),
        System::ArgumentOutOfRangeException);
}

// SPDX-License-Identifier: MS-PL
// REMED-GFX-202: the public multi-stream contract on the INSTANCED draw route.
//
// XNA 4.0 and FNA have exactly ONE vertex-binding model. `GraphicsDevice.DrawPrimitives`,
// `DrawIndexedPrimitives` and `DrawInstancedPrimitives` all call the same
// `PrepareVertexBindingArray(baseVertex)`, which hands the driver one
// `(buffer, declaration, stride, vertexOffset, instanceFrequency)` tuple per bound slot and then
// calls `FNA3D_ApplyVertexBufferBindings`. There is no separate "geometry buffer + instance
// buffer" pair anywhere in the reference: an instance stream is simply a binding whose
// `InstanceFrequency` is greater than zero. The reconciled CNA contract this file locks down is
// therefore:
//
//   every bound stream reaches the renderer, per-vertex and per-instance alike
//   each stream keeps its OWN slot, declaration, stride, VertexOffset and InstanceFrequency
//   VertexOffset is a vertex-ELEMENT offset, converted with that stream's own stride, exactly once
//   InstanceFrequency == 0 advances per vertex; > 0 advances once per that many instances, so the
//       record a per-instance stream supplies is `VertexOffset + floor(instanceIndex / frequency)`
//   the first instance always begins at instance record zero (D3D11's StartInstanceLocation is 0
//       in FNA3D's own driver; OpenGL has no start-instance at all)
//   baseVertex advances every PER-VERTEX stream and no per-instance stream -- a per-instance slot
//       is addressed by instance index, which BaseVertexLocation does not touch
//   startIndex still selects index elements only
//   a stream too short for the requested window is rejected even when the others are long enough,
//       per-instance streams included
//
// Fixture geometry -- a FOUR-axis oracle, one axis per bound stream. The target is divided into
// `kSlotCount` equal-width columns and `kBandCount` equal-height bands, as REMED-GFX-200/201 do:
//
//   * the COLUMN axis says which record the PER-INSTANCE stream at slot 2 supplied
//   * the BAND   axis says which record the PER-INSTANCE stream at slot 3 supplied
//   * the base cell each instance is measured from says which records slot 0 (POSITION) supplied
//   * the COLOUR says which records slot 1 (COLOR) supplied
//
// The two per-vertex streams are the REMED-GFX-201 arrangement: slot 0 is POSITION ONLY (12 bytes)
// and slot 1 is COLOUR ONLY (4 bytes), so their concatenation is the packed 16-byte
// position+colour vertex every renderer already recognizes but neither stream alone is a layout any
// renderer has. The two per-instance streams split the 4x4 world matrix the stock instanced shader
// reads: slot 2 supplies its first three columns (stride 48) and slot 3 supplies the fourth
// (stride 16). With every vertex at z = 0.5 the transformed position is
//
//     M * (x, y, 0.5, 1) = c0*x + c1*y + c2*0.5 + c3
//
// so with c0/c1 the identity axes, slot 2's third column is a pure HORIZONTAL displacement and
// slot 3's fourth column is a pure VERTICAL one. The two instance streams are therefore separately
// readable from a single frame, at different frequencies, with different strides and different
// element counts.
//
// Every buffer begins with its own decoy prefix, and each decoy is placed so that a distinct
// defect lights a distinct cell:
//
//   * slot 1 never bound                -> slot 0's 12-byte stride matches no layout: nothing
//                                          renders at all
//   * slot 1's VertexOffset dropped     -> the colour stream's own prefix decoy colour (green)
//   * slot 0's VertexOffset dropped     -> the position prefix decoy's column, not column 0
//   * slot 2 never bound / dropped      -> every instance lands in ONE column instead of four
//   * slot 2's VertexOffset dropped     -> the horizontal decoy shift of five columns
//   * slot 3 never bound                -> the band axis collapses to band 0
//   * slot 3's VertexOffset dropped     -> band 3 for the first instance pair
//   * slot 3's frequency read as 1      -> a fourth instance record that does not exist: the
//                                          per-instance range gate rejects the draw
//   * baseVertex applied to an instance -> every band and column moves together
//     stream
//
// Colour codes are the {0,255} RGB corners, so a nearest-corner classifier is exact rather than
// tolerant, and black is the background and never a code.
//
// Renderer scope. Three groups, each with its own declared boundary rather than a silent skip:
//
//   1. the PUBLIC TRANSPORT group -- validation, slot identity, capability rejection. Runs on
//      EVERY renderer, needs no rasterizer, and is where the red-first reproduction lives: before
//      this task a secondary per-vertex stream and every per-instance stream past the first
//      reached no validation and no renderer at all.
//   2. the PRESERVATION group -- the classic one-per-vertex + one-per-instance shape every
//      instancing renderer already renders. Must stay byte-identical.
//   3. the MIXED-STREAM PIXEL group -- gated at RUNTIME on
//      GraphicsCapability::MultiStreamVertexInput, with
//      UnsupportedRendererRejectsMixedStreamInstancingDeterministically asserting the opposite
//      arm on every renderer that does not claim it, so the skip cannot outlive the gap.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"

// Lets CNA_RENDERER_IS name identities bare, matching the guards it replaced.
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
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"

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
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexBufferBinding;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

// The renderers whose stock instanced path actually rasterizes and whose RenderTarget2D::GetData
// reads the result back -- REMED-GFX-118's own permanent suite set. A renderer outside it has no
// instanced draw implementation at all (`IGraphicsRenderer::DrawInstancedPrimitivesEx`'s default
// throws), which is a pre-existing capability boundary this task neither creates nor closes; the
// public transport group below still runs there and still asserts the shared validation contract.
/// plan_runtimerenderer.md RTR-P9-5: the same set, asked of the ACTIVE renderer.
[[nodiscard]] inline bool MultiStreamOracle()
{
    return CNA_RENDERER_IS(Bgfx, OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, WebGPU, Vulkan,
                           DirectX9, DirectX11, DirectX12, Magnum);
}

// The renderers whose instanced path was corrected to consume VertexBufferBinding.VertexOffset AND
// InstanceFrequency -- EasyGL (REMED-GFX-122), D3D11/D3D12 (REMED-GFX-123), Vulkan, bgfx and
// WebGPU (REMED-GFX-211/213). InstancedDrawRangeTests.cpp uses exactly this set for the same
// reason.
//
// This is now every renderer that rasterizes an instanced draw at all EXCEPT D3D9, so the triage
// group at the bottom of this file no longer carries a measured-defect arm. It carried one for as
// long as a renderer was known to drop both offsets and render InstanceFrequency 2 at an effective
// divisor of one: Vulkan, then bgfx, then WebGPU, each asserting its own pixel measurement until
// its correction landed and made that assertion fail, which is what forced the arm's removal. The
// legs themselves stay -- they still print what every renderer consumed -- and their remaining
// `#else` arm covers D3D9, which is outside this set only because no D3D display was reachable to
// measure it, and an unmeasured renderer must not be asserted either way.
/// plan_runtimerenderer.md RTR-P9-5: the same set, asked of the ACTIVE renderer.
[[nodiscard]] inline bool BindingOffsetOracle()
{
    return CNA_RENDERER_IS(OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, DirectX11, DirectX12, Vulkan, Bgfx, WebGPU, Magnum);
}

namespace
{
    /// Column axis: equal-width vertical cells. Eight divides 224 exactly.
    constexpr int kSlotCount = 8;

    /// Band axis: equal-height horizontal cells.
    constexpr int kBandCount = 4;

    /// Vertices one group's triangle owns.
    constexpr int kVerticesPerSlot = 3;

    /// Live records each per-vertex stream owns, one triangle per column.
    constexpr int kMeshElementCount = kSlotCount * kVerticesPerSlot;

    /// The position stream's decoy prefix, and therefore its "skip the decoy" VertexOffset.
    constexpr int kPositionPrefix = kVerticesPerSlot;

    /// The colour stream's decoy prefix -- deliberately DIFFERENT from the position stream's, so a
    /// renderer that applies one binding's offset to both streams cannot land on the right record.
    constexpr int kColorPrefix = 2 * kVerticesPerSlot;

    constexpr int kPositionElementCount = kPositionPrefix + kMeshElementCount;
    constexpr int kColorElementCount    = kColorPrefix + kMeshElementCount;

    /// The column the position stream's own decoy occupies.
    constexpr int kPositionDecoyColumn = kSlotCount - 1;

    /// The band the position stream's own decoy occupies.
    constexpr int kPositionDecoyBand = kBandCount - 1;

    /// The band every live position record is authored in; the band axis is the instance stream's.
    constexpr int kLiveBand = 0;

    /// Square render target: 224 = 8 * 28 = 4 * 56, so both axes divide exactly.
    constexpr int kTargetSize = 224;

    /// Instances every mixed-stream draw requests.
    constexpr int kInstanceCount = 4;

    /// Slot 2's InstanceFrequency: one record per instance.
    constexpr int kColumnStreamFrequency = 1;

    /// Slot 3's InstanceFrequency: one record per TWO instances -- the ">1 advances once every
    /// frequency instances" half of the contract, which a renderer that hardcodes a step rate of
    /// one cannot reproduce.
    constexpr int kBandStreamFrequency = 2;

    /// Slot 2's decoy record displaces five columns; every live record displaces 0..3.
    constexpr int kColumnDecoyShift = 5;

    /// Slot 3's decoy record displaces three bands; its live records displace 0 and 1.
    constexpr int kBandDecoyShift = 3;

    /// Slot 0's record: position only.
    struct PositionRecord
    {
        float x, y, z;
    };
    static_assert(sizeof(PositionRecord) == 12);

    /// Slot 1's record: colour only, in the R8G8B8A8 memory order every renderer's `Color`
    /// VertexElementFormat reads.
    struct ColorRecord
    {
        std::uint8_t r, g, b, a;
    };
    static_assert(sizeof(ColorRecord) == 4);

    /// Slot 2's record: the world matrix's first THREE columns.
    struct ColumnStreamRecord
    {
        float c0[4];
        float c1[4];
        float c2[4];
    };
    static_assert(sizeof(ColumnStreamRecord) == 48);

    /// Slot 3's record: the world matrix's FOURTH column, on its own, at its own frequency.
    struct BandStreamRecord
    {
        float c3[4];
    };
    static_assert(sizeof(BandStreamRecord) == 16);

    constexpr int kPositionStride     = static_cast<int>(sizeof(PositionRecord));
    constexpr int kColorStride        = static_cast<int>(sizeof(ColorRecord));
    constexpr int kColumnStreamStride = static_cast<int>(sizeof(ColumnStreamRecord));
    constexpr int kBandStreamStride   = static_cast<int>(sizeof(BandStreamRecord));

    VertexDeclaration PositionOnlyDeclaration()
    {
        return VertexDeclaration(
            kPositionStride,
            {VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0)});
    }

    VertexDeclaration ColorOnlyDeclaration()
    {
        return VertexDeclaration(
            kColorStride,
            {VertexElement(0, VertexElementFormat::Color, VertexElementUsage::Color, 0)});
    }

    /// The stock instanced shader reads the per-instance world matrix as four consecutive
    /// `Vector4`s carrying TextureCoordinate usage indices 1..4, which is the layout every CNA
    /// instanced renderer already expects. Splitting them across two bindings keeps those usages
    /// and their order exactly, and changes only WHICH buffer each column comes from.
    VertexDeclaration ColumnStreamDeclaration()
    {
        return VertexDeclaration(
            kColumnStreamStride,
            {
                VertexElement(0, VertexElementFormat::Vector4,
                              VertexElementUsage::TextureCoordinate, 1),
                VertexElement(16, VertexElementFormat::Vector4,
                              VertexElementUsage::TextureCoordinate, 2),
                VertexElement(32, VertexElementFormat::Vector4,
                              VertexElementUsage::TextureCoordinate, 3),
            });
    }

    VertexDeclaration BandStreamDeclaration()
    {
        return VertexDeclaration(
            kBandStreamStride,
            {VertexElement(0, VertexElementFormat::Vector4,
                           VertexElementUsage::TextureCoordinate, 4)});
    }

    /// The complete four-column matrix in ONE binding: the classic per-instance stream every
    /// renderer already renders, used by the preservation group and by every leg that varies only
    /// the per-vertex side.
    VertexDeclaration WholeMatrixDeclaration()
    {
        return VertexDeclaration(
            64,
            {
                VertexElement(0, VertexElementFormat::Vector4,
                              VertexElementUsage::TextureCoordinate, 1),
                VertexElement(16, VertexElementFormat::Vector4,
                              VertexElementUsage::TextureCoordinate, 2),
                VertexElement(32, VertexElementFormat::Vector4,
                              VertexElementUsage::TextureCoordinate, 3),
                VertexElement(48, VertexElementFormat::Vector4,
                              VertexElementUsage::TextureCoordinate, 4),
            });
    }

    struct WholeMatrixRecord
    {
        float c0[4];
        float c1[4];
        float c2[4];
        float c3[4];
    };
    static_assert(sizeof(WholeMatrixRecord) == 64);

    /// The {0,255} RGB corners. Every pair differs by 255 in at least one channel.
    enum class ColorCode
    {
        Background,
        LiveA,
        LiveB,
        LiveC,
        Prefix,
        Rewritten,
        Alternate,
        Unknown,
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

    /// Cycling three codes across eight groups means a displacement of one or two groups always
    /// changes the colour.
    ColorCode LiveCodeForGroup(int group)
    {
        switch (group % 3)
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
        return ColorCode::Unknown;
    }

    struct GridLayout
    {
        int width = 0;
        int height = 0;

        [[nodiscard]] float ColumnCenterX(int column) const
        {
            return static_cast<float>(width) *
                   (static_cast<float>(column) + 0.5f) / static_cast<float>(kSlotCount);
        }

        [[nodiscard]] float ColumnBoundaryX(int column) const
        {
            return static_cast<float>(width) *
                   static_cast<float>(column) / static_cast<float>(kSlotCount);
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
            return 0.28f * static_cast<float>(width) / static_cast<float>(kSlotCount);
        }

        [[nodiscard]] float HalfHeight() const
        {
            return 0.28f * static_cast<float>(height) / static_cast<float>(kBandCount);
        }
    };

    /// Identity World/View/Projection, so a vertex position IS its clip-space position.
    PositionRecord PositionAtPixel(const GridLayout& layout, float pixelX, float pixelY)
    {
        return PositionRecord{
            (2.0f * pixelX / static_cast<float>(layout.width)) - 1.0f,
            1.0f - (2.0f * pixelY / static_cast<float>(layout.height)),
            0.5f};
    }

    std::array<PositionRecord, kVerticesPerSlot> GroupTriangle(
        const GridLayout& layout, int column, int band)
    {
        const float halfWidth = layout.HalfWidth();
        const float halfHeight = layout.HalfHeight();
        const float centerX = layout.ColumnCenterX(column);
        const float centerY = layout.RowCenterY(band);
        return {
            PositionAtPixel(layout, centerX - halfWidth, centerY + halfHeight),
            PositionAtPixel(layout, centerX + halfWidth, centerY + halfHeight),
            PositionAtPixel(layout, centerX, centerY - halfHeight),
        };
    }

    /// The position stream: its own decoy triangle, then one triangle per column in `kLiveBand`.
    std::vector<PositionRecord> BuildPositionStream(const GridLayout& layout)
    {
        std::vector<PositionRecord> records;
        records.reserve(static_cast<std::size_t>(kPositionElementCount));
        const auto decoy = GroupTriangle(layout, kPositionDecoyColumn, kPositionDecoyBand);
        records.insert(records.end(), decoy.begin(), decoy.end());
        for (int group = 0; group < kSlotCount; ++group)
        {
            const auto triangle = GroupTriangle(layout, group, kLiveBand);
            records.insert(records.end(), triangle.begin(), triangle.end());
        }
        return records;
    }

    /// The colour stream: `kColorPrefix` decoy records, then one flat triangle's worth of
    /// `LiveCodeForGroup(group)` per group. `liveOverride` makes every live record one code, which
    /// is how a replacement buffer stays distinguishable from the original.
    std::vector<ColorRecord> BuildColorStream(
        ColorCode prefixCode = ColorCode::Prefix,
        ColorCode liveOverride = ColorCode::Unknown)
    {
        std::vector<ColorRecord> records;
        records.reserve(static_cast<std::size_t>(kColorElementCount));
        for (int i = 0; i < kColorPrefix; ++i)
            records.push_back(RecordForCode(prefixCode));
        for (int group = 0; group < kSlotCount; ++group)
        {
            const ColorCode code =
                liveOverride == ColorCode::Unknown ? LiveCodeForGroup(group) : liveOverride;
            for (int i = 0; i < kVerticesPerSlot; ++i)
                records.push_back(RecordForCode(code));
        }
        return records;
    }

    /// One NDC column width; a vertex at z = 0.5 receives `c2 * 0.5`, so the stored column carries
    /// twice the displacement it must produce.
    constexpr float kNdcColumnWidth = 2.0f / static_cast<float>(kSlotCount);
    constexpr float kNdcBandHeight  = 2.0f / static_cast<float>(kBandCount);

    ColumnStreamRecord MakeColumnRecord(int columnShift)
    {
        ColumnStreamRecord record{};
        record.c0[0] = 1.0f;
        record.c1[1] = 1.0f;
        record.c2[0] = 2.0f * static_cast<float>(columnShift) * kNdcColumnWidth;
        return record;
    }

    BandStreamRecord MakeBandRecord(int bandShift)
    {
        BandStreamRecord record{};
        // NDC y grows upwards while bands grow downwards.
        record.c3[1] = -static_cast<float>(bandShift) * kNdcBandHeight;
        record.c3[3] = 1.0f;
        return record;
    }

    WholeMatrixRecord MakeWholeMatrixRecord(int columnShift, int bandShift)
    {
        WholeMatrixRecord record{};
        record.c0[0] = 1.0f;
        record.c1[1] = 1.0f;
        record.c2[0] = 2.0f * static_cast<float>(columnShift) * kNdcColumnWidth;
        record.c3[1] = -static_cast<float>(bandShift) * kNdcBandHeight;
        record.c3[3] = 1.0f;
        return record;
    }

    /// Slot 2's buffer: one decoy record then `kInstanceCount` live records shifting 0..3 columns.
    std::vector<ColumnStreamRecord> BuildColumnStream()
    {
        std::vector<ColumnStreamRecord> records;
        records.push_back(MakeColumnRecord(kColumnDecoyShift));
        for (int i = 0; i < kInstanceCount; ++i)
            records.push_back(MakeColumnRecord(i));
        return records;
    }

    /// Slot 3's buffer: one decoy record then the two records four instances at frequency two
    /// consume.
    std::vector<BandStreamRecord> BuildBandStream()
    {
        std::vector<BandStreamRecord> records;
        records.push_back(MakeBandRecord(kBandDecoyShift));
        records.push_back(MakeBandRecord(0));
        records.push_back(MakeBandRecord(1));
        return records;
    }

    /// The identity index buffer over the live records only: index element `i` addresses record
    /// `VertexOffset + i` of every per-vertex stream, so every requested range is expressed purely
    /// through the public startIndex/baseVertex/VertexOffset triple.
    std::vector<std::uint16_t> BuildIdentityIndices16()
    {
        std::vector<std::uint16_t> indices;
        indices.reserve(static_cast<std::size_t>(kMeshElementCount));
        for (int i = 0; i < kMeshElementCount; ++i)
            indices.push_back(static_cast<std::uint16_t>(i));
        return indices;
    }

    std::vector<std::uint32_t> BuildIdentityIndices32()
    {
        std::vector<std::uint32_t> indices;
        indices.reserve(static_cast<std::size_t>(kMeshElementCount));
        for (int i = 0; i < kMeshElementCount; ++i)
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

    ColorCode DominantCodeInCell(
        const FrameSnapshot& snapshot, const GridLayout& layout,
        int column, int band, int& litCount)
    {
        const int x0 = static_cast<int>(layout.ColumnBoundaryX(column) + 0.999f);
        const int x1 = static_cast<int>(layout.ColumnBoundaryX(column + 1));
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

    int CountLitInCell(const FrameSnapshot& snapshot, const GridLayout& layout,
                       int column, int band)
    {
        int lit = 0;
        (void)DominantCodeInCell(snapshot, layout, column, band, lit);
        return lit;
    }

    /// A compact `band: column0..columnN` map of lit counts and colour codes. Every failure here is
    /// about which record which stream supplied, so every failure prints all four axes at once.
    std::string DescribeFrame(const FrameSnapshot& snapshot, const GridLayout& layout)
    {
        std::string map = "\n  lit cells (band: column0..column7, count/code):";
        for (int band = 0; band < kBandCount; ++band)
        {
            map += "\n    band " + std::to_string(band) + ':';
            for (int column = 0; column < kSlotCount; ++column)
            {
                int lit = 0;
                const ColorCode code = DominantCodeInCell(snapshot, layout, column, band, lit);
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

    /// One instance's expected landing cell and colour.
    struct ExpectedCell
    {
        int column = 0;
        int band = 0;
        ColorCode code = ColorCode::Unknown;
    };

    class InstancedDrawMultiStreamTest : public ::testing::Test
    {
    protected:
        GraphicsDevice device;

        /// GTEST_SKIP() only suppresses the remaining TEST BODY when raised from SetUp() --
        /// raised inside a helper the body calls, it merely returns from the helper (the exact
        /// reason the multi-stream skip is a macro). Hardware instancing is this whole file's
        /// subject and needs BOTH a 3D pipeline and an instancing path, so both capabilities
        /// are gated here: a renderer with no 3D pipeline at all (e.g. OPENVG) and a renderer
        /// whose profile reports GraphicsCapability::Instancing = false (e.g. OPENGLES2 -- core
        /// OpenGL ES 2.0 has no glDrawElementsInstanced/glVertexAttribDivisor, see
        /// docs/opengles2-renderer.md) each skip every leg here up front.
        void SetUp() override
        {
            if (!device.SupportsCapability(GraphicsCapability::ThreeD))
                GTEST_SKIP() << "Renderer explicitly does not support 3D rendering";
            if (!device.SupportsCapability(GraphicsCapability::Instancing))
                GTEST_SKIP() << "Renderer reports GraphicsCapability::Instancing = false: hardware "
                                "instancing is unavailable on this renderer profile";
        }

        void RequireInstancedRendering()
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
        /// -- which only the secondary per-vertex stream can supply -- as the entire output.
        static void ApplyMeshEffect(BasicEffect& effect)
        {
            effect.VertexColorEnabled = true;
            effect.setLightingEnabledProperty(false);
            effect.setTextureEnabledProperty(false);
            effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            effect.setAlphaProperty(1.0f);
            effect.Apply();
        }

        /// The whole oracle in one assertion: exactly these cells lit, each in its own colour, and
        /// nothing else in the frame lit at all.
        static void ExpectExactlyTheseCells(
            const FrameSnapshot& snapshot, const GridLayout& layout,
            const std::vector<ExpectedCell>& expected, const char* label)
        {
            int accounted = 0;
            for (const ExpectedCell& cell : expected)
            {
                int lit = 0;
                const ColorCode actual =
                    DominantCodeInCell(snapshot, layout, cell.column, cell.band, lit);
                accounted += lit;
                EXPECT_GT(lit, 0)
                    << label << ": nothing rendered in column " << cell.column
                    << " band " << cell.band << DescribeFrame(snapshot, layout);
                EXPECT_EQ(static_cast<int>(actual), static_cast<int>(cell.code))
                    << label << ": column " << cell.column << " band " << cell.band
                    << " carried " << CodeName(actual) << ", expected " << CodeName(cell.code)
                    << DescribeFrame(snapshot, layout);
            }
            EXPECT_EQ(accounted, snapshot.CountLit())
                << label << ": geometry rendered outside the expected cells"
                << DescribeFrame(snapshot, layout);
        }

        /// The same assertion WITHOUT the colour axis, for the legs that run on every instancing
        /// renderer. EasyGL, bgfx, Vulkan and WebGPU all colour the instanced route from the
        /// per-vertex stream now (REMED-GFX-212 corrected the last two), but D3D11's and D3D12's
        /// stock instanced shaders are still identified from source as taking `DiffuseColor`
        /// instead, and no D3D display was reachable to measure them. A cross-renderer preservation
        /// gate must therefore assert WHICH RECORDS each stream supplied, which the cell positions
        /// alone already say, and leave the colour axis to the mixed-stream group and to
        /// InstancedVertexColorTests.cpp, which measures it directly on every renderer it runs on.
        static void ExpectExactlyTheseCellsIgnoringColour(
            const FrameSnapshot& snapshot, const GridLayout& layout,
            const std::vector<ExpectedCell>& expected, const char* label)
        {
            int accounted = 0;
            for (const ExpectedCell& cell : expected)
            {
                const int lit = CountLitInCell(snapshot, layout, cell.column, cell.band);
                accounted += lit;
                EXPECT_GT(lit, 0)
                    << label << ": nothing rendered in column " << cell.column
                    << " band " << cell.band << DescribeFrame(snapshot, layout);
            }
            EXPECT_EQ(accounted, snapshot.CountLit())
                << label << ": geometry rendered outside the expected cells"
                << DescribeFrame(snapshot, layout);
        }
    };
}

/// REMED-GFX-202: a DECLARED boundary, not a silent skip. A renderer that has not yet been taught to
/// re-slot its stride-derived input elements across several bindings reports the capability as
/// false and is rejected deterministically by GraphicsDevice;
/// UnsupportedRendererRejectsMixedStreamInstancingDeterministically below asserts exactly that on
/// every renderer, and flips to the positive assertion the moment one claims the capability, so this
/// skip cannot outlive the gap it describes. A macro rather than a helper because GTEST_SKIP()
/// returns from the function it is written in.
#define CNA_REQUIRE_MIXED_STREAM_INSTANCING()                                                \
    do {                                                                                     \
        if (!device.SupportsCapability(GraphicsCapability::MultiStreamVertexInput))           \
        {                                                                                    \
            GTEST_SKIP()                                                                     \
                << "Renderer reports GraphicsCapability::MultiStreamVertexInput = false: "     \
                   "mixed-frequency multi-stream vertex input is not implemented on this "    \
                   "renderer (REMED-GFX-203..208). The draw is rejected with "                 \
                   "System::NotSupportedException rather than rendered from a subset of the " \
                   "bound streams -- see "                                                    \
                   "UnsupportedRendererRejectsMixedStreamInstancingDeterministically.";        \
        }                                                                                    \
    } while (false)

namespace
{
    /// Everything the mixed-stream legs bind, built once per leg so each leg owns its buffers.
    struct MixedStreamFixture
    {
        std::vector<PositionRecord> positions;
        std::vector<ColorRecord> colors;
        std::vector<ColumnStreamRecord> columns;
        std::vector<BandStreamRecord> bands;
        std::vector<std::uint16_t> indices16;
        std::vector<std::uint32_t> indices32;
    };

    MixedStreamFixture BuildMixedStreamFixture(
        const GridLayout& layout,
        ColorCode colorPrefix = ColorCode::Prefix,
        ColorCode colorLiveOverride = ColorCode::Unknown)
    {
        MixedStreamFixture fixture;
        fixture.positions = BuildPositionStream(layout);
        fixture.colors = BuildColorStream(colorPrefix, colorLiveOverride);
        fixture.columns = BuildColumnStream();
        fixture.bands = BuildBandStream();
        fixture.indices16 = BuildIdentityIndices16();
        fixture.indices32 = BuildIdentityIndices32();
        return fixture;
    }

    /// The four cells the canonical mixed-stream draw must produce for geometry group @p group:
    /// slot 2's live records displace 0..3 columns at frequency one, slot 3's displace 0 then 1
    /// band at frequency two.
    std::vector<ExpectedCell> CanonicalCells(int group)
    {
        const ColorCode code = LiveCodeForGroup(group);
        std::vector<ExpectedCell> cells;
        for (int instance = 0; instance < kInstanceCount; ++instance)
        {
            const int columnShift = instance / kColumnStreamFrequency;
            const int bandShift = instance / kBandStreamFrequency;
            cells.push_back(ExpectedCell{group + columnShift, kLiveBand + bandShift, code});
        }
        return cells;
    }
}


// ---------------------------------------------------------------------------
// Coverage items 3, 4, 5, 6, 7, 8, 12, 13, 14: the canonical arrangement. TWO per-vertex streams
// and TWO per-instance streams, at four different strides, with four different nonzero
// VertexOffsets, at two different InstanceFrequencies, over 32-bit indices. Every one of the four
// bound streams supplies an axis no other stream can produce.
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, TwoPerVertexAndTwoPerInstanceStreamsEachSupplyTheirOwnAxis)
{
    // plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this
    // group, so on every other renderer these tests did not exist at all.
    if (!MultiStreamOracle())
        GTEST_SKIP() << "this renderer is outside the MultiStreamOracle set";
    RequireInstancedRendering();
    CNA_REQUIRE_MIXED_STREAM_INSTANCING();

    const GridLayout layout = TargetLayout();
    const MixedStreamFixture fixture = BuildMixedStreamFixture(layout);

    VertexBuffer positionBuffer(
        device, PositionOnlyDeclaration(), kPositionElementCount, BufferUsage::None);
    positionBuffer.SetDataRaw(fixture.positions.data(), kPositionElementCount, kPositionStride);
    VertexBuffer colorBuffer(
        device, ColorOnlyDeclaration(), kColorElementCount, BufferUsage::None);
    colorBuffer.SetDataRaw(fixture.colors.data(), kColorElementCount, kColorStride);
    VertexBuffer columnBuffer(
        device, ColumnStreamDeclaration(),
        static_cast<int>(fixture.columns.size()), BufferUsage::None);
    columnBuffer.SetDataRaw(
        fixture.columns.data(), static_cast<int>(fixture.columns.size()), kColumnStreamStride);
    VertexBuffer bandBuffer(
        device, BandStreamDeclaration(),
        static_cast<int>(fixture.bands.size()), BufferUsage::None);
    bandBuffer.SetDataRaw(
        fixture.bands.data(), static_cast<int>(fixture.bands.size()), kBandStreamStride);
    IndexBuffer indexBuffer(
        device, IndexElementSize::ThirtyTwoBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(fixture.indices32.data(), kMeshElementCount);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    device.SetVertexBuffers({
        VertexBufferBinding(&positionBuffer, kPositionPrefix, 0),
        VertexBufferBinding(&colorBuffer, kColorPrefix, 0),
        VertexBufferBinding(&columnBuffer, 1, kColumnStreamFrequency),
        VertexBufferBinding(&bandBuffer, 1, kBandStreamFrequency),
    });
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 0, 0, kVerticesPerSlot, 0, 1, kInstanceCount);
    device.SetRenderTarget(nullptr);

    const FrameSnapshot snapshot = CaptureTarget(target);
    ExpectExactlyTheseCells(
        snapshot, layout, CanonicalCells(0),
        "each of the four bound streams supplies its own axis");
}

// ---------------------------------------------------------------------------
// Coverage items 9, 10, 11: nonzero baseVertex and nonzero startIndex with 16-bit indices.
// baseVertex advances both PER-VERTEX streams by that many of their own elements and must not move
// either per-instance stream; startIndex selects index elements only. Applying baseVertex to an
// instance stream moves every cell; applying it twice selects a group past the end.
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, BaseVertexAdvancesOnlyPerVertexStreams)
{
    // plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this
    // group, so on every other renderer these tests did not exist at all.
    if (!MultiStreamOracle())
        GTEST_SKIP() << "this renderer is outside the MultiStreamOracle set";
    RequireInstancedRendering();
    CNA_REQUIRE_MIXED_STREAM_INSTANCING();

    const GridLayout layout = TargetLayout();
    const MixedStreamFixture fixture = BuildMixedStreamFixture(layout);

    VertexBuffer positionBuffer(
        device, PositionOnlyDeclaration(), kPositionElementCount, BufferUsage::None);
    positionBuffer.SetDataRaw(fixture.positions.data(), kPositionElementCount, kPositionStride);
    VertexBuffer colorBuffer(
        device, ColorOnlyDeclaration(), kColorElementCount, BufferUsage::None);
    colorBuffer.SetDataRaw(fixture.colors.data(), kColorElementCount, kColorStride);
    VertexBuffer columnBuffer(
        device, ColumnStreamDeclaration(),
        static_cast<int>(fixture.columns.size()), BufferUsage::None);
    columnBuffer.SetDataRaw(
        fixture.columns.data(), static_cast<int>(fixture.columns.size()), kColumnStreamStride);
    VertexBuffer bandBuffer(
        device, BandStreamDeclaration(),
        static_cast<int>(fixture.bands.size()), BufferUsage::None);
    bandBuffer.SetDataRaw(
        fixture.bands.data(), static_cast<int>(fixture.bands.size()), kBandStreamStride);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(fixture.indices16.data(), kMeshElementCount);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    // group 1 through startIndex, then group 1 more through baseVertex: the requested group is 2.
    constexpr int kStartIndex = 1 * kVerticesPerSlot;
    constexpr int kBaseVertex = 1 * kVerticesPerSlot;
    constexpr int kRequestedGroup = 2;

    device.SetVertexBuffers({
        VertexBufferBinding(&positionBuffer, kPositionPrefix, 0),
        VertexBufferBinding(&colorBuffer, kColorPrefix, 0),
        VertexBufferBinding(&columnBuffer, 1, kColumnStreamFrequency),
        VertexBufferBinding(&bandBuffer, 1, kBandStreamFrequency),
    });
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, kBaseVertex, 0, kVerticesPerSlot, kStartIndex, 1,
        kInstanceCount);
    device.SetRenderTarget(nullptr);

    const FrameSnapshot snapshot = CaptureTarget(target);
    ExpectExactlyTheseCells(
        snapshot, layout, CanonicalCells(kRequestedGroup),
        "baseVertex advances every per-vertex stream once and no per-instance stream");
}

// ---------------------------------------------------------------------------
// Coverage items 15, 16, 17: changing ONLY the secondary per-vertex stream, then ONLY one instance
// stream, then returning to the complete original binding set. Each leg must render its own
// bindings; the final leg must reproduce the first leg exactly, which a cached input layout or a
// stale divisor cannot.
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, ReplacingOneStreamAtATimeAndReturningKeepsEachLegsBindings)
{
    // plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this
    // group, so on every other renderer these tests did not exist at all.
    if (!MultiStreamOracle())
        GTEST_SKIP() << "this renderer is outside the MultiStreamOracle set";
    RequireInstancedRendering();
    CNA_REQUIRE_MIXED_STREAM_INSTANCING();

    const GridLayout layout = TargetLayout();
    const MixedStreamFixture fixture = BuildMixedStreamFixture(layout);
    const std::vector<ColorRecord> replacementColors =
        BuildColorStream(ColorCode::Prefix, ColorCode::Alternate);

    // A replacement band stream whose live records displace 2 then 3 bands instead of 0 then 1.
    std::vector<BandStreamRecord> replacementBands;
    replacementBands.push_back(MakeBandRecord(kBandDecoyShift));
    replacementBands.push_back(MakeBandRecord(2));
    replacementBands.push_back(MakeBandRecord(3));

    VertexBuffer positionBuffer(
        device, PositionOnlyDeclaration(), kPositionElementCount, BufferUsage::None);
    positionBuffer.SetDataRaw(fixture.positions.data(), kPositionElementCount, kPositionStride);
    VertexBuffer colorBuffer(
        device, ColorOnlyDeclaration(), kColorElementCount, BufferUsage::None);
    colorBuffer.SetDataRaw(fixture.colors.data(), kColorElementCount, kColorStride);
    VertexBuffer altColorBuffer(
        device, ColorOnlyDeclaration(), kColorElementCount, BufferUsage::None);
    altColorBuffer.SetDataRaw(replacementColors.data(), kColorElementCount, kColorStride);
    VertexBuffer columnBuffer(
        device, ColumnStreamDeclaration(),
        static_cast<int>(fixture.columns.size()), BufferUsage::None);
    columnBuffer.SetDataRaw(
        fixture.columns.data(), static_cast<int>(fixture.columns.size()), kColumnStreamStride);
    VertexBuffer bandBuffer(
        device, BandStreamDeclaration(),
        static_cast<int>(fixture.bands.size()), BufferUsage::None);
    bandBuffer.SetDataRaw(
        fixture.bands.data(), static_cast<int>(fixture.bands.size()), kBandStreamStride);
    VertexBuffer altBandBuffer(
        device, BandStreamDeclaration(),
        static_cast<int>(replacementBands.size()), BufferUsage::None);
    altBandBuffer.SetDataRaw(
        replacementBands.data(), static_cast<int>(replacementBands.size()), kBandStreamStride);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(fixture.indices16.data(), kMeshElementCount);

    BasicEffect effect(device);
    device.SetIndexBuffer(&indexBuffer);

    auto renderLeg = [&](VertexBuffer& colors, VertexBuffer& bands) {
        RenderTarget2D target = MakeTarget();
        device.SetVertexBuffers({
            VertexBufferBinding(&positionBuffer, kPositionPrefix, 0),
            VertexBufferBinding(&colors, kColorPrefix, 0),
            VertexBufferBinding(&columnBuffer, 1, kColumnStreamFrequency),
            VertexBufferBinding(&bands, 1, kBandStreamFrequency),
        });
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        ApplyMeshEffect(effect);
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 0, 0, kVerticesPerSlot, 0, 1, kInstanceCount);
        device.SetRenderTarget(nullptr);
        return CaptureTarget(target);
    };

    const FrameSnapshot first = renderLeg(colorBuffer, bandBuffer);
    ExpectExactlyTheseCells(first, layout, CanonicalCells(0), "leg A: the original binding set");

    const FrameSnapshot colorOnly = renderLeg(altColorBuffer, bandBuffer);
    std::vector<ExpectedCell> colorOnlyCells = CanonicalCells(0);
    for (ExpectedCell& cell : colorOnlyCells)
        cell.code = ColorCode::Alternate;
    ExpectExactlyTheseCells(
        colorOnly, layout, colorOnlyCells,
        "leg B: only the secondary per-vertex stream changed");

    const FrameSnapshot bandOnly = renderLeg(colorBuffer, altBandBuffer);
    std::vector<ExpectedCell> bandOnlyCells;
    for (int instance = 0; instance < kInstanceCount; ++instance)
    {
        bandOnlyCells.push_back(ExpectedCell{
            instance / kColumnStreamFrequency,
            2 + instance / kBandStreamFrequency,
            LiveCodeForGroup(0)});
    }
    ExpectExactlyTheseCells(
        bandOnly, layout, bandOnlyCells, "leg C: only one per-instance stream changed");

    const FrameSnapshot back = renderLeg(colorBuffer, bandBuffer);
    ExpectExactlyTheseCells(
        back, layout, CanonicalCells(0),
        "leg D: returning to the complete original binding set");
}

// ---------------------------------------------------------------------------
// Coverage items 18, 23: draw A under bindings X, draw B under bindings Y, into the same target,
// then an ORDINARY draw between two instanced ones. A deferred renderer that re-reads the live
// binding state at replay renders both instanced draws with whichever set was bound last.
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, QueuedDrawsUnderDifferentBindingSetsKeepTheirOwn)
{
    // plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this
    // group, so on every other renderer these tests did not exist at all.
    if (!MultiStreamOracle())
        GTEST_SKIP() << "this renderer is outside the MultiStreamOracle set";
    RequireInstancedRendering();
    CNA_REQUIRE_MIXED_STREAM_INSTANCING();

    const GridLayout layout = TargetLayout();
    const MixedStreamFixture fixture = BuildMixedStreamFixture(layout);
    const std::vector<ColorRecord> replacementColors =
        BuildColorStream(ColorCode::Prefix, ColorCode::Alternate);

    VertexBuffer positionBuffer(
        device, PositionOnlyDeclaration(), kPositionElementCount, BufferUsage::None);
    positionBuffer.SetDataRaw(fixture.positions.data(), kPositionElementCount, kPositionStride);
    VertexBuffer colorBuffer(
        device, ColorOnlyDeclaration(), kColorElementCount, BufferUsage::None);
    colorBuffer.SetDataRaw(fixture.colors.data(), kColorElementCount, kColorStride);
    VertexBuffer altColorBuffer(
        device, ColorOnlyDeclaration(), kColorElementCount, BufferUsage::None);
    altColorBuffer.SetDataRaw(replacementColors.data(), kColorElementCount, kColorStride);
    VertexBuffer columnBuffer(
        device, ColumnStreamDeclaration(),
        static_cast<int>(fixture.columns.size()), BufferUsage::None);
    columnBuffer.SetDataRaw(
        fixture.columns.data(), static_cast<int>(fixture.columns.size()), kColumnStreamStride);
    VertexBuffer bandBuffer(
        device, BandStreamDeclaration(),
        static_cast<int>(fixture.bands.size()), BufferUsage::None);
    bandBuffer.SetDataRaw(
        fixture.bands.data(), static_cast<int>(fixture.bands.size()), kBandStreamStride);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(fixture.indices16.data(), kMeshElementCount);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);

    // Draw A: the original colour stream, geometry group 0.
    device.SetVertexBuffers({
        VertexBufferBinding(&positionBuffer, kPositionPrefix, 0),
        VertexBufferBinding(&colorBuffer, kColorPrefix, 0),
        VertexBufferBinding(&columnBuffer, 1, kColumnStreamFrequency),
        VertexBufferBinding(&bandBuffer, 1, kBandStreamFrequency),
    });
    ApplyMeshEffect(effect);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 0, 0, kVerticesPerSlot, 0, 1, kInstanceCount);

    // An ORDINARY multi-stream draw between the two instanced ones, into the decoy band, so a
    // renderer that leaves a divisor or an instance binding enabled cannot pass this test.
    device.SetVertexBuffers({
        VertexBufferBinding(&positionBuffer, 0, 0),
        VertexBufferBinding(&colorBuffer, 0, 0),
    });
    ApplyMeshEffect(effect);
    device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);

    // Draw B: the replacement colour stream, geometry group 4 through baseVertex.
    device.SetVertexBuffers({
        VertexBufferBinding(&positionBuffer, kPositionPrefix, 0),
        VertexBufferBinding(&altColorBuffer, kColorPrefix, 0),
        VertexBufferBinding(&columnBuffer, 1, kColumnStreamFrequency),
        VertexBufferBinding(&bandBuffer, 1, kBandStreamFrequency),
    });
    ApplyMeshEffect(effect);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 4 * kVerticesPerSlot, 0, kVerticesPerSlot, 0, 1, 1);

    device.SetRenderTarget(nullptr);

    const FrameSnapshot snapshot = CaptureTarget(target);
    std::vector<ExpectedCell> expected = CanonicalCells(0);
    // The ordinary draw consumed record 0 of both streams: the position decoy cell in the colour
    // stream's own prefix code.
    expected.push_back(
        ExpectedCell{kPositionDecoyColumn, kPositionDecoyBand, ColorCode::Prefix});
    // Draw B: one instance, so slot 2 record 1 (no column shift) and slot 3 record 1 (no band
    // shift), on geometry group 4.
    expected.push_back(ExpectedCell{4, kLiveBand, ColorCode::Alternate});
    ExpectExactlyTheseCells(
        snapshot, layout, expected,
        "each queued draw keeps the complete binding set it was issued under");
}

// ---------------------------------------------------------------------------
// Coverage item 6 (non-contiguous ACTIVE slots) and the REMED-GFX-201 semantic-composition rule on
// the instanced route. Slots 1 and 3 repeat an earlier per-vertex stream's complete (usage,
// usageIndex) set, so XNA drops them -- "Stream not in use!" -- and the ACTIVE stream slots become
// 0, 2, 4, 5 rather than 0, 1, 2, 3. Every active stream must keep its OWN public slot number.
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, DuplicateSemanticStreamsAreDroppedAndSlotsStayNonContiguous)
{
    // plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this
    // group, so on every other renderer these tests did not exist at all.
    if (!MultiStreamOracle())
        GTEST_SKIP() << "this renderer is outside the MultiStreamOracle set";
    RequireInstancedRendering();
    CNA_REQUIRE_MIXED_STREAM_INSTANCING();

    const GridLayout layout = TargetLayout();
    const MixedStreamFixture fixture = BuildMixedStreamFixture(layout);
    // The decoy streams carry the SAME usages as the streams before them, with data that would
    // paint a different cell and a different colour if they were ever composed into the vertex.
    const std::vector<PositionRecord> decoyPositions = BuildPositionStream(layout);
    const std::vector<ColorRecord> decoyColors =
        BuildColorStream(ColorCode::Rewritten, ColorCode::Rewritten);

    VertexBuffer positionBuffer(
        device, PositionOnlyDeclaration(), kPositionElementCount, BufferUsage::None);
    positionBuffer.SetDataRaw(fixture.positions.data(), kPositionElementCount, kPositionStride);
    VertexBuffer positionDecoyBuffer(
        device, PositionOnlyDeclaration(), kPositionElementCount, BufferUsage::None);
    positionDecoyBuffer.SetDataRaw(
        decoyPositions.data(), kPositionElementCount, kPositionStride);
    VertexBuffer colorBuffer(
        device, ColorOnlyDeclaration(), kColorElementCount, BufferUsage::None);
    colorBuffer.SetDataRaw(fixture.colors.data(), kColorElementCount, kColorStride);
    VertexBuffer colorDecoyBuffer(
        device, ColorOnlyDeclaration(), kColorElementCount, BufferUsage::None);
    colorDecoyBuffer.SetDataRaw(decoyColors.data(), kColorElementCount, kColorStride);
    VertexBuffer columnBuffer(
        device, ColumnStreamDeclaration(),
        static_cast<int>(fixture.columns.size()), BufferUsage::None);
    columnBuffer.SetDataRaw(
        fixture.columns.data(), static_cast<int>(fixture.columns.size()), kColumnStreamStride);
    VertexBuffer bandBuffer(
        device, BandStreamDeclaration(),
        static_cast<int>(fixture.bands.size()), BufferUsage::None);
    bandBuffer.SetDataRaw(
        fixture.bands.data(), static_cast<int>(fixture.bands.size()), kBandStreamStride);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(fixture.indices16.data(), kMeshElementCount);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    device.SetVertexBuffers({
        VertexBufferBinding(&positionBuffer, kPositionPrefix, 0),      // slot 0, active
        VertexBufferBinding(&positionDecoyBuffer, 0, 0),               // slot 1, dropped
        VertexBufferBinding(&colorBuffer, kColorPrefix, 0),            // slot 2, active
        VertexBufferBinding(&colorDecoyBuffer, 0, 0),                  // slot 3, dropped
        VertexBufferBinding(&columnBuffer, 1, kColumnStreamFrequency), // slot 4, active
        VertexBufferBinding(&bandBuffer, 1, kBandStreamFrequency),     // slot 5, active
    });
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 0, 0, kVerticesPerSlot, 0, 1, kInstanceCount);
    device.SetRenderTarget(nullptr);

    const FrameSnapshot snapshot = CaptureTarget(target);
    ExpectExactlyTheseCells(
        snapshot, layout, CanonicalCells(0),
        "a repeated (usage, usageIndex) set contributes nothing and does not renumber the rest");
}

// ---------------------------------------------------------------------------
// Coverage item 24: repeated frames through the same complete binding set. A layout/pipeline cache
// keyed on anything per-frame, or a divisor left set from the previous frame, diverges here.
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, RepeatedFramesReproduceTheSameMixedStreamFrame)
{
    // plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this
    // group, so on every other renderer these tests did not exist at all.
    if (!MultiStreamOracle())
        GTEST_SKIP() << "this renderer is outside the MultiStreamOracle set";
    RequireInstancedRendering();
    CNA_REQUIRE_MIXED_STREAM_INSTANCING();

    const GridLayout layout = TargetLayout();
    const MixedStreamFixture fixture = BuildMixedStreamFixture(layout);

    VertexBuffer positionBuffer(
        device, PositionOnlyDeclaration(), kPositionElementCount, BufferUsage::None);
    positionBuffer.SetDataRaw(fixture.positions.data(), kPositionElementCount, kPositionStride);
    VertexBuffer colorBuffer(
        device, ColorOnlyDeclaration(), kColorElementCount, BufferUsage::None);
    colorBuffer.SetDataRaw(fixture.colors.data(), kColorElementCount, kColorStride);
    VertexBuffer columnBuffer(
        device, ColumnStreamDeclaration(),
        static_cast<int>(fixture.columns.size()), BufferUsage::None);
    columnBuffer.SetDataRaw(
        fixture.columns.data(), static_cast<int>(fixture.columns.size()), kColumnStreamStride);
    VertexBuffer bandBuffer(
        device, BandStreamDeclaration(),
        static_cast<int>(fixture.bands.size()), BufferUsage::None);
    bandBuffer.SetDataRaw(
        fixture.bands.data(), static_cast<int>(fixture.bands.size()), kBandStreamStride);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(fixture.indices16.data(), kMeshElementCount);

    BasicEffect effect(device);
    device.SetIndexBuffer(&indexBuffer);

    for (int frame = 0; frame < 3; ++frame)
    {
        RenderTarget2D target = MakeTarget();
        device.SetVertexBuffers({
            VertexBufferBinding(&positionBuffer, kPositionPrefix, 0),
            VertexBufferBinding(&colorBuffer, kColorPrefix, 0),
            VertexBufferBinding(&columnBuffer, 1, kColumnStreamFrequency),
            VertexBufferBinding(&bandBuffer, 1, kBandStreamFrequency),
        });
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        ApplyMeshEffect(effect);
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 0, 0, kVerticesPerSlot, 0, 1, kInstanceCount);
        device.SetRenderTarget(nullptr);
        device.Present();

        const FrameSnapshot snapshot = CaptureTarget(target);
        ExpectExactlyTheseCells(
            snapshot, layout, CanonicalCells(0),
            (std::string("frame ") + std::to_string(frame)).c_str());
    }
}

// ---------------------------------------------------------------------------
// Coverage item 2: the existing supported instanced baseline, expressed through this file's own
// geometry so a regression in the unified transport shows here as well as in
// InstancedDrawRangeTests. ONE per-vertex stream (the packed 16-byte vertex) plus ONE per-instance
// stream carrying the whole matrix -- the shape every instancing renderer already renders, so this
// leg is NOT gated on MultiStreamVertexInput.
//
// Measured boundaries this leg deliberately stays inside, so that it is a preservation gate on
// EVERY instancing renderer rather than a restatement of open findings:
//
//   * the per-instance VertexOffset is 0 here. Vulkan, bgfx and WebGPU ignore a nonzero one --
//     REMED-GFX-122/123 corrected only EasyGL and D3D11/D3D12 -- which is recorded as
//     REMED-GFX-211 and asserted separately below on the renderers that do honour it.
//   * the colour axis is not asserted here -- only D3D11/D3D12 are still identified as colouring
//     the instanced route from DiffuseColor and neither could be measured (REMED-GFX-212; see
//     ExpectExactlyTheseCellsIgnoringColour, and InstancedVertexColorTests.cpp for the direct
//     per-renderer measurement).
//   * the instance frequency is 1. bgfx implements no per-instance divisor at all
//     (REMED-GFX-213).
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, ClassicSingleVertexAndSingleInstanceStreamIsUnchanged)
{
    // plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this
    // group, so on every other renderer these tests did not exist at all.
    if (!MultiStreamOracle())
        GTEST_SKIP() << "this renderer is outside the MultiStreamOracle set";
    RequireInstancedRendering();

    const GridLayout layout = TargetLayout();
    const std::vector<PositionRecord> positions = BuildPositionStream(layout);
    const std::vector<ColorRecord> colors = BuildColorStream();

    // The packed position+colour vertex every renderer recognizes, built from the same records the
    // split streams carry so the two arrangements are directly comparable.
    struct PackedVertex { PositionRecord position; ColorRecord color; };
    static_assert(sizeof(PackedVertex) == 16);
    std::vector<PackedVertex> packed;
    packed.reserve(static_cast<std::size_t>(kMeshElementCount));
    for (int i = 0; i < kMeshElementCount; ++i)
    {
        packed.push_back(PackedVertex{
            positions[static_cast<std::size_t>(kPositionPrefix + i)],
            colors[static_cast<std::size_t>(kColorPrefix + i)]});
    }

    const VertexDeclaration packedDeclaration(
        16,
        {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
        });

    // One record per instance, in order, at frequency 1 and VertexOffset 0: instance `i` displaces
    // `i` columns and `i / kBandStreamFrequency` bands, which is exactly CanonicalCells(0).
    std::vector<WholeMatrixRecord> matrices;
    for (int i = 0; i < kInstanceCount; ++i)
        matrices.push_back(MakeWholeMatrixRecord(i, i / kBandStreamFrequency));

    VertexBuffer meshBuffer(device, packedDeclaration, kMeshElementCount, BufferUsage::None);
    meshBuffer.SetDataRaw(packed.data(), kMeshElementCount, 16);
    VertexBuffer matrixBuffer(
        device, WholeMatrixDeclaration(),
        static_cast<int>(matrices.size()), BufferUsage::None);
    matrixBuffer.SetDataRaw(
        matrices.data(), static_cast<int>(matrices.size()), 64);
    const std::vector<std::uint16_t> indices = BuildIdentityIndices16();
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshElementCount);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    device.SetVertexBuffers({
        VertexBufferBinding(&meshBuffer, 0, 0),
        VertexBufferBinding(&matrixBuffer, 0, 1),
    });
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 0, 0, kVerticesPerSlot, 0, 1, kInstanceCount);
    device.SetRenderTarget(nullptr);

    const FrameSnapshot snapshot = CaptureTarget(target);
    ExpectExactlyTheseCellsIgnoringColour(
        snapshot, layout, CanonicalCells(0),
        "the classic one-per-vertex + one-per-instance shape is unchanged");
}

// ---------------------------------------------------------------------------
// Coverage items 21 and 22: command-container growth, and destroying a stream's public wrapper
// after its draw has been queued but BEFORE the deferred renderers replay it.
//
// Twelve mixed-stream draws are queued into one bind cycle, alternating between two colour
// streams, which grows every deferred renderer's command container past its initial capacity -- a
// container that stored a pointer INTO itself, or into GraphicsDevice's live binding state, breaks
// exactly here. Then both replaceable wrappers are destroyed while the draws are still queued and
// the readback triggers the replay. The array is captured BY VALUE and every renderer copies the
// concrete native handle, so each draw must still render its own bindings; under ASan this leg is
// a direct use-after-free detector for a stale IVertexBufferRenderer pointer.
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, QueuedMixedStreamDrawsSurviveContainerGrowthAndWrapperDeath)
{
    // plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this
    // group, so on every other renderer these tests did not exist at all.
    if (!MultiStreamOracle())
        GTEST_SKIP() << "this renderer is outside the MultiStreamOracle set";
    RequireInstancedRendering();
    CNA_REQUIRE_MIXED_STREAM_INSTANCING();

    const GridLayout layout = TargetLayout();
    const MixedStreamFixture fixture = BuildMixedStreamFixture(layout);
    const std::vector<ColorRecord> replacementColors =
        BuildColorStream(ColorCode::Prefix, ColorCode::Alternate);

    VertexBuffer positionBuffer(
        device, PositionOnlyDeclaration(), kPositionElementCount, BufferUsage::None);
    positionBuffer.SetDataRaw(fixture.positions.data(), kPositionElementCount, kPositionStride);
    VertexBuffer columnBuffer(
        device, ColumnStreamDeclaration(),
        static_cast<int>(fixture.columns.size()), BufferUsage::None);
    columnBuffer.SetDataRaw(
        fixture.columns.data(), static_cast<int>(fixture.columns.size()), kColumnStreamStride);
    VertexBuffer bandBuffer(
        device, BandStreamDeclaration(),
        static_cast<int>(fixture.bands.size()), BufferUsage::None);
    bandBuffer.SetDataRaw(
        fixture.bands.data(), static_cast<int>(fixture.bands.size()), kBandStreamStride);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(fixture.indices16.data(), kMeshElementCount);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);

    // Heap-allocated so their public wrappers can die while the draws are still queued.
    auto colorBuffer = std::make_unique<VertexBuffer>(
        device, ColorOnlyDeclaration(), kColorElementCount, BufferUsage::None);
    colorBuffer->SetDataRaw(fixture.colors.data(), kColorElementCount, kColorStride);
    auto altColorBuffer = std::make_unique<VertexBuffer>(
        device, ColorOnlyDeclaration(), kColorElementCount, BufferUsage::None);
    altColorBuffer->SetDataRaw(replacementColors.data(), kColorElementCount, kColorStride);

    // Twelve draws: geometry group `g` under the original colour stream for even `g` and the
    // replacement for odd `g`, one instance each so every draw lands in exactly one cell.
    constexpr int kQueuedDraws = 12;
    std::vector<ExpectedCell> expected;
    for (int draw = 0; draw < kQueuedDraws; ++draw)
    {
        const int group = draw % kSlotCount;
        const bool alternate = (draw % 2) != 0;
        device.SetVertexBuffers({
            VertexBufferBinding(&positionBuffer, kPositionPrefix, 0),
            VertexBufferBinding(
                alternate ? altColorBuffer.get() : colorBuffer.get(), kColorPrefix, 0),
            VertexBufferBinding(&columnBuffer, 1, kColumnStreamFrequency),
            VertexBufferBinding(&bandBuffer, 1, kBandStreamFrequency),
        });
        ApplyMeshEffect(effect);
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, group * kVerticesPerSlot, 0, kVerticesPerSlot, 0, 1, 1);
        if (draw < kSlotCount)
        {
            expected.push_back(ExpectedCell{
                group, kLiveBand,
                alternate ? ColorCode::Alternate : LiveCodeForGroup(group)});
        }
    }

    // The wrappers die while every one of those draws is still queued. Unbinding first is what the
    // established lifetime contract requires; the renderers must already hold their own handles.
    device.SetVertexBuffers({});
    device.SetVertexBuffer(nullptr);
    colorBuffer.reset();
    altColorBuffer.reset();

    device.SetRenderTarget(nullptr);
    const FrameSnapshot snapshot = CaptureTarget(target);
    // Draws 8..11 repeat groups 0..3 with the OPPOSITE colour stream, so those four cells are
    // overdrawn and only the last writer's colour is asserted for them.
    for (int draw = kSlotCount; draw < kQueuedDraws; ++draw)
    {
        const int group = draw % kSlotCount;
        expected[static_cast<std::size_t>(group)].code =
            (draw % 2) != 0 ? ColorCode::Alternate : LiveCodeForGroup(group);
    }
    ExpectExactlyTheseCells(
        snapshot, layout, expected,
        "every queued draw kept the complete binding array it was issued under, across container "
        "growth and the death of two of its wrappers");
}

// ---------------------------------------------------------------------------
// The same classic shape WITH a nonzero per-instance VertexOffset, on the renderers REMED-GFX-122,
// REMED-GFX-123 and REMED-GFX-211 corrected. A prefix decoy record displacing five columns and
// three bands sits in front of the live records, so a dropped instance offset lights column 5
// band 3 and loses the last live record -- which is exactly what Vulkan, bgfx and WebGPU each did
// until REMED-GFX-211 corrected them (A/B-proven byte-identical on the pre-REMED-GFX-202
// production tree, and measured at pixel level by the triage group below).
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, ClassicInstanceStreamHonoursItsOwnVertexOffset)
{
    // plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this
    // group, so on every other renderer these tests did not exist at all.
    if (!BindingOffsetOracle())
        GTEST_SKIP() << "this renderer is outside the BindingOffsetOracle set";
    // plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this
    // group, so on every other renderer these tests did not exist at all.
    if (!MultiStreamOracle())
        GTEST_SKIP() << "this renderer is outside the MultiStreamOracle set";
    RequireInstancedRendering();

    const GridLayout layout = TargetLayout();
    const std::vector<PositionRecord> positions = BuildPositionStream(layout);
    const std::vector<ColorRecord> colors = BuildColorStream();

    struct PackedVertex { PositionRecord position; ColorRecord color; };
    static_assert(sizeof(PackedVertex) == 16);
    std::vector<PackedVertex> packed;
    packed.reserve(static_cast<std::size_t>(kMeshElementCount));
    for (int i = 0; i < kMeshElementCount; ++i)
    {
        packed.push_back(PackedVertex{
            positions[static_cast<std::size_t>(kPositionPrefix + i)],
            colors[static_cast<std::size_t>(kColorPrefix + i)]});
    }

    const VertexDeclaration packedDeclaration(
        16,
        {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
        });

    std::vector<WholeMatrixRecord> matrices;
    matrices.push_back(MakeWholeMatrixRecord(kColumnDecoyShift, kBandDecoyShift));
    for (int i = 0; i < kInstanceCount; ++i)
        matrices.push_back(MakeWholeMatrixRecord(i, i / kBandStreamFrequency));

    VertexBuffer meshBuffer(device, packedDeclaration, kMeshElementCount, BufferUsage::None);
    meshBuffer.SetDataRaw(packed.data(), kMeshElementCount, 16);
    VertexBuffer matrixBuffer(
        device, WholeMatrixDeclaration(),
        static_cast<int>(matrices.size()), BufferUsage::None);
    matrixBuffer.SetDataRaw(matrices.data(), static_cast<int>(matrices.size()), 64);
    const std::vector<std::uint16_t> indices = BuildIdentityIndices16();
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshElementCount);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    device.SetVertexBuffers({
        VertexBufferBinding(&meshBuffer, 0, 0),
        VertexBufferBinding(&matrixBuffer, 1, 1),
    });
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 0, 0, kVerticesPerSlot, 0, 1, kInstanceCount);
    device.SetRenderTarget(nullptr);

    const FrameSnapshot snapshot = CaptureTarget(target);
    ExpectExactlyTheseCellsIgnoringColour(
        snapshot, layout, CanonicalCells(0),
        "the per-instance stream's own VertexOffset skips its decoy record");
}

// ===========================================================================
// The CHECKPOINT TRIAGE group (REMED-GFX-211, REMED-GFX-213).
//
// The legs above are gated to the renderers already known to honour the contract they assert. These
// four run on EVERY renderer that rasterizes an instanced draw at all -- the three that were never
// taught to consume `VertexBufferBinding.VertexOffset` on this route included -- and instead of
// asserting one expected frame they CLASSIFY the frame against every reading a defect of this class
// can produce, then print the reading and the full cell map unconditionally. A gate that merely
// excludes a renderer records that somebody once believed it was broken; a leg that names which
// records the renderer actually consumed records what it does, and stays useful when it is fixed.
//
// The shape is deliberately the CLASSIC one: exactly one per-vertex stream and exactly one
// per-instance stream, the arrangement every instancing renderer already accepts and which reaches
// no `MultiStreamVertexInput` capability check by design. Whatever these legs measure is therefore
// measured on a SUPPORTED public path, which is what separates a checkpoint blocker from deferred
// capability work.
// ===========================================================================

namespace
{
    /// The renderer identity a printed measurement names, so a log says which renderer produced
    /// it rather than relying on the reader to remember which build directory it came from.
    /** @brief The active renderer's display name. */
    [[nodiscard]] inline std::string RendererName()
    {
        return std::string(CNA::getGraphicsRendererName(
            CNA::GraphicsRendererSelection::GetSelected()));
    }

    /// The column and band a single per-instance record displaces its geometry by.
    struct InstanceShift
    {
        int column;
        int band;
    };

    /// The triage mesh's own decoy cell. Deliberately NOT the fixture's corner decoy at
    /// (kPositionDecoyColumn, kPositionDecoyBand): a per-vertex offset dropped there displaces its
    /// four instances to columns 7..10 and bands 3..4, so three of the four fall off the target and
    /// the reading loses the resolution that tells "the geometry stream read its decoy" apart from
    /// "the draw rendered almost nothing". From (4, 2) every instance stays on the grid.
    constexpr int kTriageMeshDecoyColumn = 4;
    constexpr int kTriageMeshDecoyBand = 2;

    /// The triage mesh's decoy prefix, and therefore its "skip the decoy" VertexOffset.
    constexpr int kTriageMeshPrefix = kVerticesPerSlot;

    /// The packed 16-byte position+colour vertex every instancing renderer recognizes -- the classic
    /// single per-vertex stream, so nothing here depends on multi-stream input.
    struct TriagePackedVertex
    {
        PositionRecord position;
        ColorRecord color;
    };
    static_assert(sizeof(TriagePackedVertex) == 16);

    VertexDeclaration TriagePackedDeclaration()
    {
        return VertexDeclaration(
            16,
            {
                VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
            });
    }

    /// The triage geometry stream: optionally one decoy triangle at the triage decoy cell, then one
    /// live triangle per column in `kLiveBand`.
    std::vector<TriagePackedVertex> BuildTriagePackedStream(const GridLayout& layout, bool withDecoy)
    {
        std::vector<TriagePackedVertex> packed;
        if (withDecoy)
        {
            const auto decoy = GroupTriangle(layout, kTriageMeshDecoyColumn, kTriageMeshDecoyBand);
            for (int v = 0; v < kVerticesPerSlot; ++v)
                packed.push_back(TriagePackedVertex{
                    decoy[static_cast<std::size_t>(v)], RecordForCode(ColorCode::Prefix)});
        }
        for (int group = 0; group < kSlotCount; ++group)
        {
            const auto triangle = GroupTriangle(layout, group, kLiveBand);
            const ColorRecord color = RecordForCode(LiveCodeForGroup(group));
            for (int v = 0; v < kVerticesPerSlot; ++v)
                packed.push_back(TriagePackedVertex{
                    triangle[static_cast<std::size_t>(v)], color});
        }
        return packed;
    }

    std::vector<WholeMatrixRecord> MatricesFromShifts(const std::vector<InstanceShift>& shifts)
    {
        std::vector<WholeMatrixRecord> matrices;
        matrices.reserve(shifts.size());
        for (const InstanceShift& shift : shifts)
            matrices.push_back(MakeWholeMatrixRecord(shift.column, shift.band));
        return matrices;
    }

    /// The distinct cells @p instanceCount instances light when the geometry group's base cell is
    /// (@p baseColumn, @p baseBand) and instance `i` consumes record
    /// `firstRecord + i / divisor` of @p shifts. A record displaced off the target is clipped and
    /// therefore lights nothing, and a divisor greater than one repeats a cell, so both are folded
    /// out here -- the reading is a SET of lit cells, not a sequence.
    std::vector<ExpectedCell> CellsForDraw(
        const std::vector<InstanceShift>& shifts, int firstRecord, int instanceCount, int divisor,
        int baseColumn, int baseBand)
    {
        std::vector<ExpectedCell> cells;
        for (int i = 0; i < instanceCount; ++i)
        {
            const int record = firstRecord + i / divisor;
            if (record < 0 || static_cast<std::size_t>(record) >= shifts.size())
                continue;
            const int column = baseColumn + shifts[static_cast<std::size_t>(record)].column;
            const int band = baseBand + shifts[static_cast<std::size_t>(record)].band;
            if (column < 0 || column >= kSlotCount || band < 0 || band >= kBandCount)
                continue;
            const bool alreadyListed = std::any_of(
                cells.begin(), cells.end(), [column, band](const ExpectedCell& cell) {
                    return cell.column == column && cell.band == band;
                });
            if (!alreadyListed)
                cells.push_back(ExpectedCell{column, band, ColorCode::Unknown});
        }
        return cells;
    }

    /// One candidate reading of a triage frame: the cells it lights and the name the record keeps.
    struct FrameReading
    {
        const char* name;
        std::vector<ExpectedCell> cells;
    };

    /// True when exactly @p cells are lit and no pixel outside them is. The "nothing else lit"
    /// half is what keeps one reading from matching another's superset.
    bool FrameShowsExactly(
        const FrameSnapshot& snapshot, const GridLayout& layout,
        const std::vector<ExpectedCell>& cells)
    {
        int accounted = 0;
        for (const ExpectedCell& cell : cells)
        {
            const int lit = CountLitInCell(snapshot, layout, cell.column, cell.band);
            if (lit <= 0)
                return false;
            accounted += lit;
        }
        return accounted == snapshot.CountLit();
    }

    std::string ReadFrame(
        const FrameSnapshot& snapshot, const GridLayout& layout,
        const std::vector<FrameReading>& readings)
    {
        for (const FrameReading& reading : readings)
            if (FrameShowsExactly(snapshot, layout, reading.cells))
                return reading.name;
        return "UNCLASSIFIED";
    }

    /// A triage leg that does not print what it measured leaves nothing behind when it is removed,
    /// so every one of them prints its reading AND the full cell map whether it passes or fails.
    void PrintTriageMeasurement(
        const char* ticket, const char* leg, const std::string& reading,
        const FrameSnapshot& snapshot, const GridLayout& layout)
    {
        std::cout << "[  TRIAGE  ] " << ticket << ' ' << RendererName() << ' ' << leg
                  << ": reading = " << reading << DescribeFrame(snapshot, layout) << std::endl;
    }

    /// Runs @p draw and returns an empty string, or the exception text when it is rejected. A
    /// rejection is a legitimate outcome of every leg here -- it is precisely what separates
    /// "declares the limit and refuses" from "accepts and renders the wrong records".
    template <typename Draw>
    std::string RejectionTextOf(Draw&& draw)
    {
        try
        {
            draw();
        }
        catch (const std::exception& e)
        {
            return std::string(e.what());
        }
        return std::string();
    }
}

// ---------------------------------------------------------------------------
// REMED-GFX-211 leg A: the PER-INSTANCE stream's own VertexOffset, on the classic 1+1 shape, with
// the per-vertex offset held at zero so nothing else can move the geometry. One decoy record sits
// in front of the four live ones, so dropping the offset lights the decoy's own cell (5, 3) and
// loses the last live record's (3, 1).
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, ClassicInstanceStreamOffsetIsReadOnEveryInstancingRenderer)
{
    // plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this
    // group, so on every other renderer these tests did not exist at all.
    if (!MultiStreamOracle())
        GTEST_SKIP() << "this renderer is outside the MultiStreamOracle set";
    RequireInstancedRendering();

    const GridLayout layout = TargetLayout();
    const std::vector<TriagePackedVertex> packed = BuildTriagePackedStream(layout, false);

    std::vector<InstanceShift> shifts;
    shifts.push_back(InstanceShift{kColumnDecoyShift, kBandDecoyShift});
    for (int i = 0; i < kInstanceCount; ++i)
        shifts.push_back(InstanceShift{i, i / kBandStreamFrequency});
    const std::vector<WholeMatrixRecord> matrices = MatricesFromShifts(shifts);

    VertexBuffer meshBuffer(
        device, TriagePackedDeclaration(), static_cast<int>(packed.size()), BufferUsage::None);
    meshBuffer.SetDataRaw(packed.data(), static_cast<int>(packed.size()), 16);
    VertexBuffer matrixBuffer(
        device, WholeMatrixDeclaration(), static_cast<int>(matrices.size()), BufferUsage::None);
    matrixBuffer.SetDataRaw(matrices.data(), static_cast<int>(matrices.size()), 64);
    const std::vector<std::uint16_t> indices = BuildIdentityIndices16();
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshElementCount);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    device.SetVertexBuffers({
        VertexBufferBinding(&meshBuffer, 0, 0),
        VertexBufferBinding(&matrixBuffer, 1, 1),
    });
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    const std::string rejection = RejectionTextOf([&] {
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 0, 0, kVerticesPerSlot, 0, 1, kInstanceCount);
    });
    device.SetRenderTarget(nullptr);

    const FrameSnapshot snapshot = CaptureTarget(target);
    const std::string reading = rejection.empty()
        ? ReadFrame(snapshot, layout, {
              {"instance-offset-honoured", CellsForDraw(shifts, 1, kInstanceCount, 1, 0, kLiveBand)},
              {"instance-offset-ignored", CellsForDraw(shifts, 0, kInstanceCount, 1, 0, kLiveBand)},
          })
        : "rejected: " + rejection;
    PrintTriageMeasurement("REMED-GFX-211", "legA(instance-offset)", reading, snapshot, layout);

    if (BindingOffsetOracle())
    {
        EXPECT_EQ(std::string("instance-offset-honoured"), reading)
            << "REMED-GFX-122/123/211/213 corrected this renderer: the per-instance binding's own VertexOffset "
               "must select records 1..4, not 0..3"
            << DescribeFrame(snapshot, layout);
    }
    else
    {
        EXPECT_NE(std::string("UNCLASSIFIED"), reading)
            << "REMED-GFX-211 triage on " << RendererName() << ": the frame matched no reading "
               "this leg can name, so the measurement is not decisive and the ticket cannot be "
               "classified from it"
            << DescribeFrame(snapshot, layout);
    }
}

// ---------------------------------------------------------------------------
// REMED-GFX-211 leg B: the PER-VERTEX side of the same classic route, isolated. The per-instance
// offset is zero and the geometry stream carries the decoy, so this leg answers -- independently of
// leg A -- whether the geometry binding's own offset survives. The ticket asserts "the per-vertex
// side is equally affected"; nothing in the record measured it.
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, ClassicPerVertexStreamOffsetIsReadOnEveryInstancingRenderer)
{
    // plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this
    // group, so on every other renderer these tests did not exist at all.
    if (!MultiStreamOracle())
        GTEST_SKIP() << "this renderer is outside the MultiStreamOracle set";
    RequireInstancedRendering();

    const GridLayout layout = TargetLayout();
    const std::vector<TriagePackedVertex> packed = BuildTriagePackedStream(layout, true);

    std::vector<InstanceShift> shifts;
    for (int i = 0; i < kInstanceCount; ++i)
        shifts.push_back(InstanceShift{i, i / kBandStreamFrequency});
    const std::vector<WholeMatrixRecord> matrices = MatricesFromShifts(shifts);

    VertexBuffer meshBuffer(
        device, TriagePackedDeclaration(), static_cast<int>(packed.size()), BufferUsage::None);
    meshBuffer.SetDataRaw(packed.data(), static_cast<int>(packed.size()), 16);
    VertexBuffer matrixBuffer(
        device, WholeMatrixDeclaration(), static_cast<int>(matrices.size()), BufferUsage::None);
    matrixBuffer.SetDataRaw(matrices.data(), static_cast<int>(matrices.size()), 64);
    const std::vector<std::uint16_t> indices = BuildIdentityIndices16();
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshElementCount);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    device.SetVertexBuffers({
        VertexBufferBinding(&meshBuffer, kTriageMeshPrefix, 0),
        VertexBufferBinding(&matrixBuffer, 0, 1),
    });
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    const std::string rejection = RejectionTextOf([&] {
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 0, 0, kVerticesPerSlot, 0, 1, kInstanceCount);
    });
    device.SetRenderTarget(nullptr);

    const FrameSnapshot snapshot = CaptureTarget(target);
    const std::string reading = rejection.empty()
        ? ReadFrame(snapshot, layout, {
              {"vertex-offset-honoured",
               CellsForDraw(shifts, 0, kInstanceCount, 1, 0, kLiveBand)},
              {"vertex-offset-ignored",
               CellsForDraw(shifts, 0, kInstanceCount, 1,
                            kTriageMeshDecoyColumn, kTriageMeshDecoyBand)},
          })
        : "rejected: " + rejection;
    PrintTriageMeasurement("REMED-GFX-211", "legB(per-vertex-offset)", reading, snapshot, layout);

    if (BindingOffsetOracle())
    {
        EXPECT_EQ(std::string("vertex-offset-honoured"), reading)
            << "REMED-GFX-122/123/211/213 corrected this renderer: the geometry binding's own VertexOffset must "
               "skip the decoy triangle on the instanced route too"
            << DescribeFrame(snapshot, layout);
    }
    else
    {
        EXPECT_NE(std::string("UNCLASSIFIED"), reading)
            << "REMED-GFX-211 triage on " << RendererName() << ": the frame matched no reading "
               "this leg can name, so the per-vertex side cannot be classified from it"
            << DescribeFrame(snapshot, layout);
    }
}

// ---------------------------------------------------------------------------
// REMED-GFX-211 leg C: BOTH offsets nonzero and DIFFERENT (3 vertices, 1 instance record), so the
// three failures legs A and B cannot see are separated too -- one binding's offset applied to both
// streams, the instance offset applied twice, and both offsets lost together. Two tail records past
// the live four give the double-application reading somewhere real to land.
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, ClassicBothStreamOffsetsAreReadOnEveryInstancingRenderer)
{
    // plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this
    // group, so on every other renderer these tests did not exist at all.
    if (!MultiStreamOracle())
        GTEST_SKIP() << "this renderer is outside the MultiStreamOracle set";
    RequireInstancedRendering();

    const GridLayout layout = TargetLayout();
    const std::vector<TriagePackedVertex> packed = BuildTriagePackedStream(layout, true);

    std::vector<InstanceShift> shifts;
    shifts.push_back(InstanceShift{kColumnDecoyShift, kBandDecoyShift});
    for (int i = 0; i < kInstanceCount; ++i)
        shifts.push_back(InstanceShift{i, i / kBandStreamFrequency});
    shifts.push_back(InstanceShift{6, 2});
    shifts.push_back(InstanceShift{7, 2});
    const std::vector<WholeMatrixRecord> matrices = MatricesFromShifts(shifts);

    VertexBuffer meshBuffer(
        device, TriagePackedDeclaration(), static_cast<int>(packed.size()), BufferUsage::None);
    meshBuffer.SetDataRaw(packed.data(), static_cast<int>(packed.size()), 16);
    VertexBuffer matrixBuffer(
        device, WholeMatrixDeclaration(), static_cast<int>(matrices.size()), BufferUsage::None);
    matrixBuffer.SetDataRaw(matrices.data(), static_cast<int>(matrices.size()), 64);
    const std::vector<std::uint16_t> indices = BuildIdentityIndices16();
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshElementCount);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    device.SetVertexBuffers({
        VertexBufferBinding(&meshBuffer, kTriageMeshPrefix, 0),
        VertexBufferBinding(&matrixBuffer, 1, 1),
    });
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    const std::string rejection = RejectionTextOf([&] {
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 0, 0, kVerticesPerSlot, 0, 1, kInstanceCount);
    });
    device.SetRenderTarget(nullptr);

    const FrameSnapshot snapshot = CaptureTarget(target);
    const std::string reading = rejection.empty()
        ? ReadFrame(snapshot, layout, {
              {"both-offsets-honoured",
               CellsForDraw(shifts, 1, kInstanceCount, 1, 0, kLiveBand)},
              {"instance-offset-ignored",
               CellsForDraw(shifts, 0, kInstanceCount, 1, 0, kLiveBand)},
              {"vertex-offset-ignored",
               CellsForDraw(shifts, 1, kInstanceCount, 1,
                            kTriageMeshDecoyColumn, kTriageMeshDecoyBand)},
              {"both-offsets-ignored",
               CellsForDraw(shifts, 0, kInstanceCount, 1,
                            kTriageMeshDecoyColumn, kTriageMeshDecoyBand)},
              {"instance-offset-applied-twice",
               CellsForDraw(shifts, 2, kInstanceCount, 1, 0, kLiveBand)},
              {"vertex-offset-applied-to-instance-stream",
               CellsForDraw(shifts, kTriageMeshPrefix, kInstanceCount, 1, 0, kLiveBand)},
          })
        : "rejected: " + rejection;
    PrintTriageMeasurement("REMED-GFX-211", "legC(both-offsets)", reading, snapshot, layout);

    if (BindingOffsetOracle())
    {
        EXPECT_EQ(std::string("both-offsets-honoured"), reading)
            << "REMED-GFX-122/123/211/213 corrected this renderer: each binding's own VertexOffset applies to "
               "its own stream, exactly once"
            << DescribeFrame(snapshot, layout);
    }
    else
    {
        EXPECT_NE(std::string("UNCLASSIFIED"), reading)
            << "REMED-GFX-211 triage on " << RendererName() << ": the frame matched none of the "
               "six readings this leg can name"
            << DescribeFrame(snapshot, layout);
    }
}

// ---------------------------------------------------------------------------
// REMED-GFX-213: the per-instance DIVISOR on the classic 1+1 shape. Six instances at
// InstanceFrequency 2 must consume records 0,0,1,1,2,2; a renderer that has no divisor consumes
// 0,1,2,3,4,5 and one that collapses the stream consumes 0 six times, and the three light three
// visibly different cell sets. The instance buffer holds SIX records -- twice the three a correct
// divisor needs -- so neither the shared range gate nor a renderer sizing its own copy by
// `instanceCount` can refuse the draw, which is the exact inference the ticket records as unmeasured.
//
// A frequency-1 control on the same buffer proves the leg can see the divisor-1 sequence at all,
// and a return to frequency 2 afterwards proves the first reading was not stale state.
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, ClassicInstanceFrequencyDivisorIsReadOnEveryInstancingRenderer)
{
    // plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this
    // group, so on every other renderer these tests did not exist at all.
    if (!MultiStreamOracle())
        GTEST_SKIP() << "this renderer is outside the MultiStreamOracle set";
    RequireInstancedRendering();

    const GridLayout layout = TargetLayout();
    const std::vector<TriagePackedVertex> packed = BuildTriagePackedStream(layout, false);

    // Six records, every one in its own cell, so any consumed sequence is readable from one frame.
    const std::vector<InstanceShift> shifts{
        {0, 0}, {1, 0}, {2, 0}, {3, 1}, {4, 1}, {5, 1},
    };
    const std::vector<WholeMatrixRecord> matrices = MatricesFromShifts(shifts);
    constexpr int kDivisorInstances = 6;
    constexpr int kDivisor = 2;

    VertexBuffer meshBuffer(
        device, TriagePackedDeclaration(), static_cast<int>(packed.size()), BufferUsage::None);
    meshBuffer.SetDataRaw(packed.data(), static_cast<int>(packed.size()), 16);
    VertexBuffer matrixBuffer(
        device, WholeMatrixDeclaration(), static_cast<int>(matrices.size()), BufferUsage::None);
    matrixBuffer.SetDataRaw(matrices.data(), static_cast<int>(matrices.size()), 64);
    const std::vector<std::uint16_t> indices = BuildIdentityIndices16();
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshElementCount);

    BasicEffect effect(device);
    device.SetIndexBuffer(&indexBuffer);

    const std::vector<FrameReading> divisorReadings{
        {"divisor-2-honoured",
         CellsForDraw(shifts, 0, kDivisorInstances, kDivisor, 0, kLiveBand)},
        {"divisor-1-silently",
         CellsForDraw(shifts, 0, kDivisorInstances, 1, 0, kLiveBand)},
        {"constant-first-record",
         CellsForDraw(shifts, 0, 1, 1, 0, kLiveBand)},
    };

    const auto drawAtFrequency = [&](int frequency, const char* leg,
                                     const std::vector<FrameReading>& readings) {
        RenderTarget2D target = MakeTarget();
        device.SetVertexBuffers({
            VertexBufferBinding(&meshBuffer, 0, 0),
            VertexBufferBinding(&matrixBuffer, 0, frequency),
        });
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        ApplyMeshEffect(effect);
        const std::string rejection = RejectionTextOf([&] {
            device.DrawInstancedPrimitives(
                PrimitiveType::TriangleList, 0, 0, kVerticesPerSlot, 0, 1, kDivisorInstances);
        });
        device.SetRenderTarget(nullptr);
        const FrameSnapshot snapshot = CaptureTarget(target);
        const std::string reading =
            rejection.empty() ? ReadFrame(snapshot, layout, readings) : "rejected: " + rejection;
        PrintTriageMeasurement("REMED-GFX-213", leg, reading, snapshot, layout);
        return reading;
    };

    const std::string atTwo = drawAtFrequency(kDivisor, "frequency=2", divisorReadings);
    const std::string atOne = drawAtFrequency(
        1, "frequency=1(control)",
        {{"divisor-1-honoured", CellsForDraw(shifts, 0, kDivisorInstances, 1, 0, kLiveBand)},
         {"constant-first-record", CellsForDraw(shifts, 0, 1, 1, 0, kLiveBand)}});
    const std::string atTwoAgain =
        drawAtFrequency(kDivisor, "frequency=2(after-frequency-1)", divisorReadings);

    // The control is the leg's own calibration: a renderer that cannot even advance one record per
    // instance says nothing about a divisor of two.
    EXPECT_EQ(std::string("divisor-1-honoured"), atOne)
        << "the frequency-1 control must consume one record per instance on any renderer that "
           "rasterizes an instanced draw at all; without it the frequency-2 reading means nothing";
    EXPECT_EQ(atTwo, atTwoAgain)
        << "the frequency-2 reading changed after an intervening frequency-1 draw, so the renderer "
           "carries stale per-instance step state across draws";

    if (BindingOffsetOracle())
    {
        EXPECT_EQ(std::string("divisor-2-honoured"), atTwo)
            << "this renderer implements the per-instance divisor -- REMED-GFX-123 by keying D3D11/D3D12's "
               "input layout on the step rate, EasyGL through glVertexAttribDivisor, REMED-GFX-213 by "
               "expanding the grouping into Vulkan's and bgfx's own instance staging copies: six "
               "instances at InstanceFrequency 2 must consume records 0,0,1,1,2,2";
    }
    else
    {
        EXPECT_NE(std::string("UNCLASSIFIED"), atTwo)
            << "REMED-GFX-213 triage on " << RendererName() << ": the frequency-2 frame matched "
               "no reading this leg can name, so the ticket cannot be classified from it";
    }
}

// ---------------------------------------------------------------------------
// REMED-GFX-212: what `BasicEffect.VertexColorEnabled` means on the instanced route, measured
// against the ONE thing that needs no reference at all -- the same renderer's own ordinary route,
// same effect state, same buffer, same triangle, same cell.
//
// The reference makes this draw-call-independent. FNA's `GraphicsDevice.DrawInstancedPrimitives`
// is `ApplyState()` + `PrepareVertexBindingArray(baseVertex)` + the driver call and touches no
// effect state; `BasicEffect.OnApply` derives its shader index from fog, vertex colour, texture and
// lighting only, with no instancing term; and every vertex-colour permutation in BasicEffect.fx
// multiplies `vout.Diffuse *= vin.Color`. The same vertex shader therefore runs for both routes,
// so a renderer whose instanced permutation substitutes DiffuseColor for the bound COLOR0 stream
// disagrees with the reference AND with itself.
//
// One instance, an identity per-instance record and VertexOffset zero everywhere: nothing this leg
// measures can be confused with REMED-GFX-211's offset defect.
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, OrdinaryAndInstancedRoutesAgreeOnVertexColorEnabled)
{
    // plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this
    // group, so on every other renderer these tests did not exist at all.
    if (!MultiStreamOracle())
        GTEST_SKIP() << "this renderer is outside the MultiStreamOracle set";
    RequireInstancedRendering();

    const GridLayout layout = TargetLayout();
    const std::vector<TriagePackedVertex> packed = BuildTriagePackedStream(layout, false);
    const std::vector<WholeMatrixRecord> matrices =
        MatricesFromShifts(std::vector<InstanceShift>{{0, 0}});

    VertexBuffer meshBuffer(
        device, TriagePackedDeclaration(), static_cast<int>(packed.size()), BufferUsage::None);
    meshBuffer.SetDataRaw(packed.data(), static_cast<int>(packed.size()), 16);
    VertexBuffer matrixBuffer(
        device, WholeMatrixDeclaration(), static_cast<int>(matrices.size()), BufferUsage::None);
    matrixBuffer.SetDataRaw(matrices.data(), static_cast<int>(matrices.size()), 64);
    const std::vector<std::uint16_t> indices = BuildIdentityIndices16();
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshElementCount);

    BasicEffect effect(device);
    device.SetIndexBuffer(&indexBuffer);

    // Group 0's triangle carries LiveCodeForGroup(0) -- red -- and DiffuseColor is white, which is
    // no colour code at all. The cell therefore reads red when the bound COLOR0 stream reached the
    // shader and "unknown" when DiffuseColor was substituted for it.
    const auto colourOfCellZero = [&](bool instanced, const char* leg) {
        RenderTarget2D target = MakeTarget();
        if (instanced)
            device.SetVertexBuffers({
                VertexBufferBinding(&meshBuffer, 0, 0),
                VertexBufferBinding(&matrixBuffer, 0, 1),
            });
        else
            device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, 0, 0)});
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        ApplyMeshEffect(effect);
        const std::string rejection = RejectionTextOf([&] {
            if (instanced)
                device.DrawInstancedPrimitives(
                    PrimitiveType::TriangleList, 0, 0, kVerticesPerSlot, 0, 1, 1);
            else
                device.DrawIndexedPrimitives(
                    PrimitiveType::TriangleList, 0, 0, kVerticesPerSlot, 0, 1);
        });
        device.SetRenderTarget(nullptr);
        const FrameSnapshot snapshot = CaptureTarget(target);
        int lit = 0;
        const ColorCode code = DominantCodeInCell(snapshot, layout, 0, kLiveBand, lit);
        const std::string reading = !rejection.empty() ? "rejected: " + rejection
                                  : lit == 0           ? std::string("nothing-rendered")
                                                       : std::string(CodeName(code));
        PrintTriageMeasurement("REMED-GFX-212", leg, reading, snapshot, layout);
        return reading;
    };

    const std::string ordinary = colourOfCellZero(false, "ordinary-route");
    const std::string instanced = colourOfCellZero(true, "instanced-route");

    // The calibration: the ordinary route is the one XNA route whose vertex-colour contract is not
    // in question anywhere in this campaign. If it does not carry the stream colour, this leg is
    // measuring its own bug rather than the instanced route's.
    EXPECT_EQ(std::string(CodeName(LiveCodeForGroup(0))), ordinary)
        << "VertexColorEnabled = true with a bound COLOR0 stream and a white DiffuseColor must "
           "produce the stream's own colour on the ORDINARY route";

    if (CNA_RENDERER_IS(OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, Bgfx, Vulkan, WebGPU))
    {
        // EasyGL and bgfx always honoured it; Vulkan and WebGPU were corrected by REMED-GFX-212, which
        // is why the measured-defect arm this leg used to carry for those two is gone. It carried one
        // for as long as either renderer was known to substitute DiffuseColor for the bound COLOR0
        // stream on this route, asserting that pixel measurement until the correction landed and made
        // the assertion fail -- which is what forced its removal.
        EXPECT_EQ(ordinary, instanced)
            << "REMED-GFX-212: this renderer was measured honouring VertexColorEnabled on BOTH routes, "
               "which is the reference contract -- BasicEffect's shader index has no instancing term, "
               "so the same vertex shader runs for both";
    }
    else
    {
        // D3D9, D3D11 and D3D12: REMED-GFX-212 identifies D3D11/D3D12 from source as colouring the
        // instanced route from DiffuseColor, but no D3D display was reachable from the triage session
        // (SDL reports "x11 not available" under Wine on the Xvfb displays this environment permits),
        // so neither arm above may claim them. The measurement above still prints on any host that can
        // run this renderer, which is the whole evidence the ticket is missing for them.
        SUCCEED() << "unmeasured renderer: ordinary=" << ordinary << " instanced=" << instanced;
    }
}


// ===========================================================================
// The PUBLIC TRANSPORT group. No rasterizer, no capability: every one of these runs on EVERY
// renderer, because a range that leaves a bound buffer is wrong everywhere and must report the same
// public exception everywhere. This is where REMED-GFX-202's red-first reproduction lives.
// ===========================================================================

// ---------------------------------------------------------------------------
// Coverage item 19: a SHORT SECONDARY PER-VERTEX stream. The position stream is long enough for
// the requested window and the colour stream is not. Before REMED-GFX-202 the instanced route
// validated only the buffer named by `currentVertexBuffer_` and the first per-instance binding, so
// this request was ACCEPTED and every renderer was free to read past the colour buffer's end.
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, ShortSecondaryPerVertexStreamIsRejected)
{
    const GridLayout layout = TargetLayout();
    const MixedStreamFixture fixture = BuildMixedStreamFixture(layout);

    VertexBuffer positionBuffer(
        device, PositionOnlyDeclaration(), kPositionElementCount, BufferUsage::None);
    positionBuffer.SetDataRaw(fixture.positions.data(), kPositionElementCount, kPositionStride);
    // Exactly enough for its own prefix plus ONE group -- one group short of the requested window.
    constexpr int kShortColorCount = kColorPrefix + kVerticesPerSlot;
    VertexBuffer shortColorBuffer(
        device, ColorOnlyDeclaration(), kShortColorCount, BufferUsage::None);
    shortColorBuffer.SetDataRaw(fixture.colors.data(), kShortColorCount, kColorStride);
    VertexBuffer columnBuffer(
        device, ColumnStreamDeclaration(),
        static_cast<int>(fixture.columns.size()), BufferUsage::None);
    columnBuffer.SetDataRaw(
        fixture.columns.data(), static_cast<int>(fixture.columns.size()), kColumnStreamStride);
    const std::vector<std::uint16_t> indices = BuildIdentityIndices16();
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshElementCount);

    BasicEffect effect(device);
    device.SetVertexBuffers({
        VertexBufferBinding(&positionBuffer, kPositionPrefix, 0),
        VertexBufferBinding(&shortColorBuffer, kColorPrefix, 0),
        VertexBufferBinding(&columnBuffer, 1, kColumnStreamFrequency),
    });
    device.SetIndexBuffer(&indexBuffer);
    effect.Apply();

    // The declared window is two groups: the position stream holds it, the colour stream does not.
    EXPECT_THROW(
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 0, 0, 2 * kVerticesPerSlot, 0, 1, kInstanceCount),
        System::ArgumentOutOfRangeException)
        << "a per-vertex stream too short for the declared window must be rejected on the "
           "instanced route too, even when stream 0 is long enough";
}

// ---------------------------------------------------------------------------
// Coverage item 20: a SHORT SECOND PER-INSTANCE stream. The first instance stream holds every
// record the draw needs and the second does not. Before REMED-GFX-202 only the FIRST per-instance
// binding was located at all -- the loop `break`s on it -- so this request was ACCEPTED.
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, ShortSecondPerInstanceStreamIsRejected)
{
    const GridLayout layout = TargetLayout();
    const MixedStreamFixture fixture = BuildMixedStreamFixture(layout);

    VertexBuffer positionBuffer(
        device, PositionOnlyDeclaration(), kPositionElementCount, BufferUsage::None);
    positionBuffer.SetDataRaw(fixture.positions.data(), kPositionElementCount, kPositionStride);
    VertexBuffer colorBuffer(
        device, ColorOnlyDeclaration(), kColorElementCount, BufferUsage::None);
    colorBuffer.SetDataRaw(fixture.colors.data(), kColorElementCount, kColorStride);
    VertexBuffer columnBuffer(
        device, ColumnStreamDeclaration(),
        static_cast<int>(fixture.columns.size()), BufferUsage::None);
    columnBuffer.SetDataRaw(
        fixture.columns.data(), static_cast<int>(fixture.columns.size()), kColumnStreamStride);
    // One record short: its VertexOffset of 1 plus the two records four instances at frequency two
    // consume needs three, and it holds two.
    VertexBuffer shortBandBuffer(
        device, BandStreamDeclaration(), 2, BufferUsage::None);
    shortBandBuffer.SetDataRaw(fixture.bands.data(), 2, kBandStreamStride);
    const std::vector<std::uint16_t> indices = BuildIdentityIndices16();
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshElementCount);

    BasicEffect effect(device);
    device.SetVertexBuffers({
        VertexBufferBinding(&positionBuffer, kPositionPrefix, 0),
        VertexBufferBinding(&colorBuffer, kColorPrefix, 0),
        VertexBufferBinding(&columnBuffer, 1, kColumnStreamFrequency),
        VertexBufferBinding(&shortBandBuffer, 1, kBandStreamFrequency),
    });
    device.SetIndexBuffer(&indexBuffer);
    effect.Apply();

    EXPECT_THROW(
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 0, 0, kVerticesPerSlot, 0, 1, kInstanceCount),
        System::ArgumentOutOfRangeException)
        << "a per-instance stream past the first must be range-validated too";
}

// ---------------------------------------------------------------------------
// The instance-frequency arithmetic, asserted without a rasterizer. `instanceCount` instances at
// frequency `f` consume exactly `1 + (instanceCount - 1) / f` records, starting at the binding's
// own VertexOffset. One record fewer must be rejected and exactly enough must be accepted, for
// both frequencies -- so a renderer that reads the frequency as a byte stride, ignores it, or
// treats it as one cannot satisfy both halves.
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, InstanceFrequencyFixesTheExactConsumedRecordCount)
{
    const GridLayout layout = TargetLayout();
    const MixedStreamFixture fixture = BuildMixedStreamFixture(layout);

    VertexBuffer positionBuffer(
        device, PositionOnlyDeclaration(), kPositionElementCount, BufferUsage::None);
    positionBuffer.SetDataRaw(fixture.positions.data(), kPositionElementCount, kPositionStride);
    const std::vector<std::uint16_t> indices = BuildIdentityIndices16();
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshElementCount);
    BasicEffect effect(device);
    device.SetIndexBuffer(&indexBuffer);

    // Frequency 3 with 7 instances consumes 1 + 6/3 = 3 records, from VertexOffset 2 onwards.
    constexpr int kFrequency = 3;
    constexpr int kInstances = 7;
    constexpr int kOffset = 2;
    constexpr int kRequired = kOffset + 1 + (kInstances - 1) / kFrequency;

    const std::vector<BandStreamRecord> records(
        static_cast<std::size_t>(kRequired), MakeBandRecord(0));

    VertexBuffer exactBuffer(device, BandStreamDeclaration(), kRequired, BufferUsage::None);
    exactBuffer.SetDataRaw(records.data(), kRequired, kBandStreamStride);
    VertexBuffer shortBuffer(device, BandStreamDeclaration(), kRequired - 1, BufferUsage::None);
    shortBuffer.SetDataRaw(records.data(), kRequired - 1, kBandStreamStride);

    device.SetVertexBuffers({
        VertexBufferBinding(&positionBuffer, kPositionPrefix, 0),
        VertexBufferBinding(&shortBuffer, kOffset, kFrequency),
    });
    effect.Apply();
    EXPECT_THROW(
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 0, 0, kVerticesPerSlot, 0, 1, kInstances),
        System::ArgumentOutOfRangeException)
        << "one record short of `VertexOffset + 1 + (instanceCount - 1) / frequency` must be "
           "rejected";

    device.SetVertexBuffers({
        VertexBufferBinding(&positionBuffer, kPositionPrefix, 0),
        VertexBufferBinding(&exactBuffer, kOffset, kFrequency),
    });
    effect.Apply();
    // Exactly enough: the SHARED layer must accept it. A renderer may still reject afterwards -- it
    // may implement no instanced path at all, or bound its own staging copy more tightly. Those are
    // native capability limits, not verdicts on the public range, so they are told apart by the
    // shared gate's own wording: only it names the offending slot.
    try
    {
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 0, 0, kVerticesPerSlot, 0, 1, kInstances);
    }
    catch (const std::exception& e)
    {
        EXPECT_EQ(nullptr, std::strstr(e.what(), "bound to slot"))
            << "exactly `VertexOffset + 1 + (instanceCount - 1) / frequency` records must be "
               "accepted by the shared range gate, got: " << e.what();
    }
}

// ---------------------------------------------------------------------------
// The declared capability boundary, asserted on EVERY renderer. A renderer that does not claim
// MultiStreamVertexInput must REJECT a mixed-stream instanced draw with System::NotSupportedException
// rather than render it from a subset of the bound streams; one that claims it must accept the
// same call. This is what keeps the skips above from outliving the gap they describe.
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, UnsupportedRendererRejectsMixedStreamInstancingDeterministically)
{
    const GridLayout layout = TargetLayout();
    const MixedStreamFixture fixture = BuildMixedStreamFixture(layout);

    VertexBuffer positionBuffer(
        device, PositionOnlyDeclaration(), kPositionElementCount, BufferUsage::None);
    positionBuffer.SetDataRaw(fixture.positions.data(), kPositionElementCount, kPositionStride);
    VertexBuffer colorBuffer(
        device, ColorOnlyDeclaration(), kColorElementCount, BufferUsage::None);
    colorBuffer.SetDataRaw(fixture.colors.data(), kColorElementCount, kColorStride);
    VertexBuffer columnBuffer(
        device, ColumnStreamDeclaration(),
        static_cast<int>(fixture.columns.size()), BufferUsage::None);
    columnBuffer.SetDataRaw(
        fixture.columns.data(), static_cast<int>(fixture.columns.size()), kColumnStreamStride);
    VertexBuffer bandBuffer(
        device, BandStreamDeclaration(),
        static_cast<int>(fixture.bands.size()), BufferUsage::None);
    bandBuffer.SetDataRaw(
        fixture.bands.data(), static_cast<int>(fixture.bands.size()), kBandStreamStride);
    const std::vector<std::uint16_t> indices = BuildIdentityIndices16();
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(indices.data(), kMeshElementCount);

    BasicEffect effect(device);
    device.SetVertexBuffers({
        VertexBufferBinding(&positionBuffer, kPositionPrefix, 0),
        VertexBufferBinding(&colorBuffer, kColorPrefix, 0),
        VertexBufferBinding(&columnBuffer, 1, kColumnStreamFrequency),
        VertexBufferBinding(&bandBuffer, 1, kBandStreamFrequency),
    });
    device.SetIndexBuffer(&indexBuffer);
    effect.Apply();

    const auto draw = [&] {
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 0, 0, kVerticesPerSlot, 0, 1, kInstanceCount);
    };

    if (!device.SupportsCapability(GraphicsCapability::MultiStreamVertexInput))
    {
        EXPECT_THROW(draw(), System::NotSupportedException)
            << "a renderer that does not claim MultiStreamVertexInput must reject a mixed-stream "
               "instanced draw deterministically, never render it from a subset of the streams";
        return;
    }

    if (MultiStreamOracle())
    {
        EXPECT_NO_THROW(draw())
            << "a renderer that claims MultiStreamVertexInput and implements instanced drawing must "
               "accept a mixed-frequency multi-stream instanced draw";
    }
    else if (CNA_RENDERER_IS(Fna3d))
    {
        // A FIFTH measured outcome (plan_fna3d.md): this renderer claims MultiStreamVertexInput
        // because several PER-VERTEX streams genuinely re-slot -- FNA3D_ApplyVertexBufferBindings
        // takes an array of per-stream declarations natively. Its instancing boundary moved when
        // plan_fx.md's compiled-effect support landed: FNA3D now issues a real instanced draw when
        // the selected effect is a compiled Direct3D 9 Effect whose vertex shader consumes the bound
        // per-instance stream, which is exactly how hardware instancing works in real XNA. This draw
        // selects no such effect, so it still reaches a declared boundary rather than stacking every
        // instance on the first -- but the reason is now "no compiled effect selected", not "this
        // renderer can never instance". XNA's stock effects declare no instance semantics, so they
        // remain on the refusing side of that boundary.
        try
        {
            draw();
            ADD_FAILURE()
                << "this draw selects a stock effect, which declares no per-instance vertex input, "
                   "and was expected to refuse rather than stack every instance on the first";
        }
        catch (const std::exception& e)
        {
            EXPECT_STREQ(
                "FNA3D renderer: instanced drawing requires a compiled XNA Effect whose vertex "
                "shader consumes the bound per-instance stream; stock effects do not declare "
                "instance semantics.",
                e.what())
                << "the refusal must be this renderer's own declared compiled-effect boundary, not a "
                   "different failure";
        }
    }
    else if (CNA_RENDERER_IS(Wicked))
    {
        // A FOURTH measured outcome (plan_wicked.md WICKED-58 / REMED-GFX-202): this renderer claims
        // MultiStreamVertexInput because several PER-VERTEX streams genuinely re-slot, and it
        // implements instanced drawing -- but its instanced vertex programs declare exactly one
        // per-instance record, so a second per-instance stream cannot be expressed. The draw must
        // reach that declared boundary, with the message naming the exact limit -- never a render
        // from a subset of the streams, and never some other failure.
        try
        {
            draw();
            ADD_FAILURE()
                << "this renderer declares exactly one per-instance stream and was expected to refuse "
                   "the second rather than render from a subset of the streams";
        }
        catch (const std::exception& e)
        {
            EXPECT_STREQ(
                "Wicked renderer: only one per-instance VertexBufferBinding is supported (2 were "
                "bound).",
                e.what())
                << "the refusal must be this renderer's own declared one-instance-stream boundary, "
                   "not a different failure";
        }
    }
    else
    {
        // A THIRD, measured outcome, not a silent skip: this renderer claims MultiStreamVertexInput
        // (REMED-GFX-201 taught it ordinary multi-stream input) but overrides no
        // DrawInstancedPrimitivesEx at all, so every instanced draw -- one stream or four -- reaches
        // IGraphicsRenderer's own default. The mixed-stream array must reach that SAME declared
        // boundary and not some other failure; this arm fails the moment such a renderer gains an
        // instanced path, so the boundary cannot outlive itself. REMED-GFX-210 records that CNA has
        // no capability a caller can query for hardware instancing (FNA3D's own
        // FNA3D_SupportsHardwareInstancing), which is why this arm has to be selected by renderer set.
        try
        {
            draw();
            ADD_FAILURE()
                << "this renderer was expected to implement no instanced draw path at all, but the "
                   "mixed-stream draw was accepted -- see REMED-GFX-210";
        }
        catch (const std::exception& e)
        {
            EXPECT_STREQ(
                "DrawInstancedPrimitives is not supported on this graphics renderer.", e.what())
                << "a renderer without an instanced draw path must reach IGraphicsRenderer's own "
                   "declared boundary for a mixed-stream array too, not a different failure";
        }
    }
}

// ---------------------------------------------------------------------------
// The public binding state itself, asserted without a rasterizer so every renderer runs it: every
// slot keeps its own buffer, its own VertexOffset and its own InstanceFrequency, per-vertex and
// per-instance alike, and a mixed-frequency array survives a round trip unchanged.
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, BindingStateKeepsEverySlotsOwnOffsetAndFrequency)
{
    const GridLayout layout = TargetLayout();
    const MixedStreamFixture fixture = BuildMixedStreamFixture(layout);

    VertexBuffer positionBuffer(
        device, PositionOnlyDeclaration(), kPositionElementCount, BufferUsage::None);
    positionBuffer.SetDataRaw(fixture.positions.data(), kPositionElementCount, kPositionStride);
    VertexBuffer colorBuffer(
        device, ColorOnlyDeclaration(), kColorElementCount, BufferUsage::None);
    colorBuffer.SetDataRaw(fixture.colors.data(), kColorElementCount, kColorStride);
    VertexBuffer columnBuffer(
        device, ColumnStreamDeclaration(),
        static_cast<int>(fixture.columns.size()), BufferUsage::None);
    columnBuffer.SetDataRaw(
        fixture.columns.data(), static_cast<int>(fixture.columns.size()), kColumnStreamStride);
    VertexBuffer bandBuffer(
        device, BandStreamDeclaration(),
        static_cast<int>(fixture.bands.size()), BufferUsage::None);
    bandBuffer.SetDataRaw(
        fixture.bands.data(), static_cast<int>(fixture.bands.size()), kBandStreamStride);

    device.SetVertexBuffers({
        VertexBufferBinding(&positionBuffer, kPositionPrefix, 0),
        VertexBufferBinding(&colorBuffer, kColorPrefix, 0),
        VertexBufferBinding(&columnBuffer, 1, kColumnStreamFrequency),
        VertexBufferBinding(&bandBuffer, 2, kBandStreamFrequency),
    });

    const std::vector<VertexBufferBinding> bindings = device.GetVertexBuffers();
    ASSERT_EQ(4u, bindings.size());

    EXPECT_EQ(&positionBuffer, bindings[0].getVertexBufferProperty());
    EXPECT_EQ(kPositionPrefix, bindings[0].getVertexOffsetProperty());
    EXPECT_EQ(0, bindings[0].getInstanceFrequencyProperty());

    EXPECT_EQ(&colorBuffer, bindings[1].getVertexBufferProperty());
    EXPECT_EQ(kColorPrefix, bindings[1].getVertexOffsetProperty());
    EXPECT_EQ(0, bindings[1].getInstanceFrequencyProperty());

    EXPECT_EQ(&columnBuffer, bindings[2].getVertexBufferProperty());
    EXPECT_EQ(1, bindings[2].getVertexOffsetProperty());
    EXPECT_EQ(kColumnStreamFrequency, bindings[2].getInstanceFrequencyProperty());

    EXPECT_EQ(&bandBuffer, bindings[3].getVertexBufferProperty());
    EXPECT_EQ(2, bindings[3].getVertexOffsetProperty());
    EXPECT_EQ(kBandStreamFrequency, bindings[3].getInstanceFrequencyProperty());
}

// ===========================================================================
// REMED-GFX-211 / REMED-GFX-213 permanent matrices, on the CLASSIC 1+1 shape.
//
// The triage group above answers "does this renderer read the two offsets and the divisor at all",
// which is the question that classified the tickets. These legs answer the questions a correction
// has to keep answering: that each term is applied to its OWN stream, exactly ONCE, that it
// survives the deferred capture, the index width, the buffer kind, a return to zero and a return
// to a previous value, and that a divisor other than the two the triage happened to use is
// arithmetic rather than a special case.
//
// The shape stays the classic one-per-vertex + one-per-instance pair throughout, so nothing here
// depends on multi-stream input and every leg runs on every renderer that has been taught the
// contract. Colour is deliberately not asserted: CNA's stock instanced shader takes DiffuseColor
// on several renderers (REMED-GFX-212, open), and WHICH RECORDS each stream supplied -- which is
// the whole question here -- is already carried by the cell positions.
// ===========================================================================
namespace
{
    /// The live instance records every leg below shares: `count` records displacing 0..count-1
    /// columns and nothing vertically, so the column axis alone reads the consumed record.
    std::vector<InstanceShift> LiveColumnShifts(int count)
    {
        std::vector<InstanceShift> shifts;
        for (int i = 0; i < count; ++i)
            shifts.push_back(InstanceShift{i, 0});
        return shifts;
    }

    /// The cells a draw lights when the geometry group's base cell is (`baseColumn`, `kLiveBand`),
    /// expressed in the `ExpectedCell` form the fixture's colour-blind assertion takes.
    std::vector<ExpectedCell> ExpectedCellsForDraw(
        const std::vector<InstanceShift>& shifts, int firstRecord, int instanceCount, int divisor,
        int baseColumn)
    {
        return CellsForDraw(shifts, firstRecord, instanceCount, divisor, baseColumn, kLiveBand);
    }

    /// The whole classic fixture in one object, so a leg that varies only the draw arguments does
    /// not repeat six buffer constructions.
    struct ClassicOffsetFixture
    {
        std::vector<TriagePackedVertex> packed;
        std::vector<WholeMatrixRecord> matrices;
        std::vector<std::uint16_t> indices16;
        std::vector<std::uint32_t> indices32;
    };

    ClassicOffsetFixture BuildClassicOffsetFixture(
        const GridLayout& layout, bool meshHasDecoy, const std::vector<InstanceShift>& shifts)
    {
        return ClassicOffsetFixture{
            BuildTriagePackedStream(layout, meshHasDecoy), MatricesFromShifts(shifts),
            BuildIdentityIndices16(), BuildIdentityIndices32()};
    }
}

// ---------------------------------------------------------------------------
// baseVertex and the geometry binding's VertexOffset are DIFFERENT terms that both advance the
// per-vertex stream, so a renderer that folds one into the other must fold it exactly once. They are
// deliberately different multiples of a group here: losing the offset lands on group 1, losing
// baseVertex on group 0, losing both on the mesh's own decoy cell, doubling the offset on group 3
// and doubling baseVertex on group 4 -- five failures, five distinct frames, one correct one.
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, ClassicBaseVertexAndPerVertexOffsetAddExactlyOnce)
{
    // plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this
    // group, so on every other renderer these tests did not exist at all.
    if (!BindingOffsetOracle())
        GTEST_SKIP() << "this renderer is outside the BindingOffsetOracle set";
    RequireInstancedRendering();

    const GridLayout layout = TargetLayout();
    const std::vector<InstanceShift> shifts = LiveColumnShifts(kInstanceCount);
    const ClassicOffsetFixture fixture = BuildClassicOffsetFixture(layout, true, shifts);

    constexpr int kPerVertexOffset = kTriageMeshPrefix;        // one group: skips the decoy
    constexpr int kBaseVertex      = 2 * kVerticesPerSlot;     // two more groups
    constexpr int kFirstLiveGroup  = 2;                        // (3 + 6 - 3) / 3

    VertexBuffer meshBuffer(
        device, TriagePackedDeclaration(), static_cast<int>(fixture.packed.size()),
        BufferUsage::None);
    meshBuffer.SetDataRaw(
        fixture.packed.data(), static_cast<int>(fixture.packed.size()), 16);
    VertexBuffer matrixBuffer(
        device, WholeMatrixDeclaration(), static_cast<int>(fixture.matrices.size()),
        BufferUsage::None);
    matrixBuffer.SetDataRaw(
        fixture.matrices.data(), static_cast<int>(fixture.matrices.size()), 64);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(fixture.indices16.data(), kMeshElementCount);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    device.SetVertexBuffers({
        VertexBufferBinding(&meshBuffer, kPerVertexOffset, 0),
        VertexBufferBinding(&matrixBuffer, 0, 1),
    });
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, kBaseVertex, 0, kVerticesPerSlot, 0, 1, kInstanceCount);
    device.SetRenderTarget(nullptr);

    const FrameSnapshot snapshot = CaptureTarget(target);
    ExpectExactlyTheseCellsIgnoringColour(
        snapshot, layout,
        ExpectedCellsForDraw(shifts, 0, kInstanceCount, 1, kFirstLiveGroup),
        "the geometry binding's VertexOffset and baseVertex each advanced the per-vertex stream "
        "exactly once, and neither replaced nor doubled the other");
}

// ---------------------------------------------------------------------------
// The same two offsets under a nonzero startIndex, on BOTH index widths. startIndex selects index
// ELEMENTS and must never reach the vertex streams; the two widths must produce the identical
// frame, since the index format changes only how the same values are stored.
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, ClassicBothOffsetsHoldUnderStartIndexOnBothIndexWidths)
{
    // plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this
    // group, so on every other renderer these tests did not exist at all.
    if (!BindingOffsetOracle())
        GTEST_SKIP() << "this renderer is outside the BindingOffsetOracle set";
    RequireInstancedRendering();

    const GridLayout layout = TargetLayout();

    // One decoy instance record in front of the four live ones, so the instance offset has
    // something to skip.
    std::vector<InstanceShift> shifts;
    shifts.push_back(InstanceShift{kColumnDecoyShift, kBandDecoyShift});
    for (int i = 0; i < kInstanceCount; ++i)
        shifts.push_back(InstanceShift{i, 0});
    const ClassicOffsetFixture fixture = BuildClassicOffsetFixture(layout, true, shifts);

    constexpr int kPerVertexOffset = kTriageMeshPrefix;      // skips the mesh decoy
    constexpr int kInstanceOffset  = 1;                      // skips the instance decoy
    constexpr int kStartIndex      = kVerticesPerSlot;       // one group of index ELEMENTS
    constexpr int kFirstLiveGroup  = 1;                      // startIndex advanced one group

    VertexBuffer meshBuffer(
        device, TriagePackedDeclaration(), static_cast<int>(fixture.packed.size()),
        BufferUsage::None);
    meshBuffer.SetDataRaw(
        fixture.packed.data(), static_cast<int>(fixture.packed.size()), 16);
    VertexBuffer matrixBuffer(
        device, WholeMatrixDeclaration(), static_cast<int>(fixture.matrices.size()),
        BufferUsage::None);
    matrixBuffer.SetDataRaw(
        fixture.matrices.data(), static_cast<int>(fixture.matrices.size()), 64);

    IndexBuffer indexBuffer16(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer16.SetData(fixture.indices16.data(), kMeshElementCount);
    IndexBuffer indexBuffer32(
        device, IndexElementSize::ThirtyTwoBits, kMeshElementCount, BufferUsage::None);
    indexBuffer32.SetData(fixture.indices32.data(), kMeshElementCount);

    BasicEffect effect(device);
    const std::vector<ExpectedCell> expected =
        ExpectedCellsForDraw(shifts, kInstanceOffset, kInstanceCount, 1, kFirstLiveGroup);

    const auto drawWith = [&](IndexBuffer& indexBuffer, const char* label) {
        RenderTarget2D target = MakeTarget();
        device.SetVertexBuffers({
            VertexBufferBinding(&meshBuffer, kPerVertexOffset, 0),
            VertexBufferBinding(&matrixBuffer, kInstanceOffset, 1),
        });
        device.SetIndexBuffer(&indexBuffer);
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        ApplyMeshEffect(effect);
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 0, 0, kVerticesPerSlot, kStartIndex, 1, kInstanceCount);
        device.SetRenderTarget(nullptr);
        const FrameSnapshot snapshot = CaptureTarget(target);
        ExpectExactlyTheseCellsIgnoringColour(snapshot, layout, expected, label);
        return snapshot.CountLit();
    };

    const int lit16 = drawWith(
        indexBuffer16,
        "16-bit indices: startIndex selected index elements only, and both bindings kept their own "
        "VertexOffset");
    const int lit32 = drawWith(
        indexBuffer32,
        "32-bit indices: startIndex selected index elements only, and both bindings kept their own "
        "VertexOffset");
    EXPECT_EQ(lit16, lit32)
        << "the index element width changed how many pixels the same geometry range covered";
}

// ---------------------------------------------------------------------------
// REMED-GFX-213 beyond the frequency the triage happened to measure. Frequency 3 must group in
// threes, a frequency LARGER than the instance count must collapse every instance onto the first
// record it owns, and both must respect the instance binding's own VertexOffset -- which together
// rule out a divisor implemented as a special case for two.
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, ClassicInstanceFrequencyIsArithmeticNotASpecialCase)
{
    // plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this
    // group, so on every other renderer these tests did not exist at all.
    if (!BindingOffsetOracle())
        GTEST_SKIP() << "this renderer is outside the BindingOffsetOracle set";
    RequireInstancedRendering();

    const GridLayout layout = TargetLayout();
    // Six records, each in its own column, so any consumed grouping is readable from one frame.
    const std::vector<InstanceShift> shifts = LiveColumnShifts(6);
    const ClassicOffsetFixture fixture = BuildClassicOffsetFixture(layout, false, shifts);

    VertexBuffer meshBuffer(
        device, TriagePackedDeclaration(), static_cast<int>(fixture.packed.size()),
        BufferUsage::None);
    meshBuffer.SetDataRaw(
        fixture.packed.data(), static_cast<int>(fixture.packed.size()), 16);
    VertexBuffer matrixBuffer(
        device, WholeMatrixDeclaration(), static_cast<int>(fixture.matrices.size()),
        BufferUsage::None);
    matrixBuffer.SetDataRaw(
        fixture.matrices.data(), static_cast<int>(fixture.matrices.size()), 64);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(fixture.indices16.data(), kMeshElementCount);

    BasicEffect effect(device);
    device.SetIndexBuffer(&indexBuffer);

    const auto drawAt = [&](int instanceOffset, int frequency, int instanceCount,
                            const char* label) {
        RenderTarget2D target = MakeTarget();
        device.SetVertexBuffers({
            VertexBufferBinding(&meshBuffer, 0, 0),
            VertexBufferBinding(&matrixBuffer, instanceOffset, frequency),
        });
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        ApplyMeshEffect(effect);
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 0, 0, kVerticesPerSlot, 0, 1, instanceCount);
        device.SetRenderTarget(nullptr);
        const FrameSnapshot snapshot = CaptureTarget(target);
        ExpectExactlyTheseCellsIgnoringColour(
            snapshot, layout,
            ExpectedCellsForDraw(shifts, instanceOffset, instanceCount, frequency, 0), label);
    };

    // Records 0,0,0,1,1,1 -- two distinct cells, not six and not one.
    drawAt(0, 3, 6, "InstanceFrequency 3 grouped six instances into records 0,0,0,1,1,1");
    // Records 1,1,1,2,2,2 -- the divisor is applied to the OFFSET stream, not from record zero.
    drawAt(1, 3, 6, "InstanceFrequency 3 with a nonzero instance VertexOffset consumed records "
                    "1,1,1,2,2,2");
    // A frequency larger than the instance count owes exactly one record.
    drawAt(0, 8, kInstanceCount,
           "an InstanceFrequency larger than the instance count consumed record 0 for every "
           "instance");
    drawAt(2, 8, kInstanceCount,
           "an InstanceFrequency larger than the instance count consumed the OFFSET record for "
           "every instance");
    // Back to one record per instance, on the same buffers, after all of the above.
    drawAt(0, 1, 6, "InstanceFrequency returned to 1 and consumed records 0..5 again");
}

// ---------------------------------------------------------------------------
// Deferred capture. Every draw in one frame is queued and replayed at the end of it, so a renderer
// that keeps a pointer into the mutable binding state -- rather than the values the draw was issued
// under -- renders the LAST draw's offsets and frequency for all of them. Six draws, six different
// (per-vertex offset, instance offset, frequency) triples, each landing in its own cells.
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, QueuedClassicDrawsKeepTheirOwnOffsetsAndFrequencies)
{
    // plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this
    // group, so on every other renderer these tests did not exist at all.
    if (!BindingOffsetOracle())
        GTEST_SKIP() << "this renderer is outside the BindingOffsetOracle set";
    RequireInstancedRendering();

    const GridLayout layout = TargetLayout();
    // Eight instance records, TWO per band: record r displaces `r % 2` columns and `r / 2` bands.
    // Each queued draw below takes its own pair, so the band axis separates the draws from one
    // another while the column axis still reads which of its two records each instance consumed.
    std::vector<InstanceShift> shifts;
    for (int r = 0; r < 8; ++r)
        shifts.push_back(InstanceShift{r % 2, r / 2});
    const ClassicOffsetFixture fixture = BuildClassicOffsetFixture(layout, true, shifts);

    VertexBuffer meshBuffer(
        device, TriagePackedDeclaration(), static_cast<int>(fixture.packed.size()),
        BufferUsage::None);
    meshBuffer.SetDataRaw(
        fixture.packed.data(), static_cast<int>(fixture.packed.size()), 16);
    VertexBuffer matrixBuffer(
        device, WholeMatrixDeclaration(), static_cast<int>(fixture.matrices.size()),
        BufferUsage::None);
    matrixBuffer.SetDataRaw(
        fixture.matrices.data(), static_cast<int>(fixture.matrices.size()), 64);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(fixture.indices16.data(), kMeshElementCount);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);

    // Each queued draw takes its own geometry group (so its cells cannot overlap another's), its
    // own instance offset and its own frequency.
    struct QueuedLeg
    {
        int perVertexOffset;   ///< element offset into the mesh, decoy included
        int instanceOffset;
        int frequency;
        int instanceCount;
    };
    // Geometry group `2k` pairs with instance records `2k, 2k+1`, which live in band `k`, so the
    // four draws occupy four disjoint band/column rectangles: (0,1)x0, (2,3)x1, (4,5)x2, (6,7)x3.
    // Two of them run at frequency 1 and two at frequency 2, and every one of the three values
    // differs from its neighbours', so a draw that adopted another's offsets or step rate would
    // land in another's rectangle or off the target entirely.
    const std::vector<QueuedLeg> legs{
        {kTriageMeshPrefix + 0 * kVerticesPerSlot, 0, 1, 2},
        {kTriageMeshPrefix + 2 * kVerticesPerSlot, 2, 1, 2},
        {kTriageMeshPrefix + 4 * kVerticesPerSlot, 4, 2, 4},
        {kTriageMeshPrefix + 6 * kVerticesPerSlot, 6, 2, 4},
    };

    std::vector<ExpectedCell> expected;
    for (const QueuedLeg& leg : legs)
    {
        device.SetVertexBuffers({
            VertexBufferBinding(&meshBuffer, leg.perVertexOffset, 0),
            VertexBufferBinding(&matrixBuffer, leg.instanceOffset, leg.frequency),
        });
        ApplyMeshEffect(effect);
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 0, 0, kVerticesPerSlot, 0, 1, leg.instanceCount);
        const int baseColumn =
            (leg.perVertexOffset - kTriageMeshPrefix) / kVerticesPerSlot;
        const std::vector<ExpectedCell> cells = ExpectedCellsForDraw(
            shifts, leg.instanceOffset, leg.instanceCount, leg.frequency, baseColumn);
        expected.insert(expected.end(), cells.begin(), cells.end());
    }

    // The public binding state is replaced entirely AFTER every draw is queued: a deferred command
    // holding a pointer into it now sees an empty array rather than any leg's values.
    device.SetVertexBuffers({});
    device.SetVertexBuffer(nullptr);

    device.SetRenderTarget(nullptr);
    const FrameSnapshot snapshot = CaptureTarget(target);
    ExpectExactlyTheseCellsIgnoringColour(
        snapshot, layout, expected,
        "every queued instanced draw kept the per-vertex offset, instance offset and "
        "InstanceFrequency it was issued under, after the public bindings were cleared");
}

// ---------------------------------------------------------------------------
// A nonzero pair, a return to both zero with an ordinary indexed draw in between, and a return to
// the SAME nonzero pair -- the sequence that catches an offset cached into a pipeline, an input
// layout or a per-frame staging arena and never recomputed. The last frame must reproduce the first
// one exactly, and the whole sequence must reproduce across frames.
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, ClassicOffsetsReturnToZeroAndBackAroundOrdinaryDraws)
{
    // plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this
    // group, so on every other renderer these tests did not exist at all.
    if (!BindingOffsetOracle())
        GTEST_SKIP() << "this renderer is outside the BindingOffsetOracle set";
    RequireInstancedRendering();

    const GridLayout layout = TargetLayout();
    std::vector<InstanceShift> shifts;
    shifts.push_back(InstanceShift{kColumnDecoyShift, kBandDecoyShift});
    for (int i = 0; i < kInstanceCount; ++i)
        shifts.push_back(InstanceShift{i, 0});
    const ClassicOffsetFixture fixture = BuildClassicOffsetFixture(layout, true, shifts);

    VertexBuffer meshBuffer(
        device, TriagePackedDeclaration(), static_cast<int>(fixture.packed.size()),
        BufferUsage::None);
    meshBuffer.SetDataRaw(
        fixture.packed.data(), static_cast<int>(fixture.packed.size()), 16);
    VertexBuffer matrixBuffer(
        device, WholeMatrixDeclaration(), static_cast<int>(fixture.matrices.size()),
        BufferUsage::None);
    matrixBuffer.SetDataRaw(
        fixture.matrices.data(), static_cast<int>(fixture.matrices.size()), 64);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(fixture.indices16.data(), kMeshElementCount);

    BasicEffect effect(device);
    device.SetIndexBuffer(&indexBuffer);

    /// One frame: an optional ordinary indexed draw of the mesh's own decoy group, then one
    /// instanced draw under the requested offsets. The ordinary draw is the route REMED-GFX-200
    /// and REMED-GFX-201 own -- running it between the instanced ones proves neither route leaves
    /// state the other reads.
    const auto renderFrame = [&](int perVertexOffset, int instanceOffset, bool withOrdinaryDraw) {
        RenderTarget2D target = MakeTarget();
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        if (withOrdinaryDraw)
        {
            // The ordinary route with its OWN nonzero binding offset, landing on the mesh decoy's
            // cell, which no instanced leg here uses.
            device.SetVertexBuffers({VertexBufferBinding(&meshBuffer, 0, 0)});
            ApplyMeshEffect(effect);
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, kVerticesPerSlot, 0, 1);
        }
        device.SetVertexBuffers({
            VertexBufferBinding(&meshBuffer, perVertexOffset, 0),
            VertexBufferBinding(&matrixBuffer, instanceOffset, 1),
        });
        ApplyMeshEffect(effect);
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 0, 0, kVerticesPerSlot, 0, 1, kInstanceCount);
        device.SetRenderTarget(nullptr);
        return CaptureTarget(target);
    };

    /// The instanced draw's own cells, plus the ordinary draw's decoy cell when it ran.
    const auto expectedFor = [&](int perVertexOffset, int instanceOffset, bool withOrdinaryDraw) {
        const int baseColumn = perVertexOffset == 0
                                   ? kTriageMeshDecoyColumn
                                   : (perVertexOffset - kTriageMeshPrefix) / kVerticesPerSlot;
        const int baseBand = perVertexOffset == 0 ? kTriageMeshDecoyBand : kLiveBand;
        std::vector<ExpectedCell> cells = CellsForDraw(
            shifts, instanceOffset, kInstanceCount, 1, baseColumn, baseBand);
        if (withOrdinaryDraw)
        {
            const bool alreadyListed = std::any_of(
                cells.begin(), cells.end(), [](const ExpectedCell& cell) {
                    return cell.column == kTriageMeshDecoyColumn &&
                           cell.band == kTriageMeshDecoyBand;
                });
            if (!alreadyListed)
                cells.push_back(ExpectedCell{
                    kTriageMeshDecoyColumn, kTriageMeshDecoyBand, ColorCode::Unknown});
        }
        return cells;
    };

    constexpr int kPerVertexOffset = kTriageMeshPrefix;
    constexpr int kInstanceOffset  = 1;

    const FrameSnapshot first = renderFrame(kPerVertexOffset, kInstanceOffset, false);
    ExpectExactlyTheseCellsIgnoringColour(
        first, layout, expectedFor(kPerVertexOffset, kInstanceOffset, false),
        "frame 1: both bindings honoured their own nonzero VertexOffset");

    const FrameSnapshot zeroed = renderFrame(0, 0, true);
    ExpectExactlyTheseCellsIgnoringColour(
        zeroed, layout, expectedFor(0, 0, true),
        "frame 2: both offsets returned to zero around an ordinary indexed draw, so the geometry "
        "stream rendered its decoy group and the instance stream its decoy record");

    const FrameSnapshot restored = renderFrame(kPerVertexOffset, kInstanceOffset, true);
    ExpectExactlyTheseCellsIgnoringColour(
        restored, layout, expectedFor(kPerVertexOffset, kInstanceOffset, true),
        "frame 3: the previous nonzero pair was restored after a zero frame and an ordinary draw");

    const FrameSnapshot repeated = renderFrame(kPerVertexOffset, kInstanceOffset, false);
    ExpectExactlyTheseCellsIgnoringColour(
        repeated, layout, expectedFor(kPerVertexOffset, kInstanceOffset, false),
        "frame 4: repeating frame 1 reproduced it exactly");
    EXPECT_EQ(first.CountLit(), repeated.CountLit())
        << "the same offsets rendered a different number of pixels on a later frame"
        << DescribeFrame(repeated, layout);
}

// ---------------------------------------------------------------------------
// The offsets and the frequency must survive the death of the wrappers that carried them, and must
// not be re-read from a replacement object that happens to reuse the freed address. Both streams
// are heap wrappers destroyed while their draws are still queued, and replacements holding
// DIFFERENT records are allocated immediately afterwards to encourage exactly that reuse.
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, QueuedClassicInstancedDrawSurvivesWrapperDeathAndAddressReuse)
{
    // plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this
    // group, so on every other renderer these tests did not exist at all.
    if (!BindingOffsetOracle())
        GTEST_SKIP() << "this renderer is outside the BindingOffsetOracle set";
    RequireInstancedRendering();

    const GridLayout layout = TargetLayout();
    std::vector<InstanceShift> shifts;
    shifts.push_back(InstanceShift{kColumnDecoyShift, kBandDecoyShift});
    for (int i = 0; i < kInstanceCount; ++i)
        shifts.push_back(InstanceShift{i, 0});
    const ClassicOffsetFixture fixture = BuildClassicOffsetFixture(layout, true, shifts);

    // The replacement instance records displace by a band instead of a column, so a draw that
    // re-read its stream after the swap lands somewhere the assertion below cannot mistake for the
    // correct frame.
    std::vector<InstanceShift> replacementShifts;
    for (int i = 0; i < static_cast<int>(shifts.size()); ++i)
        replacementShifts.push_back(InstanceShift{0, 2});
    const std::vector<WholeMatrixRecord> replacementMatrices =
        MatricesFromShifts(replacementShifts);

    auto meshBuffer = std::make_unique<VertexBuffer>(
        device, TriagePackedDeclaration(), static_cast<int>(fixture.packed.size()),
        BufferUsage::None);
    meshBuffer->SetDataRaw(
        fixture.packed.data(), static_cast<int>(fixture.packed.size()), 16);
    auto matrixBuffer = std::make_unique<VertexBuffer>(
        device, WholeMatrixDeclaration(), static_cast<int>(fixture.matrices.size()),
        BufferUsage::None);
    matrixBuffer->SetDataRaw(
        fixture.matrices.data(), static_cast<int>(fixture.matrices.size()), 64);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(fixture.indices16.data(), kMeshElementCount);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);

    constexpr int kPerVertexOffset = kTriageMeshPrefix + 2 * kVerticesPerSlot;
    constexpr int kInstanceOffset  = 1;
    constexpr int kFrequency       = 2;
    constexpr int kBaseColumn      = 2;

    device.SetVertexBuffers({
        VertexBufferBinding(meshBuffer.get(), kPerVertexOffset, 0),
        VertexBufferBinding(matrixBuffer.get(), kInstanceOffset, kFrequency),
    });
    ApplyMeshEffect(effect);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 0, 0, kVerticesPerSlot, 0, 1, kInstanceCount);

    // Unbind first -- the established lifetime contract -- then destroy both wrappers while the
    // draw is still queued, then allocate replacements of the same shapes so the allocator is free
    // to hand back the addresses just released.
    device.SetVertexBuffers({});
    device.SetVertexBuffer(nullptr);
    meshBuffer.reset();
    matrixBuffer.reset();

    auto reusedMesh = std::make_unique<VertexBuffer>(
        device, TriagePackedDeclaration(), static_cast<int>(fixture.packed.size()),
        BufferUsage::None);
    reusedMesh->SetDataRaw(
        fixture.packed.data(), static_cast<int>(fixture.packed.size()), 16);
    auto reusedMatrices = std::make_unique<VertexBuffer>(
        device, WholeMatrixDeclaration(), static_cast<int>(replacementMatrices.size()),
        BufferUsage::None);
    reusedMatrices->SetDataRaw(
        replacementMatrices.data(), static_cast<int>(replacementMatrices.size()), 64);

    device.SetRenderTarget(nullptr);
    const FrameSnapshot snapshot = CaptureTarget(target);
    ExpectExactlyTheseCellsIgnoringColour(
        snapshot, layout,
        ExpectedCellsForDraw(shifts, kInstanceOffset, kInstanceCount, kFrequency, kBaseColumn),
        "the queued draw kept its own offsets, frequency and record data after both wrappers died "
        "and replacements were allocated over their addresses");
}

// ---------------------------------------------------------------------------
// The same contract on DYNAMIC buffers, whose backing storage a renderer may orphan or re-map
// between SetData calls -- a source offset computed once against a stale mapping is exactly the
// defect this leg would catch.
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, ClassicOffsetsAndFrequencyHoldOnDynamicBuffers)
{
    // plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this
    // group, so on every other renderer these tests did not exist at all.
    if (!BindingOffsetOracle())
        GTEST_SKIP() << "this renderer is outside the BindingOffsetOracle set";
    RequireInstancedRendering();

    const GridLayout layout = TargetLayout();
    std::vector<InstanceShift> shifts;
    shifts.push_back(InstanceShift{kColumnDecoyShift, kBandDecoyShift});
    for (int i = 0; i < kInstanceCount; ++i)
        shifts.push_back(InstanceShift{i, 0});
    const ClassicOffsetFixture fixture = BuildClassicOffsetFixture(layout, true, shifts);

    DynamicVertexBuffer meshBuffer(
        device, TriagePackedDeclaration(), static_cast<int>(fixture.packed.size()),
        BufferUsage::None);
    DynamicVertexBuffer matrixBuffer(
        device, WholeMatrixDeclaration(), static_cast<int>(fixture.matrices.size()),
        BufferUsage::None);
    // Written TWICE, so anything cached from the first upload is stale by the time the draw runs.
    meshBuffer.SetDataRaw(fixture.packed.data(), static_cast<int>(fixture.packed.size()), 16);
    matrixBuffer.SetDataRaw(
        fixture.matrices.data(), static_cast<int>(fixture.matrices.size()), 64);
    meshBuffer.SetDataRaw(fixture.packed.data(), static_cast<int>(fixture.packed.size()), 16);
    matrixBuffer.SetDataRaw(
        fixture.matrices.data(), static_cast<int>(fixture.matrices.size()), 64);

    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(fixture.indices16.data(), kMeshElementCount);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    constexpr int kPerVertexOffset = kTriageMeshPrefix + kVerticesPerSlot;
    constexpr int kInstanceOffset  = 1;
    constexpr int kFrequency       = 2;
    constexpr int kBaseColumn      = 1;

    device.SetVertexBuffers({
        VertexBufferBinding(&meshBuffer, kPerVertexOffset, 0),
        VertexBufferBinding(&matrixBuffer, kInstanceOffset, kFrequency),
    });
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 0, 0, kVerticesPerSlot, 0, 1, kInstanceCount);
    device.SetRenderTarget(nullptr);

    const FrameSnapshot snapshot = CaptureTarget(target);
    ExpectExactlyTheseCellsIgnoringColour(
        snapshot, layout,
        ExpectedCellsForDraw(shifts, kInstanceOffset, kInstanceCount, kFrequency, kBaseColumn),
        "a dynamic per-vertex and a dynamic per-instance buffer, both re-uploaded before the draw, "
        "kept their own VertexOffset and InstanceFrequency");
}

// ---------------------------------------------------------------------------
// The three terms together, at a frequency neither the triage nor the return-to-zero legs use: a
// nonzero per-vertex offset, a nonzero instance offset AND a divisor of three on the same draw.
// Each of the three can only be dropped, doubled or cross-applied into a different cell set --
// losing the instance offset lands on records 0,0,0,1,1,1, losing the geometry offset on the mesh
// decoy, and losing the divisor on records 2..5 rather than 2 and 3.
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, ClassicBothOffsetsHoldAtFrequencyThree)
{
    // plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this
    // group, so on every other renderer these tests did not exist at all.
    if (!BindingOffsetOracle())
        GTEST_SKIP() << "this renderer is outside the BindingOffsetOracle set";
    RequireInstancedRendering();

    const GridLayout layout = TargetLayout();
    const std::vector<InstanceShift> shifts = LiveColumnShifts(6);
    const ClassicOffsetFixture fixture = BuildClassicOffsetFixture(layout, true, shifts);

    constexpr int kPerVertexOffset = kTriageMeshPrefix + 1 * kVerticesPerSlot;
    constexpr int kBaseColumn      = 1;
    constexpr int kInstanceOffset  = 2;
    constexpr int kFrequency       = 3;
    constexpr int kInstances       = 6;

    VertexBuffer meshBuffer(
        device, TriagePackedDeclaration(), static_cast<int>(fixture.packed.size()),
        BufferUsage::None);
    meshBuffer.SetDataRaw(
        fixture.packed.data(), static_cast<int>(fixture.packed.size()), 16);
    VertexBuffer matrixBuffer(
        device, WholeMatrixDeclaration(), static_cast<int>(fixture.matrices.size()),
        BufferUsage::None);
    matrixBuffer.SetDataRaw(
        fixture.matrices.data(), static_cast<int>(fixture.matrices.size()), 64);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(fixture.indices16.data(), kMeshElementCount);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);

    device.SetVertexBuffers({
        VertexBufferBinding(&meshBuffer, kPerVertexOffset, 0),
        VertexBufferBinding(&matrixBuffer, kInstanceOffset, kFrequency),
    });
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    ApplyMeshEffect(effect);
    device.DrawInstancedPrimitives(
        PrimitiveType::TriangleList, 0, 0, kVerticesPerSlot, 0, 1, kInstances);
    device.SetRenderTarget(nullptr);

    const FrameSnapshot snapshot = CaptureTarget(target);
    ExpectExactlyTheseCellsIgnoringColour(
        snapshot, layout,
        ExpectedCellsForDraw(shifts, kInstanceOffset, kInstances, kFrequency, kBaseColumn),
        "both bindings kept their own nonzero VertexOffset while the instance stream advanced once "
        "per three instances");
}

// ---------------------------------------------------------------------------
// The frequency must be a property of each DRAW, not of the frame or of anything cached across
// draws: three queued draws at frequencies 2, 1 and 2, in that order, in ONE frame. A renderer that
// carries the intervening frequency-1 state into the third draw lights band 3 as well; one that
// carries the first draw's 2 into the second loses a cell in band 1. Each draw also has its own
// pair of offsets, so no leg can pass on another's numbers.
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, ClassicFrequencyAlternatesWithinOneFrame)
{
    // plan_runtimerenderer.md RTR-P9-5: was a compile-time fence around this
    // group, so on every other renderer these tests did not exist at all.
    if (!BindingOffsetOracle())
        GTEST_SKIP() << "this renderer is outside the BindingOffsetOracle set";
    RequireInstancedRendering();

    const GridLayout layout = TargetLayout();
    // Eight instance records, TWO per band: record r displaces `r % 2` columns and `r / 2` bands.
    std::vector<InstanceShift> shifts;
    for (int r = 0; r < 8; ++r)
        shifts.push_back(InstanceShift{r % 2, r / 2});
    const ClassicOffsetFixture fixture = BuildClassicOffsetFixture(layout, true, shifts);

    VertexBuffer meshBuffer(
        device, TriagePackedDeclaration(), static_cast<int>(fixture.packed.size()),
        BufferUsage::None);
    meshBuffer.SetDataRaw(
        fixture.packed.data(), static_cast<int>(fixture.packed.size()), 16);
    VertexBuffer matrixBuffer(
        device, WholeMatrixDeclaration(), static_cast<int>(fixture.matrices.size()),
        BufferUsage::None);
    matrixBuffer.SetDataRaw(
        fixture.matrices.data(), static_cast<int>(fixture.matrices.size()), 64);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, kMeshElementCount, BufferUsage::None);
    indexBuffer.SetData(fixture.indices16.data(), kMeshElementCount);

    RenderTarget2D target = MakeTarget();
    BasicEffect effect(device);
    device.SetIndexBuffer(&indexBuffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);

    struct AlternatingLeg
    {
        int baseColumn;      ///< the geometry group this leg renders
        int instanceOffset;
        int frequency;
        int instanceCount;
    };
    // Bands 0, 1 and 2 respectively, so the three legs cannot overlap; the geometry columns are
    // chosen so the mesh decoy's own cell (4, 2) belongs to none of them.
    const std::vector<AlternatingLeg> legs{
        {0, 0, 2, 4},
        {2, 2, 1, 2},
        {5, 4, 2, 4},
    };

    std::vector<ExpectedCell> expected;
    for (const AlternatingLeg& leg : legs)
    {
        device.SetVertexBuffers({
            VertexBufferBinding(
                &meshBuffer, kTriageMeshPrefix + leg.baseColumn * kVerticesPerSlot, 0),
            VertexBufferBinding(&matrixBuffer, leg.instanceOffset, leg.frequency),
        });
        ApplyMeshEffect(effect);
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 0, 0, kVerticesPerSlot, 0, 1, leg.instanceCount);
        const std::vector<ExpectedCell> cells = ExpectedCellsForDraw(
            shifts, leg.instanceOffset, leg.instanceCount, leg.frequency, leg.baseColumn);
        expected.insert(expected.end(), cells.begin(), cells.end());
    }

    device.SetRenderTarget(nullptr);
    const FrameSnapshot snapshot = CaptureTarget(target);
    ExpectExactlyTheseCellsIgnoringColour(
        snapshot, layout, expected,
        "an InstanceFrequency of 2, then 1, then 2 again in the same frame gave each draw its own "
        "step rate and its own offsets");
}

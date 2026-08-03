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
//   every bound stream reaches the backend, per-vertex and per-instance alike
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
// position+colour vertex every backend already recognizes but neither stream alone is a layout any
// backend has. The two per-instance streams split the 4x4 world matrix the stock instanced shader
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
// Backend scope. Three groups, each with its own declared boundary rather than a silent skip:
//
//   1. the PUBLIC TRANSPORT group -- validation, slot identity, capability rejection. Runs on
//      EVERY backend, needs no rasterizer, and is where the red-first reproduction lives: before
//      this task a secondary per-vertex stream and every per-instance stream past the first
//      reached no validation and no backend at all.
//   2. the PRESERVATION group -- the classic one-per-vertex + one-per-instance shape every
//      instancing backend already renders. Must stay byte-identical.
//   3. the MIXED-STREAM PIXEL group -- gated at RUNTIME on
//      GraphicsCapability::MultiStreamVertexInput, with
//      UnsupportedBackendRejectsMixedStreamInstancingDeterministically asserting the opposite
//      arm on every backend that does not claim it, so the skip cannot outlive the gap.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <gtest/gtest.h>

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

// The backends whose stock instanced path actually rasterizes and whose RenderTarget2D::GetData
// reads the result back -- REMED-GFX-118's own permanent suite set. A backend outside it has no
// instanced draw implementation at all (`IGraphicsBackend::DrawInstancedPrimitivesEx`'s default
// throws), which is a pre-existing capability boundary this task neither creates nor closes; the
// public transport group below still runs there and still asserts the shared validation contract.
#if defined(CNA_BACKEND_BGFX) || defined(CNA_BACKEND_EASYGL) || \
    defined(CNA_BACKEND_WEBGPU) || defined(CNA_BACKEND_VULKAN) || \
    defined(CNA_BACKEND_D3D9) || defined(CNA_BACKEND_D3D11) || \
    defined(CNA_BACKEND_D3D12)
#define CNA_INSTANCED_MULTI_STREAM_ORACLE 1
#endif

// The backends whose instanced path was corrected to consume VertexBufferBinding.VertexOffset --
// EasyGL (REMED-GFX-122) and D3D11/D3D12 (REMED-GFX-123). InstancedDrawRangeTests.cpp uses exactly
// this set for the same reason. Vulkan, bgfx and WebGPU still ignore it (REMED-GFX-211).
#if defined(CNA_BACKEND_EASYGL) || defined(CNA_BACKEND_D3D11) || defined(CNA_BACKEND_D3D12)
#define CNA_INSTANCED_BINDING_OFFSET_ORACLE 1
#endif

// The three backends the REMED-GFX-211/213 checkpoint triage MEASURED, at pixel level, dropping
// both bindings' VertexOffset and rendering InstanceFrequency 2 at an effective divisor of one --
// silently, on the classic supported shape. The triage group at the bottom of this file asserts
// exactly that measurement on exactly these backends, so the record is a fact about what they do
// rather than a gate recording that somebody once believed they were broken; each arm fails the
// moment its backend is corrected, which is what forces the arm's own removal. D3D9 is deliberately
// NOT here: it is outside CNA_INSTANCED_BINDING_OFFSET_ORACLE too, but no D3D display was available
// to measure it, and an unmeasured backend must not be asserted either way.
#if defined(CNA_BACKEND_VULKAN) || defined(CNA_BACKEND_BGFX) || defined(CNA_BACKEND_WEBGPU)
#define CNA_INSTANCED_OFFSET_DEFECT_MEASURED 1
#endif

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
    /// backend that applies one binding's offset to both streams cannot land on the right record.
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
    /// frequency instances" half of the contract, which a backend that hardcodes a step rate of
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

    /// Slot 1's record: colour only, in the R8G8B8A8 memory order every backend's `Color`
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
    /// instanced backend already expects. Splitting them across two bindings keeps those usages
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
    /// backend already renders, used by the preservation group and by every leg that varies only
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

        void RequireInstancedRendering()
        {
            if (!device.SupportsCapability(GraphicsCapability::ThreeD))
                GTEST_SKIP() << "Backend explicitly does not support 3D rendering";
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
        /// backend. CNA's stock instanced shader is a CNA extension, not an XNA one, and only
        /// EasyGL's and bgfx's colour it from the per-vertex stream -- Vulkan's, WebGPU's, D3D11's
        /// and D3D12's take `DiffuseColor` instead (recorded as REMED-GFX-212). A cross-backend
        /// preservation gate must therefore assert WHICH RECORDS each stream supplied, which the
        /// cell positions alone already say, and leave the colour axis to the mixed-stream group.
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

/// REMED-GFX-202: a DECLARED boundary, not a silent skip. A backend that has not yet been taught to
/// re-slot its stride-derived input elements across several bindings reports the capability as
/// false and is rejected deterministically by GraphicsDevice;
/// UnsupportedBackendRejectsMixedStreamInstancingDeterministically below asserts exactly that on
/// every backend, and flips to the positive assertion the moment one claims the capability, so this
/// skip cannot outlive the gap it describes. A macro rather than a helper because GTEST_SKIP()
/// returns from the function it is written in.
#define CNA_REQUIRE_MIXED_STREAM_INSTANCING()                                                \
    do {                                                                                     \
        if (!device.SupportsCapability(GraphicsCapability::MultiStreamVertexInput))           \
        {                                                                                    \
            GTEST_SKIP()                                                                     \
                << "Backend reports GraphicsCapability::MultiStreamVertexInput = false: "     \
                   "mixed-frequency multi-stream vertex input is not implemented on this "    \
                   "backend (REMED-GFX-203..208). The draw is rejected with "                 \
                   "System::NotSupportedException rather than rendered from a subset of the " \
                   "bound streams -- see "                                                    \
                   "UnsupportedBackendRejectsMixedStreamInstancingDeterministically.";        \
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

#ifdef CNA_INSTANCED_MULTI_STREAM_ORACLE

// ---------------------------------------------------------------------------
// Coverage items 3, 4, 5, 6, 7, 8, 12, 13, 14: the canonical arrangement. TWO per-vertex streams
// and TWO per-instance streams, at four different strides, with four different nonzero
// VertexOffsets, at two different InstanceFrequencies, over 32-bit indices. Every one of the four
// bound streams supplies an axis no other stream can produce.
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, TwoPerVertexAndTwoPerInstanceStreamsEachSupplyTheirOwnAxis)
{
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
// then an ORDINARY draw between two instanced ones. A deferred backend that re-reads the live
// binding state at replay renders both instanced draws with whichever set was bound last.
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, QueuedDrawsUnderDifferentBindingSetsKeepTheirOwn)
{
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
    // backend that leaves a divisor or an instance binding enabled cannot pass this test.
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
// stream carrying the whole matrix -- the shape every instancing backend already renders, so this
// leg is NOT gated on MultiStreamVertexInput.
//
// Measured boundaries this leg deliberately stays inside, so that it is a preservation gate on
// EVERY instancing backend rather than a restatement of open findings:
//
//   * the per-instance VertexOffset is 0 here. Vulkan, bgfx and WebGPU ignore a nonzero one --
//     REMED-GFX-122/123 corrected only EasyGL and D3D11/D3D12 -- which is recorded as
//     REMED-GFX-211 and asserted separately below on the backends that do honour it.
//   * the colour axis is not asserted (REMED-GFX-212; see
//     ExpectExactlyTheseCellsIgnoringColour).
//   * the instance frequency is 1. bgfx implements no per-instance divisor at all
//     (REMED-GFX-213).
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, ClassicSingleVertexAndSingleInstanceStreamIsUnchanged)
{
    RequireInstancedRendering();

    const GridLayout layout = TargetLayout();
    const std::vector<PositionRecord> positions = BuildPositionStream(layout);
    const std::vector<ColorRecord> colors = BuildColorStream();

    // The packed position+colour vertex every backend recognizes, built from the same records the
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
// after its draw has been queued but BEFORE the deferred backends replay it.
//
// Twelve mixed-stream draws are queued into one bind cycle, alternating between two colour
// streams, which grows every deferred backend's command container past its initial capacity -- a
// container that stored a pointer INTO itself, or into GraphicsDevice's live binding state, breaks
// exactly here. Then both replaceable wrappers are destroyed while the draws are still queued and
// the readback triggers the replay. The array is captured BY VALUE and every backend copies the
// concrete native handle, so each draw must still render its own bindings; under ASan this leg is
// a direct use-after-free detector for a stale IVertexBufferBackend pointer.
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, QueuedMixedStreamDrawsSurviveContainerGrowthAndWrapperDeath)
{
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
    // established lifetime contract requires; the backends must already hold their own handles.
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

#ifdef CNA_INSTANCED_BINDING_OFFSET_ORACLE
// ---------------------------------------------------------------------------
// The same classic shape WITH a nonzero per-instance VertexOffset, on the backends REMED-GFX-122
// and REMED-GFX-123 corrected. A prefix decoy record displacing five columns and three bands sits
// in front of the live records, so a dropped instance offset lights column 5 band 3 and loses the
// last live record -- which is exactly what Vulkan, bgfx and WebGPU still do (REMED-GFX-211,
// A/B-proven byte-identical on the pre-REMED-GFX-202 production tree).
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, ClassicInstanceStreamHonoursItsOwnVertexOffset)
{
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
#endif   // CNA_INSTANCED_BINDING_OFFSET_ORACLE

// ===========================================================================
// The CHECKPOINT TRIAGE group (REMED-GFX-211, REMED-GFX-213).
//
// The legs above are gated to the backends already known to honour the contract they assert. These
// four run on EVERY backend that rasterizes an instanced draw at all -- the three that were never
// taught to consume `VertexBufferBinding.VertexOffset` on this route included -- and instead of
// asserting one expected frame they CLASSIFY the frame against every reading a defect of this class
// can produce, then print the reading and the full cell map unconditionally. A gate that merely
// excludes a backend records that somebody once believed it was broken; a leg that names which
// records the backend actually consumed records what it does, and stays useful when it is fixed.
//
// The shape is deliberately the CLASSIC one: exactly one per-vertex stream and exactly one
// per-instance stream, the arrangement every instancing backend already accepts and which reaches
// no `MultiStreamVertexInput` capability check by design. Whatever these legs measure is therefore
// measured on a SUPPORTED public path, which is what separates a checkpoint blocker from deferred
// capability work.
// ===========================================================================

namespace
{
    /// The compile-time backend identity, so a printed measurement names its own backend rather
    /// than relying on the reader to remember which build directory produced the log.
    constexpr const char* kTriageBackendName =
#if defined(CNA_BACKEND_EASYGL)
        "EasyGL";
#elif defined(CNA_BACKEND_VULKAN)
        "Vulkan";
#elif defined(CNA_BACKEND_BGFX)
        "bgfx";
#elif defined(CNA_BACKEND_WEBGPU)
        "WebGPU";
#elif defined(CNA_BACKEND_D3D9)
        "D3D9";
#elif defined(CNA_BACKEND_D3D11)
        "D3D11";
#elif defined(CNA_BACKEND_D3D12)
        "D3D12";
#else
        "unknown";
#endif

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

    /// The packed 16-byte position+colour vertex every instancing backend recognizes -- the classic
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
        std::cout << "[  TRIAGE  ] " << ticket << ' ' << kTriageBackendName << ' ' << leg
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
TEST_F(InstancedDrawMultiStreamTest, ClassicInstanceStreamOffsetIsReadOnEveryInstancingBackend)
{
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

#ifdef CNA_INSTANCED_BINDING_OFFSET_ORACLE
    EXPECT_EQ(std::string("instance-offset-honoured"), reading)
        << "REMED-GFX-122/123 corrected this backend: the per-instance binding's own VertexOffset "
           "must select records 1..4, not 0..3"
        << DescribeFrame(snapshot, layout);
#elif defined(CNA_INSTANCED_OFFSET_DEFECT_MEASURED)
    EXPECT_EQ(std::string("instance-offset-ignored"), reading)
        << "REMED-GFX-211 boundary on " << kTriageBackendName << ": this backend was measured "
           "consuming instance records 0..3 instead of the requested 1..4 -- the decoy record's "
           "own cell is lit and the last live record is lost. If this now reports the offset "
           "honoured the defect is FIXED here: delete this arm and add the backend to "
           "CNA_INSTANCED_BINDING_OFFSET_ORACLE"
        << DescribeFrame(snapshot, layout);
#else
    EXPECT_NE(std::string("UNCLASSIFIED"), reading)
        << "REMED-GFX-211 triage on " << kTriageBackendName << ": the frame matched no reading "
           "this leg can name, so the measurement is not decisive and the ticket cannot be "
           "classified from it"
        << DescribeFrame(snapshot, layout);
#endif
}

// ---------------------------------------------------------------------------
// REMED-GFX-211 leg B: the PER-VERTEX side of the same classic route, isolated. The per-instance
// offset is zero and the geometry stream carries the decoy, so this leg answers -- independently of
// leg A -- whether the geometry binding's own offset survives. The ticket asserts "the per-vertex
// side is equally affected"; nothing in the record measured it.
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, ClassicPerVertexStreamOffsetIsReadOnEveryInstancingBackend)
{
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

#ifdef CNA_INSTANCED_BINDING_OFFSET_ORACLE
    EXPECT_EQ(std::string("vertex-offset-honoured"), reading)
        << "REMED-GFX-122/123 corrected this backend: the geometry binding's own VertexOffset must "
           "skip the decoy triangle on the instanced route too"
        << DescribeFrame(snapshot, layout);
#elif defined(CNA_INSTANCED_OFFSET_DEFECT_MEASURED)
    EXPECT_EQ(std::string("vertex-offset-ignored"), reading)
        << "REMED-GFX-211 boundary on " << kTriageBackendName << ": the PER-VERTEX side of the "
           "classic instanced route drops its binding's VertexOffset too -- every instance renders "
           "the decoy triangle. If this now reports the offset honoured the defect is FIXED here: "
           "delete this arm and add the backend to CNA_INSTANCED_BINDING_OFFSET_ORACLE"
        << DescribeFrame(snapshot, layout);
#else
    EXPECT_NE(std::string("UNCLASSIFIED"), reading)
        << "REMED-GFX-211 triage on " << kTriageBackendName << ": the frame matched no reading "
           "this leg can name, so the per-vertex side cannot be classified from it"
        << DescribeFrame(snapshot, layout);
#endif
}

// ---------------------------------------------------------------------------
// REMED-GFX-211 leg C: BOTH offsets nonzero and DIFFERENT (3 vertices, 1 instance record), so the
// three failures legs A and B cannot see are separated too -- one binding's offset applied to both
// streams, the instance offset applied twice, and both offsets lost together. Two tail records past
// the live four give the double-application reading somewhere real to land.
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, ClassicBothStreamOffsetsAreReadOnEveryInstancingBackend)
{
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

#ifdef CNA_INSTANCED_BINDING_OFFSET_ORACLE
    EXPECT_EQ(std::string("both-offsets-honoured"), reading)
        << "REMED-GFX-122/123 corrected this backend: each binding's own VertexOffset applies to "
           "its own stream, exactly once"
        << DescribeFrame(snapshot, layout);
#elif defined(CNA_INSTANCED_OFFSET_DEFECT_MEASURED)
    EXPECT_EQ(std::string("both-offsets-ignored"), reading)
        << "REMED-GFX-211 boundary on " << kTriageBackendName << ": BOTH bindings lose their own "
           "VertexOffset together -- the reading is neither of the single-sided failures, and it "
           "is not a cross-applied or doubly-applied offset either, so the two streams fail "
           "independently and a correction must carry both. If this now reports both honoured the "
           "defect is FIXED here: delete this arm and add the backend to "
           "CNA_INSTANCED_BINDING_OFFSET_ORACLE"
        << DescribeFrame(snapshot, layout);
#else
    EXPECT_NE(std::string("UNCLASSIFIED"), reading)
        << "REMED-GFX-211 triage on " << kTriageBackendName << ": the frame matched none of the "
           "six readings this leg can name"
        << DescribeFrame(snapshot, layout);
#endif
}

// ---------------------------------------------------------------------------
// REMED-GFX-213: the per-instance DIVISOR on the classic 1+1 shape. Six instances at
// InstanceFrequency 2 must consume records 0,0,1,1,2,2; a backend that has no divisor consumes
// 0,1,2,3,4,5 and one that collapses the stream consumes 0 six times, and the three light three
// visibly different cell sets. The instance buffer holds SIX records -- twice the three a correct
// divisor needs -- so neither the shared range gate nor a backend sizing its own copy by
// `instanceCount` can refuse the draw, which is the exact inference the ticket records as unmeasured.
//
// A frequency-1 control on the same buffer proves the leg can see the divisor-1 sequence at all,
// and a return to frequency 2 afterwards proves the first reading was not stale state.
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, ClassicInstanceFrequencyDivisorIsReadOnEveryInstancingBackend)
{
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

    // The control is the leg's own calibration: a backend that cannot even advance one record per
    // instance says nothing about a divisor of two.
    EXPECT_EQ(std::string("divisor-1-honoured"), atOne)
        << "the frequency-1 control must consume one record per instance on any backend that "
           "rasterizes an instanced draw at all; without it the frequency-2 reading means nothing";
    EXPECT_EQ(atTwo, atTwoAgain)
        << "the frequency-2 reading changed after an intervening frequency-1 draw, so the backend "
           "carries stale per-instance step state across draws";

#ifdef CNA_INSTANCED_BINDING_OFFSET_ORACLE
    EXPECT_EQ(std::string("divisor-2-honoured"), atTwo)
        << "REMED-GFX-123 keyed this backend's input layout on the step rate: six instances at "
           "InstanceFrequency 2 must consume records 0,0,1,1,2,2";
#elif defined(CNA_INSTANCED_OFFSET_DEFECT_MEASURED)
    EXPECT_EQ(std::string("divisor-1-silently"), atTwo)
        << "REMED-GFX-213 boundary on " << kTriageBackendName << ": an instance buffer holding "
           "twice the records a divisor of two needs passes every range and capacity check, "
           "reaches the native draw, and renders one record per instance -- InstanceFrequency 2 "
           "produces exactly the frame InstanceFrequency 1 does, with no diagnostic. If this now "
           "reports the divisor honoured the defect is FIXED here: delete this arm";
#else
    EXPECT_NE(std::string("UNCLASSIFIED"), atTwo)
        << "REMED-GFX-213 triage on " << kTriageBackendName << ": the frequency-2 frame matched "
           "no reading this leg can name, so the ticket cannot be classified from it";
#endif
}

// ---------------------------------------------------------------------------
// REMED-GFX-212: what `BasicEffect.VertexColorEnabled` means on the instanced route, measured
// against the ONE thing that needs no reference at all -- the same backend's own ordinary route,
// same effect state, same buffer, same triangle, same cell.
//
// The reference makes this draw-call-independent. FNA's `GraphicsDevice.DrawInstancedPrimitives`
// is `ApplyState()` + `PrepareVertexBindingArray(baseVertex)` + the driver call and touches no
// effect state; `BasicEffect.OnApply` derives its shader index from fog, vertex colour, texture and
// lighting only, with no instancing term; and every vertex-colour permutation in BasicEffect.fx
// multiplies `vout.Diffuse *= vin.Color`. The same vertex shader therefore runs for both routes,
// so a backend whose instanced permutation substitutes DiffuseColor for the bound COLOR0 stream
// disagrees with the reference AND with itself.
//
// One instance, an identity per-instance record and VertexOffset zero everywhere: nothing this leg
// measures can be confused with REMED-GFX-211's offset defect.
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, OrdinaryAndInstancedRoutesAgreeOnVertexColorEnabled)
{
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

#if defined(CNA_BACKEND_VULKAN) || defined(CNA_BACKEND_WEBGPU)
    EXPECT_NE(ordinary, instanced)
        << "REMED-GFX-212 boundary on " << kTriageBackendName << ": this backend was measured "
           "colouring the instanced route from DiffuseColor while its own ordinary route colours "
           "from the bound COLOR0 stream, under identical effect state. If the two routes now "
           "agree the defect is FIXED here: delete this arm";
#elif defined(CNA_BACKEND_EASYGL) || defined(CNA_BACKEND_BGFX)
    EXPECT_EQ(ordinary, instanced)
        << "REMED-GFX-212: this backend was measured honouring VertexColorEnabled on BOTH routes, "
           "which is the reference contract -- BasicEffect's shader index has no instancing term, "
           "so the same vertex shader runs for both";
#else
    // D3D9, D3D11 and D3D12: REMED-GFX-212 identifies D3D11/D3D12 from source as colouring the
    // instanced route from DiffuseColor, but no D3D display was reachable from the triage session
    // (SDL reports "x11 not available" under Wine on the Xvfb displays this environment permits),
    // so neither arm above may claim them. The measurement above still prints on any host that can
    // run this backend, which is the whole evidence the ticket is missing for them.
    SUCCEED() << "unmeasured backend: ordinary=" << ordinary << " instanced=" << instanced;
#endif
}

#endif   // CNA_INSTANCED_MULTI_STREAM_ORACLE

// ===========================================================================
// The PUBLIC TRANSPORT group. No rasterizer, no capability: every one of these runs on EVERY
// backend, because a range that leaves a bound buffer is wrong everywhere and must report the same
// public exception everywhere. This is where REMED-GFX-202's red-first reproduction lives.
// ===========================================================================

// ---------------------------------------------------------------------------
// Coverage item 19: a SHORT SECONDARY PER-VERTEX stream. The position stream is long enough for
// the requested window and the colour stream is not. Before REMED-GFX-202 the instanced route
// validated only the buffer named by `currentVertexBuffer_` and the first per-instance binding, so
// this request was ACCEPTED and every backend was free to read past the colour buffer's end.
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
// both frequencies -- so a backend that reads the frequency as a byte stride, ignores it, or
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
    // Exactly enough: the SHARED layer must accept it. A backend may still reject afterwards -- it
    // may implement no instanced path at all, or, like bgfx, size its instance copy by
    // `instanceCount` records because it has no per-instance divisor (REMED-GFX-213). Those are
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
// The declared capability boundary, asserted on EVERY backend. A backend that does not claim
// MultiStreamVertexInput must REJECT a mixed-stream instanced draw with System::NotSupportedException
// rather than render it from a subset of the bound streams; one that claims it must accept the
// same call. This is what keeps the skips above from outliving the gap they describe.
// ---------------------------------------------------------------------------
TEST_F(InstancedDrawMultiStreamTest, UnsupportedBackendRejectsMixedStreamInstancingDeterministically)
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
            << "a backend that does not claim MultiStreamVertexInput must reject a mixed-stream "
               "instanced draw deterministically, never render it from a subset of the streams";
        return;
    }

#ifdef CNA_INSTANCED_MULTI_STREAM_ORACLE
    EXPECT_NO_THROW(draw())
        << "a backend that claims MultiStreamVertexInput and implements instanced drawing must "
           "accept a mixed-frequency multi-stream instanced draw";
#else
    // A THIRD, measured outcome, not a silent skip: this backend claims MultiStreamVertexInput
    // (REMED-GFX-201 taught it ordinary multi-stream input) but overrides no
    // DrawInstancedPrimitivesEx at all, so every instanced draw -- one stream or four -- reaches
    // IGraphicsBackend's own default. The mixed-stream array must reach that SAME declared
    // boundary and not some other failure; this arm fails the moment such a backend gains an
    // instanced path, so the boundary cannot outlive itself. REMED-GFX-210 records that CNA has
    // no capability a caller can query for hardware instancing (FNA3D's own
    // FNA3D_SupportsHardwareInstancing), which is why this arm has to be selected by backend set.
    try
    {
        draw();
        ADD_FAILURE()
            << "this backend was expected to implement no instanced draw path at all, but the "
               "mixed-stream draw was accepted -- see REMED-GFX-210";
    }
    catch (const std::exception& e)
    {
        EXPECT_STREQ(
            "DrawInstancedPrimitives is not supported on this graphics backend.", e.what())
            << "a backend without an instanced draw path must reach IGraphicsBackend's own "
               "declared boundary for a mixed-stream array too, not a different failure";
    }
#endif
}

// ---------------------------------------------------------------------------
// The public binding state itself, asserted without a rasterizer so every backend runs it: every
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

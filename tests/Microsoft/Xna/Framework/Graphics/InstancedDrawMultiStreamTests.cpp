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

    std::vector<WholeMatrixRecord> matrices;
    matrices.push_back(MakeWholeMatrixRecord(kColumnDecoyShift, kBandDecoyShift));
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
    ExpectExactlyTheseCells(
        snapshot, layout, CanonicalCells(0),
        "the classic one-per-vertex + one-per-instance shape is unchanged");
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
    // Exactly enough: the range is legal, so the only reason this may not draw is a backend that
    // implements no instanced path at all, which reports its own runtime_error rather than an
    // argument error.
    try
    {
        device.DrawInstancedPrimitives(
            PrimitiveType::TriangleList, 0, 0, kVerticesPerSlot, 0, 1, kInstances);
    }
    catch (const System::ArgumentOutOfRangeException& e)
    {
        ADD_FAILURE()
            << "exactly `VertexOffset + 1 + (instanceCount - 1) / frequency` records must be "
               "accepted, got: " << e.what();
    }
    catch (const std::exception&)
    {
        // A backend without an instanced implementation, or without this stream shape: not an
        // argument-range verdict, which is all this test asserts.
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

    if (device.SupportsCapability(GraphicsCapability::MultiStreamVertexInput))
    {
        EXPECT_NO_THROW(draw())
            << "a backend that claims MultiStreamVertexInput must accept a mixed-frequency "
               "multi-stream instanced draw";
    }
    else
    {
        EXPECT_THROW(draw(), System::NotSupportedException)
            << "a backend that does not claim MultiStreamVertexInput must reject a mixed-stream "
               "instanced draw deterministically, never render it from a subset of the streams";
    }
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

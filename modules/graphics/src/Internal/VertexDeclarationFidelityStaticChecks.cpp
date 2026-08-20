// SPDX-License-Identifier: MS-PL
//
// plans/plan_gltf.md GLTF-156: the canonical stride table, tied to the GPU stream structs it describes.
//
// `InferredLayoutForStride` states, for each stride the importer can emit, which attribute lives at
// which byte offset. Every renderer reads a vertex buffer through that table. The buffers
// themselves are laid out by the packed GPU stream structs in `BuiltInVertexStreams.hpp` -- the
// same structs every built-in `VertexDeclaration` takes its own offsets from. (The public
// `VertexPositionColor`-style types are NOT those bytes: `Color` alone is 24 bytes of packed-vector
// machinery, so `sizeof(VertexPositionColor)` is 40, not 16. The stream structs are the layout.)
//
// So the table is a *restatement* of a stream layout, and a restatement goes stale silently:
// reorder two fields of `PositionNormalTextureSkinnedStream` and every test still passes while
// every skinned mesh is read with its weights and its UVs swapped. That is precisely the failure
// GLTF-278 recorded.
//
// This translation unit contains no code. It exists so that the coupling is checked by the
// COMPILER: each assertion below pairs a table offset with `offsetof` on the struct that produces
// those bytes, so a field reordering fails the build with the field named, rather than failing a
// test somewhere downstream or -- worse -- passing.
//
// A stride with no stream struct of its own is listed here too, with the reason, so that "not
// asserted" is a visible decision rather than an omission.

#include <cstddef>

#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"

#include "CNA/Internal/Graphics/BuiltInVertexStreams.hpp"

namespace
{
    using namespace Microsoft::Xna::Framework::Graphics;
    using namespace CNA::Internal::Graphics;
    namespace fidelity = CNA::Internal::Graphics::VertexDeclarationFidelityDetail;

    /// The offset the canonical table gives for one usage at one stride, or -1 when the table's
    /// entry for that stride carries no such usage at all -- which is itself a mismatch worth
    /// failing on, and is why this returns a sentinel rather than asserting internally.
    constexpr int TableOffset(const CNA::Internal::Graphics::InferredVertexElement* elements,
                              std::size_t count, VertexElementUsage usage)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            if (elements[i].usage == usage) { return elements[i].offset; }
        }
        return -1;
    }

    // --- stride 16: PositionColorStream -------------------------------------------------------
    static_assert(sizeof(PositionColorStream) == 16,
                  "PositionColorStream no longer matches the canonical stride-16 record");
    static_assert(TableOffset(fidelity::kStride16, 2, VertexElementUsage::Position) ==
                      static_cast<int>(offsetof(PositionColorStream, x)),
                  "stride 16: Position moved in the struct but not in the table");
    static_assert(TableOffset(fidelity::kStride16, 2, VertexElementUsage::Color) ==
                      static_cast<int>(offsetof(PositionColorStream, r)),
                  "stride 16: Color moved in the struct but not in the table");

    // --- stride 20: PositionTextureStream -----------------------------------------------------
    static_assert(sizeof(PositionTextureStream) == 20,
                  "PositionTextureStream no longer matches the canonical stride-20 record");
    static_assert(TableOffset(fidelity::kStride20, 2, VertexElementUsage::Position) ==
                      static_cast<int>(offsetof(PositionTextureStream, x)),
                  "stride 20: Position moved in the struct but not in the table");
    static_assert(TableOffset(fidelity::kStride20, 2, VertexElementUsage::TextureCoordinate) ==
                      static_cast<int>(offsetof(PositionTextureStream, u)),
                  "stride 20: TextureCoordinate moved in the struct but not in the table");

    // --- stride 24: PositionColorTextureStream ------------------------------------------------
    static_assert(sizeof(PositionColorTextureStream) == 24,
                  "PositionColorTextureStream no longer matches the canonical stride-24 record");
    static_assert(TableOffset(fidelity::kStride24, 3, VertexElementUsage::Position) ==
                      static_cast<int>(offsetof(PositionColorTextureStream, x)),
                  "stride 24: Position moved in the struct but not in the table");
    static_assert(TableOffset(fidelity::kStride24, 3, VertexElementUsage::Color) ==
                      static_cast<int>(offsetof(PositionColorTextureStream, r)),
                  "stride 24: Color moved in the struct but not in the table");
    static_assert(TableOffset(fidelity::kStride24, 3, VertexElementUsage::TextureCoordinate) ==
                      static_cast<int>(offsetof(PositionColorTextureStream, u)),
                  "stride 24: TextureCoordinate moved in the struct but not in the table");

    // --- stride 32: PositionNormalTextureStream -----------------------------------------------
    static_assert(sizeof(PositionNormalTextureStream) == 32,
                  "PositionNormalTextureStream no longer matches the canonical stride-32 record");
    static_assert(TableOffset(fidelity::kStride32, 3, VertexElementUsage::Position) ==
                      static_cast<int>(offsetof(PositionNormalTextureStream, x)),
                  "stride 32: Position moved in the struct but not in the table");
    static_assert(TableOffset(fidelity::kStride32, 3, VertexElementUsage::Normal) ==
                      static_cast<int>(offsetof(PositionNormalTextureStream, nx)),
                  "stride 32: Normal moved in the struct but not in the table");
    static_assert(TableOffset(fidelity::kStride32, 3, VertexElementUsage::TextureCoordinate) ==
                      static_cast<int>(offsetof(PositionNormalTextureStream, u)),
                  "stride 32: TextureCoordinate moved in the struct but not in the table");

    // --- stride 48: PositionNormalTangentTextureStream ----------------------------------------
    static_assert(sizeof(PositionNormalTangentTextureStream) == 48,
                  "PositionNormalTangentTextureStream no longer matches the canonical stride-48 "
                  "record");
    static_assert(TableOffset(fidelity::kStride48, 4, VertexElementUsage::Position) ==
                      static_cast<int>(offsetof(PositionNormalTangentTextureStream, x)),
                  "stride 48: Position moved in the struct but not in the table");
    static_assert(TableOffset(fidelity::kStride48, 4, VertexElementUsage::Normal) ==
                      static_cast<int>(offsetof(PositionNormalTangentTextureStream, nx)),
                  "stride 48: Normal moved in the struct but not in the table");
    static_assert(TableOffset(fidelity::kStride48, 4, VertexElementUsage::Tangent) ==
                      static_cast<int>(offsetof(PositionNormalTangentTextureStream, tx)),
                  "stride 48: Tangent moved in the struct but not in the table");
    static_assert(TableOffset(fidelity::kStride48, 4, VertexElementUsage::TextureCoordinate) ==
                      static_cast<int>(
                          offsetof(PositionNormalTangentTextureStream, u)),
                  "stride 48: TextureCoordinate moved in the struct but not in the table");

    // --- stride 60: PositionNormalTangentTexture2ColorStream -------------------------------
    static_assert(sizeof(PositionNormalTangentTexture2ColorStream) == 60,
                  "the rigid PBR+UV1 stream no longer matches its padded stride-60 record");
    static_assert(TableOffset(fidelity::kStride60, 5, VertexElementUsage::Position) ==
                      static_cast<int>(offsetof(PositionNormalTangentTexture2ColorStream, x)));
    static_assert(TableOffset(fidelity::kStride60, 5, VertexElementUsage::Normal) ==
                      static_cast<int>(offsetof(PositionNormalTangentTexture2ColorStream, nx)));
    static_assert(TableOffset(fidelity::kStride60, 5, VertexElementUsage::Tangent) ==
                      static_cast<int>(offsetof(PositionNormalTangentTexture2ColorStream, tx)));
    static_assert(fidelity::kStride60[3].offset ==
                      static_cast<int>(offsetof(PositionNormalTangentTexture2ColorStream, u0)));
    static_assert(fidelity::kStride60[4].offset ==
                      static_cast<int>(offsetof(PositionNormalTangentTexture2ColorStream, u1)));
    // GLTF-462: the four bytes GLTF-182 reserved as padding are the packed COLOR_0 slot now.
    static_assert(TableOffset(fidelity::kStride60, 6, VertexElementUsage::Color) ==
                      static_cast<int>(offsetof(PositionNormalTangentTexture2ColorStream, r)),
                  "stride 60: the colour slot moved in the struct but not in the table");


    // --- stride 52: PositionNormalTextureSkinnedStream ----------------------------------------
    static_assert(sizeof(PositionNormalTextureSkinnedStream) == 52,
                  "PositionNormalTextureSkinnedStream no longer matches the canonical stride-52 "
                  "record");
    static_assert(TableOffset(fidelity::kStride52, 5, VertexElementUsage::Position) ==
                      static_cast<int>(offsetof(PositionNormalTextureSkinnedStream, x)),
                  "stride 52: Position moved in the struct but not in the table");
    static_assert(TableOffset(fidelity::kStride52, 5, VertexElementUsage::Normal) ==
                      static_cast<int>(offsetof(PositionNormalTextureSkinnedStream, nx)),
                  "stride 52: Normal moved in the struct but not in the table");
    static_assert(TableOffset(fidelity::kStride52, 5, VertexElementUsage::TextureCoordinate) ==
                      static_cast<int>(
                          offsetof(PositionNormalTextureSkinnedStream, u)),
                  "stride 52: TextureCoordinate moved in the struct but not in the table");
    static_assert(TableOffset(fidelity::kStride52, 5, VertexElementUsage::BlendWeight) ==
                      static_cast<int>(offsetof(PositionNormalTextureSkinnedStream, w0)),
                  "stride 52: BlendWeight moved in the struct but not in the table");
    static_assert(TableOffset(fidelity::kStride52, 5, VertexElementUsage::BlendIndices) ==
                      static_cast<int>(offsetof(PositionNormalTextureSkinnedStream, i0)),
                  "stride 52: BlendIndices moved in the struct but not in the table");

    // --- stride 68: PositionNormalTangentTextureSkinnedStream ---------------------------------
    static_assert(sizeof(PositionNormalTangentTextureSkinnedStream) == 68,
                  "PositionNormalTangentTextureSkinnedStream no longer matches the canonical "
                  "stride-68 record");
    static_assert(
        TableOffset(fidelity::kStride68, 6, VertexElementUsage::Position) ==
            static_cast<int>(offsetof(PositionNormalTangentTextureSkinnedStream, x)),
        "stride 68: Position moved in the struct but not in the table");
    static_assert(
        TableOffset(fidelity::kStride68, 6, VertexElementUsage::Normal) ==
            static_cast<int>(offsetof(PositionNormalTangentTextureSkinnedStream, nx)),
        "stride 68: Normal moved in the struct but not in the table");
    static_assert(
        TableOffset(fidelity::kStride68, 6, VertexElementUsage::Tangent) ==
            static_cast<int>(offsetof(PositionNormalTangentTextureSkinnedStream, tx)),
        "stride 68: Tangent moved in the struct but not in the table");
    static_assert(
        TableOffset(fidelity::kStride68, 6, VertexElementUsage::TextureCoordinate) ==
            static_cast<int>(
                offsetof(PositionNormalTangentTextureSkinnedStream, u)),
        "stride 68: TextureCoordinate moved in the struct but not in the table");
    static_assert(
        TableOffset(fidelity::kStride68, 6, VertexElementUsage::BlendWeight) ==
            static_cast<int>(offsetof(PositionNormalTangentTextureSkinnedStream, w0)),
        "stride 68: BlendWeight moved in the struct but not in the table");
    static_assert(
        TableOffset(fidelity::kStride68, 6, VertexElementUsage::BlendIndices) ==
            static_cast<int>(offsetof(PositionNormalTangentTextureSkinnedStream, i0)),
        "stride 68: BlendIndices moved in the struct but not in the table");

    // --- stride 76: PositionNormalTangentTextureSkinned2Stream ------------------------------
    static_assert(sizeof(PositionNormalTangentTextureSkinned2Stream) == 76,
                  "the skinned PBR+UV1 stream no longer matches its stride-76 record");
    static_assert(fidelity::kStride76[0].offset ==
                      static_cast<int>(offsetof(PositionNormalTangentTextureSkinned2Stream, x)));
    static_assert(fidelity::kStride76[1].offset ==
                      static_cast<int>(offsetof(PositionNormalTangentTextureSkinned2Stream, nx)));
    static_assert(fidelity::kStride76[2].offset ==
                      static_cast<int>(offsetof(PositionNormalTangentTextureSkinned2Stream, tx)));
    static_assert(fidelity::kStride76[3].offset ==
                      static_cast<int>(offsetof(PositionNormalTangentTextureSkinned2Stream, u0)));
    static_assert(fidelity::kStride76[4].offset ==
                      static_cast<int>(offsetof(PositionNormalTangentTextureSkinned2Stream, w0)));
    static_assert(fidelity::kStride76[5].offset ==
                      static_cast<int>(offsetof(PositionNormalTangentTextureSkinned2Stream, i0)));
    static_assert(fidelity::kStride76[6].offset ==
                      static_cast<int>(offsetof(PositionNormalTangentTextureSkinned2Stream, u1)));

    // --- stride 80: PositionNormalTangentTextureSkinned2ColorStream ---------------------------
    // GLTF-463: the skinned counterpart of stride 60's colour slot.
    static_assert(sizeof(PositionNormalTangentTextureSkinned2ColorStream) == 80,
                  "the skinned PBR + UV1 + colour stream no longer matches its stride-80 record");
    static_assert(TableOffset(fidelity::kStride80, 8, VertexElementUsage::Position) ==
                      static_cast<int>(
                          offsetof(PositionNormalTangentTextureSkinned2ColorStream, x)));
    static_assert(TableOffset(fidelity::kStride80, 8, VertexElementUsage::Normal) ==
                      static_cast<int>(
                          offsetof(PositionNormalTangentTextureSkinned2ColorStream, nx)));
    static_assert(TableOffset(fidelity::kStride80, 8, VertexElementUsage::Tangent) ==
                      static_cast<int>(
                          offsetof(PositionNormalTangentTextureSkinned2ColorStream, tx)));
    static_assert(fidelity::kStride80[3].offset ==
                      static_cast<int>(
                          offsetof(PositionNormalTangentTextureSkinned2ColorStream, u0)));
    static_assert(TableOffset(fidelity::kStride80, 8, VertexElementUsage::BlendWeight) ==
                      static_cast<int>(
                          offsetof(PositionNormalTangentTextureSkinned2ColorStream, w0)));
    static_assert(TableOffset(fidelity::kStride80, 8, VertexElementUsage::BlendIndices) ==
                      static_cast<int>(
                          offsetof(PositionNormalTangentTextureSkinned2ColorStream, i0)));
    static_assert(fidelity::kStride80[6].offset ==
                      static_cast<int>(
                          offsetof(PositionNormalTangentTextureSkinned2ColorStream, u1)));
    static_assert(TableOffset(fidelity::kStride80, 8, VertexElementUsage::Color) ==
                      static_cast<int>(
                          offsetof(PositionNormalTangentTextureSkinned2ColorStream, r)));

    // --- stride 56 -----------------------------------------------------------------------------
    //
    // Not tied to a struct, and the omission is the point of saying so: stride 56 is the skinned
    // stride-52 record with a packed Color appended, and `BuiltInVertexStreams.hpp` has no stream
    // for it -- the importer writes those bytes itself. There is therefore nothing for `offsetof`
    // to check against. Its first five elements are byte-for-byte the stride-52 ones, so the
    // assertions above already pin everything it shares; only the trailing Color at 52 is
    // unpinned, and it is the last field of the record, where a change cannot silently shift
    // anything else.
    static_assert(TableOffset(fidelity::kStride56, 6, VertexElementUsage::Color) == 52,
                  "stride 56: the appended Color is no longer the trailing field at offset 52");
}

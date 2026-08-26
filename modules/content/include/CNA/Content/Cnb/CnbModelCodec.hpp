// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbFormat.hpp"
#include "CNA/Content/Cnb/CnbModelData.hpp"

namespace CNA::Content::Cnb
{
    /** @brief Chunk identifiers defined by the `Model` asset schema (plans/plan_cnb.md `CNBF-071`). */
    namespace CnbModelChunk
    {
        /** @brief `MDLH` -- flags and the counts every other chunk is cross-checked against. Mandatory, one. */
        inline constexpr CnbChunkId Header = MakeChunkId('M', 'D', 'L', 'H');

        /** @brief `MSTR` -- the deduplicated string table every name indexes into. Mandatory, one. */
        inline constexpr CnbChunkId Strings = MakeChunkId('M', 'S', 'T', 'R');

        /** @brief `MBON` -- the scene-graph bone table. Optional, at most one. */
        inline constexpr CnbChunkId Bones = MakeChunkId('M', 'B', 'O', 'N');

        /** @brief `MMSH` -- the mesh table, the part table and the mesh-to-part slot array. Mandatory, one. */
        inline constexpr CnbChunkId Meshes = MakeChunkId('M', 'M', 'S', 'H');

        /** @brief `MMAT` -- the material table. Mandatory, one. */
        inline constexpr CnbChunkId Materials = MakeChunkId('M', 'M', 'A', 'T');

        /** @brief `MVTX` -- one chunk of raw vertex bytes per part, in part order. */
        inline constexpr CnbChunkId VertexData = MakeChunkId('M', 'V', 'T', 'X');

        /** @brief `MIDX` -- one chunk of raw index bytes per part, in part order. */
        inline constexpr CnbChunkId IndexData = MakeChunkId('M', 'I', 'D', 'X');

        /** @brief `MMRP` -- one chunk per part that has morph targets. */
        inline constexpr CnbChunkId MorphData = MakeChunkId('M', 'M', 'R', 'P');

        /** @brief `MSKL` -- the skinning skeleton. Optional, at most one. */
        inline constexpr CnbChunkId Skeleton = MakeChunkId('M', 'S', 'K', 'L');

        /** @brief `MANM` -- embedded animation clips. Optional, at most one. */
        inline constexpr CnbChunkId Animations = MakeChunkId('M', 'A', 'N', 'M');

        /** @brief `MLIT` -- punctual lights. Optional, at most one. */
        inline constexpr CnbChunkId Lights = MakeChunkId('M', 'L', 'I', 'T');
    }

    /** @brief Highest `Model` schema version this build understands. */
    inline constexpr std::uint32_t CnbModelSchemaVersion = 1u;

    /** @brief Bytes one bone occupies in the `MBON` chunk. */
    inline constexpr std::uint32_t CnbModelBoneStride = 72u;

    /** @brief Bytes one mesh row occupies in the `MMSH` chunk. */
    inline constexpr std::uint32_t CnbModelMeshStride = 16u;

    /** @brief Bytes one part row occupies in the `MMSH` chunk. */
    inline constexpr std::uint32_t CnbModelPartStride = 56u;

    /** @brief Bytes one material record occupies in the `MMAT` chunk. */
    inline constexpr std::uint32_t CnbModelMaterialStride = 368u;

    /** @brief Value used where a `u32` index field means "no entry". */
    inline constexpr std::uint32_t CnbNoIndex = 0xFFFFFFFFu;

    /**
     * @brief Encodes a decoded model description as a complete `.cnb` byte image
     *        (plans/plan_cnb.md `CNBF-071`).
     *
     * Vertex, index and morph data become their own chunks, aligned so a future memory-mapped
     * reader can address them directly; every descriptor table is a flat fixed-stride array;
     * every name goes through one deduplicated string table; and every external asset the model
     * refers to goes through the container's `XREF` table, so a dependency scanner can list them
     * without understanding this schema at all.
     *
     * @param model       The model to encode.
     * @param contentName Logical content name recorded in the debug `CMET` chunk; may be empty.
     * @return The complete `.cnb` file bytes.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if @p model is internally
     *         inconsistent (a declared count disagreeing with the bytes supplied, an index out of
     *         range, or a count too large to encode).
     */
    [[nodiscard]] std::vector<std::uint8_t> EncodeModelToCnb(const CnbModelData& model,
                                                              const std::string& contentName = {});

    /**
     * @brief Decodes a model description from a parsed `.cnb` container.
     *
     * @param document A container already validated by CnbDocument::Parse().
     * @return The decoded model description.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the document is not a
     *         `Model`, uses an unsupported schema version, is missing a mandatory chunk, or
     *         carries any count, length, index or enumerator its own declarations do not support.
     */
    [[nodiscard]] CnbModelData DecodeModelFromCnb(const CnbDocument& document);
}

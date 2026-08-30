// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbFormat.hpp"
#include "CNA/Content/Cnb/CnbModelV2Data.hpp"

namespace CNA::Content::Cnb
{
    /** @brief Model schema-2 chunk identifiers. */
    namespace CnbModelV2Chunk
    {
        /** @brief `M2HD` -- fixed header and table counts. */
        inline constexpr CnbChunkId Header = MakeChunkId('M', '2', 'H', 'D');
        /** @brief `M2ST` -- deduplicated schema string table. */
        inline constexpr CnbChunkId Strings = MakeChunkId('M', '2', 'S', 'T');
        /** @brief `M2BN` -- fixed bone rows. */
        inline constexpr CnbChunkId Bones = MakeChunkId('M', '2', 'B', 'N');
        /** @brief `M2MS` -- fixed mesh rows. */
        inline constexpr CnbChunkId Meshes = MakeChunkId('M', '2', 'M', 'S');
        /** @brief `M2PT` -- fixed mesh-part rows. */
        inline constexpr CnbChunkId Parts = MakeChunkId('M', '2', 'P', 'T');
        /** @brief `M2VD` -- declaration rows followed by element rows. */
        inline constexpr CnbChunkId VertexDeclarations = MakeChunkId('M', '2', 'V', 'D');
        /** @brief `M2VR` -- vertex-buffer resource rows. */
        inline constexpr CnbChunkId VertexResources = MakeChunkId('M', '2', 'V', 'R');
        /** @brief `MVTX` -- one raw vertex payload per vertex resource. */
        inline constexpr CnbChunkId VertexData = MakeChunkId('M', 'V', 'T', 'X');
        /** @brief `M2IR` -- index-buffer resource rows. */
        inline constexpr CnbChunkId IndexResources = MakeChunkId('M', '2', 'I', 'R');
        /** @brief `MIDX` -- one raw index payload per index resource. */
        inline constexpr CnbChunkId IndexData = MakeChunkId('M', 'I', 'D', 'X');
        /** @brief `M2FX` -- fixed stock-effect resource rows. */
        inline constexpr CnbChunkId Effects = MakeChunkId('M', '2', 'F', 'X');
    }

    /** @brief Model schema version implemented by this codec. */
    inline constexpr std::uint32_t CnbModelV2SchemaVersion = 2u;
    /** @brief Bytes in the schema-2 header. */
    inline constexpr std::uint32_t CnbModelV2HeaderStride = 64u;
    /** @brief Bytes in one schema-2 bone row. */
    inline constexpr std::uint32_t CnbModelV2BoneStride = 72u;
    /** @brief Bytes in one schema-2 mesh row. */
    inline constexpr std::uint32_t CnbModelV2MeshStride = 32u;
    /** @brief Bytes in one schema-2 part row. */
    inline constexpr std::uint32_t CnbModelV2PartStride = 32u;
    /** @brief Bytes in one schema-2 vertex-declaration row. */
    inline constexpr std::uint32_t CnbModelV2DeclarationStride = 16u;
    /** @brief Bytes in one schema-2 vertex-element row. */
    inline constexpr std::uint32_t CnbModelV2ElementStride = 20u;
    /** @brief Bytes in one schema-2 buffer-resource row. */
    inline constexpr std::uint32_t CnbModelV2ResourceStride = 16u;
    /** @brief Bytes in one schema-2 stock-effect row. */
    inline constexpr std::uint32_t CnbModelV2EffectStride = 96u;
    /** @brief Sentinel for an absent XREF row. */
    inline constexpr std::uint32_t CnbModelV2NoIndex = 0xFFFFFFFFu;

    /**
     * @brief Encodes a fully validated Model schema-2 CPU document.
     *
     * @param model CPU-only graph and resource tables.
     * @param contentName Optional logical name for diagnostic metadata.
     * @return Complete deterministic CNB bytes using Model asset type 5/schema 2.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if any graph, declaration,
     *         resource, effect, count, byte product, or draw window is invalid.
     */
    [[nodiscard]] std::vector<std::uint8_t> EncodeModelV2ToCnb(
        const CnbModelV2Data& model, const std::string& contentName = {});

    /**
     * @brief Decodes and validates a Model schema-2 document without creating GPU objects.
     *
     * @param document Structurally validated CNB container.
     * @return Complete CPU-only schema-2 graph and resource tables.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the asset/schema,
     *         chunks, tables, indices, values, payloads, or draw windows are invalid.
     */
    [[nodiscard]] CnbModelV2Data DecodeModelV2FromCnb(const CnbDocument& document);
}

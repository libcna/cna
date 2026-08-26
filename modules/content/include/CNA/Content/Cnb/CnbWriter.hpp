// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbFormat.hpp"

namespace CNA::Content::Cnb
{
    /**
     * @brief Builds a complete, valid `.cnb` byte image (plans/plan_cnb.md `CNBF-009`).
     *
     * The writer is deterministic by construction: it reads no clock, no random source and no
     * pointer value, it emits chunks in the order they were added, it lays the table of contents
     * out in that same order (which is also ascending-offset order), and it zero-fills every
     * alignment gap. Given identical inputs it therefore produces byte-identical output, which is
     * what `CNBF-033` and `CNBF-064` assert.
     *
     * The container-level `CMET` and `XREF` chunks are always emitted first, ahead of the schema's
     * own chunks, regardless of when they were set. Schemas must therefore address each other's
     * chunks by ordinal within a chunk type (`CnbDocument::FindAll`), never by table-of-contents
     * index -- see `plans/plan_cnb.md` decision `D4`.
     */
    class CnbWriter
    {
    public:
        /**
         * @brief Starts a new `.cnb` image for one asset.
         *
         * @param assetTypeId        The asset type this file will declare; must not be
         *                           CnbAssetTypeId::Invalid.
         * @param assetSchemaVersion The schema version this file will declare; must be at least 1.
         * @throws Microsoft::Xna::Framework::Content::ContentLoadException if either argument is
         *         out of range.
         */
        CnbWriter(std::uint32_t assetTypeId, std::uint32_t assetSchemaVersion);

        /**
         * @brief Sets the optional `CMET` debug metadata chunk.
         *
         * Diagnostic only -- nothing dispatches on it. Both strings are derived from the
         * compiler's inputs, so setting them does not compromise determinism.
         *
         * @param assetTypeName Human-readable type name, e.g. `"Microsoft.Xna.Framework.Curve"`.
         * @param contentName   The logical content name being compiled, or empty.
         */
        void SetMetadata(std::string assetTypeName, std::string contentName);

        /**
         * @brief Sets the optional `XREF` external-reference table.
         *
         * @param references The assets this file refers to by logical name, in the order the
         *                   schema's own indices expect.
         */
        void SetExternalReferences(std::vector<CnbExternalReference> references);

        /**
         * @brief Appends one chunk.
         *
         * @param type      The chunk's four-character identifier; every byte must be printable
         *                  ASCII.
         * @param data      The chunk's bytes. Moved.
         * @param flags     Chunk flags; see CnbChunkFlags.
         * @param alignment Power-of-two byte alignment the chunk's offset will satisfy.
         * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the identifier,
         *         flags or alignment are invalid.
         */
        void AddChunk(CnbChunkId type, std::vector<std::uint8_t> data,
                      std::uint32_t flags = CnbChunkFlags::None, std::uint32_t alignment = 4u);

        /**
         * @brief Number of schema chunks added so far, excluding the container-level ones.
         *
         * @return The count of AddChunk() calls made.
         */
        [[nodiscard]] std::size_t SchemaChunkCount() const noexcept;

        /**
         * @brief Assembles the finished `.cnb` image.
         *
         * @return The complete file bytes.
         * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the result would
         *         exceed what the format can express.
         */
        [[nodiscard]] std::vector<std::uint8_t> Build() const;

        /**
         * @brief Assembles the image and writes it to @p path.
         *
         * @param path Filesystem path to create or overwrite.
         * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the file cannot be
         *         written.
         */
        void WriteToFile(const std::string& path) const;

    private:
        struct PendingChunk
        {
            CnbChunkId type{};
            std::uint32_t flags = 0u;
            std::uint32_t alignment = 4u;
            std::vector<std::uint8_t> data;
        };

        [[nodiscard]] std::vector<PendingChunk> AssembleChunkList() const;

        std::uint32_t assetTypeId_ = 0u;
        std::uint32_t assetSchemaVersion_ = 0u;
        bool hasMetadata_ = false;
        std::string assetTypeName_;
        std::string contentName_;
        bool hasExternalReferences_ = false;
        std::vector<CnbExternalReference> externalReferences_;
        std::vector<PendingChunk> chunks_;
    };
}

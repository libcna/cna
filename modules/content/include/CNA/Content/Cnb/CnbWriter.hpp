// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbChunkCompression.hpp"
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
         * @brief Sets the `CMET` metadata chunk.
         *
         * For a **built-in** asset type this is diagnostic only: dispatch is by the header's
         * numeric identifier, which CNA assigns and freezes.
         *
         * For a **custom** asset type it is not optional and not decorative. A custom identifier
         * is a 31-bit hash of the type name, so the load path proves identity by comparing
         * @p assetTypeName against the name the loader was registered under. Build() refuses a
         * custom-typed file that has no name, or whose name does not hash to the declared
         * identifier (plans/plan_cnb.md `CNBF-H002`).
         *
         * Both strings are derived from the compiler's inputs, so setting them does not compromise
         * determinism.
         *
         * @param assetTypeName The type's canonical name, e.g. `"Microsoft.Xna.Framework.Curve"`.
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
         *         exceed what the format can express, or if the asset type is custom and no
         *         matching canonical type name was set (see SetMetadata()).
         */
        /**
         * @brief Compresses this document's schema chunks with @p codec
         *        (plans/plan_cnb.md `CNBF-105`).
         *
         * **Off by default, and that is a measured default rather than a cautious one.**
         * `docs/cnb-compression-measurements.md` records roughly half off a texture payload, three
         * quarters off audio and six sevenths off vertex data — but decompression only *saves load
         * time* on storage slower than 456–1469 MB/s, so on desktop NVMe it makes loading slower.
         * Size always wins; time only sometimes does.
         *
         * A chunk is emitted compressed only when compression actually made it **smaller**;
         * otherwise it is stored, because a chunk that grew would cost both bytes and
         * decompression time. That decision is per chunk, so a document can hold a compressed
         * 4 MB payload beside a stored 24-byte header.
         *
         * Container-level chunks (`CMET`, `XREF`) are always stored uncompressed: they are small
         * enough that a codec is pure overhead, and an inspector should be able to read a file's
         * identity without the codec being available.
         *
         * @param codec The codec to apply. `CnbCompression::None` restores the default.
         * @param level Codec-specific effort; for Zstandard, 1-19. 3 is the measured sweet spot.
         * @throws std::invalid_argument if this build does not implement @p codec.
         */
        void SetCompression(CnbCompression codec, int level = 3);

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

        /// Codec applied to every chunk this writer emits, and the effort level for it.
        /// CnbCompression::None unless SetCompression() says otherwise.
        CnbCompression compression_ = CnbCompression::None;
        int compressionLevel_ = 3;

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

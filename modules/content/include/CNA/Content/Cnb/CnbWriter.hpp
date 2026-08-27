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
         * Each name is validated when Build() assembles the file, against the container's own
         * rule -- `CnbLogicalNameProblem()`, the same function the reader applies (relative,
         * `/`-separated, well-formed UTF-8, no `..` segment). Sharing the rule is what stops the
         * writer producing a file its own reader would refuse (plans/plan_cnb.md `CNBF-115`).
         *
         * @param references The assets this file refers to by logical name, in the order the
         *                   schema's own indices expect.
         */
        void SetExternalReferences(std::vector<CnbExternalReference> references);

        /**
         * @brief Appends one schema chunk.
         *
         * The container-defined identifiers `CMET` and `XREF` are **refused** here: the writer
         * emits each of them at most once, from SetMetadata() and SetExternalReferences(), and a
         * schema adding one as an ordinary chunk would produce a file carrying two of a singleton
         * the reader requires to be unique -- accepted by Build() and refused by
         * CnbDocument::Parse() (plans/plan_cnb.md `CNBF-115`).
         *
         * @param type      The chunk's four-character identifier; every byte must be printable
         *                  ASCII, and it must not be a container-defined identifier.
         * @param data      The chunk's bytes. Moved.
         * @param flags     Chunk flags; see CnbChunkFlags.
         * @param alignment Power-of-two byte alignment the chunk's offset will satisfy.
         * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the identifier is
         *         malformed or container-defined, or the flags or alignment are invalid.
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
         * Container-level chunks (`CMET`, `XREF`) are always stored uncompressed, because a codec
         * is pure overhead on a chunk that small.
         *
         * That is **not** enough to let a build without the codec inspect such a file
         * (plans/plan_cnb.md `CNBF-121`): CnbDocument::Parse() refuses an unimplemented codec while
         * reading the table of contents, long before any chunk is decoded, so a compressed `.cnb`
         * cannot be opened at all without the codec. Reading a compressed file's identity without
         * one would need a metadata-only parser, which does not exist.
         *
         * @param codec The codec to apply. `CnbCompression::None` restores the default.
         * @param level Codec-specific effort; for Zstandard, 1-19. 3 is the measured sweet spot.
         * @throws std::invalid_argument if this build does not implement @p codec.
         */
        void SetCompression(CnbCompression codec, int level = 3);

        /**
         * @brief Bounds the file this writer will produce, so it cannot exceed what a reader with
         *        @p limits will open (plans/plan_cnb.md `CNBF-122`).
         *
         * Build() applies these the way `CnbDocument::Parse()` applies its own: the number of
         * chunks against `maxChunkCount`, each chunk's stored and logical size against
         * `maxChunkSize`, the sum of every chunk's **logical** size against
         * `maxTotalUncompressedSize`, and the finished image against `maxFileSize`. Together with
         * the chunk-alignment ceiling AddChunk() already enforces, that is every applicable limit
         * a default reader applies to a well-formed file.
         *
         * It matters because compression breaks the intuition that a file a writer built is a file
         * a reader can open. A highly compressible document -- a megabyte of zeros in each of a
         * few hundred chunks -- serializes to very little and expands to a great deal, so before
         * this it could Build() successfully and then be refused by a default
         * `CnbDocument::Parse()` for exceeding the aggregate expansion budget `CNBF-114` added.
         * The producer is the right place to find that out.
         *
         * Defaults to DefaultCnbReadLimits(), so a writer that says nothing produces files the
         * default reader accepts. Tests inject small limits to exercise the boundaries without
         * allocating anything large.
         *
         * @param limits The reader limits this writer must stay inside.
         */
        void SetLimits(const CnbReadLimits& limits);

        /**
         * @brief The limits Build() will enforce; DefaultCnbReadLimits() unless SetLimits() says
         *        otherwise.
         *
         * @return The current writer limits.
         */
        [[nodiscard]] const CnbReadLimits& Limits() const noexcept;

        /**
         * @brief Assembles the finished `.cnb` image.
         *
         * Every file this returns is loadable by CnbDocument::Parse(): the external-reference
         * names, the chunk identifiers and the custom-type rule are all checked here or at the
         * call that supplied them, so the writer has no path to a file its own reader refuses
         * (plans/plan_cnb.md `CNBF-115`).
         *
         * That guarantee is bounded by SetLimits() as well as by the format: a document whose
         * chunks are individually legal but whose aggregate logical size, chunk count or finished
         * length exceeds the configured reader limits is refused here rather than at load time
         * (plans/plan_cnb.md `CNBF-122`).
         *
         * @return The complete file bytes.
         * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the result would
         *         exceed what the format can express or what Limits() allows, if an external
         *         reference is not a valid relative logical name, or if the asset type is custom
         *         and no matching canonical type name was set (see SetMetadata()).
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

        /// Codec applied to every chunk this writer emits, and the effort level for it.
        /// CnbCompression::None unless SetCompression() says otherwise.
        CnbCompression compression_ = CnbCompression::None;
        int compressionLevel_ = 3;

        /// The reader limits Build() must stay inside. A copy rather than a reference, because a
        /// caller's own CnbReadLimits need not outlive the writer.
        CnbReadLimits limits_ = DefaultCnbReadLimits();

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
